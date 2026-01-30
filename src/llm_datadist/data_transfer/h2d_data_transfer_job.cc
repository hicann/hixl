/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "data_transfer/h2d_data_transfer_job.h"
#include <numeric>

namespace llm {
namespace {
constexpr size_t kDefaultBlockSize = 2 * 1024 * 1024;
constexpr size_t kDefaultBufferNum = 2U;
class BufferState {
 public:
  BufferState() = default;
  virtual ~BufferState() = default;
  virtual ge::Status UpdateState(BufferContext &context) const = 0;
};

class BufferStateIdle : public BufferState {
 public:
  static const size_t kStateId = 0;
  ge::Status UpdateState(BufferContext &context) const override;
};

class BufferStateCopy : public BufferState {
 public:
  static const size_t kStateId = 1;
  ge::Status UpdateState(BufferContext &context) const override;

 private:
  static ge::Status CopyAsync(BufferContext &context, size_t slice_index);
};

class BufferStateTransfer : public BufferState {
 public:
  static const size_t kStateId = 2;
  ge::Status UpdateState(BufferContext &context) const override;

  static ge::Status BatchPutAsync(BufferContext &context);
};

class BufferStateEnd : public BufferState {
 public:
  static const size_t kStateId = 3;
  ge::Status UpdateState(BufferContext &) const override {
    // do nothing
    return ge::SUCCESS;
  }
};

const BufferStateIdle kIdleBufferState;
const BufferStateCopy kCopyBufferState;
const BufferStateTransfer kTransferBufferState;
const BufferStateEnd kEndBufferState;

constexpr std::array<const BufferState *, 4U>
    kBufferStates{&kIdleBufferState, &kCopyBufferState, &kTransferBufferState, &kEndBufferState};
}  // namespace

ge::Status H2DDataTransferJob::Initialize(const CacheEntry &cache_entry, CommEntity &comm_entity, uint64_t offset) {
  const auto &request = comm_entity.GetRequest();
  const auto is_same_layout = ((cache_entry.num_blocks > 0) == (request.block_size > 0));
  LLM_CHK_BOOL_RET_STATUS(is_same_layout,
                         ge::LLM_PARAM_INVALID,
                         "different layout is not supported by H2D data transfer, src_is_block = %d dst_is_block = %d",
                         static_cast<int32_t>(cache_entry.num_blocks > 0),
                         static_cast<int32_t>(request.block_size > 0));
  LLM_CHK_BOOL_RET_STATUS(comm_entity.GetCacheManager()->GetNpuMemPool() != nullptr, ge::LLM_PARAM_INVALID,
                         "Device memory pool is not enabled.");
  comm_entity_ = &comm_entity;
  src_task_generator_ = TaskBatcher(buffer_size_);
  dst_task_generator_ = TaskBatcher(buffer_size_);
  uint32_t block_size = cache_entry.stride;
  if (cache_entry.num_blocks == 0) {
    // cont.
    block_size = std::min(kDefaultBlockSize, static_cast<size_t>(request.pull_size));
  }
  const uint64_t num_tensors = (request.src_tensor_indices_size == 0U)
                                   ? cache_entry.cache_addrs.size()
                                   : static_cast<uint64_t>(request.src_tensor_indices_size);
  LLM_CHK_BOOL_RET_STATUS((num_tensors <= cache_entry.cache_addrs.size()),
      ge::LLM_PARAM_INVALID, "src_tensor_indices_size[%u] is invalid, src_cache num is[%zu]",
      request.src_tensor_indices_size, cache_entry.cache_addrs.size());
  src_task_generator_.Initialize(num_tensors,
                                 block_size,
                                 request.buffer_info_count,
                                 &request.transfer_infos[request.dst_addr_count]);
  dst_task_generator_.Initialize(num_tensors,
                                 block_size,
                                 request.buffer_info_count,
                                 &request.transfer_infos[request.dst_addr_count + request.buffer_info_count]);
  InitBufferContexts(cache_entry, request, comm_entity, offset);
  return ge::SUCCESS;
}

void H2DDataTransferJob::InitBufferContexts(const CacheEntry &cache_entry, const TransferCacheReq &request,
                                            CommEntity &comm_entity, uint64_t offset) {
  std::vector<std::shared_ptr<void>> src_cache_addrs;
  if (request.src_tensor_indices_size == 0U) {
    src_cache_addrs = cache_entry.cache_addrs;
  } else {
    const size_t layer_start_tensor_index = static_cast<size_t>(request.src_tensor_start_index);
    const size_t layer_range_num_tensors = static_cast<size_t>(request.src_tensor_indices_size);
    src_cache_addrs.assign(cache_entry.cache_addrs.begin() + layer_start_tensor_index,
                           cache_entry.cache_addrs.begin() + layer_start_tensor_index + layer_range_num_tensors);
  }
  for (size_t i = 0U; i < kDefaultBufferNum; ++i) {
    BufferContext buffer_context{};
    buffer_context.buffer_index = i;
    buffer_context.offset = offset;
    buffer_context.stream = comm_entity.GetStream();
    buffer_context.comm_entity = &comm_entity;
    buffer_context.thread_pool = &thread_pool_;
    buffer_context.src_task_batcher = &src_task_generator_;
    buffer_context.dst_task_batcher = &dst_task_generator_;
    buffer_context.data_addresses = src_cache_addrs;
    buffer_context.request = &request;
    buffer_context.aclrt_context = comm_entity.GetCurrentContext();
    buffers_.emplace_back(std::move(buffer_context));
  }
}

ge::Status H2DDataTransferJob::Process(bool &is_done) {
  size_t done_count = 0;
  for (auto &buffer_context : buffers_) {
    if (buffer_context.buffer == nullptr) {
      auto mem_pool = comm_entity_->GetCacheManager()->GetNpuMemPool();
      buffer_context.buffer = mem_pool->AllocShared(buffer_size_);
      LLMLOGI("Alloc npu buffer end.");
      if (buffer_context.buffer == nullptr) {
        LLMLOGW("Failed to allocate buffer memory, size = %zu, try next time", buffer_size_);
        continue;
      }
    }

    bool state_changed = true;
    while (state_changed) {
      int prev_state = buffer_context.state;
      LLM_CHK_STATUS_RET(kBufferStates[buffer_context.state]->UpdateState(buffer_context));
      state_changed = (prev_state != buffer_context.state);
    }
    if (buffer_context.state == BufferStateEnd::kStateId) {
      ++done_count;
    }
  }
  is_done = (done_count == buffers_.size());
  if (is_done) {
    LLM_CHK_STATUS_RET(comm_entity_->SendResponse(ge::SUCCESS), "Failed to send response");
    LLMLOGI("Process done, send end flag");
  }
  return ge::SUCCESS;
}

ge::Status BufferStateIdle::UpdateState(BufferContext &context) const {
  context.dst_buffer_slices = context.dst_task_batcher->NextBatch();
  if (context.dst_buffer_slices.empty()) {
    LLMLOGI("Buffer[%zu] changed to END state", context.buffer_index);
    context.state = BufferStateEnd::kStateId;
  } else {
    auto dst_transfer_info_num = context.dst_task_batcher->GetTransferInfoNum();
    context.buffer_slices = context.src_task_batcher->NextBatch(dst_transfer_info_num);
    LLMLOGI("Buffer[%zu] Next batch generated, num_tasks = %zu", context.buffer_index, context.buffer_slices.size());
    for (auto &task : context.buffer_slices) {
      LLMLOGI("buffer offset = %zu, data_index = %u, data_offset = %lu, data_size = %u",
             task.buffer_offset,
             task.data_index,
             task.data_offset,
             task.data_size);
    }
    LLMLOGI("Buffer[%zu] changed to COPY state", context.buffer_index);
    context.state = BufferStateCopy::kStateId;
  }
  return ge::SUCCESS;
}

ge::Status BufferStateCopy::UpdateState(BufferContext &context) const {
  if (context.copy_futures.empty()) {
    context.batch_copy_start = std::chrono::steady_clock::now();
    const auto &tasks = context.buffer_slices;
    for (size_t i = 0; i < tasks.size(); ++i) {
      auto fut = context.thread_pool->commit([&context, i]() -> ge::Status {
        return BufferStateCopy::CopyAsync(context, i);
      });
      context.copy_futures.emplace_back(std::move(fut));
    }
  } else {
    bool need_wait = false;
    for (auto &fut : context.copy_futures) {
      if (fut.valid()) {
        if (fut.wait_for(std::chrono::microseconds(1)) == std::future_status::ready) {
          auto ret = fut.get();
          LLM_CHK_STATUS_RET(ret, "copy failed");
        } else {
          need_wait = true;
          break;
        }
      }
    }
    if (!need_wait) {
      auto tp_end = std::chrono::steady_clock::now();
      auto cost = std::chrono::duration_cast<std::chrono::microseconds>(tp_end - context.batch_copy_start).count();
      context.state = BufferStateTransfer::kStateId;
      LLMLOGI("Buffer[%zu] changed to TRANSFER state, copy_num = %zu, copy_size = %u, cost = %ld us.",
             context.buffer_index, context.buffer_slices.size(), context.buffer_slices.front().data_size, cost);
      context.copy_futures.clear();
    }
  }
  return ge::SUCCESS;
}

ge::Status BufferStateCopy::CopyAsync(BufferContext &context, size_t slice_index) {
  LLM_CHK_ACL_RET(aclrtSetCurrentContext(context.aclrt_context));
  const auto &tasks = context.buffer_slices;
  auto &task = tasks[slice_index];
  auto src_addr = PtrToPtr<void, uint8_t>(context.data_addresses[task.data_index].get()) +
      task.data_offset + context.offset;
  auto dst_addr = PtrToPtr<void, uint8_t>(context.buffer.get()) + task.buffer_offset;
  LLM_CHK_ACL_RET(aclrtMemcpy(dst_addr, task.data_size, src_addr, task.data_size, ACL_MEMCPY_HOST_TO_DEVICE));
  LLMLOGI("Buffer[%zu] copy success, src_offset = %lu, dst_offset = %lu, size = %u",
         context.buffer_index,
         task.data_offset + context.offset,
         task.buffer_offset,
         task.data_size);
  return ge::SUCCESS;
}

ge::Status BufferStateTransfer::UpdateState(BufferContext &context) const {
  if (context.event == nullptr) {
    // BatchPut
    LLM_CHK_STATUS_RET(BatchPutAsync(context));
    LLM_CHK_ACL_RET(aclrtCreateEvent(&context.event));
    LLM_CHK_ACL_RET(aclrtRecordEvent(context.event, context.stream));
  } else {
    aclrtEventRecordedStatus event_status{};
    aclrtQueryEventStatus(context.event, &event_status);
    if (event_status != ACL_EVENT_RECORDED_STATUS_COMPLETE) {
      // 没有wait到，等待下一次, 不更新task index
      LLMLOGI("Buffer[%zu] transfer not ended", context.buffer_index);
    } else {
      aclrtDestroyEvent(context.event);
      context.event = nullptr;
      context.state = BufferStateIdle::kStateId;
      LLMLOGI("Buffer[%zu] changed to IDLE state", context.buffer_index);
    }
  }
  return ge::SUCCESS;
}

ge::Status BufferStateTransfer::BatchPutAsync(BufferContext &context) {
  const auto buffer = PtrToPtr<void, uint8_t>(context.buffer.get());
  std::vector<HcclOneSideOpDesc> op_desc_batch;
  op_desc_batch.reserve(context.dst_buffer_slices.size());
  for (const auto &task : context.dst_buffer_slices) {
    const auto src_addr = buffer + task.buffer_offset;
    const auto dst_addr =
        PtrToPtr<void, uint8_t>(context.request->transfer_infos[task.data_index].dst_addr) + task.data_offset;
    op_desc_batch.emplace_back(HcclOneSideOpDesc{src_addr, dst_addr, task.data_size, HCCL_DATA_TYPE_UINT8});
    LLMLOGI("Buffer[%zu] [BatchPut] task added, src_offset = %u, dst_offset = %lu, size = %u",
           context.buffer_index, task.buffer_offset, task.data_offset, task.data_size);
  }
  LLM_CHK_STATUS_RET(context.comm_entity->BatchPutAsync(op_desc_batch), "Failed to batch put data");
  LLMLOGI("Buffer[%zu] BatchPutAsync success", context.buffer_index);
  return ge::SUCCESS;
}
}  // namespace llm