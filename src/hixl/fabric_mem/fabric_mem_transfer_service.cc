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

namespace hixl {
namespace {
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
}  // namespace

uint64_t FabricMemTransferService::GetTransferBytes(const std::vector<TransferOpDesc> &op_descs) {
  uint64_t total_bytes = 0UL;
  for (const auto &op_desc : op_descs) {
    total_bytes += static_cast<uint64_t>(op_desc.len);
  }
  return total_bytes;
}

uint64_t FabricMemTransferService::GetDurationUs(const std::chrono::steady_clock::time_point &start,
                                                 const std::chrono::steady_clock::time_point &end) {
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
}

FabricMemTransferService::~FabricMemTransferService() {
  // Qualify to avoid virtual dispatch: derived parts are already destroyed here.
  FabricMemTransferService::Finalize();
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

Status FabricMemTransferService::InitCommon(const FabricMemTransferServiceInitParam &param, bool enable_aicpu_unfold) {
  HIXL_CHK_BOOL_RET_STATUS(param.device_id >= 0, PARAM_INVALID, "device_id must be non-negative.");
  HIXL_CHK_BOOL_RET_STATUS(param.max_stream_num > 0, PARAM_INVALID, "max_stream_num must be greater than zero.");
  HIXL_CHK_BOOL_RET_STATUS(param.statistic != nullptr && param.local_memory != nullptr, PARAM_INVALID,
                           "Invalid fabric mem transfer service initialization parameters.");
  const size_t task_stream_num = param.task_stream_num;
  HIXL_CHK_BOOL_RET_STATUS(task_stream_num > 0, PARAM_INVALID, "task_stream_num must be greater than zero.");
  // An AICPU slot pairs every control stream with a device-only RTSQ worker stream, so it costs
  // twice the streams a host slot does and the pool must be sized accordingly.
  const size_t streams_per_slot = enable_aicpu_unfold ? (task_stream_num * 2U) : task_stream_num;
  HIXL_CHK_BOOL_RET_STATUS(param.max_stream_num >= streams_per_slot, PARAM_INVALID,
                           "max_stream_num must leave room for at least one slot's streams.");
  device_id_ = param.device_id;
  task_stream_num_ = task_stream_num;
  max_stream_num_ = param.max_stream_num;
  statistic_ = param.statistic;
  local_memory_ = param.local_memory;
  const size_t max_async_slot_num = param.max_stream_num / streams_per_slot;
  HIXL_CHK_STATUS_RET(slot_pool_.Initialize(param.device_id, max_async_slot_num, task_stream_num),
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
  manager_param.enable_aicpu_unfold = enable_aicpu_unfold;
  HIXL_CHK_STATUS_RET(channel_manager_.Initialize(manager_param), "Initialize fabric mem channel manager failed.");
  HIXL_LOGI(
      "FabricMemTransferService common initialized, device:%d, max_stream:%zu, task_stream:%zu, max_async_slot:%zu, "
      "enable_aicpu_unfold:%d.",
      device_id_, max_stream_num_, task_stream_num_, max_async_slot_num, static_cast<int32_t>(enable_aicpu_unfold));
  return SUCCESS;
}

void FabricMemTransferService::Finalize() {
  // The owning FabricMemEngine quiesces public traffic before Finalize(). Channel
  // cleanup aborts and destroys in-flight transfers, then closes fds / Finalizes
  // VMM so the slot pool can be torn down without dangling AsyncRecord references.
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

Status FabricMemTransferService::WaitControlStreamsWithTimeout(const AsyncSlot &slot,
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

void FabricMemTransferService::FillPollInfo(const AsyncRecord &record, AsyncTransferPollInfo *info) {
  if (info == nullptr) {
    return;
  }
  info->op_type = record.op_type;
  info->prof_start_time = record.prof_start_time;
  info->channel_id = record.channel_id;
}

Status FabricMemTransferService::ResolveTransferAddrs(std::vector<TransferOpDesc> &op_descs,
                                                      const FabricMemTransferContext &context) const {
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

void FabricMemTransferService::UpdateStats(const std::string &channel_id, const std::string &statistic_channel_id,
                                           const std::shared_ptr<FabricMemTransferStatisticInfo> &stat_info,
                                           uint64_t transfer_cost, uint64_t real_copy_cost, uint64_t transfer_bytes,
                                           uint64_t op_desc_count) const {
  if (statistic_ == nullptr) {
    return;
  }
  if (stat_info != nullptr) {
    statistic_->UpdateCostsDirect(*stat_info, transfer_cost, real_copy_cost, transfer_bytes, op_desc_count);
    return;
  }
  const auto &stat_channel = statistic_channel_id.empty() ? channel_id : statistic_channel_id;
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
