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
#include "notify_addr_resolver.h"

#include <chrono>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "acl/acl.h"
#include "acl/acl_rt.h"
#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/hixl_utils.h"
#include "common/llm_utils.h"
#include "common/scope_guard.h"
#include "common/soc_type.h"
#include "proxy/hcomm_proxy.h"
#include "load_kernel.h"
#include "proxy/ascend_hal_proxy.h"

namespace {
constexpr uint64_t kFlagInitValue = 0ULL;
constexpr CommEngine kDefaultEngine = CommEngine::COMM_ENGINE_AICPU_TS;
constexpr uint32_t kDefaultThreadNum = 1U;
constexpr uint32_t kDefaultNotifyNumPerThread = 0U;
constexpr uint32_t kSyncContextKernelTimeoutMs = 10U * 1000U;
constexpr uint32_t kSyncContextRetryTimeoutMs = 30U * 1000U;
constexpr uint32_t kSyncContextRetryIntervalMs = 100U;
constexpr int32_t kHcommHostExceptionMinVersion = 90300000;
constexpr const char *kDeviceFuncGet = "HixlBatchGet";
constexpr const char *kDeviceFuncPut = "HixlBatchPut";
constexpr const char *kDeviceFuncSyncContext = "HixlSyncTransferContext";
constexpr const char *kUbMemFuncRead = "HixlUbMemBatchRead";
constexpr const char *kUbMemFuncWrite = "HixlUbMemBatchWrite";

// host 异常回调（进程级）：kHixlCbMagic 作为 userData 传给 hcomm，回调按指针相等判定注册来源；
// g_host_cb_registered 缓存进程级注册结果，成功只注册一次，失败保持 false 以便下次 Initialize 重试。
char g_host_exc_magic[] = "hixl";
void *const kHixlCbMagic = g_host_exc_magic;
std::mutex g_host_cb_mu;
bool g_host_cb_registered = false;
// 进程级注册的引用计数：回调是进程级单例，注册生命周期应与所有存活且持引用的 pool 一致，
// 只有最后一个持引用的 pool 析构时才注销，避免收尾/析构单个 pool 时影响其它存活 pool。
uint32_t g_host_cb_ref_cnt = 0U;
}  // namespace

namespace hixl {

TransferPool *TransferPool::GetInstance(int32_t device_id) {
  HIXL_LOGD("[TransferPool] GetInstance start. device_id=%d", device_id);
  static std::mutex registry_mu;
  static std::unordered_map<int32_t, std::unique_ptr<TransferPool>> pools;
  std::lock_guard<std::mutex> reg_lock(registry_mu);
  auto it = pools.find(device_id);
  if (it != pools.end()) {
    HIXL_LOGD("[TransferPool] GetInstance success. device_id=%d pool=%p existing=1", device_id, it->second.get());
    return it->second.get();
  }
  auto pool_ptr = MakeUnique<TransferPool>(device_id);
  if (pool_ptr == nullptr) {
    HIXL_LOGE(FAILED, "[TransferPool] MakeUnique failed, device_id=%d", device_id);
    return nullptr;
  }
  const auto &inserted = pools.emplace(device_id, std::move(pool_ptr));
  HIXL_LOGD("[TransferPool] GetInstance success. device_id=%d pool=%p existing=0", device_id,
            inserted.first->second.get());
  return inserted.first->second.get();
}

TransferPool::TransferPool(int32_t device_id)
    : device_id_(device_id), ref_cnt_(0U), inited_(false), pool_size_(0U), free_list_(), slots_() {}

TransferPool::~TransferPool() {
  // 进程级回调只在这里注销（Finalize 不注销：pool 对象仍存活，回调靠 inited_ 自守）；必须在 mu_ 之外。
  DisableHostExceptionCallback();
  std::lock_guard<std::mutex> lock(mu_);
  try {
    DeinitAllSlotsLocked();
  } catch (const std::exception &e) {
    HIXL_LOGE(FAILED, "[TransferPool] DeinitAllSlotsLocked caught exception: %s, device_id=%d", e.what(), device_id_);
  } catch (...) {
    HIXL_LOGE(FAILED, "[TransferPool] DeinitAllSlotsLocked caught unknown exception, device_id=%d", device_id_);
  }
}

bool TransferPool::EnableHostExceptionCallback(SocType soc_type) {
  // A2/A3 keep device callbacks and NotifyWait.
  if (soc_type != SocType::kDefault || GetHcommVersion() < kHcommHostExceptionMinVersion) {
    return false;
  }
  std::lock_guard<std::mutex> lock(g_host_cb_mu);
  if (!g_host_cb_registered) {
    const int32_t ret = HcommProxy::RegisterExceptionCallback(&TransferPool::OnHostException, kHixlCbMagic);
    g_host_cb_registered = (ret == 0);
    if (g_host_cb_registered) {
      HIXL_LOGI("[TransferPool] host exception callback registered. device_id=%d", device_id_);
    } else {
      HIXL_LOGW("[TransferPool] host exception callback unavailable, fall back to device side. device_id=%d ret=%d",
                device_id_, ret);
    }
  }
  // 引用计数只用于 ~TransferPool：多个 pool 共享进程级注册，最后一个 pool 析构时才注销。
  if (g_host_cb_registered && !host_cb_ref_held_) {
    g_host_cb_ref_cnt += 1U;
    host_cb_ref_held_ = true;
  }
  return g_host_cb_registered;
}

void TransferPool::DisableHostExceptionCallback() {
  std::lock_guard<std::mutex> lock(g_host_cb_mu);
  if (!host_cb_ref_held_) {
    // 幂等：本 pool 未持引用（重复调用或注册失败）直接返回。
    return;
  }
  host_cb_ref_held_ = false;
  if (g_host_cb_ref_cnt > 0U) {
    g_host_cb_ref_cnt -= 1U;
  }
  // 仅最后一个持引用的 pool 析构时才注销，避免收尾单个 pool 影响其它存活 pool 的回调。
  if (g_host_cb_ref_cnt != 0U || !g_host_cb_registered) {
    return;
  }
  const int32_t ret = HcommProxy::UnregisterExceptionCallback(&TransferPool::OnHostException);
  if ((ret == 0) || (ret == static_cast<int32_t>(HCCL_E_NOT_SUPPORT))) {
    g_host_cb_registered = false;
    return;
  }
  HIXL_LOGW("[TransferPool] UnregisterExceptionCallback failed in destructor. device_id=%d ret=%d", device_id_, ret);
}

void TransferPool::OnHostException(const HcommExceptionInfo *raw_info, void *user_data) {
  // magic 校验：单 cb 会收到进程内所有组件（含 HCCL）的异常，只有 userData 恒为 hixl 自己时才受理。
  if (user_data != kHixlCbMagic || raw_info == nullptr) {
    return;
  }
  const auto &info = *raw_info;
  const auto &header = info.header;
  if (header.magicWord != HCOMM_EXCEPTION_MAGIC_WORD || header.version != HCOMM_EXCEPTION_VERSION) {
    HIXL_LOGW(
        "[TransferPool] Invalid Host exception ABI header. "
        "magic=0x%X expected_magic=0x%X version=%u expected_version=%u",
        header.magicWord, HCOMM_EXCEPTION_MAGIC_WORD, header.version, HCOMM_EXCEPTION_VERSION);
    return;
  }
  // HCOMM reports a logic device ID; pools are keyed by the ACL user device ID.
  int32_t user_device_id = -1;
  const aclError ret = aclrtGetUserDevIdByLogicDevId(static_cast<int32_t>(info.deviceId), &user_device_id);
  HIXL_CHK_ACL(ret, "[TransferPool] aclrtGetUserDevIdByLogicDevId failed. logic_device_id=%u ret=%d", info.deviceId,
               static_cast<int32_t>(ret));
  if (ret != ACL_SUCCESS) {
    return;
  }
  TransferPool *pool = TransferPool::GetInstance(user_device_id);
  if (pool == nullptr) {
    HIXL_LOGE(FAILED, "[TransferPool] GetInstance failed for host exception. logic_device_id=%u user_device_id=%d",
              info.deviceId, user_device_id);
    return;
  }
  std::lock_guard<std::mutex> lock(pool->mu_);
  if (!pool->inited_ || !pool->host_exception_enabled_) {
    HIXL_LOGD("[TransferPool] host exception ignored, pool host callback inactive. device_id=%d", pool->device_id_);
    return;
  }
  for (uint32_t i = 0U; i < pool->pool_size_; ++i) {
    Slot &slot = pool->slots_[i];
    if (slot.in_use && slot.thread == info.thread) {
      pool->AbortSlotStreamOnHostExceptionLocked(i, info);
      return;
    }
  }
  HIXL_LOGW("[TransferPool] host exception thread=%lu not matched. device_id=%d", static_cast<uint64_t>(info.thread),
            pool->device_id_);
}

void TransferPool::AbortSlotStreamOnHostExceptionLocked(uint32_t idx, const HcommExceptionInfo &info) {
  Slot &slot = slots_[idx];
  if (slot.err_flag_host_addr != nullptr) {
    *(slot.err_flag_host_addr) = 1U;
  }
  // (2) 可观测性。
  HIXL_LOGE(FAILED, "[TransferPool] device kernel exception. device_id=%d slot=%u thread=%lu taskId=%u retCode=%u",
            device_id_, idx, static_cast<uint64_t>(info.thread), info.taskId, info.retCode);
  // (3) 解 host 阻塞：abort 本 slot 的 stream，让同步等待立即返回。runtime 允许重复 abort，故用 cb_aborted
  //     单次消费标志跳过用户路径的第二遍，避免二次 abort 的 E19999 误报与最坏 60s 轮询。
  if (slot.cb_aborted || slot.stream == nullptr || slot.ctx == nullptr) {
    return;
  }
  {
    const hixl::TemporaryRtContext ctx_guard(slot.ctx);
    const aclError ret = aclrtStreamAbort(slot.stream);
    if (ret != ACL_SUCCESS) {
      HIXL_LOGE(ret, "[TransferPool] aclrtStreamAbort failed in host exception. device_id=%d slot=%u ret=0x%X",
                device_id_, idx, static_cast<uint32_t>(ret));
      return;
    }
  }
  slot.cb_aborted = true;
}

void TransferPool::InitFreeListLocked() {
  free_list_.clear();
  for (uint32_t i = 0U; i < pool_size_; ++i) {
    free_list_.push_back(i);
  }
}

static void RegisterDeviceAbortCallback();

Status TransferPool::Initialize(uint32_t pool_size) {
  HIXL_LOGI("[TransferPool] Initialize start. device_id=%d pool_size=%u", device_id_, pool_size);
  if ((pool_size == 0U) || (pool_size > kMaxPoolSize)) {
    HIXL_LOGE(PARAM_INVALID, "[TransferPool] Initialize invalid pool_size=%u (device_id=%d, max=%u)", pool_size,
              device_id_, kMaxPoolSize);
    return PARAM_INVALID;
  }
  SocType soc_type = SocType::kDefault;
  HIXL_CHK_STATUS_RET(SocTypeUtil::GetSocType(soc_type), "[TransferPool] GetSocType failed. device_id=%d", device_id_);
  // 进程级注册 host 异常回调：必须在 lock(mu_) 之前。
  // 注销只在 ~TransferPool 执行，Finalize 与 Initialize 失败都不注销，pool 对象存活期间回调靠 inited_ 自守。
  const bool host_exception_registered = EnableHostExceptionCallback(soc_type);
  std::lock_guard<std::mutex> lock(mu_);
  if (inited_) {
    if (pool_size != pool_size_) {
      HIXL_LOGE(PARAM_INVALID, "[TransferPool] Initialize pool_size mismatch. inited=%u got=%u (device_id=%d)",
                pool_size_, pool_size, device_id_);
      return PARAM_INVALID;
    }
    ref_cnt_ += 1U;
    HIXL_LOGI("[TransferPool] Initialize success. device_id=%d pool_size=%u ref_cnt=%u already_inited=1", device_id_,
              pool_size_, ref_cnt_);
    return SUCCESS;
  }
  // Keep the callback route fixed for the lifetime of the device transfer contexts.
  host_exception_enabled_ = host_exception_registered;
  pool_size_ = pool_size;
  ResetSlotsLocked(pool_size_);
  HIXL_DISMISSABLE_GUARD(init_rollback, ([this]() { DeinitAllSlotsLocked(); }));
  HIXL_CHK_ACL_RET(aclrtCreateContext(&rts_context_, device_id_), "aclrtCreateContext rts_context_ failed");
  HIXL_CHK_STATUS_RET(InitializeResourcesLocked(), "[TransferPool] InitializeResourcesLocked failed");
  ref_cnt_ = 1U;
  inited_ = true;
  HIXL_DISMISS_GUARD(init_rollback);
  RegisterDeviceAbortCallback();
  HIXL_LOGI("[TransferPool] Initialize success. device_id=%d pool_size=%u ref_cnt=%u", device_id_, pool_size_,
            ref_cnt_);
  return SUCCESS;
}

void TransferPool::ResetSlotsLocked(uint32_t pool_size) {
  slots_.clear();
  slots_.resize(pool_size);
  for (Slot &slot : slots_) {
    slot = Slot{};
  }
}

Status TransferPool::InitializeResourcesLocked() {
  Status ret = EnsureDeviceKernelsLocked();
  if (ret != SUCCESS) {
    return ret;
  }
  ret = EnsureErrFlagMemLocked();
  if (ret != SUCCESS) {
    return ret;
  }
  if (host_exception_enabled_) {
    HIXL_CHK_STATUS_RET(EnsureFlagBlockLocked(), "[TransferPool] initialize completion flag block failed");
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
  return SUCCESS;
}

// stop_device 的 POST 回调：只置标志（独立锁，不抢 mu_），由后续接口重建线程。
static int32_t HixlDeviceAbortCallback(int32_t device_id, aclrtDeviceTaskAbortStage stage, uint32_t timeout,
                                       void *args) {
  (void)timeout;
  (void)args;
  if (stage != ACL_RT_DEVICE_TASK_ABORT_POST) {
    return 0;
  }
  auto *pool = TransferPool::GetInstance(device_id);
  if (pool == nullptr) {
    return 0;
  }
  pool->OnDeviceAbortPost();
  return 0;
}

static std::once_flag g_abort_cb_once;
static void RegisterDeviceAbortCallback() {
  std::call_once(g_abort_cb_once, []() {
    const aclError ret = aclrtSetDeviceTaskAbortCallback("hixl_transfer_pool", HixlDeviceAbortCallback, nullptr);
    if (ret != ACL_SUCCESS) {
      HIXL_LOGW("[TransferPool] RegisterDeviceAbortCallback failed ret=%d", ret);
    }
  });
}

void TransferPool::OnDeviceAbortPost() {
  device_abort_post_.store(true);
  HIXL_LOGI("[TransferPool] OnDeviceAbortPost, threads need rebuild. device_id=%d", device_id_);
}

void TransferPool::EnsureThreadsRebuiltIfNeededLocked() {
  if (!device_abort_post_.load()) {
    return;
  }
  if (free_list_.empty()) {
    return;  // 无空闲 slot 可重建，保留标志待下次 Acquire 再消费
  }
  device_abort_post_.store(false);
  HIXL_LOGI("[TransferPool] Rebuilding threads after device abort. device_id=%d", device_id_);

  ResetAbortSlotNotifyLocked(slots_[free_list_.front()]);  // 探测设备已从 abort 恢复（一次）

  DeleteStaleSlotContextsLocked();
  const std::vector<uint32_t> rebuilt_idx = RebuildSlotThreadsLocked();
  AddRebuiltSlotContextsLocked(rebuilt_idx);
}

void TransferPool::DeleteStaleSlotContextsLocked() {
  std::vector<HixlTransferContextSyncEntry> del_entries;
  for (const uint32_t idx : free_list_) {
    if (slots_[idx].thread != 0U) {
      del_entries.push_back(BuildSyncEntry(slots_[idx], TRANSFER_CONTEXT_OP_DELETE));
    }
  }
  HIXL_CHK_STATUS(DeleteTransferContextsLocked(del_entries),
                  "[TransferPool] thread rebuild batch delete ctx failed. device_id=%d", device_id_);
}

std::vector<uint32_t> TransferPool::RebuildSlotThreadsLocked() {
  std::vector<uint32_t> rebuilt_idx;
  rebuilt_idx.reserve(free_list_.size());
  const hixl::TemporaryRtContext rts_guard(rts_context_);
  for (const uint32_t idx : free_list_) {
    Slot &slot = slots_[idx];
    if (slot.thread != 0U) {
      const HcclResult free_ret = HcommProxy::ThreadFree(&slot.thread, 1U);
      if (free_ret != HCCL_SUCCESS) {
        HIXL_LOGE(FAILED,
                  "[TransferPool] thread rebuild free failed, keep handle. device_id=%d slot=%u thread=%lu ret=%d",
                  device_id_, idx, static_cast<uint64_t>(slot.thread), static_cast<int32_t>(free_ret));
        continue;
      }
      slot.thread = 0U;
    }
    uint32_t notify_num = kDefaultNotifyNumPerThread;
    const HcclResult alloc_ret = HcommProxy::ThreadAlloc(kDefaultEngine, kDefaultThreadNum, &notify_num, &slot.thread);
    if (alloc_ret != HCCL_SUCCESS) {
      HIXL_LOGE(FAILED, "[TransferPool] thread rebuild alloc failed. device_id=%d slot=%u ret=%d", device_id_, idx,
                static_cast<int32_t>(alloc_ret));
      continue;
    }
    rebuilt_idx.push_back(idx);
  }
  return rebuilt_idx;
}

void TransferPool::AddRebuiltSlotContextsLocked(const std::vector<uint32_t> &rebuilt_idx) {
  std::vector<HixlTransferContextSyncEntry> add_entries;
  add_entries.reserve(rebuilt_idx.size());
  for (const uint32_t idx : rebuilt_idx) {
    add_entries.push_back(BuildSyncEntry(slots_[idx], TRANSFER_CONTEXT_OP_ADD));
  }
  HIXL_CHK_STATUS(SyncContextsLocked(add_entries, TRANSFER_CONTEXT_OP_ADD, TRANSFER_THREAD_STATE_INITIALIZED),
                  "[TransferPool] thread rebuild batch add ctx failed. device_id=%d", device_id_);
}

void TransferPool::Finalize() {
  HIXL_LOGI("[TransferPool] Finalize start. device_id=%d", device_id_);
  std::lock_guard<std::mutex> lock(mu_);
  if (ref_cnt_ == 0U) {
    HIXL_LOGI("[TransferPool] Finalize success. device_id=%d ref_cnt=0 already_finalized=1", device_id_);
    return;
  }
  ref_cnt_ -= 1U;
  if (ref_cnt_ != 0U) {
    HIXL_LOGI("[TransferPool] Finalize success. device_id=%d ref_cnt=%u defer_deinit=1", device_id_, ref_cnt_);
    return;
  }
  if (inited_) {
    AbortInUseStreamsLocked();
  }
  DeinitAllSlotsLocked();
  HIXL_LOGI("[TransferPool] Finalize success. device_id=%d ref_cnt=%u", device_id_, ref_cnt_);
}

Status TransferPool::Acquire(SlotHandle *handle) {
  HIXL_LOGD("[TransferPool] Acquire start. device_id=%d", device_id_);
  HIXL_CHECK_NOTNULL(handle);
  std::lock_guard<std::mutex> lock(mu_);
  HIXL_CHK_BOOL_RET_STATUS(inited_, FAILED, "[TransferPool] Acquire failed: not initialized, device_id:%d", device_id_);
  EnsureThreadsRebuiltIfNeededLocked();
  if (free_list_.empty()) {
    HIXL_LOGE(RESOURCE_EXHAUSTED, "[TransferPool] Acquire failed: no free slots (device_id=%d, pool_size=%u)",
              device_id_, pool_size_);
    return RESOURCE_EXHAUSTED;
  }
  const uint32_t idx = free_list_.front();
  free_list_.pop_front();
  Slot &slot = slots_[idx];
  slot.in_use = true;
  // Clear the callback abort marker for the next use of this slot.
  slot.cb_aborted = false;
  FillHandleFromSlot(device_id_, idx, slot, handle);
  handle->dev_const_one = dev_const_one_;
  HIXL_LOGD("[TransferPool] Acquire slot success. device_id=%d index=%u", device_id_, idx);
  return SUCCESS;
}

void TransferPool::Release(const SlotHandle &handle) {
  HIXL_LOGD("[TransferPool] Release start. device_id=%d slot=%u", handle.device_id, handle.slot_index);
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
  slot.launched_tasks = handle.launched_tasks;
  ResetSlotErrFlagLocked(handle.slot_index);
  ResetSlotFlagLocked(slot);
  // 清 host 异常回调单次消费标志，防止污染下一个使用者。
  slot.cb_aborted = false;
  slot.in_use = false;
  free_list_.push_back(handle.slot_index);
  HIXL_LOGD("[TransferPool] Release success. device_id=%d slot=%u", device_id_, handle.slot_index);
}

void TransferPool::Abort(const SlotHandle &handle) {
  HIXL_LOGD("[TransferPool] Abort start. device_id=%d slot=%u", handle.device_id, handle.slot_index);
  std::lock_guard<std::mutex> lock(mu_);
  if (!inited_) {
    HIXL_LOGD("[TransferPool] Abort success. device_id=%d slot=%u inited=0", handle.device_id, handle.slot_index);
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
  AbortSlotByIndexLocked(handle.slot_index, handle.launched_tasks);
  HIXL_LOGD("[TransferPool] Abort success. device_id=%d slot=%u", device_id_, handle.slot_index);
}

Status TransferPool::GetAllSlots(std::vector<SlotHandle> &out) const {
  HIXL_LOGD("[TransferPool] GetAllSlots start. device_id=%d", device_id_);
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
  HIXL_LOGD("[TransferPool] GetAllSlots success. device_id=%d slot_count=%zu", device_id_, out.size());
  return SUCCESS;
}

Status TransferPool::ResolveNotifyAddr() {
  HIXL_LOGD("[TransferPool] ResolveNotifyAddr start. device_id=%d", device_id_);
  std::lock_guard<std::mutex> lock(mu_);
  HIXL_CHK_BOOL_RET_STATUS(inited_, FAILED, "[TransferPool] ResolveNotifyAddr failed: not initialized, device_id:%d",
                           device_id_);
  for (Slot &slot : slots_) {
    if (slot.notify_addr != 0U && slot.notify_len != 0U) {
      continue;
    }
    HIXL_CHK_STATUS_RET(ResolveNotifyAddressLocked(slot), "[TransferPool] ResolveNotifyAddressLocked failed");
  }
  HIXL_LOGD("[TransferPool] ResolveNotifyAddr success. device_id=%d slot_count=%zu", device_id_, slots_.size());
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
  handle->notify_addr = slot.notify_addr;
  handle->notify_len = slot.notify_len;
  handle->err_flag_host_addr = slot.err_flag_host_addr;
  handle->err_flag_dev_addr = slot.err_flag_dev_addr;
  handle->flag_dev_addr = slot.flag_dev_addr;
  handle->launched_tasks = slot.launched_tasks;
  handle->ubmem_stream = slot.ubmem_stream;
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
  slot.launched_tasks = 0;
  HIXL_CHK_STATUS_RET(EnsureContextLocked(slot), "[TransferPool] EnsureContextLocked failed");
  HIXL_CHK_STATUS_RET(EnsureDefaultStreamLocked(slot), "[TransferPool] EnsureDefaultStreamLocked failed");
  HIXL_CHK_STATUS_RET(EnsureThreadLocked(slot), "[TransferPool] EnsureThreadLocked failed");
  HIXL_CHK_STATUS_RET(EnsureNotifyLocked(slot), "[TransferPool] EnsureNotifyLocked failed");
  AssignSlotErrFlagLocked(slot, slot_index);
  return SUCCESS;
}

Status TransferPool::ResolveNotifyAddressLocked(Slot &slot) const {
  slot.notify_addr = 0U;
  slot.notify_len = 0U;
  HIXL_CHECK_NOTNULL(slot.notify, "[TransferPool] ResolveNotifyAddressLocked notify is null");
  HIXL_CHK_STATUS_RET(NotifyAddrResolver::Resolve(device_id_, slot.notify, slot.notify_addr, slot.notify_len),
                      "[TransferPool] resolve notify address failed");
  HIXL_LOGD("[TransferPool] Resolved notify address. notify_id=%u addr=0x%llx len=%u", slot.notify_id,
            static_cast<unsigned long long>(slot.notify_addr), slot.notify_len);
  return SUCCESS;
}

void TransferPool::AbortSlotByIndexLocked(uint32_t slot_index, uint64_t launched) {
  if (slot_index >= pool_size_) {
    return;
  }
  Slot &slot = slots_[slot_index];
  if (!slot.in_use) {
    return;
  }

  AbortSlotRuntimeLocked(slot);
  uint64_t dev_dispatched = 0;
  const Status sync_ret = DeleteSlotThreadContextForAbortLocked(slot, slot_index, &dev_dispatched);
  if (sync_ret == SUCCESS && launched != dev_dispatched) {
    HIXL_EVENT("[TransferPool] Slot task count mismatch on abort: slot=%u launched=%lu device_dispatched=%lu",
               slot_index, launched, dev_dispatched);
  }
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
  if (slot.ubmem_stream != nullptr) {
    HIXL_CHK_ACL(aclrtStreamStop(slot.ubmem_stream), "[TransferPool] aclrtStreamStop ubmem stream failed in Finalize");
  }
  // host 异常回调已 abort 过该 slot 的 stream 时跳过，避免二次 abort。
  if (!slot.cb_aborted && slot.stream != nullptr) {
    HIXL_CHK_ACL(aclrtStreamAbort(slot.stream), "[TransferPool] aclrtStreamAbort failed in Finalize");
  }
}

void TransferPool::AbortSlotRuntimeLocked(Slot &slot) const {
  // Abort drains the host-visible stream that memcpy submissions use. The device-only AICPU worker
  // stream has no host-visible completion, so destroying it releases the device resources after abort.
  {
    hixl::TemporaryRtContext guard(slot.ctx);
    if (!slot.cb_aborted && slot.stream != nullptr) {
      HIXL_CHK_ACL(aclrtStreamAbort(slot.stream), "[TransferPool] aclrtStreamAbort failed");
    }
  }
  (void)DestroyUbMemStreamLocked(slot);
  slot.stream = nullptr;
  ResetAbortSlotNotifyLocked(slot);
}

void TransferPool::ResetAbortSlotNotifyLocked(Slot &slot) {
  if (slot.notify == nullptr) {
    return;
  }
  aclrtNotify *notify_ptr = &slot.notify;
  // 设备在 stop/abort 窗口时 aclrtNotifyBatchReset 在 host 侧快速返 107022(DEVICE_TASK_ABORT)；
  // 隔 interval 重试，DeviceClean 恢复 status 后成功，再往下走销毁+重建。
  constexpr uint32_t kAbortRetryIntervalMs = 20U;
  constexpr uint32_t kAbortRetryTotalMs = 60000U;
  uint32_t waited = 0U;
  while (true) {
    const aclError reset_ret = aclrtNotifyBatchReset(notify_ptr, static_cast<size_t>(1));
    if (reset_ret == ACL_SUCCESS) {
      return;
    }
    if (reset_ret != ACL_ERROR_RT_DEVICE_TASK_ABORT || waited >= kAbortRetryTotalMs) {
      HIXL_LOGE(FAILED, "[TransferPool] aclrtNotifyBatchReset failed in abort, notify=%p ret=0x%X", slot.notify,
                static_cast<uint32_t>(reset_ret));
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(kAbortRetryIntervalMs));
    waited += kAbortRetryIntervalMs;
  }
}

Status TransferPool::DeleteSlotThreadContextForAbortLocked(Slot &slot, uint32_t slot_index,
                                                           uint64_t *out_dispatched) const {
  Status sync_ret = SUCCESS;
  if (slot.thread != 0U) {
    sync_ret =
        SyncOneTransferContextLocked(slot, TRANSFER_CONTEXT_OP_DELETE, TRANSFER_THREAD_STATE_DELETED, out_dispatched);
    HIXL_CHK_STATUS(sync_ret,
                    "[TransferPool] delete transfer context failed in AbortSlotByIndexLocked, slot=%u device_id=%d",
                    slot_index, device_id_);
  }
  if (slot.thread != 0U) {
    const ThreadHandle thread = slot.thread;
    const hixl::TemporaryRtContext rts_guard(rts_context_);
    const auto free_ret = HcommProxy::ThreadFree(&slot.thread, 1U);
    HIXL_CHK_ACL(free_ret, "[TransferPool] HcommThreadFree failed in AbortSlotByIndexLocked, device_id=%d, thread=%lu",
                 device_id_, static_cast<uint64_t>(thread));
    HIXL_EVENT("[TransferPool] slot thread free success, device_id=%d, thread=%lu, scene=abort", device_id_,
               static_cast<uint64_t>(thread));
    slot.thread = 0U;
  }
  return sync_ret;
}

void TransferPool::DestroySlotContextForAbortLocked(Slot &slot) const {
  if (slot.ctx != nullptr) {
    HIXL_CHK_ACL(aclrtDestroyContext(slot.ctx), "[TransferPool] aclrtDestroyContext failed for slot");
    slot.ctx = nullptr;
    slot.stream = nullptr;
    slot.cb_aborted = false;
  }
}

void TransferPool::CleanupSlotAfterAbortReinitFailureLocked(Slot &slot, uint32_t slot_index) const {
  ResetAbortSlotNotifyLocked(slot);
  (void)DeleteSlotThreadContextForAbortLocked(slot, slot_index);
  DestroySlotContextForAbortLocked(slot);
  slot.in_use = false;
}

Status TransferPool::ReinitSlotAfterAbortLocked(Slot &slot, uint32_t slot_index) const {
  Status ret = InitOneSlotLocked(slot, slot_index);
  if (ret != SUCCESS) {
    HIXL_LOGE(ret, "[TransferPool] AbortSlotByIndexLocked re-init failed slot=%u device_id=%d", slot_index, device_id_);
    CleanupSlotAfterAbortReinitFailureLocked(slot, slot_index);
    return ret;
  }
  ret = SyncOneTransferContextLocked(slot, TRANSFER_CONTEXT_OP_ADD, TRANSFER_THREAD_STATE_INITIALIZED);
  if (ret != SUCCESS) {
    HIXL_LOGE(ret, "[TransferPool] AbortSlotByIndexLocked add context failed slot=%u device_id=%d", slot_index,
              device_id_);
    CleanupSlotAfterAbortReinitFailureLocked(slot, slot_index);
    return ret;
  }
  return SUCCESS;
}

void TransferPool::ReturnSlotToFreeListLocked(uint32_t slot_index) {
  ResetSlotErrFlagLocked(slot_index);
  Slot &slot = slots_[slot_index];
  ResetSlotFlagLocked(slot);
  slot.in_use = false;
  free_list_.push_back(slot_index);
}

Status TransferPool::EnsureNotifyLocked(Slot &slot) const {
  if (slot.notify == nullptr) {
    ResetNotifyResourcesLocked(slot);
    HIXL_CHK_STATUS_RET(CreateNotifyLocked(slot), "[TransferPool] CreateNotifyLocked failed");
  }
  return SUCCESS;
}

void TransferPool::ResetNotifyResourcesLocked(Slot &slot) {
  if (slot.notify != nullptr) {
    HIXL_CHK_ACL(aclrtDestroyNotify(slot.notify));
    slot.notify = nullptr;
  }
  slot.notify_id = 0U;
  slot.notify_addr = 0U;
  slot.notify_len = 0U;
}

Status TransferPool::CreateNotifyLocked(Slot &slot) {
  HIXL_CHK_ACL_RET(aclrtCreateNotify(&slot.notify, ACL_NOTIFY_DEVICE_USE_ONLY),
                   "[TransferPool] aclrtCreateNotify failed");
  HIXL_CHK_ACL_RET(aclrtGetNotifyId(slot.notify, &slot.notify_id), "[TransferPool] aclrtGetNotifyId failed");
  HIXL_LOGD("[TransferPool] Created notify. notify_id=%u", slot.notify_id);
  return SUCCESS;
}

void TransferPool::DeinitAllSlotsLocked() {
  std::vector<HixlTransferContextSyncEntry> entries = BuildSyncEntriesFromSlots(slots_, TRANSFER_CONTEXT_OP_DELETE);
  if (!entries.empty()) {
    HIXL_CHK_STATUS(DeleteTransferContextsLocked(entries),
                    "[TransferPool] delete transfer contexts failed in DeinitAllSlotsLocked, device_id=%d", device_id_);
  }
  if (dev_const_one_ != nullptr) {
    HIXL_CHK_ACL(aclrtFree(dev_const_one_));
    dev_const_one_ = nullptr;
    HIXL_LOGI("[TransferPool] released dev_const_one_ on device %d", device_id_);
  }
  for (uint32_t i = 0U; i < pool_size_; ++i) {
    if ((slots_[i].ctx != nullptr) || (slots_[i].stream != nullptr) || (slots_[i].thread != 0U) ||
        (slots_[i].notify != nullptr) || (slots_[i].ubmem_stream != nullptr)) {
      HIXL_CHK_STATUS(DestroySlotLocked(slots_[i], false),
                      "[TransferPool] DeinitAllSlotsLocked destroy slot failed, idx=%u", i);
    }
    slots_[i] = Slot{};
    slots_[i].in_use = false;
  }
  if (flag_device_base_ != nullptr) {
    const hixl::TemporaryRtContext rts_guard(rts_context_);
    FreeFlagBlockLocked();
  }
  if ((err_flag_host_base_ != nullptr) || (err_flag_device_base_ != nullptr)) {
    const hixl::TemporaryRtContext rts_guard(rts_context_);
    FreeErrFlagMemLocked();
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
  HIXL_EVENT("[TransferPool] slot thread alloc success, device_id=%d, thread=%lu", device_id_,
             static_cast<uint64_t>(slot.thread));
  return SUCCESS;
}

Status TransferPool::CreateUbMemStreamLocked(Slot &slot) const {
  if (slot.ubmem_stream != nullptr) {
    return SUCCESS;
  }
  HIXL_CHK_BOOL_RET_STATUS(slot.ctx != nullptr, FAILED, "[TransferPool] CreateUbMemStreamLocked: slot.ctx is null");
  const hixl::TemporaryRtContext guard(slot.ctx);
  aclrtStream stream = nullptr;
  HIXL_CHK_ACL_RET(aclrtCreateStreamWithConfig(&stream, 0U, ACL_STREAM_DEVICE_USE_ONLY),
                   "[TransferPool] aclrtCreateStreamWithConfig ubmem stream failed");
  slot.ubmem_stream = stream;
  return SUCCESS;
}

void TransferPool::DestroyUbMemStreamLocked(Slot &slot) {
  if (slot.ubmem_stream == nullptr) {
    return;
  }
  const hixl::TemporaryRtContext guard(slot.ctx);
  HIXL_CHK_ACL(aclrtDestroyStream(slot.ubmem_stream), "[TransferPool] aclrtDestroyStream ubmem stream failed");
  slot.ubmem_stream = nullptr;
}

bool TransferPool::IsInitialized() const {
  std::lock_guard<std::mutex> lock(mu_);
  return inited_;
}

bool TransferPool::IsHostExceptionEnabled() const {
  std::lock_guard<std::mutex> lock(mu_);
  return host_exception_enabled_;
}

Status TransferPool::EnsureUbMemStream(SlotHandle &handle) {
  std::lock_guard<std::mutex> lock(mu_);
  HIXL_CHK_BOOL_RET_STATUS(inited_, FAILED, "[TransferPool] EnsureUbMemStream failed: not initialized, device_id:%d",
                           device_id_);
  HIXL_CHK_BOOL_RET_STATUS(handle.device_id == device_id_, PARAM_INVALID,
                           "[TransferPool] EnsureUbMemStream device_id mismatch. pool=%d handle=%d", device_id_,
                           handle.device_id);
  HIXL_CHK_BOOL_RET_STATUS(handle.slot_index < pool_size_, PARAM_INVALID,
                           "[TransferPool] EnsureUbMemStream invalid slot index %u", handle.slot_index);
  Slot &slot = slots_[handle.slot_index];
  HIXL_CHK_BOOL_RET_STATUS(slot.in_use, FAILED, "[TransferPool] EnsureUbMemStream slot %u is not in use",
                           handle.slot_index);
  HIXL_CHK_STATUS_RET(CreateUbMemStreamLocked(slot), "[TransferPool] CreateUbMemStreamLocked failed, slot=%u",
                      handle.slot_index);
  handle.ubmem_stream = slot.ubmem_stream;
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

Status TransferPool::AllocErrFlagHostMappedLocked(uint64_t total_size, uint32_t logic_dev_id) {
  err_flag_from_device_ = false;
  void *host_va = nullptr;
  HIXL_CHK_ACL_RET(aclrtMallocHost(&host_va, total_size), "[TransferPool] aclrtMallocHost err_flag failed");
  HIXL_DISMISSABLE_GUARD(err_flag_guard, ([this, &host_va]() {
                           if (host_va != nullptr) {
                             HIXL_CHK_ACL(aclrtFreeHost(host_va));
                             host_va = nullptr;
                           }
                           err_flag_host_base_ = nullptr;
                           err_flag_device_base_ = nullptr;
                         }));
  HIXL_CHK_ACL_RET(aclrtMemset(host_va, total_size, 0, total_size), "[TransferPool] aclrtMemset err_flag failed");

  void *device_va = nullptr;
  const Status reg_ret =
      AscendHalProxy::HostRegister(host_va, total_size, kHostMemMapDevPcieTh, logic_dev_id, &device_va);
  if (reg_ret != SUCCESS) {
    HIXL_LOGW("[TransferPool] halHostRegister HOST_MEM_MAP_DEV_PCIE_TH failed, continue without device mapping");
    return SUCCESS;
  }

  err_flag_host_base_ = host_va;
  err_flag_device_base_ = device_va;
  HIXL_DISMISS_GUARD(err_flag_guard);
  return SUCCESS;
}

Status TransferPool::AllocErrFlagDeviceMappedLocked(uint64_t total_size, uint32_t logic_dev_id) {
  err_flag_from_device_ = true;
  void *device_va = nullptr;
  HIXL_DISMISSABLE_GUARD(err_flag_guard, ([this, &device_va]() {
                           if (device_va != nullptr) {
                             HIXL_CHK_ACL(aclrtFree(device_va));
                             device_va = nullptr;
                           }
                           err_flag_host_base_ = nullptr;
                           err_flag_device_base_ = nullptr;
                           err_flag_from_device_ = false;
                         }));
  HIXL_CHK_ACL_RET(aclrtMalloc(&device_va, total_size, static_cast<aclrtMemMallocPolicy>(ACL_MEM_TYPE_HIGH_BAND_WIDTH)),
                   "[TransferPool] aclrtMalloc err_flag device failed");
  HIXL_CHK_ACL_RET(aclrtMemset(device_va, total_size, 0, total_size), "[TransferPool] aclrtMemset err_flag failed");

  void *host_va = nullptr;
  const Status reg_ret = AscendHalProxy::HostRegister(device_va, total_size, kDevSvmMapHost, logic_dev_id, &host_va);
  if (reg_ret != SUCCESS) {
    HIXL_LOGW("[TransferPool] halHostRegister DEV_SVM_MAP_HOST failed, continue without host mapping");
    return SUCCESS;
  }

  err_flag_device_base_ = device_va;
  err_flag_host_base_ = host_va;
  HIXL_DISMISS_GUARD(err_flag_guard);
  return SUCCESS;
}

Status TransferPool::EnsureErrFlagMemLocked() {
  if ((err_flag_host_base_ != nullptr) || (err_flag_device_base_ != nullptr)) {
    return SUCCESS;
  }
  const uint64_t total_size = static_cast<uint64_t>(pool_size_) * sizeof(uint8_t);
  int32_t logic_dev_id = 0;
  HIXL_CHK_ACL_RET(aclrtGetLogicDevIdByUserDevId(device_id_, &logic_dev_id),
                   "[TransferPool] aclrtGetLogicDevIdByUserDevId failed. device_id=%d logic_dev_id=%d", device_id_,
                   logic_dev_id);
  SocType soc_type = SocType::kDefault;
  HIXL_CHK_STATUS_RET(SocTypeUtil::GetSocType(soc_type),
                      "[TransferPool] GetSocType failed. device_id=%d logic_dev_id=%d", device_id_, logic_dev_id);
  if ((soc_type == SocType::kGen1) || (soc_type == SocType::kGen2)) {
    HIXL_CHK_STATUS_RET(AllocErrFlagDeviceMappedLocked(total_size, static_cast<uint32_t>(logic_dev_id)),
                        "[TransferPool] AllocErrFlagDeviceMappedLocked failed");
  } else {
    HIXL_CHK_STATUS_RET(AllocErrFlagHostMappedLocked(total_size, static_cast<uint32_t>(logic_dev_id)),
                        "[TransferPool] AllocErrFlagHostMappedLocked failed");
  }
  HIXL_LOGI("[TransferPool] err_flag block initialized. host=%p device_base=%p size=%lu device_id=%d from_device=%d",
            err_flag_host_base_, err_flag_device_base_, total_size, device_id_,
            static_cast<int32_t>(err_flag_from_device_));
  return SUCCESS;
}

void TransferPool::FreeErrFlagMemLocked() {
  void *reg_src = err_flag_from_device_ ? err_flag_device_base_ : err_flag_host_base_;
  const bool mapped = (err_flag_host_base_ != nullptr) && (err_flag_device_base_ != nullptr);
  if (mapped && (reg_src != nullptr)) {
    int32_t logic_dev_id = 0;
    const aclError logic_ret = aclrtGetLogicDevIdByUserDevId(device_id_, &logic_dev_id);
    if (logic_ret == ACL_SUCCESS) {
      HIXL_CHK_STATUS(AscendHalProxy::HostUnregister(reg_src, static_cast<uint32_t>(logic_dev_id)),
                      "[TransferPool] HostUnregister failed. device_id=%d logic_dev_id=%d", device_id_, logic_dev_id);
    } else {
      HIXL_LOGW(
          "[TransferPool] aclrtGetLogicDevIdByUserDevId failed on free, skip HostUnregister. ret=%d "
          "device_id=%d logic_dev_id=%d",
          static_cast<int32_t>(logic_ret), device_id_, logic_dev_id);
    }
  }
  if (err_flag_from_device_) {
    if (err_flag_device_base_ != nullptr) {
      HIXL_CHK_ACL(aclrtFree(err_flag_device_base_));
    }
  } else if (err_flag_host_base_ != nullptr) {
    HIXL_CHK_ACL(aclrtFreeHost(err_flag_host_base_));
  }
  err_flag_host_base_ = nullptr;
  err_flag_device_base_ = nullptr;
  err_flag_from_device_ = false;
}

void TransferPool::AssignSlotErrFlagLocked(Slot &slot, uint32_t slot_index) const {
  if ((err_flag_host_base_ == nullptr) || (err_flag_device_base_ == nullptr)) {
    return;
  }
  uint8_t *host_base = static_cast<uint8_t *>(err_flag_host_base_);
  uint8_t *device_base = static_cast<uint8_t *>(err_flag_device_base_);
  slot.err_flag_host_addr = host_base + slot_index;
  slot.err_flag_dev_addr = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(device_base + slot_index));
}

void TransferPool::ResetSlotErrFlagLocked(uint32_t slot_index) const {
  if (err_flag_host_base_ == nullptr) {
    return;
  }
  uint8_t *host_base = static_cast<uint8_t *>(err_flag_host_base_);
  host_base[slot_index] = 0U;
}

Status TransferPool::EnsureFlagBlockLocked() {
  if (flag_device_base_ != nullptr) {
    return SUCCESS;
  }
  const uint64_t total_size = static_cast<uint64_t>(pool_size_) * sizeof(uint64_t);
  // RDMA reads and stream wait/write tasks access the flags in device memory.
  const aclError alloc_ret =
      aclrtMalloc(&flag_device_base_, total_size, static_cast<aclrtMemMallocPolicy>(ACL_MEM_TYPE_HIGH_BAND_WIDTH));
  if (alloc_ret != ACL_SUCCESS) {
    flag_device_base_ = nullptr;
    HIXL_LOGE(alloc_ret, "[TransferPool] aclrtMalloc flag block failed. ret=%d device_id=%d size=%lu bytes",
              static_cast<int32_t>(alloc_ret), device_id_, total_size);
    return static_cast<Status>(alloc_ret);
  }
  const aclError memset_ret = aclrtMemset(flag_device_base_, total_size, 0, total_size);
  if (memset_ret != ACL_SUCCESS) {
    HIXL_LOGE(memset_ret, "[TransferPool] aclrtMemset flag block failed. ret=%d device_id=%d size=%lu bytes",
              static_cast<int32_t>(memset_ret), device_id_, total_size);
    FreeFlagBlockLocked();
    return static_cast<Status>(memset_ret);
  }
  for (size_t i = 0U; i < slots_.size(); ++i) {
    slots_[i].flag_dev_addr = reinterpret_cast<uint64_t>(flag_device_base_) + i * sizeof(uint64_t);
  }
  HIXL_LOGI("[TransferPool] flag block initialized. device_base=%p size=%lu bytes device_id=%d", flag_device_base_,
            total_size, device_id_);
  return SUCCESS;
}

void TransferPool::ResetSlotFlagLocked(Slot &slot) const {
  if (slot.flag_dev_addr == 0U) {
    return;
  }
  void *addr = reinterpret_cast<void *>(static_cast<uintptr_t>(slot.flag_dev_addr));
  const hixl::TemporaryRtContext guard(slot.ctx != nullptr ? slot.ctx : rts_context_);
  HIXL_CHK_ACL(aclrtMemset(addr, sizeof(uint64_t), 0, sizeof(uint64_t)),
               "[TransferPool] reset slot flag failed. device_id=%d addr=%p size=%zu bytes", device_id_, addr,
               sizeof(uint64_t));
}

void TransferPool::FreeFlagBlockLocked() {
  if (flag_device_base_ != nullptr) {
    HIXL_CHK_ACL(aclrtFree(flag_device_base_));
  }
  flag_device_base_ = nullptr;
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
  HIXL_CHK_STATUS_RET(LoadOptionalUbMemKernelsLocked(), "[TransferPool] LoadOptionalUbMemKernelsLocked failed");
  return SUCCESS;
}

Status TransferPool::LoadOptionalUbMemKernelsLocked() {
  std::vector<aclrtFuncHandle> handles;
  const Status ret = LoadDeviceKernelFunctions({kUbMemFuncRead, kUbMemFuncWrite}, kernel_bin_handle_, handles);
  if (ret != SUCCESS || handles.size() != 2U || handles[0U] == nullptr || handles[1U] == nullptr) {
    HIXL_LOGW("[TransferPool] UbMem AICPU kernels are unavailable, device_id=%d", device_id_);
    device_func_handles_.ubmem_batch_read = nullptr;
    device_func_handles_.ubmem_batch_write = nullptr;
    return SUCCESS;
  }
  device_func_handles_.ubmem_batch_read = handles[0U];
  device_func_handles_.ubmem_batch_write = handles[1U];
  return SUCCESS;
}

Status TransferPool::LaunchSyncContextKernelLocked(std::vector<HixlTransferContextSyncEntry> &entries,
                                                   std::vector<uint32_t> &states, bool readback_entries) const {
  HIXL_CHECK_NOTNULL(device_func_handles_.sync_transfer_context);
  HIXL_CHECK_NOTNULL(rts_context_, "[TransferPool] rts_context_ is null when launching sync context kernel");
  HIXL_CHK_BOOL_RET_STATUS(!entries.empty(), PARAM_INVALID, "[TransferPool] sync context entries is empty");
  const hixl::TemporaryRtContext rts_guard(rts_context_);
  const size_t entry_bytes = entries.size() * sizeof(HixlTransferContextSyncEntry);
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
  HixlTransferContextSyncParam param{};
  param.entry_list_addr = PtrToValue(dev_entries);
  param.state_list_addr = PtrToValue(dev_states);
  param.entry_num = static_cast<uint32_t>(entries.size());
  param.version = kHixlSyncParamVersion;
  param.host_exception_enabled = host_exception_enabled_;
  aclrtArgsHandle args_handle = nullptr;
  HIXL_CHK_ACL_RET(aclrtKernelArgsInit(device_func_handles_.sync_transfer_context, &args_handle),
                   "[TransferPool] aclrtKernelArgsInit HixlSyncTransferContext failed");
  aclrtParamHandle para_handle = nullptr;
  HIXL_CHK_ACL_RET(aclrtKernelArgsAppend(args_handle, &param, sizeof(HixlTransferContextSyncParam), &para_handle),
                   "[TransferPool] aclrtKernelArgsAppend HixlSyncTransferContext failed");
  HIXL_CHK_ACL_RET(aclrtKernelArgsFinalize(args_handle), "[TransferPool] aclrtKernelArgsFinalize failed");
  aclrtStream stream = nullptr;
  HIXL_CHK_ACL_RET(aclrtCtxGetCurrentDefaultStream(&stream),
                   "[TransferPool] aclrtCtxGetCurrentDefaultStream for HixlSyncTransferContext failed");
  constexpr uint32_t kBlockDim = 1U;
  HIXL_LOGD("[TransferPool] Launch HixlSyncTransferContext start. entries=%zu stream=%p device_id=%d", entries.size(),
            stream, device_id_);
  HIXL_CHK_ACL_RET(aclrtLaunchKernelWithConfig(device_func_handles_.sync_transfer_context, kBlockDim, stream, nullptr,
                                               args_handle, nullptr),
                   "[TransferPool] aclrtLaunchKernelWithConfig HixlSyncTransferContext failed");
  HIXL_CHK_ACL_RET(aclrtSynchronizeStreamWithTimeout(stream, static_cast<int32_t>(kSyncContextKernelTimeoutMs)),
                   "[TransferPool] aclrtSynchronizeStreamWithTimeout HixlSyncTransferContext failed");
  HIXL_CHK_ACL_RET(aclrtMemcpy(states.data(), state_bytes, dev_states, state_bytes, ACL_MEMCPY_DEVICE_TO_HOST),
                   "[TransferPool] aclrtMemcpy sync context states D2H failed");
  if (readback_entries) {
    HIXL_CHK_ACL_RET(aclrtMemcpy(entries.data(), entry_bytes, dev_entries, entry_bytes, ACL_MEMCPY_DEVICE_TO_HOST),
                     "[TransferPool] aclrtMemcpy sync context entries D2H failed");
  }
  HIXL_LOGD("[TransferPool] Launch HixlSyncTransferContext end. entries=%zu device_id=%d", entries.size(), device_id_);
  return SUCCESS;
}

Status TransferPool::SyncContextsLocked(const std::vector<HixlTransferContextSyncEntry> &entries, uint32_t op,
                                        uint32_t expect_state, uint64_t *out_dispatched) const {
  if (entries.empty()) {
    return SUCCESS;
  }
  HIXL_CHK_BOOL_RET_STATUS(device_func_handles_.sync_transfer_context != nullptr, FAILED,
                           "[TransferPool] sync context func is null");
  std::vector<HixlTransferContextSyncEntry> pending = entries;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kSyncContextRetryTimeoutMs);
  std::vector<uint32_t> pending_states;
  while (!pending.empty()) {
    std::vector<HixlTransferContextSyncEntry> retry_entries;
    std::vector<uint32_t> retry_states;
    HIXL_CHK_STATUS_RET(
        RunSyncContextOnceLocked(pending, op, expect_state, retry_entries, retry_states, out_dispatched),
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

Status TransferPool::RunSyncContextOnceLocked(std::vector<HixlTransferContextSyncEntry> &pending, uint32_t op,
                                              uint32_t expect_state,
                                              std::vector<HixlTransferContextSyncEntry> &retry_entries,
                                              std::vector<uint32_t> &retry_states, uint64_t *out_dispatched) const {
  std::vector<uint32_t> states;
  HIXL_CHK_STATUS_RET(LaunchSyncContextKernelLocked(pending, states, out_dispatched != nullptr),
                      "[TransferPool] launch sync context kernel failed");
  if (out_dispatched != nullptr && !states.empty() && states[0] == expect_state) {
    *out_dispatched = pending[0].dispatched_tasks;
  }
  retry_entries.reserve(pending.size());
  retry_states.reserve(pending.size());
  return CollectRetrySyncEntries(pending, states, op, expect_state, retry_entries, retry_states);
}

Status TransferPool::CollectRetrySyncEntries(const std::vector<HixlTransferContextSyncEntry> &entries,
                                             const std::vector<uint32_t> &states, uint32_t op, uint32_t expect_state,
                                             std::vector<HixlTransferContextSyncEntry> &retry_entries,
                                             std::vector<uint32_t> &retry_states) const {
  HIXL_CHK_BOOL_RET_STATUS(entries.size() == states.size(), FAILED,
                           "[TransferPool] sync context state size mismatch, entries=%zu states=%zu", entries.size(),
                           states.size());
  for (size_t i = 0U; i < entries.size(); ++i) {
    const uint32_t state = states[i];
    if (state == expect_state) {
      continue;
    }
    HIXL_CHK_BOOL_RET_STATUS(state == TRANSFER_THREAD_STATE_DELETING, FAILED,
                             "[TransferPool] unexpected transfer context state:%u, thread:%lu, op:%u, expect:%u", state,
                             static_cast<uint64_t>(entries[i].thread), op, expect_state);
    HixlTransferContextSyncEntry entry{};
    entry.thread = entries[i].thread;
    entry.op = op;
    entry.notify_id = entries[i].notify_id;
    entry.err_flag_dev_va = entries[i].err_flag_dev_va;
    retry_entries.push_back(entry);
    retry_states.push_back(state);
  }
  return SUCCESS;
}

Status TransferPool::HandleSyncContextTimeout(const std::vector<HixlTransferContextSyncEntry> &pending,
                                              const std::vector<uint32_t> &states, uint32_t op) const {
  HIXL_CHK_BOOL_RET_STATUS(op == TRANSFER_CONTEXT_OP_DELETE, TIMEOUT,
                           "[TransferPool] sync transfer context timeout, pending:%zu, op:%u, device_id:%d",
                           pending.size(), op, device_id_);
  for (size_t i = 0U; i < pending.size(); ++i) {
    const uint32_t state = (i < states.size()) ? states[i] : TRANSFER_THREAD_STATE_DELETING;
    HIXL_EVENT(
        "[TransferPool] delete transfer context timeout after %u ms, force cleanup. thread=%lu state=%u device_id=%d",
        kSyncContextRetryTimeoutMs, static_cast<uint64_t>(pending[i].thread), state, device_id_);
  }
  return SUCCESS;
}

Status TransferPool::AddTransferContextsLocked() const {
  std::vector<HixlTransferContextSyncEntry> entries = BuildSyncEntriesFromSlots(slots_, TRANSFER_CONTEXT_OP_ADD);
  return SyncContextsLocked(entries, TRANSFER_CONTEXT_OP_ADD, TRANSFER_THREAD_STATE_INITIALIZED);
}

Status TransferPool::DeleteTransferContextsLocked(const std::vector<HixlTransferContextSyncEntry> &entries) const {
  return SyncContextsLocked(entries, TRANSFER_CONTEXT_OP_DELETE, TRANSFER_THREAD_STATE_DELETED);
}

Status TransferPool::SyncOneTransferContextLocked(const Slot &slot, uint32_t op, uint32_t expect_state,
                                                  uint64_t *out_dispatched) const {
  if (slot.thread == 0U) {
    return SUCCESS;
  }
  HixlTransferContextSyncEntry entry{};
  entry.thread = slot.thread;
  entry.op = op;
  entry.notify_id = slot.notify_id;
  entry.err_flag_dev_va = slot.err_flag_dev_addr;
  std::vector<HixlTransferContextSyncEntry> entries{entry};
  return SyncContextsLocked(entries, op, expect_state, out_dispatched);
}

HixlTransferContextSyncEntry TransferPool::BuildSyncEntry(const Slot &slot, uint32_t op) {
  HixlTransferContextSyncEntry entry{};
  entry.thread = slot.thread;
  entry.op = op;
  entry.notify_id = slot.notify_id;
  entry.err_flag_dev_va = slot.err_flag_dev_addr;
  return entry;
}

std::vector<HixlTransferContextSyncEntry> TransferPool::BuildSyncEntriesFromSlots(const std::vector<Slot> &slots,
                                                                                  uint32_t op) {
  std::vector<HixlTransferContextSyncEntry> entries;
  entries.reserve(slots.size());
  for (const auto &slot : slots) {
    if (slot.thread == 0U) {
      continue;
    }
    entries.push_back(BuildSyncEntry(slot, op));
  }
  return entries;
}

Status TransferPool::DestroySlotLocked(Slot &slot, bool sync_context) const {
  DestroyUbMemStreamLocked(slot);
  if (sync_context) {
    Status ret = SyncOneTransferContextLocked(slot, TRANSFER_CONTEXT_OP_DELETE, TRANSFER_THREAD_STATE_DELETED);
    HIXL_CHK_STATUS(ret, "[TransferPool] delete transfer context failed before slot destroy");
  }
  {
    hixl::TemporaryRtContext with_context(slot.ctx);
    if (slot.notify != nullptr) {
      HIXL_CHK_ACL(aclrtDestroyNotify(slot.notify));
      slot.notify = nullptr;
      slot.notify_id = 0U;
      slot.notify_addr = 0U;
      slot.notify_len = 0U;
    }
  }
  if (slot.thread != 0U) {
    const ThreadHandle thread = slot.thread;
    const hixl::TemporaryRtContext rts_guard(rts_context_);
    HIXL_CHK_ACL(HcommProxy::ThreadFree(&slot.thread, 1U),
                 "[TransferPool] HcommThreadFree failed in DestroySlotLocked, device_id=%d, thread=%lu", device_id_,
                 static_cast<uint64_t>(thread));
    HIXL_EVENT("[TransferPool] slot thread free success, device_id=%d, thread=%lu, scene=deinit", device_id_,
               static_cast<uint64_t>(thread));
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

aclrtFuncHandle TransferPool::GetDeviceKernelFunc(bool is_get, CommProtocol protocol) const {
  HIXL_LOGD("[TransferPool] GetDeviceKernelFunc start. device_id=%d is_get=%d protocol=%d", device_id_,
            static_cast<int>(is_get), static_cast<int>(protocol));
  std::lock_guard<std::mutex> lock(mu_);
  aclrtFuncHandle func = nullptr;
  if (IsUbMemProtocol(protocol)) {
    func = is_get ? device_func_handles_.ubmem_batch_read : device_func_handles_.ubmem_batch_write;
  } else {
    func = is_get ? device_func_handles_.batch_get : device_func_handles_.batch_put;
  }
  HIXL_LOGD("[TransferPool] GetDeviceKernelFunc success. device_id=%d is_get=%d func=%p", device_id_,
            static_cast<int>(is_get), func);
  return func;
}

}  // namespace hixl
