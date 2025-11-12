/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "llm_datadist/llm_error_codes.h"
#include "common/llm_utils.h"
#include "data_transfer/data_transfer_utils.h"
#include "data_transfer/layer_wise_transfer_job.h"
#include "common/llm_scope_guard.h"

namespace llm {
namespace {
constexpr uint32_t kMaxTaskNum = 1024U;
constexpr uint64_t kMaxBatchPutNum = 64U;
constexpr uint64_t kBlocksCacheKey = 1UL;
constexpr uint64_t kCacheKeyByIdType = 2UL;
}  // namespace
LayerWiseTransferJob::LayerWiseTransferJob(CommEntity &comm_entity, rtStream_t stream)
    : stream_(stream), comm_entity_(&comm_entity) {}

ge::Status LayerWiseTransferJob::GenerateCacheToCacheTask(const CacheEntry &cache_entry,
                                                          const std::vector<std::shared_ptr<void>> &src_layer_addrs,
                                                          const TransferCacheConfig &transfer_cache_config) {
  const uint64_t batch_index = transfer_cache_config.batch_index;
  LLM_CHK_BOOL_RET_STATUS(batch_index < static_cast<uint64_t>(cache_entry.batch_size), ge::LLM_PARAM_INVALID,
                         "batch index[%lu] is out of range[0, %u)", batch_index, cache_entry.batch_size);
  const uint64_t offset = batch_index * cache_entry.stride;
  for (size_t i = 0UL; i < src_layer_addrs.size(); ++i) {
    HcclOneSideOpDesc layer_desc;
    layer_desc.localAddr = ValueToPtr(PtrToValue(src_layer_addrs[i].get()) + offset);
    layer_desc.remoteAddr = ValueToPtr(transfer_cache_config.dst_addrs[i]);
    layer_desc.count = cache_entry.stride;
    layer_desc.dataType = HCCL_DATA_TYPE_INT8;
    layer_transfer_tasks_.push_back(layer_desc);
  }
  return ge::SUCCESS;
}

ge::Status LayerWiseTransferJob::GenerateCacheToBlocksTask(const CacheEntry &cache_entry,
                                                           const std::vector<std::shared_ptr<void>> &src_layer_addrs,
                                                           const TransferCacheConfig &transfer_cache_config,
                                                           const TransferBlockConfig &transfer_block_config) {
  LLM_CHK_BOOL_RET_STATUS(
      (transfer_block_config.block_mem_size > 0U) && (transfer_block_config.block_mem_size < cache_entry.tensor_size),
      ge::LLM_PARAM_INVALID,
      "block_mem_size[%lu] must > 0 and < tensor size[%lu] when transfer from continuous cache to blocks",
      transfer_block_config.block_mem_size, cache_entry.tensor_size);
  const uint64_t batch_index = transfer_cache_config.batch_index;
  LLM_CHK_BOOL_RET_STATUS(batch_index < static_cast<uint64_t>(cache_entry.batch_size), ge::LLM_PARAM_INVALID,
                         "batch index[%lu] is out of range[0, %u)", batch_index, cache_entry.batch_size);
  const uint64_t offset = batch_index * cache_entry.stride;
  const uint64_t remainder = cache_entry.tensor_size % transfer_block_config.block_mem_size;
  const uint64_t num = cache_entry.tensor_size / transfer_block_config.block_mem_size;
  const uint64_t block_num = (remainder == 0U) ? num : num + 1;
  const size_t dst_blocks_num = transfer_block_config.dst_blocks.size();
  LLM_CHK_BOOL_RET_STATUS(block_num >= dst_blocks_num, ge::LLM_PARAM_INVALID,
                         "The number[%lu] after splitting contiguous memory by size[%lu] < the dst_block num[%zu].",
                         block_num, transfer_block_config.block_mem_size, dst_blocks_num);
  LLMLOGI("The number after splitting contiguous memory by size[%lu] is:%lu, dst_block num[%zu].",
         transfer_block_config.block_mem_size, block_num, dst_blocks_num);
  for (size_t i = 0UL; i < src_layer_addrs.size(); ++i) {
    const auto src_layer_addr = src_layer_addrs[i];
    for (size_t j = 0UL; j < dst_blocks_num; ++j) {
      HcclOneSideOpDesc layer_desc;
      layer_desc.localAddr =
          ValueToPtr(PtrToValue(src_layer_addr.get()) + offset + j * transfer_block_config.block_mem_size);
      layer_desc.remoteAddr =
          ValueToPtr(transfer_cache_config.dst_addrs[i] +
                         transfer_block_config.dst_blocks[j] * transfer_block_config.block_mem_size);
      layer_desc.count =
          ((j == dst_blocks_num - 1) && (remainder > 0U)) ? remainder : transfer_block_config.block_mem_size;
      layer_desc.dataType = HCCL_DATA_TYPE_INT8;
      layer_transfer_tasks_.push_back(layer_desc);
    }
  }
  return ge::SUCCESS;
}

ge::Status LayerWiseTransferJob::GenerateBlocksToBlocksTask(const CacheEntry &cache_entry,
                                                            const std::vector<std::shared_ptr<void>> &src_layer_addrs,
                                                            const TransferCacheConfig &transfer_cache_config,
                                                            const TransferBlockConfig &transfer_block_config) {
  LLM_CHK_BOOL_RET_STATUS(cache_entry.num_blocks > 0, ge::LLM_PARAM_INVALID,
                         "check failed, request expect local cache is blocks");
  std::vector<std::vector<std::pair<int64_t, int64_t>>> contiguous_blocks_pair;
  LLM_CHK_STATUS_RET(LLMUtils::FindContiguousBlockIndexPair(transfer_block_config.src_blocks,
                                                           transfer_block_config.dst_blocks, contiguous_blocks_pair));
  if (transfer_block_config.block_mem_size > 0U) {
    LLM_CHK_BOOL_RET_STATUS(cache_entry.stride == transfer_block_config.block_mem_size, ge::LLM_PARAM_INVALID,
                           "block_mem_size[%lu] is not match cache stride:%lu", transfer_block_config.block_mem_size,
                           cache_entry.stride);
  }
  for (size_t i = 0UL; i < src_layer_addrs.size(); ++i) {
    const auto src_layer_addr = src_layer_addrs[i];
    const auto dst_layer_addr = transfer_cache_config.dst_addrs[i];
    for (const auto &blocks_pair : contiguous_blocks_pair) {
      LLM_CHK_BOOL_RET_STATUS((static_cast<uint64_t>(blocks_pair.front().first) < cache_entry.num_blocks) &&
                                 (static_cast<uint64_t>(blocks_pair.back().first) < cache_entry.num_blocks),
                             ge::LLM_PARAM_INVALID, "src block index[%ld] or [%ld] is out of range [0, %lu)",
                             blocks_pair.front().first, blocks_pair.back().first, cache_entry.num_blocks);
      HcclOneSideOpDesc layer_desc;
      layer_desc.localAddr =
          ValueToPtr(PtrToValue(src_layer_addr.get()) + blocks_pair.front().first * cache_entry.stride);
      layer_desc.remoteAddr = ValueToPtr(dst_layer_addr + blocks_pair.front().second * cache_entry.stride);
      layer_desc.count = cache_entry.stride * blocks_pair.size();
      LLMLOGI("transfer from src block index[%lu] to dst block index[%lu], block size:%lu", blocks_pair.front().first,
             blocks_pair.front().second, layer_desc.count);
      layer_desc.dataType = HCCL_DATA_TYPE_INT8;
      layer_transfer_tasks_.push_back(layer_desc);
    }
  }
  return ge::SUCCESS;
}

ge::Status LayerWiseTransferJob::Prepare(const CacheEntry &cache_entry,
                                         const TransferCacheConfig &transfer_cache_config,
                                         const TransferBlockConfig &transfer_block_config) {
  const auto &src_cache_addrs = cache_entry.cache_addrs;
  const uint64_t layer_index = transfer_cache_config.layer_index;
  const uint64_t layer_num = src_cache_addrs.size() / transfer_cache_config.tensor_num_per_layer;
  LLM_CHK_BOOL_RET_STATUS(layer_index < layer_num, ge::LLM_PARAM_INVALID, "layer index[%lu] is out of range[0, %lu)",
                         layer_index, layer_num);
  const size_t remainder = src_cache_addrs.size() % transfer_cache_config.tensor_num_per_layer;
  LLM_CHK_BOOL_RET_STATUS(remainder == 0U, ge::LLM_PARAM_INVALID,
                         "When using layer wise transfer, the tensor_num [%zu] of caches must be a multiple of tensor_num_per_layer[%lu].",
                         src_cache_addrs.size(), transfer_cache_config.tensor_num_per_layer);
  const std::vector<std::shared_ptr<void>> src_layer_addrs(
      src_cache_addrs.begin() + layer_index * transfer_cache_config.tensor_num_per_layer,
      src_cache_addrs.begin() + layer_index * transfer_cache_config.tensor_num_per_layer +
      transfer_cache_config.tensor_num_per_layer);
  LLM_CHK_BOOL_RET_STATUS(src_layer_addrs.size() == transfer_cache_config.dst_addrs.size(), ge::LLM_PARAM_INVALID,
                         "src_layer_addrs size[%zu] is not match dst_addrs size[%zu]", src_layer_addrs.size(),
                         transfer_cache_config.dst_addrs.size());
  if (transfer_block_config.dst_blocks.empty()) {
    LLM_CHK_STATUS_RET(GenerateCacheToCacheTask(cache_entry, src_layer_addrs, transfer_cache_config));
  } else if (transfer_block_config.src_blocks.empty()) {
    LLM_CHK_STATUS_RET(
        GenerateCacheToBlocksTask(cache_entry, src_layer_addrs, transfer_cache_config, transfer_block_config));
  } else {
    LLM_CHK_STATUS_RET(
        GenerateBlocksToBlocksTask(cache_entry, src_layer_addrs, transfer_cache_config, transfer_block_config));
  }
  return ge::SUCCESS;
}

ge::Status LayerWiseTransferJob::SynchronizeTransferCacheWithRecord(const int32_t timeout_in_ms) {
  const auto start = std::chrono::steady_clock::now();
  while (!layer_transfer_tasks_.empty()) {
    LLM_CHK_STATUS_RET(DataTransferUtils::SendCache(stream_, *comm_entity_, layer_transfer_tasks_, event_),
                      "comm_entity:%s send cache failed", comm_entity_->GetDesc().c_str());
    rtEventStatus_t status = RT_EVENT_INIT;
    while (status != RT_EVENT_RECORDED) {
      if (std::chrono::steady_clock::now() > start + std::chrono::milliseconds(timeout_in_ms)) {
        LLMLOGE(ge::LLM_TIMEOUT, "stream handle transfer request timeout");
        return ge::LLM_TIMEOUT;
      }
      LLM_CHK_STATUS_RET(DataTransferUtils::QueryEventStatus(event_, status), "comm_entity:%s query event status failed",
                        comm_entity_->GetDesc().c_str());
      LLMLOGI("query event status ret=%d", status);
    }
    if (event_ != nullptr) {
      LLM_ASSERT_RT_OK(rtEventDestroy(event_));
      event_ = nullptr;
    }
  }
  LLM_CHK_ACL_RET(rtStreamSynchronizeWithTimeout(stream_, timeout_in_ms));

  const auto finished_time_point = std::chrono::steady_clock::now();
  const auto cost =
      static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(finished_time_point - start).count());
  auto &send_statistic_info = comm_entity_->GetSendStatisticInfo(stream_);
  StatisticManager::GetInstance().UpdateCost(cost, send_statistic_info.send_times, send_statistic_info.send_min_cost,
                                             send_statistic_info.send_max_cost, send_statistic_info.send_total_cost);
  LLMLOGI("comm_entity:%s send all task of request finished", comm_entity_->GetDesc().c_str());
  return ge::SUCCESS;
}

ge::Status LayerWiseTransferJob::SynchronizeTransferCache(const int32_t timeout_in_ms) {
  const auto start = std::chrono::steady_clock::now();
  LLM_CHK_STATUS_RET(DataTransferUtils::SendCache(stream_, *comm_entity_, layer_transfer_tasks_),
                    "comm_entity:%s send cache failed", comm_entity_->GetDesc().c_str());
  LLM_CHK_ACL_RET(rtStreamSynchronizeWithTimeout(stream_, timeout_in_ms));
  const auto finished_time_point = std::chrono::steady_clock::now();
  const auto cost =
      static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(finished_time_point - start).count());
  auto &send_statistic_info = comm_entity_->GetSendStatisticInfo(stream_);
  StatisticManager::GetInstance().UpdateCost(cost, send_statistic_info.send_times, send_statistic_info.send_min_cost,
                                             send_statistic_info.send_max_cost, send_statistic_info.send_total_cost);
  LLMLOGI("comm_entity:%s send all task of request finished", comm_entity_->GetDesc().c_str());
  return ge::SUCCESS;
}

ge::Status LayerWiseTransferJob::FillRemoteLayerAddrs(int32_t timeout_in_ms,
                                                      TransferCacheConfig &transfer_config,
                                                      const TransferBlockConfig &transfer_block_config) {
  TransferCacheReq request{};
  request.timeout_in_ms = timeout_in_ms;
  if (transfer_config.type == kBlocksCacheKey) {
    request.req_id = UINT64_MAX;
    request.model_id = transfer_config.model_id_or_cache_id;
  } else if (transfer_config.type == kCacheKeyByIdType) {
    request.cache_id = static_cast<int64_t>(transfer_config.model_id_or_cache_id);
  } else {
  }
  CacheEntry remote_cache_entry;
  LLM_CHK_STATUS_RET(comm_entity_->GetCacheAccessTable().FindCacheEntry(request, remote_cache_entry));
  LLM_CHK_STATUS_RET(ValidateRemoteCache(remote_cache_entry, transfer_config, transfer_block_config),
                    "Validate remote cache failed.");
  auto begin_index = remote_cache_entry.cache_addrs.begin() +
      transfer_config.dst_layer_index * transfer_config.tensor_num_per_layer;
  const std::vector<std::shared_ptr<void>> dst_layer_addrs(begin_index, begin_index +
      transfer_config.tensor_num_per_layer);
  for (auto &cache_addr : dst_layer_addrs) {
    transfer_config.dst_addrs.emplace_back(
        reinterpret_cast<uintptr_t>(cache_addr.get()) + transfer_config.dst_batch_index * remote_cache_entry.stride);
  }
  LLMLOGI("Transfer type:%lu, model_or_cache_id:%lu, dst_layer_index:%lu.", transfer_config.type,
         transfer_config.model_id_or_cache_id, transfer_config.dst_layer_index);
  return ge::SUCCESS;
}

ge::Status LayerWiseTransferJob::ValidateRemoteCache(const CacheEntry &remote_cache_entry,
                                                     const TransferCacheConfig &transfer_cache_config,
                                                     const TransferBlockConfig &transfer_block_config) const {
  LLM_CHK_BOOL_RET_STATUS(remote_cache_entry.remote_accessible, ge::LLM_PARAM_INVALID,
                         "remote cache is not remote accessible.");
  // validate layer_index
  const uint64_t layer_num = remote_cache_entry.cache_addrs.size() / transfer_cache_config.tensor_num_per_layer;
  LLM_CHK_BOOL_RET_STATUS(transfer_cache_config.dst_layer_index < layer_num, ge::LLM_PARAM_INVALID,
                         "layer index[%lu] is out of range[0, %lu)", transfer_cache_config.dst_layer_index, layer_num);
  if (transfer_block_config.dst_blocks.empty()) {
    // C2C
    const uint64_t batch_index = transfer_cache_config.dst_batch_index;
    LLM_CHK_BOOL_RET_STATUS(batch_index < static_cast<uint64_t>(remote_cache_entry.batch_size), ge::LLM_PARAM_INVALID,
                           "batch index[%lu] is out of range[0, %u) of remote cache.", batch_index,
                           remote_cache_entry.batch_size);
  } else {
    auto max_ele = std::max_element(transfer_block_config.dst_blocks.cbegin(), transfer_block_config.dst_blocks.cend());
    LLM_CHK_BOOL_RET_STATUS((*max_ele < remote_cache_entry.num_blocks),
                           ge::LLM_PARAM_INVALID, "dst block index[%ld] is out of range [0, %lu)",
                           *max_ele, remote_cache_entry.num_blocks);
    LLM_CHK_BOOL_RET_STATUS((transfer_block_config.block_mem_size == 0) ||
                           (transfer_block_config.block_mem_size == remote_cache_entry.stride), ge::LLM_PARAM_INVALID,
                           "dst_block_memory_size[%lu] is not equal to stride[%lu] of remote cache.",
                           transfer_block_config.block_mem_size, remote_cache_entry.stride);
  }
  return ge::SUCCESS;
}

ge::Status LayerWiseTransferJob::TransferCache(const CacheEntry &cache_entry,
                                               const TransferCacheConfig &transfer_cache_config,
                                               const TransferBlockConfig &transfer_block_config,
                                               int32_t timeout_in_ms,
                                               bool access_remote_cache) {
  TransferCacheConfig transfer_config = transfer_cache_config;
  LLM_DISMISSABLE_GUARD(stream, [this]() -> void {
    LLM_CHK_ACL(rtStreamAbort(comm_entity_->GetStream()));
  });
  if (access_remote_cache) {
    LLM_CHK_BOOL_RET_STATUS(cache_entry.remote_accessible, ge::LLM_PARAM_INVALID,
                           "local cache is not remote accessible.");
    LLM_CHK_STATUS_RET(FillRemoteLayerAddrs(timeout_in_ms, transfer_config, transfer_block_config), "Fill remote addrs failed.");
  }
  LLM_CHK_STATUS_RET(Prepare(cache_entry, transfer_config, transfer_block_config), "prepare transfer task failed");
  if (access_remote_cache) {
    LLM_CHK_STATUS_RET(SynchronizeTransferCache(timeout_in_ms), "transfer cache failed");
  } else {
    LLM_CHK_STATUS_RET(SynchronizeTransferCacheWithRecord(timeout_in_ms), "transfer cache with record failed");
  }
  LLM_DISMISS_GUARD(stream);
  return ge::SUCCESS;
}

}  // namespace llm
