/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "comm_channel.h"
#include <mutex>
#include <thread>
#include <fcntl.h>
#include <unistd.h>
#include "adxl/adxl_checker.h"
#include "adxl/adxl_types.h"
#include "adxl/adxl_utils.h"
#include "common/hixl_utils.h"
#include "common/llm_scope_guard.h"
#include "common/def_types.h"
#include "common/llm_log.h"
#include "statistic_manager.h"

#include <base/err_msg.h>

namespace adxl {
namespace {
// hccl HcclCommInitClusterInfoMemConfig not support parallel call, so use mutex to protect it
std::mutex g_mutex_;
constexpr uint32_t kMaxOpDescNum = 256U;
constexpr int64_t kHeartbeatTimeoutInMillis = 120000;
constexpr int32_t kMillisToMicros = 1000;
// streamAbort cannot stop tasks already being unfolded by aicpu; when a disconnect races with that expansion the
// comm must not be destroyed underneath it. If tasks were in flight we sleep this long (enough for aicpu to finish
// unfolding) after aborting the stream and before destroying the comm.
constexpr int32_t kAicpuUnfoldGuardMs = 100;
constexpr uint64_t kHostFlagDoneValue = 1ULL;

uint64_t GetDurationUs(const std::chrono::steady_clock::time_point &start,
                       const std::chrono::steady_clock::time_point &end) {
  return std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
}
}  // namespace

int64_t CommChannel::timeout_in_millis_ = kHeartbeatTimeoutInMillis;

Status CommChannel::Initialize() {
  const auto hccl_start = std::chrono::steady_clock::now();
  ADXL_CHK_STATUS_RET(InitializeHcclComm(), "Failed to initialize hccl comm");

  std::vector<void *> bind_handles;
  LLM_DISMISSABLE_GUARD(fail_guard, ([this, &bind_handles]() {
                          for (auto bind_handle : bind_handles) {
                            (void)llm::HcclAdapter::GetInstance().HcclCommUnbindMem(channel_info_.comm, bind_handle);
                          }
                          (void)llm::HcclAdapter::GetInstance().HcclCommDestroy(channel_info_.comm);
                        }));
  ADXL_CHK_STATUS_RET(BindRegisteredMemory(bind_handles), "Failed to bind registered memory");
  ADXL_CHK_STATUS_RET(PrepareHcclComm(hccl_start), "Failed to prepare hccl comm");
  LLM_DISMISS_GUARD(fail_guard);
  return SUCCESS;
}

std::string CommChannel::GetChannelId() const {
  return channel_info_.channel_id;
}

std::string CommChannel::GetStatisticChannelId() const {
  return StatisticManager::GetStatisticChannelId(channel_info_.channel_id,
                                                 channel_info_.channel_type == ChannelType::kClient);
}

Status CommChannel::InitializeHcclComm() {
  LLMLOGI("HcclCommInitClusterInfoMemConfig begin, comm_name=%s, local rank_id=%u, rank_table=%s",
          channel_info_.comm_config.hcclCommName, channel_info_.local_rank_id, channel_info_.rank_table.c_str());
  std::lock_guard<std::mutex> lock(g_mutex_);
  const auto start = std::chrono::steady_clock::now();
  ADXL_CHK_HCCL_RET(llm::HcclAdapter::GetInstance().HcclCommInitClusterInfoMemConfig(
      channel_info_.rank_table.c_str(), channel_info_.local_rank_id, &channel_info_.comm_config, &channel_info_.comm));
  const auto cost = GetDurationUs(start, std::chrono::steady_clock::now());
  StatisticManager::GetInstance().UpdateHcclCommInitCost(GetStatisticChannelId(), cost);
  LLMEVENT("HcclCommInitClusterInfoMemConfig success, channel_id:%s, time cost:%lu us.",
           channel_info_.channel_id.c_str(), cost);
  return SUCCESS;
}

Status CommChannel::BindRegisteredMemory(std::vector<void *> &bind_handles) {
  const auto start = std::chrono::steady_clock::now();
  for (const auto &reg_handle_it : channel_info_.registered_mems) {
    auto reg_handle = reg_handle_it.first;
    ADXL_CHK_HCCL_RET(llm::HcclAdapter::GetInstance().HcclCommBindMem(channel_info_.comm, reg_handle));
    bind_handles.emplace_back(reg_handle);
  }
  const auto cost = GetDurationUs(start, std::chrono::steady_clock::now());
  StatisticManager::GetInstance().UpdateHcclCommBindMemCost(GetStatisticChannelId(), cost);
  LLMEVENT("HcclCommBindMem success, channel_id:%s, time cost:%lu us.", channel_info_.channel_id.c_str(), cost);
  return SUCCESS;
}

Status CommChannel::PrepareHcclComm(const std::chrono::steady_clock::time_point &hccl_start) {
  const auto start = std::chrono::steady_clock::now();
  HcclPrepareConfig prepareConfig{};
  ADXL_CHK_HCCL_RET(
      llm::HcclAdapter::GetInstance().HcclCommPrepare(channel_info_.comm, &prepareConfig, channel_info_.timeout_sec));
  const auto end = std::chrono::steady_clock::now();
  const auto prepare_cost = GetDurationUs(start, end);
  StatisticManager::GetInstance().UpdateHcclCommPrepareCost(GetStatisticChannelId(), prepare_cost);
  StatisticManager::GetInstance().UpdateHcclTotalCost(GetStatisticChannelId(), GetDurationUs(hccl_start, end));
  LLMEVENT("HcclCommPrepare success, channel_id:%s, time cost:%lu us.", channel_info_.channel_id.c_str(), prepare_cost);
  return SUCCESS;
}

void CommChannel::ClearNotifyMessages() {
  {
    std::lock_guard<std::mutex> notify_lock(notify_message_mutex_);
    notify_messages_.clear();
  }
}

Status CommChannel::Finalize() {
  finalized_.store(true, std::memory_order_release);
  ADXL_CHK_STATUS_RET(ClearResources(), "Failed to clear channel resources.");

  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (fd_ > 0) {
      (void)close(fd_);
      fd_ = -1;
    }
    with_heartbeat_.store(false, std::memory_order_release);
  }
  ClearNotifyMessages();
  disconnect_flag_.store(false, std::memory_order_release);
  transfer_count_.store(0, std::memory_order_release);
  unavailable_.store(false, std::memory_order_release);
  LLMLOGI("Channel finalized, channel_id:%s.", channel_info_.channel_id.c_str());
  return SUCCESS;
}

Status CommChannel::ClearResources() {
  std::lock_guard<std::mutex> launch_lock(device_launch_mu_);
  AbortActiveSlotStreamForDisconnect();
  const bool in_flight = HasInFlightWorkForDisconnect();
  if (in_flight) {
    LLMLOGW("Channel has in-flight tasks at disconnect, sleep %dms before destroying comm, channel_id:%s.",
            kAicpuUnfoldGuardMs, channel_info_.channel_id.c_str());
    std::this_thread::sleep_for(std::chrono::milliseconds(kAicpuUnfoldGuardMs));
  }
  const Status ret = TeardownHcclComm();
  ResetAsyncStateOnDisconnect();
  return ret;
}

bool CommChannel::HasInFlightWorkForDisconnect() {
  // unavailable_ means a fatal error (e.g. sync timeout) already hit the stream; even when async records
  // and active_slot_ are cleared by FailChannel, aicpu may still be unfolding — keep the sleep guard.
  bool in_flight = IsUnavailable();
  {
    std::lock_guard<std::mutex> reqs_lock(transfer_reqs_mutex_);
    in_flight = in_flight || !req_2_async_record_.empty();
  }
  {
    std::lock_guard<std::mutex> slot_lock(active_slot_mu_);
    if (active_slot_ != nullptr) {
      in_flight = true;
    }
  }
  return in_flight;
}

void CommChannel::AbortActiveSlotStreamForDisconnect() {
  std::lock_guard<std::mutex> slot_lock(active_slot_mu_);
  if ((active_slot_ == nullptr) || (active_slot_->stream == nullptr)) {
    return;
  }
  hixl::TemporaryRtContext ctx_guard(active_slot_->ctx);
  const auto abort_ret = aclrtStreamAbort(active_slot_->stream);
  if (abort_ret != ACL_ERROR_NONE) {
    LLMLOGE(FAILED, "Call aclrtStreamAbort ret:%d.", abort_ret);
  }
}

Status CommChannel::TeardownHcclComm() {
  auto ret = SUCCESS;
  for (const auto &reg_handle_it : channel_info_.registered_mems) {
    auto reg_handle = reg_handle_it.first;
    auto hccl_ret = llm::HcclAdapter::GetInstance().HcclCommUnbindMem(channel_info_.comm, reg_handle);
    ret = hccl_ret != HcclResult::HCCL_SUCCESS ? FAILED : ret;
  }
  if (channel_info_.comm != nullptr) {
    auto hccl_ret = llm::HcclAdapter::GetInstance().HcclCommDestroy(channel_info_.comm);
    channel_info_.comm = nullptr;
    ret = hccl_ret != HcclResult::HCCL_SUCCESS ? FAILED : ret;
  }
  return ret;
}

void CommChannel::ResetAsyncStateOnDisconnect() {
  // Same recycling as the fatal path, minus marking the link unavailable (this is a normal teardown).
  PurgeAllAsyncRecords();
  AbortSharedSlot();
}

void CommChannel::SetSlotPool(TransferSlotPool *slot_pool) {
  slot_pool_ = slot_pool;
}

void CommChannel::MarkUnavailableOnError(Status ret) {
  if (!IsLinkFatal(ret)) {
    return;
  }
  if (!IsUnavailable()) {
    LLMLOGW("Mark link unavailable due to transport failure, channel_id:%s, status:%u.",
            channel_info_.channel_id.c_str(), ret);
  }
  unavailable_.store(true, std::memory_order_release);
}

Status CommChannel::CheckAvailableLocked() const {
  if (fail_fast_enabled_ && IsUnavailable()) {
    ADXL_CHK_BOOL_RET_STATUS(false, FAILED,
                             "Channel is unavailable due to a previous transport failure, channel_id:%s.",
                             channel_info_.channel_id.c_str());
  }
  return SUCCESS;
}

Status CommChannel::AcquireSharedSlot(std::shared_ptr<SlotHandle> &slot_out) {
  std::lock_guard<std::mutex> lock(active_slot_mu_);
  // Reuse the slot already bound to this channel so all in-flight transfers share one stream.
  if (active_slot_ != nullptr) {
    slot_out = active_slot_;
    return SUCCESS;
  }
  ADXL_CHK_BOOL_RET_STATUS(slot_pool_ != nullptr, FAILED, "Slot pool is null, channel_id:%s.",
                           channel_info_.channel_id.c_str());
  SlotHandle new_slot{};
  ADXL_CHK_STATUS_RET(slot_pool_->Acquire(&new_slot), "Slot pool acquire failed.");
  active_slot_ = std::make_shared<SlotHandle>(new_slot);
  slot_out = active_slot_;
  return SUCCESS;
}

void CommChannel::ReleaseSharedSlotRef(std::shared_ptr<SlotHandle> &slot_ref) {
  std::lock_guard<std::mutex> lock(active_slot_mu_);
  if (slot_ref == nullptr) {
    return;
  }
  if (active_slot_ != slot_ref) {
    // The slot was already unbound (e.g. aborted on failure); just drop this caller's reference.
    slot_ref.reset();
    return;
  }
  slot_ref.reset();
  // Only the channel's own binding remains: the whole batch is done, return the slot to the pool.
  if (active_slot_.use_count() == 1) {
    if (slot_pool_ != nullptr) {
      slot_pool_->Release(*active_slot_);
    }
    active_slot_.reset();
  }
}

void CommChannel::AbortSharedSlot() {
  std::lock_guard<std::mutex> lock(active_slot_mu_);
  if (active_slot_ == nullptr) {
    return;
  }
  if (slot_pool_ != nullptr) {
    slot_pool_->Abort(*active_slot_);
  }
  active_slot_.reset();
}

void CommChannel::ReleaseHostFlag(const std::shared_ptr<SlotHandle> &slot, void *host_flag) {
  if (host_flag == nullptr) {
    return;
  }
  if ((slot == nullptr) || (slot_pool_ == nullptr)) {
    (void)aclrtFreeHost(host_flag);
    return;
  }
  slot_pool_->ReleaseHostFlag(*slot, host_flag);
}

void CommChannel::ReleaseAsyncRecord(uint64_t id) {
  std::shared_ptr<SlotHandle> slot;
  void *host_flag = nullptr;
  {
    std::lock_guard<std::mutex> reqs_lock(transfer_reqs_mutex_);
    auto it = req_2_async_record_.find(id);
    if (it == req_2_async_record_.end()) {
      return;
    }
    slot = std::move(it->second.slot);
    host_flag = it->second.host_flag;
    req_2_async_record_.erase(it);
  }
  ReleaseHostFlag(slot, host_flag);
  ReleaseSharedSlotRef(slot);
}

void CommChannel::PurgeAllAsyncRecords() {
  std::unordered_map<uint64_t, AsyncRecord> records;
  {
    std::lock_guard<std::mutex> reqs_lock(transfer_reqs_mutex_);
    records.swap(req_2_async_record_);
  }
  // Recycle each request's host_flag; the slot references drop when `records` is destroyed at function end. We do
  // not return the slot to the pool here: the caller (FailChannel / disconnect) tears it down via AbortSharedSlot.
  for (auto &record_it : records) {
    ReleaseHostFlag(record_it.second.slot, record_it.second.host_flag);
  }
}

void CommChannel::FailChannel(Status ret) {
  PurgeAllAsyncRecords();
  AbortSharedSlot();
  MarkUnavailableOnError(ret);
}

void CommChannel::CompleteRequest(uint64_t id, const std::chrono::steady_clock::time_point &transfer_start,
                                  uint64_t transfer_bytes, uint64_t op_desc_count) {
  const auto end = std::chrono::steady_clock::now();
  const auto cost = std::chrono::duration_cast<std::chrono::microseconds>(end - transfer_start).count();
  StatisticManager::GetInstance().UpdateDirectTransferCost(GetStatisticChannelId(), cost, transfer_bytes,
                                                           op_desc_count);
  ReleaseAsyncRecord(id);
  LLMLOGI("Transfer async request completed, req:%lu, time cost:%ld us.", id, cost);
}

Status CommChannel::IssueAsyncBatchWithHostFlag(TransferOp operation, const std::vector<TransferOpDesc> &op_descs,
                                                const std::shared_ptr<SlotHandle> &slot, void *host_flag) {
  hixl::TemporaryRtContext ctx_guard(slot->ctx);
  ADXL_CHK_STATUS_RET(IssueHcclBatch(operation, op_descs, slot->stream), "Channel Hccl batch issue failed.");
  ADXL_CHK_ACL_RET(aclrtMemcpyAsync(host_flag, sizeof(uint64_t), slot->dev_const_one, sizeof(uint64_t),
                                    ACL_MEMCPY_DEVICE_TO_HOST, slot->stream));
  return SUCCESS;
}

Status CommChannel::TransferAsync(TransferOp operation, const std::vector<TransferOpDesc> &op_descs,
                                  const TransferArgs &optional_args, TransferReq &req) {
  (void)optional_args;
  std::lock_guard<std::mutex> launch_lock(device_launch_mu_);
  ADXL_CHK_STATUS_RET(CheckAvailableLocked(), "Channel unavailable.");
  ADXL_CHK_BOOL_RET_STATUS(channel_info_.comm != nullptr, FAILED,
                           "Channel comm is null, channel may have been finalized, channel_id:%s.",
                           channel_info_.channel_id.c_str());
  const auto id = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(req));
  const auto transfer_start = std::chrono::steady_clock::now();
  const auto transfer_bytes = GetTransferBytes(op_descs);
  const auto op_desc_count = GetTransferOpDescCount(op_descs);

  std::shared_ptr<SlotHandle> slot;
  ADXL_CHK_STATUS_RET(AcquireSharedSlot(slot), "Failed to acquire transfer slot.");

  ADXL_CHK_BOOL_RET_STATUS(slot_pool_ != nullptr, FAILED, "Slot pool is null, channel_id:%s.",
                           channel_info_.channel_id.c_str());
  void *host_flag = nullptr;
  const Status alloc_ret = slot_pool_->AcquireHostFlag(*slot, host_flag);
  if (alloc_ret != SUCCESS) {
    ReleaseSharedSlotRef(slot);
    MarkUnavailableOnError(alloc_ret);
    LLMLOGE(alloc_ret, "Failed to allocate host flag, req:%lu.", id);
    return alloc_ret;
  }

  // Register the record before issuing so the host_flag and slot reference live in one place (req_2_async_record_).
  // A submit failure is then recycled by FailChannel together with every other in-flight request.
  {
    std::lock_guard<std::mutex> reqs_lock(transfer_reqs_mutex_);
    AsyncRecord record;
    record.slot = slot;
    record.host_flag = host_flag;
    record.transfer_start = transfer_start;
    record.transfer_bytes = transfer_bytes;
    record.op_desc_count = op_desc_count;
    req_2_async_record_[id] = std::move(record);
  }

  const Status issue_ret = IssueAsyncBatchWithHostFlag(operation, op_descs, slot, host_flag);
  if (issue_ret != SUCCESS) {
    FailChannel(issue_ret);
    LLMLOGE(issue_ret, "Failed to submit async transfer, req:%lu.", id);
    return issue_ret;
  }
  return SUCCESS;
}

Status CommChannel::LookupPendingAsyncTransfer(uint64_t id, std::shared_ptr<SlotHandle> &slot, void *&host_flag,
                                               std::chrono::steady_clock::time_point &transfer_start,
                                               uint64_t &transfer_bytes, uint64_t &op_desc_count) {
  std::lock_guard<std::mutex> reqs_lock(transfer_reqs_mutex_);
  auto it = req_2_async_record_.find(id);
  ADXL_CHK_BOOL_RET_STATUS(it != req_2_async_record_.end(), FAILED, "Request not found, req:%lu.", id);
  slot = it->second.slot;
  host_flag = it->second.host_flag;
  transfer_start = it->second.transfer_start;
  transfer_bytes = it->second.transfer_bytes;
  op_desc_count = it->second.op_desc_count;
  ADXL_CHK_BOOL_RET_STATUS((slot != nullptr) && (host_flag != nullptr), FAILED, "Invalid async record, req:%lu.", id);
  return SUCCESS;
}

bool CommChannel::IsHostFlagDone(void *host_flag) {
  volatile uint64_t *flag_ptr = static_cast<uint64_t *>(host_flag);
  if (*flag_ptr != kHostFlagDoneValue) {
    return false;
  }
  std::atomic_thread_fence(std::memory_order_acquire);
  return true;
}

Status CommChannel::GetTransferStatus(const TransferReq &req, TransferStatus &status) {
  std::lock_guard<std::mutex> launch_lock(device_launch_mu_);
  const auto id = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(req));

  Status avail_ret = CheckAvailableLocked();
  if (avail_ret != SUCCESS) {
    status = TransferStatus::FAILED;
    ReleaseAsyncRecord(id);
    return avail_ret;
  }

  std::shared_ptr<SlotHandle> slot;
  void *host_flag = nullptr;
  std::chrono::steady_clock::time_point transfer_start;
  uint64_t transfer_bytes = 0UL;
  uint64_t op_desc_count = 0UL;
  Status lookup_ret = LookupPendingAsyncTransfer(id, slot, host_flag, transfer_start, transfer_bytes, op_desc_count);
  if (lookup_ret != SUCCESS) {
    status = TransferStatus::FAILED;
    return lookup_ret;
  }

  if (IsHostFlagDone(host_flag)) {
    CompleteRequest(id, transfer_start, transfer_bytes, op_desc_count);
    status = TransferStatus::COMPLETED;
    return SUCCESS;
  }
  status = TransferStatus::WAITING;
  return SUCCESS;
}

Status CommChannel::IssueHcclBatch(TransferOp operation, const std::vector<TransferOpDesc> &op_descs,
                                   aclrtStream stream) {
  ADXL_CHK_BOOL_RET_STATUS(channel_info_.comm != nullptr, FAILED,
                           "Channel comm is null, channel may have been finalized, channel_id:%s.",
                           channel_info_.channel_id.c_str());
  auto trans_func = [this, operation, &stream](HcclOneSideOpDesc *descs, uint32_t desc_num) -> Status {
    HcclResult ret = HCCL_SUCCESS;
    if (operation == READ) {
      ret = llm::HcclAdapter::GetInstance().HcclBatchGet(channel_info_.comm, channel_info_.peer_rank_id, descs,
                                                         desc_num, stream);
    } else {
      ret = llm::HcclAdapter::GetInstance().HcclBatchPut(channel_info_.comm, channel_info_.peer_rank_id, descs,
                                                         desc_num, stream);
    }
    ADXL_CHK_BOOL_RET_STATUS(ret == HCCL_SUCCESS, HcclError2AdxlStatus(ret), "Failed to invoke %s, hccl_result = %d",
                             operation == READ ? "HcclBatchGet" : "HcclBatchPut", static_cast<int32_t>(ret));
    return SUCCESS;
  };
  BufferedTransfer transfer(trans_func);
  ADXL_CHK_STATUS_RET(transfer.Put(op_descs), "Failed to batch transfer");
  return SUCCESS;
}

Status CommChannel::TransferAsyncWithTimeout(TransferOp operation, const std::vector<TransferOpDesc> &op_descs,
                                             aclrtStream stream, uint64_t timeout) {
  ADXL_CHK_BOOL_RET_STATUS(channel_info_.comm != nullptr, FAILED,
                           "Channel comm is null, channel may have been finalized, channel_id:%s.",
                           channel_info_.channel_id.c_str());
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

Status CommChannel::RunSyncOnSlot(const std::function<Status(aclrtStream stream)> &issue_fn,
                                  int32_t timeout_in_millis) {
  // Serialize against async submit/poll so the shared stream is never shared or aborted mid-issue.
  std::lock_guard<std::mutex> launch_lock(device_launch_mu_);
  ADXL_CHK_STATUS_RET(CheckAvailableLocked(), "Channel unavailable.");
  std::shared_ptr<SlotHandle> slot;
  ADXL_CHK_STATUS_RET(AcquireSharedSlot(slot), "Failed to acquire transfer slot.");
  const Status ret = [&issue_fn, &slot, timeout_in_millis]() -> Status {
    hixl::TemporaryRtContext ctx_guard(slot->ctx);
    ADXL_CHK_STATUS_RET(issue_fn(slot->stream), "Sync transfer issue failed.");
    ADXL_CHK_ACL_RET(aclrtSynchronizeStreamWithTimeout(slot->stream, timeout_in_millis));
    return SUCCESS;
  }();
  if (ret != SUCCESS) {
    FailChannel(ret);
    return ret;
  }
  ReleaseSharedSlotRef(slot);
  return SUCCESS;
}

Status CommChannel::TransferSync(TransferOp operation, const std::vector<TransferOpDesc> &op_descs,
                                 int32_t timeout_in_millis) {
  const auto start = std::chrono::steady_clock::now();
  auto issue_fn = [this, operation, &op_descs](aclrtStream stream) -> Status {
    return IssueHcclBatch(operation, op_descs, stream);
  };
  ADXL_CHK_STATUS_RET(RunSyncOnSlot(issue_fn, timeout_in_millis), "TransferSync failed.");
  const auto end = std::chrono::steady_clock::now();
  const auto cost = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
  StatisticManager::GetInstance().UpdateDirectTransferCost(GetStatisticChannelId(), cost, GetTransferBytes(op_descs),
                                                           GetTransferOpDescCount(op_descs));
  LLMLOGI("TransferSync success, operation:%s, num = %zu, channel_id:%s, time cost:%lu us.",
          operation == READ ? "HcclBatchGet" : "HcclBatchPut", op_descs.size(), channel_info_.channel_id.c_str(), cost);
  return SUCCESS;
}

Status CommChannel::SetSocketNonBlocking(int32_t fd) {
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

void CommChannel::StopHeartbeat() {
  std::lock_guard<std::mutex> lock(mutex_);
  with_heartbeat_.store(false, std::memory_order_release);
  disconnect_flag_.store(true, std::memory_order_release);
}

Status CommChannel::CommWithFd(const std::function<Status(int32_t)> &func) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (fd_ < 0) {
    return FAILED;
  }
  return func(fd_);
}

Status CommChannel::SendControlMsg(const std::function<Status(int32_t)> &func) {
  return CommWithFd(func);
}

Status CommChannel::SendHeartBeat(const std::function<Status(int32_t)> &func) {
  if (with_heartbeat_.load(std::memory_order_acquire)) {
    return CommWithFd(func);
  }
  return SUCCESS;
}

void CommChannel::SetHeartbeatTimeout(int64_t timeout_in_millis) {
  timeout_in_millis_ = timeout_in_millis;
}

void CommChannel::UpdateHeartbeatTime() {
  last_heartbeat_time_ = std::chrono::steady_clock::now();
}

bool CommChannel::IsHeartbeatTimeout() const {
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

void CommChannel::GetNotifyMessages(std::vector<NotifyDesc> &notifies) {
  std::lock_guard<std::mutex> lock(notify_message_mutex_);
  for (auto &notify_msg : notify_messages_) {
    NotifyDesc notify;
    notify.name = AscendString(notify_msg.name.c_str());
    notify.notify_msg = AscendString(notify_msg.notify_msg.c_str());
    notifies.push_back(std::move(notify));
  }
  notify_messages_.clear();
}

BufferedTransfer::BufferedTransfer(std::function<Status(HcclOneSideOpDesc *descs, uint32_t desc_num)> trans_func)
    : trans_func_(trans_func) {
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
    LLMLOGI("Batch transfer sync, index:%zu, local addr:%p, remote addr:%p, len:%zu.", index, desc.local_addr,
            desc.remote_addr, desc.len);
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
