/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "utils/task_batcher.h"
#include "common/llm_log.h"

namespace llm {
namespace {
constexpr int32_t kMaxTaskNumInBatch = 64;

}  // namespace
void TaskBatcher::Initialize(uint32_t num_tensors, uint32_t block_size, size_t num_transfer_infos,
                             const TransferInfo *transfer_infos) {
  num_tensors_ = num_tensors;
  block_size_ = block_size;
  num_transfer_infos_ = num_transfer_infos;
  transfer_infos_ = transfer_infos;
}

std::vector<BufferSlice> TaskBatcher::NextBatch(uint32_t max_transfer_info_num) {
  std::vector<BufferSlice> ret;
  if ((buffer_size_ == 0U) || (num_transfer_infos_ == 0U)) {
    LLMLOGW("invalid batcher state, buffer_size=%u, num_transfer_infos=%u", buffer_size_, num_transfer_infos_);
    return ret;
  }
  BatchSliceState state{};
  state.remaining_buffer_len = buffer_size_;
  transfer_info_num_ = 0;
  while (state.remaining_buffer_len > 0) {
    if (!AppendNextSlice(ret, state, max_transfer_info_num)) {
      break;
    }
  }
  return ret;
}

bool TaskBatcher::AppendNextSlice(std::vector<BufferSlice> &ret, BatchSliceState &state,
                                  uint32_t max_transfer_info_num) {
  if (current_tensor_index_ >= num_tensors_) {
    LLMLOGI("no more task");
    return false;
  }
  if ((max_transfer_info_num == UINT32_MAX) && (state.num_tasks >= kMaxTaskNumInBatch)) {
    LLMLOGI("reached max task number in batch");
    return false;
  }
  if (transfer_info_num_ >= max_transfer_info_num) {
    LLMLOGI("reached max block number:%u in batch", max_transfer_info_num);
    return false;
  }
  uint64_t data_offset = 0U;
  uint64_t data_size_cur_task = 0U;
  GetOffsetAndLength(state.remaining_buffer_len, data_offset, data_size_cur_task);
  if (data_size_cur_task == 0U) {
    LLMLOGW("zero buffer_len at transfer_info_index=%u, tensor_index=%u", current_transfer_info_index_,
            current_tensor_index_);
    return false;
  }
  const auto data_size = static_cast<uint32_t>(data_size_cur_task);
  if ((current_tensor_index_ == state.prev_tensor_index) && (data_offset == state.prev_block_end_offset) &&
      ((ret.back().data_size + data_size) <= max_block_size_)) {
    ret.back().data_size += data_size;
  } else {
    ret.emplace_back(BufferSlice{
        state.buffer_offset,
        current_tensor_index_,
        data_offset,
        static_cast<uint32_t>(data_size),
    });
    ++state.num_tasks;
  }

  state.buffer_offset += data_size;
  state.remaining_buffer_len -= data_size;
  state.prev_block_end_offset = data_offset + data_size;
  state.prev_tensor_index = current_tensor_index_;
  ++transfer_info_num_;
  UpdateIndices();
  return true;
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
    if (num_transfer_infos_ == 0U) {
      current_tensor_index_ += 1U;
      return;
    }
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
