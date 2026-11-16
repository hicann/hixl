/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "data_transfer/data_transfer_utils.h"

#include <algorithm>
#include <iterator>

namespace llm {
namespace {
constexpr uint32_t kMaxTaskNum = 1024U;
constexpr uint64_t kMaxBatchPutNum = 64U;
}  // namespace

ge::Status DataTransferUtils::SendBatchCache(const aclrtStream stream, std::list<HcclOneSideOpDesc> &transfer_tasks,
                                             CommEntity &comm_entity) {
  size_t submitted = 0U;
  while (submitted < kMaxTaskNum && !transfer_tasks.empty()) {
    const size_t batch_size =
        std::min(static_cast<size_t>(kMaxBatchPutNum), static_cast<size_t>(kMaxTaskNum) - submitted);
    auto batch_end = transfer_tasks.begin();
    std::advance(batch_end, std::min(batch_size, transfer_tasks.size()));
    std::vector<HcclOneSideOpDesc> batch(transfer_tasks.begin(), batch_end);
    LLM_CHK_STATUS_RET(comm_entity.BatchPutAsync(batch, stream),
                       "put cache data to remote cluster[%lu] failed, data num:%zu", comm_entity.GetClusterId(),
                       batch.size());
    LLMLOGI("comm entity:%s successfully batched cache, cache size:%zu", comm_entity.GetDesc().c_str(), batch.size());
    transfer_tasks.erase(transfer_tasks.begin(), batch_end);
    submitted += batch.size();
  }
  return ge::SUCCESS;
}

ge::Status DataTransferUtils::SendCache(const aclrtStream stream, CommEntity &comm_entity,
                                        std::list<HcclOneSideOpDesc> &transfer_tasks, aclrtEvent &event) {
  auto &send_statistic_info = comm_entity.GetSendStatisticInfo(stream);
  const size_t task_num = std::min(static_cast<size_t>(kMaxTaskNum), transfer_tasks.size());
  LLMLOGI("comm entity:%s begin send cache, cache size:%zu, send_task size:%zu", comm_entity.GetDesc().c_str(),
          task_num, transfer_tasks.size());
  LLM_CHK_STATUS_RET(SendBatchCache(stream, transfer_tasks, comm_entity),
                     "comm_entity:%s put batch cache data to remote cluster[%lu] failed, data num:%zu",
                     comm_entity.GetDesc().c_str(), comm_entity.GetClusterId(), task_num);

  if ((transfer_tasks.empty() || task_num == kMaxTaskNum) && event == nullptr) {
    LLMLOGI("transfer tasks[%zu] are empty or task num[%zu] reached %zu, create event and record",
            transfer_tasks.size(), task_num, static_cast<size_t>(kMaxTaskNum));
    const auto start = std::chrono::steady_clock::now();
    LLM_CHK_ACL_RET(aclrtCreateEvent(&event));
    const aclError record_ret = aclrtRecordEvent(event, stream);
    if (record_ret != ACL_SUCCESS) {
      (void)aclrtDestroyEvent(event);
      event = nullptr;
      const auto status = llm::ConvertAclError2Ge(static_cast<int32_t>(record_ret));
      LLMLOGE(status, "Call aclrtRecordEvent failed, ret:0x%X, stream:%p", static_cast<uint32_t>(record_ret), stream);
      return status;
    }
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
