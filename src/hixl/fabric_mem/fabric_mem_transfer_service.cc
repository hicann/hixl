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

#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/hixl_utils.h"
#include "common/scope_guard.h"
#include "fabric_mem/fabric_mem_allocator.h"
#include "fabric_mem/virtual_memory_manager.h"

namespace hixl {
namespace {
constexpr uint64_t kMillisToMicros = 1000UL;
constexpr uint64_t kHostFlagInitValue = 0ULL;
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

Status FabricMemTransferService::Initialize(int32_t device_id, size_t max_stream_num, size_t task_stream_num,
                                            FabricMemStatistic *statistic) {
  HIXL_CHK_BOOL_RET_STATUS(device_id >= 0, PARAM_INVALID, "device_id must be non-negative.");
  HIXL_CHK_BOOL_RET_STATUS(max_stream_num > 0, PARAM_INVALID, "max_stream_num must be greater than zero.");
  HIXL_CHK_BOOL_RET_STATUS(task_stream_num > 0, PARAM_INVALID, "task_stream_num must be greater than zero.");
  HIXL_CHK_BOOL_RET_STATUS(max_stream_num >= task_stream_num, PARAM_INVALID,
                           "max_stream_num must be greater than or equal to task_stream_num.");
  device_id_ = device_id;
  task_stream_num_ = task_stream_num;
  max_async_slot_num_ = max_stream_num / task_stream_num;
  max_stream_num_ = max_stream_num;
  statistic_ = statistic;
  HIXL_CHK_STATUS_RET(InitDevConstOne(), "Initialize fabric mem dev_const_one failed.");
  HIXL_LOGI("FabricMemTransferService initialized, device:%d, max_stream:%zu, task_stream:%zu, max_async_slot:%zu.",
            device_id_, max_stream_num_, task_stream_num_, max_async_slot_num_);
  return SUCCESS;
}

void FabricMemTransferService::Finalize() {
  // Lifecycle quiescence is guaranteed by the owning FabricMemEngine: it drains
  // its own in-flight operations and stops the control server before invoking
  // Finalize(), so no service method can run concurrently here.
  std::vector<AsyncSlot> pending_slots;
  {
    std::lock_guard<std::mutex> lock(async_req_mutex_);
    pending_slots.reserve(req_2_async_record_.size());
    for (auto &req_record : req_2_async_record_) {
      pending_slots.emplace_back(std::move(req_record.second.slot));
    }
    req_2_async_record_.clear();
  }
  for (auto &slot : pending_slots) {
    ReleaseSlot(slot, true);
  }
  {
    std::lock_guard<std::mutex> lock(channel_2_req_mutex_);
    channel_2_req_.clear();
  }
  {
    std::lock_guard<std::mutex> lock(stream_pool_mutex_);
    for (auto &entry : slot_pool_) {
      DestroySlotEntryLocked(entry, false);
    }
    slot_pool_.clear();
    while (!free_slot_indices_.empty()) {
      free_slot_indices_.pop();
    }
  }
  FreeDevConstOne();
  FinalizeShareHandles();
}

void FabricMemTransferService::FinalizeShareHandles() {
  std::lock_guard<std::mutex> lock(share_handle_mutex_);
  for (auto &share_handle : share_handles_) {
    const auto &info = share_handle.second;
    if (info.imported_va != 0) {
      HIXL_CHK_ACL(aclrtUnmapMem(reinterpret_cast<void *>(info.imported_va)), "Unmap local host mapping failed.");
      (void)VirtualMemoryManager::GetInstance().ReleaseMemory(info.imported_va);
    }
    if (info.imported_handle != nullptr) {
      HIXL_CHK_ACL(aclrtFreePhysical(info.imported_handle), "Free imported local handle failed.");
    }
    if (info.is_retained) {
      HIXL_CHK_ACL(aclrtFreePhysical(share_handle.first), "Free retained handle failed.");
    }
  }
  share_handles_.clear();
}

Status FabricMemTransferService::ImportHostMemoryForRegister(const MemDesc &mem, aclrtMemFabricHandle &share_handle,
                                                             aclrtDrvMemHandle &imported_pa_handle,
                                                             uintptr_t &imported_va) {
  HIXL_CHK_STATUS_RET(VirtualMemoryManager::GetInstance().ReserveMemory(mem.len, imported_va),
                      "Reserve local host fabric mapping failed.");
  HIXL_CHK_ACL_RET(
      aclrtMemImportFromShareableHandleV2(&share_handle, ACL_MEM_SHARE_HANDLE_TYPE_FABRIC, 0U, &imported_pa_handle),
      "Import local host fabric share handle failed.");
  HIXL_CHK_ACL_RET(aclrtMapMem(reinterpret_cast<void *>(imported_va), mem.len, 0, imported_pa_handle, 0),
                   "Map local host fabric memory failed.");
  return SUCCESS;
}

Status FabricMemTransferService::RegisterMem(const MemDesc &mem, MemType type, MemHandle &mem_handle) {
  HIXL_CHK_BOOL_RET_STATUS(mem.addr != 0 && mem.len > 0, PARAM_INVALID, "Invalid fabric mem registration range.");
  aclrtDrvMemHandle pa_handle = nullptr;
  bool is_retained = false;
  if (FabricMemAllocator::GetPaHandleFromVa(mem.addr, pa_handle) != SUCCESS) {
    HIXL_CHK_ACL_RET(aclrtMemRetainAllocationHandle(reinterpret_cast<void *>(mem.addr), &pa_handle),
                     "Retain allocation handle failed.");
    is_retained = true;
  }
  aclrtDrvMemHandle imported_pa_handle = nullptr;
  uintptr_t imported_va = 0;
  HIXL_DISMISSABLE_GUARD(
      fail_guard, ([&pa_handle, is_retained, &imported_va, &imported_pa_handle]() {
        if (is_retained && pa_handle != nullptr) {
          HIXL_CHK_ACL(aclrtFreePhysical(pa_handle), "Free retained handle after register failure failed.");
        }
        if (imported_va != 0) {
          HIXL_CHK_ACL(aclrtUnmapMem(reinterpret_cast<void *>(imported_va)),
                       "Unmap local import after register failure failed.");
          (void)VirtualMemoryManager::GetInstance().ReleaseMemory(imported_va);
        }
        if (imported_pa_handle != nullptr) {
          HIXL_CHK_ACL(aclrtFreePhysical(imported_pa_handle), "Free imported handle after register failure failed.");
        }
      }));

  aclrtMemFabricHandle share_handle = {};
  HIXL_CHK_ACL_RET(aclrtMemExportToShareableHandleV2(pa_handle, ACL_RT_VMM_EXPORT_FLAG_DISABLE_PID_VALIDATION,
                                                     ACL_MEM_SHARE_HANDLE_TYPE_FABRIC, &share_handle),
                   "Export fabric share handle failed.");
  if (type == MEM_HOST) {
    HIXL_CHK_STATUS_RET(ImportHostMemoryForRegister(mem, share_handle, imported_pa_handle, imported_va),
                        "Import host memory for register failed.");
  }

  {
    std::lock_guard<std::mutex> lock(share_handle_mutex_);
    share_handles_[pa_handle] = {mem.addr, mem.len, share_handle, imported_pa_handle, imported_va, is_retained};
  }
  if (type == MEM_HOST) {
    has_host_memory_ = true;
  }
  mem_handle = pa_handle;
  HIXL_LOGI("Register fabric mem success, type:%s, addr:%lu, len:%zu, retained:%d, handle:%p.",
            MemTypeToString(type).c_str(), mem.addr, mem.len, static_cast<int32_t>(is_retained), mem_handle);
  HIXL_DISMISS_GUARD(fail_guard);
  return SUCCESS;
}

Status FabricMemTransferService::DeregisterMem(MemHandle mem_handle) {
  std::lock_guard<std::mutex> lock(share_handle_mutex_);
  const auto it = share_handles_.find(static_cast<aclrtDrvMemHandle>(mem_handle));
  if (it == share_handles_.end()) {
    HIXL_LOGW("Fabric mem handle:%p is not registered.", mem_handle);
    return SUCCESS;
  }
  const auto info = it->second;
  if (info.imported_va != 0) {
    HIXL_CHK_ACL(aclrtUnmapMem(reinterpret_cast<void *>(info.imported_va)), "Unmap local host mapping failed.");
    (void)VirtualMemoryManager::GetInstance().ReleaseMemory(info.imported_va);
  }
  if (info.imported_handle != nullptr) {
    HIXL_CHK_ACL(aclrtFreePhysical(info.imported_handle), "Free imported local handle failed.");
  }
  if (info.is_retained) {
    HIXL_CHK_ACL(aclrtFreePhysical(static_cast<aclrtDrvMemHandle>(mem_handle)), "Free retained handle failed.");
  }
  share_handles_.erase(it);
  HIXL_LOGI("Deregister fabric mem success, handle:%p.", mem_handle);
  return SUCCESS;
}

Status FabricMemTransferService::Transfer(const FabricMemTransferContext &context, TransferOp operation,
                                          const std::vector<TransferOpDesc> &op_descs, int32_t timeout_in_millis) {
  const auto start = std::chrono::steady_clock::now();
  const uint64_t timeout_us = static_cast<uint64_t>(timeout_in_millis) * kMillisToMicros;
  AsyncSlot slot;
  HIXL_CHK_STATUS_RET(TryAcquireSlotWithTimeout(slot, timeout_us), "Failed to acquire fabric mem transfer slot.");
  HIXL_DISMISSABLE_GUARD(fail_guard, ([this, &slot]() { ReleaseSlot(slot, true); }));
  TemporaryRtContext ctx_guard(slot.ctx);
  auto op_descs_copy = op_descs;
  auto real_copy_start = std::chrono::steady_clock::now();
  HIXL_CHK_STATUS_RET(DoTransfer(slot, context, operation, op_descs_copy, real_copy_start), "Fabric mem copy failed.");
  for (auto &stream : slot.streams) {
    const auto cost = GetDurationUs(start, std::chrono::steady_clock::now());
    HIXL_CHK_BOOL_RET_STATUS(cost < timeout_us, TIMEOUT, "Fabric mem transfer timeout.");
    const uint64_t stream_timeout_ms = (timeout_us - cost) / kMillisToMicros;
    HIXL_CHK_BOOL_RET_STATUS(stream_timeout_ms > 0, TIMEOUT, "Fabric mem transfer timeout.");
    HIXL_CHK_ACL_RET(aclrtSynchronizeStreamWithTimeout(stream, stream_timeout_ms),
                     "Synchronize fabric mem stream failed.");
  }
  const auto real_copy_cost = GetDurationUs(real_copy_start, std::chrono::steady_clock::now());
  HIXL_DISMISS_GUARD(fail_guard);
  ReleaseSlot(slot, false);
  const auto transfer_cost = GetDurationUs(start, std::chrono::steady_clock::now());
  UpdateStats(context, transfer_cost, real_copy_cost, GetTransferBytes(op_descs_copy),
              static_cast<uint64_t>(op_descs_copy.size()));
  HIXL_LOGI("Fabric mem transfer cost:%lu us, real copy:%lu us, channel:%s.", transfer_cost, real_copy_cost,
            context.channel_id.c_str());
  return SUCCESS;
}

Status FabricMemTransferService::TransferAsync(const FabricMemTransferContext &context, TransferOp operation,
                                               const std::vector<TransferOpDesc> &op_descs, TransferReq &req) {
  const auto start = std::chrono::steady_clock::now();
  AsyncSlot slot;
  HIXL_CHK_STATUS_RET(TryAcquireAsyncSlot(slot), "Failed to acquire fabric mem async slot.");
  HIXL_DISMISSABLE_GUARD(fail_guard, ([this, &slot]() { ReleaseSlot(slot, true); }));
  TemporaryRtContext ctx_guard(slot.ctx);

  auto op_descs_copy = op_descs;
  auto real_copy_start = std::chrono::steady_clock::now();
  HIXL_CHK_STATUS_RET(DoTransfer(slot, context, operation, op_descs_copy, real_copy_start),
                      "Fabric mem async copy failed.");
  HIXL_CHK_STATUS_RET(AppendHostFlagCopies(slot), "Failed to append fabric mem host flag copies.");
  RegisterAsyncTransferRecord(context, req, std::move(slot), start, real_copy_start, GetTransferBytes(op_descs_copy),
                              static_cast<uint64_t>(op_descs_copy.size()));
  HIXL_DISMISS_GUARD(fail_guard);
  HIXL_LOGI("Fabric mem async transfer submitted, channel:%s, req:%lu, cost:%lu us.", context.channel_id.c_str(),
            reinterpret_cast<uintptr_t>(req), GetDurationUs(start, std::chrono::steady_clock::now()));
  return SUCCESS;
}

bool FabricMemTransferService::TryFastPathComplete(const FabricMemTransferContext &context, uint64_t req_id,
                                                   AsyncRecord &async_record, TransferStatus &status) {
  {
    std::lock_guard<std::mutex> lock(async_req_mutex_);
    const auto it = req_2_async_record_.find(req_id);
    if (it == req_2_async_record_.end() || !AllHostFlagsDone(it->second.slot)) {
      return false;
    }
    async_record = std::move(it->second);
    req_2_async_record_.erase(it);
  }
  (void)CompleteAsyncTransferAndUpdateStats(context, req_id, async_record, status);
  return true;
}

std::shared_ptr<std::mutex> FabricMemTransferService::GetChannelMutex(const std::string &channel_id) {
  std::lock_guard<std::mutex> lock(channel_mutex_map_mutex_);
  auto &channel_mutex = channel_mutexes_[channel_id];
  if (channel_mutex == nullptr) {
    channel_mutex = std::make_shared<std::mutex>();
  }
  return channel_mutex;
}

FabricMemTransferService::AsyncStreamQueryResult FabricMemTransferService::ResolveAsyncRecord(
    uint64_t req_id, const AsyncStreamQueryResult &query_result, AsyncRecord &async_record) {
  std::lock_guard<std::mutex> lock(async_req_mutex_);
  const auto it = req_2_async_record_.find(req_id);
  if (it == req_2_async_record_.end()) {
    return AsyncStreamQueryResult::kWaiting;
  }
  if (query_result == AsyncStreamQueryResult::kFailed) {
    async_record = std::move(it->second);
    req_2_async_record_.erase(it);
    return AsyncStreamQueryResult::kFailed;
  }
  async_record = std::move(it->second);
  req_2_async_record_.erase(it);
  return AsyncStreamQueryResult::kComplete;
}

Status FabricMemTransferService::GetTransferStatus(const FabricMemTransferContext &context, const TransferReq &req,
                                                   TransferStatus &status) {
  const auto channel_mutex = GetChannelMutex(context.channel_id);
  std::lock_guard<std::mutex> channel_lock(*channel_mutex);
  const uint64_t req_id = reinterpret_cast<uintptr_t>(req);
  AsyncRecord async_record;

  // Phase 1: fast-path check under lock (volatile reads only).
  if (TryFastPathComplete(context, req_id, async_record, status)) {
    return SUCCESS;
  }

  // Phase 2: slow-path stream query WITHOUT holding async_req_mutex_, so concurrent
  // GetTransferStatus calls for other requests are not blocked.
  AsyncSlot slot_snapshot;
  {
    std::lock_guard<std::mutex> lock(async_req_mutex_);
    const auto it = req_2_async_record_.find(req_id);
    HIXL_CHK_BOOL_RET_STATUS(it != req_2_async_record_.end(), FAILED, "Fabric mem request:%lu not found.", req_id);
    slot_snapshot.ctx = it->second.slot.ctx;
    slot_snapshot.streams = it->second.slot.streams;
  }
  AsyncStreamQueryResult query_result;
  {
    TemporaryRtContext ctx_guard(slot_snapshot.ctx);
    query_result = QueryAsyncSlotStreams(slot_snapshot);
  }
  if (query_result == AsyncStreamQueryResult::kWaiting) {
    status = TransferStatus::WAITING;
    return SUCCESS;
  }

  // Phase 3: resolve the record for completion or failure handling.
  const auto resolve_result = ResolveAsyncRecord(req_id, query_result, async_record);
  if (resolve_result == AsyncStreamQueryResult::kFailed) {
    return HandleAsyncStreamQueryFailure(context, req_id, async_record, status);
  }
  if (resolve_result == AsyncStreamQueryResult::kComplete) {
    if (SynchronizeAsyncSlotStreams(async_record.slot) != SUCCESS) {
      return HandleAsyncStreamQueryFailure(context, req_id, async_record, status);
    }
    return CompleteAsyncTransferAndUpdateStats(context, req_id, async_record, status);
  }
  status = TransferStatus::WAITING;
  return SUCCESS;
}

Status FabricMemTransferService::HandleAsyncStreamQueryFailure(const FabricMemTransferContext &context, uint64_t req_id,
                                                               AsyncRecord &async_record, TransferStatus &status) {
  ReleaseSlot(async_record.slot, true);
  RemoveChannelReqRelation(context.channel_id, req_id);
  status = TransferStatus::FAILED;
  HIXL_LOGE(FAILED, "Fabric mem async transfer failed on stream query, req:%lu.", req_id);
  return SUCCESS;
}

Status FabricMemTransferService::AppendHostFlagCopies(const AsyncSlot &slot) const {
  HIXL_CHK_BOOL_RET_STATUS(slot.streams.size() == slot.host_flags.size(), FAILED,
                           "Fabric mem async slot stream/flag size mismatch.");
  HIXL_CHECK_NOTNULL(dev_const_one_, "Fabric mem dev_const_one is null.");
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
  HIXL_CHK_BOOL_RET_STATUS(slot.ctx != nullptr, FAILED, "Fabric mem async slot context is null.");
  TemporaryRtContext ctx_guard(slot.ctx);
  for (const auto &stream : slot.streams) {
    HIXL_CHK_ACL_RET(aclrtSynchronizeStream(stream), "Synchronize fabric mem async stream failed.");
  }
  return SUCCESS;
}

void FabricMemTransferService::RegisterAsyncTransferRecord(const FabricMemTransferContext &context, TransferReq &req,
                                                           AsyncSlot &&slot,
                                                           const std::chrono::steady_clock::time_point &transfer_start,
                                                           const std::chrono::steady_clock::time_point &real_copy_start,
                                                           uint64_t transfer_bytes, uint64_t op_desc_count) {
  const uint64_t req_id = reinterpret_cast<uintptr_t>(req);
  std::lock_guard<std::mutex> async_lock(async_req_mutex_);
  std::lock_guard<std::mutex> channel_lock(channel_2_req_mutex_);
  req_2_async_record_[req_id] =
      AsyncRecord{std::move(slot), transfer_start, real_copy_start, transfer_bytes, op_desc_count};
  channel_2_req_[context.channel_id].emplace(req_id);
}

Status FabricMemTransferService::CompleteAsyncTransferAndUpdateStats(const FabricMemTransferContext &context,
                                                                     uint64_t req_id, AsyncRecord &async_record,
                                                                     TransferStatus &status) {
  ReleaseSlot(async_record.slot, false);
  RemoveChannelReqRelation(context.channel_id, req_id);
  const auto end = std::chrono::steady_clock::now();
  const auto real_copy_cost = GetDurationUs(async_record.real_copy_start, end);
  const auto transfer_cost = GetDurationUs(async_record.transfer_start, end);
  UpdateStats(context, transfer_cost, real_copy_cost, async_record.transfer_bytes, async_record.op_desc_count);
  status = TransferStatus::COMPLETED;
  HIXL_LOGI("Fabric mem async transfer completed, channel:%s, req:%lu, cost:%lu us, real copy:%lu us.",
            context.channel_id.c_str(), req_id, transfer_cost, real_copy_cost);
  return SUCCESS;
}

void FabricMemTransferService::RemoveChannel(const std::string &channel_id) {
  const auto channel_mutex = GetChannelMutex(channel_id);
  std::lock_guard<std::mutex> channel_lock(*channel_mutex);
  std::vector<AsyncSlot> pending_slots;
  {
    // Lock async_req_mutex_ first to maintain consistent lock ordering
    // (async_req_mutex_ -> channel_2_req_mutex_).
    std::lock_guard<std::mutex> async_lock(async_req_mutex_);
    std::vector<uint64_t> req_ids;
    {
      std::lock_guard<std::mutex> lock(channel_2_req_mutex_);
      const auto it = channel_2_req_.find(channel_id);
      if (it == channel_2_req_.end()) {
        return;
      }
      req_ids.assign(it->second.begin(), it->second.end());
      channel_2_req_.erase(it);
    }
    pending_slots.reserve(req_ids.size());
    for (const auto &req_id : req_ids) {
      const auto record_it = req_2_async_record_.find(req_id);
      if (record_it == req_2_async_record_.end()) {
        continue;
      }
      pending_slots.emplace_back(std::move(record_it->second.slot));
      req_2_async_record_.erase(record_it);
    }
  }
  for (auto &slot : pending_slots) {
    ReleaseSlot(slot, true);
  }
  {
    std::lock_guard<std::mutex> lock(channel_mutex_map_mutex_);
    channel_mutexes_.erase(channel_id);
  }
}

void FabricMemTransferService::RemoveChannelReqRelation(const std::string &channel_id, uint64_t req_id) {
  std::lock_guard<std::mutex> lock(channel_2_req_mutex_);
  const auto channel_it = channel_2_req_.find(channel_id);
  if (channel_it == channel_2_req_.end()) {
    return;
  }
  channel_it->second.erase(req_id);
}

bool FabricMemTransferService::CancelAsyncTransfer(TransferReq req) {
  const uint64_t req_id = reinterpret_cast<uintptr_t>(req);
  AsyncSlot slot;
  bool found = false;
  {
    std::lock_guard<std::mutex> lock(async_req_mutex_);
    const auto it = req_2_async_record_.find(req_id);
    if (it != req_2_async_record_.end()) {
      slot = std::move(it->second.slot);
      req_2_async_record_.erase(it);
      found = true;
    }
  }
  if (found) {
    ReleaseSlot(slot, true);
  }
  // Also remove the channel->req relation so RemoveChannel doesn't see a
  // stale entry. This is best-effort: the channel may have already been
  // removed, which is harmless.
  if (found) {
    std::lock_guard<std::mutex> lock(channel_2_req_mutex_);
    for (auto &channel : channel_2_req_) {
      if (channel.second.erase(req_id) > 0U) {
        break;
      }
    }
  }
  return found;
}

Status FabricMemTransferService::TryAcquireAsyncSlot(AsyncSlot &slot) {
  std::lock_guard<std::mutex> lock(stream_pool_mutex_);
  slot.ctx = nullptr;
  slot.streams.clear();
  slot.host_flags.clear();
  HIXL_CHK_STATUS_RET(TryAcquireSlotLocked(slot), "Failed to acquire fabric mem async transfer slot.");
  return SUCCESS;
}

Status FabricMemTransferService::CreateSlotStream(aclrtStream &stream) {
  stream = nullptr;
  HIXL_CHK_ACL_RET(aclrtCreateStreamWithConfig(&stream, 0, ACL_STREAM_FAST_LAUNCH | ACL_STREAM_FAST_SYNC),
                   "Create fabric mem stream failed.");
  HIXL_DISMISSABLE_GUARD(stream_guard, ([&stream]() {
                           HIXL_CHK_ACL(aclrtDestroyStream(stream), "Destroy fabric mem stream failed.");
                           stream = nullptr;
                         }));
  HIXL_CHK_ACL_RET(aclrtSetStreamFailureMode(stream, ACL_STOP_ON_FAILURE),
                   "Set fabric mem stream failure mode failed.");
  HIXL_DISMISS_GUARD(stream_guard);
  return SUCCESS;
}

void FabricMemTransferService::DestroySlotStreams(std::vector<aclrtStream> &streams, bool abort_streams) {
  for (auto &stream : streams) {
    if (stream == nullptr) {
      continue;
    }
    if (abort_streams) {
      HIXL_CHK_ACL(aclrtStreamAbort(stream), "Abort fabric mem stream failed.");
    }
    HIXL_CHK_ACL(aclrtDestroyStream(stream), "Destroy fabric mem stream failed.");
    stream = nullptr;
  }
  streams.clear();
}

void FabricMemTransferService::DestroySlotHostFlags(std::vector<void *> &host_flags) {
  for (void *host_flag : host_flags) {
    if (host_flag != nullptr) {
      HIXL_CHK_ACL(aclrtFreeHost(host_flag), "Free fabric mem host flag failed.");
    }
  }
  host_flags.clear();
}

void FabricMemTransferService::ResetSlotHostFlags(const std::vector<void *> &host_flags) {
  for (void *host_flag : host_flags) {
    if (host_flag != nullptr) {
      *static_cast<uint64_t *>(host_flag) = kHostFlagInitValue;
    }
  }
}

void FabricMemTransferService::DestroyCreatedSlotEntry(AsyncSlot &entry) {
  if (entry.ctx == nullptr) {
    return;
  }
  TemporaryRtContext with_context(entry.ctx);
  DestroySlotStreams(entry.streams, false);
  DestroySlotHostFlags(entry.host_flags);
  HIXL_CHK_ACL(aclrtDestroyContext(entry.ctx), "Destroy fabric mem transfer context failed.");
  entry.ctx = nullptr;
}

Status FabricMemTransferService::PopulateSlotStreams(AsyncSlot &entry) {
  entry.streams.clear();
  entry.streams.reserve(task_stream_num_);
  for (size_t i = 0U; i < task_stream_num_; ++i) {
    aclrtStream stream = nullptr;
    HIXL_CHK_STATUS_RET(CreateSlotStream(stream), "Create fabric mem stream failed.");
    entry.streams.emplace_back(stream);
  }
  return SUCCESS;
}

Status FabricMemTransferService::PopulateSlotHostFlags(AsyncSlot &entry) {
  entry.host_flags.clear();
  entry.host_flags.reserve(task_stream_num_);
  for (size_t i = 0U; i < task_stream_num_; ++i) {
    void *host_flag = nullptr;
    HIXL_CHK_ACL_RET(aclrtMallocHost(&host_flag, kHostFlagSize), "Allocate fabric mem host flag failed.");
    *static_cast<uint64_t *>(host_flag) = kHostFlagInitValue;
    entry.host_flags.emplace_back(host_flag);
  }
  return SUCCESS;
}

Status FabricMemTransferService::CreateSlotEntryLocked(AsyncSlot &entry) {
  HIXL_CHK_ACL_RET(aclrtCreateContext(&entry.ctx, device_id_), "Create fabric mem transfer context failed.");
  HIXL_DISMISSABLE_GUARD(ctx_guard, ([&entry]() { DestroyCreatedSlotEntry(entry); }));
  TemporaryRtContext with_context(entry.ctx);
  HIXL_CHK_STATUS_RET(PopulateSlotStreams(entry), "Populate fabric mem slot streams failed.");
  HIXL_CHK_STATUS_RET(PopulateSlotHostFlags(entry), "Populate fabric mem slot host flags failed.");
  entry.available = true;
  HIXL_DISMISS_GUARD(ctx_guard);
  return SUCCESS;
}

void FabricMemTransferService::DestroySlotEntryLocked(AsyncSlot &entry, bool abort_streams) {
  if (entry.ctx == nullptr) {
    return;
  }
  aclrtContext ctx = entry.ctx;
  entry.ctx = nullptr;
  {
    TemporaryRtContext with_context(ctx);
    DestroySlotStreams(entry.streams, abort_streams);
    DestroySlotHostFlags(entry.host_flags);
  }
  HIXL_CHK_ACL(aclrtDestroyContext(ctx), "Destroy fabric mem transfer context failed.");
  entry.available = false;
}

Status FabricMemTransferService::TryAcquireSlotLocked(AsyncSlot &slot) {
  if (!free_slot_indices_.empty()) {
    const size_t idx = free_slot_indices_.front();
    free_slot_indices_.pop();
    auto &entry = slot_pool_[idx];
    entry.available = false;
    // Host flags are owned by the pooled entry and reused; clear stale completion
    // values before handing the slot out for the next transfer.
    ResetSlotHostFlags(entry.host_flags);
    slot.ctx = entry.ctx;
    slot.streams = entry.streams;
    slot.host_flags = entry.host_flags;
    return SUCCESS;
  }
  if (slot_pool_.size() >= max_async_slot_num_) {
    return FAILED;
  }
  AsyncSlot entry;
  const Status status = CreateSlotEntryLocked(entry);
  if (status != SUCCESS) {
    return status;
  }
  entry.available = false;
  slot.ctx = entry.ctx;
  slot.streams = entry.streams;
  slot.host_flags = entry.host_flags;
  slot_pool_.emplace_back(std::move(entry));
  return SUCCESS;
}

Status FabricMemTransferService::TryAcquireSlotWithTimeout(AsyncSlot &slot, uint64_t timeout_us) {
  const auto start = std::chrono::steady_clock::now();
  while (true) {
    {
      std::lock_guard<std::mutex> lock(stream_pool_mutex_);
      // TryAcquireSlotLocked leaves slot untouched on failure, so retries are safe.
      if (TryAcquireSlotLocked(slot) == SUCCESS) {
        return SUCCESS;
      }
    }
    const auto cost = GetDurationUs(start, std::chrono::steady_clock::now());
    if (cost >= timeout_us) {
      HIXL_LOGE(TIMEOUT, "Get fabric mem transfer slot timeout.");
      return TIMEOUT;
    }
    const auto remaining = std::chrono::microseconds(timeout_us - cost);
    std::unique_lock<std::mutex> lock(stream_pool_mutex_);
    slot_pool_cv_.wait_for(lock, remaining, [this]() {
      return !free_slot_indices_.empty() || slot_pool_.size() < max_async_slot_num_;
    });
  }
}

void FabricMemTransferService::ClearReleasedSlotLocked(AsyncSlot &slot) {
  // slot only holds views of the pooled entry's ctx/streams/host_flags, so just
  // drop the references here; the entry itself owns and reuses those resources.
  slot.ctx = nullptr;
  slot.streams.clear();
  slot.host_flags.clear();
}

void FabricMemTransferService::RebuildFreeSlotIndicesLocked() {
  while (!free_slot_indices_.empty()) {
    free_slot_indices_.pop();
  }
  for (size_t i = 0U; i < slot_pool_.size(); ++i) {
    if (slot_pool_[i].available) {
      free_slot_indices_.push(i);
    }
  }
}

bool FabricMemTransferService::ReleaseSlotEntryLocked(AsyncSlot &slot, bool destroy_slot) {
  for (size_t i = 0U; i < slot_pool_.size(); ++i) {
    if (slot_pool_[i].ctx != slot.ctx) {
      continue;
    }
    if (destroy_slot) {
      DestroySlotEntryLocked(slot_pool_[i], true);
      slot_pool_.erase(slot_pool_.begin() + static_cast<ptrdiff_t>(i));
      RebuildFreeSlotIndicesLocked();
      return true;
    }
    slot_pool_[i].available = true;
    free_slot_indices_.push(i);
    return true;
  }
  return false;
}

void FabricMemTransferService::ReleaseSlot(AsyncSlot &slot, bool destroy_slot) {
  bool released = false;
  {
    std::lock_guard<std::mutex> lock(stream_pool_mutex_);
    if (slot.ctx == nullptr) {
      ClearReleasedSlotLocked(slot);
      return;
    }
    released = ReleaseSlotEntryLocked(slot, destroy_slot);
    ClearReleasedSlotLocked(slot);
  }
  if (released) {
    slot_pool_cv_.notify_one();
  }
}

Status FabricMemTransferService::DoTransfer(const AsyncSlot &slot, const FabricMemTransferContext &context,
                                            TransferOp operation, std::vector<TransferOpDesc> &op_descs,
                                            std::chrono::steady_clock::time_point &start) {
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
    HIXL_CHK_STATUS_RET(TransLocalHostOpAddrs(op_descs), "Local host fabric mem address translation failed.");
  }
  start = std::chrono::steady_clock::now();
  HIXL_CHK_STATUS_RET(ProcessCopyWithAsync(slot, operation, op_descs), "Fabric mem async copy failed.");
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

bool FabricMemTransferService::FindLocalHostRegisteredAddrLocked(uintptr_t old_addr, size_t len,
                                                                 uintptr_t &new_addr) const {
  for (const auto &item : share_handles_) {
    const auto &info = item.second;
    if (info.imported_va == 0) {
      continue;
    }
    if (!IsRangeContained(old_addr, len, info.va_addr, info.len)) {
      continue;
    }
    new_addr = info.imported_va + (old_addr - info.va_addr);
    return true;
  }
  return false;
}

Status FabricMemTransferService::TransLocalHostOpAddrs(std::vector<TransferOpDesc> &op_descs) {
  std::lock_guard<std::mutex> lock(share_handle_mutex_);
  for (auto &op : op_descs) {
    HIXL_CHK_BOOL_RET_STATUS(FindLocalHostRegisteredAddrLocked(op.local_addr, op.len, op.local_addr), PARAM_INVALID,
                             "Local host fabric mem address:%lu, len:%zu is not registered.", op.local_addr, op.len);
  }
  return SUCCESS;
}

Status FabricMemTransferService::ProcessCopyWithAsync(const AsyncSlot &slot, TransferOp operation,
                                                      const std::vector<TransferOpDesc> &op_descs) {
  HIXL_CHK_BOOL_RET_STATUS(slot.ctx != nullptr, PARAM_INVALID, "Fabric mem transfer context cannot be null.");
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

Status FabricMemTransferService::NeedTransLocalAddr(const std::vector<TransferOpDesc> &op_descs,
                                                    bool &need_trans_local_addr) const {
  need_trans_local_addr = false;
  if (op_descs.empty() || !has_host_memory_) {
    return SUCCESS;
  }
  aclrtPtrAttributes attributes{};
  HIXL_CHK_ACL_RET(aclrtPointerGetAttributes(reinterpret_cast<void *>(op_descs[0].local_addr), &attributes),
                   "Get local pointer attributes failed.");
  need_trans_local_addr = (attributes.location.type == ACL_MEM_LOCATION_TYPE_HOST);
  return SUCCESS;
}

std::vector<ShareHandleInfo> FabricMemTransferService::GetShareHandles() {
  std::lock_guard<std::mutex> lock(share_handle_mutex_);
  std::vector<ShareHandleInfo> share_handles;
  share_handles.reserve(share_handles_.size());
  for (const auto &share_handle : share_handles_) {
    share_handles.emplace_back(share_handle.second);
  }
  return share_handles;
}
}  // namespace hixl
