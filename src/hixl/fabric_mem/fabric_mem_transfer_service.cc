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

#include <algorithm>
#include <thread>

#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/hixl_utils.h"
#include "common/scope_guard.h"
#include "fabric_mem/fabric_mem_allocator.h"
#include "fabric_mem/virtual_memory_manager.h"

namespace hixl {
namespace {
constexpr uint64_t kMillisToMicros = 1000UL;
constexpr uint32_t kStreamWaitIntervalUs = 1000U;

void *ValueToPtr(uintptr_t value) {
  return reinterpret_cast<void *>(value);
}

uintptr_t PtrToValue(const void *ptr) {
  return reinterpret_cast<uintptr_t>(ptr);
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

Status FabricMemTransferService::Initialize(size_t max_stream_num, size_t task_stream_num,
                                            FabricMemStatistic *statistic) {
  HIXL_CHK_BOOL_RET_STATUS(max_stream_num > 0, PARAM_INVALID, "max_stream_num must be greater than zero.");
  HIXL_CHK_BOOL_RET_STATUS(task_stream_num > 0, PARAM_INVALID, "task_stream_num must be greater than zero.");
  task_stream_num_ = task_stream_num;
  async_task_stream_num_ = task_stream_num_ + 1U;
  HIXL_CHK_ACL_RET(aclrtGetDevice(&device_id_), "Get current device failed.");
  max_stream_num_ = max_stream_num;
  statistic_ = statistic;
  HIXL_LOGI("FabricMemTransferService initialized, device:%d, max_stream:%zu, task_stream:%zu.", device_id_,
            max_stream_num_, task_stream_num_);
  return SUCCESS;
}

void FabricMemTransferService::Finalize() {
  {
    std::lock_guard<std::mutex> lock(async_req_mutex_);
    for (const auto &req_record : req_2_async_record_) {
      DestroyAsyncResources(req_record.second.async_resources);
    }
    req_2_async_record_.clear();
  }
  {
    std::lock_guard<std::mutex> lock(channel_2_req_mutex_);
    channel_2_req_.clear();
  }
  {
    std::lock_guard<std::mutex> lock(stream_pool_mutex_);
    for (const auto &stream_stat : stream_pool_) {
      if (stream_stat.first != nullptr) {
        HIXL_CHK_ACL(aclrtDestroyStream(stream_stat.first), "Destroy fabric mem stream failed.");
      }
    }
    stream_pool_.clear();
  }
  {
    std::lock_guard<std::mutex> lock(share_handle_mutex_);
    for (auto &share_handle : share_handles_) {
      const auto &info = share_handle.second;
      if (info.imported_va != 0) {
        HIXL_CHK_ACL(aclrtUnmapMem(ValueToPtr(info.imported_va)), "Unmap local host mapping failed.");
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
}

Status FabricMemTransferService::RegisterMem(const MemDesc &mem, MemType type, MemHandle &mem_handle) {
  HIXL_CHK_BOOL_RET_STATUS(mem.addr != 0 && mem.len > 0, PARAM_INVALID, "Invalid fabric mem registration range.");
  aclrtDrvMemHandle pa_handle = nullptr;
  bool is_retained = false;
  if (FabricMemAllocator::GetPaHandleFromVa(mem.addr, pa_handle) != SUCCESS) {
    HIXL_CHK_ACL_RET(aclrtMemRetainAllocationHandle(ValueToPtr(mem.addr), &pa_handle),
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
          HIXL_CHK_ACL(aclrtUnmapMem(ValueToPtr(imported_va)), "Unmap local import after register failure failed.");
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
    HIXL_CHK_STATUS_RET(VirtualMemoryManager::GetInstance().ReserveMemory(mem.len, imported_va),
                        "Reserve local host fabric mapping failed.");
    HIXL_CHK_ACL_RET(
        aclrtMemImportFromShareableHandleV2(&share_handle, ACL_MEM_SHARE_HANDLE_TYPE_FABRIC, 0U, &imported_pa_handle),
        "Import local host fabric share handle failed.");
    HIXL_CHK_ACL_RET(aclrtMapMem(ValueToPtr(imported_va), mem.len, 0, imported_pa_handle, 0),
                     "Map local host fabric memory failed.");
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
    HIXL_CHK_ACL(aclrtUnmapMem(ValueToPtr(info.imported_va)), "Unmap local host mapping failed.");
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
  std::vector<aclrtStream> streams;
  HIXL_CHK_STATUS_RET(TryGetStream(streams, timeout_us), "Failed to get fabric mem streams.");
  HIXL_DISMISSABLE_GUARD(fail_guard, ([this, &streams]() {
                           std::vector<AsyncResource> resources;
                           resources.reserve(streams.size());
                           for (const auto &stream : streams) {
                             resources.emplace_back(stream, nullptr);
                           }
                           DestroyAsyncResources(resources);
                         }));
  auto op_descs_copy = op_descs;
  auto real_copy_start = std::chrono::steady_clock::now();
  HIXL_CHK_STATUS_RET(DoTransfer(streams, context, operation, op_descs_copy, real_copy_start),
                      "Fabric mem copy failed.");
  for (auto &stream : streams) {
    const auto cost = GetDurationUs(start, std::chrono::steady_clock::now());
    HIXL_CHK_BOOL_RET_STATUS(cost < timeout_us, TIMEOUT, "Fabric mem transfer timeout.");
    const uint64_t stream_timeout_ms = (timeout_us - cost) / kMillisToMicros;
    HIXL_CHK_BOOL_RET_STATUS(stream_timeout_ms > 0, TIMEOUT, "Fabric mem transfer timeout.");
    HIXL_CHK_ACL_RET(aclrtSynchronizeStreamWithTimeout(stream, stream_timeout_ms),
                     "Synchronize fabric mem stream failed.");
  }
  const auto real_copy_cost = GetDurationUs(real_copy_start, std::chrono::steady_clock::now());
  HIXL_DISMISS_GUARD(fail_guard);
  ReleaseStreams(streams);
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
  std::vector<aclrtStream> streams;
  HIXL_CHK_STATUS_RET(TryGetStreamOnce(streams, async_task_stream_num_), "Failed to get fabric mem async streams.");
  std::vector<AsyncResource> async_resources;
  HIXL_DISMISSABLE_GUARD(fail_guard, ([this, &async_resources, &streams]() {
                           for (auto &async_resource : async_resources) {
                             if (async_resource.second != nullptr) {
                               HIXL_CHK_ACL(aclrtDestroyEvent(async_resource.second),
                                            "Destroy fabric mem event after failure failed.");
                               async_resource.second = nullptr;
                             }
                           }
                           std::vector<AsyncResource> resources;
                           resources.reserve(streams.size());
                           for (const auto &stream : streams) {
                             resources.emplace_back(stream, nullptr);
                           }
                           DestroyAsyncResources(resources);
                         }));

  std::vector<aclrtStream> copy_streams(task_stream_num_, nullptr);
  for (size_t i = 0U; i < task_stream_num_; ++i) {
    copy_streams[i] = streams[i + 1U];
  }
  auto op_descs_copy = op_descs;
  auto real_copy_start = std::chrono::steady_clock::now();
  HIXL_CHK_STATUS_RET(DoTransfer(copy_streams, context, operation, op_descs_copy, real_copy_start),
                      "Fabric mem async copy failed.");
  HIXL_CHK_STATUS_RET(RecordCopyStreamEvents(streams[0U], copy_streams, async_resources),
                      "Failed to record fabric mem copy stream events.");
  RegisterAsyncTransferRecord(context, req, std::move(async_resources), start, real_copy_start,
                              GetTransferBytes(op_descs_copy), static_cast<uint64_t>(op_descs_copy.size()));
  HIXL_DISMISS_GUARD(fail_guard);
  HIXL_LOGI("Fabric mem async transfer submitted, channel:%s, req:%lu, cost:%lu us.", context.channel_id.c_str(),
            PtrToValue(req), GetDurationUs(start, std::chrono::steady_clock::now()));
  return SUCCESS;
}

Status FabricMemTransferService::GetTransferStatus(const FabricMemTransferContext &context, const TransferReq &req,
                                                   TransferStatus &status) {
  const uint64_t req_id = PtrToValue(req);
  AsyncRecord async_record;
  {
    std::lock_guard<std::mutex> lock(async_req_mutex_);
    const auto it = req_2_async_record_.find(req_id);
    HIXL_CHK_BOOL_RET_STATUS(it != req_2_async_record_.end(), FAILED, "Fabric mem request:%lu not found.", req_id);
    status = TransferStatus::WAITING;
    bool completed = true;
    for (size_t i = 1U; i < it->second.async_resources.size(); ++i) {
      aclrtEventWaitStatus event_wait_status{};
      const auto ret = aclrtQueryEventWaitStatus(it->second.async_resources[i].second, &event_wait_status);
      if (ret != ACL_ERROR_NONE) {
        HIXL_LOGE(FAILED, "Query fabric mem async event failed, req:%lu, ret:%d.", req_id, ret);
        status = TransferStatus::FAILED;
        break;
      }
      completed = completed && (event_wait_status == ACL_EVENT_WAIT_STATUS_COMPLETE);
    }
    if (status == TransferStatus::FAILED || completed) {
      async_record = std::move(it->second);
      req_2_async_record_.erase(it);
    }
  }
  if (async_record.async_resources.empty()) {
    return (status == TransferStatus::FAILED) ? FAILED : SUCCESS;
  }
  if (status == TransferStatus::FAILED) {
    DestroyAsyncResources(async_record.async_resources);
    RemoveChannelReqRelation(context.channel_id, req_id);
    return FAILED;
  }
  HIXL_DISMISSABLE_GUARD(clean_guard, ([this, &context, &async_record, req_id]() {
                           DestroyAsyncResources(async_record.async_resources);
                           RemoveChannelReqRelation(context.channel_id, req_id);
                         }));
  HIXL_CHK_STATUS_RET(
      CompleteAsyncTransferAndUpdateStats(context, req_id, async_record.async_resources, async_record, status),
      "Complete fabric mem async transfer failed.");
  HIXL_DISMISS_GUARD(clean_guard);
  return SUCCESS;
}

Status FabricMemTransferService::RecordCopyStreamEvents(aclrtStream record_stream,
                                                        const std::vector<aclrtStream> &copy_streams,
                                                        std::vector<AsyncResource> &async_resources) const {
  async_resources.reserve(copy_streams.size() + 1U);
  async_resources.emplace_back(record_stream, nullptr);
  for (auto &stream : copy_streams) {
    aclrtEvent event = nullptr;
    HIXL_CHK_ACL_RET(aclrtCreateEvent(&event), "Create fabric mem copy event failed.");
    async_resources.emplace_back(stream, event);
    HIXL_CHK_ACL_RET(aclrtRecordEvent(event, record_stream), "Record fabric mem event failed.");
    HIXL_CHK_ACL_RET(aclrtStreamWaitEvent(stream, event), "Make fabric mem stream wait event failed.");
  }
  return SUCCESS;
}

void FabricMemTransferService::RegisterAsyncTransferRecord(const FabricMemTransferContext &context, TransferReq &req,
                                                           std::vector<AsyncResource> &&async_resources,
                                                           const std::chrono::steady_clock::time_point &transfer_start,
                                                           const std::chrono::steady_clock::time_point &real_copy_start,
                                                           uint64_t transfer_bytes, uint64_t op_desc_count) {
  const uint64_t req_id = PtrToValue(req);
  {
    std::lock_guard<std::mutex> lock(channel_2_req_mutex_);
    channel_2_req_[context.channel_id].emplace(req_id);
  }
  {
    std::lock_guard<std::mutex> lock(async_req_mutex_);
    req_2_async_record_[req_id] =
        AsyncRecord{std::move(async_resources), transfer_start, real_copy_start, transfer_bytes, op_desc_count};
  }
}

Status FabricMemTransferService::CompleteAsyncTransferAndUpdateStats(const FabricMemTransferContext &context,
                                                                     uint64_t req_id,
                                                                     const std::vector<AsyncResource> &async_resources,
                                                                     const AsyncRecord &async_record,
                                                                     TransferStatus &status) {
  SynchronizeStream(async_resources, req_id, status);
  HIXL_CHK_BOOL_RET_SPECIAL_STATUS(status == TransferStatus::FAILED, FAILED,
                                   "Synchronize fabric mem stream failed, req:%lu.", req_id);
  for (const auto &async_resource : async_resources) {
    if (async_resource.second != nullptr) {
      HIXL_CHK_ACL(aclrtDestroyEvent(async_resource.second), "Destroy fabric mem event failed.");
    }
  }
  std::vector<aclrtStream> streams;
  streams.reserve(async_resources.size());
  for (const auto &async_resource : async_resources) {
    streams.emplace_back(async_resource.first);
  }
  ReleaseStreams(streams);
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

void FabricMemTransferService::SynchronizeStream(const std::vector<AsyncResource> &async_resources, uint64_t req_id,
                                                 TransferStatus &status) {
  status = TransferStatus::COMPLETED;
  for (const auto &async_resource : async_resources) {
    if (async_resource.second == nullptr) {
      continue;
    }
    const auto ret = aclrtSynchronizeStream(async_resource.first);
    if (ret != ACL_ERROR_NONE) {
      HIXL_LOGE(FAILED, "Synchronize fabric mem stream failed, req:%lu, ret:%d.", req_id, ret);
      status = TransferStatus::FAILED;
    }
  }
}

void FabricMemTransferService::RemoveChannel(const std::string &channel_id) {
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
  for (const auto &req_id : req_ids) {
    std::lock_guard<std::mutex> async_lock(async_req_mutex_);
    const auto record_it = req_2_async_record_.find(req_id);
    if (record_it == req_2_async_record_.end()) {
      continue;
    }
    DestroyAsyncResources(record_it->second.async_resources);
    req_2_async_record_.erase(record_it);
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

void FabricMemTransferService::DestroyAsyncResources(const std::vector<AsyncResource> &async_resources) {
  std::lock_guard<std::mutex> lock(stream_pool_mutex_);
  for (const auto &async_resource : async_resources) {
    if (async_resource.second != nullptr) {
      HIXL_CHK_ACL(aclrtDestroyEvent(async_resource.second), "Destroy fabric mem event failed.");
    }
    auto stream = async_resource.first;
    if (stream == nullptr) {
      continue;
    }
    HIXL_CHK_ACL(aclrtDestroyStream(stream), "Destroy fabric mem stream failed.");
    stream_pool_.erase(stream);
  }
}

Status FabricMemTransferService::TryGetStream(std::vector<aclrtStream> &streams, uint64_t timeout_us) {
  const auto start = std::chrono::steady_clock::now();
  while (true) {
    if (TryGetStreamOnce(streams, task_stream_num_) == SUCCESS) {
      return SUCCESS;
    }
    const auto cost = GetDurationUs(start, std::chrono::steady_clock::now());
    HIXL_CHK_BOOL_RET_STATUS(cost < timeout_us, TIMEOUT, "Get fabric mem stream timeout.");
    std::this_thread::sleep_for(std::chrono::microseconds(kStreamWaitIntervalUs));
  }
}

Status FabricMemTransferService::ReuseStreamsLocked(std::vector<aclrtStream> &streams, size_t stream_num) {
  for (auto &stream_stat : stream_pool_) {
    if (!stream_stat.second) {
      continue;
    }
    stream_stat.second = false;
    streams.emplace_back(stream_stat.first);
    if (streams.size() >= stream_num) {
      return SUCCESS;
    }
  }
  return FAILED;
}

Status FabricMemTransferService::CreateStreamLocked(std::vector<aclrtStream> &streams,
                                                    std::vector<aclrtStream> &new_streams) {
  aclrtStream stream = nullptr;
  HIXL_CHK_ACL_RET(aclrtCreateStreamWithConfig(&stream, 0, ACL_STREAM_FAST_LAUNCH | ACL_STREAM_FAST_SYNC),
                   "Create fabric mem stream failed.");
  streams.emplace_back(stream);
  new_streams.emplace_back(stream);
  return SUCCESS;
}

void FabricMemTransferService::ReturnStreamsLocked(const std::vector<aclrtStream> &streams) {
  for (auto &stream : streams) {
    const auto it = stream_pool_.find(stream);
    if (it == stream_pool_.end()) {
      continue;
    }
    it->second = true;
  }
}

void FabricMemTransferService::DestroyStreams(const std::vector<aclrtStream> &streams) {
  for (auto &stream : streams) {
    HIXL_CHK_ACL(aclrtDestroyStream(stream), "Destroy newly created fabric mem stream failed.");
  }
}

Status FabricMemTransferService::RollbackStreamsLocked(std::vector<aclrtStream> &streams,
                                                       const std::vector<aclrtStream> &new_streams) {
  HIXL_EVENT("Fabric mem stream pool is full, current size:%zu.", stream_pool_.size());
  ReturnStreamsLocked(streams);
  DestroyStreams(new_streams);
  streams.clear();
  return FAILED;
}

Status FabricMemTransferService::TryGetStreamOnce(std::vector<aclrtStream> &streams, size_t stream_num) {
  std::lock_guard<std::mutex> lock(stream_pool_mutex_);
  streams.clear();
  std::vector<aclrtStream> new_streams;
  if (ReuseStreamsLocked(streams, stream_num) == SUCCESS) {
    return SUCCESS;
  }
  while (streams.size() < stream_num) {
    if (stream_pool_.size() + new_streams.size() >= max_stream_num_) {
      return RollbackStreamsLocked(streams, new_streams);
    }
    const Status status = CreateStreamLocked(streams, new_streams);
    if (status != SUCCESS) {
      ReturnStreamsLocked(streams);
      DestroyStreams(new_streams);
      streams.clear();
      return status;
    }
  }
  for (const auto &stream : streams) {
    stream_pool_[stream] = false;
  }
  return SUCCESS;
}

void FabricMemTransferService::ReleaseStreams(std::vector<aclrtStream> &streams) {
  std::lock_guard<std::mutex> lock(stream_pool_mutex_);
  for (auto &stream : streams) {
    const auto it = stream_pool_.find(stream);
    if (it != stream_pool_.end()) {
      it->second = true;
    }
  }
}

Status FabricMemTransferService::DoTransfer(const std::vector<aclrtStream> &streams,
                                            const FabricMemTransferContext &context, TransferOp operation,
                                            std::vector<TransferOpDesc> &op_descs,
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
  HIXL_CHK_STATUS_RET(ProcessCopyWithAsync(streams, operation, op_descs), "Fabric mem async copy failed.");
  return SUCCESS;
}

Status FabricMemTransferService::TransOpAddr(uintptr_t old_addr, size_t len,
                                             const std::unordered_map<uintptr_t, VaInfo> &new_va_to_old_va,
                                             uintptr_t &new_addr) {
  for (const auto &item : new_va_to_old_va) {
    const auto &info = item.second;
    const auto registered_old_va_end = info.va_addr + info.len;
    if (old_addr >= info.va_addr && old_addr + len <= registered_old_va_end) {
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
    const auto registered_end = info.va_addr + info.len;
    if (old_addr < info.va_addr || old_addr + len > registered_end) {
      continue;
    }
    new_addr = info.imported_va + (old_addr - info.va_addr);
    return true;
  }
  return false;
}

Status FabricMemTransferService::TransLocalHostOpAddr(uintptr_t old_addr, size_t len, uintptr_t &new_addr) {
  std::lock_guard<std::mutex> lock(share_handle_mutex_);
  HIXL_CHK_BOOL_RET_STATUS(FindLocalHostRegisteredAddrLocked(old_addr, len, new_addr), PARAM_INVALID,
                           "Local host fabric mem address:%lu, len:%zu is not registered.", old_addr, len);
  return SUCCESS;
}

Status FabricMemTransferService::TransLocalHostOpAddrs(std::vector<TransferOpDesc> &op_descs) {
  std::lock_guard<std::mutex> lock(share_handle_mutex_);
  for (auto &op : op_descs) {
    HIXL_CHK_BOOL_RET_STATUS(FindLocalHostRegisteredAddrLocked(op.local_addr, op.len, op.local_addr), PARAM_INVALID,
                             "Local host fabric mem address:%lu, len:%zu is not registered.", op.local_addr, op.len);
  }
  return SUCCESS;
}

Status FabricMemTransferService::ProcessCopyWithAsync(const std::vector<aclrtStream> &streams, TransferOp operation,
                                                      const std::vector<TransferOpDesc> &op_descs) {
  HIXL_CHK_BOOL_RET_STATUS(!streams.empty(), PARAM_INVALID, "Fabric mem copy streams cannot be empty.");
  const size_t stream_count = streams.size();
  size_t stream_idx = 0U;
  for (const auto &op : op_descs) {
    auto &stream = streams[stream_idx];
    if (++stream_idx >= stream_count) {
      stream_idx = 0U;
    }
    if (operation == TransferOp::WRITE) {
      HIXL_CHK_ACL_RET(aclrtMemcpyAsync(ValueToPtr(op.remote_addr), op.len, ValueToPtr(op.local_addr), op.len,
                                        ACL_MEMCPY_DEVICE_TO_DEVICE, stream),
                       "Fabric mem write copy failed.");
      continue;
    }
    HIXL_CHK_BOOL_RET_STATUS(operation == TransferOp::READ, PARAM_INVALID, "Invalid fabric mem transfer operation.");
    HIXL_CHK_ACL_RET(aclrtMemcpyAsync(ValueToPtr(op.local_addr), op.len, ValueToPtr(op.remote_addr), op.len,
                                      ACL_MEMCPY_DEVICE_TO_DEVICE, stream),
                     "Fabric mem read copy failed.");
  }
  return SUCCESS;
}

void FabricMemTransferService::UpdateStats(const FabricMemTransferContext &context, uint64_t transfer_cost,
                                           uint64_t real_copy_cost, uint64_t transfer_bytes, uint64_t op_desc_count) {
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
                                                    bool &need_trans_local_addr) {
  need_trans_local_addr = false;
  if (op_descs.empty() || !has_host_memory_) {
    return SUCCESS;
  }
  aclrtPtrAttributes attributes{};
  HIXL_CHK_ACL_RET(aclrtPointerGetAttributes(ValueToPtr(op_descs[0].local_addr), &attributes),
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
