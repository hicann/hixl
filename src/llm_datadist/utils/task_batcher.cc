/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "utils/task_batcher.h"

namespace llm {
namespace {
constexpr int32_t kMaxTaskNumInBatch = 64;

}  // namespace
void TaskBatcher::Initialize(uint32_t num_tensors,
                             uint32_t block_size,
                             size_t num_transfer_infos,
                             const TransferInfo *transfer_infos) {
  num_tensors_ = num_tensors;
  block_size_ = block_size;
  num_transfer_infos_ = num_transfer_infos;
  transfer_infos_ = transfer_infos;
}

std::vector<BufferSlice> TaskBatcher::NextBatch(uint32_t max_transfer_info_num) {
  std::vector<BufferSlice> ret;
  uint32_t buffer_offset = 0;
  uint32_t remaining_buffer_len = buffer_size_;
  uint64_t prev_block_end_offset = UINT64_MAX;
  uint32_t prev_tensor_index = UINT32_MAX;
  uint32_t num_tasks = 0;
  transfer_info_num_ = 0;
  while (remaining_buffer_len > 0) {
    if (current_tensor_index_ >= num_tensors_) {
      LLMLOGI("no more task");
      break;
    }
    if ((max_transfer_info_num == UINT32_MAX) && (num_tasks >= kMaxTaskNumInBatch)) {
      LLMLOGI("reached max task number in batch");
      break;
    }
    if (transfer_info_num_ >= max_transfer_info_num) {
      LLMLOGI("reached max block number:%u in batch", max_transfer_info_num);
      break;
    }
    uint64_t data_offset = 0U;
    uint64_t data_size_cur_task = 0U;
    GetOffsetAndLength(remaining_buffer_len, data_offset, data_size_cur_task);
    const auto data_size = static_cast<uint32_t>(data_size_cur_task);
    if ((current_tensor_index_ == prev_tensor_index) &&
        (data_offset == prev_block_end_offset) &&
        ((ret.back().data_size + data_size) <= max_block_size_)) {
      ret.back().data_size += data_size;
    } else {
      ret.emplace_back(BufferSlice{
          buffer_offset,
          current_tensor_index_,
          data_offset,
          static_cast<uint32_t>(data_size),
      });
      ++num_tasks;
    }

    buffer_offset += data_size;
    remaining_buffer_len -= data_size;
    prev_block_end_offset = data_offset + data_size;
    prev_tensor_index = current_tensor_index_;
    ++transfer_info_num_;
    UpdateIndices();
  }
  return ret;
}

void TaskBatcher::GetOffsetAndLength(uint32_t remaining_buffer_len, uint64_t &data_offset, uint64_t &data_size) {
  if (remaining_data_len_ > 0) {
    // 上次的没处理完, 继续处理
    data_size = remaining_data_len_;
    data_offset = remaining_data_offset_;
  } else {
    const auto &buffer_info = transfer_infos_[current_transfer_info_index_].buffer_info;
    const auto block_index = buffer_info.block_start_index;
    data_offset = block_index * block_size_;
    data_size = buffer_info.buffer_len;
  }

  auto max_data_size = std::min(remaining_buffer_len, max_block_size_);
  if (max_data_size < data_size) {
    remaining_data_len_ = data_size - max_data_size;
    remaining_data_offset_ = data_offset + max_data_size;
    data_size = max_data_size;
  } else {
    remaining_data_len_ = 0;
    remaining_data_offset_ = 0;
  }
}

void TaskBatcher::UpdateIndices() {
  if (remaining_data_len_ == 0) {
    const auto is_tail_block = current_transfer_info_index_ == (num_transfer_infos_ - 1);
    if (is_tail_block) {
      current_transfer_info_index_ = 0U;
      current_tensor_index_ += 1;
    } else {
      current_transfer_info_index_ += 1;
    }
  }
}

uint32_t TaskBatcher::GetTransferInfoNum() const {
  return transfer_info_num_;
}
}  // namespace llm