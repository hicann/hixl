/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "data_transfer/data_transfer_utils.h"

namespace llm {
namespace {
constexpr uint32_t kMaxTaskNum = 1024U;
constexpr uint64_t kMaxBatchPutNum = 64U;
}  // namespace

ge::Status DataTransferUtils::SendBatchCache(const aclrtStream stream, const std::vector<HcclOneSideOpDesc> &desces,
                                             CommEntity &comm_entity) {
  size_t start = 0U;
  while (start < desces.size()) {
    size_t end = std::min(start + kMaxBatchPutNum, desces.size());
    std::vector<HcclOneSideOpDesc> batch(desces.begin() + start, desces.begin() + end);
    LLM_CHK_STATUS_RET(comm_entity.BatchPutAsync(batch, stream),
                      "put cache data to remote cluster[%lu] failed, data num:%zu", comm_entity.GetClusterId(),
                      batch.size());
    LLMLOGI("comm entity:%s success batch cache, cache size:%zu", comm_entity.GetDesc().c_str(), batch.size());
    start = end;
  }
  return ge::SUCCESS;
}

ge::Status DataTransferUtils::SendCache(const aclrtStream stream, CommEntity &comm_entity,
                                        std::list<HcclOneSideOpDesc> &transfer_tasks, aclrtEvent &event) {
  auto &send_statistic_info = comm_entity.GetSendStatisticInfo(stream);
  std::vector<HcclOneSideOpDesc> desces;
  while (desces.size() < kMaxTaskNum) {
    if (transfer_tasks.empty()) {
      break;
    }
    HcclOneSideOpDesc send_task = transfer_tasks.front();
    transfer_tasks.pop_front();
    desces.emplace_back(send_task);
  }
  LLMLOGI("comm entity:%s begin send cache, cache size:%zu, send_task size:%zu", comm_entity.GetDesc().c_str(),
         desces.size(), transfer_tasks.size());
  LLM_CHK_STATUS_RET(SendBatchCache(stream, desces, comm_entity),
                    "comm_entity:%s put batch cache data to remote cluster[%lu] failed, data num:%zu",
                    comm_entity.GetDesc().c_str(), comm_entity.GetClusterId(), desces.size());

  if ((transfer_tasks.empty() || desces.size() == kMaxTaskNum) && event == nullptr) {
    LLMLOGI("transfer tasks[%zu] is empty or task num[%zu] reach 1024, create event and record", transfer_tasks.size(),
           desces.size());
    const auto start = std::chrono::steady_clock::now();
    LLM_CHK_ACL_RET(aclrtCreateEvent(&event));
    LLM_ASSERT_RT_OK(aclrtRecordEvent(event, stream));
    const auto end = std::chrono::steady_clock::now();
    send_statistic_info.event_record_times++;
    const uint64_t cost = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    send_statistic_info.event_record_total_cost += cost;
  }
  return ge::SUCCESS;
}

ge::Status DataTransferUtils::QueryEventStatus(const aclrtEvent &event, aclrtEventRecordedStatus &status) {
  if (event != nullptr) {
    LLM_ASSERT_RT_OK(aclrtQueryEventStatus(event, &status));
    return ge::SUCCESS;
  }
  status = ACL_EVENT_RECORDED_STATUS_COMPLETE;
  return ge::SUCCESS;
}
}  // namespace llm
