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

#include <chrono>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "acl/acl.h"
#include "common/hixl_log.h"
#include "common/hixl_utils.h"
#include "common/llm_utils.h"
#include "common/scope_guard.h"
#include "load_kernel.h"
#include "proxy/hcomm_proxy.h"

namespace {
constexpr uint64_t kFlagInitValue = 0ULL;
constexpr CommEngine kDefaultEngine = CommEngine::COMM_ENGINE_AICPU_TS;
constexpr uint32_t kDefaultThreadNum = 1U;
constexpr uint32_t kDefaultNotifyNumPerThread = 0U;
constexpr uint32_t kSyncContextKernelTimeoutMs = 10U * 1000U;
constexpr uint32_t kSyncContextRetryTimeoutMs = 30U * 1000U;
constexpr uint32_t kSyncContextRetryIntervalMs = 100U;
constexpr const char *kDeviceFuncGet = "HixlBatchGet";
constexpr const char *kDeviceFuncPut = "HixlBatchPut";
constexpr const char *kDeviceFuncSyncContext = "HixlSyncTransferContext";
}  // namespace

namespace hixl {

TransferPool *TransferPool::GetInstance(int32_t device_id) {
  static std::mutex registry_mu;
  static std::unordered_map<int32_t, std::unique_ptr<TransferPool>> pools;
  std::lock_guard<std::mutex> reg_lock(registry_mu);
  auto it = pools.find(device_id);
  if (it != pools.end()) {
    return it->second.get();
  }
  auto pool_ptr = MakeUnique<TransferPool>(device_id);
  if (pool_ptr == nullptr) {
    HIXL_LOGE(FAILED, "[TransferPool] MakeUnique failed, device_id=%d", device_id);
    return nullptr;
  }
  const auto &inserted = pools.emplace(device_id, std::move(pool_ptr));
  return inserted.first->second.get();
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
      HIXL_LOGE(PARAM_INVALID, "[TransferPool] Initialize pool_size mismatch. inited=%u got=%u (device_id=%d)",
                pool_size_, pool_size, device_id_);
      return PARAM_INVALID;
    }
    ref_cnt_ += 1U;
    return SUCCESS;
  }
  pool_size_ = pool_size;
  slots_.clear();
  slots_.resize(pool_size_);
  HIXL_DISMISSABLE_GUARD(init_rollback, ([this]() { DeinitAllSlotsLocked(); }));
  HIXL_CHK_ACL_RET(aclrtCreateContext(&rts_context_, device_id_), "aclrtCreateContext rts_context_ failed");
  for (uint32_t i = 0U; i < pool_size_; ++i) {
    Slot &s = slots_[i];
    s.in_use = false;
    s.ctx = nullptr;
    s.stream = nullptr;
    s.thread = 0U;
    s.notify = nullptr;
  }
  Status ret = EnsureDeviceKernelsLocked();
  if (ret != SUCCESS) {
    return ret;
  }
  ret = InitAllSlotsLocked();
  if (ret != SUCCESS) {
    return ret;
  }
  ret = EnsureDevConstOneLocked();
  if (ret != SUCCESS) {
    return ret;
  }
  ret = AddTransferContextsLocked();
  if (ret != SUCCESS) {
    return ret;
  }
  ref_cnt_ = 1U;
  inited_ = true;
  HIXL_DISMISS_GUARD(init_rollback);
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
  HIXL_LOGI("[TransferPool] Finalize start. device_id=%d", device_id_);
  if (inited_) {
    AbortInUseStreamsLocked();
  }
  DeinitAllSlotsLocked();
  HIXL_LOGI("[TransferPool] Finalize end. device_id=%d", device_id_);
}

Status TransferPool::Acquire(SlotHandle *handle) {
  HIXL_CHECK_NOTNULL(handle);
  std::lock_guard<std::mutex> lock(mu_);
  if (!inited_) {
    HIXL_LOGE(FAILED, "[TransferPool] Acquire failed: not initialized (device_id=%d)", device_id_);
    return FAILED;
  }
  if (free_list_.empty()) {
    HIXL_LOGE(RESOURCE_EXHAUSTED, "[TransferPool] Acquire failed: no free slots (device_id=%d, pool_size=%u)",
              device_id_, pool_size_);
    return RESOURCE_EXHAUSTED;
  }
  const uint32_t idx = free_list_.front();
  free_list_.pop_front();
  Slot &slot = slots_[idx];
  slot.in_use = true;
  FillHandleFromSlot(device_id_, idx, slot, handle);
  handle->dev_const_one = dev_const_one_;
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
  handle->dev_const_one = nullptr;
  handle->notify_id = slot.notify_id;
}

Status TransferPool::InitAllSlotsLocked() {
  InitFreeListLocked();
  for (uint32_t i = 0U; i < pool_size_; ++i) {
    Status ret = InitOneSlotLocked(slots_[i], i);
    if (ret != SUCCESS) {
      return ret;
    }
  }
  return SUCCESS;
}

Status TransferPool::InitOneSlotLocked(Slot &slot, uint32_t slot_index) const {
  (void)slot_index;
  HIXL_CHK_STATUS_RET(EnsureContextLocked(slot), "[TransferPool] EnsureContextLocked failed");
  HIXL_CHK_STATUS_RET(EnsureDefaultStreamLocked(slot), "[TransferPool] EnsureDefaultStreamLocked failed");
  HIXL_CHK_STATUS_RET(EnsureThreadLocked(slot), "[TransferPool] EnsureThreadLocked failed");
  HIXL_CHK_STATUS_RET(EnsureNotifyLocked(slot), "[TransferPool] EnsureNotifyLocked failed");
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

  AbortSlotRuntimeLocked(slot);
  DeleteSlotThreadContextForAbortLocked(slot, slot_index);
  DestroySlotContextForAbortLocked(slot);
  Status ret = ReinitSlotAfterAbortLocked(slot, slot_index);
  if (ret == SUCCESS) {
    ReturnSlotToFreeListLocked(slot_index);
  }
}

void TransferPool::AbortInUseStreamsLocked() const {
  for (uint32_t i = 0U; i < pool_size_; ++i) {
    if (slots_[i].in_use) {
      AbortInUseStreamLocked(slots_[i]);
    }
  }
}

void TransferPool::AbortInUseStreamLocked(const Slot &slot) {
  hixl::TemporaryRtContext guard(slot.ctx);
  if (slot.stream != nullptr) {
    HIXL_CHK_ACL(aclrtStreamAbort(slot.stream), "[TransferPool] aclrtStreamAbort failed in Finalize");
  }
}

void TransferPool::AbortSlotRuntimeLocked(Slot &slot) const {
  hixl::TemporaryRtContext guard(slot.ctx);
  if (slot.stream != nullptr) {
    HIXL_CHK_ACL(aclrtStreamAbort(slot.stream), "[TransferPool] aclrtStreamAbort failed");
  }
  ResetAbortSlotNotifyLocked(slot);
}

void TransferPool::ResetAbortSlotNotifyLocked(Slot &slot) {
  if (slot.notify == nullptr) {
    return;
  }
  aclrtNotify *notify_ptr = &slot.notify;
  const aclError reset_ret = aclrtNotifyBatchReset(notify_ptr, static_cast<size_t>(1));
  if (reset_ret == ACL_SUCCESS) {
    return;
  }
  HIXL_CHK_ACL(aclrtDestroyNotify(slot.notify), "[TransferPool] aclrtDestroyNotify fallback after BatchReset");
  slot.notify = nullptr;
}

void TransferPool::DeleteSlotThreadContextForAbortLocked(Slot &slot, uint32_t slot_index) const {
  if (slot.thread != 0U) {
    Status sync_ret =
        SyncOneTransferContextLocked(slot.thread, TRANSFER_CONTEXT_OP_DELETE, TRANSFER_THREAD_STATE_DELETED);
    HIXL_CHK_STATUS(sync_ret,
                    "[TransferPool] delete transfer context failed in AbortSlotByIndexLocked, slot=%u device_id=%d",
                    slot_index, device_id_);
    const hixl::TemporaryRtContext rts_guard(rts_context_);
    HIXL_CHK_ACL(HcommProxy::ThreadFree(&slot.thread, 1U), "HcommThreadFree failed");
    slot.thread = 0U;
  }
}

void TransferPool::DestroySlotContextForAbortLocked(Slot &slot) {
  if (slot.ctx != nullptr) {
    HIXL_LOGI("[TransferPool] destroying context %p in AbortSlotByIndexLocked", slot.ctx);
    HIXL_CHK_ACL(aclrtDestroyContext(slot.ctx), "[TransferPool] aclrtDestroyContext failed in Abort");
    slot.ctx = nullptr;
    slot.stream = nullptr;
  }
}

Status TransferPool::ReinitSlotAfterAbortLocked(Slot &slot, uint32_t slot_index) {
  Status ret = InitOneSlotLocked(slot, slot_index);
  if (ret != SUCCESS) {
    HIXL_LOGE(ret, "[TransferPool] AbortSlotByIndexLocked re-init failed slot=%u device_id=%d", slot_index, device_id_);
    slot.in_use = false;
    free_list_.push_back(slot_index);
    return ret;
  }
  ret = SyncOneTransferContextLocked(slot.thread, TRANSFER_CONTEXT_OP_ADD, TRANSFER_THREAD_STATE_INITIALIZED);
  if (ret != SUCCESS) {
    HIXL_LOGE(ret, "[TransferPool] AbortSlotByIndexLocked add context failed slot=%u device_id=%d", slot_index,
              device_id_);
    HIXL_CHK_STATUS(DestroySlotLocked(slot), "[TransferPool] AbortSlotByIndexLocked destroy reinit slot failed");
    slot = Slot{};
    slot.in_use = false;
    free_list_.push_back(slot_index);
    return ret;
  }
  return SUCCESS;
}

void TransferPool::ReturnSlotToFreeListLocked(uint32_t slot_index) {
  Slot &slot = slots_[slot_index];
  slot.in_use = false;
  free_list_.push_back(slot_index);
}

Status TransferPool::EnsureNotifyLocked(Slot &slot) {
  if (slot.notify != nullptr) {
    return SUCCESS;
  }
  ResetNotifyResourcesLocked(slot);
  uint32_t notify_id = 0U;
  HIXL_CHK_STATUS_RET(CreateNotifyLocked(slot), "[TransferPool] CreateNotifyLocked failed");
  (void)notify_id;
  return SUCCESS;
}

void TransferPool::ResetNotifyResourcesLocked(Slot &slot) {
  if (slot.notify != nullptr) {
    HIXL_CHK_ACL(aclrtDestroyNotify(slot.notify));
    slot.notify = nullptr;
  }
}

Status TransferPool::CreateNotifyLocked(Slot &slot) {
  HIXL_CHK_ACL_RET(aclrtCreateNotify(&slot.notify, ACL_NOTIFY_DEVICE_USE_ONLY),
                   "[TransferPool] aclrtCreateNotify failed");
  HIXL_CHK_ACL_RET(aclrtGetNotifyId(slot.notify, &slot.notify_id), "[TransferPool] aclrtGetNotifyId failed");
  HIXL_LOGD("[TransferPool] Created notify. notify_id=%u", slot.notify_id);
  return SUCCESS;
}

void TransferPool::DeinitAllSlotsLocked() {
  const std::vector<ThreadHandle> threads = CollectLiveThreads(slots_);
  if (!threads.empty()) {
    HIXL_CHK_STATUS(DeleteTransferContextsLocked(threads),
                    "[TransferPool] delete transfer contexts failed in DeinitAllSlotsLocked, device_id=%d", device_id_);
  }
  if (dev_const_one_ != nullptr) {
    HIXL_CHK_ACL(aclrtFree(dev_const_one_));
    dev_const_one_ = nullptr;
    HIXL_LOGI("[TransferPool] released dev_const_one_ on device %d", device_id_);
  }
  for (uint32_t i = 0U; i < pool_size_; ++i) {
    HIXL_CHK_STATUS(DestroySlotLocked(slots_[i], false),
                    "[TransferPool] DeinitAllSlotsLocked destroy slot failed, idx=%u", i);
    slots_[i] = Slot{};
    slots_[i].in_use = false;
  }
  if (kernel_bin_handle_ != nullptr) {
    const hixl::TemporaryRtContext rts_guard(rts_context_);
    HIXL_CHK_ACL(aclrtBinaryUnLoad(kernel_bin_handle_));
    kernel_bin_handle_ = nullptr;
    device_func_handles_ = {};
  }
  if (rts_context_ != nullptr) {
    HIXL_LOGI("[TransferPool] destroying rts context %p", rts_context_);
    HIXL_CHK_ACL(aclrtDestroyContext(rts_context_));
    rts_context_ = nullptr;
  }
  slots_.clear();
  pool_size_ = 0U;
  free_list_.clear();
  inited_ = false;
}

Status TransferPool::EnsureContextLocked(Slot &slot) const {
  if (slot.ctx != nullptr) {
    return SUCCESS;
  }
  aclrtContext ctx = nullptr;
  HIXL_CHK_ACL_RET(aclrtCreateContext(&ctx, device_id_));
  slot.ctx = ctx;
  return SUCCESS;
}

Status TransferPool::EnsureDefaultStreamLocked(Slot &slot) const {
  if (slot.stream != nullptr) {
    return SUCCESS;
  }
  HIXL_CHK_BOOL_RET_STATUS(slot.ctx != nullptr, FAILED, "[TransferPool] EnsureDefaultStreamLocked: slot.ctx is null");
  const hixl::TemporaryRtContext guard(slot.ctx);
  aclrtStream stream = nullptr;
  HIXL_CHK_ACL_RET(aclrtCtxGetCurrentDefaultStream(&stream), "[TransferPool] aclrtCtxGetCurrentDefaultStream failed");
  HIXL_CHK_ACL_RET(aclrtSetStreamFailureMode(stream, ACL_STOP_ON_FAILURE),
                   "[TransferPool] aclrtSetStreamFailureMode failed");
  slot.stream = stream;
  return SUCCESS;
}

Status TransferPool::EnsureThreadLocked(Slot &slot) const {
  if (slot.thread != 0U) {
    return SUCCESS;
  }
  uint32_t notify_num = kDefaultNotifyNumPerThread;
  HIXL_CHK_HCCL_RET(HcommProxy::ThreadAlloc(kDefaultEngine, kDefaultThreadNum, &notify_num, &slot.thread));
  return SUCCESS;
}

Status TransferPool::EnsureDevConstOneLocked() {
  if (dev_const_one_ != nullptr) {
    return SUCCESS;
  }
  HIXL_CHK_ACL_RET(aclrtMalloc(&dev_const_one_, sizeof(uint64_t), ACL_MEM_MALLOC_NORMAL_ONLY),
                   "[TransferPool] aclrtMalloc dev_const_one_ failed");
  constexpr uint64_t host_one = 1U;
  HIXL_CHK_ACL_RET(
      aclrtMemcpy(dev_const_one_, sizeof(uint64_t), &host_one, sizeof(uint64_t), ACL_MEMCPY_HOST_TO_DEVICE),
      "[TransferPool] aclrtMemcpy dev_const_one_ failed");
  HIXL_LOGI("[TransferPool] dev_const_one initialized at %p on device %d", dev_const_one_, device_id_);
  return SUCCESS;
}

Status TransferPool::EnsureDeviceKernelsLocked() {
  if ((device_func_handles_.batch_get != nullptr) && (device_func_handles_.batch_put != nullptr) &&
      (device_func_handles_.sync_transfer_context != nullptr)) {
    return SUCCESS;
  }
  HIXL_CHECK_NOTNULL(rts_context_, "[TransferPool] rts_context_ is null when loading device kernels");
  const hixl::TemporaryRtContext rts_guard(rts_context_);
  HIXL_CHK_STATUS_RET(hixl::LoadDeviceKernelAndGetHandles(kDeviceFuncGet, kDeviceFuncPut, kernel_bin_handle_,
                                                          device_func_handles_, kDeviceFuncSyncContext),
                      "[TransferPool] LoadDeviceKernelAndGetHandles failed");
  HIXL_CHECK_NOTNULL(device_func_handles_.batch_get, "[TransferPool] batch get func is null");
  HIXL_CHECK_NOTNULL(device_func_handles_.batch_put, "[TransferPool] batch put func is null");
  HIXL_CHECK_NOTNULL(device_func_handles_.sync_transfer_context, "[TransferPool] sync transfer context func is null");
  return SUCCESS;
}

Status TransferPool::LaunchSyncContextKernelLocked(const std::vector<TransferContextSyncEntry> &entries,
                                                   std::vector<uint32_t> &states) const {
  HIXL_CHECK_NOTNULL(device_func_handles_.sync_transfer_context);
  HIXL_CHECK_NOTNULL(rts_context_, "[TransferPool] rts_context_ is null when launching sync context kernel");
  HIXL_CHK_BOOL_RET_STATUS(!entries.empty(), PARAM_INVALID, "[TransferPool] sync context entries is empty");
  const hixl::TemporaryRtContext rts_guard(rts_context_);
  const size_t entry_bytes = entries.size() * sizeof(TransferContextSyncEntry);
  void *dev_entries = nullptr;
  HIXL_CHK_ACL_RET(aclrtMalloc(&dev_entries, entry_bytes, ACL_MEM_MALLOC_NORMAL_ONLY),
                   "[TransferPool] aclrtMalloc sync context entries failed");
  HIXL_DISMISSABLE_GUARD(free_dev_entries, ([dev_entries]() {
                           HIXL_CHK_ACL(aclrtFree(dev_entries), "[TransferPool] aclrtFree sync context entries failed");
                         }));
  states.assign(entries.size(), TRANSFER_THREAD_STATE_DELETED);
  const size_t state_bytes = states.size() * sizeof(uint32_t);
  void *dev_states = nullptr;
  HIXL_CHK_ACL_RET(aclrtMalloc(&dev_states, state_bytes, ACL_MEM_MALLOC_NORMAL_ONLY),
                   "[TransferPool] aclrtMalloc sync context states failed");
  HIXL_DISMISSABLE_GUARD(free_dev_states, ([dev_states]() {
                           HIXL_CHK_ACL(aclrtFree(dev_states), "[TransferPool] aclrtFree sync context states failed");
                         }));
  HIXL_CHK_ACL_RET(aclrtMemcpy(dev_entries, entry_bytes, entries.data(), entry_bytes, ACL_MEMCPY_HOST_TO_DEVICE),
                   "[TransferPool] aclrtMemcpy sync context entries H2D failed");
  TransferContextSyncParam param{};
  param.entry_list_addr = PtrToValue(dev_entries);
  param.state_list_addr = PtrToValue(dev_states);
  param.entry_num = static_cast<uint32_t>(entries.size());
  aclrtArgsHandle args_handle = nullptr;
  HIXL_CHK_ACL_RET(aclrtKernelArgsInit(device_func_handles_.sync_transfer_context, &args_handle),
                   "[TransferPool] aclrtKernelArgsInit HixlSyncTransferContext failed");
  aclrtParamHandle para_handle = nullptr;
  HIXL_CHK_ACL_RET(aclrtKernelArgsAppend(args_handle, &param, sizeof(TransferContextSyncParam), &para_handle),
                   "[TransferPool] aclrtKernelArgsAppend HixlSyncTransferContext failed");
  HIXL_CHK_ACL_RET(aclrtKernelArgsFinalize(args_handle), "[TransferPool] aclrtKernelArgsFinalize failed");
  aclrtStream stream = nullptr;
  HIXL_CHK_ACL_RET(aclrtCtxGetCurrentDefaultStream(&stream),
                   "[TransferPool] aclrtCtxGetCurrentDefaultStream for HixlSyncTransferContext failed");
  constexpr uint32_t kBlockDim = 1U;
  HIXL_LOGI("[TransferPool] Launch HixlSyncTransferContext start. entries=%zu stream=%p device_id=%d", entries.size(),
            stream, device_id_);
  HIXL_CHK_ACL_RET(aclrtLaunchKernelWithConfig(device_func_handles_.sync_transfer_context, kBlockDim, stream, nullptr,
                                               args_handle, nullptr),
                   "[TransferPool] aclrtLaunchKernelWithConfig HixlSyncTransferContext failed");
  HIXL_CHK_ACL_RET(aclrtSynchronizeStreamWithTimeout(stream, static_cast<int32_t>(kSyncContextKernelTimeoutMs)),
                   "[TransferPool] aclrtSynchronizeStreamWithTimeout HixlSyncTransferContext failed");
  HIXL_CHK_ACL_RET(aclrtMemcpy(states.data(), state_bytes, dev_states, state_bytes, ACL_MEMCPY_DEVICE_TO_HOST),
                   "[TransferPool] aclrtMemcpy sync context states D2H failed");
  HIXL_LOGI("[TransferPool] Launch HixlSyncTransferContext end. entries=%zu device_id=%d", entries.size(), device_id_);
  return SUCCESS;
}

Status TransferPool::SyncContextsLocked(const std::vector<ThreadHandle> &threads, uint32_t op,
                                        uint32_t expect_state) const {
  if (threads.empty()) {
    return SUCCESS;
  }
  HIXL_CHK_BOOL_RET_STATUS(device_func_handles_.sync_transfer_context != nullptr, FAILED,
                           "[TransferPool] sync context func is null");
  std::vector<TransferContextSyncEntry> pending;
  pending.reserve(threads.size());
  for (ThreadHandle thread : threads) {
    if (thread == 0U) {
      continue;
    }
    pending.push_back({thread, op});
  }
  if (pending.empty()) {
    return SUCCESS;
  }
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kSyncContextRetryTimeoutMs);
  std::vector<uint32_t> pending_states;
  while (!pending.empty()) {
    std::vector<TransferContextSyncEntry> retry_entries;
    std::vector<uint32_t> retry_states;
    HIXL_CHK_STATUS_RET(RunSyncContextOnceLocked(pending, op, expect_state, retry_entries, retry_states),
                        "[TransferPool] run sync context failed");
    pending.swap(retry_entries);
    pending_states.swap(retry_states);
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

Status TransferPool::RunSyncContextOnceLocked(std::vector<TransferContextSyncEntry> &pending, uint32_t op,
                                              uint32_t expect_state,
                                              std::vector<TransferContextSyncEntry> &retry_entries,
                                              std::vector<uint32_t> &retry_states) const {
  std::vector<uint32_t> states;
  HIXL_CHK_STATUS_RET(LaunchSyncContextKernelLocked(pending, states),
                      "[TransferPool] launch sync context kernel failed");
  retry_entries.reserve(pending.size());
  retry_states.reserve(pending.size());
  return CollectRetrySyncEntries(pending, states, op, expect_state, retry_entries, retry_states);
}

Status TransferPool::CollectRetrySyncEntries(const std::vector<TransferContextSyncEntry> &entries,
                                             const std::vector<uint32_t> &states, uint32_t op, uint32_t expect_state,
                                             std::vector<TransferContextSyncEntry> &retry_entries,
                                             std::vector<uint32_t> &retry_states) const {
  HIXL_CHK_BOOL_RET_STATUS(entries.size() == states.size(), FAILED,
                           "[TransferPool] sync context state size mismatch, entries=%zu states=%zu", entries.size(),
                           states.size());
  for (size_t i = 0U; i < entries.size(); ++i) {
    const uint32_t state = states[i];
    if (state == expect_state) {
      continue;
    }
    if (state != TRANSFER_THREAD_STATE_DELETING) {
      HIXL_LOGE(FAILED, "[TransferPool] unexpected transfer context state=%u thread=%lu op=%u expect=%u", state,
                static_cast<uint64_t>(entries[i].thread), op, expect_state);
      return FAILED;
    }
    retry_entries.push_back({entries[i].thread, op});
    retry_states.push_back(state);
  }
  return SUCCESS;
}

Status TransferPool::HandleSyncContextTimeout(const std::vector<TransferContextSyncEntry> &pending,
                                              const std::vector<uint32_t> &states, uint32_t op) const {
  if (op != TRANSFER_CONTEXT_OP_DELETE) {
    HIXL_LOGE(TIMEOUT, "[TransferPool] sync transfer context timeout, pending=%zu op=%u device_id=%d", pending.size(),
              op, device_id_);
    return TIMEOUT;
  }
  for (size_t i = 0U; i < pending.size(); ++i) {
    const uint32_t state = (i < states.size()) ? states[i] : TRANSFER_THREAD_STATE_DELETING;
    HIXL_EVENT(
        "[TransferPool] delete transfer context timeout after %u ms, force cleanup. thread=%lu state=%u device_id=%d",
        kSyncContextRetryTimeoutMs, static_cast<uint64_t>(pending[i].thread), state, device_id_);
  }
  return SUCCESS;
}

Status TransferPool::AddTransferContextsLocked() const {
  std::vector<ThreadHandle> threads = CollectLiveThreads(slots_);
  return SyncContextsLocked(threads, TRANSFER_CONTEXT_OP_ADD, TRANSFER_THREAD_STATE_INITIALIZED);
}

Status TransferPool::DeleteTransferContextsLocked(const std::vector<ThreadHandle> &threads) const {
  return SyncContextsLocked(threads, TRANSFER_CONTEXT_OP_DELETE, TRANSFER_THREAD_STATE_DELETED);
}

Status TransferPool::SyncOneTransferContextLocked(ThreadHandle thread, uint32_t op, uint32_t expect_state) const {
  if (thread == 0U) {
    return SUCCESS;
  }
  std::vector<ThreadHandle> threads{thread};
  return SyncContextsLocked(threads, op, expect_state);
}

std::vector<ThreadHandle> TransferPool::CollectLiveThreads(const std::vector<Slot> &slots) {
  std::vector<ThreadHandle> threads;
  threads.reserve(slots.size());
  for (const auto &slot : slots) {
    if (slot.thread != 0U) {
      threads.push_back(slot.thread);
    }
  }
  return threads;
}

Status TransferPool::DestroySlotLocked(Slot &slot, bool sync_context) const {
  {
    hixl::TemporaryRtContext with_context(slot.ctx);
    if (slot.notify != nullptr) {
      HIXL_CHK_ACL(aclrtDestroyNotify(slot.notify));
      slot.notify = nullptr;
    }
  }
  if (slot.thread != 0U) {
    if (sync_context) {
      Status ret = SyncOneTransferContextLocked(slot.thread, TRANSFER_CONTEXT_OP_DELETE, TRANSFER_THREAD_STATE_DELETED);
      HIXL_CHK_STATUS(ret, "[TransferPool] delete transfer context failed before ThreadFree");
    }
    const hixl::TemporaryRtContext rts_guard(rts_context_);
    HIXL_CHK_ACL(HcommProxy::ThreadFree(&slot.thread, 1U), "HcommThreadFree failed");
    slot.thread = 0U;
  }
  if (slot.ctx != nullptr) {
    HIXL_LOGI("[TransferPool] destroying context %p", slot.ctx);
    HIXL_CHK_ACL(aclrtDestroyContext(slot.ctx), "destroy context failed");
    slot.ctx = nullptr;
  }
  slot.stream = nullptr;
  return SUCCESS;
}

aclrtContext TransferPool::GetContext() const {
  return rts_context_;
}

aclrtFuncHandle TransferPool::GetDeviceKernelFunc(bool is_get) const {
  std::lock_guard<std::mutex> lock(mu_);
  return is_get ? device_func_handles_.batch_get : device_func_handles_.batch_put;
}

}  // namespace hixl
