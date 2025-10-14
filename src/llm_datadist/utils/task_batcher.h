/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_GRAPH_ENGINE_RUNTIME_LLM_ENGINE_V2_UTILS_TASK_BATCHER_H_
#define CANN_GRAPH_ENGINE_RUNTIME_LLM_ENGINE_V2_UTILS_TASK_BATCHER_H_

#include "common/common.h"

namespace llm {

struct BufferSlice {
  uint32_t buffer_offset;
  uint32_t data_index;
  uint64_t data_offset;
  uint32_t data_size;
};

class TaskBatcher {
 public:
  TaskBatcher() = default;
  explicit TaskBatcher(uint32_t buffer_size) : buffer_size_(buffer_size) {}
  void Initialize(uint32_t num_tensors,
                  uint32_t block_size,
                  size_t num_transfer_infos,
                  const TransferInfo *transfer_infos);

  std::vector<BufferSlice> NextBatch(uint32_t max_transfer_info_num = UINT32_MAX);
  uint32_t GetTransferInfoNum() const;

 private:
  void GetOffsetAndLength(uint32_t remaining_buffer_len, uint64_t &data_offset, uint64_t &data_size);
  void UpdateIndices();

  uint32_t buffer_size_ = 0U;
  uint32_t num_tensors_ = 0U;
  uint32_t block_size_ = 0U;
  uint32_t max_block_size_ = 4 * 1024 * 1024; // 4MB
  uint32_t current_tensor_index_ = 0U;
  uint32_t current_transfer_info_index_ = 0U;
  uint32_t num_transfer_infos_ = 0U;
  uint64_t remaining_data_len_ = 0U;
  uint64_t remaining_data_offset_ = 0U;
  uint32_t transfer_info_num_ = 0;
  const TransferInfo *transfer_infos_ = nullptr;
};
}  // namespace llm

#endif  // CANN_GRAPH_ENGINE_RUNTIME_LLM_ENGINE_V2_UTILS_TASK_BATCHER_H_
