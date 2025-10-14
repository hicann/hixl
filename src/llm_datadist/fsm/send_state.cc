/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "send_state.h"

#include "common/llm_log.h"
#include "common/llm_utils.h"
#include "common/mem_utils.h"
#include "data_transfer/d2h_data_transfer_job.h"
#include "data_transfer/h2d_data_transfer_job.h"
#include "data_transfer/d2d_data_transfer_job.h"
#include "common/llm_checker.h"
#include "common/llm_scope_guard.h"

namespace llm {
namespace {
constexpr int32_t kTransferTypeD2D = 0;
constexpr int32_t kTransferTypeD2H = 1;
constexpr int32_t kTransferTypeH2D = 2;
constexpr int32_t kDefaultTimeoutInMs = 1800 * 1000;  // 1800s
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
  auto &reqeust = entity.GetRequest();
  if (reqeust.timeout_in_ms > 0) {
    timeout_in_ms = reqeust.timeout_in_ms;
    LLMLOGI("set timeout by request = %d(ms)", timeout_in_ms);
  }
  entity.SetTimeoutPoint(std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_in_ms));
  CacheEntry cache_entry{};
  uint64_t offset;
  LLM_CHK_STATUS_RET(QueryCacheEntryAndOffset(entity, cache_entry, offset));
  LLMLOGI("Query cache entry success, offset = %lu", offset);
  LLM_CHK_STATUS_RET(CheckParam(cache_entry, reqeust), "Failed to check param");
  auto transfer_type = ResolveTransferType(entity.GetRequest(), cache_entry);
  LLMLOGI("transfer type = %d", transfer_type);
  LLM_CHK_BOOL_RET_STATUS(transfer_type >= 0, ge::LLM_FEATURE_NOT_ENABLED, "dst_placement = %d, src_placement = %d is not supported",
                         entity.GetRequest().dst_placement, static_cast<int32_t>(cache_entry.placement));
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
    LLMLOGE(ge::FAILED, "handle request timeout");
    return Postprocess(entity);
  }
  bool is_done = false;
  LLM_CHK_STATUS_RET(entity.GetDataTransferJob()->Process(is_done));
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
                               request.pull_size, cache_entry.stride, request.block_size, request.block_size);
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
