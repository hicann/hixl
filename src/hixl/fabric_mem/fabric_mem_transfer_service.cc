/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "fabric_mem/fabric_mem_transfer_service.h"

#include <atomic>
#include <utility>

#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/hixl_utils.h"
#include "common/scope_guard.h"
#include "fabric_mem/fabric_mem_allocator.h"
#include "profiling/prof_api_reg.h"

namespace hixl {
namespace {
constexpr uint64_t kMillisToMicros = 1000UL;
constexpr uint64_t kHostFlagDoneValue = 1ULL;
constexpr uint64_t kDevConstOneValue = 1ULL;
constexpr size_t kHostFlagSize = sizeof(uint64_t);

bool IsRangeContained(uintptr_t old_addr, size_t len, uintptr_t base, size_t size) {
  if (old_addr < base) {
    return false;
  }
  const uintptr_t offset = old_addr - base;
  return (offset <= size) && (len <= size - offset);
}

uint64_t GetTransferBytes(const std::vector<TransferOpDesc> &op_descs) {
  uint64_t total_bytes = 0UL;
  for (const auto &op_desc : op_descs) {
    total_bytes += static_cast<uint64_t>(op_desc.len);
  }
  return total_bytes;
}

uint64_t GetDurationUs(const std::chrono::steady_clock::time_point &start,
                       const std::chrono::steady_clock::time_point &end) {
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
}
}  // namespace

FabricMemTransferService::~FabricMemTransferService() {
  Finalize();
}

void FabricMemTransferService::SetKeepaliveCheckIntervalMs(int64_t interval_ms) {
  FabricMemChannelManager::SetKeepaliveCheckIntervalMs(interval_ms);
}

Status FabricMemTransferService::MallocMem(MemType type, size_t size, void **ptr) {
  return FabricMemAllocator::MallocMem(type, size, ptr);
}

Status FabricMemTransferService::FreeMem(void *ptr) {
  return FabricMemAllocator::FreeMem(ptr);
}

Status FabricMemTransferService::InitDevConstOne() {
  if (dev_const_one_ != nullptr) {
    return SUCCESS;
  }
  HIXL_CHK_ACL_RET(aclrtMalloc(&dev_const_one_, kHostFlagSize, ACL_MEM_MALLOC_NORMAL_ONLY),
                   "Allocate fabric mem dev_const_one failed.");
  HIXL_DISMISSABLE_GUARD(fail_guard, ([this]() { FreeDevConstOne(); }));
  HIXL_CHK_ACL_RET(
      aclrtMemcpy(dev_const_one_, kHostFlagSize, &kDevConstOneValue, kHostFlagSize, ACL_MEMCPY_HOST_TO_DEVICE),
      "Initialize fabric mem dev_const_one failed.");
  HIXL_DISMISS_GUARD(fail_guard);
  return SUCCESS;
}

void FabricMemTransferService::FreeDevConstOne() {
  if (dev_const_one_ != nullptr) {
    HIXL_CHK_ACL(aclrtFree(dev_const_one_), "Free fabric mem dev_const_one failed.");
    dev_const_one_ = nullptr;
  }
}

Status FabricMemTransferService::Initialize(const FabricMemTransferServiceInitParam &param) {
  HIXL_CHK_BOOL_RET_STATUS(param.device_id >= 0, PARAM_INVALID, "device_id must be non-negative.");
  HIXL_CHK_BOOL_RET_STATUS(param.max_stream_num > 0, PARAM_INVALID, "max_stream_num must be greater than zero.");
  HIXL_CHK_BOOL_RET_STATUS(param.task_stream_num > 0, PARAM_INVALID, "task_stream_num must be greater than zero.");
  HIXL_CHK_BOOL_RET_STATUS(param.max_stream_num >= param.task_stream_num, PARAM_INVALID,
                           "max_stream_num must be greater than or equal to task_stream_num.");
  HIXL_CHK_BOOL_RET_STATUS(param.statistic != nullptr && param.local_memory != nullptr, PARAM_INVALID,
                           "Invalid fabric mem transfer service initialization parameters.");
  device_id_ = param.device_id;
  task_stream_num_ = param.task_stream_num;
  max_stream_num_ = param.max_stream_num;
  statistic_ = param.statistic;
  local_memory_ = param.local_memory;
  const size_t max_async_slot_num = param.max_stream_num / param.task_stream_num;
  HIXL_CHK_STATUS_RET(slot_pool_.Initialize(param.device_id, max_async_slot_num, param.task_stream_num),
                      "Initialize fabric mem slot pool failed.");
  HIXL_CHK_STATUS_RET(InitDevConstOne(), "Initialize fabric mem dev_const_one failed.");
  FabricMemChannelManagerInitParam manager_param;
  manager_param.local_engine = param.local_engine;
  manager_param.device_id = param.device_id;
  manager_param.auto_connect = param.auto_connect;
  manager_param.statistic = param.statistic;
  manager_param.slot_pool = &slot_pool_;
  manager_param.control_server = param.control_server;
  manager_param.aclrt_context = param.aclrt_context;
  HIXL_CHK_STATUS_RET(channel_manager_.Initialize(manager_param), "Initialize fabric mem channel manager failed.");
  HIXL_LOGI("FabricMemTransferService initialized, device:%d, max_stream:%zu, task_stream:%zu, max_async_slot:%zu.",
            device_id_, max_stream_num_, task_stream_num_, max_async_slot_num);
  return SUCCESS;
}

void FabricMemTransferService::Finalize() {
  // The owning FabricMemEngine quiesces public traffic before Finalize(). Finalizing the channel
  // manager disconnects every remote (aborting in-flight transfers and releasing their slots), after
  // which the slot pool and dev_const_one can be torn down.
  channel_manager_.Finalize();
  slot_pool_.AbortAndDestroyAll();
  FreeDevConstOne();
}

Status FabricMemTransferService::Connect(const AscendString &remote_engine, int32_t timeout_in_millis) {
  return channel_manager_.Connect(remote_engine, timeout_in_millis);
}

Status FabricMemTransferService::EnsureConnected(const AscendString &remote_engine, int32_t timeout_in_millis) {
  return channel_manager_.EnsureConnected(remote_engine, timeout_in_millis);
}

Status FabricMemTransferService::Disconnect(const AscendString &remote_engine, int32_t timeout_in_millis) {
  return channel_manager_.Disconnect(remote_engine, timeout_in_millis);
}

void FabricMemTransferService::DisconnectAll() {
  channel_manager_.DisconnectAll();
}

bool FabricMemTransferService::HasChannels() const {
  return channel_manager_.HasChannels();
}

bool FabricMemTransferService::IsConnected(const std::string &remote_engine) const {
  return channel_manager_.IsConnected(remote_engine);
}

Status FabricMemTransferService::StartKeepaliveMonitor() {
  return channel_manager_.StartKeepaliveMonitor();
}

void FabricMemTransferService::StopKeepaliveMonitor() {
  channel_manager_.StopKeepaliveMonitor();
}

Status FabricMemTransferService::PrepareChannelTransfer(const std::string &remote_engine,
                                                        std::shared_ptr<FabricMemChannel> &channel,
                                                        FabricMemTransferContext &context) const {
  HIXL_CHK_STATUS_RET(channel_manager_.GetChannel(remote_engine, channel),
                      "Fabric mem remote engine:%s is not connected.", remote_engine.c_str());
  HIXL_CHK_STATUS_RET(channel_manager_.BuildTransferContext(remote_engine, statistic_, context),
                      "Build fabric mem transfer context failed, remote:%s.", remote_engine.c_str());
  return SUCCESS;
}

void FabricMemTransferService::UnregisterSyncSlot(const std::shared_ptr<FabricMemChannel> &channel,
                                                  const AsyncSlot &slot) {
  std::lock_guard<std::mutex> lock(channel->records_mutex);
  for (auto it = channel->active_sync_slots.begin(); it != channel->active_sync_slots.end(); ++it) {
    if (it->ctx == slot.ctx) {
      (void)channel->active_sync_slots.erase(it);
      return;
    }
  }
}

Status FabricMemTransferService::IssueSyncCopy(const std::shared_ptr<FabricMemChannel> &channel, const AsyncSlot &slot,
                                               const FabricMemTransferContext &context,
                                               std::vector<TransferOpDesc> &op_descs,
                                               TransferInvocation &invocation) const {
  TemporaryRtContext ctx_guard(slot.ctx);
  // Cheap lock-free reject so a disconnecting channel fails fast with NOT_CONNECTED before any work;
  // the authoritative re-check under submit_gate below closes the race with a concurrent disconnect.
  HIXL_CHK_BOOL_RET_STATUS(!channel->disconnecting.load(std::memory_order_acquire), NOT_CONNECTED,
                           "Fabric mem channel:%s is disconnecting.", context.channel_id.c_str());
  // Resolve addresses before locking: it only reads the context's private VA snapshot and the local op
  // addrs (plus an aclrtPointerGetAttributes query), touching no shared channel state.
  HIXL_CHK_STATUS_RET(ResolveTransferAddrs(op_descs, context), "Resolve fabric mem addresses failed.");
  // Submit under a SHARED lock so concurrent transfers on the same channel issue copies in parallel; the
  // slot is registered (under records_mutex) before submitting and before releasing the shared lock, so a
  // concurrent disconnect (which takes submit_gate EXCLUSIVE) sees it and aborts the streams before unmap.
  std::shared_lock<std::shared_mutex> submit_lock(channel->submit_gate);
  HIXL_CHK_BOOL_RET_STATUS(!channel->disconnecting.load(std::memory_order_acquire), NOT_CONNECTED,
                           "Fabric mem channel:%s is disconnecting.", context.channel_id.c_str());
  {
    std::lock_guard<std::mutex> reg(channel->records_mutex);
    channel->active_sync_slots.emplace_back(slot);
  }
  invocation.real_copy_start = std::chrono::steady_clock::now();
  HIXL_CHK_STATUS_RET(ProcessCopyWithAsync(slot, invocation.operation, op_descs), "Fabric mem copy failed.");
  return SUCCESS;
}

Status FabricMemTransferService::WaitSyncStreams(const AsyncSlot &slot,
                                                 const std::chrono::steady_clock::time_point &start,
                                                 uint64_t timeout_us) const {
  TemporaryRtContext ctx_guard(slot.ctx);
  for (const auto &stream : slot.streams) {
    const auto cost = GetDurationUs(start, std::chrono::steady_clock::now());
    HIXL_CHK_BOOL_RET_STATUS(cost < timeout_us, TIMEOUT, "Fabric mem transfer timeout.");
    const uint64_t stream_timeout_ms = (timeout_us - cost) / kMillisToMicros;
    HIXL_CHK_BOOL_RET_STATUS(stream_timeout_ms > 0, TIMEOUT, "Fabric mem transfer timeout.");
    HIXL_CHK_ACL_RET(aclrtSynchronizeStreamWithTimeout(stream, stream_timeout_ms),
                     "Synchronize fabric mem stream failed.");
  }
  return SUCCESS;
}

Status FabricMemTransferService::TransferSync(const std::string &remote_engine, TransferOp operation,
                                              const std::vector<TransferOpDesc> &op_descs, int32_t timeout_in_millis) {
  HIXL_CHK_BOOL_RET_STATUS(timeout_in_millis >= 0, PARAM_INVALID,
                           "Fabric mem transfer timeout must be non-negative, got:%d.", timeout_in_millis);
  const auto start = std::chrono::steady_clock::now();
  const uint64_t timeout_us = static_cast<uint64_t>(timeout_in_millis) * kMillisToMicros;
  std::shared_ptr<FabricMemChannel> channel;
  FabricMemTransferContext context;
  HIXL_CHK_STATUS_RET(PrepareChannelTransfer(remote_engine, channel, context), "Prepare fabric mem transfer failed.");
  AsyncSlot slot;
  HIXL_CHK_STATUS_RET(slot_pool_.AcquireWithTimeout(slot, timeout_us), "Failed to acquire fabric mem transfer slot.");
  HIXL_DISMISSABLE_GUARD(fail_guard, ([this, &channel, &slot]() {
                           UnregisterSyncSlot(channel, slot);
                           slot_pool_.Release(slot, true);
                         }));
  auto op_descs_copy = op_descs;
  TransferInvocation invocation;
  invocation.operation = operation;
  HIXL_CHK_STATUS_RET(IssueSyncCopy(channel, slot, context, op_descs_copy, invocation), "Fabric mem sync copy failed.");
  HIXL_CHK_STATUS_RET(WaitSyncStreams(slot, start, timeout_us), "Wait fabric mem sync streams failed.");
  const auto real_copy_cost = GetDurationUs(invocation.real_copy_start, std::chrono::steady_clock::now());
  HIXL_DISMISS_GUARD(fail_guard);
  UnregisterSyncSlot(channel, slot);
  slot_pool_.Release(slot, false);
  const auto transfer_cost = GetDurationUs(start, std::chrono::steady_clock::now());
  UpdateStats(context, transfer_cost, real_copy_cost, GetTransferBytes(op_descs_copy),
              static_cast<uint64_t>(op_descs_copy.size()));
  HIXL_LOGI("Fabric mem transfer cost:%lu us, real copy:%lu us, channel:%s.", transfer_cost, real_copy_cost,
            context.channel_id.c_str());
  return SUCCESS;
}

Status FabricMemTransferService::IssueAsyncCopyAndRegister(const std::shared_ptr<FabricMemChannel> &channel,
                                                           AsyncSlot &slot, const FabricMemTransferContext &context,
                                                           std::vector<TransferOpDesc> &op_descs,
                                                           TransferInvocation &invocation) {
  TemporaryRtContext ctx_guard(slot.ctx);
  // Cheap lock-free reject so a disconnecting channel fails fast with NOT_CONNECTED before any work;
  // the authoritative re-check under submit_gate below closes the race with a concurrent disconnect.
  HIXL_CHK_BOOL_RET_STATUS(!channel->disconnecting.load(std::memory_order_acquire), NOT_CONNECTED,
                           "Fabric mem channel:%s is disconnecting.", context.channel_id.c_str());
  // Resolve addresses before locking (see IssueSyncCopy).
  HIXL_CHK_STATUS_RET(ResolveTransferAddrs(op_descs, context), "Resolve fabric mem addresses failed.");
  // Submit under a SHARED lock so concurrent transfers run in parallel. The record is registered (under
  // records_mutex) AFTER submitting but BEFORE releasing the shared lock, so a concurrent disconnect
  // (submit_gate EXCLUSIVE) is guaranteed to observe it and abort the slot's streams before unmap.
  std::shared_lock<std::shared_mutex> submit_lock(channel->submit_gate);
  HIXL_CHK_BOOL_RET_STATUS(!channel->disconnecting.load(std::memory_order_acquire), NOT_CONNECTED,
                           "Fabric mem channel:%s is disconnecting.", context.channel_id.c_str());
  invocation.real_copy_start = std::chrono::steady_clock::now();
  HIXL_CHK_STATUS_RET(ProcessCopyWithAsync(slot, invocation.operation, op_descs), "Fabric mem async copy failed.");
  HIXL_CHK_STATUS_RET(AppendHostFlagCopies(slot), "Failed to append fabric mem host flag copies.");
  AsyncRecord record;
  record.slot = std::move(slot);
  record.transfer_start = invocation.transfer_start;
  record.real_copy_start = invocation.real_copy_start;
  record.transfer_bytes = GetTransferBytes(op_descs);
  record.op_desc_count = static_cast<uint64_t>(op_descs.size());
  record.channel_id = context.channel_id;
  record.statistic_channel_id = context.statistic_channel_id;
  record.stat_info = context.stat_info;
  record.op_type = invocation.operation;
  record.prof_start_time = invocation.prof_start_time;
  {
    std::lock_guard<std::mutex> reg(channel->records_mutex);
    channel->async_records[invocation.req_id] = std::move(record);
  }
  channel_manager_.AddReqRoute(invocation.req_id, channel);
  return SUCCESS;
}

Status FabricMemTransferService::TransferAsync(const std::string &remote_engine, TransferOp operation,
                                               const std::vector<TransferOpDesc> &op_descs, TransferReq &req) {
  const uint64_t req_id = next_req_id_.fetch_add(1U, std::memory_order_relaxed);
  req = reinterpret_cast<TransferReq>(static_cast<uintptr_t>(req_id));
  const uint64_t prof_start_time = HixlProfilingReporter::GetSysCycleTime();
  const auto start = std::chrono::steady_clock::now();
  std::shared_ptr<FabricMemChannel> channel;
  FabricMemTransferContext context;
  HIXL_CHK_STATUS_RET(PrepareChannelTransfer(remote_engine, channel, context), "Prepare fabric mem transfer failed.");
  AsyncSlot slot;
  HIXL_CHK_STATUS_RET(slot_pool_.AcquireAsync(slot), "Failed to acquire fabric mem async slot.");
  HIXL_DISMISSABLE_GUARD(fail_guard, ([this, &slot]() { slot_pool_.Release(slot, true); }));
  auto op_descs_copy = op_descs;
  TransferInvocation invocation;
  invocation.operation = operation;
  invocation.req_id = req_id;
  invocation.prof_start_time = prof_start_time;
  invocation.transfer_start = start;
  invocation.real_copy_start = std::chrono::steady_clock::now();
  HIXL_CHK_STATUS_RET(IssueAsyncCopyAndRegister(channel, slot, context, op_descs_copy, invocation),
                      "Fabric mem async submit failed.");
  HIXL_DISMISS_GUARD(fail_guard);
  HIXL_LOGI("Fabric mem async transfer submitted, channel:%s, req:%lu, cost:%lu us.", context.channel_id.c_str(),
            req_id, GetDurationUs(start, std::chrono::steady_clock::now()));
  return SUCCESS;
}

Status FabricMemTransferService::GetTransferStatus(const TransferReq &req, TransferStatus &status,
                                                   AsyncTransferPollInfo *info) {
  const uint64_t req_id = reinterpret_cast<uintptr_t>(req);
  std::shared_ptr<FabricMemChannel> channel;
  // Unknown req / route already removed (completed) / route cleared by Disconnect abort -> PARAM_INVALID.
  // FindChannelByReq returns FAILED internally; map to PARAM_INVALID for the caller-facing contract.
  HIXL_CHK_BOOL_RET_STATUS(channel_manager_.FindChannelByReq(req_id, channel) == SUCCESS, PARAM_INVALID,
                           "Fabric mem request:%lu not found.", req_id);

  AsyncRecord async_record;
  bool host_flags_done = false;
  AsyncStreamQueryResult query_result = AsyncStreamQueryResult::kFailed;
  {
    // Single critical section: locate the record once, then decide completion via the cheap host-flag
    // poll first and fall back to aclrtStreamQuery (with its rt-context setup) only when the flags are
    // not yet set. Holding records_mutex across the lookup and extraction stops a concurrent Disconnect
    // (AbortAndClearChannelRecords) from moving the slot out and destroying its streams in between; the
    // blocking SynchronizeAsyncSlotStreams below runs only after the record is locally owned.
    std::lock_guard<std::mutex> lock(channel->records_mutex);
    const auto it = channel->async_records.find(req_id);
    HIXL_CHK_BOOL_RET_STATUS(it != channel->async_records.end(), PARAM_INVALID, "Fabric mem request:%lu not found.",
                             req_id);
    host_flags_done = AllHostFlagsDone(it->second.slot);
    if (!host_flags_done) {
      TemporaryRtContext ctx_guard(it->second.slot.ctx);
      query_result = QueryAsyncSlotStreams(it->second.slot);
      if (query_result == AsyncStreamQueryResult::kWaiting) {
        status = TransferStatus::WAITING;
        return SUCCESS;
      }
    }
    async_record = std::move(it->second);
    (void)channel->async_records.erase(it);
  }

  Status ret;
  if (host_flags_done) {
    // Host completion flags are already set (acquire fence in AllHostFlagsDone), so the copy results are
    // guaranteed visible and the stream sync can be skipped.
    ret = CompleteAsyncTransferAndUpdateStats(req_id, async_record, status);
  } else if (query_result == AsyncStreamQueryResult::kComplete &&
             SynchronizeAsyncSlotStreams(async_record.slot) == SUCCESS) {
    ret = CompleteAsyncTransferAndUpdateStats(req_id, async_record, status);
  } else {
    ret = HandleAsyncStreamQueryFailure(req_id, async_record, status);
  }
  FillPollInfo(async_record, info);
  channel_manager_.RemoveReqRoute(req_id);
  return ret;
}

Status FabricMemTransferService::HandleAsyncStreamQueryFailure(uint64_t req_id, AsyncRecord &async_record,
                                                               TransferStatus &status) {
  slot_pool_.Release(async_record.slot, true);
  status = TransferStatus::FAILED;
  HIXL_LOGE(FAILED, "Fabric mem async transfer failed on stream query, req:%lu.", req_id);
  return SUCCESS;
}

Status FabricMemTransferService::AppendHostFlagCopies(const AsyncSlot &slot) const {
  HIXL_CHK_BOOL_RET_STATUS(slot.streams.size() == slot.host_flags.size(), FAILED,
                           "Fabric mem async slot stream/flag size mismatch.");
  for (size_t i = 0U; i < slot.streams.size(); ++i) {
    HIXL_CHK_ACL_RET(aclrtMemcpyAsync(slot.host_flags[i], kHostFlagSize, dev_const_one_, kHostFlagSize,
                                      ACL_MEMCPY_DEVICE_TO_HOST, slot.streams[i]),
                     "Fabric mem host flag D2H copy failed.");
  }
  return SUCCESS;
}

bool FabricMemTransferService::AllHostFlagsDone(const AsyncSlot &slot) {
  for (void *host_flag : slot.host_flags) {
    const volatile uint64_t *flag_ptr = static_cast<const volatile uint64_t *>(host_flag);
    if (*flag_ptr != kHostFlagDoneValue) {
      return false;
    }
  }
  if (slot.host_flags.empty()) {
    return false;
  }
  // Ensure all preceding device-to-host DMA writes (including data transfer results) are
  // visible to this thread before the caller observes the completion status.
  std::atomic_thread_fence(std::memory_order_acquire);
  return true;
}

FabricMemTransferService::AsyncStreamQueryResult FabricMemTransferService::QueryAsyncSlotStreams(
    const AsyncSlot &slot) {
  if (slot.streams.empty()) {
    HIXL_LOGE(FAILED, "Fabric mem async slot has no streams.");
    return AsyncStreamQueryResult::kFailed;
  }
  bool all_complete = true;
  for (size_t i = 0U; i < slot.streams.size(); ++i) {
    aclrtStreamStatus stream_status = ACL_STREAM_STATUS_RESERVED;
    const aclError ret = aclrtStreamQuery(slot.streams[i], &stream_status);
    if (ret != ACL_SUCCESS) {
      HIXL_LOGE(FAILED, "Fabric mem aclrtStreamQuery failed, stream[%zu]:%p, ret:%d.", i,
                static_cast<void *>(slot.streams[i]), ret);
      return AsyncStreamQueryResult::kFailed;
    }
    if (stream_status != ACL_STREAM_STATUS_NOT_READY && stream_status != ACL_STREAM_STATUS_COMPLETE) {
      HIXL_LOGE(FAILED, "Fabric mem aclrtStreamQuery returned unexpected status:%d, stream[%zu]:%p.",
                static_cast<int32_t>(stream_status), i, static_cast<void *>(slot.streams[i]));
      return AsyncStreamQueryResult::kFailed;
    }
    if (stream_status != ACL_STREAM_STATUS_COMPLETE) {
      all_complete = false;
    }
  }
  return all_complete ? AsyncStreamQueryResult::kComplete : AsyncStreamQueryResult::kWaiting;
}

Status FabricMemTransferService::SynchronizeAsyncSlotStreams(const AsyncSlot &slot) {
  TemporaryRtContext ctx_guard(slot.ctx);
  for (const auto &stream : slot.streams) {
    HIXL_CHK_ACL_RET(aclrtSynchronizeStream(stream), "Synchronize fabric mem async stream failed.");
  }
  return SUCCESS;
}

Status FabricMemTransferService::CompleteAsyncTransferAndUpdateStats(uint64_t req_id, AsyncRecord &async_record,
                                                                     TransferStatus &status) {
  slot_pool_.Release(async_record.slot, false);
  const auto end = std::chrono::steady_clock::now();
  const auto real_copy_cost = GetDurationUs(async_record.real_copy_start, end);
  const auto transfer_cost = GetDurationUs(async_record.transfer_start, end);
  UpdateStats(async_record, transfer_cost, real_copy_cost, async_record.transfer_bytes, async_record.op_desc_count);
  status = TransferStatus::COMPLETED;
  HIXL_LOGI("Fabric mem async transfer completed, channel:%s, req:%lu, cost:%lu us, real copy:%lu us.",
            async_record.channel_id.c_str(), req_id, transfer_cost, real_copy_cost);
  return SUCCESS;
}

void FabricMemTransferService::FillPollInfo(const AsyncRecord &record, AsyncTransferPollInfo *info) {
  if (info == nullptr) {
    return;
  }
  info->op_type = record.op_type;
  info->prof_start_time = record.prof_start_time;
  info->channel_id = record.channel_id;
}

void FabricMemTransferService::CleanupAsyncTransfer(const TransferReq &req) {
  const uint64_t req_id = reinterpret_cast<uintptr_t>(req);
  std::shared_ptr<FabricMemChannel> channel;
  if (channel_manager_.FindChannelByReq(req_id, channel) != SUCCESS) {
    return;
  }
  AsyncSlot slot;
  bool found = false;
  {
    std::lock_guard<std::mutex> lock(channel->records_mutex);
    const auto it = channel->async_records.find(req_id);
    if (it != channel->async_records.end()) {
      slot = std::move(it->second.slot);
      (void)channel->async_records.erase(it);
      found = true;
    }
  }
  if (found) {
    slot_pool_.Release(slot, true);
  }
  channel_manager_.RemoveReqRoute(req_id);
}

Status FabricMemTransferService::ResolveTransferAddrs(std::vector<TransferOpDesc> &op_descs,
                                                      const FabricMemTransferContext &context) const {
  // Pure address resolution, intentionally lock-free: reads only the context's private VA snapshot and
  // the caller-owned op_descs (and queries the local pointer's location), so it can run outside the
  // submit lock without racing a concurrent disconnect/unmap.
  bool need_trans_local_addr = false;
  HIXL_CHK_STATUS_RET(NeedTransLocalAddr(op_descs, need_trans_local_addr),
                      "Check local fabric mem address type failed.");
  for (auto &op : op_descs) {
    uintptr_t new_remote_addr = 0;
    HIXL_CHK_STATUS_RET(TransOpAddr(op.remote_addr, op.len, context.remote_va_to_old_va, new_remote_addr),
                        "Remote fabric mem address is not registered.");
    op.remote_addr = new_remote_addr;
  }
  if (need_trans_local_addr) {
    HIXL_CHK_BOOL_RET_STATUS(local_memory_ != nullptr, FAILED, "Fabric mem local memory is not available.");
    HIXL_CHK_STATUS_RET(local_memory_->TranslateLocalHostOpAddrs(op_descs),
                        "Local host fabric mem address translation failed.");
  }
  return SUCCESS;
}

Status FabricMemTransferService::TransOpAddr(uintptr_t old_addr, size_t len,
                                             const std::unordered_map<uintptr_t, VaInfo> &new_va_to_old_va,
                                             uintptr_t &new_addr) {
  for (const auto &item : new_va_to_old_va) {
    const auto &info = item.second;
    if (IsRangeContained(old_addr, len, info.va_addr, info.len)) {
      new_addr = item.first + (old_addr - info.va_addr);
      return SUCCESS;
    }
  }
  HIXL_LOGE(PARAM_INVALID, "Fabric mem address:%lu, len:%zu not found in registered segments.", old_addr, len);
  return PARAM_INVALID;
}

Status FabricMemTransferService::ProcessCopyWithAsync(const AsyncSlot &slot, TransferOp operation,
                                                      const std::vector<TransferOpDesc> &op_descs) {
  HIXL_CHK_BOOL_RET_STATUS(!slot.streams.empty(), PARAM_INVALID, "Fabric mem copy streams cannot be empty.");
  const size_t stream_count = slot.streams.size();
  size_t stream_idx = 0U;
  for (const auto &op : op_descs) {
    auto &stream = slot.streams[stream_idx];
    if (++stream_idx >= stream_count) {
      stream_idx = 0U;
    }
    if (operation == TransferOp::WRITE) {
      HIXL_CHK_ACL_RET(
          aclrtMemcpyAsync(reinterpret_cast<void *>(op.remote_addr), op.len, reinterpret_cast<void *>(op.local_addr),
                           op.len, ACL_MEMCPY_DEVICE_TO_DEVICE, stream),
          "Fabric mem write copy failed.");
      continue;
    }
    HIXL_CHK_BOOL_RET_STATUS(operation == TransferOp::READ, PARAM_INVALID, "Invalid fabric mem transfer operation.");
    HIXL_CHK_ACL_RET(
        aclrtMemcpyAsync(reinterpret_cast<void *>(op.local_addr), op.len, reinterpret_cast<void *>(op.remote_addr),
                         op.len, ACL_MEMCPY_DEVICE_TO_DEVICE, stream),
        "Fabric mem read copy failed.");
  }
  return SUCCESS;
}

void FabricMemTransferService::UpdateStats(const FabricMemTransferContext &context, uint64_t transfer_cost,
                                           uint64_t real_copy_cost, uint64_t transfer_bytes,
                                           uint64_t op_desc_count) const {
  if (statistic_ == nullptr) {
    return;
  }
  if (context.stat_info != nullptr) {
    statistic_->UpdateCostsDirect(*context.stat_info, transfer_cost, real_copy_cost, transfer_bytes, op_desc_count);
    return;
  }
  const auto &stat_channel = context.statistic_channel_id.empty() ? context.channel_id : context.statistic_channel_id;
  statistic_->UpdateCosts(stat_channel, transfer_cost, real_copy_cost, transfer_bytes, op_desc_count);
}

void FabricMemTransferService::UpdateStats(const AsyncRecord &record, uint64_t transfer_cost, uint64_t real_copy_cost,
                                           uint64_t transfer_bytes, uint64_t op_desc_count) const {
  if (statistic_ == nullptr) {
    return;
  }
  if (record.stat_info != nullptr) {
    statistic_->UpdateCostsDirect(*record.stat_info, transfer_cost, real_copy_cost, transfer_bytes, op_desc_count);
    return;
  }
  const auto &stat_channel = record.statistic_channel_id.empty() ? record.channel_id : record.statistic_channel_id;
  statistic_->UpdateCosts(stat_channel, transfer_cost, real_copy_cost, transfer_bytes, op_desc_count);
}

Status FabricMemTransferService::NeedTransLocalAddr(const std::vector<TransferOpDesc> &op_descs,
                                                    bool &need_trans_local_addr) const {
  need_trans_local_addr = false;
  if (op_descs.empty() || local_memory_ == nullptr || !local_memory_->HasHostMemory()) {
    return SUCCESS;
  }
  aclrtPtrAttributes attributes{};
  HIXL_CHK_ACL_RET(aclrtPointerGetAttributes(reinterpret_cast<void *>(op_descs[0].local_addr), &attributes),
                   "Get local pointer attributes failed.");
  need_trans_local_addr = (attributes.location.type == ACL_MEM_LOCATION_TYPE_HOST);
  return SUCCESS;
}
}  // namespace hixl
