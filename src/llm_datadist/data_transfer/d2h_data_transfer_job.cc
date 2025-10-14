/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "data_transfer/d2h_data_transfer_job.h"
#include <numeric>
#include "common/llm_thread_pool.h"

namespace llm {
namespace {
constexpr uint32_t kBlockSizeForContMem = 512 * 1024;
constexpr uint32_t kDefaultBufferSize = 32 * 1024 * 1024;
constexpr uint32_t kBufferBlocks = 64;
constexpr int32_t kTaskTypeStartBlock = 0;
constexpr int32_t kTaskTypeTransferBlock = 1;
constexpr int32_t kTaskTypeEndBlock = 2;
constexpr int32_t kTimeoutOffset = 500;
constexpr size_t kCopyThreadNum = 8U;
constexpr size_t kMaxBlockNum = 60 * 1024U;
}  // namespace

D2HDataTransferJob::~D2HDataTransferJob() {
  if (event_ != nullptr) {
    LLM_CHK_ACL(rtEventDestroy(event_));
  }
}

ge::Status D2HDataTransferJob::Initialize(const CacheEntry &cache_entry, CommEntity &comm_entity, uint64_t offset) {
  stream_ = comm_entity.GetStream();
  buffered_sender_.Initialize(comm_entity);
  const auto &req = comm_entity.GetRequest();
  auto resp_len = sizeof(ResponseInfo) + req.dst_addr_count * sizeof(uint64_t);
  auto *local_recv_flag_addr_base = PtrToPtr<void, uint8_t>(comm_entity.GetEntityInfo().local_resp_ptr) + resp_len;
  auto *remote_recv_flag_addr_base =
      PtrToPtr<void, uint8_t>(comm_entity.GetEntityInfo().remote_req_ptr) + req.req_size;
  std::vector<uint64_t> sync_flag_addresses;
  for (uint32_t i = 0U; i < req.dst_addr_count; ++i) {
    dst_buffers_.emplace_back(PtrToPtr<void, uint8_t>(req.transfer_infos[i].dst_addr));
    auto remote_recv_flag_addr = remote_recv_flag_addr_base + sizeof(int32_t) * i;
    dst_receive_flag_addresses_.emplace_back(remote_recv_flag_addr);
    auto recv_flag_addr = PtrToPtr<uint8_t, int32_t>(local_recv_flag_addr_base + sizeof(int32_t) * i);
    *recv_flag_addr = 1;  // 第一次不需要等待
    receive_flags_.emplace_back(recv_flag_addr);
    sync_flag_addresses.emplace_back(PtrToValue(recv_flag_addr));
  }
  send_sync_flag_ = PtrToPtr<void, uint8_t>(comm_entity.GetEntityInfo().send_dev_buffer_resp_ptr) + resp_len;
  auto send_sync_host_flag_ = static_cast<uint8_t>(1U);
  LLM_CHK_ACL_RET(
      rtMemcpy(send_sync_flag_, sizeof(uint8_t), &send_sync_host_flag_, sizeof(uint8_t), RT_MEMCPY_HOST_TO_DEVICE));

  size_t start_index = 0U;
  size_t end_index = 0U;
  if (req.src_tensor_indices_size == 0U) {
    end_index = cache_entry.cache_addrs.size();
  } else {
    start_index = static_cast<size_t>(req.src_tensor_start_index);
    end_index = start_index + static_cast<size_t>(req.src_tensor_indices_size);
  }
  for (; start_index < end_index; ++start_index) {
    data_addresses_.emplace_back(PtrToPtr<void, uint8_t>(cache_entry.cache_addrs[start_index].get()) + offset);
  }

  LLM_CHK_STATUS_RET(ResolveBlockSize(req, cache_entry));
  LLM_CHK_STATUS_RET(GenerateTasks(req, cache_entry));
  sync_buffer_events_.resize(req.dst_addr_count);
  const auto ret =
      comm_entity.SendResponse([this, resp_len, &sync_flag_addresses](ResponseInfo &resp, uint64_t &size) -> void {
        size = resp_len;
        resp.ret_code = ge::SUCCESS;
        resp.block_size = block_size_;
        for (size_t i = 0U; i < sync_flag_addresses.size(); ++i) {
          resp.sync_flag_addresses[i] = sync_flag_addresses[i];
        }
      });
  LLM_CHK_STATUS_RET(ret, "Failed to send response");
  LLMLOGI("Initialize successfully, send response, size = %zu", resp_len);
  return ge::SUCCESS;
}

ge::Status D2HDataTransferJob::Process(bool &is_done) {
  LLMLOGI("Process In, current_task_index = %zu, total = %zu", current_index_, tasks_.size());
  // 查询是否发送完成
  auto task_index = current_index_;
  for (; task_index < tasks_.size(); ++task_index) {
    const auto &task = tasks_[task_index];
    if (task.task_type == kTaskTypeStartBlock) {
      auto &sync_flag = receive_flags_[task.buffer_index];
      if (sync_flag.Check() != ge::SUCCESS) {
        // need schedule next time.
        return ge::SUCCESS;
      }
      LLMLOGI("Buffer[%u] wait sync flag success", task.buffer_index);
    } else if (task.task_type == kTaskTypeTransferBlock) {
      auto buffer_index = task.buffer_index;
      auto src_addr = data_addresses_[task.block_span.tensor_index] + task.block_span.tensor_offset;
      auto dst_addr =
          dst_buffers_[buffer_index] + task.block_span.buffer_block_start * block_size_;
      LLM_CHK_STATUS_RET(buffered_sender_.Put(src_addr, dst_addr, task.block_span.size));
      LLMLOGI("Buffer[%u] [Transfer] tensor_index:%u, src_offset = %lu, dst_offset = %lu, size = %u",
             task.buffer_index,
             task.block_span.tensor_index,
             task.block_span.tensor_offset,
             task.block_span.buffer_block_start * block_size_,
             task.block_span.size);
    } else if (task.task_type == kTaskTypeEndBlock) {
      LLM_CHK_STATUS_RET(buffered_sender_.Flush(), "Failed to transfer data");
      LLM_CHK_STATUS_RET(buffered_sender_.Put(send_sync_flag_, dst_receive_flag_addresses_[task.buffer_index], 1, true));
      LLMLOGI("Buffer[%u] put async done", task.buffer_index); // in transfer_stream
    } else {
      // no op
    }

    ++current_index_;
  }

  // 全部task下发完成, 记录event
  if (event_ == nullptr) {
    LLM_CHK_ACL_RET(rtEventCreate(&event_));
    LLM_CHK_ACL_RET(rtEventRecord(event_, stream_));
  }
  rtEventStatus event_status{};
  LLM_CHK_ACL_RET(rtEventQueryStatus(event_, &event_status));
  if (event_status != RT_EVENT_RECORDED) {
    // 没有wait到，等待下一次
    LLMLOGI("event not recorded");
    return ge::SUCCESS;
  }
  // all task done
  LLMLOGI("pull cache done");
  is_done = true;
  return ge::SUCCESS;
}

ge::Status D2HDataTransferJob::GenerateTasks(const TransferCacheReq &req, const CacheEntry &cache_entry) {
  DataTransferTaskGenerator task_generator(static_cast<uint32_t>(data_addresses_.size()),
                                           req.dst_addr_count,
                                           req.dst_buffer_size);
  if (cache_entry.num_blocks == 0U) {
    // local is cont.
    tasks_ = task_generator.GenerateTasks(tensor_size_, block_size_);
  } else {
    //  local is blocks
    std::vector<uint64_t> block_indices;
    LLM_CHK_BOOL_RET_STATUS(req.buffer_info_count > 0, ge::LLM_PARAM_INVALID, "buffer_info_count is 0");
    block_indices.reserve(req.buffer_info_count);
    for (uint32_t i = 0U; i < req.buffer_info_count; ++i) {
      block_indices.emplace_back(req.transfer_infos[req.dst_addr_count + i].buffer_info.block_start_index);
    }
    tasks_ = task_generator.GenerateTasks(block_size_,
                                          static_cast<uint32_t>(block_indices.size()),
                                          block_indices.data());
  }
  if (LlmIsLogEnable(LLM_MODULE_NAME, DLOG_INFO)) {
    PrintTasks(tasks_);
  }
  return ge::SUCCESS;
}

void D2HDataTransferJob::PrintTasks(const std::vector<TransferBlocksTask> &tasks) {
  for (const auto &task : tasks) {
    if (task.task_type == kTaskTypeTransferBlock) {
      std::stringstream ss;
      LLMLOGI("Buffer[%u] transfer, tensor_index = %u, buffer_block_start = %u, tensor_offset = %u, size = %u",
             task.buffer_index,
             task.block_span.tensor_index,
             task.block_span.buffer_block_start,
             task.block_span.tensor_offset,
             task.block_span.size);
    } else if (task.task_type == kTaskTypeStartBlock) {
      LLMLOGI("Buffer[%u] Start", task.buffer_index);
    } else if (task.task_type == kTaskTypeEndBlock) {
      LLMLOGI("Buffer[%u] End", task.buffer_index);
    } else {
      // do nothing
    }
  }
}

ge::Status D2HDataTransferJob::ResolveBlockSize(const TransferCacheReq &request, const CacheEntry &cache_entry) {
  tensor_size_ = static_cast<int64_t>(request.pull_size);
  if (request.block_size != 0) {
    // dst is blocks
    block_size_ = request.block_size;
    if (cache_entry.num_blocks != 0) {
      // blocks to blocks, check block size
      LLM_CHK_BOOL_RET_STATUS(request.block_size == cache_entry.stride,
                             ge::LLM_PARAM_INVALID, "block size mismatches, in req = %lu, local = %lu",
                             request.block_size, cache_entry.stride);
    } else {
      // cont. to blocks
      tensor_size_ = std::min(tensor_size_, static_cast<int64_t>(cache_entry.stride));
    }
  } else {
    // dst is cont.
    LLM_CHK_BOOL_RET_STATUS(tensor_size_ > 0, ge::LLM_PARAM_INVALID, "request tensor size == 0");
    LLM_CHK_BOOL_RET_STATUS(cache_entry.num_blocks == 0, ge::LLM_PARAM_INVALID,
                           "Blocks to continuous memory is not supported by D2H data transfer");
    // cont. to cont.
    block_size_ = kBlockSizeForContMem;
  }
  LLMLOGI("tensor_size = %ld, block_size = %u", tensor_size_, block_size_);
  return ge::SUCCESS;
}

std::vector<TransferBlocksTask> DataTransferTaskGenerator::DoGenerate(uint32_t block_size, uint32_t tail_block_size,
                                                                      uint32_t num_block_indices,
                                                                      const uint64_t *block_indices) {
  std::vector<TransferBlocksTask> ret;
  uint32_t buffer_block_num = buffer_size_ / block_size;
  uint32_t buffer_index = 0;
  uint32_t prev_buffer_index = UINT32_MAX;
  uint32_t buffer_block_index = 0;
  std::set<uint32_t> used_buffer_indices;
  uint32_t num_transfer_tasks = 0;
  for (uint32_t i = 0U; i < static_cast<uint32_t>(num_tensors_); ++i) {
    TransferBlocksTask *prev_task = nullptr;
    uint64_t prev_block_index = UINT64_MAX;
    for (size_t k = 0U; k < num_block_indices; ++k) {
      const bool is_last_block = (k == num_block_indices - 1);
      const auto block_index = block_indices[k];
      const auto cur_block_size = is_last_block ? tail_block_size : block_size;
      if (buffer_index != prev_buffer_index) { // first task in current buffer
        ret.emplace_back(TransferBlocksTask{kTaskTypeStartBlock, buffer_index, TransferBlockSpan{}});
        num_transfer_tasks = 0;
      }
      prev_buffer_index = buffer_index;
      if ((prev_task != nullptr) && (prev_block_index != UINT64_MAX) && (block_index == prev_block_index + 1U) &&
          prev_task->block_span.size + static_cast<uint32_t>(cur_block_size) <= max_block_size_) {
        prev_task->block_span.size += static_cast<uint32_t>(cur_block_size);  // 连续block, 并且单次大小<=max_block_size_
      } else {
        const auto tensor_offset = block_index * block_size;
        ret.emplace_back(TransferBlocksTask{kTaskTypeTransferBlock, buffer_index,
                                            TransferBlockSpan{buffer_block_index, tensor_offset, i, cur_block_size}});
        ++num_transfer_tasks;
      }
      ++buffer_block_index;
      if ((buffer_block_index >= buffer_block_num) || (num_transfer_tasks >= kBufferBlocks)) {
        buffer_block_nums_.emplace_back(buffer_block_index);
        // buffer满, 或一次传输的个数超过64-连续是32M
        used_buffer_indices.emplace(buffer_index);
        // switch buffer
        // buffer write done, notify src to process buffer
        ret.emplace_back(TransferBlocksTask{kTaskTypeEndBlock, buffer_index, TransferBlockSpan{}});
        buffer_index = (buffer_index + 1) % num_buffers_;
        buffer_block_index = 0;
        prev_task = nullptr;
      } else {
        prev_task = &ret.back();
      }
      prev_block_index = block_index;
    }
  }
  if (buffer_block_index > 0) {
    buffer_block_nums_.emplace_back(buffer_block_index);
  }
  if ((!ret.empty()) && (ret.back().task_type != kTaskTypeEndBlock)) {
    ret.emplace_back(TransferBlocksTask{kTaskTypeEndBlock, buffer_index, TransferBlockSpan{}});
  }
  return ret;
}

std::vector<TransferBlocksTask> DataTransferTaskGenerator::DoGenerateForClientBlocks(
    uint32_t block_size, uint32_t tail_block_size, uint32_t num_block_indices, const uint64_t *block_indices,
    const uint64_t *remote_block_indices) {
  std::vector<TransferBlocksTask> ret;
  uint32_t buffer_block_num = buffer_size_ / block_size;
  uint32_t buffer_index = 0;
  uint32_t prev_buffer_index = UINT32_MAX;
  uint32_t buffer_block_index = 0;
  std::set<uint32_t> used_buffer_indices;
  (void)DoGenerate(block_size, tail_block_size, num_block_indices, remote_block_indices);
  uint32_t buffer_task_index = 0U;
  // genereate remote block num for every buffer
  auto remote_buffer_block_num = buffer_block_nums_[buffer_task_index];
  for (uint32_t i = 0U; i < static_cast<uint32_t>(num_tensors_); ++i) {
    TransferBlocksTask *prev_task = nullptr;
    uint64_t prev_block_index = UINT64_MAX;
    for (size_t k = 0U; k < num_block_indices; ++k) {
      const bool is_last_block = (k == num_block_indices - 1);
      const auto block_index = block_indices[k];
      const auto cur_block_size = is_last_block ? tail_block_size : block_size;
      if (buffer_index != prev_buffer_index) { // first task in current buffer
        ret.emplace_back(TransferBlocksTask{kTaskTypeStartBlock, buffer_index, TransferBlockSpan{}});
      }
      prev_buffer_index = buffer_index;
      if ((prev_task != nullptr) && (prev_block_index != UINT64_MAX) && (block_index == prev_block_index + 1U) &&
          prev_task->block_span.size + static_cast<uint32_t>(cur_block_size) <= max_block_size_) {
        prev_task->block_span.size += static_cast<uint32_t>(cur_block_size);  // 连续block, 并且单次大小<=max_block_size_
      } else {
        const auto tensor_offset = block_index * block_size;
        ret.emplace_back(TransferBlocksTask{kTaskTypeTransferBlock, buffer_index,
                                            TransferBlockSpan{buffer_block_index, tensor_offset, i, cur_block_size}});
      }
      ++buffer_block_index;
      if ((buffer_block_index >= buffer_block_num) || (buffer_block_index >= remote_buffer_block_num)) {
        used_buffer_indices.emplace(buffer_index);
        ret.emplace_back(TransferBlocksTask{kTaskTypeEndBlock, buffer_index, TransferBlockSpan{}});
        buffer_task_index++;
        GetNextBufBlockNum(buffer_task_index, remote_buffer_block_num);
        // change buffer
        buffer_index = (buffer_index + 1) % num_buffers_;
        buffer_block_index = 0;
        prev_task = nullptr;
      } else {
        prev_task = &ret.back();
      }
      prev_block_index = block_index;
    }
  }
  if ((!ret.empty()) && (ret.back().task_type != kTaskTypeEndBlock)) {
    ret.emplace_back(TransferBlocksTask{kTaskTypeEndBlock, buffer_index, TransferBlockSpan{}});
  }
  return ret;
}

void DataTransferTaskGenerator::GetNextBufBlockNum(uint32_t buffer_task_index, uint32_t &remote_buffer_block_num) {
  if (buffer_task_index < buffer_block_nums_.size()) {
    remote_buffer_block_num = buffer_block_nums_[buffer_task_index];
  }
}

std::vector<TransferBlocksTask> DataTransferTaskGenerator::DoGenerateForLargeBlock(uint32_t block_size,
                                                                                   uint32_t num_block_indices,
                                                                                   const uint64_t *block_indices) const {
  std::vector<TransferBlocksTask> ret;
  uint32_t buffer_index = 0;
  std::set<uint32_t> used_buffer_indices;
  for (uint32_t i = 0U; i < static_cast<uint32_t>(num_tensors_); ++i) {
    for (size_t k = 0U; k < num_block_indices; ++k) {
      auto block_index = block_indices[k];
      auto tensor_offset = block_index * block_size;
      auto remaining_block_size = block_size;
      while (remaining_block_size > 0) {
        auto cur_block_size = std::min(remaining_block_size, buffer_size_);
        ret.emplace_back(TransferBlocksTask{kTaskTypeStartBlock, buffer_index, TransferBlockSpan{}});
        ret.emplace_back(TransferBlocksTask{kTaskTypeTransferBlock, buffer_index,
                                            TransferBlockSpan{0, tensor_offset, i, cur_block_size}});
        remaining_block_size -= cur_block_size;
        tensor_offset += cur_block_size;
        ret.emplace_back(TransferBlocksTask{kTaskTypeEndBlock, buffer_index, TransferBlockSpan{}});
      }
    }
  }
  return ret;
}

std::vector<TransferBlocksTask> DataTransferTaskGenerator::GenerateTasks(int64_t tensor_size,
                                                                         uint32_t block_size) {
  std::vector<TransferBlocksTask> ret;
  auto block_num = tensor_size / block_size;
  auto tail_block_size = tensor_size - block_size * block_num;
  if (tail_block_size > 0) {
    ++block_num;
  } else {
    tail_block_size = block_size;
  }
  std::vector<uint64_t> block_indices(block_num);
  std::iota(block_indices.begin(), block_indices.end(), 0U);
  return (block_size > buffer_size_)
             ? DoGenerateForLargeBlock(block_size, static_cast<uint32_t>(block_indices.size()), block_indices.data())
             : DoGenerate(static_cast<uint32_t>(block_size), static_cast<uint32_t>(tail_block_size),
                          static_cast<uint32_t>(block_indices.size()), block_indices.data());
}

std::vector<TransferBlocksTask> DataTransferTaskGenerator::GenerateTasks(uint32_t block_size,
                                                                         uint32_t num_block_indices,
                                                                         const uint64_t *block_indices,
                                                                         const uint64_t *remote_block_indices) {
  LLMLOGI("GenerateTasks block_size:%u, buffer_size:%u", block_size, buffer_size_);
  if (block_size > buffer_size_) {
    return DoGenerateForLargeBlock(block_size, num_block_indices, block_indices);
  } else if (remote_block_indices == nullptr){
    return DoGenerate(block_size, block_size, num_block_indices, block_indices);
  } else {
    return DoGenerateForClientBlocks(block_size, block_size, num_block_indices, block_indices, remote_block_indices);
  }
}

D2HDataTransferClient::D2HDataTransferClient(CommEntity &comm_entity, rtStream_t stream)
    : comm_entity_(&comm_entity), stream_(stream) {
  buffered_sender_.Initialize(comm_entity, stream);
}

D2HDataTransferClient::~D2HDataTransferClient() {
  for (auto buffer_data : buffers_) {
    comm_entity_->GetCacheManager()->GetNpuMemPool()->Free(buffer_data);
  }
  if (send_dev_flag_ != nullptr) {
    comm_entity_->GetCacheManager()->GetNpuMemPool()->Free(send_dev_flag_);
  }
}

ge::Status D2HDataTransferClient::PullCache(const CacheEntry &cache_entry,
                                            const CacheKey &cache_key,
                                            const PullCacheParam &pull_cache_param,
                                            int32_t timeout_in_ms) {
  timeout_in_ms_ = timeout_in_ms;
  timeout_tp_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_in_ms_ + kTimeoutOffset);
  LLM_CHK_STATUS_RET(Prepare(cache_entry, cache_key, pull_cache_param));
  LLM_CHK_STATUS_RET(RunTasks());
  return ge::SUCCESS;
}

ge::Status D2HDataTransferClient::Prepare(const CacheEntry &cache_entry,
                                          const CacheKey &cache_key,
                                          const PullCacheParam &pull_cache_param) {
  bool is_blocks_to_cont = (!pull_cache_param.prompt_blocks.empty()) && pull_cache_param.decoder_blocks.empty();
  LLM_CHK_BOOL_RET_STATUS((!is_blocks_to_cont), ge::LLM_PARAM_INVALID,
                         "Blocks to continuous memory is not supported by D2H data transfer");
  LLM_CHK_BOOL_RET_STATUS(pull_cache_param.prompt_blocks.size() <= kMaxBlockNum, ge::LLM_PARAM_INVALID,
                         "number of prompt blocks (%zu) out of bound, at most %zu is supported",
                         pull_cache_param.prompt_blocks.size(), kMaxBlockNum);
  buffer_size_ = kDefaultBufferSize;
  auto recv_flag_base = PtrToPtr<void, uint8_t>(comm_entity_->GetEntityInfo().local_req_ptr) +
                        sizeof(TransferCacheReq) +
                        sizeof(TransferInfo) * (num_buffers_ + pull_cache_param.prompt_blocks.size());
  for (uint32_t i = 0U; i < num_buffers_; ++i) {
    const size_t buffer_size = buffer_size_;
    auto buffer_data =
        comm_entity_->GetCacheManager()->GetNpuMemPool()->Alloc(buffer_size, static_cast<int32_t>(timeout_in_ms_));
    // 需要新的错误码
    LLM_CHK_BOOL_RET_STATUS(buffer_data != nullptr, ge::LLM_OUT_OF_MEMORY,
                           "Failed to allocate transfer buffer, size = %zu", buffer_size);
    buffers_.emplace_back(PtrToPtr<void, uint8_t>(buffer_data));
    auto recv_flag_ptr = PtrToPtr<uint8_t, int32_t>(recv_flag_base + i * sizeof(int32_t));
    *recv_flag_ptr = 0;
    recv_flags_.emplace_back(recv_flag_ptr);
  }
  auto send_flag =
      comm_entity_->GetCacheManager()->GetNpuMemPool()->Alloc(sizeof(int32_t), static_cast<int32_t>(timeout_in_ms_));
  LLM_CHK_BOOL_RET_STATUS(send_flag != nullptr, ge::LLM_OUT_OF_MEMORY,
                         "Failed to allocate memory for sending flag.");
  send_dev_flag_ = PtrToPtr<void, uint8_t>(send_flag);
  auto host_flag = static_cast<uint8_t>(1U);
  LLM_CHK_ACL_RET(rtMemcpy(send_dev_flag_, sizeof(uint8_t), &host_flag, sizeof(uint8_t), RT_MEMCPY_HOST_TO_DEVICE));

  LLM_CHK_STATUS_RET(SendRequest(cache_entry, cache_key, pull_cache_param), "Failed to send request");
  // PUT req & flag
  LLMLOGI("request sent");
  const ResponseInfo *response_info = nullptr;
  LLM_CHK_STATUS_RET(comm_entity_->GetResponse(response_info, &timeout_tp_), "Failed to get response");
  const auto &response = *response_info;
  LLM_CHK_STATUS_RET(response.ret_code, "Failed to pull cache, server returned: %u", response.ret_code);
  LLMLOGI("response received, block_size = %u", response.block_size);
  for (uint32_t i = 0U; i < num_buffers_; ++i) {
    auto remote_flag_addr = ValueToPtr(response.sync_flag_addresses[i]);
    remote_receive_flag_addresses_.emplace_back(static_cast<uint8_t *>(remote_flag_addr));
  }
  LLM_CHK_STATUS_RET(GenerateTasks(cache_entry, pull_cache_param, response), "Failed to generate tasks");
  return ge::SUCCESS;
}

ge::Status D2HDataTransferClient::GenerateTasks(const CacheEntry &cache_entry,
                                                const PullCacheParam &pull_cache_param,
                                                const ResponseInfo &response) {
  std::vector<std::shared_ptr<void>> cache_addrs;
  if (pull_cache_param.dst_tensor_indices.empty()) {
    cache_addrs = cache_entry.cache_addrs;
  } else {
    const uint64_t layer_start_tensor_index = pull_cache_param.dst_tensor_indices.front();
    cache_addrs.assign(
        cache_entry.cache_addrs.begin() + layer_start_tensor_index,
        cache_entry.cache_addrs.begin() + layer_start_tensor_index + pull_cache_param.dst_tensor_indices.size());
  }
  DataTransferTaskGenerator task_generator(cache_addrs.size(), buffers_.size(), buffer_size_);
  if (cache_entry.num_blocks == 0) {
    // to cont. tensor
    block_size_ = response.block_size;
    LLM_CHK_BOOL_RET_STATUS(block_size_ > 0, ge::FAILED, "block size from response is 0");
    const auto tensor_size = pull_cache_param.size > 0
                             ? pull_cache_param.size
                             : static_cast<int64_t>(cache_entry.stride);
    tasks_ = task_generator.GenerateTasks(tensor_size, response.block_size);
    for (const auto &cache_addr : cache_addrs) {
      tensor_addresses_.emplace_back(
          PtrToPtr<void, uint8_t>(cache_addr.get()) + cache_entry.stride * pull_cache_param.batch_index);
    }
  } else {
    // to blocks
    block_size_ = cache_entry.stride;
    if (pull_cache_param.prompt_blocks.empty()) {
      std::vector<uint64_t> remote_block_indices(pull_cache_param.decoder_blocks.size());
      std::iota(remote_block_indices.begin(), remote_block_indices.end(), 0U);
      tasks_ = task_generator.GenerateTasks(block_size_, pull_cache_param.decoder_blocks.size(),
                                            pull_cache_param.decoder_blocks.data(), remote_block_indices.data());
    } else {
      tasks_ = task_generator.GenerateTasks(block_size_,
                                            pull_cache_param.decoder_blocks.size(),
                                            pull_cache_param.decoder_blocks.data(),
                                            pull_cache_param.prompt_blocks.data());
    }
    for (const auto &cache_addr: cache_addrs) {
      tensor_addresses_.emplace_back(PtrToPtr<void, uint8_t>(cache_addr.get()));
    }
  }
  return ge::SUCCESS;
}

ge::Status D2HDataTransferClient::SendRequest(const CacheEntry &cache_entry,
                                              const CacheKey &cache_key,
                                              const PullCacheParam &pull_cache_param) const {
  auto fill_req_func =
      [this, &cache_entry, &cache_key, &pull_cache_param](TransferCacheReq &request, uint64_t &size) -> void {
        FillRequest(cache_entry, cache_key, pull_cache_param, request, size);
      };
  return comm_entity_->SendRequest(fill_req_func, stream_);
}

void D2HDataTransferClient::FillRequest(const CacheEntry &cache_entry,
                                        const CacheKey &cache_key,
                                        const PullCacheParam &pull_cache_param,
                                        TransferCacheReq &request,
                                        uint64_t &size) const {
  size = sizeof(TransferCacheReq) + sizeof(TransferInfo) * (buffers_.size() + pull_cache_param.prompt_blocks.size());
  request.req_size = size;
  request.is_pull_block = static_cast<uint32_t>(!pull_cache_param.prompt_blocks.empty());
  request.num_tensors = pull_cache_param.dst_tensor_indices.empty()
                            ? static_cast<uint32_t>(cache_entry.cache_addrs.size())
                            : static_cast<uint32_t>(pull_cache_param.dst_tensor_indices.size());
  request.cache_id = cache_key.prompt_cache_id;
  request.batch_index = cache_key.prompt_batch_index;
  request.req_id = cache_key.req_id;
  request.prefix_id = cache_key.prefix_id;
  request.model_id = cache_key.model_id;
  request.cache_id = cache_key.prompt_cache_id;
  request.dst_placement = 0;
  request.batch_index = cache_key.prompt_batch_index;
  request.block_size = cache_entry.num_blocks > 0 ? cache_entry.stride : 0;
  request.buffer_info_count = pull_cache_param.prompt_blocks.size();
  request.dst_addr_count = buffers_.size();
  request.dst_buffer_size = buffer_size_;
  request.timeout_in_ms = static_cast<int32_t>(timeout_in_ms_);
  request.src_tensor_indices_size = pull_cache_param.src_tensor_indices.size();
  request.src_tensor_start_index = 0U;
  if (!pull_cache_param.src_tensor_indices.empty()) {
    request.src_tensor_start_index = pull_cache_param.src_tensor_indices.front();
  }
  if (cache_entry.num_blocks == 0) {
    // local is cont.
    request.pull_size = pull_cache_param.size > 0 ? pull_cache_param.size : static_cast<int64_t>(cache_entry.stride);
  } else {
    request.pull_size =
        static_cast<int64_t>(pull_cache_param.decoder_blocks.size()) * static_cast<int64_t>(cache_entry.stride);
  }
  for (size_t i = 0U; i < buffers_.size(); ++i) {
    request.transfer_infos[i].dst_addr = buffers_[i];
  }
  auto *block_infos = &request.transfer_infos[request.dst_addr_count];
  uint64_t max_block_index = 0;
  for (size_t i = 0U; i < pull_cache_param.prompt_blocks.size(); ++i) {
    const auto remote_block_index = pull_cache_param.prompt_blocks[i];
    if (max_block_index < remote_block_index) {
      max_block_index = remote_block_index;
    }
    block_infos[i].buffer_info.block_start_index = remote_block_index;
    block_infos[i].buffer_info.buffer_len = request.block_size;
  }
  request.max_block_index = max_block_index;
}

ge::Status D2HDataTransferClient::RunTasks() {
  LLMThreadPool thread_pool("ge_llm_copy", kCopyThreadNum);
  std::vector<std::future<ge::Status>> futures;
  std::chrono::steady_clock::time_point copy_start;
  for (const auto &task: tasks_) {
    if (task.task_type == kTaskTypeStartBlock) {
      LLM_CHK_BOOL_RET_STATUS_NOLOG(recv_flags_[task.buffer_index].Wait(&timeout_tp_) != 0, ge::LLM_TIMEOUT,
                                   "Wait flag timeout");
      LLMLOGI("wait flag success");
      copy_start = std::chrono::steady_clock::now();
    } else if (task.task_type == kTaskTypeTransferBlock) {
      auto fut = thread_pool.commit([this, &task]() -> ge::Status{
        return CopyAsync(task);
      });
      futures.emplace_back(std::move(fut));
    } else if (task.task_type == kTaskTypeEndBlock) {
      for (auto &fut : futures) {
        LLM_CHK_STATUS_RET(fut.get());
      }
      auto copy_end = std::chrono::steady_clock::now();
      auto cost = std::chrono::duration_cast<std::chrono::microseconds>(copy_end - copy_start).count();
      futures.clear();
      LLMLOGI("Buffer[%u] copy end, cost = %ld us", task.buffer_index, cost);
      // D2H flag
      LLM_CHK_STATUS_RET(buffered_sender_.Put(send_dev_flag_, remote_receive_flag_addresses_[task.buffer_index],
                                             sizeof(int32_t), true));
      LLMLOGI("Buffer[%u] flag sent", task.buffer_index);
    } else {
      // do nothing
    }
  }
  return ge::SUCCESS;
}
ge::Status D2HDataTransferClient::CopyAsync(const TransferBlocksTask &task) {
  LLM_CHK_ACL_RET(rtCtxSetCurrent(comm_entity_->GetCurrentContext()));
  auto src_addr = buffers_[task.buffer_index] + task.block_span.buffer_block_start * block_size_;
  auto dst_addr = tensor_addresses_[task.block_span.tensor_index] + task.block_span.tensor_offset;
  const auto size = task.block_span.size;
  LLMLOGI("Buffer[%u] copy, tensor_index:%u, src_offset = %lu, dst_offset = %lu, size = %u",
         task.buffer_index,
         task.block_span.tensor_index,
         task.block_span.buffer_block_start * block_size_,
         task.block_span.tensor_offset,
         size);
  LLM_CHK_ACL_RET(rtMemcpy(dst_addr, size, src_addr, size, RT_MEMCPY_DEVICE_TO_HOST));
  return ge::SUCCESS;
}
}  // namespace llm
