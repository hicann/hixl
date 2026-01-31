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
#include "adxl/adxl_checker.h"
#include "adxl/adxl_types.h"
#include "adxl/adxl_utils.h"
#include "common/llm_scope_guard.h"
#include "common/def_types.h"
#include "common/llm_log.h"
#include "virtual_memory_manager.h"

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

Status Channel::Initialize(bool enable_use_fabric_mem) {
  if (enable_use_fabric_mem) {
    LLMLOGI("Initialize channel in use fabric mem mode, channel_id:%s", channel_info_.channel_id.c_str());
    enable_use_fabric_mem_ = enable_use_fabric_mem;
    return SUCCESS;
  }
  LLMLOGI("HcclCommInitClusterInfoMemConfig begin, comm_name=%s, local rank_id=%u, rank_table=%s",
         channel_info_.comm_config.hcclCommName, channel_info_.local_rank_id, channel_info_.rank_table.c_str());
  {
    std::lock_guard<std::mutex> lock(g_mutex_);
    const auto start = std::chrono::steady_clock::now();
    ADXL_CHK_HCCL_RET(llm::HcclAdapter::GetInstance().HcclCommInitClusterInfoMemConfig(
        channel_info_.rank_table.c_str(),
        channel_info_.local_rank_id,
        &channel_info_.comm_config,
        &channel_info_.comm));
    const auto end = std::chrono::steady_clock::now();
    const auto cost = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    LLMEVENT("HcclCommInitClusterInfoMemConfig success, channel_id:%s, time cost:%lu us.",
             channel_info_.channel_id.c_str(), cost);
  }

  std::vector<void *> bind_handles;
  LLM_DISMISSABLE_GUARD(fail_guard, ([this, &bind_handles]() {
    for (auto bind_handle : bind_handles) {
      (void) llm::HcclAdapter::GetInstance().HcclCommUnbindMem(channel_info_.comm, bind_handle);
    }
    (void) llm::HcclAdapter::GetInstance().HcclCommDestroy(channel_info_.comm);
  }));

  auto start = std::chrono::steady_clock::now();
  for (const auto &reg_handle_it : channel_info_.registered_mems) {
    auto reg_handle = reg_handle_it.first;
    ADXL_CHK_HCCL_RET(llm::HcclAdapter::GetInstance().HcclCommBindMem(channel_info_.comm, reg_handle));
    bind_handles.emplace_back(reg_handle);
  }
  auto end = std::chrono::steady_clock::now();
  auto cost = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
  LLMEVENT("HcclCommBindMem success, channel_id:%s, time cost:%lu us.", channel_info_.channel_id.c_str(), cost);

  start = std::chrono::steady_clock::now();
  HcclPrepareConfig prepareConfig{};
  ADXL_CHK_HCCL_RET(llm::HcclAdapter::GetInstance().HcclCommPrepare(channel_info_.comm, &prepareConfig,
                                                                    channel_info_.timeout_sec));
  end = std::chrono::steady_clock::now();
  cost = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
  LLMEVENT("HcclCommPrepare success, channel_id:%s, time cost:%lu us.", channel_info_.channel_id.c_str(), cost);
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
  if (enable_use_fabric_mem_) {
    ClearImportedMem();
  } else {
    ADXL_CHK_STATUS_RET(ClearResources());
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
  disconnect_flag_.store(false, std::memory_order_release);
  transfer_count_.store(0, std::memory_order_release);
  return ret;
}

Status Channel::ClearResources() {
  std::lock_guard<std::mutex> lock(transfer_mutex_);
  auto ret = SUCCESS;
  for (const auto &reg_handle_it : channel_info_.registered_mems) {
    auto reg_handle = reg_handle_it.first;
    auto hccl_ret = llm::HcclAdapter::GetInstance().HcclCommUnbindMem(channel_info_.comm, reg_handle);
    ret = hccl_ret != HcclResult::HCCL_SUCCESS ? FAILED : ret;
  }

  if (channel_info_.comm != nullptr) {
    auto hccl_ret = llm::HcclAdapter::GetInstance().HcclCommDestroy(channel_info_.comm);
    ret = hccl_ret != HcclResult::HCCL_SUCCESS ? FAILED : ret;
  }

  std::lock_guard<std::mutex> transfer_reqs_lock(transfer_reqs_mutex_);
  for (const auto &transfer_req : req_2_async_record_) {
    const auto &async_resources = transfer_req.second.async_resources;
    ADXL_CHK_BOOL_RET_STATUS(!async_resources.empty(), FAILED, "Failed to get request async resources, req:%lu.",
                             transfer_req.first);
    aclrtEvent event = async_resources[0].first;
    aclrtStream stream = async_resources[0].second;
    if (event != nullptr) {
      auto aclrt_ret = aclrtDestroyEvent(event);
      if (aclrt_ret != ACL_ERROR_NONE) {
        LLMLOGE(FAILED, "Call aclrtDestroyEvent ret:%d.", aclrt_ret);
        ret = FAILED;
      }
    }
    //during exceptional scenarios, destroy the stream when destroying the channel.
    if (stream != nullptr) {
      stream_pool_->DestroyStream(stream);
    }
  }
  return ret;
}

void Channel::ClearImportedMem() {
  // unmap all va
  std::lock_guard<std::mutex> lock(va_map_mutex_);
  for (auto &it : new_va_to_old_va_) {
    LLMLOGI("Unmap mem:%lu", it.first);
    LLM_CHK_ACL(aclrtUnmapMem(llm::ValueToPtr(it.first)));
    (void)VirtualMemoryManager::GetInstance().ReleaseMemory(it.first);
  }
  new_va_to_old_va_.clear();
  // free imported pa handle
  for (auto &remote_pa_handle : remote_pa_handles_) {
    LLM_CHK_ACL(aclrtFreePhysical(remote_pa_handle));
  }
  remote_pa_handles_.clear();
}

void Channel::SetStreamPool(StreamPool *stream_pool) {
  stream_pool_ = stream_pool;
}

Status Channel::ImportMem(const std::vector<ShareHandleInfo> &remote_share_handles, int32_t device_id) {
  LLM_DISMISSABLE_GUARD(fail_guard, ([this]() { ClearImportedMem(); }));
  for (auto &remote_share_handle_info : remote_share_handles) {
    uintptr_t remote_va_addr = 0;
    aclrtDrvMemHandle remote_pa_handle = nullptr;
    ADXL_CHK_STATUS_RET(VirtualMemoryManager::GetInstance().ReserveMemory(remote_share_handle_info.len, remote_va_addr));
    LLM_DISMISSABLE_GUARD(free_mem_guard, ([&remote_va_addr, &remote_pa_handle]() {
                            if (remote_va_addr != 0) {
                              (void)VirtualMemoryManager::GetInstance().ReleaseMemory(remote_va_addr);
                            }
                            if (remote_pa_handle != nullptr) {
                              LLM_CHK_ACL(aclrtFreePhysical(remote_pa_handle));
                            }
                          }));

    auto share_handle = remote_share_handle_info.share_handle;
    ADXL_CHK_ACL_RET(aclrtMemImportFromShareableHandleV2(&share_handle, ACL_MEM_SHARE_HANDLE_TYPE_FABRIC, 0U,
                                                      &remote_pa_handle));
    void *remote_va = llm::ValueToPtr(remote_va_addr);
    ADXL_CHK_ACL_RET(aclrtMapMem(remote_va, remote_share_handle_info.len, 0, remote_pa_handle, 0));
    std::lock_guard<std::mutex> lock(va_map_mutex_);
    remote_pa_handles_.emplace_back(remote_pa_handle);
    new_va_to_old_va_[remote_va_addr] = remote_share_handle_info;
    LLM_DISMISS_GUARD(free_mem_guard);
    LLMLOGI("Imported mem from share handle, va:%lu, new mapped va addr:%lu, len:%zu for device:%d.",
            remote_share_handle_info.va_addr, remote_va_addr, remote_share_handle_info.len, device_id);
  }
  LLM_DISMISS_GUARD(fail_guard);
  return SUCCESS;
}

std::unordered_map<uintptr_t, ShareHandleInfo> Channel::GetNewVaToOldVa() {
  std::lock_guard<std::mutex> lock(va_map_mutex_);
  return new_va_to_old_va_;
}

Status Channel::TransferAsync(TransferOp operation,
                              const std::vector<TransferOpDesc> &op_descs,
                              const TransferArgs &optional_args,
                              TransferReq &req) {
  (void)optional_args;
  aclrtStream stream = nullptr;
  ADXL_CHK_STATUS_RET(stream_pool_->TryAllocStream(stream), "Stream pool get stream failed.");
  auto id = reinterpret_cast<uint64_t>(req);
  aclrtEvent event = nullptr;
  LLM_DISMISSABLE_GUARD(fail_guard, ([&]() {
    if (event != nullptr) {
      aclrtDestroyEvent(event);
    }
    if (stream != nullptr) {
      stream_pool_->DestroyStream(stream);
    }
  }));
  const auto transfer_start = std::chrono::steady_clock::now();
  ADXL_CHK_STATUS_RET(TransferAsync(operation, op_descs, stream), "Channel transfer async failed.");
  LLM_CHK_ACL_RET(aclrtCreateEvent(&event));
  LLM_CHK_ACL_RET(aclrtRecordEvent(event, stream));
  std::lock_guard<std::mutex> lock(transfer_reqs_mutex_);
  std::vector<AsyncResource> async_resources;
  async_resources.emplace_back(event, stream);
  req_2_async_record_[id] = AsyncRecord{std::move(async_resources), transfer_start};
  LLM_DISMISS_GUARD(fail_guard);
  return SUCCESS;
}

Status Channel::GetTransferStatus(const TransferReq &req, TransferStatus &status) {
  std::lock_guard<std::mutex> lock(transfer_reqs_mutex_);
  auto id = reinterpret_cast<uint64_t>(req);
  auto it = req_2_async_record_.find(id);
  if (it == req_2_async_record_.end()) {
    status = TransferStatus::FAILED;
    LLMLOGE(FAILED, "Request not found, req:%lu.", id);
    return FAILED;
  }

  const auto &async_resources = it->second.async_resources;
  ADXL_CHK_BOOL_RET_STATUS(!async_resources.empty(), FAILED, "Failed to get request async resources.");
  auto event = async_resources[0].first;
  auto stream = async_resources[0].second;
  aclrtEventRecordedStatus event_status{};
  auto ret = aclrtQueryEventStatus(event, &event_status);
  if (ret != ACL_ERROR_NONE) {
    LLMLOGE(FAILED, "aclrtQueryEventStatus failed for req:%lu, ret:%d.", id, ret);
    aclrtDestroyEvent(event);
    stream_pool_->DestroyStream(stream);
    req_2_async_record_.erase(id);
    status = TransferStatus::FAILED;
    return FAILED;
  }
  if (event_status != ACL_EVENT_RECORDED_STATUS_COMPLETE) {
    LLMLOGI("Transfer async request not yet completed, req:%lu.", id);
    status = TransferStatus::WAITING;
    return SUCCESS;
  }
  auto steam_status = aclrtSynchronizeStream(stream);
  if (steam_status != ACL_ERROR_NONE) {
    //stream synchronize failed
    status = TransferStatus::FAILED;
    aclrtDestroyEvent(event);
    stream_pool_->DestroyStream(stream);
    req_2_async_record_.erase(id);
    LLMLOGE(FAILED, "rtStreamSynchronize failed for req:%lu, ret:%d.", id, steam_status);
    return FAILED;
  }
  status = TransferStatus::COMPLETED;
  const auto end = std::chrono::steady_clock::now();
  const auto cost = std::chrono::duration_cast<std::chrono::microseconds>(end - it->second.real_start).count();
  LLMLOGI("Transfer async request completed, req:%lu, time cost:%lu us.", id, cost);
  aclrtDestroyEvent(event);
  stream_pool_->FreeStream(stream);
  req_2_async_record_.erase(id);
  return SUCCESS;
}

Status Channel::TransferAsync(TransferOp operation, const std::vector<TransferOpDesc> &op_descs,
                              aclrtStream stream) {
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
                                         aclrtStream stream, uint64_t timeout) {
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
  aclrtStream stream = nullptr;
  ADXL_CHK_STATUS_RET(stream_pool_->TryAllocStream(stream), "Stream pool get stream failed.");
  LLM_DISMISSABLE_GUARD(fail_guard, ([this, &stream]() {
    if (stream != nullptr) {
      this->stream_pool_->DestroyStream(stream);
    }
  }));
  ADXL_CHK_STATUS_RET(TransferAsync(operation, op_descs, stream), "Transfer failed.");

  ADXL_CHK_ACL_RET(aclrtSynchronizeStreamWithTimeout(stream, timeout_in_millis));
  const auto end = std::chrono::steady_clock::now();
  const auto cost = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
  LLMLOGI("TransferSync success, operation:%s, num = %zu, channel_id:%s, time cost:%lu us.",
          operation == READ ? "HcclBatchGet" : "HcclBatchPut", op_descs.size(), channel_info_.channel_id.c_str(), cost);
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
  disconnect_flag_.store(true, std::memory_order_release);
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
