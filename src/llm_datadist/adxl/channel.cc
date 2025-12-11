/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "channel.h"
#include <mutex>
#include <fcntl.h>
#include <unistd.h>
#include "adxl/adxl_utils.h"
#include "common/llm_inner_types.h"
#include "common/llm_scope_guard.h"
#include "common/def_types.h"

#include <base/err_msg.h>

namespace adxl {
namespace {
// hccl HcclCommInitClusterInfoMemConfig not support parallel call, so use mutex to protect it
std::mutex g_mutex_;
constexpr uint32_t kMaxOpDescNum = 256U;
constexpr int64_t kHeartbeatTimeoutInMillis = 120000;
constexpr int32_t kMillisToMicros = 1000;
}

int64_t Channel::timeout_in_millis_ = kHeartbeatTimeoutInMillis;

Status Channel::Initialize() {
  LLMLOGI("HcclCommInitClusterInfoMemConfig begin, comm_name=%s, local rank_id=%u, rank_table=%s",
         channel_info_.comm_config.hcclCommName, channel_info_.local_rank_id, channel_info_.rank_table.c_str());
  {
    std::lock_guard<std::mutex> lock(g_mutex_);
    ADXL_CHK_HCCL_RET(llm::HcclAdapter::GetInstance().HcclCommInitClusterInfoMemConfig(
        channel_info_.rank_table.c_str(),
        channel_info_.local_rank_id,
        &channel_info_.comm_config,
        &channel_info_.comm));
  }
  std::vector<void *> bind_handles;
  LLM_DISMISSABLE_GUARD(fail_guard, ([this, &bind_handles]() {
    for (auto bind_handle : bind_handles) {
      (void) llm::HcclAdapter::GetInstance().HcclCommUnbindMem(channel_info_.comm, bind_handle);
    }
    (void) llm::HcclAdapter::GetInstance().HcclCommDestroy(channel_info_.comm);
  }));

  for (const auto &reg_handle_it : channel_info_.registered_mems) {
    auto reg_handle = reg_handle_it.first;
    ADXL_CHK_HCCL_RET(llm::HcclAdapter::GetInstance().HcclCommBindMem(channel_info_.comm, reg_handle));
    bind_handles.emplace_back(reg_handle);
  }

  const auto start = std::chrono::steady_clock::now();
  HcclPrepareConfig prepareConfig{};
  ADXL_CHK_HCCL_RET(llm::HcclAdapter::GetInstance().HcclCommPrepare(channel_info_.comm, &prepareConfig,
                                                                    channel_info_.timeout_sec));
  auto cost = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
  LLMLOGI("HcclCommPrepare success, cost=%ld ms.", cost);
  LLM_DISMISS_GUARD(fail_guard);
  return SUCCESS;
}

std::string Channel::GetChannelId() const {
  return channel_info_.channel_id;
}

void Channel::ClearNotifyMessages() {
  {
    std::lock_guard<std::mutex> notify_lock(notify_message_mutex_);
    notify_messages_.clear();
  }
}

Status Channel::Finalize() {
  auto ret = SUCCESS;
  {
    std::lock_guard<std::mutex> lock(transfer_mutex_);
    for (const auto &reg_handle_it : channel_info_.registered_mems) {
      auto reg_handle = reg_handle_it.first;
      auto hccl_ret = llm::HcclAdapter::GetInstance().HcclCommUnbindMem(channel_info_.comm, reg_handle);
      ret = hccl_ret != HcclResult::HCCL_SUCCESS ? FAILED : ret;
    }

    auto hccl_ret = llm::HcclAdapter::GetInstance().HcclCommDestroy(channel_info_.comm);
    ret = hccl_ret != HcclResult::HCCL_SUCCESS ? FAILED : ret;

    std::lock_guard<std::mutex> transfer_reqs_lock(transfer_reqs_mutex_);
    for (const auto &transfer_req : transfer_reqs_) {
      rtEvent_t event = transfer_req.second.first;
      rtStream_t stream = transfer_req.second.second;
      if (event != nullptr) {
        auto rt_ret = rtEventDestroy(event);
        if (rt_ret != RT_ERROR_NONE) {
          LLMLOGE(FAILED, "Call rtEventDestroy ret:%d.", rt_ret);
          ret = FAILED;
        }
      }
      //during exceptional scenarios, destroy the stream when destroying the channel.
      if (stream != nullptr) {
        stream_pool_->DestroyStream(stream);
      }
    }
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (fd_ > 0) {
      (void) close(fd_);
      fd_ = -1;
    }
    with_heartbeat_.store(false, std::memory_order_release);
  }
  ClearNotifyMessages();
  return ret;
}

void Channel::SetStreamPool(StreamPool *stream_pool) {
  stream_pool_ = stream_pool;
}

Status Channel::TransferAsync(TransferOp operation,
                              const std::vector<TransferOpDesc> &op_descs,
                              const TransferArgs &optional_args,
                              TransferReq &req) {
  (void)optional_args;
  rtStream_t stream = nullptr;
  ADXL_CHK_STATUS_RET(stream_pool_->TryAllocStream(stream), "Stream pool get stream failed.");
  auto id = reinterpret_cast<uint64_t>(req);
  rtEvent_t event = nullptr;
  LLM_DISMISSABLE_GUARD(fail_guard, ([&]() {
    if (event != nullptr) {
      rtEventDestroy(event);
    }
    if (stream != nullptr) {
      stream_pool_->DestroyStream(stream);
    }
  }));
  ADXL_CHK_STATUS_RET(TransferAsync(operation, op_descs, stream), "Channel transfer async failed.");
  LLM_CHK_ACL_RET(rtEventCreate(&event));
  LLM_CHK_ACL_RET(rtEventRecord(event, stream));
  std::lock_guard<std::mutex> lock(transfer_reqs_mutex_);
  transfer_reqs_[id] = std::make_pair(event, stream);
  LLM_DISMISS_GUARD(fail_guard);
  return SUCCESS;
}

Status Channel::GetTransferStatus(const TransferReq &req, TransferStatus &status) {
  std::lock_guard<std::mutex> lock(transfer_reqs_mutex_);
  auto id = reinterpret_cast<uint64_t>(req);
  auto it = transfer_reqs_.find(id);
  if (it == transfer_reqs_.end()) {
    status = TransferStatus::FAILED;
    LLMLOGE(FAILED, "Request not found, req:%llu.", id);
    return FAILED;
  }

  auto event = it->second.first;
  auto stream = it->second.second;
  rtEventStatus_t event_status{};
  auto ret = rtEventQueryStatus(event, &event_status);
  if (ret != RT_ERROR_NONE) {
    LLMLOGE(FAILED, "rtEventQueryStatus failed for req:%llu, ret:%d.", id, ret);
    rtEventDestroy(event);
    stream_pool_->DestroyStream(stream);
    transfer_reqs_.erase(id);
    status = TransferStatus::FAILED;
    return FAILED;
  }
  if (event_status != RT_EVENT_RECORDED) {
    LLMLOGI("Transfer async request not yet completed, req:%llu.", id);
    status = TransferStatus::WAITING;
    return SUCCESS;
  }
  auto steam_status = rtStreamSynchronize(stream);
  if (steam_status != RT_ERROR_NONE) {
    //stream syncronize failed
    status = TransferStatus::FAILED;
    rtEventDestroy(event);
    stream_pool_->DestroyStream(stream);
    transfer_reqs_.erase(id);
    LLMLOGE(FAILED, "rtStreamSyncronize failed for req:%llu, ret:%d.", id, steam_status);
    return FAILED;
  }
  LLMLOGI("Transfer async request completed, req:%llu.", id);
  status = TransferStatus::COMPLETED;
  rtEventDestroy(event);
  stream_pool_->FreeStream(stream);
  transfer_reqs_.erase(id);
  return SUCCESS;
}

Status Channel::TransferAsync(TransferOp operation, const std::vector<TransferOpDesc> &op_descs,
                              rtStream_t stream) {
  auto trans_func = [this, operation, &stream](HcclOneSideOpDesc *descs, uint32_t desc_num) -> Status {
    HcclResult ret = HCCL_SUCCESS;
    if (operation == READ) {
      ret = llm::HcclAdapter::GetInstance().HcclBatchGet(channel_info_.comm, channel_info_.peer_rank_id,
                                                         descs, desc_num, stream);
    } else {
      ret = llm::HcclAdapter::GetInstance().HcclBatchPut(channel_info_.comm, channel_info_.peer_rank_id,
                                                         descs, desc_num, stream);
    }
    ADXL_CHK_BOOL_RET_STATUS(ret == HCCL_SUCCESS,
                             HcclError2AdxlStatus(ret),
                             "Failed to invoke %s, hccl_result = %d",
                             operation == READ ? "HcclBatchGet" : "HcclBatchPut", static_cast<int32_t>(ret));
    return SUCCESS;
  };
  BufferedTransfer transfer(trans_func);
  ADXL_CHK_STATUS_RET(transfer.Put(op_descs), "Failed to batch transfer");
  return SUCCESS;
}

Status Channel::TransferAsyncWithTimeout(TransferOp operation, const std::vector<TransferOpDesc> &op_descs,
                                         rtStream_t stream, uint64_t timeout) {
  const auto start = std::chrono::steady_clock::now();
  std::vector<HcclOneSideOpDesc> hccl_op_descs;
  hccl_op_descs.reserve(kMaxOpDescNum);
  for (size_t i = 0; i < op_descs.size(); ++i) {
    auto &desc = op_descs[i];
    HcclOneSideOpDesc hccl_op_desc{};
    hccl_op_desc.localAddr = llm::ValueToPtr(desc.local_addr);
    hccl_op_desc.remoteAddr = llm::ValueToPtr(desc.remote_addr);
    hccl_op_desc.count = desc.len;
    hccl_op_desc.dataType = HCCL_DATA_TYPE_UINT8;
    hccl_op_descs.emplace_back(hccl_op_desc);
    if (hccl_op_descs.size() == hccl_op_descs.capacity() || i == op_descs.size() - 1) {
      const auto end = std::chrono::steady_clock::now();
      uint64_t cost = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
      ADXL_CHK_BOOL_RET_STATUS(cost < timeout, TIMEOUT, "Transfer timeout.");
      HcclResult ret = HCCL_SUCCESS;
      if (operation == READ) {
        ret = llm::HcclAdapter::GetInstance().HcclBatchGet(channel_info_.comm, channel_info_.peer_rank_id,
                                                           hccl_op_descs.data(), hccl_op_descs.size(), stream);
      } else {
        ret = llm::HcclAdapter::GetInstance().HcclBatchPut(channel_info_.comm, channel_info_.peer_rank_id,
                                                           hccl_op_descs.data(), hccl_op_descs.size(), stream);
      }
      ADXL_CHK_BOOL_RET_STATUS(ret == HCCL_SUCCESS, HcclError2AdxlStatus(ret), "Failed to invoke %s, hccl_result = %d",
                               operation == READ ? "HcclBatchGet" : "HcclBatchPut", static_cast<int32_t>(ret));
      hccl_op_descs.clear();
    }
  }
  return SUCCESS;
}

Status Channel::TransferSync(TransferOp operation,
                             const std::vector<TransferOpDesc> &op_descs,
                             int32_t timeout_in_millis) {
  const auto start = std::chrono::steady_clock::now();
  rtStream_t stream = nullptr;
  ADXL_CHK_STATUS_RET(stream_pool_->TryAllocStream(stream), "Stream pool get stream failed.");
  LLM_DISMISSABLE_GUARD(fail_guard, ([this, &stream]() {
    if (stream != nullptr) {
      this->stream_pool_->DestroyStream(stream);
    }
  }));
  ADXL_CHK_STATUS_RET(TransferAsync(operation, op_descs, stream), "Transfer failed.");

  ADXL_CHK_ACL_RET(rtStreamSynchronizeWithTimeout(stream, timeout_in_millis));
  const auto end = std::chrono::steady_clock::now();
  const auto cost = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
  LLMLOGI("%s success, num = %zu, cost = %ld us.",
         operation == READ ? "HcclBatchGet" : "HcclBatchPut", op_descs.size(), cost);
  LLM_DISMISS_GUARD(fail_guard);
  stream_pool_->FreeStream(stream);
  return SUCCESS;
}

Status Channel::SetSocketNonBlocking(int32_t fd) {
  std::lock_guard<std::mutex> lock(mutex_);
  int flags = fcntl(fd, F_GETFL, 0);
  ADXL_CHK_BOOL_RET_STATUS(flags != -1, FAILED, "Failed to get fd flags: %s", strerror(errno));

  ADXL_CHK_BOOL_RET_STATUS(fcntl(fd, F_SETFL, static_cast<uint32_t>(flags) | static_cast<uint32_t>(O_NONBLOCK)) != -1,
                           FAILED, "Failed to set fd to non-blocking: %s", strerror(errno));

  fd_ = fd;
  last_heartbeat_time_ = std::chrono::steady_clock::now();
  with_heartbeat_.store(true, std::memory_order_release);
  return SUCCESS;
}

void Channel::StopHeartbeat() {
  std::lock_guard<std::mutex> lock(mutex_);
  with_heartbeat_.store(false, std::memory_order_release);
}

Status Channel::CommWithFd(const std::function<Status(int32_t)> &func) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (fd_ < 0) {
    return FAILED;
  }
  return func(fd_);
}

Status Channel::SendControlMsg(const std::function<Status(int32_t)> &func) {
  return CommWithFd(func);
}

Status Channel::SendHeartBeat(const std::function<Status(int32_t)> &func) {
  if (with_heartbeat_.load(std::memory_order_acquire)) {
    return CommWithFd(func);
  }
  return SUCCESS;
}

void Channel::SetHeartbeatTimeout(int64_t timeout_in_millis) {
  timeout_in_millis_ = timeout_in_millis;
}

void Channel::UpdateHeartbeatTime() {
  last_heartbeat_time_ = std::chrono::steady_clock::now();
}

bool Channel::IsHeartbeatTimeout() const {
  if (with_heartbeat_.load(std::memory_order_acquire)) {
    auto now = std::chrono::steady_clock::now();
    const auto cost = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_heartbeat_time_).count();
    if (cost >= timeout_in_millis_) {
      LLMLOGW("Channel heartbeat timeout detected, cost:%ld ms, channel_id:%s", cost, channel_info_.channel_id.c_str());
      return true;
    }
  }
  return false;
}

StreamPool* Channel::GetStreamPool() {
  return stream_pool_;
}

std::mutex &Channel::GetTransferMutex() {
  return transfer_mutex_;
}

void Channel::GetNotifyMessages(std::vector<NotifyDesc> &notifies) {
  std::lock_guard<std::mutex> lock(notify_message_mutex_);
  for (auto &notify_msg : notify_messages_) {
    NotifyDesc notify;
    notify.name = AscendString(notify_msg.name.c_str());
    notify.notify_msg = AscendString(notify_msg.notify_msg.c_str());
    notifies.push_back(std::move(notify));
  }
  notify_messages_.clear();
}

BufferedTransfer::BufferedTransfer(
    std::function<Status(HcclOneSideOpDesc *descs, uint32_t desc_num)> trans_func) : trans_func_(trans_func) {
  op_descs_.reserve(kMaxOpDescNum);
}

Status BufferedTransfer::Put(const std::vector<TransferOpDesc> &op_descs) {
  size_t index = 0U;
  for (const auto &desc : op_descs) {
    HcclOneSideOpDesc hccl_op_desc{};
    hccl_op_desc.localAddr = llm::ValueToPtr(desc.local_addr);
    hccl_op_desc.remoteAddr = llm::ValueToPtr(desc.remote_addr);
    hccl_op_desc.count = desc.len;
    hccl_op_desc.dataType = HCCL_DATA_TYPE_UINT8;
    op_descs_.emplace_back(hccl_op_desc);
    LLMLOGI("Batch transfer sync, index:%zu, local addr:%p, remote addr:%p, len:%zu.",
           index, desc.local_addr, desc.remote_addr, desc.len);
    if (op_descs_.size() == op_descs_.capacity()) {
      ADXL_CHK_STATUS_RET(Flush(), "Failed to batch transfer.");
    }
  }
  if (!op_descs_.empty()) {
    ADXL_CHK_STATUS_RET(Flush(), "Failed to batch transfer.");
  }
  return SUCCESS;
}

Status BufferedTransfer::Flush() {
  if (!op_descs_.empty()) {
    ADXL_CHK_STATUS_RET(trans_func_(op_descs_.data(), static_cast<uint32_t>(op_descs_.size())),
                      "Failed to batch transfer.");
    op_descs_.clear();
  }
  return SUCCESS;
}

}  // namespace adxl
