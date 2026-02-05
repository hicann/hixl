/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "data_transfer/d2d_data_transfer_job.h"
#include "utils/extern_math_util.h"
#include "llm_datadist/llm_error_codes.h"
#include "common/def_types.h"
#include "common/llm_checker.h"
#include "cache_mgr/cache_manager.h"
#include "statistic_manager.h"
#include "data_transfer/data_transfer_utils.h"
#include "acl/acl.h"
#include "common/llm_scope_guard.h"

namespace llm {
namespace {
constexpr uint32_t kMaxTaskNum = 1024U;
constexpr uint64_t kMaxBatchPutNum = 64U;

ge::Status GetSendTask(const CacheEntry &cache_entry, const TransferCacheReq &request, const uint64_t block_size,
                       const uint64_t offset, std::list<HcclOneSideOpDesc> &send_tasks) {
  const uint64_t remainder = cache_entry.tensor_size % block_size;
  const uint64_t num = cache_entry.tensor_size / block_size;
  const uint64_t batch_or_block_num = remainder == 0U ? num : num + 1;
  uint32_t dst_addr_count = request.dst_addr_count;
  const uint32_t buffer_info_count = request.buffer_info_count;
  const uint64_t src_layer_range_cache_num = static_cast<uint64_t>(request.src_tensor_indices_size);
  const size_t src_addr_num =
      (request.src_tensor_indices_size == 0U) ? cache_entry.cache_addrs.size() : src_layer_range_cache_num;
  LLM_CHK_BOOL_RET_STATUS(src_addr_num == static_cast<size_t>(dst_addr_count), ge::LLM_PARAM_INVALID,
                         "src addr count:%zu, not match received dst_addr_count:%u", src_addr_num, dst_addr_count);
  for (size_t i = 0UL; i < src_addr_num; ++i) {
    for (uint32_t j = 0U; j < buffer_info_count; ++j) {
      HcclOneSideOpDesc hccl_one_side_op_desc;
      const auto src_buffer_info = request.transfer_infos[dst_addr_count + j].buffer_info;
      const uint64_t src_block_index = src_buffer_info.block_start_index;
      const auto dst_buffer_info = request.transfer_infos[dst_addr_count + buffer_info_count + j].buffer_info;
      const uint64_t dst_block_index = dst_buffer_info.block_start_index;
      LLM_CHK_BOOL_RET_STATUS(src_block_index < batch_or_block_num, ge::LLM_PARAM_INVALID,
                             "req_id[%lu], model_id[%lu], src block index[%lu] is out of range[0, %lu)",
                             request.req_id, request.model_id, src_block_index, batch_or_block_num);
      LLM_CHK_BOOL_RET_STATUS(src_buffer_info.buffer_len == dst_buffer_info.buffer_len, ge::LLM_PARAM_INVALID,
                             "req_id[%lu], model_id[%lu], src buffer_len[%lu] not equal dst buffer_len[%lu]",
                             request.req_id, request.model_id, src_buffer_info.buffer_len, dst_buffer_info.buffer_len);
      // offet is used batch index cachekey, if send congiguous to contiguous, block_index is 0
      const size_t src_addr_index = i + static_cast<uint64_t>(request.src_tensor_start_index);
      hccl_one_side_op_desc.localAddr = ValueToPtr(PtrToValue(cache_entry.cache_addrs[src_addr_index].get()) +
                                                   offset + src_block_index * block_size);
      // if dst_addr is contiguous, offset is done in pull
      hccl_one_side_op_desc.remoteAddr =
          ValueToPtr(PtrToValue(request.transfer_infos[i].dst_addr) + dst_block_index * block_size);
      const uint64_t target_size = request.is_pull_block == 1U ? cache_entry.tensor_size : cache_entry.stride;
      LLM_CHK_BOOL_RET_STATUS(src_buffer_info.buffer_len <= target_size, ge::LLM_PARAM_INVALID,
                             "req_id[%lu], model_id[%lu], tensor size (%lu) < required size (%lu)", request.req_id,
                             request.model_id, target_size, src_buffer_info.buffer_len);
      hccl_one_side_op_desc.count =
          (j == buffer_info_count - 1) && (remainder > 0) ? remainder : src_buffer_info.buffer_len;
      hccl_one_side_op_desc.dataType = HCCL_DATA_TYPE_INT8;
      send_tasks.push_back(std::move(hccl_one_side_op_desc));
    }
  }
  size_t total_num = 0UL;
  LLM_ASSERT_TRUE(!ge::MulOverflow(src_addr_num, static_cast<size_t>(buffer_info_count), total_num));
  LLM_CHK_BOOL_RET_STATUS(send_tasks.size() == total_num, ge::LLM_PARAM_INVALID,
                         "send task size:%zu, not match total num:%zu", send_tasks.size(), total_num);
  return ge::SUCCESS;
}
}  // namespace

ge::Status D2DDataTransferJob::Initialize(const CacheEntry &cache_entry, CommEntity &comm_entity, uint64_t offset) {
  comm_entity_ = &comm_entity;
  LLM_CHK_STATUS_RET(GenerateSendTask(cache_entry, offset), "comm_entity:%s generate send task failed",
                    comm_entity.GetDesc().c_str());
  timeout_point_ = std::chrono::steady_clock::now();
  return ge::SUCCESS;
}

ge::Status D2DDataTransferJob::Process(bool &is_done) {
  aclrtEventRecordedStatus status = ACL_EVENT_RECORDED_STATUS_NOT_READY;
  while (status != ACL_EVENT_RECORDED_STATUS_COMPLETE) {
    if (std::chrono::steady_clock::now() > comm_entity_->GetTimeoutPoint()) {
      LLM_CHK_STATUS_RET(comm_entity_->SendResponse(ge::LLM_TIMEOUT));
      LLMLOGE(ge::LLM_TIMEOUT, "handle request timeout");
      return ge::LLM_TIMEOUT;
    }
    LLM_CHK_STATUS_RET(DataTransferUtils::QueryEventStatus(event_, status), "comm_entity:%s query event status failed",
                      comm_entity_->GetDesc().c_str());
    LLMLOGI("QueryEventStatus ret=%d", status);
  }
  if (event_ != nullptr) {
    LLM_ASSERT_RT_OK(aclrtDestroyEvent(event_));
    event_ = nullptr;
  }
  if (send_tasks_.empty()) {
    LLM_CHK_STATUS_RET(comm_entity_->SendResponse(ge::SUCCESS));
    is_done = true;
    const auto finished_time_point = std::chrono::steady_clock::now();
    const auto cost = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(finished_time_point - timeout_point_).count());
    auto &send_statistic_info = comm_entity_->GetSendStatisticInfo();
    StatisticManager::GetInstance().UpdateCost(cost, send_statistic_info.send_times, send_statistic_info.send_min_cost,
                                               send_statistic_info.send_max_cost, send_statistic_info.send_total_cost);
    LLMLOGI("comm_entity:%s send all task of request finished", comm_entity_->GetDesc().c_str());
    return ge::SUCCESS;
  }
  LLM_CHK_STATUS_RET(DataTransferUtils::SendCache(comm_entity_->GetStream(), *comm_entity_, send_tasks_, event_),
                    "comm_entity:%s send cache failed", comm_entity_->GetDesc().c_str());
  return ge::SUCCESS;
}

ge::Status D2DDataTransferJob::GenerateCacheTask(const CacheEntry &cache_entry, const TransferCacheReq &request,
                                                 const uint64_t offset) {
  if (request.is_pull_block == 1U) {
    LLM_CHK_BOOL_RET_STATUS(request.block_size != 0U, ge::LLM_PARAM_INVALID,
                           "req:%lu, model_id:%lu, block size(%lu) is invalid", request.req_id, request.model_id,
                           request.block_size);
  }
  uint64_t stride_or_block_size = (request.block_size != 0U) ? request.block_size : cache_entry.stride;
  const auto ret = GetSendTask(cache_entry, request, stride_or_block_size, offset, send_tasks_);
  LLM_CHK_STATUS_RET(ret, "comm_entity:%s get send task failed", comm_entity_->GetDesc().c_str());
  return ge::SUCCESS;
}

ge::Status D2DDataTransferJob::GenerateSendTask(const CacheEntry &cache_entry, const uint64_t offset) {
  // 解析本地地址上发送过来的cache info
  const TransferCacheReq &request = comm_entity_->GetRequest();
  return GenerateCacheTask(cache_entry, request, offset);
}

ge::Status D2DDataTransferJob::PullCache() {
  LLM_CHK_STATUS_RET(comm_entity_->BatchTransfer(send_tasks_, false, true, comm_entity_->GetRequest().timeout_in_ms),
                     "Failed to batch get, task size:%zu.", send_tasks_.size());
  return ge::SUCCESS;
}
}  // namespace llm
