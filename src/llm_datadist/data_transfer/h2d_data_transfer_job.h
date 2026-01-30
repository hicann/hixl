/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#ifndef CANN_GRAPH_ENGINE_RUNTIME_LLM_ENGINE_V2_DATA_TRANSFER_H2D_DATA_TRANSFER_JOB_H_
#define CANN_GRAPH_ENGINE_RUNTIME_LLM_ENGINE_V2_DATA_TRANSFER_H2D_DATA_TRANSFER_JOB_H_

#include <future>
#include "data_transfer/data_transfer_job.h"
#include "acl/acl.h"
#include "link_mgr/comm_entity.h"
#include "utils/task_batcher.h"
#include "common/llm_thread_pool.h"
#include "hccl/hccl_adapter.h"

namespace llm {
struct BufferContext {
  size_t buffer_index;
  std::shared_ptr<void> buffer;
  uint32_t buffer_size;
  uint64_t offset;
  TaskBatcher *src_task_batcher;
  TaskBatcher *dst_task_batcher;
  std::vector<BufferSlice> buffer_slices;
  std::vector<BufferSlice> dst_buffer_slices;
  int32_t state;
  // shared
  LLMThreadPool *thread_pool;
  std::vector<std::future<ge::Status>> copy_futures;
  aclrtEvent event;
  aclrtStream stream;
  aclrtContext aclrt_context;
  CommEntity *comm_entity;
  std::vector<std::shared_ptr<void>> data_addresses;
  const TransferCacheReq *request;
  std::chrono::steady_clock::time_point batch_copy_start;
};

class H2DDataTransferJob : public DataTransferJob {
 public:
  ge::Status Initialize(const CacheEntry &cache_entry, CommEntity &comm_entity, uint64_t offset) override;
  ge::Status Process(bool &is_done) override;

 private:
  void InitBufferContexts(const CacheEntry &cache_entry, const TransferCacheReq &request, CommEntity &comm_entity,
                          uint64_t offset);
  CommEntity *comm_entity_ = nullptr;
  std::vector<BufferContext> buffers_;
  size_t buffer_size_ = 32UL * 1024 * 1024; // 32MB
  LLMThreadPool thread_pool_{"ge_llm_h2d", 8};
  TaskBatcher src_task_generator_;
  TaskBatcher dst_task_generator_;
};
}  // namespace llm

#endif  // CANN_GRAPH_ENGINE_RUNTIME_LLM_ENGINE_V2_DATA_TRANSFER_H2D_DATA_TRANSFER_JOB_H_
