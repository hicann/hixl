/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm_device_channel.h"

#include <algorithm>
#include <cinttypes>

#include "acl/acl.h"
#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/hixl_utils.h"
#include "common/scope_guard.h"
#include "endpoint.h"
#include "hixl_mem_store.h"
#include "cs/global_config.h"

namespace hixl {
namespace {
constexpr uint32_t kDeviceCompleteMagic = 0x55425548U;
constexpr uint64_t kDeviceFlagDoneValue = 1ULL;
constexpr uint64_t kDeviceFlagInitValue = 0ULL;
constexpr const char *kDeviceFuncGet = "HixlBatchGet";
constexpr const char *kDeviceFuncPut = "HixlBatchPut";
constexpr uint32_t kCustomTimeoutMs = 1800;
constexpr uint32_t kMaxKernelBatchSize = 128U;
// ACL_RT_LAUNCH_KERNEL_ATTR_TIMEOUT uses seconds. Async callers observe timeout by CheckStatus.
constexpr uint16_t kNotifyDefaultWaitTimeS = 27 * 68;
}  // namespace

HcommDeviceChannel::HcommDeviceChannel(Endpoint &local_endpoint) : HcommChannel(local_endpoint) {}

HcommDeviceChannel::~HcommDeviceChannel() {
  (void)Destroy();
}

Status HcommDeviceChannel::Initialize(const ChannelContext &ctx) {
  HIXL_CHK_STATUS_RET(HcommChannel::Initialize(ctx), "[Channel] base Initialize failed");
  return InitNotifyResources();
}

Status HcommDeviceChannel::SetRemoteMemInfo(const CommMem &trans_flag) {
  HIXL_CHK_STATUS_RET(HcommChannel::SetRemoteMemInfo(trans_flag), "[Channel] base SetRemoteMemInfo failed");
  // A new import may carry a different builtin flag address, so drop the cached one and re-resolve.
  // The client clears its remote mem info before every import, which the channel cannot observe.
  device_remote_flag_inited_ = false;
  device_remote_flag_addr_ = nullptr;
  return EnsureDeviceRemoteFlagInited();
}

Status HcommDeviceChannel::Destroy() {
  AbortAllPendingDeviceHandles();
  ReleaseDeviceResources();
  return HcommChannel::Destroy();
}

Status HcommDeviceChannel::RegisterNotifyMemForAllSlots(const std::vector<TransferPool::SlotHandle> &slots) {
  notify_mem_handles_.clear();
  notify_mem_handles_.resize(slots.size());
  for (size_t i = 0U; i < slots.size(); ++i) {
    HIXL_CHK_BOOL_RET_STATUS(slots[i].notify_addr != 0U && slots[i].notify_len != 0U, FAILED,
                             "[HcommDeviceChannel] invalid notify address for slot %zu. addr=0x%llx len=%u", i,
                             static_cast<unsigned long long>(slots[i].notify_addr), slots[i].notify_len);
    CommMem mem{};
    mem.type = COMM_MEM_TYPE_DEVICE;
    mem.addr = reinterpret_cast<void *>(static_cast<uintptr_t>(slots[i].notify_addr));
    mem.size = slots[i].notify_len;
    HIXL_CHK_STATUS_RET(local_endpoint_.RegisterMem(nullptr, mem, notify_mem_handles_[i]),
                        "[HcommDeviceChannel] register notify mem failed for slot %zu", i);
  }
  return SUCCESS;
}

Status RoceDeviceChannel::InitNotifyResources() {
  auto *pool = TransferPool::GetInstance(ctx_.device_id);
  HIXL_CHECK_NOTNULL(pool);
  HIXL_CHK_STATUS_RET(pool->ResolveNotifyAddr(), "[HcommDeviceChannel] TransferPool ResolveNotifyAddr failed. devId=%d",
                      ctx_.device_id);
  return SUCCESS;
}

Status UbDeviceChannel::InitNotifyResources() {
  auto *pool = TransferPool::GetInstance(ctx_.device_id);
  HIXL_CHECK_NOTNULL(pool);
  HIXL_CHK_STATUS_RET(pool->ResolveNotifyAddr(), "[HcommDeviceChannel] TransferPool ResolveNotifyAddr failed. devId=%d",
                      ctx_.device_id);
  std::vector<TransferPool::SlotHandle> all_slots;
  HIXL_CHK_STATUS_RET(pool->GetAllSlots(all_slots), "[HcommDeviceChannel] TransferPool GetAllSlots failed. devId=%d",
                      ctx_.device_id);
  return RegisterNotifyMemForAllSlots(all_slots);
}

Status HcommDeviceChannel::EnsureDeviceRemoteFlagInited() {
  if (device_remote_flag_inited_) {
    return SUCCESS;
  }
  // The client passes the builtin flag registered by the peer. A missing flag stays uninitialized so
  // PrepareDeviceRemoteFlagAndKernel reports it at transfer time.
  const CommMem &mem = remote_trans_flag_;
  if (mem.addr == nullptr) {
    HIXL_LOGD("[HcommDeviceChannel] builtin remote_flag addr is null, skip initialization");
    return SUCCESS;
  }

  if (mem.size < static_cast<uint64_t>(sizeof(uint64_t))) {
    HIXL_LOGD("[HcommDeviceChannel] builtin remote_flag size too small. size=%" PRIu64 ", skip initialization",
              mem.size);
    return SUCCESS;
  }

  device_remote_flag_addr_ = mem.addr;
  device_remote_flag_inited_ = true;

  HIXL_LOGI("[HcommDeviceChannel] builtin remote_flag ready. addr=%p u64=%p size=%" PRIu64, mem.addr,
            device_remote_flag_addr_, mem.size);
  return SUCCESS;
}

Status HcommDeviceChannel::ReleaseDevCompleteHandle(DeviceCompleteHandle *handle) {
  if (handle == nullptr) {
    return SUCCESS;
  }
  HIXL_LOGI("[HcommDeviceChannel] ReleaseDevCompleteHandle start");
  if (handle->magic != kDeviceCompleteMagic) {
    HIXL_LOGE(PARAM_INVALID, "[HcommDeviceChannel] ReleaseDevCompleteHandle bad magic=0x%X", handle->magic);
    return PARAM_INVALID;
  }
  (void)pending_device_handles_.erase(handle);

  // Free independent host_flag (allocated for async transfers)
  if (handle->host_flag != nullptr) {
    HIXL_CHK_ACL(aclrtFreeHost(handle->host_flag));
    handle->host_flag = nullptr;
  }

  // Free device op desc buffer
  if (handle->dev_op_desc_buf != nullptr) {
    HIXL_CHK_ACL(aclrtFree(handle->dev_op_desc_buf));
    handle->dev_op_desc_buf = nullptr;
  }

  // Release shared slot reference
  std::shared_ptr<TransferPool::SlotHandle> slot_ref = std::move(handle->shared_slot);
  if (slot_ref != nullptr) {
    ReleaseSharedSlotRef(slot_ref);
  }

  handle->magic = 0U;
  delete handle;
  HIXL_LOGI("[HcommDeviceChannel] ReleaseDevCompleteHandle end");
  return SUCCESS;
}

Status HcommDeviceChannel::AcquireSharedSlot(std::shared_ptr<TransferPool::SlotHandle> &slot_out) {
  // If active slot exists (pending transfer), reuse it
  if (active_slot_ != nullptr) {
    const long ref_before = active_slot_.use_count();
    slot_out = active_slot_;  // Share existing slot (increases ref_count)
    HIXL_LOGI("[HcommDeviceChannel] Reusing active slot. slot_index=%u ref_before=%ld ref_after=%ld",
              active_slot_->slot_index, ref_before, active_slot_.use_count());
    return SUCCESS;
  }

  // No active slot - acquire new from pool
  TransferPool::SlotHandle new_slot{};
  auto *pool = TransferPool::GetInstance(ctx_.device_id);
  HIXL_CHECK_NOTNULL(pool);
  HIXL_CHK_STATUS_RET(pool->Acquire(&new_slot), "[HcommDeviceChannel] Acquire slot from pool failed");

  active_slot_ = std::make_shared<TransferPool::SlotHandle>(new_slot);
  slot_out = active_slot_;
  HIXL_LOGI("[HcommDeviceChannel] Acquired new slot. slot_index=%u ref_count=%ld", new_slot.slot_index,
            active_slot_.use_count());
  return SUCCESS;
}

void HcommDeviceChannel::ReleaseSharedSlotRef(std::shared_ptr<TransferPool::SlotHandle> &slot_ref) {
  if (slot_ref == nullptr) {
    return;
  }
  if (active_slot_ == nullptr || active_slot_ != slot_ref) {
    slot_ref.reset();
    return;
  }

  const uint32_t slot_index = active_slot_->slot_index;
  const long ref_before = active_slot_.use_count();
  slot_ref.reset();
  HIXL_LOGI("[HcommDeviceChannel] ReleaseSharedSlotRef. slot_index=%u ref_before=%ld ref_after=%ld", slot_index,
            ref_before, active_slot_.use_count());

  if (active_slot_.use_count() == 1) {
    // Reset err flag only when the shared slot has no pending transfer references.
    if (active_slot_->err_flag_host_addr != nullptr) {
      *(active_slot_->err_flag_host_addr) = 0U;
    }
    auto *pool = TransferPool::GetInstance(active_slot_->device_id);
    if (pool != nullptr) {
      if (transfer_failure_latched_ || abort_active_slot_) {
        pool->Abort(*active_slot_);
        HIXL_LOGI("[HcommDeviceChannel] Aborted slot on last ref. slot_index=%u", slot_index);
      } else {
        pool->Release(*active_slot_);
        HIXL_LOGI("[HcommDeviceChannel] Released slot to pool. slot_index=%u", slot_index);
      }
    }
    abort_active_slot_ = false;
    active_slot_.reset();
  }
}

void HcommDeviceChannel::CleanupActiveSlot() {
  if (active_slot_ != nullptr) {
    auto *abort_pool = TransferPool::GetInstance(active_slot_->device_id);
    if (abort_pool != nullptr) {
      abort_pool->Abort(*active_slot_);
    }
    HIXL_LOGI("[HcommDeviceChannel] Aborted active slot. slot_index=%u", active_slot_->slot_index);
    active_slot_.reset();
  }
  abort_active_slot_ = false;
}

Status HcommDeviceChannel::ValidateDeviceInputs(uint32_t list_num, const HixlOneSideOpDesc *desc_list) const {
  HIXL_CHK_BOOL_RET_STATUS(list_num > 0U, PARAM_INVALID, "[Channel] list_num must be > 0");
  HIXL_CHECK_NOTNULL(desc_list);
  return SUCCESS;
}

Status HcommDeviceChannel::PrepareDeviceRemoteFlagAndKernel(void *&remote_flag) const {
  HIXL_LOGI("[HcommDeviceChannel] PrepareDeviceRemoteFlagAndKernel start");
  remote_flag = device_remote_flag_addr_;
  HIXL_CHECK_NOTNULL(remote_flag, "[HcommDeviceChannel] remote_flag is nullptr");
  HIXL_LOGI("[HcommDeviceChannel] PrepareDeviceRemoteFlagAndKernel end, remote_flag=%p", remote_flag);
  return SUCCESS;
}

Status HcommDeviceChannel::AllocateHostFlag(void *&host_flag) const {
  host_flag = nullptr;
  HIXL_CHK_ACL_RET(aclrtMallocHost(&host_flag, sizeof(uint64_t)),
                   "[HcommDeviceChannel] aclrtMallocHost host_flag failed");
  *(static_cast<uint64_t *>(host_flag)) = kDeviceFlagInitValue;
  return SUCCESS;
}

Status HcommDeviceChannel::LaunchDeviceChunkedKernels(bool is_get, DeviceCompleteHandle &handle,
                                                      uint32_t list_num) const {
  const uint32_t batch_size = ctx_.configurable_transfer_supported ? ctx_.global_config->MaxTransferCountPerBatch()
                                                                   : kDefaultMaxTransferCountPerBatch;
  // Device transfers have two batching boundaries: each kernel accepts at most 128 descriptors, while each logical
  // transfer batch accepts at most batch_size descriptors. A kernel never crosses a logical batch boundary, and the
  // last kernel in every logical batch records a Notify that the host waits for before submitting the next batch.
  uint32_t chunk_offset = 0U;
  uint32_t batch_remaining = batch_size;
  uint32_t chunk_idx = 0U;
  while (chunk_offset < list_num) {
    uint32_t chunk_list_num = std::min({kMaxKernelBatchSize, batch_remaining, list_num - chunk_offset});
    batch_remaining -= chunk_list_num;
    const bool need_notify_wait = batch_remaining == 0U || chunk_offset + chunk_list_num == list_num;
    HixlOneSideOpParam param{};
    HIXL_CHK_STATUS_RET(BuildDeviceChunkParam(handle, chunk_offset, chunk_list_num, need_notify_wait, param),
                        "BuildDeviceChunkParam failed for chunk %u", chunk_idx);
    HIXL_CHK_STATUS_RET(LaunchDeviceKernel(is_get, handle, param, need_notify_wait),
                        "LaunchDeviceKernel failed for chunk %u", chunk_idx);
    chunk_offset += chunk_list_num;
    ++chunk_idx;
    if (batch_remaining == 0U) {
      batch_remaining = batch_size;
    }
  }
  return SUCCESS;
}

Status HcommDeviceChannel::AllocateDeviceDescBuf(DeviceCompleteHandle &handle, uint32_t total_list_num,
                                                 const HixlOneSideOpDesc *desc_list) const {
  size_t desc_buf_size = total_list_num * sizeof(HixlOneSideOpDesc);
  HIXL_CHK_ACL_RET(aclrtMalloc(&handle.dev_op_desc_buf, desc_buf_size, ACL_MEM_MALLOC_HUGE_ONLY),
                   "[HcommDeviceChannel] aclrtMalloc op_desc_buf failed");
  HIXL_CHK_ACL_RET(
      aclrtMemcpy(handle.dev_op_desc_buf, desc_buf_size, desc_list, desc_buf_size, ACL_MEMCPY_HOST_TO_DEVICE),
      "[HcommDeviceChannel] aclrtMemcpy op_desc_buf failed");
  return SUCCESS;
}

Status HcommDeviceChannel::BuildDeviceChunkParam(DeviceCompleteHandle &handle, uint32_t chunk_offset,
                                                 uint32_t chunk_list_num, bool need_notify_wait,
                                                 HixlOneSideOpParam &param) const {
  param.thread = handle.shared_slot->thread;
  param.channel = static_cast<uint64_t>(GetHandle());
  param.list_num = chunk_list_num;
  auto *chunk_base = static_cast<uint8_t *>(handle.dev_op_desc_buf) + chunk_offset * sizeof(HixlOneSideOpDesc);
  param.op_desc_list_addr = PtrToValue(chunk_base);
  if (need_notify_wait) {
    void *remote_flag = nullptr;
    HIXL_CHK_STATUS_RET(PrepareDeviceRemoteFlagAndKernel(remote_flag), "PrepareDeviceRemoteFlagAndKernel failed");
    param.remote_flag_addr = PtrToValue(remote_flag);
    param.local_flag_addr = handle.shared_slot->notify_addr;
    param.flag_size = handle.shared_slot->notify_len;
    param.notify_id = handle.shared_slot->notify_id;
  } else {
    param.remote_flag_addr = 0;
    param.local_flag_addr = 0;
    param.flag_size = 0;
    param.notify_id = 0;
  }
  if (UseNotifyRecord()) {
    param.use_notify_record = 1;
  }
  HIXL_LOGI("[HcommDeviceChannel] protocol=%s, use_notify_record=%u",
            ProtocolToString(local_endpoint_.GetEndpoint().protocol).c_str(), param.use_notify_record);
  return SUCCESS;
}

Status HcommDeviceChannel::LaunchDeviceKernel(bool is_get, DeviceCompleteHandle &handle,
                                              const HixlOneSideOpParam &param, bool wait_notify) {
  const char *kernel_name = is_get ? kDeviceFuncGet : kDeviceFuncPut;
  HIXL_LOGI("[HcommDeviceChannel] LaunchDeviceKernel start. kernel=%s wait_notify=%d", kernel_name, wait_notify);
  HIXL_CHECK_NOTNULL(handle.shared_slot.get(), "[HcommDeviceChannel] LaunchDeviceKernel shared_slot is null");
  const ThreadHandle thread = handle.shared_slot->thread;
  auto *pool = TransferPool::GetInstance(handle.shared_slot->device_id);
  HIXL_CHECK_NOTNULL(pool, "[HcommDeviceChannel] TransferPool is null for device=%d", handle.shared_slot->device_id);
  void *func = pool->GetDeviceKernelFunc(is_get);
  HIXL_CHECK_NOTNULL(func, "[HcommDeviceChannel] func is null for %s", kernel_name);
  constexpr uint32_t block_dim = 1U;

  aclrtFuncHandle funcHandle = func;
  aclrtArgsHandle argsHandle = nullptr;
  HIXL_CHK_ACL_RET(aclrtKernelArgsInit(funcHandle, &argsHandle),
                   "[HcommDeviceChannel] aclrtKernelArgsInit failed. kernel=%s", kernel_name);
  aclrtParamHandle paraHandle;
  HIXL_CHK_ACL_RET(aclrtKernelArgsAppend(argsHandle, const_cast<HixlOneSideOpParam *>(&param),
                                         sizeof(HixlOneSideOpParam), &paraHandle),
                   "[HcommDeviceChannel] aclrtKernelArgsAppend param failed, kernel = %s", kernel_name);
  HIXL_CHK_ACL_RET(aclrtKernelArgsFinalize(argsHandle),
                   "[HcommDeviceChannel] aclrtKernelArgsFinalize failed, kernel = %s", kernel_name);

  aclrtLaunchKernelCfg cfg;
  aclrtLaunchKernelAttr attr;
  attr.id = ACL_RT_LAUNCH_KERNEL_ATTR_TIMEOUT;
  attr.value.timeout = kNotifyDefaultWaitTimeS;
  cfg.numAttrs = 1;
  cfg.attrs = &attr;

  HIXL_CHK_ACL_RET(
      aclrtLaunchKernelWithConfig(funcHandle, block_dim, handle.shared_slot->stream, &cfg, argsHandle, nullptr),
      "[HcommDeviceChannel] aclrtLaunchKernelWithConfig failed, kernel=%s, thread=%lu", kernel_name,
      static_cast<uint64_t>(thread));
  handle.shared_slot->launched_tasks += param.list_num;
  if (wait_notify) {
    HIXL_CHK_ACL_RET(aclrtWaitAndResetNotify(handle.shared_slot->notify, handle.shared_slot->stream, kCustomTimeoutMs),
                     "[HcommDeviceChannel] aclrtWaitAndResetNotify failed, kernel=%s, thread=%lu", kernel_name,
                     static_cast<uint64_t>(thread));
  }
  HIXL_LOGI("[HcommDeviceChannel] LaunchDeviceKernel end. kernel=%s", kernel_name);
  return SUCCESS;
}

Status HcommDeviceChannel::LaunchAndCopyDoneFlag(bool is_get, DeviceCompleteHandle &handle, uint32_t list_num) {
  hixl::TemporaryRtContext ctx_guard(handle.shared_slot->ctx);
  // The caller's handle guard is the only return. A failed launch or flag copy can leave work on the
  // stream, so mark the slot before that guard releases it.
  const Status launch_ret = LaunchDeviceChunkedKernels(is_get, handle, list_num);
  if (launch_ret != SUCCESS) {
    abort_active_slot_ = true;
  }
  HIXL_CHK_STATUS_RET(launch_ret,
                      "[HcommDeviceChannel] LaunchDeviceChunkedKernels failed, is_get=%d, list_num=%u, slot=%u, "
                      "thread=%lu",
                      static_cast<int32_t>(is_get), list_num, handle.shared_slot->slot_index,
                      static_cast<uint64_t>(handle.shared_slot->thread));
  const aclError copy_ret = aclrtMemcpyAsync(handle.host_flag, sizeof(uint64_t), handle.shared_slot->dev_const_one,
                                             sizeof(uint64_t), ACL_MEMCPY_DEVICE_TO_HOST, handle.shared_slot->stream);
  if (copy_ret != ACL_SUCCESS) {
    abort_active_slot_ = true;
  }
  HIXL_CHK_ACL_RET(copy_ret,
                   "[HcommDeviceChannel] aclrtMemcpyAsync (Flag D2H) failed, is_get=%d, list_num=%u, slot=%u, "
                   "thread=%lu",
                   static_cast<int32_t>(is_get), list_num, handle.shared_slot->slot_index,
                   static_cast<uint64_t>(handle.shared_slot->thread));
  return SUCCESS;
}

Status HcommDeviceChannel::TransferAsyncInternal(bool is_get, const HixlOneSideOpDesc *desc_list, uint32_t list_num,
                                                 void **complete_handle) {
  HIXL_CHK_STATUS_RET(ValidateDeviceInputs(list_num, desc_list), "ValidateDeviceInputs failed");

  std::shared_ptr<TransferPool::SlotHandle> slot;
  HIXL_CHK_STATUS_RET(AcquireSharedSlot(slot), "[HcommDeviceChannel] AcquireSharedSlot failed");
  HIXL_DISMISSABLE_GUARD(slot_guard, ([this, &slot]() { ReleaseSharedSlotRef(slot); }));

  HIXL_CHECK_NOTNULL(slot->notify, "[HcommDeviceChannel] slot->notify is null");

  void *host_flag = nullptr;
  HIXL_CHK_STATUS_RET(AllocateHostFlag(host_flag), "[HcommDeviceChannel] AllocateHostFlag failed");
  HIXL_DISMISSABLE_GUARD(flag_guard, [&host_flag]() {
    if (host_flag != nullptr) {
      aclrtFreeHost(host_flag);
    }
  });

  auto *handle = new (std::nothrow) DeviceCompleteHandle();
  HIXL_CHK_BOOL_RET_STATUS(handle != nullptr, FAILED, "[HcommDeviceChannel] Allocate DeviceCompleteHandle failed");
  HIXL_DISMISSABLE_GUARD(handle_guard, ([this, handle]() { ReleaseDevCompleteHandle(handle); }));
  handle->magic = kDeviceCompleteMagic;
  handle->shared_slot = std::move(slot);
  HIXL_DISMISS_GUARD(slot_guard);
  handle->host_flag = host_flag;
  handle->dev_op_desc_buf = nullptr;
  HIXL_DISMISS_GUARD(flag_guard);

  HIXL_CHK_STATUS_RET(AllocateDeviceDescBuf(*handle, list_num, desc_list), "AllocateDeviceDescBuf failed");

  HIXL_LOGI("[HcommDeviceChannel] TransferAsyncInternal. is_get=%d list_num=%u slot=%u magic=%u",
            static_cast<int32_t>(is_get), list_num, handle->shared_slot->slot_index, handle->magic);

  HIXL_CHK_STATUS_RET(LaunchAndCopyDoneFlag(is_get, *handle, list_num),
                      "[HcommDeviceChannel] LaunchAndCopyDoneFlag failed");

  *complete_handle = static_cast<void *>(handle);
  HIXL_DISMISS_GUARD(handle_guard);
  HIXL_LOGI("[HcommDeviceChannel] BatchTransfer submitted. is_get=%d list_num=%u slot=%u", static_cast<int32_t>(is_get),
            list_num, handle->shared_slot->slot_index);
  pending_device_handles_.insert(handle);
  return SUCCESS;
}

Status HcommDeviceChannel::TransferSyncInternal(bool is_get, const HixlOneSideOpDesc *desc_list, uint32_t list_num,
                                                uint32_t timeout_ms) {
  HIXL_CHK_STATUS_RET(ValidateDeviceInputs(list_num, desc_list), "ValidateDeviceInputs failed");

  std::shared_ptr<TransferPool::SlotHandle> slot;
  HIXL_CHK_STATUS_RET(AcquireSharedSlot(slot), "[HcommDeviceChannel] AcquireSharedSlot failed");
  HIXL_DISMISSABLE_GUARD(slot_guard, ([this, &slot]() { ReleaseSharedSlotRef(slot); }));
  HIXL_CHECK_NOTNULL(slot->notify, "[HcommDeviceChannel] slot->notify is null");

  auto *handle = new (std::nothrow) DeviceCompleteHandle();
  HIXL_CHK_BOOL_RET_STATUS(handle != nullptr, FAILED, "[HcommDeviceChannel] Allocate DeviceCompleteHandle failed");
  HIXL_MAKE_GUARD(handle_guard, ([this, handle]() { (void)ReleaseDevCompleteHandle(handle); }));
  handle->magic = kDeviceCompleteMagic;
  handle->shared_slot = std::move(slot);
  HIXL_DISMISS_GUARD(slot_guard);
  handle->host_flag = nullptr;
  handle->dev_op_desc_buf = nullptr;

  HIXL_CHK_STATUS_RET(AllocateDeviceDescBuf(*handle, list_num, desc_list), "AllocateDeviceDescBuf failed");

  HIXL_LOGI("[HcommDeviceChannel] TransferSyncInternal. is_get=%d list_num=%u slot=%u", static_cast<int32_t>(is_get),
            list_num, handle->shared_slot->slot_index);

  {
    hixl::TemporaryRtContext ctx_guard(handle->shared_slot->ctx);
    // The handle guard below is the only return. Mark the slot so that guard Aborts it once.
    const Status launch_ret = LaunchDeviceChunkedKernels(is_get, *handle, list_num);
    const ThreadHandle thread = handle->shared_slot->thread;
    const uint32_t slot_index = handle->shared_slot->slot_index;
    if (launch_ret != SUCCESS) {
      abort_active_slot_ = true;
    }
    HIXL_CHK_STATUS_RET(launch_ret,
                        "[HcommDeviceChannel] LaunchDeviceChunkedKernels failed, is_get=%d, list_num=%u, slot=%u, "
                        "thread=%lu",
                        static_cast<int32_t>(is_get), list_num, slot_index, static_cast<uint64_t>(thread));
    const aclError sync_ret = aclrtSynchronizeStreamWithTimeout(handle->shared_slot->stream, timeout_ms);
    if (sync_ret != ACL_SUCCESS) {
      abort_active_slot_ = true;
    }
    HIXL_CHK_ACL_RET(sync_ret,
                     "[HcommDeviceChannel] aclrtSynchronizeStreamWithTimeout failed, kernel=%s, list_num=%u, slot=%u, "
                     "thread=%lu",
                     is_get ? kDeviceFuncGet : kDeviceFuncPut, list_num, slot_index, static_cast<uint64_t>(thread));
  }

  HIXL_LOGI("[HcommDeviceChannel] TransferSyncInternal done. is_get=%d list_num=%u", static_cast<int32_t>(is_get),
            list_num);
  return SUCCESS;
}

Status HcommDeviceChannel::DoBatchTransferSync(bool is_get, const HixlOneSideOpDesc *desc_list, uint32_t list_num,
                                               uint32_t timeout_ms) {
  if (local_endpoint_.NeedHostVaMapping()) {
    std::vector<HixlOneSideOpDesc> converted(desc_list, desc_list + list_num);
    HIXL_CHK_STATUS_RET(ConvertHostMappedDescs(list_num, converted.data()),
                        "[HcommDeviceChannel] convert host mapped descs failed.");
    return TransferSyncInternal(is_get, converted.data(), list_num, timeout_ms);
  }
  return TransferSyncInternal(is_get, desc_list, list_num, timeout_ms);
}

Status HcommDeviceChannel::DoBatchTransferAsync(bool is_get, const HixlOneSideOpDesc *desc_list, uint32_t list_num,
                                                void **complete_handle) {
  if (local_endpoint_.NeedHostVaMapping()) {
    std::vector<HixlOneSideOpDesc> converted(desc_list, desc_list + list_num);
    HIXL_CHK_STATUS_RET(ConvertHostMappedDescs(list_num, converted.data()),
                        "[HcommDeviceChannel] convert host mapped descs failed.");
    return TransferAsyncInternal(is_get, converted.data(), list_num, complete_handle);
  }
  return TransferAsyncInternal(is_get, desc_list, list_num, complete_handle);
}

Status HcommDeviceChannel::ConvertHostMappedDescs(uint32_t list_num, HixlOneSideOpDesc *desc_list) const {
  return ctx_.mem_store->BatchConvertHostAddr(list_num, desc_list);
}

Status HcommDeviceChannel::CheckStatusDevice(DeviceCompleteHandle &query_handle, HixlCompleteStatus &status) {
  HIXL_CHK_BOOL_RET_STATUS(query_handle.magic == kDeviceCompleteMagic, PARAM_INVALID,
                           "[HcommDeviceChannel] CheckStatusDevice bad magic=0x%X", query_handle.magic);
  HIXL_CHECK_NOTNULL(query_handle.shared_slot.get(), "[HcommDeviceChannel] CheckStatusDevice shared_slot is null");
  HIXL_CHECK_NOTNULL(query_handle.host_flag, "[HcommDeviceChannel] CheckStatusDevice host_flag is null");

  volatile uint64_t *flag_ptr = static_cast<uint64_t *>(query_handle.host_flag);
  const uint64_t flag_val = *flag_ptr;
  HIXL_LOGI("[HcommDeviceChannel] CheckStatusDevice flag_val=%lu", flag_val);
  if (flag_val == kDeviceFlagDoneValue) {
    status = HixlCompleteStatus::HIXL_COMPLETE_STATUS_COMPLETED;
    HIXL_LOGI("[HcommDeviceChannel] Batch completed. slot=%u", query_handle.shared_slot->slot_index);
    return ReleaseDevCompleteHandle(&query_handle);
  }

  if (transfer_failure_latched_) {
    status = HixlCompleteStatus::HIXL_COMPLETE_STATUS_FAILED;
    HIXL_LOGE(transfer_failure_status_,
              "[HcommDeviceChannel] CheckStatusDevice failed due to latched transfer failure. status=%u slot=%u",
              static_cast<uint32_t>(transfer_failure_status_), query_handle.shared_slot->slot_index);
    return ReleaseDevCompleteHandle(&query_handle);
  }

  if (query_handle.shared_slot->err_flag_host_addr != nullptr && *query_handle.shared_slot->err_flag_host_addr != 0U) {
    LatchTransferFailure(FAILED);
    status = HixlCompleteStatus::HIXL_COMPLETE_STATUS_FAILED;
    HIXL_LOGE(FAILED, "[HcommDeviceChannel] err_flag set. slot=%u thread=%" PRIu64 " channel=%" PRIu64,
              query_handle.shared_slot->slot_index, static_cast<uint64_t>(query_handle.shared_slot->thread),
              static_cast<uint64_t>(GetHandle()));
    return ReleaseDevCompleteHandle(&query_handle);
  }

  status = HixlCompleteStatus::HIXL_COMPLETE_STATUS_WAITING;
  return SUCCESS;
}

Status HcommDeviceChannel::QueryCompleteStatus(void *complete_handle, HixlCompleteStatus *status) {
  HIXL_CHECK_NOTNULL(complete_handle);
  HIXL_CHECK_NOTNULL(status);
  auto *handle = static_cast<DeviceCompleteHandle *>(complete_handle);
  return CheckStatusDevice(*handle, *status);
}

void HcommDeviceChannel::AbortAllPendingDeviceHandles() {
  if (pending_device_handles_.empty()) {
    return;
  }
  std::vector<DeviceCompleteHandle *> pending(pending_device_handles_.begin(), pending_device_handles_.end());
  pending_device_handles_.clear();
  // These handles share active_slot_. The last ReleaseDevCompleteHandle aborts that slot once.
  abort_active_slot_ = true;
  for (DeviceCompleteHandle *h : pending) {
    (void)ReleaseDevCompleteHandle(h);
  }
}

void HcommDeviceChannel::ReleaseDeviceResources() {
  for (size_t i = 0U; i < notify_mem_handles_.size(); ++i) {
    if (notify_mem_handles_[i] != nullptr) {
      local_endpoint_.DeregisterMem(notify_mem_handles_[i]);
      notify_mem_handles_[i] = nullptr;
    }
  }
  notify_mem_handles_.clear();
  CleanupActiveSlot();
}

}  // namespace hixl
