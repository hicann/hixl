/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "complete_pool.h"
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <securec.h>
#include "acl/acl.h"
#include "common/hixl_log.h"
#include "common/hixl_utils.h"
#include "common/llm_utils.h"
#include "common/scope_guard.h"
#include "runtime/runtime/rts/rts_device.h"

namespace {
constexpr uint64_t kFlagInitValue = 0ULL;
constexpr uint64_t kFlagDoneValue = 1ULL;
constexpr rtDevResProcType_t kDefaultProcType = RT_PROCESS_CP1;
constexpr rtDevResType_t kDefaultResType = RT_RES_TYPE_STARS_NOTIFY_RECORD;
constexpr const char *kUbLocalNotifyTagPrefix = "_hixl_ub_local_dev_flag";
}  // namespace

namespace hixl {

CompletePool &GetCompletePool() {
  static CompletePool pool;
  return pool;
}

CompletePool::CompletePool()
    : ref_cnt_(0U),
      inited_(false),
      free_list_(),
      slots_(),
      init_device_id_(-1),
      init_engine_(CommEngine::COMM_ENGINE_RESERVED),
      init_thread_num_(0U),
      init_notify_num_per_thread_(0U) {
  for (uint32_t i = 0U; i < kMaxSlots; ++i) {
    Slot &slot = slots_[i];
    slot.in_use = false;
    slot.ctx = nullptr;
    slot.stream = nullptr;
    slot.thread = 0U;
    slot.notify = nullptr;
    slot.notify_addr = 0;
    slot.notify_len = 0U;
    slot.host_flag = nullptr;
    slot.notify_tag.fill('\0');
  }
}

CompletePool::~CompletePool() {
  std::lock_guard<std::mutex> lock(mu_);
  DeinitAllSlotsLocked();
}

bool CompletePool::IsInitedParamsSame(int32_t device_id, CommEngine engine, uint32_t thread_num,
                                      uint32_t notify_num_per_thread) const {
  return (device_id == init_device_id_) && (engine == init_engine_) && (thread_num == init_thread_num_) &&
         (notify_num_per_thread == init_notify_num_per_thread_);
}

void CompletePool::SaveInitParams(int32_t device_id, CommEngine engine, uint32_t thread_num,
                                  uint32_t notify_num_per_thread) {
  init_device_id_ = device_id;
  init_engine_ = engine;
  init_thread_num_ = thread_num;
  init_notify_num_per_thread_ = notify_num_per_thread;
}

void CompletePool::ResetInitParamsLocked() {
  init_device_id_ = -1;
  init_engine_ = CommEngine::COMM_ENGINE_RESERVED;
  init_thread_num_ = 0U;
  init_notify_num_per_thread_ = 0U;
}

void CompletePool::InitFreeListLocked() {
  free_list_.clear();
  for (uint32_t i = 0U; i < kMaxSlots; ++i) {
    free_list_.push_back(i);
  }
}

Status CompletePool::AddRefAndInitIfNeeded(int32_t device_id, CommEngine engine, uint32_t thread_num,
                                           uint32_t notify_num_per_thread) {
  std::lock_guard<std::mutex> lock(mu_);
  if (inited_) {
    if (!IsInitedParamsSame(device_id, engine, thread_num, notify_num_per_thread)) {
      HIXL_LOGE(PARAM_INVALID,
                "[CompletePool] AddRef with different params. "
                "inited(dev=%d,engine=%d,thread=%u,notify=%u) got(dev=%d,engine=%d,thread=%u,notify=%u)",
                init_device_id_, static_cast<int32_t>(init_engine_), init_thread_num_, init_notify_num_per_thread_,
                device_id, static_cast<int32_t>(engine), thread_num, notify_num_per_thread);
      return PARAM_INVALID;
    }
    ref_cnt_ += 1U;
    return SUCCESS;
  }
  SaveInitParams(device_id, engine, thread_num, notify_num_per_thread);
  ref_cnt_ += 1U;
  Status ret = InitAllSlotsLocked(device_id, engine, thread_num, notify_num_per_thread);
  if (ret != SUCCESS) {
    ref_cnt_ -= 1U;
    ResetInitParamsLocked();
    return ret;
  }
  inited_ = true;
  return SUCCESS;
}

void CompletePool::ReleaseRefAndDeinitIfNeeded() {
  std::lock_guard<std::mutex> lock(mu_);
  if (ref_cnt_ == 0U) {
    return;
  }
  ref_cnt_ -= 1U;
  if (ref_cnt_ != 0U) {
    return;
  }
  DeinitAllSlotsLocked();
}

uint32_t CompletePool::GetInUseCount() const {
  std::lock_guard<std::mutex> lock(mu_);
  uint32_t count = 0U;
  for (uint32_t i = 0U; i < kMaxSlots; ++i) {
    if (slots_[i].in_use) {
      count += 1U;
    }
  }
  return count;
}

Status CompletePool::Acquire(SlotHandle *handle) {
  HIXL_CHECK_NOTNULL(handle);
  std::lock_guard<std::mutex> lock(mu_);
  if (!inited_) {
    return FAILED;
  }
  if (free_list_.empty()) {
    return RESOURCE_EXHAUSTED;
  }
  const uint32_t idx = free_list_.front();
  free_list_.pop_front();
  Slot &slot = slots_[idx];
  slot.in_use = true;
  handle->slot_index = idx;
  handle->ctx = slot.ctx;
  handle->stream = slot.stream;
  handle->thread = slot.thread;
  handle->notify = slot.notify;
  handle->host_flag = slot.host_flag;
  handle->notify_addr = slot.notify_addr;
  handle->notify_len = slot.notify_len;
  handle->notify_tag = slot.notify_tag;
  HIXL_LOGI("[CompletePool] Acquire slot success. index=%u, tag=%s, len=%u",
            idx, handle->notify_tag.data(), handle->notify_len);
  return SUCCESS;
}

void CompletePool::Release(uint32_t slot_index) {
  HIXL_LOGI("[CompletePool] Release start. slot=%u", slot_index);
  std::lock_guard<std::mutex> lock(mu_);
  if (slot_index >= kMaxSlots) {
    HIXL_LOGW("[CompletePool] Release invalid slot index %u", slot_index);
    return;
  }
  Slot &slot = slots_[slot_index];
  if (!slot.in_use) {
    HIXL_LOGW("[CompletePool] Release slot %u which is not in use", slot_index);
    return;
  }
  if (slot.host_flag != nullptr) {
    *(static_cast<uint64_t *>(slot.host_flag)) = kFlagInitValue;
  }
  slot.in_use = false;
  free_list_.push_back(slot_index);
  HIXL_LOGI("[CompletePool] Release end. slot=%u", slot_index);
}

bool CompletePool::IsComplete(const SlotHandle &handle) const {
  if (handle.host_flag == nullptr) {
    return false;
  }
  return (*(static_cast<const uint64_t *>(handle.host_flag)) == kFlagDoneValue);
}

void CompletePool::ResetHostFlag(const SlotHandle &handle) const {
  if (handle.host_flag == nullptr) {
    return;
  }
  *(static_cast<uint64_t *>(handle.host_flag)) = kFlagInitValue;
}

Status CompletePool::GetSlotNotifyInfo(uint32_t slot_index, uint64_t &notify_addr, uint32_t &notify_len,
                                       std::array<char, kNotifyTagSize> &tag) const {
  std::lock_guard<std::mutex> lock(mu_);
  if (!inited_ || slot_index >= kMaxSlots) {
    return FAILED;
  }
  notify_addr = slots_[slot_index].notify_addr;
  notify_len = slots_[slot_index].notify_len;
  tag = slots_[slot_index].notify_tag;
  return SUCCESS;
}

Status CompletePool::InitAllSlotsLocked(int32_t device_id, CommEngine engine, uint32_t thread_num,
                                        uint32_t notify_num_per_thread) {
  InitFreeListLocked();
  llm::TemporaryRtContext ctx_guard(nullptr);
  for (uint32_t i = 0U; i < kMaxSlots; ++i) {
    Status ret = InitOneSlotLocked(slots_[i], i, device_id, engine, thread_num, notify_num_per_thread);
    if (ret != SUCCESS) {
      DestroySlotLocked(slots_[i]);
      return ret;
    }
  }
  return SUCCESS;
}

Status CompletePool::InitOneSlotLocked(Slot &slot, uint32_t slot_index, int32_t device_id, CommEngine engine,
                                       uint32_t thread_num, uint32_t notify_num_per_thread) {
  HIXL_CHK_STATUS_RET(EnsureContextLocked(slot, device_id), "[CompletePool] EnsureContextLocked failed");
  HIXL_CHK_STATUS_RET(EnsureStreamLocked(slot), "[CompletePool] EnsureStreamLocked failed");
  HIXL_CHK_STATUS_RET(EnsureThreadLocked(slot, engine, thread_num, notify_num_per_thread),
                      "[CompletePool] EnsureThreadLocked failed");
  HIXL_CHK_STATUS_RET(EnsureNotifyRecordLocked(slot, slot_index),
                      "[CompletePool] EnsureNotifyRecordLocked failed");
  HIXL_CHK_STATUS_RET(EnsurePinnedHostFlagLocked(slot), "[CompletePool] EnsurePinnedHostFlagLocked failed");
  return SUCCESS;
}

Status CompletePool::EnsureNotifyRecordLocked(Slot &slot, uint32_t slot_index) {
  if ((slot.notify != nullptr) && (slot.notify_addr != 0)) {
    return SUCCESS;
  }
  ResetNotifyResourcesLocked(slot);
  uint32_t notify_id = 0U;
  HIXL_CHK_STATUS_RET(CreateNotifyLocked(slot, notify_id), "[CompletePool] CreateNotifyLocked failed");
  uint32_t notify_len = 0U;
  HIXL_CHK_STATUS_RET(GetNotifyAddrLocked(notify_id, slot.notify_addr, notify_len),
                      "[CompletePool] GetNotifyAddrLocked failed");
  slot.notify_len = notify_len;
  std::array<char, kNotifyTagSize> tag{};
  HIXL_CHK_STATUS_RET(BuildNotifyTagLocked(slot_index, tag), "[CompletePool] BuildNotifyTagLocked failed");
  slot.notify_tag = tag;

  return SUCCESS;
}

void CompletePool::ResetNotifyResourcesLocked(Slot &slot) {
  if (slot.notify != nullptr) {
    HIXL_CHK_ACL(aclrtDestroyNotify(slot.notify));
    slot.notify = nullptr;
  }
  slot.notify_addr = 0;
  slot.notify_len = 0U;
  slot.notify_tag.fill('\0');
}

Status CompletePool::CreateNotifyLocked(Slot &slot, uint32_t &notify_id) {
  notify_id = 0U;
  HIXL_CHK_ACL_RET(aclrtCreateNotify(&slot.notify, ACL_NOTIFY_DEVICE_USE_ONLY),
                   "[CompletePool] aclrtCreateNotify failed");
  HIXL_CHK_ACL_RET(aclrtGetNotifyId(slot.notify, &notify_id), "[CompletePool] aclrtGetNotifyId failed");
  HIXL_LOGD("[CompletePool] Created notify. notify_id=%u", notify_id);
  return SUCCESS;
}

Status CompletePool::GetNotifyAddrLocked(uint32_t notify_id, uint64_t &notify_addr, uint32_t &notify_len) const {
  rtDevResInfo res_info{};
  res_info.dieId = 0U;
  res_info.procType = kDefaultProcType;
  res_info.resType = kDefaultResType;
  res_info.resId = notify_id;
  res_info.flag = 0U;
  rtDevResAddrInfo addr_info{};
  addr_info.resAddress = &notify_addr;
  addr_info.len = &notify_len;
  HIXL_CHK_ACL_RET(rtGetDevResAddress(&res_info, &addr_info));
  HIXL_LOGI("rtGetDevResAddress end");
  HIXL_LOGI("[CompletePool] rtDevResInfo: dieId=%u, procType=%d, resType=%d, resId=%u, flag=%u", res_info.dieId,
            static_cast<int>(res_info.procType), static_cast<int>(res_info.resType), res_info.resId, res_info.flag);
  HIXL_CHECK_NOTNULL(addr_info.resAddress);
  HIXL_CHECK_NOTNULL(addr_info.len);
  HIXL_LOGI("[HixlClient] rtDevResAddrInfo: resAddress=%p[0]=%lu, len=%p, *len=%u", addr_info.resAddress,
            *static_cast<uint64_t *>(addr_info.resAddress), addr_info.len, *addr_info.len);
  return SUCCESS;
}

Status CompletePool::BuildNotifyTagLocked(uint32_t slot_index, std::array<char, kNotifyTagSize> &tag) const {
  tag.fill('\0');
  const int nret =
      snprintf_s(tag.data(), tag.size(), tag.size() - 1U, "%s_%03u", kUbLocalNotifyTagPrefix, slot_index);
  HIXL_CHK_BOOL_RET_STATUS(nret >= 0, FAILED, "[CompletePool] snprintf_s notify tag failed. slot=%u", slot_index);
  return SUCCESS;
}

void CompletePool::DeinitAllSlotsLocked() {
  for (uint32_t i = 0U; i < kMaxSlots; ++i) {
    DestroySlotLocked(slots_[i]);
    slots_[i].in_use = false;
  }
  free_list_.clear();
  inited_ = false;
  ResetInitParamsLocked();
}

Status CompletePool::EnsureContextLocked(Slot &slot, int32_t device_id) {
  if (slot.ctx != nullptr) {
    return SUCCESS;
  }
  aclrtContext ctx = nullptr;
  HIXL_CHK_ACL_RET(aclrtCreateContext(&ctx, device_id));
  slot.ctx = ctx;
  return SUCCESS;
}

Status CompletePool::EnsureStreamLocked(Slot &slot) {
  if (slot.stream != nullptr) {
    return SUCCESS;
  }
  aclrtStream stream = nullptr;
  HIXL_CHK_ACL_RET(aclrtCreateStream(&stream), "[CompletePool] aclrtCreateStream failed");

  HIXL_DISMISSABLE_GUARD(stream_guard, [stream]() {
    aclrtDestroyStream(stream);
  });
  aclrtStreamAttrValue attr_val = {0};
  attr_val.failureMode = 1; // 1: 遇错即停
  HIXL_CHK_ACL_RET(
      aclrtSetStreamAttribute(stream, ACL_STREAM_ATTR_FAILURE_MODE, &attr_val),
      "[CompletePool] Set stream failure mode failed");
  HIXL_DISMISS_GUARD(stream_guard);
  slot.stream = stream;
  return SUCCESS;
}

Status CompletePool::EnsureThreadLocked(Slot &slot, CommEngine engine, uint32_t thread_num,
                                        uint32_t notify_num_per_thread) {
  if (slot.thread != 0U) {
    return SUCCESS;
  }
  HIXL_CHK_HCCL_RET(HcommThreadAlloc(engine, thread_num, notify_num_per_thread, &slot.thread));
  return SUCCESS;
}

Status CompletePool::EnsurePinnedHostFlagLocked(Slot &slot) {
  if (slot.host_flag != nullptr) {
    return SUCCESS;
  }
  void *p = nullptr;
  HIXL_CHK_ACL_RET(aclrtMallocHost(&p, sizeof(uint64_t)));
  HIXL_CHK_BOOL_RET_STATUS(p != nullptr, FAILED, "[CompletePool] rtMallocHost returned null");

  slot.host_flag = p;
  *(static_cast<uint64_t *>(slot.host_flag)) = kFlagInitValue;
  return SUCCESS;
}

void CompletePool::DestroySlotLocked(Slot &slot) {
  llm::TemporaryRtContext with_context(slot.ctx);

  if (slot.notify != nullptr) {
    HIXL_CHK_ACL(aclrtDestroyNotify(slot.notify));
    slot.notify = nullptr;
  }
  if (slot.thread != 0U) {
    slot.thread = 0U;
  }
  if (slot.stream != nullptr) {
    HIXL_CHK_ACL(aclrtDestroyStream(slot.stream), "destroy stream failed");
    slot.stream = nullptr;
  }
  if (slot.ctx != nullptr) {
    HIXL_CHK_ACL(aclrtDestroyContext(slot.ctx), "destroy context failed");
    slot.ctx = nullptr;
  }
  if (slot.host_flag != nullptr) {
    HIXL_CHK_ACL(aclrtFreeHost(slot.host_flag));
    slot.host_flag = nullptr;
  }
  slot.notify_addr = 0;
  slot.notify_len = 0U;
  slot.notify_tag.fill('\0');
}

}  // namespace hixl