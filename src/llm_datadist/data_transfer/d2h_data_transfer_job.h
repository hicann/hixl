/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_GRAPH_ENGINE_RUNTIME_LLM_ENGINE_V2_DATA_TRANSFER_D2H_DATA_TRANSFER_JOB_H_
#define CANN_GRAPH_ENGINE_RUNTIME_LLM_ENGINE_V2_DATA_TRANSFER_D2H_DATA_TRANSFER_JOB_H_

#include "llm_datadist/llm_error_codes.h"
#include "data_transfer/data_transfer_job.h"
#include "utils/sync_flag.h"

namespace llm {
struct TransferBlockSpan {
  uint64_t buffer_block_start;
  uint64_t tensor_offset;
  uint32_t tensor_index;
  uint32_t size;
};

struct TransferBlocksTask {
  int32_t task_type;  // 0: start, 1: transfer, 2: end
  uint32_t buffer_index;
  TransferBlockSpan block_span;
};

class DataTransferTaskGenerator {
 public:
  DataTransferTaskGenerator(uint32_t num_tensors, uint32_t num_buffers, uint32_t buffer_size)
      : num_tensors_(num_tensors), num_buffers_(num_buffers), buffer_size_(buffer_size) {
  }

  // for continous
  std::vector<TransferBlocksTask> GenerateTasks(int64_t tensor_size,
                                                uint32_t block_size);

  std::vector<TransferBlocksTask> GenerateTasks(uint32_t block_size,
                                                uint32_t num_block_indices,
                                                const uint64_t *block_indices,
                                                const uint64_t *remote_block_indices = nullptr);

 private:
  std::vector<TransferBlocksTask> DoGenerate(uint32_t block_size,
                                             uint32_t tail_block_size,
                                             uint32_t num_block_indices,
                                             const uint64_t *block_indices);
  std::vector<TransferBlocksTask> DoGenerateForClientBlocks(uint32_t block_size, uint32_t tail_block_size,
                                                            uint32_t num_block_indices, const uint64_t *block_indices,
                                                            const uint64_t *remote_block_indices);
  std::vector<TransferBlocksTask> DoGenerateForLargeBlock(uint32_t block_size,
                                                          uint32_t num_block_indices,
                                                          const uint64_t *block_indices) const;
  void GetNextBufBlockNum(uint32_t buffer_task_index, uint32_t &remote_buffer_block_num);

  uint32_t num_tensors_;
  uint32_t num_buffers_;
  uint32_t buffer_size_;
  uint32_t max_block_size_ = 4 * 1024 * 1024;
  std::vector<uint32_t> buffer_block_nums_;
};

class D2HDataTransferJob : public DataTransferJob {
 public:
  ~D2HDataTransferJob() override;
  ge::Status Initialize(const CacheEntry &cache_entry, CommEntity &comm_entity, uint64_t offset) override;
  ge::Status Process(bool &is_done) override;

 private:
  ge::Status ResolveBlockSize(const TransferCacheReq &request, const CacheEntry &cache_entry);
  ge::Status GenerateTasks(const TransferCacheReq &req, const CacheEntry &cache_entry);
  static void PrintTasks(const std::vector<TransferBlocksTask> &tasks);

  size_t current_index_ = 0;
  uint32_t block_size_ = 0;
  int64_t tensor_size_ = 0;
  std::vector<TransferBlocksTask> tasks_;
  std::set<uint32_t> used_buffer_indices_;
  std::vector<uint8_t *> data_addresses_;
  std::vector<uint8_t *> dst_buffers_;
  aclrtEvent event_ = nullptr;
  aclrtStream stream_ = nullptr;
  std::vector<aclrtEvent> sync_buffer_events_;
  BufferedSender buffered_sender_;
  std::vector<uint8_t *> dst_receive_flag_addresses_;
  std::vector<SyncFlag> receive_flags_;
  uint8_t *send_sync_flag_ = nullptr;
};

class D2HDataTransferClient {
 public:
  explicit D2HDataTransferClient(CommEntity &comm_entity, aclrtStream stream);
  ~D2HDataTransferClient();

  ge::Status PullCache(const CacheEntry &cache_entry,
                       const CacheKey &cache_key,
                       const PullCacheParam &pull_cache_param,
                       int32_t timeout_in_ms = 1000);
 private:
  ge::Status Prepare(const CacheEntry &cache_entry, const CacheKey &cache_key, const PullCacheParam &pull_cache_param);
  ge::Status GenerateTasks(const CacheEntry &cache_entry,
                           const PullCacheParam &pull_cache_param,
                           const ResponseInfo &response);
  ge::Status SendRequest(const CacheEntry &cache_entry,
                         const CacheKey &cache_key,
                         const PullCacheParam &pull_cache_param) const;
  ge::Status RunTasks();
  void FillRequest(const CacheEntry &cache_entry,
                   const CacheKey &cache_key,
                   const PullCacheParam &pull_cache_param,
                   TransferCacheReq &request,
                   uint64_t &size) const;
  ge::Status CopyAsync(const TransferBlocksTask &task);

  CommEntity *comm_entity_ = nullptr;
  aclrtStream stream_;
  std::vector<uint8_t *> buffers_;
  std::vector<uint8_t *> tensor_addresses_;
  std::vector<SyncFlag> recv_flags_;
  std::vector<uint8_t *> remote_receive_flag_addresses_;
  // device addr
  uint8_t *send_dev_flag_ = nullptr;
  uint32_t buffer_size_ = 0U;
  uint32_t num_buffers_ = 2U;
  uint32_t block_size_ = 0U;
  int64_t timeout_in_ms_ = 1000;
  std::chrono::steady_clock::time_point timeout_tp_;
  BufferedSender buffered_sender_;
  std::vector<TransferBlocksTask> tasks_;
};
}  // namespace llm
#endif  // CANN_GRAPH_ENGINE_RUNTIME_LLM_ENGINE_V2_DATA_TRANSFER_D2H_DATA_TRANSFER_JOB_H_
