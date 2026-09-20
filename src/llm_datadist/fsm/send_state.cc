/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "send_state.h"

#include "common/llm_log.h"
#include "common/mem_utils.h"
#include "data_transfer/d2h_data_transfer_job.h"
#include "data_transfer/h2d_data_transfer_job.h"
#include "data_transfer/d2d_data_transfer_job.h"
#include "common/llm_checker.h"
#include "common/transfer_message_limits.h"
#include "common/transfer_request_validation.h"

namespace llm {
namespace {
constexpr int32_t kTransferTypeD2D = 0;
constexpr int32_t kTransferTypeD2H = 1;
constexpr int32_t kTransferTypeH2D = 2;
constexpr int32_t kDefaultTimeoutInMs = 1800 * 1000;  // 1800s

ge::Status ValidateBufferInfoLens(const TransferCacheReq &request, uint32_t buffer_info_multiplier) {
  using namespace transfer_message_limits;
  if (request.buffer_info_count == 0U) {
    return ge::SUCCESS;
  }
  for (uint32_t i = 0U; i < request.buffer_info_count; ++i) {
    const auto &src_buffer_info = request.transfer_infos[request.dst_addr_count + i].buffer_info;
    LLM_CHK_BOOL_RET_STATUS(src_buffer_info.buffer_len > 0U, ge::LLM_PARAM_INVALID,
                            "src buffer_info[%u].buffer_len is 0", i);
    if (buffer_info_multiplier > kBufferInfoMultiplierD2h) {
      const auto &dst_buffer_info =
          request.transfer_infos[request.dst_addr_count + request.buffer_info_count + i].buffer_info;
      LLM_CHK_BOOL_RET_STATUS(dst_buffer_info.buffer_len > 0U, ge::LLM_PARAM_INVALID,
                              "dst buffer_info[%u].buffer_len is 0", i);
    }
  }
  return ge::SUCCESS;
}

ge::Status ValidateTransferRequest(const TransferCacheReq &request, int32_t transfer_type) {
  using namespace transfer_message_limits;
  LLM_CHK_BOOL_RET_STATUS(request.dst_addr_count > 0U, ge::LLM_PARAM_INVALID, "dst_addr_count is 0");
  LLM_CHK_BOOL_RET_STATUS(request.dst_addr_count <= kMaxDstAddrCount, ge::LLM_PARAM_INVALID,
                          "dst_addr_count:%u exceeds max:%u", request.dst_addr_count, kMaxDstAddrCount);

  const uint32_t buffer_info_multiplier =
      (transfer_type == kTransferTypeD2H) ? kBufferInfoMultiplierD2h : kBufferInfoMultiplierD2dH2d;
  const uint64_t transfer_info_count = static_cast<uint64_t>(request.dst_addr_count) +
                                       static_cast<uint64_t>(request.buffer_info_count) * buffer_info_multiplier;
  LLM_CHK_BOOL_RET_STATUS(transfer_info_count <= kMaxTransferInfoCount, ge::LLM_PARAM_INVALID,
                          "transfer info count:%lu exceeds max:%lu, dst_addr_count:%u, buffer_info_count:%u",
                          transfer_info_count, kMaxTransferInfoCount, request.dst_addr_count,
                          request.buffer_info_count);

  // req_size is intentionally NOT compared for equality here, to stay compatible with legacy peers (older D2D/H2D
  // clients do not populate req_size, so a strict check would reject otherwise-valid cross-version requests). Memory
  // safety does not rely on req_size: the dst_addr_count / transfer_info_count bounds above keep every transfer_infos[]
  // access inside the fixed request buffer, and the server-side D2H recv-flag base is derived from these validated
  // counts (see D2HDataTransferJob::Initialize) rather than the wire req_size, so a forged req_size cannot redirect
  // remote writes.
  LLM_CHK_STATUS_RET(ValidateBufferInfoLens(request, buffer_info_multiplier), "Failed to validate buffer info lens");
  return ge::SUCCESS;
}

uint64_t ResolveSrcNumTensors(const CacheEntry &cache_entry, const TransferCacheReq &request) {
  return (request.src_tensor_indices_size == 0U) ? static_cast<uint64_t>(cache_entry.cache_addrs.size())
                                                 : static_cast<uint64_t>(request.src_tensor_indices_size);
}

// src_tensor_indices_size 为 0 时各消费方都按"整段 cache"处理；为了与它们一致，
// 这里把 start_index 一律**视作 0**（对端给的非 0 值被忽略，而不是拒绝），
// 否则它会被叠加到下标上，把访问推到 cache 之外。
uint64_t ResolveSrcStartIndex(const TransferCacheReq &request) {
  return (request.src_tensor_indices_size == 0U) ? 0U : static_cast<uint64_t>(request.src_tensor_start_index);
}

// 对端给的 src 张量区间必须整体落在本端 cache 的张量数组内。
ge::Status ValidateSrcTensorRange(const CacheEntry &cache_entry, const TransferCacheReq &request) {
  using namespace transfer_request_validation;
  const uint64_t src_start_index = ResolveSrcStartIndex(request);
  const uint64_t src_num_tensors = ResolveSrcNumTensors(cache_entry, request);
  LLM_CHK_BOOL_RET_STATUS(
      CheckSrcTensorRange(src_start_index, src_num_tensors, cache_entry.cache_addrs.size()) == ge::SUCCESS,
      ge::LLM_PARAM_INVALID, "src tensor range out of range, start:%lu, num:%lu, src_cache num:%zu", src_start_index,
      src_num_tensors, cache_entry.cache_addrs.size());
  return ge::SUCCESS;
}

// 对端给的 buffer_info.block_start_index 会被用来换算本端读写偏移；H2D 与 D2H(block) 在这里校验。
// D2D 的同一个下标要经过 remainder 修正才能算出真正下发的长度，因此在 GetSendTask 里按实际 count 校验。
ge::Status ValidateBlockSpans(const CacheEntry &cache_entry, const TransferCacheReq &request, int32_t transfer_type) {
  using namespace transfer_request_validation;
  const bool is_d2h = (transfer_type == kTransferTypeD2H);
  const bool needs_block_span_check = (transfer_type == kTransferTypeH2D) || (is_d2h && (cache_entry.num_blocks > 0U));
  if (!needs_block_span_check) {
    return ge::SUCCESS;
  }
  // is_block_cache：D2H 的 block 对 block，或 H2D 的 block host cache。块大小就是 stride，
  // 单个张量实际分配 tensor_size 字节；H2D 的 cont cache 实际用的 block_size =
  // min(kDefaultBlockSize, pull_size) <= pull_size，这里用 pull_size 作上界偏保守但同样能保证偏移落在张量内。
  const bool is_block_cache = cache_entry.num_blocks > 0U;
  const uint64_t block_size = is_block_cache ? cache_entry.stride : request.pull_size;
  const uint64_t region_size = is_block_cache ? cache_entry.tensor_size : cache_entry.stride;
  for (uint32_t i = 0U; i < request.buffer_info_count; ++i) {
    const auto &buffer_info = request.transfer_infos[request.dst_addr_count + i].buffer_info;
    // D2H 的 block 路径由 DataTransferTaskGenerator 按 block_indices 逐块下发，每块固定 stride 字节
    // （cur_block_size == block_size），报文里的 buffer_len 不参与下发，因此校验长度取 stride，
    // 不再依赖"tensor_size 能被 stride 整除"这条注册侧的不变量。H2D 由 TaskBatcher 按报文里的
    // buffer_len 取数，长度仍用 buffer_len 校验。
    const uint64_t span_len = is_d2h ? cache_entry.stride : buffer_info.buffer_len;
    LLM_CHK_BOOL_RET_STATUS(
        CheckBlockSpanWithinRegion(buffer_info.block_start_index, block_size, span_len, region_size) == ge::SUCCESS,
        ge::LLM_PARAM_INVALID,
        "src buffer_info[%u] out of the local tensor region, block_start_index:%lu, block_size:%lu, span_len:%lu, "
        "region_size:%lu, local block_num:%lu",
        i, buffer_info.block_start_index, block_size, span_len, region_size, cache_entry.num_blocks);
  }
  return ge::SUCCESS;
}

// D2H 服务端本地范围：dst_buffer_size 的取值域，以及响应区之后 recv flag 区的容量。
ge::Status ValidateD2hLocalBounds(const TransferCacheReq &request) {
  using namespace transfer_request_validation;
  LLM_CHK_BOOL_RET_STATUS(CheckD2hDstBufferSize(request.dst_buffer_size) == ge::SUCCESS, ge::LLM_PARAM_INVALID,
                          "dst_buffer_size:%lu is out of range [%lu, %u]", request.dst_buffer_size, kMinDstBufferSize,
                          UINT32_MAX);
  const uint64_t response_size = transfer_message_limits::CalcResponseSize(request.dst_addr_count);
  LLM_CHK_BOOL_RET_STATUS(
      CheckRecvFlagArea(request.dst_addr_count, response_size, transfer_message_limits::kMaxResponsePayloadSize,
                        sizeof(int32_t)) == ge::SUCCESS,
      ge::LLM_PARAM_INVALID, "dst_addr_count:%u is too large for the response buffer, response size:%lu, max:%lu",
      request.dst_addr_count, response_size, transfer_message_limits::kMaxResponsePayloadSize);
  return ge::SUCCESS;
}

// 对端报文里的下标/长度字段必须与本端 cache 的实际范围对齐，否则后续 job 会越界读本端内存
// 或把 buffer_info 的字节当成地址使用。这里集中做这一层交叉校验。
ge::Status ValidateLocalIndexBounds(const CacheEntry &cache_entry, const TransferCacheReq &request,
                                    int32_t transfer_type) {
  using namespace transfer_request_validation;
  LLM_CHK_STATUS_RET(ValidateSrcTensorRange(cache_entry, request), "Failed to validate src tensor range");

  if (transfer_type == kTransferTypeH2D) {
    // H2D 的传输任务用张量下标索引 dst_addr 段，张量数不能超过该段容量。
    const uint64_t src_num_tensors = ResolveSrcNumTensors(cache_entry, request);
    LLM_CHK_BOOL_RET_STATUS(CheckTensorCountWithinDstAddr(src_num_tensors, request.dst_addr_count) == ge::SUCCESS,
                            ge::LLM_PARAM_INVALID,
                            "num_tensors:%lu exceeds dst_addr_count:%u, the dst_addr section would be overrun",
                            src_num_tensors, request.dst_addr_count);
  }

  LLM_CHK_STATUS_RET(ValidateBlockSpans(cache_entry, request, transfer_type), "Failed to validate block spans");

  if (transfer_type == kTransferTypeD2D) {
    // block_size 参与 D2D 的整除/取模；request.block_size 为 0 时会回落到本端 stride，
    // 两者同时为 0 就会除零，所以在建 job 之前先挡掉。
    LLM_CHK_BOOL_RET_STATUS(
        CheckD2dBlockSize(request.block_size, cache_entry.stride) == ge::SUCCESS, ge::LLM_PARAM_INVALID,
        "d2d block size is 0, request.block_size:%lu, local cache stride:%lu", request.block_size, cache_entry.stride);
  }

  if (transfer_type == kTransferTypeD2H) {
    LLM_CHK_STATUS_RET(ValidateD2hLocalBounds(request), "Failed to validate d2h local bounds");
  }
  return ge::SUCCESS;
}
}  // namespace
ge::Status SendState::Preprocess(CommEntity &entity) {
  auto ret = Prepare(entity);
  if (ret != ge::SUCCESS) {
    LLM_CHK_STATUS_RET(entity.SendResponse(ret));
    return Postprocess(entity);
  }
  return Process(entity);
}

ge::Status SendState::Prepare(CommEntity &entity) {
  auto timeout_in_ms = kDefaultTimeoutInMs;
  auto &request = entity.GetRequest();
  if (request.timeout_in_ms > 0) {
    timeout_in_ms = request.timeout_in_ms;
    LLMLOGI("set timeout by request = %d(ms)", timeout_in_ms);
  }
  entity.SetTimeoutPoint(std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_in_ms));
  CacheEntry cache_entry{};
  uint64_t offset;
  LLM_CHK_STATUS_RET(QueryCacheEntryAndOffset(entity, cache_entry, offset));
  LLMLOGI("Query cache entry success, offset = %lu", offset);
  LLM_CHK_STATUS_RET(CheckParam(cache_entry, request), "Failed to check param");
  auto transfer_type = ResolveTransferType(entity.GetRequest(), cache_entry);
  LLMLOGI("transfer type = %d", transfer_type);
  LLM_CHK_BOOL_RET_STATUS(transfer_type >= 0, ge::LLM_FEATURE_NOT_ENABLED,
                          "dst_placement = %d, src_placement = %d is not supported", entity.GetRequest().dst_placement,
                          static_cast<int32_t>(cache_entry.placement));
  LLM_CHK_STATUS_RET(ValidateTransferRequest(request, transfer_type), "Failed to validate transfer request");
  LLM_CHK_STATUS_RET(ValidateLocalIndexBounds(cache_entry, request, transfer_type),
                     "Failed to validate request against local cache");
  if (transfer_type == kTransferTypeD2H) {
    entity.SetDataTransferJob(MakeUnique<D2HDataTransferJob>());
  } else if (transfer_type == kTransferTypeH2D) {
    entity.SetDataTransferJob(MakeUnique<H2DDataTransferJob>());
  } else {  // D2D
    entity.SetDataTransferJob(MakeUnique<D2DDataTransferJob>());
  }
  const auto &transfer_job = entity.GetDataTransferJob();
  LLM_CHECK_NOTNULL(transfer_job);
  LLM_CHK_STATUS_RET(transfer_job->Initialize(cache_entry, entity, offset));
  return ge::SUCCESS;
}

ge::Status SendState::Process(CommEntity &entity) {
  if (std::chrono::steady_clock::now() > entity.GetTimeoutPoint()) {
    entity.SendResponse(ge::LLM_TIMEOUT);
    LLMLOGE(ge::FAILED, "Request handling timed out");
    return Postprocess(entity);
  }
  bool is_done = false;
  const auto process_ret = entity.GetDataTransferJob()->Process(is_done);
  if (process_ret != ge::SUCCESS) {
    LLMLOGE(process_ret, "DataTransferJob::Process failed, ret = %d, release data transfer job",
            static_cast<int32_t>(process_ret));
    (void)Postprocess(entity);
    return process_ret;
  }
  if (is_done) {
    const auto &data_cache_key = entity.GetCacheKeyToRemove();
    if (data_cache_key.first != UINT64_MAX) {
      LLM_CHK_STATUS(entity.GetCacheManager()->RemoveCacheKey(data_cache_key, false,
                                                              GetLayerRangeTensorIndices(entity.GetRequest())));
    }
    return Postprocess(entity);
  }
  return ge::SUCCESS;
}

ge::Status SendState::Postprocess(CommEntity &entity) {
  entity.SetDataTransferJob(nullptr);
  return entity.ChangeState(FsmState::FSM_IDLE_STATE);
}

ge::Status SendState::QueryCacheEntryAndOffset(CommEntity &entity, CacheEntry &cache_entry, uint64_t &offset) {
  const TransferCacheReq &request = entity.GetRequest();
  auto &recv_statistic_info = entity.GetRecvStatisticInfo();
  recv_statistic_info.req_info_get_times++;
  ge::Status ret = ge::SUCCESS;
  const auto cache_manager = entity.GetCacheManager();
  LLM_CHECK_NOTNULL(cache_manager, "entity:%s get cache manager failed", entity.GetDesc().c_str());
  entity.SetCacheKeyToRemove({UINT64_MAX, UINT64_MAX});
  if (request.is_pull_block == 1U) {
    offset = 0U;
    return QueryBlocksCache(*cache_manager, request, cache_entry);
  }

  DataCacheKey data_cache_key;
  bool is_prefix = false;
  if (!GetCacheKey(*cache_manager, request, data_cache_key, is_prefix)) {
    ret = QueryCacheByCacheId(*cache_manager, request, cache_entry);
    LLM_CHK_STATUS_RET(ret, "query cache by cache id[%lu] failed", request.cache_id);
    LLM_CHK_BOOL_RET_STATUS(request.batch_index < cache_entry.batch_size, ge::LLM_KV_CACHE_NOT_EXIST,
                            "batch_index (%lu)out of range [0, %u)", request.batch_index, cache_entry.batch_size);
    offset = request.batch_index * cache_entry.stride;
    return ge::SUCCESS;
  }
  // query by cache_key
  LLM_CHK_BOOL_RET_STATUS(cache_manager->GetCacheEntry(data_cache_key, is_prefix, cache_entry),
                          ge::LLM_KV_CACHE_NOT_EXIST,
                          "Failed to get cache entry by data_cache_key: (%lu, %lu), is_prefix = %d",
                          data_cache_key.first, data_cache_key.second, static_cast<int32_t>(is_prefix));
  offset = cache_entry.id_to_batch_index_and_size.at(data_cache_key.first).first * cache_entry.stride;
  if ((!is_prefix) && (cache_entry.is_owned) && (request.is_pull_block == 0U)) {
    LLMLOGI("CacheKey(%lu, %lu) need to be removed after pulling", data_cache_key.first, data_cache_key.second);
    entity.SetCacheKeyToRemove(data_cache_key);
  }
  return ge::SUCCESS;
}

ge::Status SendState::QueryBlocksCache(const CacheManager &cache_manager, const TransferCacheReq &request,
                                       CacheEntry &cache_entry) {
  std::pair<uint64_t, uint64_t> cache_key = std::make_pair(request.req_id, request.model_id);
  LLM_CHK_BOOL_RET_STATUS(cache_manager.GetCacheEntry(cache_key, false, cache_entry), ge::LLM_KV_CACHE_NOT_EXIST,
                          "cache_id:%ld, req:%lu, model_id:%lu, cache not found", request.cache_id, request.req_id,
                          request.model_id);
  LLM_CHK_BOOL_RET_STATUS(request.block_size != 0U, ge::LLM_PARAM_INVALID,
                          "req:%lu, model_id:%lu, block size(%lu) is invalid", request.req_id, request.model_id,
                          request.block_size);
  return ge::SUCCESS;
}

bool SendState::GetCacheKey(const CacheManager &cache_manager, const TransferCacheReq &request,
                            std::pair<uint64_t, uint64_t> &cache_key, bool &is_prefix) {
  bool found = true;
  if (request.cache_id >= 0) {
    const auto search_key = std::make_pair(request.cache_id, request.batch_index);
    found = cache_manager.GetCacheKey(search_key, cache_key);
    if (found) {
      LLMLOGI("cache_id:%lu, batch_index:%lu maps to CacheKey(req_id:%lu, model_id:%lu)", search_key.first,
              search_key.second, cache_key.first, cache_key.second);
    } else {
      LLMLOGI("cache_id:%lu, batch_index:%lu maps to No CacheKey", search_key.first, search_key.second);
    }
  } else {
    is_prefix = request.prefix_id != UINT64_MAX;
    auto real_req_id = is_prefix ? request.prefix_id : request.req_id;
    cache_key = std::make_pair(real_req_id, request.model_id);
  }
  return found;
}

int32_t SendState::ResolveTransferType(const TransferCacheReq &request, const CacheEntry &cache_entry) {
  auto dst_placement = static_cast<CachePlacement>(request.dst_placement);
  auto src_placement = cache_entry.placement;
  if (src_placement == CachePlacement::DEVICE && dst_placement == CachePlacement::DEVICE) {
    return kTransferTypeD2D;
  }
  if (src_placement == CachePlacement::DEVICE && dst_placement == CachePlacement::HOST) {
    return kTransferTypeD2H;
  }
  if (src_placement == CachePlacement::HOST && dst_placement == CachePlacement::DEVICE) {
    return kTransferTypeH2D;
  }
  return -1;
}

ge::Status SendState::QueryCacheByCacheId(const CacheManager &cache_manager, const TransferCacheReq &request,
                                          CacheEntry &cache_entry) {
  LLM_CHK_BOOL_RET_STATUS(cache_manager.GetCacheEntry(request.cache_id, cache_entry), ge::LLM_KV_CACHE_NOT_EXIST,
                          "cache_id:%ld, cache not found", request.cache_id);
  LLM_CHK_BOOL_RET_STATUS(request.batch_index < static_cast<uint64_t>(cache_entry.batch_size),
                          ge::LLM_KV_CACHE_NOT_EXIST, "cache id:%ld, batch_index (%lu) >= batch_size (%u)",
                          request.cache_id, request.batch_index, cache_entry.batch_size);
  return ge::SUCCESS;
}

ge::Status SendState::CheckParam(const CacheEntry &cache_entry, const TransferCacheReq &request) {
  size_t cache_num = (request.src_tensor_indices_size != 0U) ? static_cast<size_t>(request.src_tensor_indices_size)
                                                             : cache_entry.cache_addrs.size();
  LLM_CHK_BOOL_RET_STATUS(cache_num == request.num_tensors, ge::LLM_PARAM_INVALID,
                          "num_tensors mismatches, src = %zu, dst = %u", cache_num, request.num_tensors);
  LLM_CHK_BOOL_RET_STATUS((request.is_pull_block == 0U) == (cache_entry.num_blocks == 0), ge::LLM_PARAM_INVALID,
                          "request pull block = %u, but local cache is block = %d", request.is_pull_block,
                          (cache_entry.num_blocks == 0) ? 0 : 1);
  if (request.is_pull_block == 1U) {
    // local is PA
    LLM_CHK_BOOL_RET_STATUS((request.max_block_index == 0) || (request.max_block_index < cache_entry.num_blocks),
                            ge::LLM_PARAM_INVALID,
                            "request max_block_index out of bound, requested = %lu, local block_num = %lu",
                            request.max_block_index, cache_entry.num_blocks);
  } else {
    // local is Non-PA
    if (request.block_size > 0U) {
      if (request.dst_placement == static_cast<int32_t>(CachePlacement::HOST)) {
        // is d2h c2b
        auto padded_size = (cache_entry.stride + request.block_size - 1U) / request.block_size * request.block_size;
        LLM_CHK_BOOL_RET_STATUS(request.pull_size <= padded_size, ge::LLM_PARAM_INVALID,
                                "pull_size(%lu) > padded_cache_stride(%lu), block_size = %lu, cache_stride = %lu",
                                request.pull_size, padded_size, request.block_size, cache_entry.stride);
      }
    } else {
      LLM_CHK_BOOL_RET_STATUS(request.pull_size <= cache_entry.stride, ge::LLM_PARAM_INVALID,
                              "pull_size(%lu) > cache stride(%lu)", request.pull_size, cache_entry.stride);
    }
  }
  if (request.src_tensor_indices_size != 0U) {
    LLM_CHK_BOOL_RET_STATUS(
        (cache_num <= cache_entry.cache_addrs.size()) &&
            (static_cast<size_t>(request.src_tensor_start_index) < cache_entry.cache_addrs.size()) &&
            (static_cast<size_t>(request.src_tensor_start_index + request.src_tensor_indices_size - 1) <
             cache_entry.cache_addrs.size()),
        ge::LLM_PARAM_INVALID,
        "src_tensor_indices_size[%u] or src_tensor_start_index[%u] is invalid, src_cache num is[%zu]",
        request.src_tensor_indices_size, request.src_tensor_start_index, cache_entry.cache_addrs.size());
  } else {
    LLM_CHK_BOOL_RET_STATUS(static_cast<size_t>(request.src_tensor_start_index) < cache_entry.cache_addrs.size(),
                            ge::LLM_PARAM_INVALID,
                            "src_tensor_start_index[%u] is out of range, cache_addrs size is[%zu]",
                            request.src_tensor_start_index, cache_entry.cache_addrs.size());
  }
  return ge::SUCCESS;
}

std::unordered_set<uint64_t> SendState::GetLayerRangeTensorIndices(const TransferCacheReq &request) {
  if (request.src_tensor_indices_size == 0U) {
    return {};
  }
  std::unordered_set<uint64_t> tensor_indices;
  const size_t layer_start_tensor_index = static_cast<size_t>(request.src_tensor_start_index);
  const size_t layer_range_num_tensors = static_cast<size_t>(request.src_tensor_indices_size);
  for (uint64_t i = layer_start_tensor_index; i < layer_start_tensor_index + layer_range_num_tensors; ++i) {
    tensor_indices.insert(i);
  }
  return tensor_indices;
}
}  // namespace llm
