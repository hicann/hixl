/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "fabric_mem/fabric_mem_host_transfer_service.h"

#include <shared_mutex>
#include <utility>

#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/hixl_utils.h"
#include "common/scope_guard.h"
#include "profiling/prof_api_reg.h"

namespace hixl {

Status FabricMemHostTransferService::Initialize(const FabricMemTransferServiceInitParam &param) {
  HIXL_CHK_BOOL_RET_STATUS(!param.enable_aicpu_unfold, PARAM_INVALID,
                           "FabricMemHostTransferService does not support enable_aicpu_unfold.");
  HIXL_DISMISSABLE_GUARD(fail_guard, ([this]() { Finalize(); }));
  HIXL_CHK_STATUS_RET(InitCommon(param, false), "Initialize fabric mem host transfer service common failed.");
  HIXL_DISMISS_GUARD(fail_guard);
  return SUCCESS;
}

void FabricMemHostTransferService::UnregisterSyncSlot(const std::shared_ptr<FabricMemChannel> &channel,
                                                      const AsyncSlot &slot) {
  std::lock_guard<std::mutex> lock(channel->records_mutex);
  for (auto it = channel->active_sync_slots.begin(); it != channel->active_sync_slots.end(); ++it) {
    if (it->ctx == slot.ctx) {
      (void)channel->active_sync_slots.erase(it);
      return;
    }
  }
}

Status FabricMemHostTransferService::IssueSyncCopy(const std::shared_ptr<FabricMemChannel> &channel,
                                                   const AsyncSlot &slot, const FabricMemTransferContext &context,
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

Status FabricMemHostTransferService::TransferSync(const std::string &remote_engine, TransferOp operation,
                                                  const std::vector<TransferOpDesc> &op_descs,
                                                  int32_t timeout_in_millis) {
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
  HIXL_CHK_STATUS_RET(WaitControlStreamsWithTimeout(slot, start, timeout_us), "Wait fabric mem sync streams failed.");
  const auto real_copy_cost = GetDurationUs(invocation.real_copy_start, std::chrono::steady_clock::now());
  HIXL_DISMISS_GUARD(fail_guard);
  UnregisterSyncSlot(channel, slot);
  slot_pool_.Release(slot, false);
  const auto transfer_cost = GetDurationUs(start, std::chrono::steady_clock::now());
  UpdateStats(context.channel_id, context.statistic_channel_id, context.stat_info, transfer_cost, real_copy_cost,
              GetTransferBytes(op_descs_copy), static_cast<uint64_t>(op_descs_copy.size()));
  HIXL_LOGI("Fabric mem transfer cost:%lu us, real copy:%lu us, channel:%s.", transfer_cost, real_copy_cost,
            context.channel_id.c_str());
  return SUCCESS;
}

Status FabricMemHostTransferService::IssueAsyncCopyAndRegister(const std::shared_ptr<FabricMemChannel> &channel,
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

Status FabricMemHostTransferService::TransferAsync(const std::string &remote_engine, TransferOp operation,
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

Status FabricMemHostTransferService::GetTransferStatus(const TransferReq &req, TransferStatus &status,
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

Status FabricMemHostTransferService::HandleAsyncStreamQueryFailure(uint64_t req_id, AsyncRecord &async_record,
                                                                   TransferStatus &status) {
  slot_pool_.Release(async_record.slot, true);
  status = TransferStatus::FAILED;
  HIXL_LOGE(FAILED, "Fabric mem async transfer failed on stream query, req:%lu.", req_id);
  return SUCCESS;
}

Status FabricMemHostTransferService::SynchronizeAsyncSlotStreams(const AsyncSlot &slot) {
  TemporaryRtContext ctx_guard(slot.ctx);
  for (const auto &stream : slot.streams) {
    HIXL_CHK_ACL_RET(aclrtSynchronizeStream(stream), "Synchronize fabric mem async stream failed.");
  }
  return SUCCESS;
}

Status FabricMemHostTransferService::CompleteAsyncTransferAndUpdateStats(uint64_t req_id, AsyncRecord &async_record,
                                                                         TransferStatus &status) {
  slot_pool_.Release(async_record.slot, false);
  const auto end = std::chrono::steady_clock::now();
  const auto real_copy_cost = GetDurationUs(async_record.real_copy_start, end);
  const auto transfer_cost = GetDurationUs(async_record.transfer_start, end);
  UpdateStats(async_record.channel_id, async_record.statistic_channel_id, async_record.stat_info, transfer_cost,
              real_copy_cost, async_record.transfer_bytes, async_record.op_desc_count);
  status = TransferStatus::COMPLETED;
  HIXL_LOGI("Fabric mem async transfer completed, channel:%s, req:%lu, cost:%lu us, real copy:%lu us.",
            async_record.channel_id.c_str(), req_id, transfer_cost, real_copy_cost);
  return SUCCESS;
}

void FabricMemHostTransferService::CleanupAsyncTransfer(const TransferReq &req) {
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

Status FabricMemHostTransferService::ProcessCopyWithAsync(const AsyncSlot &slot, TransferOp operation,
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
}  // namespace hixl
