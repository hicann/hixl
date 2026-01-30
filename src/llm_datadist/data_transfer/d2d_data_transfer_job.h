/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_GRAPH_ENGINE_RUNTIME_LLM_ENGINE_DATA_TRANSFER_D2D_DATA_TRANSFER_JOB_H_
#define CANN_GRAPH_ENGINE_RUNTIME_LLM_ENGINE_DATA_TRANSFER_D2D_DATA_TRANSFER_JOB_H_

#include <list>
#include <queue>
#include <list>
#include "common/common.h"
#include "hccl/hccl_adapter.h"
#include "link_mgr/comm_entity.h"
#include "data_transfer/data_transfer_job.h"

namespace llm {
class D2DDataTransferJob : public DataTransferJob {
 public:
  ge::Status Initialize(const CacheEntry &cache_entry, CommEntity &comm_entity, uint64_t offset) override;
  ge::Status Process(bool &is_done) override;
  ge::Status PullCache() override;

 private:
  ge::Status GenerateCacheTask(const CacheEntry &cache_entry, const TransferCacheReq &request, const uint64_t offset);
  ge::Status GenerateSendTask(const CacheEntry &cache_entry, const uint64_t offset);
  CommEntity *comm_entity_;
  std::list<HcclOneSideOpDesc> send_tasks_;
  aclrtEvent event_;
  std::chrono::steady_clock::time_point timeout_point_;
};
}  // namespace llm
#endif  // CANN_GRAPH_ENGINE_RUNTIME_LLM_ENGINE_DATA_TRANSFER_D2D_DATA_TRANSFER_JOB_H_
