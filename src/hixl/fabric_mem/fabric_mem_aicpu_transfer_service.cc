/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "fabric_mem/fabric_mem_aicpu_transfer_service.h"

#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/hixl_utils.h"
#include "common/scope_guard.h"
#include "profiling/prof_api_reg.h"

#include <utility>

namespace hixl {
namespace {
constexpr uint64_t kHostFlagInitValue = 0ULL;
constexpr size_t kHostFlagSize = sizeof(uint64_t);
}  // namespace

FabricMemAicpuTransferService::~FabricMemAicpuTransferService() {
  // Qualify to avoid virtual dispatch from the destructor. Must run before the base
  // destructor: base Finalize still needs the AICPU dispatcher for channel/slot abort.
  FabricMemAicpuTransferService::Finalize();
}

Status FabricMemAicpuTransferService::Initialize(const FabricMemTransferServiceInitParam &param) {
  HIXL_CHK_BOOL_RET_STATUS(param.task_stream_num == 1U, PARAM_INVALID,
                           "enable_aicpu_unfold requires task_stream_num=1, got %zu.", param.task_stream_num);
  HIXL_DISMISSABLE_GUARD(fail_guard, ([this]() { Finalize(); }));
  const Status dispatcher_status = aicpu_dispatcher_.Initialize(param.device_id);
  if (dispatcher_status != SUCCESS) {
    HIXL_LOGE(UNSUPPORTED, "FabricMem AICPU unfold requested but unavailable on device:%d.", param.device_id);
    return UNSUPPORTED;
  }
  HIXL_CHK_STATUS_RET(InitCommon(param, true), "Initialize fabric mem AICPU transfer service common failed.");
  slot_pool_.SetAicpuDispatcher(&aicpu_dispatcher_);
  HIXL_DISMISS_GUARD(fail_guard);
  return SUCCESS;
}

void FabricMemAicpuTransferService::Finalize() {
  // Base Finalize aborts the remaining channels and slots, which still needs the dispatcher.
  FabricMemTransferService::Finalize();
  slot_pool_.SetAicpuDispatcher(nullptr);
  aicpu_dispatcher_.Finalize();
}

Status FabricMemAicpuTransferService::AllocateTransferHostFlags(AsyncSlot &slot) {
  HIXL_CHK_BOOL_RET_STATUS(!slot.streams.empty(), PARAM_INVALID, "Fabric mem copy streams cannot be empty.");
  // Drop any borrowed pool-flag view before installing per-transfer flags (hixl_cs style).
  slot.host_flags.clear();
  slot.owns_host_flags = false;
  std::vector<void *> flags;
  flags.reserve(slot.streams.size());
  HIXL_DISMISSABLE_GUARD(flag_guard, ([&flags]() {
                           for (void *host_flag : flags) {
                             if (host_flag != nullptr) {
                               (void)aclrtFreeHost(host_flag);
                             }
                           }
                           flags.clear();
                         }));
  for (size_t i = 0U; i < slot.streams.size(); ++i) {
    void *host_flag = nullptr;
    HIXL_CHK_ACL_RET(aclrtMallocHost(&host_flag, kHostFlagSize), "Allocate fabric mem transfer host flag failed.");
    *static_cast<uint64_t *>(host_flag) = kHostFlagInitValue;
    flags.emplace_back(host_flag);
  }
  slot.host_flags = std::move(flags);
  slot.owns_host_flags = true;
  HIXL_DISMISS_GUARD(flag_guard);
  return SUCCESS;
}

Status FabricMemAicpuTransferService::AcquireBoundSlotLocked(const std::shared_ptr<FabricMemChannel> &channel,
                                                             AsyncSlot &slot, uint64_t timeout_us, bool async_acquire) {
  HIXL_CHK_BOOL_RET_STATUS(!channel->disconnecting.load(std::memory_order_acquire), NOT_CONNECTED,
                           "Fabric mem channel is disconnecting.");
  if (channel->bound_slot_refs > 0U) {
    // Reuse the channel's active slot like hixl_cs AcquireSharedSlot.
    HIXL_CHK_BOOL_RET_STATUS(channel->bound_slot.ctx != nullptr, FAILED,
                             "Fabric mem AICPU bound slot missing with non-zero refs.");
    slot = channel->bound_slot;
    ++channel->bound_slot_refs;
    return SUCCESS;
  }

  Status acquire_status = SUCCESS;
  if (async_acquire) {
    acquire_status = slot_pool_.AcquireAsync(slot);
  } else {
    acquire_status = slot_pool_.AcquireWithTimeout(slot, timeout_us);
  }
  HIXL_CHK_STATUS_RET(acquire_status, "Failed to acquire fabric mem AICPU transfer slot.");
  // Slot is occupied; abort it if worker stream / notify / transfer-context setup fails before binding.
  HIXL_DISMISSABLE_GUARD(slot_guard, ([this, &slot]() { slot_pool_.AbortSlot(slot, {}); }));
  HIXL_CHK_STATUS_RET(slot_pool_.EnsureAicpuRtsqStreams(slot), "Create FabricMem AICPU RTSQ worker streams failed.");
  slot.has_aicpu_unfold = true;
  HIXL_CHK_STATUS_RET(aicpu_dispatcher_.AddTransferContext(slot), "Register FabricMem AICPU transfer context failed.");
  channel->bound_slot = slot;
  channel->bound_slot_refs = 1U;
  HIXL_DISMISS_GUARD(slot_guard);
  return SUCCESS;
}

void FabricMemAicpuTransferService::ReleaseBoundSlotLocked(const std::shared_ptr<FabricMemChannel> &channel,
                                                           AsyncSlot &slot) {
  // Success path only: the last owner returns the intact slot to the pool for reuse. Anything that
  // failed goes through AbortAicpuChannelLocked instead, which destroys the slot.
  FabricMemSlotPool::DestroyOwnedHostFlags(slot);
  slot = AsyncSlot{};
  if (channel->bound_slot_refs == 0U) {
    return;
  }
  --channel->bound_slot_refs;
  if (channel->bound_slot_refs > 0U) {
    return;
  }
  AsyncSlot pool_slot = channel->bound_slot;
  channel->bound_slot = AsyncSlot{};
  slot_pool_.Release(pool_slot, false);
}

void FabricMemAicpuTransferService::CleanupFailedTransferLocked(const std::shared_ptr<FabricMemChannel> &channel,
                                                                AsyncSlot &slot,
                                                                FabricMemAicpuRequestResource &aicpu_resource) {
  // An allocated resource means Submit got far enough to enqueue a kernel that reads it, so the
  // channel's shared control stream is no longer trustworthy and every pipelined sibling dies with
  // it: run the full abort. Without one, nothing reached the device and the channel stays usable.
  if (aicpu_resource.descriptor_buffer == nullptr && aicpu_resource.status_buffer == nullptr) {
    ReleaseBoundSlotLocked(channel, slot);
    return;
  }
  channel_manager_.AbortAicpuChannelLocked(channel, &slot, &aicpu_resource);
}

Status FabricMemAicpuTransferService::IssueCopyLocked(const std::shared_ptr<FabricMemChannel> &channel, AsyncSlot &slot,
                                                      const FabricMemTransferContext &context,
                                                      std::vector<TransferOpDesc> &op_descs,
                                                      TransferInvocation &invocation,
                                                      FabricMemAicpuRequestResource &aicpu_resource) {
  TemporaryRtContext ctx_guard(slot.ctx);
  HIXL_CHK_BOOL_RET_STATUS(!channel->disconnecting.load(std::memory_order_acquire), NOT_CONNECTED,
                           "Fabric mem channel:%s is disconnecting.", context.channel_id.c_str());
  HIXL_CHK_STATUS_RET(ResolveTransferAddrs(op_descs, context), "Resolve fabric mem addresses failed.");
  invocation.transfer_bytes = GetTransferBytes(op_descs);
  invocation.op_desc_count = static_cast<uint64_t>(op_descs.size());
  invocation.real_copy_start = std::chrono::steady_clock::now();
  return ProcessCopyWithAsync(slot, invocation.operation, op_descs, aicpu_resource, invocation.rtsq_timeout_ms);
}

Status FabricMemAicpuTransferService::CompleteSyncTransferLocked(const std::shared_ptr<FabricMemChannel> &channel,
                                                                 AsyncSlot &slot,
                                                                 FabricMemAicpuRequestResource &aicpu_resource,
                                                                 const FabricMemTransferContext &context,
                                                                 const TransferInvocation &invocation) {
  HIXL_CHK_BOOL_RET_STATUS(!channel->disconnecting.load(std::memory_order_acquire), NOT_CONNECTED,
                           "Fabric mem channel is disconnecting.");
  {
    TemporaryRtContext ctx_guard(slot.ctx);
    HIXL_CHK_STATUS_RET(FabricMemAicpuDispatcher::CheckRequestStatus(aicpu_resource),
                        "FabricMem AICPU sync launch reported failure.");
    FabricMemAicpuDispatcher::ReleaseRequestResource(aicpu_resource);
  }
  const auto end = std::chrono::steady_clock::now();
  const auto real_copy_cost = GetDurationUs(invocation.real_copy_start, end);
  ReleaseBoundSlotLocked(channel, slot);
  const auto transfer_cost = GetDurationUs(invocation.transfer_start, end);
  UpdateStats(context.channel_id, context.statistic_channel_id, context.stat_info, transfer_cost, real_copy_cost,
              invocation.transfer_bytes, invocation.op_desc_count);
  HIXL_LOGD("Fabric mem AICPU transfer cost:%lu us, real copy:%lu us, channel:%s.", transfer_cost, real_copy_cost,
            context.channel_id.c_str());
  return SUCCESS;
}

Status FabricMemAicpuTransferService::TransferSync(const std::string &remote_engine, TransferOp operation,
                                                   const std::vector<TransferOpDesc> &op_descs,
                                                   int32_t timeout_in_millis) {
  HIXL_CHK_BOOL_RET_STATUS(timeout_in_millis >= 0, PARAM_INVALID,
                           "Fabric mem transfer timeout must be non-negative, got:%d.", timeout_in_millis);
  const auto start = std::chrono::steady_clock::now();
  const uint64_t timeout_us = static_cast<uint64_t>(timeout_in_millis) * kMillisToMicros;
  std::shared_ptr<FabricMemChannel> channel;
  FabricMemTransferContext context;
  HIXL_CHK_STATUS_RET(PrepareChannelTransfer(remote_engine, channel, context), "Prepare fabric mem transfer failed.");

  std::unique_lock<std::mutex> lock(channel->transfer_mu);
  AsyncSlot slot;
  HIXL_CHK_STATUS_RET(AcquireBoundSlotLocked(channel, slot, timeout_us, false),
                      "Begin fabric mem AICPU sync transfer failed.");
  FabricMemAicpuRequestResource aicpu_resource;
  HIXL_DISMISSABLE_GUARD(fail_guard, ([this, &channel, &slot, &aicpu_resource]() {
                           CleanupFailedTransferLocked(channel, slot, aicpu_resource);
                         }));
  auto op_descs_copy = op_descs;
  TransferInvocation invocation;
  invocation.operation = operation;
  invocation.transfer_start = start;
  // Kernel treats timeout_ms==0 as "use async default (60s)". Sync must never pass 0 when the
  // caller budget is already exhausted (or truncated below 1ms); fail with TIMEOUT instead.
  const auto elapsed_us = GetDurationUs(start, std::chrono::steady_clock::now());
  HIXL_CHK_BOOL_RET_STATUS(elapsed_us < timeout_us, TIMEOUT, "Fabric mem AICPU transfer timeout.");
  const uint64_t remaining_ms = (timeout_us - elapsed_us) / kMillisToMicros;
  HIXL_CHK_BOOL_RET_STATUS(remaining_ms > 0U, TIMEOUT, "Fabric mem AICPU transfer timeout.");
  invocation.rtsq_timeout_ms = static_cast<uint32_t>(remaining_ms);
  HIXL_CHK_STATUS_RET(IssueCopyLocked(channel, slot, context, op_descs_copy, invocation, aicpu_resource),
                      "Fabric mem AICPU sync copy failed.");
  HIXL_CHK_STATUS_RET(WaitControlStreamsWithTimeout(slot, start, timeout_us),
                      "Wait fabric mem AICPU sync streams failed.");
  HIXL_CHK_STATUS_RET(CompleteSyncTransferLocked(channel, slot, aicpu_resource, context, invocation),
                      "Complete fabric mem AICPU sync transfer failed.");
  HIXL_DISMISS_GUARD(fail_guard);
  return SUCCESS;
}

Status FabricMemAicpuTransferService::TransferAsync(const std::string &remote_engine, TransferOp operation,
                                                    const std::vector<TransferOpDesc> &op_descs, TransferReq &req) {
  const uint64_t req_id = next_req_id_.fetch_add(1U, std::memory_order_relaxed);
  req = reinterpret_cast<TransferReq>(static_cast<uintptr_t>(req_id));
  const uint64_t prof_start_time = HixlProfilingReporter::GetSysCycleTime();
  const auto start = std::chrono::steady_clock::now();
  std::shared_ptr<FabricMemChannel> channel;
  FabricMemTransferContext context;
  HIXL_CHK_STATUS_RET(PrepareChannelTransfer(remote_engine, channel, context), "Prepare fabric mem transfer failed.");

  std::unique_lock<std::mutex> lock(channel->transfer_mu);
  AsyncSlot slot;
  HIXL_CHK_STATUS_RET(AcquireBoundSlotLocked(channel, slot, 0U, true), "Begin fabric mem AICPU async transfer failed.");
  FabricMemAicpuRequestResource aicpu_resource;
  HIXL_DISMISSABLE_GUARD(fail_guard, ([this, &channel, &slot, &aicpu_resource]() {
                           CleanupFailedTransferLocked(channel, slot, aicpu_resource);
                         }));
  auto op_descs_copy = op_descs;
  TransferInvocation invocation;
  invocation.operation = operation;
  invocation.req_id = req_id;
  invocation.prof_start_time = prof_start_time;
  invocation.transfer_start = start;
  HIXL_CHK_STATUS_RET(IssueCopyLocked(channel, slot, context, op_descs_copy, invocation, aicpu_resource),
                      "Fabric mem AICPU async copy failed.");
  HIXL_CHK_STATUS_RET(AllocateTransferHostFlags(slot), "Allocate fabric mem AICPU async host flags failed.");
  {
    TemporaryRtContext ctx_guard(slot.ctx);
    HIXL_CHK_STATUS_RET(AppendHostFlagCopies(slot), "Failed to append fabric mem host flag copies.");
  }
  HIXL_CHK_BOOL_RET_STATUS(!channel->disconnecting.load(std::memory_order_acquire), NOT_CONNECTED,
                           "Fabric mem channel:%s is disconnecting.", context.channel_id.c_str());
  RegisterAsyncTransferRecord(req_id, channel, context, invocation, slot, aicpu_resource);
  HIXL_DISMISS_GUARD(fail_guard);
  HIXL_LOGD("Fabric mem AICPU async transfer submitted, channel:%s, req:%lu, cost:%lu us.", context.channel_id.c_str(),
            req_id, GetDurationUs(start, std::chrono::steady_clock::now()));
  return SUCCESS;
}

void FabricMemAicpuTransferService::RegisterAsyncTransferRecord(uint64_t req_id,
                                                                const std::shared_ptr<FabricMemChannel> &channel,
                                                                const FabricMemTransferContext &context,
                                                                const TransferInvocation &invocation, AsyncSlot &slot,
                                                                FabricMemAicpuRequestResource &aicpu_resource) {
  AsyncRecord record;
  record.slot = std::move(slot);
  record.aicpu_resource = std::move(aicpu_resource);
  record.transfer_start = invocation.transfer_start;
  record.real_copy_start = invocation.real_copy_start;
  record.transfer_bytes = invocation.transfer_bytes;
  record.op_desc_count = invocation.op_desc_count;
  record.channel_id = context.channel_id;
  record.statistic_channel_id = context.statistic_channel_id;
  record.stat_info = context.stat_info;
  record.op_type = invocation.operation;
  record.prof_start_time = invocation.prof_start_time;
  {
    std::lock_guard<std::mutex> reg(channel->records_mutex);
    channel->async_records[req_id] = std::move(record);
  }
  channel_manager_.AddReqRoute(req_id, channel);
}

Status FabricMemAicpuTransferService::GetTransferStatus(const TransferReq &req, TransferStatus &status,
                                                        AsyncTransferPollInfo *info) {
  const uint64_t req_id = reinterpret_cast<uintptr_t>(req);
  std::shared_ptr<FabricMemChannel> channel;
  HIXL_CHK_BOOL_RET_STATUS(channel_manager_.FindChannelByReq(req_id, channel) == SUCCESS, PARAM_INVALID,
                           "Fabric mem request:%lu not found.", req_id);
  std::lock_guard<std::mutex> transfer_lock(channel->transfer_mu);

  AsyncRecord async_record;
  bool host_flags_done = false;
  {
    std::lock_guard<std::mutex> lock(channel->records_mutex);
    const auto it = channel->async_records.find(req_id);
    HIXL_CHK_BOOL_RET_STATUS(it != channel->async_records.end(), PARAM_INVALID, "Fabric mem request:%lu not found.",
                             req_id);
    host_flags_done = AllHostFlagsDone(it->second.slot);
    if (!host_flags_done) {
      TemporaryRtContext ctx_guard(it->second.slot.ctx);
      if (QueryAsyncSlotStreams(it->second.slot) != AsyncStreamQueryResult::kFailed) {
        status = TransferStatus::WAITING;
        return SUCCESS;
      }
    }
    async_record = std::move(it->second);
    (void)channel->async_records.erase(it);
  }

  Status ret = SUCCESS;
  if (host_flags_done) {
    ret = CompleteAsyncTransferAndUpdateStats(req_id, channel, async_record, status);
  } else {
    ret = HandleAsyncStreamQueryFailure(req_id, channel, async_record, status);
  }
  if (ret != SUCCESS) {
    return ret;
  }
  FillPollInfo(async_record, info);
  channel_manager_.RemoveReqRoute(req_id);
  return ret;
}

Status FabricMemAicpuTransferService::HandleAsyncStreamQueryFailure(uint64_t req_id,
                                                                    const std::shared_ptr<FabricMemChannel> &channel,
                                                                    AsyncRecord &async_record, TransferStatus &status) {
  CleanupFailedTransferLocked(channel, async_record.slot, async_record.aicpu_resource);
  status = TransferStatus::FAILED;
  HIXL_LOGE(FAILED, "Fabric mem AICPU async transfer failed on stream query, req:%lu.", req_id);
  return SUCCESS;
}

Status FabricMemAicpuTransferService::CompleteAsyncTransferAndUpdateStats(
    uint64_t req_id, const std::shared_ptr<FabricMemChannel> &channel, AsyncRecord &async_record,
    TransferStatus &status) {
  Status launch_status = SUCCESS;
  {
    TemporaryRtContext ctx_guard(async_record.slot.ctx);
    launch_status = FabricMemAicpuDispatcher::CheckRequestStatus(async_record.aicpu_resource);
    if (launch_status == SUCCESS) {
      FabricMemAicpuDispatcher::ReleaseRequestResource(async_record.aicpu_resource);
    }
  }
  if (launch_status != SUCCESS) {
    // The kernel reported a submit error; let the abort flow free the descriptors it may still hold.
    CleanupFailedTransferLocked(channel, async_record.slot, async_record.aicpu_resource);
    status = TransferStatus::FAILED;
    HIXL_LOGE(launch_status, "FabricMem AICPU async launch reported failure, req:%lu.", req_id);
    return SUCCESS;
  }
  ReleaseBoundSlotLocked(channel, async_record.slot);
  const auto end = std::chrono::steady_clock::now();
  const auto real_copy_cost = GetDurationUs(async_record.real_copy_start, end);
  const auto transfer_cost = GetDurationUs(async_record.transfer_start, end);
  UpdateStats(async_record.channel_id, async_record.statistic_channel_id, async_record.stat_info, transfer_cost,
              real_copy_cost, async_record.transfer_bytes, async_record.op_desc_count);
  status = TransferStatus::COMPLETED;
  HIXL_LOGD("Fabric mem AICPU async transfer completed, channel:%s, req:%lu, cost:%lu us, real copy:%lu us.",
            async_record.channel_id.c_str(), req_id, transfer_cost, real_copy_cost);
  return SUCCESS;
}

void FabricMemAicpuTransferService::CleanupAsyncTransfer(const TransferReq &req) {
  const uint64_t req_id = reinterpret_cast<uintptr_t>(req);
  std::shared_ptr<FabricMemChannel> channel;
  if (channel_manager_.FindChannelByReq(req_id, channel) != SUCCESS) {
    return;
  }
  std::lock_guard<std::mutex> transfer_lock(channel->transfer_mu);
  AsyncRecord async_record;
  bool found = false;
  {
    std::lock_guard<std::mutex> lock(channel->records_mutex);
    const auto it = channel->async_records.find(req_id);
    if (it != channel->async_records.end()) {
      async_record = std::move(it->second);
      (void)channel->async_records.erase(it);
      found = true;
    }
  }
  if (found) {
    // A submitted request cannot be cancelled in isolation once it shares the channel's control
    // stream, so this tears the channel down the same way a failure does.
    CleanupFailedTransferLocked(channel, async_record.slot, async_record.aicpu_resource);
  }
  channel_manager_.RemoveReqRoute(req_id);
}

Status FabricMemAicpuTransferService::ProcessCopyWithAsync(AsyncSlot &slot, TransferOp operation,
                                                           const std::vector<TransferOpDesc> &op_descs,
                                                           FabricMemAicpuRequestResource &aicpu_resource,
                                                           uint32_t rtsq_timeout_ms) {
  HIXL_CHK_BOOL_RET_STATUS(!slot.streams.empty(), PARAM_INVALID, "Fabric mem copy streams cannot be empty.");
  HIXL_CHK_BOOL_RET_STATUS(aicpu_dispatcher_.IsInitialized(), FAILED, "FabricMem AICPU dispatcher is not initialized.");
  HIXL_CHK_BOOL_RET_STATUS(slot.has_aicpu_unfold, FAILED, "FabricMem AICPU RTSQ worker streams are not ready.");
  // UT and direct callers may skip AcquireBoundSlotLocked; ensure TransferContext exists before Submit.
  if (slot.transfer_ctx_key == 0U) {
    HIXL_CHK_STATUS_RET(aicpu_dispatcher_.AddTransferContext(slot),
                        "Register FabricMem AICPU transfer context failed.");
  }
  return aicpu_dispatcher_.Submit(slot, operation, op_descs, aicpu_resource, rtsq_timeout_ms);
}
}  // namespace hixl
