/**
 * Copyright (c) 2025-2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "adxl/transfer_slot_pool.h"
#include <mutex>
#include "acl/acl.h"
#include "adxl_checker.h"
#include "common/hixl_utils.h"

namespace adxl {
namespace {
constexpr uint64_t kDevConstOneValue = 1ULL;
constexpr uint64_t kHostFlagInitValue = 0ULL;
constexpr size_t kHostFlagSize = sizeof(uint64_t);
}  // namespace

TransferSlotPool::~TransferSlotPool() {
  Finalize();
}

Status TransferSlotPool::Initialize() {
  std::lock_guard<std::mutex> lock(mu_);
  if (inited_) {
    return SUCCESS;
  }
  ADXL_CHK_STATUS_RET(EnsureDevConstOneLocked(), "Failed to init dev_const_one.");
  slots_.clear();
  free_list_.clear();
  inited_ = true;
  return SUCCESS;
}

void TransferSlotPool::Finalize() {
  std::lock_guard<std::mutex> lock(mu_);
  for (auto &slot : slots_) {
    // Abort streams that may still be driving aicpu task expansion before destroying their contexts.
    if (slot.in_use && slot.stream != nullptr) {
      hixl::TemporaryRtContext guard(slot.ctx);
      const auto abort_ret = aclrtStreamAbort(slot.stream);
      if (abort_ret != ACL_ERROR_NONE) {
        LLMLOGE(FAILED, "Call aclrtStreamAbort ret:%d.", abort_ret);
      }
    }
    DestroySlotLocked(slot);
  }
  slots_.clear();
  free_list_.clear();
  if (dev_const_one_ != nullptr) {
    (void)aclrtFree(dev_const_one_);
    dev_const_one_ = nullptr;
  }
  inited_ = false;
}

Status TransferSlotPool::Acquire(SlotHandle *handle) {
  ADXL_CHECK_NOTNULL(handle);
  std::lock_guard<std::mutex> lock(mu_);
  ADXL_CHK_BOOL_RET_STATUS(inited_, FAILED, "Transfer slot pool is not initialized.");
  uint32_t index;
  if (!free_list_.empty()) {
    index = free_list_.front();
    free_list_.pop_front();
  } else {
    ADXL_CHK_BOOL_RET_STATUS(slots_.size() < max_slot_num_, RESOURCE_EXHAUSTED,
                             "Transfer slot pool capacity limit reached, current size:%zu.", slots_.size());
    slots_.emplace_back();
    index = static_cast<uint32_t>(slots_.size() - 1U);
  }
  Slot &slot = slots_[index];
  if ((slot.ctx == nullptr) || (slot.stream == nullptr)) {
    const auto ret = InitSlotLocked(slot);
    if (ret != SUCCESS) {
      // Return the (still uninitialized) slot index to the free list so a later Acquire can retry it.
      free_list_.push_back(index);
      LLMLOGE(ret, "Failed to init transfer slot, index:%u.", index);
      return ret;
    }
  }
  slot.in_use = true;
  FillHandleLocked(index, slot, handle);
  LLMLOGI("Acquire transfer slot success, index:%u.", index);
  return SUCCESS;
}

void TransferSlotPool::Release(const SlotHandle &handle) {
  std::lock_guard<std::mutex> lock(mu_);
  if (handle.slot_index >= slots_.size()) {
    LLMLOGW("Release invalid slot index:%u.", handle.slot_index);
    return;
  }
  Slot &slot = slots_[handle.slot_index];
  if (!slot.in_use) {
    LLMLOGW("Release slot:%u not in use.", handle.slot_index);
    return;
  }
  slot.in_use = false;
  free_list_.push_back(handle.slot_index);
  LLMLOGI("Release transfer slot:%u.", handle.slot_index);
}

void TransferSlotPool::Abort(const SlotHandle &handle) {
  std::lock_guard<std::mutex> lock(mu_);
  if (handle.slot_index >= slots_.size()) {
    LLMLOGW("Abort invalid slot index:%u.", handle.slot_index);
    return;
  }
  Slot &slot = slots_[handle.slot_index];
  {
    hixl::TemporaryRtContext guard(slot.ctx);
    if (slot.stream != nullptr) {
      const auto abort_ret = aclrtStreamAbort(slot.stream);
      if (abort_ret != ACL_ERROR_NONE) {
        LLMLOGE(FAILED, "Call aclrtStreamAbort ret:%d.", abort_ret);
      }
    }
  }
  // Destroy ctx+stream then recreate a clean slot, same as hixl_cs TransferPool::AbortSlotByIndexLocked.
  DestroySlotLocked(slot);
  const Status init_ret = InitSlotLocked(slot);
  if (init_ret != SUCCESS) {
    LLMLOGE(init_ret, "Failed to re-init transfer slot after abort, index:%u.", handle.slot_index);
    slot.in_use = false;
    // Return the (now clean) slot index to the free list so a later Acquire can retry it; otherwise this slot is
    // permanently orphaned even though it occupies a slots_ slot, leaking pool capacity.
    free_list_.push_back(handle.slot_index);
    return;
  }
  slot.in_use = false;
  free_list_.push_back(handle.slot_index);
  LLMLOGI("Abort transfer slot:%u.", handle.slot_index);
}

Status TransferSlotPool::AcquireHostFlag(const SlotHandle &handle, void *&host_flag) {
  host_flag = nullptr;
  std::lock_guard<std::mutex> lock(mu_);
  ADXL_CHK_BOOL_RET_STATUS(inited_, FAILED, "Transfer slot pool is not initialized.");
  ADXL_CHK_BOOL_RET_STATUS(handle.slot_index < slots_.size(), FAILED, "Invalid slot index:%u.", handle.slot_index);
  Slot &slot = slots_[handle.slot_index];
  if (!slot.host_flag_free_list.empty()) {
    host_flag = slot.host_flag_free_list.back();
    slot.host_flag_free_list.pop_back();
  } else {
    ADXL_CHK_ACL_RET(aclrtMallocHost(&host_flag, kHostFlagSize));
  }
  *(static_cast<uint64_t *>(host_flag)) = kHostFlagInitValue;
  return SUCCESS;
}

void TransferSlotPool::ReleaseHostFlag(const SlotHandle &handle, void *host_flag) {
  if (host_flag == nullptr) {
    return;
  }
  std::lock_guard<std::mutex> lock(mu_);
  if (!inited_ || handle.slot_index >= slots_.size()) {
    (void)aclrtFreeHost(host_flag);
    return;
  }
  *(static_cast<uint64_t *>(host_flag)) = kHostFlagInitValue;
  slots_[handle.slot_index].host_flag_free_list.emplace_back(host_flag);
}

void TransferSlotPool::DestroySlotHostFlagsLocked(Slot &slot) {
  for (void *host_flag : slot.host_flag_free_list) {
    if (host_flag != nullptr) {
      (void)aclrtFreeHost(host_flag);
    }
  }
  slot.host_flag_free_list.clear();
}

Status TransferSlotPool::InitSlotLocked(Slot &slot) const {
  // Save and restore the caller's context: aclrtCreateContext makes the new context current, and the slot's default
  // stream must be queried while that context is current.
  hixl::TemporaryRtContext guard(nullptr);
  // Stage ctx/stream into locals: if a later step fails, destroy the just-created context instead of leaking it into
  // the slot (a retry would otherwise overwrite slot.ctx and orphan the first context).
  aclrtContext ctx = nullptr;
  ADXL_CHK_ACL_RET(aclrtCreateContext(&ctx, device_id_));
  aclrtStream stream = nullptr;
  aclError acl_ret = aclrtCtxGetCurrentDefaultStream(&stream);
  if (acl_ret == ACL_ERROR_NONE) {
    acl_ret = aclrtSetStreamFailureMode(stream, ACL_STOP_ON_FAILURE);
  }
  if (acl_ret != ACL_ERROR_NONE) {
    (void)aclrtDestroyContext(ctx);
    REPORT_INNER_ERR_MSG("E19999", "Init transfer slot acl api failed, ret: 0x%X", static_cast<uint32_t>(acl_ret));
    const Status adxl_ret = AclError2AdxlStatus(acl_ret);
    LLMLOGE(adxl_ret, "Init transfer slot acl api failed, ret: 0x%X", static_cast<uint32_t>(acl_ret));
    return adxl_ret;
  }
  slot.ctx = ctx;
  slot.stream = stream;
  return SUCCESS;
}

void TransferSlotPool::DestroySlotLocked(Slot &slot) {
  DestroySlotHostFlagsLocked(slot);
  if (slot.ctx != nullptr) {
    const auto ret = aclrtDestroyContext(slot.ctx);
    if (ret != ACL_ERROR_NONE) {
      LLMLOGE(FAILED, "Call aclrtDestroyContext ret:%d.", ret);
    }
    slot.ctx = nullptr;
  }
  // The default stream is owned by the context and is released together with it.
  slot.stream = nullptr;
}

Status TransferSlotPool::EnsureDevConstOneLocked() {
  if (dev_const_one_ != nullptr) {
    return SUCCESS;
  }
  ADXL_CHK_ACL_RET(aclrtMalloc(&dev_const_one_, sizeof(uint64_t), ACL_MEM_MALLOC_NORMAL_ONLY));
  constexpr uint64_t host_one = kDevConstOneValue;
  ADXL_CHK_ACL_RET(
      aclrtMemcpy(dev_const_one_, sizeof(uint64_t), &host_one, sizeof(uint64_t), ACL_MEMCPY_HOST_TO_DEVICE));
  LLMLOGI("Transfer slot pool dev_const_one initialized at %p on device %d.", dev_const_one_, device_id_);
  return SUCCESS;
}

void TransferSlotPool::FillHandleLocked(uint32_t index, const Slot &slot, SlotHandle *handle) const {
  handle->device_id = device_id_;
  handle->slot_index = index;
  handle->ctx = slot.ctx;
  handle->stream = slot.stream;
  handle->dev_const_one = dev_const_one_;
}
}  // namespace adxl
