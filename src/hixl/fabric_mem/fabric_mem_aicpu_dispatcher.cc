/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "fabric_mem/fabric_mem_aicpu_dispatcher.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <thread>
#include <utility>

#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/hixl_utils.h"
#include "common/scope_guard.h"
#include "cs/load_kernel.h"
#include "runtime/rt_external_stream.h"

namespace hixl {
namespace {
constexpr const char *kHixlFabricMemBatchRead = "HixlFabricMemBatchRead";
constexpr const char *kHixlFabricMemBatchWrite = "HixlFabricMemBatchWrite";
constexpr const char *kHixlSyncTransferContext = "HixlSyncTransferContext";
constexpr uint32_t kFabricMemKernelBlockDim = 1U;
constexpr uint32_t kMaxDescriptorsPerKernelLaunch = 128U;
constexpr uint32_t kDefaultNotifyWaitTimeoutSec = 60U;
constexpr uint32_t kMillisPerSecond = 1000U;
constexpr uint64_t kMaxSdmaTransferBytes = std::numeric_limits<uint32_t>::max();
constexpr uint32_t kSyncContextKernelTimeoutMs = 10U * 1000U;
constexpr uint32_t kSyncContextRetryTimeoutMs = 30U * 1000U;
constexpr uint32_t kSyncContextRetryIntervalMs = 100U;

Status CountKernelLaunches(size_t desc_count, size_t &launch_count) {
  HIXL_CHK_BOOL_RET_STATUS(desc_count > 0U, PARAM_INVALID, "FabricMem AICPU launch count inputs are invalid.");
  launch_count = (desc_count + kMaxDescriptorsPerKernelLaunch - 1U) / kMaxDescriptorsPerKernelLaunch;
  return SUCCESS;
}

Status BuildDeviceDescriptors(TransferOp operation, const std::vector<TransferOpDesc> &op_descs,
                              std::vector<FabricMemAicpuTransferDesc> &device_descs,
                              FabricMemAicpuTransferDirection &direction) {
  HIXL_CHK_BOOL_RET_STATUS(!op_descs.empty(), PARAM_INVALID, "FabricMem AICPU transfer descriptors cannot be empty.");
  if (operation == TransferOp::READ) {
    direction = FabricMemAicpuTransferDirection::kRead;
  } else if (operation == TransferOp::WRITE) {
    direction = FabricMemAicpuTransferDirection::kWrite;
  } else {
    HIXL_LOGE(PARAM_INVALID, "Invalid FabricMem AICPU transfer operation.");
    return PARAM_INVALID;
  }
  device_descs.clear();
  device_descs.reserve(op_descs.size());
  for (const auto &op_desc : op_descs) {
    HIXL_CHK_BOOL_RET_STATUS(op_desc.local_addr != 0U && op_desc.remote_addr != 0U && op_desc.len > 0U, PARAM_INVALID,
                             "Invalid FabricMem AICPU transfer descriptor.");
    uint64_t local_addr = static_cast<uint64_t>(op_desc.local_addr);
    uint64_t remote_addr = static_cast<uint64_t>(op_desc.remote_addr);
    uint64_t remaining = static_cast<uint64_t>(op_desc.len);
    HIXL_CHK_BOOL_RET_STATUS(remaining <= std::numeric_limits<uint64_t>::max() - local_addr &&
                                 remaining <= std::numeric_limits<uint64_t>::max() - remote_addr,
                             PARAM_INVALID, "FabricMem AICPU transfer descriptor address range overflows.");
    // Split here rather than letting the kernel do it, so one device descriptor is exactly one SDMA
    // SQE. That is what lets the host count the RTSQ entries a launch will occupy and throttle with
    // NotifyRecord waits before the queue can fill up.
    while (remaining > 0U) {
      const uint64_t block_size = std::min(remaining, kMaxSdmaTransferBytes);
      FabricMemAicpuTransferDesc device_desc;
      if (direction == FabricMemAicpuTransferDirection::kRead) {
        device_desc.src_addr = remote_addr;
        device_desc.dst_addr = local_addr;
      } else {
        device_desc.src_addr = local_addr;
        device_desc.dst_addr = remote_addr;
      }
      device_desc.length = block_size;
      device_descs.emplace_back(device_desc);
      local_addr += block_size;
      remote_addr += block_size;
      remaining -= block_size;
    }
  }
  HIXL_CHK_BOOL_RET_STATUS(
      device_descs.size() <= std::numeric_limits<size_t>::max() / sizeof(FabricMemAicpuTransferDesc), PARAM_INVALID,
      "FabricMem AICPU descriptor buffer size overflows.");
  return SUCCESS;
}
}  // namespace

FabricMemAicpuDispatcher::~FabricMemAicpuDispatcher() {
  Finalize();
}

Status FabricMemAicpuDispatcher::InitializeRtsqDevice() {
  // The host passes the physical device id to the AICPU kernel, which converts it
  // to the driver-local id via drvGetLocalDevIDByHostDevID before calling halSqCqQuery.
  int32_t physical_device_id = -1;
  if (aclrtGetPhyDevIdByLogicDevId(device_id_, &physical_device_id) != ACL_SUCCESS || physical_device_id < 0) {
    HIXL_LOGW("FabricMem AICPU unfold cannot resolve physical device id for logical device:%d.", device_id_);
    return UNSUPPORTED;
  }
  rtsq_device_id_ = static_cast<uint32_t>(physical_device_id);
  return SUCCESS;
}

Status FabricMemAicpuDispatcher::Initialize(int32_t device_id) {
  HIXL_CHK_BOOL_RET_STATUS(device_id >= 0, PARAM_INVALID, "FabricMem AICPU device id must be non-negative.");
  std::lock_guard<std::mutex> lock(lifecycle_mutex_);
  if (initialized_.load(std::memory_order_acquire)) {
    if (device_id_ == device_id) {
      return SUCCESS;
    }
    HIXL_LOGE(PARAM_INVALID,
              "FabricMem AICPU dispatcher already initialized on device:%d, reject re-init on device:%d.", device_id_,
              device_id);
    return PARAM_INVALID;
  }
  device_id_ = device_id;
  if (InitializeRtsqDevice() != SUCCESS) {
    device_id_ = -1;
    return UNSUPPORTED;
  }

  std::vector<aclrtFuncHandle> handles;
  const Status load_status = LoadDeviceKernelFunctions(
      {kHixlFabricMemBatchRead, kHixlFabricMemBatchWrite, kHixlSyncTransferContext}, binary_handle_, handles);
  if (load_status != SUCCESS || handles.size() != 3U || handles[0U] == nullptr || handles[1U] == nullptr ||
      handles[2U] == nullptr) {
    HIXL_LOGW("FabricMem AICPU unfold is unavailable: kernel binary or A3 RTSQ runtime is not installed.");
    if (binary_handle_ != nullptr) {
      HIXL_CHK_ACL(aclrtBinaryUnLoad(binary_handle_), "Unload FabricMem AICPU kernel failed.");
      binary_handle_ = nullptr;
    }
    device_id_ = -1;
    rtsq_device_id_ = 0U;
    return UNSUPPORTED;
  }
  batch_read_ = handles[0U];
  batch_write_ = handles[1U];
  sync_transfer_context_ = handles[2U];
  initialized_.store(true, std::memory_order_release);
  HIXL_LOGI("FabricMem AICPU dispatcher initialized, device:%d.", device_id_);
  return SUCCESS;
}

void FabricMemAicpuDispatcher::Finalize() {
  std::lock_guard<std::mutex> lock(lifecycle_mutex_);
  initialized_.store(false, std::memory_order_release);
  if (binary_handle_ != nullptr) {
    HIXL_CHK_ACL(aclrtBinaryUnLoad(binary_handle_), "Unload FabricMem AICPU kernel failed.");
  }
  binary_handle_ = nullptr;
  batch_read_ = nullptr;
  batch_write_ = nullptr;
  sync_transfer_context_ = nullptr;
  device_id_ = -1;
  rtsq_device_id_ = 0U;
  std::lock_guard<std::mutex> task_id_lock(task_id_mutex_);
  rtsq_next_task_ids_.clear();
}

bool FabricMemAicpuDispatcher::IsInitialized() const {
  return initialized_.load(std::memory_order_acquire);
}

void FabricMemAicpuDispatcher::ReleaseRequestResource(FabricMemAicpuRequestResource &resource) {
  if (resource.descriptor_buffer != nullptr) {
    HIXL_CHK_ACL(aclrtFree(resource.descriptor_buffer), "Free FabricMem AICPU descriptor buffer failed.");
  }
  resource.descriptor_buffer = nullptr;
  resource.descriptor_buffer_size = 0U;
  if (resource.status_buffer != nullptr) {
    HIXL_CHK_ACL(aclrtFree(resource.status_buffer), "Free FabricMem AICPU status buffer failed.");
  }
  resource.status_buffer = nullptr;
  resource.status_count = 0U;
  if (resource.kernel_args_buffer != nullptr) {
    HIXL_CHK_ACL(aclrtFree(resource.kernel_args_buffer), "Free FabricMem AICPU kernel args buffer failed.");
  }
  resource.kernel_args_buffer = nullptr;
  resource.kernel_args_count = 0U;
}

Status FabricMemAicpuDispatcher::CheckRequestStatus(const FabricMemAicpuRequestResource &resource) {
  if (resource.status_buffer == nullptr || resource.status_count == 0U) {
    return SUCCESS;
  }
  std::vector<uint32_t> statuses(resource.status_count, 0U);
  const size_t bytes = resource.status_count * sizeof(uint32_t);
  HIXL_CHK_ACL_RET(aclrtMemcpy(statuses.data(), bytes, resource.status_buffer, bytes, ACL_MEMCPY_DEVICE_TO_HOST),
                   "Download FabricMem AICPU launch status failed.");
  for (size_t idx = 0U; idx < statuses.size(); ++idx) {
    HIXL_CHK_BOOL_RET_STATUS(statuses[idx] == 0U, FAILED, "FabricMem AICPU launch failed, status index:%zu value:%u.",
                             idx, statuses[idx]);
  }
  return SUCCESS;
}

Status FabricMemAicpuDispatcher::LaunchKernel(aclrtFuncHandle function, aclrtStream stream,
                                              const FabricMemAicpuKernelParam &param, void *dev_args) const {
  HIXL_CHK_BOOL_RET_STATUS(function != nullptr && stream != nullptr && dev_args != nullptr, FAILED,
                           "FabricMem AICPU kernel function, stream or device args is null.");
  FabricMemAicpuKernelParam kernel_param = param;
  HIXL_CHK_ACL_RET(
      aclrtMemcpy(dev_args, sizeof(kernel_param), &kernel_param, sizeof(kernel_param), ACL_MEMCPY_HOST_TO_DEVICE),
      "Upload FabricMem AICPU kernel parameter failed.");
  HIXL_CHK_ACL_RET(
      aclrtLaunchKernelV2(function, kFabricMemKernelBlockDim, dev_args, sizeof(kernel_param), nullptr, stream),
      "Launch FabricMem AICPU kernel failed.");
  return SUCCESS;
}

Status FabricMemAicpuDispatcher::BuildRtsqKernelParam(aclrtStream worker_stream, uint32_t task_count,
                                                      FabricMemAicpuKernelParam &param) {
  HIXL_CHK_BOOL_RET_STATUS(worker_stream != nullptr && task_count > 0U, PARAM_INVALID,
                           "FabricMem AICPU RTSQ worker stream or task count is invalid.");
  int32_t stream_id = -1;
  HIXL_CHK_ACL_RET(aclrtStreamGetId(worker_stream, &stream_id), "Get FabricMem AICPU RTSQ stream id failed.");
  HIXL_CHK_BOOL_RET_STATUS(stream_id >= 0 && stream_id <= std::numeric_limits<uint16_t>::max(), UNSUPPORTED,
                           "FabricMem AICPU RTSQ stream id is outside the A3 SQE ABI range.");
  uint32_t sq_id = 0U;
  HIXL_CHK_BOOL_RET_STATUS(rtStreamGetSqid(reinterpret_cast<rtStream_t>(worker_stream), &sq_id) == RT_ERROR_NONE,
                           UNSUPPORTED, "Get FabricMem AICPU RTSQ queue id failed.");
  uint32_t cq_id = 0U;
  uint32_t logic_cq_id = 0U;
  HIXL_CHK_BOOL_RET_STATUS(
      rtStreamGetCqid(reinterpret_cast<rtStream_t>(worker_stream), &cq_id, &logic_cq_id) == RT_ERROR_NONE, UNSUPPORTED,
      "Get FabricMem AICPU RTSQ completion queue ids failed.");
  (void)cq_id;
  param.device_id = rtsq_device_id_;
  param.rtsq_id = sq_id;
  param.rtsq_stream_id = static_cast<uint32_t>(stream_id);
  param.rtsq_logic_cq_id = logic_cq_id;
  // SQE task_id is uint16 on A3; keep a host-side counter and truncate on use.
  param.rtsq_task_id = static_cast<uint16_t>(ReserveRtsqTaskIds(sq_id, task_count));
  return SUCCESS;
}

uint32_t FabricMemAicpuDispatcher::ReserveRtsqTaskIds(uint32_t sq_id, uint32_t task_count) {
  std::lock_guard<std::mutex> lock(task_id_mutex_);
  const uint32_t first_task_id = rtsq_next_task_ids_[sq_id];
  rtsq_next_task_ids_[sq_id] = first_task_id + task_count;
  return first_task_id;
}

Status FabricMemAicpuDispatcher::LaunchSyncContextKernel(const std::vector<HixlTransferContextSyncEntry> &entries,
                                                         std::vector<uint32_t> &states) const {
  HIXL_CHK_BOOL_RET_STATUS(sync_transfer_context_ != nullptr, FAILED,
                           "FabricMem AICPU sync transfer context func is null.");
  HIXL_CHK_BOOL_RET_STATUS(!entries.empty(), PARAM_INVALID, "FabricMem AICPU sync context entries is empty.");
  const size_t entry_bytes = entries.size() * sizeof(HixlTransferContextSyncEntry);
  void *dev_entries = nullptr;
  HIXL_CHK_ACL_RET(aclrtMalloc(&dev_entries, entry_bytes, ACL_MEM_MALLOC_NORMAL_ONLY),
                   "Allocate FabricMem AICPU sync context entries failed.");
  HIXL_DISMISSABLE_GUARD(free_dev_entries, ([dev_entries]() {
                           HIXL_CHK_ACL(aclrtFree(dev_entries), "Free FabricMem AICPU sync context entries failed.");
                         }));
  states.assign(entries.size(), TRANSFER_THREAD_STATE_DELETED);
  const size_t state_bytes = states.size() * sizeof(uint32_t);
  void *dev_states = nullptr;
  HIXL_CHK_ACL_RET(aclrtMalloc(&dev_states, state_bytes, ACL_MEM_MALLOC_NORMAL_ONLY),
                   "Allocate FabricMem AICPU sync context states failed.");
  HIXL_DISMISSABLE_GUARD(free_dev_states, ([dev_states]() {
                           HIXL_CHK_ACL(aclrtFree(dev_states), "Free FabricMem AICPU sync context states failed.");
                         }));
  HIXL_CHK_ACL_RET(aclrtMemcpy(dev_entries, entry_bytes, entries.data(), entry_bytes, ACL_MEMCPY_HOST_TO_DEVICE),
                   "Upload FabricMem AICPU sync context entries failed.");
  HixlTransferContextSyncParam param{};
  param.entry_list_addr = PtrToValue(dev_entries);
  param.state_list_addr = PtrToValue(dev_states);
  param.entry_num = static_cast<uint32_t>(entries.size());
  void *dev_args = nullptr;
  HIXL_CHK_ACL_RET(aclrtMalloc(&dev_args, sizeof(param), ACL_MEM_MALLOC_NORMAL_ONLY),
                   "Allocate HixlSyncTransferContext args failed.");
  HIXL_DISMISSABLE_GUARD(free_dev_args, ([dev_args]() {
                           HIXL_CHK_ACL(aclrtFree(dev_args), "Free HixlSyncTransferContext args failed.");
                         }));
  HIXL_CHK_ACL_RET(aclrtMemcpy(dev_args, sizeof(param), &param, sizeof(param), ACL_MEMCPY_HOST_TO_DEVICE),
                   "Upload HixlSyncTransferContext param failed.");
  aclrtStream stream = nullptr;
  HIXL_CHK_ACL_RET(aclrtCtxGetCurrentDefaultStream(&stream), "Get default stream for HixlSyncTransferContext failed.");
  HIXL_CHK_ACL_RET(
      aclrtLaunchKernelV2(sync_transfer_context_, kFabricMemKernelBlockDim, dev_args, sizeof(param), nullptr, stream),
      "Launch HixlSyncTransferContext failed.");
  HIXL_CHK_ACL_RET(aclrtSynchronizeStreamWithTimeout(stream, static_cast<int32_t>(kSyncContextKernelTimeoutMs)),
                   "Synchronize HixlSyncTransferContext failed.");
  HIXL_CHK_ACL_RET(aclrtMemcpy(states.data(), state_bytes, dev_states, state_bytes, ACL_MEMCPY_DEVICE_TO_HOST),
                   "Download FabricMem AICPU sync context states failed.");
  return SUCCESS;
}

Status FabricMemAicpuDispatcher::CollectRetrySyncEntries(
    const std::vector<HixlTransferContextSyncEntry> &entries, const std::vector<uint32_t> &states, uint32_t op,
    uint32_t expect_state, std::vector<HixlTransferContextSyncEntry> &retry_entries) const {
  HIXL_CHK_BOOL_RET_STATUS(entries.size() == states.size(), FAILED,
                           "FabricMem AICPU sync context state size mismatch, entries=%zu states=%zu", entries.size(),
                           states.size());
  for (size_t i = 0U; i < entries.size(); ++i) {
    const uint32_t state = states[i];
    if (state == expect_state) {
      continue;
    }
    if (state != TRANSFER_THREAD_STATE_DELETING) {
      HIXL_LOGE(FAILED, "FabricMem AICPU unexpected transfer context state=%u key=%lu op=%u expect=%u", state,
                static_cast<uint64_t>(entries[i].thread), op, expect_state);
      return FAILED;
    }
    HIXL_LOGD("FabricMem AICPU delete transfer context busy (try_lock failed), will retry. key=%lu op=%u",
              static_cast<uint64_t>(entries[i].thread), op);
    HixlTransferContextSyncEntry entry{};
    entry.thread = entries[i].thread;
    entry.op = op;
    retry_entries.push_back(entry);
  }
  return SUCCESS;
}

Status FabricMemAicpuDispatcher::HandleSyncContextTimeout(const std::vector<HixlTransferContextSyncEntry> &pending,
                                                          const std::vector<uint32_t> &states, uint32_t op) const {
  if (op != TRANSFER_CONTEXT_OP_DELETE) {
    HIXL_LOGE(TIMEOUT, "FabricMem AICPU sync transfer context timeout, pending=%zu op=%u device_id=%d", pending.size(),
              op, device_id_);
    return TIMEOUT;
  }
  for (size_t i = 0U; i < pending.size(); ++i) {
    const uint32_t state = (i < states.size()) ? states[i] : TRANSFER_THREAD_STATE_DELETING;
    HIXL_EVENT(
        "FabricMem AICPU delete transfer context timeout after %u ms, force cleanup. key=%lu state=%u device_id=%d",
        kSyncContextRetryTimeoutMs, static_cast<uint64_t>(pending[i].thread), state, device_id_);
  }
  return SUCCESS;
}

Status FabricMemAicpuDispatcher::SyncTransferContext(ThreadHandle key, uint32_t op, uint32_t expect_state) const {
  if (key == 0U) {
    return SUCCESS;
  }
  HIXL_CHK_BOOL_RET_STATUS(sync_transfer_context_ != nullptr, FAILED,
                           "FabricMem AICPU sync transfer context func is null.");
  std::vector<HixlTransferContextSyncEntry> pending(1U);
  pending[0U].thread = key;
  pending[0U].op = op;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kSyncContextRetryTimeoutMs);
  std::vector<uint32_t> pending_states;
  while (!pending.empty()) {
    std::vector<uint32_t> states;
    HIXL_CHK_STATUS_RET(LaunchSyncContextKernel(pending, states), "FabricMem AICPU launch sync context failed.");
    std::vector<HixlTransferContextSyncEntry> retry_entries;
    HIXL_CHK_STATUS_RET(CollectRetrySyncEntries(pending, states, op, expect_state, retry_entries),
                        "FabricMem AICPU collect sync context retries failed.");
    pending.swap(retry_entries);
    pending_states.swap(states);
    if (pending.empty()) {
      return SUCCESS;
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      return HandleSyncContextTimeout(pending, pending_states, op);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(kSyncContextRetryIntervalMs));
  }
  return SUCCESS;
}

Status FabricMemAicpuDispatcher::AddTransferContext(AsyncSlot &slot) const {
  HIXL_CHK_BOOL_RET_STATUS(!slot.notifies.empty() && slot.notifies[0U] != nullptr, PARAM_INVALID,
                           "FabricMem AICPU slot notify is required for transfer context.");
  HIXL_CHK_BOOL_RET_STATUS(slot.ctx != nullptr, PARAM_INVALID, "FabricMem AICPU slot context is null.");
  slot.transfer_ctx_key = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(slot.notifies[0U]));
  HIXL_CHK_BOOL_RET_STATUS(IsInitialized(), FAILED, "FabricMem AICPU dispatcher is not initialized.");
  TemporaryRtContext ctx_guard(slot.ctx);
  return SyncTransferContext(static_cast<ThreadHandle>(slot.transfer_ctx_key), TRANSFER_CONTEXT_OP_ADD,
                             TRANSFER_THREAD_STATE_INITIALIZED);
}

Status FabricMemAicpuDispatcher::DeleteTransferContext(const AsyncSlot &slot) const {
  if (slot.transfer_ctx_key == 0U || slot.ctx == nullptr) {
    return SUCCESS;
  }
  // Runs on the aborting thread only, which may block here for up to the sync-context retry budget.
  // No dispatcher-wide lock is taken, so other channels keep submitting meanwhile.
  HIXL_CHK_BOOL_RET_STATUS(IsInitialized(), FAILED, "FabricMem AICPU dispatcher is not initialized.");
  TemporaryRtContext ctx_guard(slot.ctx);
  return SyncTransferContext(static_cast<ThreadHandle>(slot.transfer_ctx_key), TRANSFER_CONTEXT_OP_DELETE,
                             TRANSFER_THREAD_STATE_DELETED);
}

Status FabricMemAicpuDispatcher::LaunchOneDescriptorBatch(const AsyncSlot &slot, aclrtFuncHandle function,
                                                          FabricMemAicpuTransferDirection direction,
                                                          void *descriptor_buffer, void *status_buffer,
                                                          void *kernel_args_buffer, size_t begin, size_t count,
                                                          size_t total_descs, uint32_t rtsq_timeout_ms,
                                                          size_t &status_idx, size_t &tasks_since_notify) {
  // BuildDeviceDescriptors already split oversized transfers, so this batch is exactly `count` SDMA
  // SQEs plus an optional NotifyRecord.
  const size_t next_task_count = tasks_since_notify + count;
  const bool transfer_tail = begin + count == total_descs;
  const bool emit_notify = next_task_count >= kFabricMemMaxInFlightRtsqTasks || transfer_tail;
  FabricMemAicpuKernelParam param;
  param.desc_addr = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(descriptor_buffer) +
                                          begin * sizeof(FabricMemAicpuTransferDesc));
  param.desc_count = static_cast<uint32_t>(count);
  param.direction = static_cast<uint32_t>(direction);
  param.timeout_ms = rtsq_timeout_ms;
  param.notify_id = slot.notify_ids[0U];
  param.emit_notify_record = emit_notify ? 1U : 0U;
  param.transfer_ctx_key = slot.transfer_ctx_key;
  if (status_buffer != nullptr) {
    param.status_addr =
        static_cast<uint64_t>(reinterpret_cast<uintptr_t>(status_buffer) + status_idx * sizeof(uint32_t));
  }
  const uint32_t rtsq_task_count = static_cast<uint32_t>(count) + (emit_notify ? 1U : 0U);
  HIXL_CHK_STATUS_RET(BuildRtsqKernelParam(slot.sdma_streams[0U], rtsq_task_count, param),
                      "Build FabricMem AICPU RTSQ kernel parameter failed.");
  void *dev_args = static_cast<uint8_t *>(kernel_args_buffer) + status_idx * sizeof(FabricMemAicpuKernelParam);
  HIXL_CHK_STATUS_RET(LaunchKernel(function, slot.streams[0U], param, dev_args),
                      "Launch FabricMem AICPU descriptor batch failed.");
  if (emit_notify) {
    const uint32_t wait_timeout_sec = rtsq_timeout_ms == 0U
                                          ? kDefaultNotifyWaitTimeoutSec
                                          : std::max(1U, (rtsq_timeout_ms / kMillisPerSecond) +
                                                             ((rtsq_timeout_ms % kMillisPerSecond) == 0U ? 0U : 1U));
    HIXL_CHK_ACL_RET(aclrtWaitAndResetNotify(slot.notifies[0U], slot.streams[0U], wait_timeout_sec),
                     "Enqueue FabricMem AICPU notify wait failed.");
    tasks_since_notify = 0U;
  } else {
    tasks_since_notify = next_task_count;
  }
  ++status_idx;
  return SUCCESS;
}

Status FabricMemAicpuDispatcher::LaunchDescriptorBatches(const AsyncSlot &slot,
                                                         const std::vector<FabricMemAicpuTransferDesc> &descs,
                                                         FabricMemAicpuTransferDirection direction,
                                                         void *descriptor_buffer, void *status_buffer,
                                                         void *kernel_args_buffer, uint32_t rtsq_timeout_ms) {
  const aclrtFuncHandle function = direction == FabricMemAicpuTransferDirection::kRead ? batch_read_ : batch_write_;
  size_t status_idx = 0U;
  size_t begin = 0U;
  size_t tasks_since_notify = 0U;
  while (begin < descs.size()) {
    const size_t count = std::min(static_cast<size_t>(kMaxDescriptorsPerKernelLaunch), descs.size() - begin);
    HIXL_CHK_STATUS_RET(
        LaunchOneDescriptorBatch(slot, function, direction, descriptor_buffer, status_buffer, kernel_args_buffer, begin,
                                 count, descs.size(), rtsq_timeout_ms, status_idx, tasks_since_notify),
        "Launch FabricMem AICPU descriptor batch failed.");
    begin += count;
  }
  return SUCCESS;
}

Status FabricMemAicpuDispatcher::Submit(const AsyncSlot &slot, TransferOp operation,
                                        const std::vector<TransferOpDesc> &op_descs,
                                        FabricMemAicpuRequestResource &resource, uint32_t rtsq_timeout_ms) {
  HIXL_CHK_BOOL_RET_STATUS(resource.descriptor_buffer == nullptr && resource.status_buffer == nullptr, PARAM_INVALID,
                           "FabricMem AICPU descriptor resource is already in use.");
  // AICPU unfold is fixed to a single control/worker/notify triple.
  HIXL_CHK_BOOL_RET_STATUS(slot.streams.size() == 1U && slot.sdma_streams.size() == 1U && slot.notifies.size() == 1U &&
                               slot.notify_ids.size() == 1U,
                           PARAM_INVALID, "FabricMem AICPU unfold requires exactly one stream/notify pair.");
  HIXL_CHK_BOOL_RET_STATUS(slot.transfer_ctx_key != 0U, PARAM_INVALID,
                           "FabricMem AICPU transfer context key is not registered.");

  std::vector<FabricMemAicpuTransferDesc> device_descs;
  FabricMemAicpuTransferDirection direction = FabricMemAicpuTransferDirection::kRead;
  HIXL_CHK_STATUS_RET(BuildDeviceDescriptors(operation, op_descs, device_descs, direction),
                      "Build FabricMem AICPU device descriptors failed.");
  size_t launch_count = 0U;
  HIXL_CHK_STATUS_RET(CountKernelLaunches(device_descs.size(), launch_count),
                      "Count FabricMem AICPU kernel launches failed.");

  HIXL_CHK_BOOL_RET_STATUS(IsInitialized(), FAILED, "FabricMem AICPU dispatcher is not initialized.");
  const size_t descriptor_bytes = device_descs.size() * sizeof(FabricMemAicpuTransferDesc);
  const size_t status_bytes = launch_count * sizeof(uint32_t);
  HIXL_CHK_ACL_RET(aclrtMalloc(&resource.descriptor_buffer, descriptor_bytes, ACL_MEM_MALLOC_NORMAL_ONLY),
                   "Allocate FabricMem AICPU descriptor buffer failed.");
  resource.descriptor_buffer_size = descriptor_bytes;
  // Free only on pre-launch failures. Once any kernel may have been enqueued the buffer is owned by
  // the device: the caller keeps it and hands it to FabricMemSlotPool::AbortSlot, which frees it
  // after the kernel has exited.
  HIXL_DISMISSABLE_GUARD(buffer_guard, ([&resource]() { ReleaseRequestResource(resource); }));
  HIXL_CHK_ACL_RET(aclrtMalloc(&resource.status_buffer, status_bytes, ACL_MEM_MALLOC_NORMAL_ONLY),
                   "Allocate FabricMem AICPU status buffer failed.");
  resource.status_count = launch_count;
  const size_t kernel_args_bytes = launch_count * sizeof(FabricMemAicpuKernelParam);
  HIXL_CHK_ACL_RET(aclrtMalloc(&resource.kernel_args_buffer, kernel_args_bytes, ACL_MEM_MALLOC_NORMAL_ONLY),
                   "Allocate FabricMem AICPU kernel args buffer failed.");
  resource.kernel_args_count = launch_count;
  std::vector<uint32_t> zero_status(launch_count, 0U);
  HIXL_CHK_ACL_RET(
      aclrtMemcpy(resource.status_buffer, status_bytes, zero_status.data(), status_bytes, ACL_MEMCPY_HOST_TO_DEVICE),
      "Initialize FabricMem AICPU status buffer failed.");
  HIXL_CHK_ACL_RET(aclrtMemcpy(resource.descriptor_buffer, descriptor_bytes, device_descs.data(), descriptor_bytes,
                               ACL_MEMCPY_HOST_TO_DEVICE),
                   "Upload FabricMem AICPU descriptors failed.");
  const Status launch_status =
      LaunchDescriptorBatches(slot, device_descs, direction, resource.descriptor_buffer, resource.status_buffer,
                              resource.kernel_args_buffer, rtsq_timeout_ms);
  HIXL_DISMISS_GUARD(buffer_guard);
  return launch_status;
}

}  // namespace hixl
