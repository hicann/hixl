/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "buffer_transfer_service.h"
#include <chrono>
#include <thread>
#include <map>
#include <mutex>
#include <atomic>
#include "common/def_types.h"
#include "base/err_msg.h"
#include "common/llm_scope_guard.h"
#include "common/llm_checker.h"

namespace adxl {
namespace {
constexpr int64_t kDefaultSleepTime = 1;
constexpr int32_t kMillisToMicros = 1000;
constexpr int32_t kTimeoutLoss = 50;
constexpr uint64_t kBlockSize = 512 * 1024U;
constexpr size_t kServerPoolIndex = 1U;
}  // namespace

Status BufferTransferService::Initialize() {
  ADXL_CHK_ACL_RET(rtCtxGetCurrent(&rt_context_));
  ADXL_CHK_ACL_RET(rtGetDevice(&device_id_));
  buffer_req_processor_ = std::thread([this]() { ProcessBufferReqFirstStep(); });
  buffer_resp_processor_ = std::thread([this]() { ProcessBufferResp(); });
  buffer_second_step_processor_ = std::thread([this]() { ProcessBufferReqSecondStep(); });
  ctrl_msg_processor_ = std::thread([this]() { ProcessCtrlMsg(); });
  buff_addr_idles_.resize(npu_mem_pools_.size());
  for (size_t i = 0; i < npu_mem_pools_.size(); ++i) {
    auto &npu_mem_pool = npu_mem_pools_[i];
    while (true) {
      auto dev_buffer = npu_mem_pool->Alloc(buffer_size_);
      if (dev_buffer == nullptr) {
        LLMLOGI("Allocated buff num:%zu.", buff_addr_idles_[i].size());
        break;
      }
      buff_addr_idles_[i].emplace(dev_buffer, true);
    }
  }
  return SUCCESS;
}

void BufferTransferService::Finalize() {
  stop_signal_.store(true);
  buffer_req_cv_.notify_all();
  if (buffer_req_processor_.joinable()) {
    buffer_req_processor_.join();
  }
  buffer_resp_cv_.notify_all();
  if (buffer_resp_processor_.joinable()) {
    buffer_resp_processor_.join();
  }
  buffer_second_step_cv_.notify_all();
  if (buffer_second_step_processor_.joinable()) {
    buffer_second_step_processor_.join();
  }
  ctrl_msg_cv_.notify_all();
  if (ctrl_msg_processor_.joinable()) {
    ctrl_msg_processor_.join();
  }
  for (size_t i = 0; i < npu_mem_pools_.size(); ++i) {
    auto &npu_mem_pool = npu_mem_pools_[i];
    for (auto &dev_buffer : buff_addr_idles_[i]) {
      npu_mem_pool->Free(dev_buffer.first);
    }
  }
}

Status BufferTransferService::Transfer(const ChannelPtr &channel, TransferType type,
                                       const std::vector<TransferOpDesc> &op_descs, int32_t timeout_in_millis) {
  uint64_t timeout = timeout_in_millis * kMillisToMicros;
  const auto start = std::chrono::steady_clock::now();
  auto req_id = next_req_id_.fetch_add(1);
  {
    std::lock_guard<std::mutex> lock(req_id_mutex_);
    req_id_buffers_.emplace(req_id, std::set<void *>());
  }
  LLM_MAKE_GUARD(req_id_guard, ([this, &req_id]() {
                   std::lock_guard<std::mutex> lock(req_id_mutex_);
                   for (auto &buffer_addr : req_id_buffers_[req_id]) {
                     ReleaseBuffer(buffer_addr);
                   }
                   req_id_buffers_.erase(req_id);
                 }));
  uint64_t time_cost =
      std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
  ADXL_CHK_BOOL_RET_STATUS(time_cost < timeout, TIMEOUT, "Transfer timeout.");
  auto left_timeout = timeout - time_cost;
  auto ret = DoTransferTask(channel, op_descs, left_timeout, type, req_id);
  LLMLOGI("DoTransferTask ret:%d.", ret);
  // wait until timeout to clear task in server
  time_cost = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
  ADXL_CHK_BOOL_RET_STATUS(time_cost < timeout, TIMEOUT, "Transfer timeout.");
  left_timeout = timeout - time_cost;
  ADXL_CHK_STATUS_RET(CheckReqFinishStatus(left_timeout, req_id), "Transfer failed.");
  LLMLOGI("Success to transfer with buffer type:%d, channel id:%s.", type, channel->GetChannelId().c_str());
  return SUCCESS;
}

Status BufferTransferService::DoTransferTask(const ChannelPtr &channel, const std::vector<TransferOpDesc> &op_descs,
                                             uint64_t timeout, TransferType type, uint64_t req_id) {
  const auto start = std::chrono::steady_clock::now();
  std::vector<TransferOpDesc> current_batch;
  size_t current_batch_size = 0;
  for (const auto &op_desc : op_descs) {
    uint64_t time_cost =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
    ADXL_CHK_BOOL_RET_STATUS(time_cost < timeout, TIMEOUT, "Transfer timeout.");
    auto left_timeout = timeout - time_cost;
    if (op_desc.len >= buffer_size_) {
      ADXL_CHK_STATUS_RET(TransferLargeData(channel, op_desc, left_timeout, req_id, type),
                          "Transfer large data failed");
      continue;
    }
    // small data, add to batch until buffer is full
    if (current_batch_size + op_desc.len > buffer_size_) {
      ADXL_CHK_STATUS_RET(FlushBatch(channel, current_batch, left_timeout, req_id, type), "Flush batch failed.");
      current_batch.clear();
      current_batch_size = 0;
    }
    current_batch.push_back(op_desc);
    current_batch_size += op_desc.len;
  }
  if (!current_batch.empty()) {
    uint64_t time_cost =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
    ADXL_CHK_BOOL_RET_STATUS(time_cost < timeout, TIMEOUT, "Transfer timeout.");
    auto left_timeout = timeout - time_cost;
    ADXL_CHK_STATUS_RET(FlushBatch(channel, current_batch, left_timeout, req_id, type), "Flush batch failed.");
  }
  return SUCCESS;
}

Status BufferTransferService::CheckReqFinishStatus(uint64_t timeout, uint64_t req_id) {
  const auto start = std::chrono::steady_clock::now();
  while (true) {
    {
      std::lock_guard<std::mutex> lock(req_id_mutex_);
      if (req_id_buffers_.find(req_id) == req_id_buffers_.end() || req_id_buffers_[req_id].empty()) {
        break;
      }
    }
    uint64_t time_cost =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
    ADXL_CHK_BOOL_RET_STATUS(time_cost < timeout, TIMEOUT, "Transfer timeout.");
  }
  return SUCCESS;
}

Status BufferTransferService::FlushBatch(const ChannelPtr &channel, const std::vector<TransferOpDesc> &op_descs,
                                         uint64_t timeout, uint64_t req_id, TransferType type) {
  auto start = std::chrono::steady_clock::now();
  void *dev_buffer;
  ADXL_CHK_STATUS_RET(TryGetBuffer(dev_buffer, timeout), "Failed to get buffer.");
  {
    std::lock_guard<std::mutex> lock(req_id_mutex_);
    req_id_buffers_[req_id].emplace(dev_buffer);
  }
  auto buffer_addr = llm::PtrToValue(dev_buffer);

  BufferReq buffer_req{};
  buffer_req.req_id = req_id;
  buffer_req.dst_addrs.reserve(op_descs.size());
  buffer_req.buffer_addr = buffer_addr;
  buffer_req.buffer_lens.reserve(op_descs.size());
  buffer_req.transfer_type = type;
  std::vector<uintptr_t> src_addrs;
  std::vector<uintptr_t> buffer_addrs;
  src_addrs.reserve(op_descs.size());
  buffer_addrs.reserve(op_descs.size());
  size_t total_buffer_len = 0;
  for (const auto &op_desc : op_descs) {
    buffer_req.dst_addrs.emplace_back(op_desc.remote_addr);
    buffer_req.buffer_lens.emplace_back(op_desc.len);
    src_addrs.emplace_back(op_desc.local_addr);
    buffer_addrs.emplace_back(buffer_addr);
    buffer_addr += op_desc.len;
    total_buffer_len += op_desc.len;
  }
  buffer_req.total_buffer_len = total_buffer_len;
  uint64_t time_cost =
      std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
  ADXL_CHK_BOOL_RET_STATUS(time_cost < timeout, TIMEOUT, "Transfer timeout.");
  auto left_timeout = timeout - time_cost;
  if (type == TransferType::kReadRH2H || type == TransferType::kReadRD2H || type == TransferType::kReadRH2D ||
      type == TransferType::kReadRD2D) {
    buffer_req.src_addrs = std::move(src_addrs);
  } else {
    auto kind = (type == TransferType::kWriteD2RH || type == TransferType::kWriteD2RD) ? RT_MEMCPY_DEVICE_TO_DEVICE
                                                                                       : RT_MEMCPY_HOST_TO_DEVICE;
    ADXL_CHK_STATUS_RET(ProcessCopy(channel, src_addrs, buffer_addrs, buffer_req.buffer_lens, kind, left_timeout),
                        "Copy failed");
  }
  ADXL_CHK_STATUS_RET(SendBufferReq(channel, buffer_req, timeout, start), "Send req failed.");
  return SUCCESS;
}

Status BufferTransferService::TransferLargeData(const ChannelPtr &channel, const TransferOpDesc &op_desc,
                                                uint64_t timeout, uint64_t req_id, TransferType type) {
  auto start = std::chrono::steady_clock::now();
  auto left_size = op_desc.len;
  auto addr = op_desc.local_addr;
  auto remote_addr = op_desc.remote_addr;
  auto left_timeout = timeout;
  while (left_size > 0) {
    void *dev_buffer;
    ADXL_CHK_STATUS_RET(TryGetBuffer(dev_buffer, left_timeout), "Failed to get buffer.");
    {
      std::lock_guard<std::mutex> lock(req_id_mutex_);
      req_id_buffers_[req_id].emplace(dev_buffer);
    }
    auto count = std::min(buffer_size_, left_size);
    auto dev_buffer_addr = llm::PtrToValue(dev_buffer);
    BufferReq buffer_req{type,  req_id,          0,    {addr}, dev_buffer_addr, {dev_buffer_addr}, {count},
                         count, dev_buffer_addr, start};
    auto buffer_addrs = GenerateBufferReq(buffer_req, addr, remote_addr, dev_buffer_addr, count);
    uint64_t time_cost =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
    ADXL_CHK_BOOL_RET_STATUS(time_cost < timeout, TIMEOUT, "Transfer timeout.");
    left_timeout = timeout - time_cost;
    if (type == TransferType::kWriteH2RH || type == TransferType::kWriteH2RD || type == TransferType::kWriteD2RH ||
        type == TransferType::kWriteD2RD) {
      auto kind = (type == TransferType::kWriteD2RH || type == TransferType::kWriteD2RD) ? RT_MEMCPY_DEVICE_TO_DEVICE
                                                                                         : RT_MEMCPY_HOST_TO_DEVICE;
      ADXL_CHK_STATUS_RET(
          ProcessCopy(channel, buffer_req.src_addrs, buffer_addrs, buffer_req.buffer_lens, kind, left_timeout),
          "Copy failed");
      buffer_req.src_addrs = {};
    }
    ADXL_CHK_STATUS_RET(SendBufferReq(channel, buffer_req, timeout, start), "Send req failed.");
    left_size -= count;
    addr += count;
    remote_addr += count;
    time_cost = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
    ADXL_CHK_BOOL_RET_STATUS(time_cost < timeout, TIMEOUT, "Transfer timeout.");
    left_timeout = timeout - time_cost;
  }
  return SUCCESS;
}

std::vector<uintptr_t> BufferTransferService::GenerateBufferReq(BufferReq &buffer_req, uintptr_t addr,
                                                                uintptr_t remote_addr, uintptr_t dev_buffer_addr,
                                                                uint64_t count) {
  std::vector<uintptr_t> buffer_addrs = {dev_buffer_addr};
  buffer_addrs.clear();
  buffer_req.src_addrs.clear();
  buffer_req.dst_addrs.clear();
  buffer_req.buffer_lens.clear();
  auto src_addr = addr;
  auto dst_addr = remote_addr;
  while (count > 0) {
    auto block_count = std::min(kBlockSize, count);
    buffer_req.src_addrs.emplace_back(src_addr);
    buffer_req.buffer_lens.emplace_back(block_count);
    buffer_req.dst_addrs.emplace_back(dst_addr);
    buffer_addrs.emplace_back(dev_buffer_addr);
    count -= block_count;
    src_addr += block_count;
    dst_addr += block_count;
    dev_buffer_addr += block_count;
  }
  return buffer_addrs;
}

void BufferTransferService::ProcessBufferReqFirstStep() {
  rtCtxSetCurrent(rt_context_);
  while (!stop_signal_.load()) {
    std::pair<ChannelPtr, BufferReq> req;
    {
      std::unique_lock<std::mutex> lock(buffer_req_mutex_);
      buffer_req_cv_.wait(lock, [this] { return !buffer_req_queue_.empty() || stop_signal_.load(); });
      if (stop_signal_.load()) {
        break;
      }
      req = std::move(buffer_req_queue_.front());
      buffer_req_queue_.pop();
    }
    auto &buffer_req = req.second;
    if (!CheckTimeout(buffer_req)) {
      auto type = buffer_req.transfer_type;
      (type == TransferType::kReadRH2H || type == TransferType::kReadRD2H || type == TransferType::kReadRH2D ||
       type == TransferType::kReadRD2D)
          ? HandleBufferCopy(req.first, req.second)
          : HandleBufferD2D(req.first, req.second);
    }
  }
}

void BufferTransferService::ProcessBufferReqSecondStep() {
  rtCtxSetCurrent(rt_context_);
  while (!stop_signal_.load()) {
    std::pair<ChannelPtr, BufferReq> req;
    {
      std::unique_lock<std::mutex> lock(buffer_second_step_mutex_);
      buffer_second_step_cv_.wait(lock, [this] { return !buffer_second_step_queue_.empty() || stop_signal_.load(); });
      if (stop_signal_.load()) {
        break;
      }
      req = std::move(buffer_second_step_queue_.front());
      buffer_second_step_queue_.pop();
    }
    auto &buffer_req = req.second;
    if (!CheckTimeout(buffer_req)) {
      auto type = buffer_req.transfer_type;
      (type == TransferType::kReadRH2H || type == TransferType::kReadRD2H || type == TransferType::kReadRH2D ||
       type == TransferType::kReadRD2D)
          ? HandleBufferD2D(req.first, req.second)
          : HandleBufferCopy(req.first, req.second);
    }
  }
}

Status BufferTransferService::HandleBufferD2D(const ChannelPtr &channel, BufferReq &buffer_req) {
  LLMLOGI("Processing BufferReq for channel:%s, type:%d", channel->GetChannelId().c_str(), buffer_req.transfer_type);
  ADXL_CHK_BOOL_RET_STATUS(buffer_req.total_buffer_len <= buffer_size_, PARAM_INVALID,
                           "Total buffer length:%lu is bigger than buffer size:%lu.", buffer_req.total_buffer_len,
                           buffer_size_);
  LLM_DISMISSABLE_GUARD(
      failed_guard, ([this, &buffer_req]() { ReleaseServerBuffer(llm::ValueToPtr(buffer_req.local_buffer_addr)); }));
  const auto start = std::chrono::steady_clock::now();
  const auto timeout = buffer_req.timeout;

  auto type = buffer_req.transfer_type;
  auto is_write = (type == TransferType::kWriteH2RH || type == TransferType::kWriteH2RD ||
                   type == TransferType::kWriteD2RH || type == TransferType::kWriteD2RD);
  TransferOp op = TransferOp::WRITE;
  if (is_write) {
    op = TransferOp::READ;
    void *dev_buffer = nullptr;
    ADXL_CHK_STATUS_RET(TryGetServerBuffer(dev_buffer, timeout));
    buffer_req.local_buffer_addr = llm::PtrToValue(dev_buffer);
  }
  uint64_t time_cost =
      std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
  ADXL_CHK_BOOL_RET_STATUS(time_cost < timeout, TIMEOUT, "Transfer timeout.");
  auto left_time = timeout - time_cost;
  std::vector<TransferOpDesc> op_descs{
      TransferOpDesc{buffer_req.local_buffer_addr, buffer_req.buffer_addr, buffer_req.total_buffer_len}};
  ADXL_CHK_STATUS_RET(D2DTransfer(channel, op, op_descs, left_time, start), "D2D transfer failed.");

  time_cost = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
  ADXL_CHK_BOOL_RET_STATUS(time_cost < timeout, TIMEOUT, "Transfer timeout.");
  left_time = timeout - time_cost;
  buffer_req.timeout = left_time;
  LLM_DISMISS_GUARD(failed_guard);
  if (is_write) {
    PushSecondStepReq(channel, buffer_req);
  } else {
    PushCtrlMsg(channel, buffer_req);
  }
  return SUCCESS;
}

Status BufferTransferService::D2DTransfer(const ChannelPtr &channel, TransferOp transfer_op,
                                          std::vector<TransferOpDesc> &op_descs, uint64_t timeout,
                                          const std::chrono::steady_clock::time_point &start) {
  auto &stream = channel->GetStream();
  ADXL_CHK_STATUS_RET(channel->TransferAsync(transfer_op, op_descs, stream), "transfer failed.");
  uint64_t time_cost =
      std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
  ADXL_CHK_BOOL_RET_STATUS(time_cost < timeout, TIMEOUT, "Transfer timeout.");
  auto left_timeout = timeout - time_cost;
  auto timeout_in_millis = left_timeout / kMillisToMicros;
  ADXL_CHK_BOOL_RET_STATUS(timeout_in_millis > 0, TIMEOUT, "Transfer timeout.");
  ADXL_CHK_ACL_RET(rtStreamSynchronizeWithTimeout(stream, timeout_in_millis));
  LLMLOGI("D2D time cost:%lu us, data num:%zu.",
          std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count(),
          op_descs.size());
  return SUCCESS;
}

Status BufferTransferService::HandleBufferCopy(const ChannelPtr &channel, BufferReq &buffer_req) {
  ADXL_CHK_BOOL_RET_STATUS(buffer_req.total_buffer_len <= buffer_size_, PARAM_INVALID,
                           "Total buffer length:%lu is bigger than buffer size:%lu.", buffer_req.total_buffer_len,
                           buffer_size_);
  LLM_DISMISSABLE_GUARD(
      failed_guard, ([this, &buffer_req]() { ReleaseServerBuffer(llm::ValueToPtr(buffer_req.local_buffer_addr)); }));
  auto start = std::chrono::steady_clock::now();
  auto left_timeout = buffer_req.timeout;
  auto type = buffer_req.transfer_type;
  auto is_read = (type == TransferType::kReadRH2H || type == TransferType::kReadRD2H ||
                  type == TransferType::kReadRH2D || type == TransferType::kReadRD2D);
  if (is_read) {
    void *dev_buffer = nullptr;
    ADXL_CHK_STATUS_RET(TryGetServerBuffer(dev_buffer, buffer_req.timeout));
    buffer_req.local_buffer_addr = llm::PtrToValue(dev_buffer);

    uint64_t time_cost =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    ADXL_CHK_BOOL_RET_STATUS(time_cost < buffer_req.timeout, TIMEOUT, "Transfer timeout.");
    left_timeout = buffer_req.timeout - time_cost;
  }
  std::vector<uintptr_t> copy_buff_addrs;
  copy_buff_addrs.reserve(buffer_req.dst_addrs.size());
  auto dev_buffer_addr = buffer_req.local_buffer_addr;
  for (size_t i = 0; i < buffer_req.dst_addrs.size(); ++i) {
    copy_buff_addrs.emplace_back(dev_buffer_addr);
    dev_buffer_addr += buffer_req.buffer_lens[i];
  }
  if (is_read) {
    auto kind = (type == TransferType::kReadRD2H || type == TransferType::kReadRD2D) ? RT_MEMCPY_DEVICE_TO_DEVICE
                                                                                     : RT_MEMCPY_HOST_TO_DEVICE;
    ADXL_CHK_STATUS_RET(
        ProcessCopy(channel, buffer_req.dst_addrs, copy_buff_addrs, buffer_req.buffer_lens, kind, left_timeout),
        "Copy failed.");
  } else {
    auto kind = (type == TransferType::kWriteH2RD || type == TransferType::kWriteD2RD) ? RT_MEMCPY_DEVICE_TO_DEVICE
                                                                                       : RT_MEMCPY_DEVICE_TO_HOST;
    ADXL_CHK_STATUS_RET(
        ProcessCopy(channel, copy_buff_addrs, buffer_req.dst_addrs, buffer_req.buffer_lens, kind, left_timeout),
        "Copy failed.");
  }
  LLMLOGI("Copy time cost:%lu us",
          std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count());
  uint64_t time_cost =
      std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
  ADXL_CHK_BOOL_RET_STATUS(time_cost < buffer_req.timeout, TIMEOUT, "Transfer timeout.");
  auto left_time = buffer_req.timeout - time_cost;
  buffer_req.timeout = left_time;
  LLM_DISMISS_GUARD(failed_guard);
  if (is_read) {
    PushSecondStepReq(channel, buffer_req);
  } else {
    PushCtrlMsg(channel, buffer_req);
  }
  return SUCCESS;
}

Status BufferTransferService::ProcessCopy(const ChannelPtr &channel, const std::vector<uintptr_t> &src_addrs,
                                          const std::vector<uintptr_t> &dst_addrs, std::vector<size_t> &sizes,
                                          rtMemcpyKind_t kind, uint64_t timeout) {
  if (src_addrs.size() == 1U) {
    ADXL_CHK_ACL_RET(rtMemcpy(llm::ValueToPtr(dst_addrs[0]), sizes[0], llm::ValueToPtr(src_addrs[0]), sizes[0], kind));
  } else if ((kind == RT_MEMCPY_DEVICE_TO_DEVICE) || !support_batch_copy_batch_) {
    return ProcessCopyWithAsync(channel, src_addrs, dst_addrs, sizes, kind, timeout);
  } else {
    std::vector<void *> void_dst_addrs(dst_addrs.size());
    std::vector<void *> void_src_addrs(dst_addrs.size());
    std::vector<rtMemcpyBatchAttr> attrs(dst_addrs.size());
    std::vector<size_t> attrsIds(dst_addrs.size());
    size_t idx = 0;
    for (size_t i = 0; i < dst_addrs.size(); i++) {
      auto loc = rtMemLocation{static_cast<uint32_t>(device_id_), rtMemLocationType::RT_MEMORY_LOC_DEVICE};
      auto another_loc = rtMemLocation{0, rtMemLocationType::RT_MEMORY_LOC_HOST};
      if (kind == RT_MEMCPY_DEVICE_TO_HOST) {
        attrs[i] = rtMemcpyBatchAttr{another_loc, loc, {}};
      } else {
        attrs[i] = rtMemcpyBatchAttr{loc, another_loc, {}};
      }
      attrsIds[i] = idx++;
      void_src_addrs[i] = llm::ValueToPtr(src_addrs[i]);
      void_dst_addrs[i] = llm::ValueToPtr(dst_addrs[i]);
    }
    size_t fail_idx;
    auto ret = rtsMemcpyBatch(void_dst_addrs.data(), void_src_addrs.data(), sizes.data(), dst_addrs.size(),
                              attrs.data(), attrsIds.data(), attrs.size(), &fail_idx);
    if (ret == ACL_ERROR_RT_FEATURE_NOT_SUPPORT) {
      // fallback
      support_batch_copy_batch_ = false;
      return ProcessCopyWithAsync(channel, src_addrs, dst_addrs, sizes, kind, timeout);
    }
    return ret == RT_ERROR_NONE ? SUCCESS : FAILED;
  }
  return SUCCESS;
}

Status BufferTransferService::ProcessCopyWithAsync(const ChannelPtr &channel, const std::vector<uintptr_t> &src_addrs,
                                                   const std::vector<uintptr_t> &dst_addrs, std::vector<size_t> &sizes,
                                                   rtMemcpyKind_t kind, uint64_t timeout) {
  auto &stream = channel->GetStream();
  auto start = std::chrono::steady_clock::now();
  for (size_t i = 0; i < src_addrs.size(); ++i) {
    ADXL_CHK_ACL_RET(
        rtMemcpyAsync(llm::ValueToPtr(dst_addrs[i]), sizes[i], llm::ValueToPtr(src_addrs[i]), sizes[i], kind, stream));
  }
  uint64_t time_cost =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
  ADXL_CHK_BOOL_RET_STATUS(time_cost < timeout, TIMEOUT, "Transfer timeout.");
  auto left_timeout = timeout - time_cost;
  auto timeout_in_millis = left_timeout / kMillisToMicros;
  ADXL_CHK_BOOL_RET_STATUS(timeout_in_millis > 0, TIMEOUT, "Transfer timeout.");
  ADXL_CHK_ACL_RET(rtStreamSynchronizeWithTimeout(stream, timeout_in_millis));
  return SUCCESS;
}

void BufferTransferService::ProcessCtrlMsg() {
  while (!stop_signal_.load()) {
    std::pair<ChannelPtr, BufferReq> buffer_req;
    {
      std::unique_lock<std::mutex> lock(buffer_ctrl_msg_mutex_);
      ctrl_msg_cv_.wait(lock, [this] { return !buffer_ctrl_msg_queue_.empty() || stop_signal_.load(); });
      if (stop_signal_.load()) {
        break;
      }
      buffer_req = std::move(buffer_ctrl_msg_queue_.front());
      buffer_ctrl_msg_queue_.pop();
    }
    HandleCtrlMsg(buffer_req.first, buffer_req.second);
  }
}

Status BufferTransferService::HandleCtrlMsg(const ChannelPtr &channel, const BufferReq &buffer_req) {
  auto start = std::chrono::steady_clock::now();
  // free local buffer
  ReleaseServerBuffer(llm::ValueToPtr(buffer_req.local_buffer_addr));
  BufferResp buffer_resp{};
  buffer_resp.transfer_type = buffer_req.transfer_type;
  buffer_resp.req_id = buffer_req.req_id;
  buffer_resp.buffer_addr = buffer_req.buffer_addr;
  buffer_resp.src_addrs = std::move(buffer_req.src_addrs);
  buffer_resp.buffer_lens = std::move(buffer_req.buffer_lens);
  buffer_resp.timeout = buffer_req.timeout;
  auto func = [&buffer_resp, &buffer_req, &start](int32_t fd) {
    uint64_t time_cost =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
    ADXL_CHK_BOOL_RET_STATUS(time_cost < buffer_req.timeout, TIMEOUT, "Transfer timeout.");
    return ControlMsgHandler::SendMsg(fd, ControlMsgType::kBufferResp, buffer_resp, buffer_req.timeout - time_cost);
  };
  ADXL_CHK_STATUS_RET(channel->SendControlMsg(func), "Send resp msg failed.");
  return SUCCESS;
}

void BufferTransferService::ProcessBufferResp() {
  rtCtxSetCurrent(rt_context_);
  while (!stop_signal_.load()) {
    std::pair<ChannelPtr, BufferResp> resp;
    {
      std::unique_lock<std::mutex> lock(buffer_resp_mutex_);
      buffer_resp_cv_.wait(lock, [this] { return !buffer_resp_queue_.empty() || stop_signal_.load(); });
      if (stop_signal_.load()) {
        break;
      }
      resp = std::move(buffer_resp_queue_.front());
      buffer_resp_queue_.pop();
    }
    HandleBufferResp(resp.first, resp.second);
  }
}

Status BufferTransferService::HandleBufferResp(const ChannelPtr &channel, BufferResp &buffer_resp) {
  std::lock_guard<std::mutex> req_id_lock(req_id_mutex_);
  if (req_id_buffers_.find(buffer_resp.req_id) != req_id_buffers_.end()) {
    auto type = buffer_resp.transfer_type;
    auto is_read = (type == TransferType::kReadRH2H || type == TransferType::kReadRD2H ||
                    type == TransferType::kReadRH2D || type == TransferType::kReadRD2D);
    if (is_read && buffer_resp.src_addrs.size() > 0) {
      ADXL_CHK_BOOL_RET_STATUS(buffer_resp.src_addrs.size() == buffer_resp.buffer_lens.size(), FAILED,
                               "Addr not valid.");
      std::vector<uintptr_t> buffer_addrs;
      buffer_addrs.reserve(buffer_resp.src_addrs.size());
      auto buffer_addr = buffer_resp.buffer_addr;
      for (size_t i = 0; i < buffer_resp.src_addrs.size(); ++i) {
        buffer_addrs.emplace_back(buffer_addr);
        buffer_addr += buffer_resp.buffer_lens[i];
      }
      auto kind = (type == TransferType::kReadRH2D || type == TransferType::kReadRD2D) ? RT_MEMCPY_DEVICE_TO_DEVICE
                                                                                       : RT_MEMCPY_DEVICE_TO_HOST;
      ADXL_CHK_STATUS_RET(
          ProcessCopy(channel, buffer_addrs, buffer_resp.src_addrs, buffer_resp.buffer_lens, kind, buffer_resp.timeout),
          "Copy failed.");
    }
    auto ptr = llm::ValueToPtr(buffer_resp.buffer_addr);
    ReleaseBuffer(ptr);
    req_id_buffers_[buffer_resp.req_id].erase(ptr);
  }
  LLMLOGI("Recv resp, req id:%lu.", buffer_resp.req_id);
  return SUCCESS;
}

void BufferTransferService::PushBufferReq(const ChannelPtr &channel, BufferReq &buffer_req) {
  {
    std::lock_guard<std::mutex> lock(buffer_req_mutex_);
    buffer_req.recv_start_time = std::chrono::steady_clock::now();
    buffer_req.timeout = buffer_req.timeout - kTimeoutLoss;
    buffer_req_queue_.emplace(channel, buffer_req);
  }
  buffer_req_cv_.notify_one();
}

void BufferTransferService::PushSecondStepReq(const ChannelPtr &channel, BufferReq &buffer_req) {
  {
    std::lock_guard<std::mutex> lock(buffer_second_step_mutex_);
    buffer_req.recv_start_time = std::chrono::steady_clock::now();
    buffer_req.timeout = buffer_req.timeout - kTimeoutLoss;
    buffer_second_step_queue_.emplace(channel, buffer_req);
  }
  buffer_second_step_cv_.notify_one();
}

void BufferTransferService::PushCtrlMsg(const ChannelPtr &channel, BufferReq &buffer_req) {
  {
    std::lock_guard<std::mutex> lock(buffer_ctrl_msg_mutex_);
    buffer_ctrl_msg_queue_.emplace(channel, buffer_req);
  }
  ctrl_msg_cv_.notify_one();
}

void BufferTransferService::PushBufferResp(const ChannelPtr &channel, const BufferResp &buffer_resp) {
  {
    std::lock_guard<std::mutex> lock(buffer_resp_mutex_);
    buffer_resp_queue_.emplace(channel, buffer_resp);
  }
  buffer_resp_cv_.notify_one();
}

Status BufferTransferService::TryGetBuffer(void *&buffer_addr, uint64_t timeout, size_t pool_index) {
  auto start = std::chrono::steady_clock::now();
  while (true) {
    {
      auto &mutex = (pool_index == 0) ? buff_idle_mutex_ : server_buff_idle_mutex_;
      std::lock_guard<std::mutex> lock(mutex);
      for (auto &dev_buffer : buff_addr_idles_[pool_index]) {
        if (dev_buffer.second) {
          dev_buffer.second = false;
          buffer_addr = dev_buffer.first;
          return SUCCESS;
        }
      }
    }
    uint64_t time_cost =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
    ADXL_CHK_BOOL_RET_STATUS(time_cost < timeout, TIMEOUT, "Get buffer addr timeout.");
  }
}

void BufferTransferService::ReleaseBuffer(void *buffer_addr, size_t pool_index) {
  if (buffer_addr == nullptr) {
    return;
  }
  auto &mutex = (pool_index == 0) ? buff_idle_mutex_ : server_buff_idle_mutex_;
  std::lock_guard<std::mutex> buff_idle_lock(mutex);
  auto &buff_addr_idle = buff_addr_idles_[pool_index];
  if (buff_addr_idle.find(buffer_addr) != buff_addr_idle.end()) {
    buff_addr_idle[buffer_addr] = true;
  }
}

Status BufferTransferService::TryGetServerBuffer(void *&buffer_addr, uint64_t timeout) {
  return TryGetBuffer(buffer_addr, timeout, kServerPoolIndex);
}

void BufferTransferService::ReleaseServerBuffer(void *buffer_addr) {
  return ReleaseBuffer(buffer_addr, kServerPoolIndex);
}

Status BufferTransferService::SendBufferReq(const ChannelPtr &channel, BufferReq &buffer_req, uint64_t timeout,
                                            std::chrono::steady_clock::time_point start) {
  uint64_t time_cost =
      std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
  LLMLOGI("Time cost:%lu us.", time_cost);
  ADXL_CHK_BOOL_RET_STATUS(time_cost < timeout, TIMEOUT, "Transfer timeout.");
  buffer_req.timeout = timeout - time_cost;
  auto func = [&buffer_req](int32_t fd) {
    return ControlMsgHandler::SendMsg(fd, ControlMsgType::kBufferReq, buffer_req, buffer_req.timeout);
  };
  ADXL_CHK_STATUS_RET(channel->SendControlMsg(func), "Send req msg failed.");
  LLMLOGI("End send buffer req control msg, req id:%u.", buffer_req.req_id);
  return SUCCESS;
}

bool BufferTransferService::CheckTimeout(const BufferReq &req) {
  uint64_t time_cost =
      std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - req.recv_start_time)
          .count();
  return time_cost >= req.timeout;
}
}  // namespace adxl