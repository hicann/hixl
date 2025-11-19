/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "data_transfer/data_transfer_client.h"
#include "data_transfer/d2d_data_transfer_job.h"
#include "common/llm_utils.h"
#include "common/llm_scope_guard.h"

namespace llm {
namespace {
constexpr size_t kSrcAndDstNum = 2U;
constexpr uint64_t kDefaultReqBufferSize = 112U * 1024U;
constexpr uint32_t kFlagSize = 8U;

ge::Status SetBufferInfoCount(const PullCacheParam &pull_cache_param, uint32_t &buffer_info_count,
                              uint32_t &is_pull_block,
                              std::vector<std::vector<std::pair<int64_t, int64_t>>> &contiguous_blocks_pair) {
  LLM_CHK_BOOL_RET_STATUS(pull_cache_param.prompt_blocks.empty() || !pull_cache_param.decoder_blocks.empty(),
                         ge::LLM_PARAM_INVALID, "currently not support pull from discrete to continuous");
  if (pull_cache_param.decoder_blocks.empty()) {
    buffer_info_count = 1U;
    LLMLOGD("enter pull from contiguous to contiguous");
  } else if (pull_cache_param.prompt_blocks.empty()) {
    for (size_t i = 0UL; i < pull_cache_param.decoder_blocks.size(); ++i) {
      std::vector<std::pair<int64_t, int64_t>> block_pair{std::make_pair(i, pull_cache_param.decoder_blocks[i])};
      contiguous_blocks_pair.emplace_back(std::move(block_pair));
      buffer_info_count = contiguous_blocks_pair.size();
    }
    LLMLOGD("enter pull from contiguous to discrete");
  } else {
    is_pull_block = 1;
    LLM_CHK_STATUS_RET(LLMUtils::FindContiguousBlockIndexPair(pull_cache_param.prompt_blocks,
                                                             pull_cache_param.decoder_blocks, contiguous_blocks_pair),
                      "find contiguous blocks failed");
    buffer_info_count = contiguous_blocks_pair.size();
    LLMLOGD("enter pull from discrete to discrete");
  }

  LLMLOGI("request set buffer info count is:%u", buffer_info_count);
  return ge::SUCCESS;
}

void SetDstAddr(const PullCacheParam &pull_cache_param, const CacheEntry &cache_entry, TransferCacheReq &request) {
  request.block_size = 0U;
  uint64_t layer_start_tensor_index = 0U;
  if (!pull_cache_param.dst_tensor_indices.empty()) {
    layer_start_tensor_index = pull_cache_param.dst_tensor_indices.front();
  }
  for (size_t i = 0UL; i < request.dst_addr_count; ++i) {
    if (pull_cache_param.decoder_blocks.empty()) {
      request.transfer_infos[i].dst_addr =
          ValueToPtr(PtrToValue(cache_entry.cache_addrs[i + layer_start_tensor_index].get()) +
                         pull_cache_param.batch_index * cache_entry.stride);
      LLMLOGI("request set %uth dst_addr:%lu", i, reinterpret_cast<uintptr_t>(request.transfer_infos[i].dst_addr));
    } else {
      request.block_size = cache_entry.stride;
      request.transfer_infos[i].dst_addr = cache_entry.cache_addrs[i + layer_start_tensor_index].get();
      LLMLOGI("request set block size is:%lu, set %uth dst_addr:%lu", cache_entry.stride, i,
             reinterpret_cast<uintptr_t>(request.transfer_infos[i].dst_addr));
    }
  }
}

ge::Status SetBufferInfo(const PullCacheParam &pull_cache_param, const CacheEntry &cache_entry,
                         const std::vector<std::vector<std::pair<int64_t, int64_t>>> &contiguous_blocks_pair,
                         TransferCacheReq &request) {
  const uint32_t dst_addr_count = request.dst_addr_count;
  const uint64_t pull_size = request.pull_size;
  for (uint32_t i = 0U; i < request.buffer_info_count; ++i) {
    auto &src_buffer_info = request.transfer_infos[dst_addr_count + i].buffer_info;
    auto &dst_buffer_info = request.transfer_infos[dst_addr_count + request.buffer_info_count + i].buffer_info;
    if (pull_cache_param.decoder_blocks.empty()) {
      dst_buffer_info.buffer_len = pull_size;
      dst_buffer_info.block_start_index = 0U;
      src_buffer_info.buffer_len = pull_size;
      src_buffer_info.block_start_index = 0U;
      LLMLOGI("request buffer_len is:%lu, block_start_index is:%lu",
              dst_buffer_info.buffer_len, dst_buffer_info.block_start_index);
      break;
    }
    const auto &contiguous_block_pair = contiguous_blocks_pair[i];
    const int64_t dst_index = contiguous_block_pair.front().second;
    const int64_t dst_end_index = contiguous_block_pair.back().second;
    LLM_CHK_BOOL_RET_STATUS((static_cast<uint64_t>(dst_index) < cache_entry.num_blocks) &&
                               (static_cast<uint64_t>(dst_end_index) < cache_entry.num_blocks),
                           ge::LLM_PARAM_INVALID,
                           "dst block begin index[%ld] or dst block end index[%ld] is out of range[0, %lu)",
                           dst_index, dst_end_index, cache_entry.num_blocks);
    src_buffer_info.buffer_len = contiguous_block_pair.size() * cache_entry.stride;
    src_buffer_info.block_start_index = contiguous_blocks_pair[i].front().first;
    dst_buffer_info.buffer_len = contiguous_block_pair.size() * cache_entry.stride;
    dst_buffer_info.block_start_index = contiguous_blocks_pair[i].front().second;
    LLMLOGI("request buffer_len is:%lu, src block start index is:%lu, dst block start index is:%lu",
            src_buffer_info.buffer_len, src_buffer_info.block_start_index, dst_buffer_info.block_start_index);
  }
  return ge::SUCCESS;
}

}  // namespace

ge::Status DataTransferClient::ConstructTransferInfo(const PullCacheParam &pull_cache_param,
                                                     const CacheEntry &cache_entry, const CacheKey &cache_key,
                                                     int32_t timeout, TransferCacheReq &request) const {
  std::vector<std::vector<std::pair<int64_t, int64_t>>> contiguous_blocks_pair;
  uint32_t buffer_info_count = 0U;
  uint32_t is_pull_block = 0U;
  LLM_CHK_STATUS_RET(SetBufferInfoCount(pull_cache_param, buffer_info_count, is_pull_block, contiguous_blocks_pair),
                    "set buffer_info_count failed");
  uint64_t request_size =
      sizeof(TransferCacheReq) + sizeof(TransferInfo) * (static_cast<uint64_t>(buffer_info_count) * kSrcAndDstNum +
                                                         cache_entry.cache_addrs.size());
  LLM_CHK_BOOL_RET_STATUS(request_size <= (kDefaultReqBufferSize - kFlagSize), ge::LLM_PARAM_INVALID,
                         "buffer info count[%u] is to large, request size[%lu] is out of range[0, %lu]",
                         buffer_info_count, request_size, (kDefaultReqBufferSize - kFlagSize));

  request.cache_id = cache_key.prompt_cache_id;
  request.batch_index = cache_key.prompt_batch_index;
  request.req_id = cache_key.req_id;
  request.prefix_id = cache_key.prefix_id;
  request.model_id = cache_key.model_id;
  request.dst_addr_count = pull_cache_param.dst_tensor_indices.empty()
                               ? static_cast<uint32_t>(cache_entry.cache_addrs.size())
                               : static_cast<uint32_t>(pull_cache_param.dst_tensor_indices.size());
  request.buffer_info_count = buffer_info_count;
  request.is_pull_block = is_pull_block;
  request.dst_placement = 1;
  request.timeout_in_ms = timeout;
  request.num_tensors = request.dst_addr_count;
  request.pull_size = pull_cache_param.size > 0 ? static_cast<uint64_t>(pull_cache_param.size) : cache_entry.stride;
  request.max_block_index =
      pull_cache_param.prompt_blocks.empty()
          ? 0UL
          : *std::max_element(pull_cache_param.prompt_blocks.cbegin(), pull_cache_param.prompt_blocks.cend());
  request.src_tensor_indices_size = pull_cache_param.src_tensor_indices.size();
  request.src_tensor_start_index = 0U;
  if (!pull_cache_param.src_tensor_indices.empty()) {
    request.src_tensor_start_index = pull_cache_param.src_tensor_indices.front();
  }
  SetDstAddr(pull_cache_param, cache_entry, request);
  LLM_CHK_STATUS_RET(SetBufferInfo(pull_cache_param, cache_entry, contiguous_blocks_pair, request),
                    "Failed to set buffer info");
  return ge::SUCCESS;
}

ge::Status DataTransferClient::GetResponseInfo() const {
  void *resp_addr = comm_entity_->GetEntityInfo().local_resp_ptr;
  const auto &response_info = PtrToPtr<void, ResponseInfo>(resp_addr);
  LLM_ASSERT_NOTNULL(response_info);
  // 校验response里面的数据
  auto ret = static_cast<ge::Status>(response_info->ret_code);
  LLM_CHK_BOOL_RET_STATUS(ret == ge::SUCCESS, ret, "pull cache failed, not find kv in remote cluster[%lu]",
                         comm_entity_->GetClusterId());
  LLMLOGI("entity:%s success to receive cache", comm_entity_->GetDesc().c_str());
  return ge::SUCCESS;
}

ge::Status DataTransferClient::SendCacheInfoToRemote() const {
  const auto &request = *PtrToPtr<uint8_t, TransferCacheReq>(comm_entity_->GetEntityInfo().send_buffer_req_ptr);
  uint64_t request_size = sizeof(TransferCacheReq) +
                          sizeof(TransferInfo) * (static_cast<uint64_t>(request.buffer_info_count) * kSrcAndDstNum +
                                                  request.dst_addr_count);
  LLMLOGI("transfer_cache_req size:%lu", request_size);
  auto fill_req_func = [request_size](TransferCacheReq &request, uint64_t &size) -> void {
    // request already filled, just set size
    (void)request;
    size = request_size;
  };
  LLM_CHK_STATUS_RET(comm_entity_->SendRequest(fill_req_func, req_stream_),
                    "put cache info to remote_cluster[%lu] failed", comm_entity_->GetClusterId());
  return ge::SUCCESS;
}

ge::Status DataTransferClient::SynchronizeStreamTask(const TimePoint &start_time) const {
  LLM_CHK_ACL_RET(rtStreamSynchronizeWithTimeout(req_stream_, timeout_in_ms_));
  void *local_sync_flag_addr = comm_entity_->GetEntityInfo().local_resp_flag_ptr;
  volatile int8_t *volatile local_sync_flag = PtrToPtr<void, int8_t>(local_sync_flag_addr);

  while (true) {
    const int8_t flag = *local_sync_flag;
    if (flag == 1) {
      auto &recv_statistic_info = comm_entity_->GetRecvStatisticInfo();
      recv_statistic_info.sync_flag_get_times++;
      LLMLOGI("entity:%s get sync flag success:%u", comm_entity_->GetDesc().c_str(), flag);
      break;
    }
    const auto &current_time = std::chrono::steady_clock::now();
    const uint64_t duration = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - start_time).count();
    LLM_CHK_BOOL_RET_STATUS(duration <= static_cast<uint64_t>(timeout_in_ms_), ge::LLM_TIMEOUT,
                           "entity:%s synchronize timeout, time cost:%lu ms.", comm_entity_->GetDesc().c_str(),
                           duration);
  }
  comm_entity_->ClearResponseFlags();
  LLMLOGI("entity:%s synchronize success", comm_entity_->GetDesc().c_str());
  return ge::SUCCESS;
}

ge::Status DataTransferClient::PullCacheFromRemote(const TimePoint &start_time) const {
  LLM_CHK_STATUS_RET(SendCacheInfoToRemote(), "send cache info to remote cluster:%lu failed",
                    comm_entity_->GetClusterId());
  LLM_CHK_STATUS_RET(SynchronizeStreamTask(start_time), "pull cache timeout");
  LLM_CHK_STATUS_RET(GetResponseInfo(), "get response failed from remote cluster:%lu", comm_entity_->GetClusterId());
  return ge::SUCCESS;
}

ge::Status DataTransferClient::PullCache(const CacheEntry &cache_entry, const CacheKey &cache_key,
                                         const PullCacheParam &pull_cache_param, int32_t timeout_in_ms) {
  const auto start = std::chrono::steady_clock::now();
  timeout_in_ms_ = timeout_in_ms;
  auto &request = *PtrToPtr<uint8_t, TransferCacheReq>(comm_entity_->GetEntityInfo().send_buffer_req_ptr);
  LLM_CHK_STATUS_RET(ConstructTransferInfo(pull_cache_param, cache_entry, cache_key, timeout_in_ms, request));
  LLM_CHK_STATUS_RET(PullCacheFromRemote(start), "Failed to pull kv from remote cluster:%lu",
                    cache_key.prompt_cluster_id);
  return ge::SUCCESS;
}

ge::Status DataTransferClient::PullCacheByGet(const CacheEntry &cache_entry, const CacheKey &cache_key,
                                              const PullCacheParam &pull_cache_param, int32_t timeout_in_ms) const {
  auto &request = *PtrToPtr<void, TransferCacheReq>(comm_entity_->GetEntityInfo().local_req_ptr);
  LLM_CHK_STATUS_RET(ConstructTransferInfo(pull_cache_param, cache_entry, cache_key, timeout_in_ms, request));
  LLM_DISMISSABLE_GUARD(stream, [this]() -> void {
    LLM_CHK_ACL(rtStreamAbort(comm_entity_->GetStream()));
  });
  CacheEntry remote_cache_entry;
  LLM_CHK_STATUS_RET(comm_entity_->GetCacheAccessTable().FindCacheEntry(request, remote_cache_entry));
  LLM_CHK_BOOL_RET_STATUS(cache_entry.remote_accessible, ge::LLM_PARAM_INVALID,
                         "local cache is not remote accessible.");
  LLM_CHK_BOOL_RET_STATUS(remote_cache_entry.remote_accessible, ge::LLM_PARAM_INVALID,
                         "remote cache is not remote accessible.");
  D2DDataTransferJob job;
  uint64_t offset = 0;
  if (cache_entry.cache_mem_type != CacheMemType::BLOCKS) {
    offset += cache_key.prompt_batch_index * cache_entry.stride;
  }
  LLMLOGI("pull_cache begin from the offset: %u.", offset);
  LLM_CHK_STATUS_RET(job.Initialize(remote_cache_entry, *comm_entity_, offset));
  LLM_CHK_STATUS_RET(job.PullCache(), "Failed to pull cache");
  LLM_DISMISS_GUARD(stream);
  return ge::SUCCESS;
}
}  // namespace llm
