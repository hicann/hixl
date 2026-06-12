/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "fabric_mem/fabric_mem_slot_pool.h"

#include <chrono>

#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/hixl_utils.h"
#include "common/scope_guard.h"

namespace hixl {
namespace {
constexpr uint64_t kHostFlagInitValue = 0ULL;
constexpr size_t kHostFlagSize = sizeof(uint64_t);

uint64_t GetDurationUs(const std::chrono::steady_clock::time_point &start,
                       const std::chrono::steady_clock::time_point &end) {
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
}
}  // namespace

FabricMemSlotPool::~FabricMemSlotPool() {
  AbortAndDestroyAll();
}

Status FabricMemSlotPool::Initialize(int32_t device_id, size_t max_async_slot_num, size_t task_stream_num) {
  HIXL_CHK_BOOL_RET_STATUS(device_id >= 0, PARAM_INVALID, "device_id must be non-negative.");
  HIXL_CHK_BOOL_RET_STATUS(max_async_slot_num > 0, PARAM_INVALID, "max_async_slot_num must be greater than zero.");
  HIXL_CHK_BOOL_RET_STATUS(task_stream_num > 0, PARAM_INVALID, "task_stream_num must be greater than zero.");
  device_id_ = device_id;
  max_async_slot_num_ = max_async_slot_num;
  task_stream_num_ = task_stream_num;
  return SUCCESS;
}

Status FabricMemSlotPool::CreateSlotStream(aclrtStream &stream) {
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

void FabricMemSlotPool::DestroySlotStreams(std::vector<aclrtStream> &streams, bool abort_streams) {
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

void FabricMemSlotPool::DestroySlotHostFlags(std::vector<void *> &host_flags) {
  for (void *host_flag : host_flags) {
    if (host_flag != nullptr) {
      HIXL_CHK_ACL(aclrtFreeHost(host_flag), "Free fabric mem host flag failed.");
    }
  }
  host_flags.clear();
}

void FabricMemSlotPool::ResetSlotHostFlags(const std::vector<void *> &host_flags) {
  for (void *host_flag : host_flags) {
    if (host_flag != nullptr) {
      *static_cast<uint64_t *>(host_flag) = kHostFlagInitValue;
    }
  }
}

void FabricMemSlotPool::DestroyCreatedSlotEntry(AsyncSlot &entry) {
  if (entry.ctx == nullptr) {
    return;
  }
  TemporaryRtContext with_context(entry.ctx);
  DestroySlotStreams(entry.streams, false);
  DestroySlotHostFlags(entry.host_flags);
  HIXL_CHK_ACL(aclrtDestroyContext(entry.ctx), "Destroy fabric mem transfer context failed.");
  entry.ctx = nullptr;
}

Status FabricMemSlotPool::PopulateSlotStreams(AsyncSlot &entry) const {
  entry.streams.clear();
  entry.streams.reserve(task_stream_num_);
  for (size_t i = 0U; i < task_stream_num_; ++i) {
    aclrtStream stream = nullptr;
    HIXL_CHK_STATUS_RET(CreateSlotStream(stream), "Create fabric mem stream failed.");
    entry.streams.emplace_back(stream);
  }
  return SUCCESS;
}

Status FabricMemSlotPool::PopulateSlotHostFlags(AsyncSlot &entry) const {
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

Status FabricMemSlotPool::CreateSlotEntryLocked(AsyncSlot &entry) const {
  HIXL_CHK_ACL_RET(aclrtCreateContext(&entry.ctx, device_id_), "Create fabric mem transfer context failed.");
  HIXL_DISMISSABLE_GUARD(ctx_guard, ([&entry]() { DestroyCreatedSlotEntry(entry); }));
  TemporaryRtContext with_context(entry.ctx);
  HIXL_CHK_STATUS_RET(PopulateSlotStreams(entry), "Populate fabric mem slot streams failed.");
  HIXL_CHK_STATUS_RET(PopulateSlotHostFlags(entry), "Populate fabric mem slot host flags failed.");
  entry.available = true;
  HIXL_DISMISS_GUARD(ctx_guard);
  return SUCCESS;
}

void FabricMemSlotPool::DestroySlotEntryLocked(AsyncSlot &entry, bool abort_streams) const {
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

Status FabricMemSlotPool::TryAcquireSlotLocked(AsyncSlot &slot) {
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

Status FabricMemSlotPool::AcquireAsync(AsyncSlot &slot) {
  std::lock_guard<std::mutex> lock(pool_mutex_);
  slot.ctx = nullptr;
  slot.streams.clear();
  slot.host_flags.clear();
  HIXL_CHK_STATUS_RET(TryAcquireSlotLocked(slot), "Failed to acquire fabric mem async transfer slot.");
  return SUCCESS;
}

Status FabricMemSlotPool::AcquireWithTimeout(AsyncSlot &slot, uint64_t timeout_us) {
  const auto start = std::chrono::steady_clock::now();
  std::unique_lock<std::mutex> lock(pool_mutex_);
  while (true) {
    // When there is a free slot or room to grow, TryAcquireSlotLocked either succeeds (free path) or
    // fails only because slot creation (ACL stream/context) failed. Such a creation error is not pool
    // contention, so return it immediately instead of busy-retrying ACL calls until the timeout.
    if (!free_slot_indices_.empty() || slot_pool_.size() < max_async_slot_num_) {
      // TryAcquireSlotLocked leaves slot untouched on failure, so retries are safe.
      const Status status = TryAcquireSlotLocked(slot);
      if (status == SUCCESS) {
        return SUCCESS;
      }
      HIXL_LOGE(status, "Failed to create fabric mem transfer slot.");
      return status;
    }
    // Pool is full with nothing free: wait for a release (or timeout).
    const auto cost = GetDurationUs(start, std::chrono::steady_clock::now());
    if (cost >= timeout_us) {
      HIXL_LOGE(TIMEOUT, "Get fabric mem transfer slot timeout.");
      return TIMEOUT;
    }
    const auto remaining = std::chrono::microseconds(timeout_us - cost);
    pool_cv_.wait_for(lock, remaining,
                      [this]() { return !free_slot_indices_.empty() || slot_pool_.size() < max_async_slot_num_; });
  }
}

void FabricMemSlotPool::ClearReleasedSlot(AsyncSlot &slot) {
  // slot only holds views of the pooled entry's ctx/streams/host_flags, so just
  // drop the references here; the entry itself owns and reuses those resources.
  slot.ctx = nullptr;
  slot.streams.clear();
  slot.host_flags.clear();
}

void FabricMemSlotPool::RebuildFreeSlotIndicesLocked() {
  while (!free_slot_indices_.empty()) {
    free_slot_indices_.pop();
  }
  for (size_t i = 0U; i < slot_pool_.size(); ++i) {
    if (slot_pool_[i].available) {
      free_slot_indices_.push(i);
    }
  }
}

bool FabricMemSlotPool::ReleaseSlotEntryLocked(AsyncSlot &slot, bool destroy_slot) {
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

void FabricMemSlotPool::Release(AsyncSlot &slot, bool destroy_slot) {
  bool released = false;
  {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    if (slot.ctx == nullptr) {
      ClearReleasedSlot(slot);
      return;
    }
    released = ReleaseSlotEntryLocked(slot, destroy_slot);
    ClearReleasedSlot(slot);
  }
  if (released) {
    pool_cv_.notify_one();
  }
}

void FabricMemSlotPool::AbortSlotStreams(const AsyncSlot &slot) {
  if (slot.ctx == nullptr) {
    return;
  }
  TemporaryRtContext with_context(slot.ctx);
  for (const auto &stream : slot.streams) {
    if (stream != nullptr) {
      HIXL_CHK_ACL(aclrtStreamAbort(stream), "Abort fabric mem stream failed.");
    }
  }
}

void FabricMemSlotPool::AbortAndDestroyAll() {
  std::lock_guard<std::mutex> lock(pool_mutex_);
  for (auto &entry : slot_pool_) {
    DestroySlotEntryLocked(entry, true);
  }
  slot_pool_.clear();
  while (!free_slot_indices_.empty()) {
    free_slot_indices_.pop();
  }
  pool_cv_.notify_all();
}
}  // namespace hixl
