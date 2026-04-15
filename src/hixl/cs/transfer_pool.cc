/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "transfer_pool.h"

#include <memory>
#include <mutex>
#include <unordered_map>

#include "acl/acl.h"
#include "common/hixl_log.h"
#include "common/hixl_utils.h"
#include "common/llm_utils.h"
#include "common/scope_guard.h"
#include "proxy/hcomm_proxy.h"

namespace {
constexpr uint64_t kFlagInitValue = 0ULL;
constexpr CommEngine kDefaultEngine = CommEngine::COMM_ENGINE_AICPU_TS;
constexpr uint32_t kDefaultThreadNum = 1U;
constexpr uint32_t kDefaultNotifyNumPerThread = 0U;
}  // namespace

namespace hixl {

TransferPool &TransferPool::GetInstance(int32_t device_id) {
  static std::mutex registry_mu;
  static std::unordered_map<int32_t, std::unique_ptr<TransferPool>> pools;
  std::lock_guard<std::mutex> reg_lock(registry_mu);
  auto it = pools.find(device_id);
  if (it == pools.end()) {
    auto inserted = pools.emplace(device_id, std::unique_ptr<TransferPool>(new TransferPool(device_id)));
    it = inserted.first;
  }
  return *it->second;
}

TransferPool::TransferPool(int32_t device_id)
    : device_id_(device_id), ref_cnt_(0U), inited_(false), pool_size_(0U), free_list_(), slots_() {}

TransferPool::~TransferPool() {
  std::lock_guard<std::mutex> lock(mu_);
  DeinitAllSlotsLocked();
}

void TransferPool::InitFreeListLocked() {
  free_list_.clear();
  for (uint32_t i = 0U; i < pool_size_; ++i) {
    free_list_.push_back(i);
  }
}

Status TransferPool::Initialize(uint32_t pool_size) {
  if ((pool_size == 0U) || (pool_size > kMaxPoolSize)) {
    HIXL_LOGE(PARAM_INVALID, "[TransferPool] Initialize invalid pool_size=%u (device_id=%d)", pool_size, device_id_);
    return PARAM_INVALID;
  }
  std::lock_guard<std::mutex> lock(mu_);
  if (inited_) {
    if (pool_size != pool_size_) {
      HIXL_LOGE(PARAM_INVALID,
                "[TransferPool] Initialize pool_size mismatch. inited=%u got=%u (device_id=%d)", pool_size_, pool_size,
                device_id_);
      return PARAM_INVALID;
    }
    ref_cnt_ += 1U;
    return SUCCESS;
  }
  pool_size_ = pool_size;
  slots_.clear();
  slots_.resize(pool_size_);
  for (uint32_t i = 0U; i < pool_size_; ++i) {
    Slot &s = slots_[i];
    s.in_use = false;
    s.ctx = nullptr;
    s.stream = nullptr;
    s.thread = 0U;
    s.notify = nullptr;
    s.host_flag = nullptr;
  }
  Status ret = InitAllSlotsLocked();
  if (ret != SUCCESS) {
    DeinitAllSlotsLocked();
    return ret;
  }
  ref_cnt_ = 1U;
  inited_ = true;
  return SUCCESS;
}

void TransferPool::Finalize() {
  std::lock_guard<std::mutex> lock(mu_);
  if (ref_cnt_ == 0U) {
    return;
  }
  ref_cnt_ -= 1U;
  if (ref_cnt_ != 0U) {
    return;
  }
  if (inited_) {
    for (uint32_t i = 0U; i < pool_size_; ++i) {
      if (slots_[i].in_use) {
        AbortSlotByIndexLocked(i);
      }
    }
  }
  DeinitAllSlotsLocked();
}

Status TransferPool::Acquire(SlotHandle *handle) {
  HIXL_CHECK_NOTNULL(handle);
  std::lock_guard<std::mutex> lock(mu_);
  if (!inited_) {
    HIXL_LOGE(FAILED, "[TransferPool] Acquire failed: not initialized (device_id=%d)", device_id_);
    return FAILED;
  }
  if (free_list_.empty()) {
    HIXL_LOGE(RESOURCE_EXHAUSTED,
              "[TransferPool] Acquire failed: no free slots (device_id=%d, pool_size=%u)", device_id_, pool_size_);
    return RESOURCE_EXHAUSTED;
  }
  const uint32_t idx = free_list_.front();
  free_list_.pop_front();
  Slot &slot = slots_[idx];
  slot.in_use = true;
  FillHandleFromSlot(device_id_, idx, slot, handle);
  HIXL_LOGI("[TransferPool] Acquire slot success. device_id=%d index=%u", device_id_, idx);
  return SUCCESS;
}

void TransferPool::Release(const SlotHandle &handle) {
  HIXL_LOGI("[TransferPool] Release start. device_id=%d slot=%u", handle.device_id, handle.slot_index);
  if (handle.device_id != device_id_) {
    HIXL_LOGW("[TransferPool] Release device_id mismatch. pool=%d handle=%d slot=%u", device_id_, handle.device_id,
              handle.slot_index);
    return;
  }
  std::lock_guard<std::mutex> lock(mu_);
  if (handle.slot_index >= pool_size_) {
    HIXL_LOGW("[TransferPool] Release invalid slot index %u (device_id=%d)", handle.slot_index, device_id_);
    return;
  }
  Slot &slot = slots_[handle.slot_index];
  if (!slot.in_use) {
    HIXL_LOGW("[TransferPool] Release slot %u not in use (device_id=%d)", handle.slot_index, device_id_);
    return;
  }
  if (slot.host_flag != nullptr) {
    *(static_cast<uint64_t *>(slot.host_flag)) = kFlagInitValue;
  }
  slot.in_use = false;
  free_list_.push_back(handle.slot_index);
  HIXL_LOGI("[TransferPool] Release end. device_id=%d slot=%u", device_id_, handle.slot_index);
}

void TransferPool::Abort(const SlotHandle &handle) {
  std::lock_guard<std::mutex> lock(mu_);
  if (!inited_) {
    return;
  }
  if (handle.device_id != device_id_) {
    HIXL_LOGW("[TransferPool] Abort device_id mismatch. pool=%d handle=%d", device_id_, handle.device_id);
    return;
  }
  if (handle.slot_index >= pool_size_) {
    HIXL_LOGW("[TransferPool] Abort invalid slot index %u", handle.slot_index);
    return;
  }
  AbortSlotByIndexLocked(handle.slot_index);
}

Status TransferPool::GetAllSlots(std::vector<SlotHandle> &out) const {
  std::lock_guard<std::mutex> lock(mu_);
  if (!inited_) {
    return FAILED;
  }
  out.clear();
  out.reserve(pool_size_);
  for (uint32_t i = 0U; i < pool_size_; ++i) {
    const Slot &slot = slots_[i];
    SlotHandle h{};
    FillHandleFromSlot(device_id_, i, slot, &h);
    out.push_back(h);
  }
  return SUCCESS;
}

void TransferPool::FillHandleFromSlot(int32_t device_id, uint32_t index, const Slot &slot, SlotHandle *handle) {
  handle->device_id = device_id;
  handle->slot_index = index;
  handle->ctx = slot.ctx;
  handle->stream = slot.stream;
  handle->thread = slot.thread;
  handle->notify = slot.notify;
  handle->host_flag = slot.host_flag;
}

Status TransferPool::InitAllSlotsLocked() {
  InitFreeListLocked();
  hixl::TemporaryRtContext ctx_guard(nullptr);
  for (uint32_t i = 0U; i < pool_size_; ++i) {
    Status ret = InitOneSlotLocked(slots_[i], i);
    if (ret != SUCCESS) {
      RollbackInitLocked(i);
      return ret;
    }
  }
  return SUCCESS;
}

void TransferPool::RollbackInitLocked(uint32_t failed_index) {
  for (uint32_t j = 0U; j < failed_index; ++j) {
    DestroySlotLocked(slots_[j]);
    slots_[j] = Slot{};
  }
  if (failed_index < pool_size_) {
    DestroySlotLocked(slots_[failed_index]);
    slots_[failed_index] = Slot{};
  }
  free_list_.clear();
}

Status TransferPool::InitOneSlotLocked(Slot &slot, uint32_t slot_index) {
  (void)slot_index;
  HIXL_CHK_STATUS_RET(EnsureContextLocked(slot), "[TransferPool] EnsureContextLocked failed");
  HIXL_CHK_STATUS_RET(EnsureDefaultStreamLocked(slot), "[TransferPool] EnsureDefaultStreamLocked failed");
  HIXL_CHK_STATUS_RET(EnsureThreadLocked(slot), "[TransferPool] EnsureThreadLocked failed");
  HIXL_CHK_STATUS_RET(EnsureNotifyLocked(slot), "[TransferPool] EnsureNotifyLocked failed");
  HIXL_CHK_STATUS_RET(EnsurePinnedHostFlagLocked(slot), "[TransferPool] EnsurePinnedHostFlagLocked failed");
  return SUCCESS;
}

void TransferPool::AbortSlotByIndexLocked(uint32_t slot_index) {
  if (slot_index >= pool_size_) {
    return;
  }
  Slot &slot = slots_[slot_index];
  if (!slot.in_use) {
    return;
  }

  {
    hixl::TemporaryRtContext guard(slot.ctx);
    if (slot.stream != nullptr) {
      HIXL_CHK_ACL(aclrtStreamAbort(slot.stream), "[TransferPool] aclrtStreamAbort failed");
    }

    if (slot.notify != nullptr) {
      aclrtNotify *notify_ptr = &slot.notify;
      const aclError reset_ret = aclrtNotifyBatchReset(notify_ptr, static_cast<size_t>(1));
      if (reset_ret != ACL_SUCCESS) {
        HIXL_CHK_ACL(aclrtDestroyNotify(slot.notify), "[TransferPool] aclrtDestroyNotify fallback after BatchReset");
        slot.notify = nullptr;
      }
    }

    if (slot.thread != 0U) {
      HIXL_CHK_ACL(HcommProxy::ThreadFree(&slot.thread, 1U), "HcommThreadFree failed");
      slot.thread = 0U;
    }
    if (slot.host_flag != nullptr) {
      HIXL_CHK_ACL(aclrtFreeHost(slot.host_flag));
      slot.host_flag = nullptr;
    }
    if (slot.ctx != nullptr) {
      HIXL_CHK_ACL(aclrtDestroyContext(slot.ctx), "[TransferPool] aclrtDestroyContext failed in Abort");
      slot.ctx = nullptr;
    }
    slot.stream = nullptr;
  }

  Status ret = InitOneSlotLocked(slot, slot_index);
  if (ret != SUCCESS) {
    HIXL_LOGE(ret, "[TransferPool] AbortSlotByIndexLocked re-init failed slot=%u device_id=%d", slot_index,
              device_id_);
    slot.in_use = false;
    free_list_.push_back(slot_index);
    return;
  }
  slot.in_use = false;
  free_list_.push_back(slot_index);
}

Status TransferPool::EnsureNotifyLocked(Slot &slot) {
  if (slot.notify != nullptr) {
    return SUCCESS;
  }
  ResetNotifyResourcesLocked(slot);
  uint32_t notify_id = 0U;
  HIXL_CHK_STATUS_RET(CreateNotifyLocked(slot, notify_id), "[TransferPool] CreateNotifyLocked failed");
  (void)notify_id;
  return SUCCESS;
}

void TransferPool::ResetNotifyResourcesLocked(Slot &slot) {
  if (slot.notify != nullptr) {
    HIXL_CHK_ACL(aclrtDestroyNotify(slot.notify));
    slot.notify = nullptr;
  }
}

Status TransferPool::CreateNotifyLocked(Slot &slot, uint32_t &notify_id) {
  notify_id = 0U;
  HIXL_CHK_ACL_RET(aclrtCreateNotify(&slot.notify, ACL_NOTIFY_DEVICE_USE_ONLY),
                   "[TransferPool] aclrtCreateNotify failed");
  HIXL_CHK_ACL_RET(aclrtGetNotifyId(slot.notify, &notify_id), "[TransferPool] aclrtGetNotifyId failed");
  HIXL_LOGD("[TransferPool] Created notify. notify_id=%u", notify_id);
  return SUCCESS;
}

void TransferPool::DeinitAllSlotsLocked() {
  for (uint32_t i = 0U; i < pool_size_; ++i) {
    DestroySlotLocked(slots_[i]);
    slots_[i] = Slot{};
    slots_[i].in_use = false;
  }
  slots_.clear();
  pool_size_ = 0U;
  free_list_.clear();
  inited_ = false;
}

Status TransferPool::EnsureContextLocked(Slot &slot) {
  if (slot.ctx != nullptr) {
    return SUCCESS;
  }
  aclrtContext ctx = nullptr;
  HIXL_CHK_ACL_RET(aclrtCreateContext(&ctx, device_id_));
  slot.ctx = ctx;
  return SUCCESS;
}

Status TransferPool::EnsureDefaultStreamLocked(Slot &slot) {
  if (slot.stream != nullptr) {
    return SUCCESS;
  }
  HIXL_CHK_BOOL_RET_STATUS(slot.ctx != nullptr, FAILED, "[TransferPool] EnsureDefaultStreamLocked: ctx is null");
  const hixl::TemporaryRtContext guard(slot.ctx);
  aclrtStream stream = nullptr;
  HIXL_CHK_ACL_RET(aclrtCtxGetCurrentDefaultStream(&stream), "[TransferPool] aclrtCtxGetCurrentDefaultStream failed");
  slot.stream = stream;
  return SUCCESS;
}

Status TransferPool::EnsureThreadLocked(Slot &slot) {
  if (slot.thread != 0U) {
    return SUCCESS;
  }
  uint32_t notify_num = kDefaultNotifyNumPerThread;
  HIXL_CHK_HCCL_RET(HcommProxy::ThreadAlloc(kDefaultEngine, kDefaultThreadNum, &notify_num, &slot.thread));
  return SUCCESS;
}

Status TransferPool::EnsurePinnedHostFlagLocked(Slot &slot) {
  if (slot.host_flag != nullptr) {
    return SUCCESS;
  }
  void *p = nullptr;
  HIXL_CHK_ACL_RET(aclrtMallocHost(&p, sizeof(uint64_t)));
  HIXL_CHECK_NOTNULL(p, "[TransferPool] aclrtMallocHost returned null");
  slot.host_flag = p;
  *(static_cast<uint64_t *>(slot.host_flag)) = kFlagInitValue;
  return SUCCESS;
}

void TransferPool::DestroySlotLocked(Slot &slot) {
  hixl::TemporaryRtContext with_context(slot.ctx);

  if (slot.notify != nullptr) {
    HIXL_CHK_ACL(aclrtDestroyNotify(slot.notify));
    slot.notify = nullptr;
  }
  if (slot.thread != 0U) {
    HIXL_CHK_ACL(HcommProxy::ThreadFree(&slot.thread, 1U), "HcommThreadFree failed");
    slot.thread = 0U;
  }
  if (slot.ctx != nullptr) {
    HIXL_CHK_ACL(aclrtDestroyContext(slot.ctx), "destroy context failed");
    slot.ctx = nullptr;
  }
  slot.stream = nullptr;
  if (slot.host_flag != nullptr) {
    HIXL_CHK_ACL(aclrtFreeHost(slot.host_flag));
    slot.host_flag = nullptr;
  }
}

}  // namespace hixl
