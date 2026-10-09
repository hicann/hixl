/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "cs/ubmem/ubmem_engine.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <thread>

#include "acl/acl.h"
#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/llm_utils.h"
#include "common/scope_guard.h"
#include "cs/ubmem/ubmem_aicpu_param.h"

namespace hixl {
namespace {
constexpr uint32_t kUbMemCompleteMagic = 0x5542554DU;
constexpr uint64_t kUbMemFlagInitValue = 0ULL;
constexpr uint64_t kUbMemFlagDoneValue = 1ULL;
constexpr uint32_t kUbMemMaxKernelBatchSize = 128U;
constexpr uint32_t kUbMemNotifyTimeoutMs = 1800U;
constexpr uint16_t kUbMemKernelTimeoutS = 1836U;
constexpr auto kUbMemSyncQueryIntervalUs = 10U;
constexpr const char *kUbMemFuncRead = "HixlUbMemBatchRead";
constexpr const char *kUbMemFuncWrite = "HixlUbMemBatchWrite";
uint32_t CalculateAicpuKernelCount(uint32_t desc_count, uint32_t batch_size) {
  if (desc_count == 0U || batch_size == 0U) {
    return 0U;
  }
  const uint32_t full_batch_count = desc_count / batch_size;
  const uint32_t tail_count = desc_count % batch_size;
  const uint32_t launches_per_full_batch =
      batch_size / kUbMemMaxKernelBatchSize + static_cast<uint32_t>(batch_size % kUbMemMaxKernelBatchSize != 0U);
  const uint32_t tail_launches =
      tail_count / kUbMemMaxKernelBatchSize + static_cast<uint32_t>(tail_count % kUbMemMaxKernelBatchSize != 0U);
  return full_batch_count * launches_per_full_batch + tail_launches;
}
}  // namespace

UbMemEngine::UbMemEngine(int32_t device_id, const GlobalConfig &global_config, Endpoint &local_endpoint)
    : device_id_(device_id), global_config_(global_config), local_endpoint_(local_endpoint) {}

UbMemEngine::~UbMemEngine() {
  Finalize();
}

bool UbMemEngine::OwnsHandle(uint32_t magic) {
  return magic == kUbMemCompleteMagic;
}

bool UbMemEngine::UseMemcpyMode() const {
  return !global_config_.UbMemory().enable_aicpu_unfold.value_or(true);
}

Status UbMemEngine::ValidateInputs(uint32_t list_num, const HixlOneSideOpDesc *desc_list) const {
  HIXL_CHK_BOOL_RET_STATUS(list_num > 0U, PARAM_INVALID, "[UbMemEngine] list_num must be > 0");
  HIXL_CHECK_NOTNULL(desc_list);
  return SUCCESS;
}

Status UbMemEngine::TranslateUbMemOpDescs(uint32_t list_num, const HixlOneSideOpDesc *src,
                                          std::vector<HixlOneSideOpDesc> &dst) const {
  return local_endpoint_.TranslateRemoteDescs(list_num, src, dst);
}

Status UbMemEngine::AcquireUbMemSlot(std::shared_ptr<TransferPool::SlotHandle> &slot_out) {
  if (active_slot_ != nullptr && active_slot_.use_count() > 0) {
    slot_out = active_slot_;
    return SUCCESS;
  }
  TransferPool::SlotHandle new_slot{};
  auto *pool = TransferPool::GetInstance(device_id_);
  HIXL_CHECK_NOTNULL(pool);
  HIXL_CHK_STATUS_RET(pool->Acquire(&new_slot), "[UbMemEngine] Acquire slot failed, device_id=%d", device_id_);
  active_slot_ = std::make_shared<TransferPool::SlotHandle>(new_slot);
  slot_out = active_slot_;
  return SUCCESS;
}

void UbMemEngine::ReleaseUbMemSlot(std::shared_ptr<TransferPool::SlotHandle> &slot_ref) {
  if (slot_ref == nullptr) {
    return;
  }
  if (active_slot_ == nullptr || active_slot_ != slot_ref) {
    slot_ref.reset();
    return;
  }
  slot_ref.reset();
  if (active_slot_.use_count() == 1U) {
    auto *pool = TransferPool::GetInstance(active_slot_->device_id);
    if (pool != nullptr) {
      if (active_slot_->err_flag_host_addr != nullptr && *active_slot_->err_flag_host_addr != 0U) {
        pool->Abort(*active_slot_);
      } else {
        pool->Release(*active_slot_);
      }
    }
    active_slot_.reset();
  }
}

Status UbMemEngine::AllocateUbMemHostFlag(void *&host_flag) const {
  host_flag = nullptr;
  HIXL_CHK_ACL_RET(aclrtMallocHost(&host_flag, sizeof(uint64_t)), "[UbMemEngine] aclrtMallocHost host_flag failed");
  *(static_cast<uint64_t *>(host_flag)) = kUbMemFlagInitValue;
  return SUCCESS;
}

Status UbMemEngine::AllocateUbMemCompleteHandle(UbMemCompleteHandle *&handle) const {
  handle = new (std::nothrow) UbMemCompleteHandle();
  HIXL_CHK_BOOL_RET_STATUS(handle != nullptr, FAILED, "[UbMemEngine] allocate complete handle failed");
  handle->magic = kUbMemCompleteMagic;
  handle->desc_count = 0U;
  handle->status_count = 0U;
  handle->host_flag = nullptr;
  handle->desc_buf = nullptr;
  handle->status_buf = nullptr;
  return SUCCESS;
}

Status UbMemEngine::AllocateUbMemStatusBuf(uint32_t status_count, void *&status_buf) const {
  status_buf = nullptr;
  if (status_count == 0U) {
    return SUCCESS;
  }
  HIXL_CHK_ACL_RET(aclrtMalloc(&status_buf, status_count * sizeof(uint32_t), ACL_MEM_MALLOC_NORMAL_ONLY),
                   "[UbMemEngine] aclrtMalloc status_buf failed, size:%zu bytes", status_count * sizeof(uint32_t));
  const std::vector<uint32_t> initial_status(status_count, 0U);
  HIXL_CHK_ACL_RET(aclrtMemcpy(status_buf, status_count * sizeof(uint32_t), initial_status.data(),
                               status_count * sizeof(uint32_t), ACL_MEMCPY_HOST_TO_DEVICE),
                   "[UbMemEngine] aclrtMemcpy initial status_buf failed, size:%zu bytes",
                   status_count * sizeof(uint32_t));
  return SUCCESS;
}

void UbMemEngine::ReleaseUbMemCompleteHandle(UbMemCompleteHandle *handle) {
  if (handle == nullptr) {
    return;
  }
  (void)pending_handles_.erase(handle);
  std::shared_ptr<TransferPool::SlotHandle> slot_ref = std::move(handle->slot);
  // Do not abort here: an err_flag failure must stay visible until the slot's last shared reference is
  // released (ReleaseUbMemSlot / client ReleaseSharedSlotRef / Finalize), so stream abort is deferred.
  if (handle->host_flag != nullptr) {
    {
      hixl::TemporaryRtContext ctx_guard(slot_ref != nullptr ? slot_ref->ctx : nullptr);
      (void)aclrtFreeHost(handle->host_flag);
    }
    handle->host_flag = nullptr;
  }
  if (handle->desc_buf != nullptr) {
    {
      hixl::TemporaryRtContext ctx_guard(slot_ref != nullptr ? slot_ref->ctx : nullptr);
      (void)aclrtFree(handle->desc_buf);
    }
    handle->desc_buf = nullptr;
  }
  if (handle->status_buf != nullptr) {
    {
      hixl::TemporaryRtContext ctx_guard(slot_ref != nullptr ? slot_ref->ctx : nullptr);
      (void)aclrtFree(handle->status_buf);
    }
    handle->status_buf = nullptr;
  }
  handle->status_count = 0U;
  handle->magic = 0U;
  delete handle;
  if (slot_ref != nullptr) {
    slot_ref.reset();
  }
}

Status UbMemEngine::SubmitAsync(bool is_get, uint32_t list_num, const HixlOneSideOpDesc *desc_list,
                                void **query_handle) {
  HIXL_CHECK_NOTNULL(query_handle);
  *query_handle = nullptr;
  HIXL_CHK_STATUS_RET(ValidateInputs(list_num, desc_list), "[UbMemEngine] ValidateInputs failed");
  if (UseMemcpyMode()) {
    return SubmitMemcpyAsync(is_get, list_num, desc_list, query_handle);
  }
  // AICPU treats zero as its 60-second async default deadline; async callers poll completion separately.
  return SubmitAicpuAsync(is_get, list_num, desc_list, query_handle, 0U);
}

Status UbMemEngine::SubmitSync(bool is_get, uint32_t list_num, const HixlOneSideOpDesc *desc_list,
                               uint32_t timeout_ms) {
  HIXL_CHK_STATUS_RET(ValidateInputs(list_num, desc_list), "[UbMemEngine] ValidateInputs failed");
  if (UseMemcpyMode()) {
    return SubmitMemcpySync(is_get, list_num, desc_list, timeout_ms);
  }
  return SubmitAicpuSync(is_get, list_num, desc_list, timeout_ms);
}

Status UbMemEngine::SubmitMemcpyAsync(bool is_get, uint32_t list_num, const HixlOneSideOpDesc *desc_list,
                                      void **query_handle) {
  std::vector<HixlOneSideOpDesc> translated;
  HIXL_CHK_STATUS_RET(TranslateUbMemOpDescs(list_num, desc_list, translated),
                      "[UbMemEngine] TranslateUbMemOpDescs failed, list_num=%u", list_num);
  std::vector<HixlOneSideOpDesc> owned_descs =
      translated.empty() ? std::vector<HixlOneSideOpDesc>(desc_list, desc_list + list_num) : std::move(translated);
  const HixlOneSideOpDesc *op_descs = owned_descs.data();
  const uint32_t op_num = static_cast<uint32_t>(owned_descs.size());

  std::shared_ptr<TransferPool::SlotHandle> slot;
  HIXL_CHK_STATUS_RET(AcquireUbMemSlot(slot), "[UbMemEngine] AcquireUbMemSlot failed");
  HIXL_DISMISSABLE_GUARD(slot_guard, ([this, &slot]() { ReleaseUbMemSlot(slot); }));
  void *host_flag = nullptr;
  HIXL_CHK_STATUS_RET(AllocateUbMemHostFlag(host_flag), "[UbMemEngine] AllocateUbMemHostFlag failed");
  HIXL_DISMISSABLE_GUARD(flag_guard, ([&host_flag]() {
                           if (host_flag != nullptr) {
                             (void)aclrtFreeHost(host_flag);
                           }
                         }));
  UbMemCompleteHandle *handle = nullptr;
  HIXL_CHK_STATUS_RET(AllocateUbMemCompleteHandle(handle), "[UbMemEngine] AllocateUbMemCompleteHandle failed");
  HIXL_DISMISSABLE_GUARD(handle_guard, ([this, handle]() { ReleaseUbMemCompleteHandle(handle); }));
  handle->slot = slot;
  handle->host_flag = host_flag;
  HIXL_DISMISS_GUARD(slot_guard);
  HIXL_DISMISS_GUARD(flag_guard);

  {
    hixl::TemporaryRtContext ctx_guard(handle->slot->ctx);
    for (uint32_t i = 0U; i < op_num; ++i) {
      void *dst = is_get ? op_descs[i].local_buf : op_descs[i].remote_buf;
      const void *src = is_get ? op_descs[i].remote_buf : op_descs[i].local_buf;
      HIXL_CHK_ACL_RET(aclrtMemcpyAsync(dst, op_descs[i].len, src, op_descs[i].len, ACL_MEMCPY_DEVICE_TO_DEVICE,
                                        handle->slot->stream),
                       "[UbMemEngine] aclrtMemcpyAsync data failed, dst=%p, src=%p, size=%lu bytes", dst, src,
                       op_descs[i].len);
    }
    HIXL_CHK_ACL_RET(aclrtMemcpyAsync(handle->host_flag, sizeof(uint64_t), handle->slot->dev_const_one,
                                      sizeof(uint64_t), ACL_MEMCPY_DEVICE_TO_HOST, handle->slot->stream),
                     "[UbMemEngine] aclrtMemcpyAsync completion flag failed, dst=%p, src=%p, size=%zu bytes",
                     handle->host_flag, handle->slot->dev_const_one, sizeof(uint64_t));
  }

  *query_handle = handle;
  HIXL_DISMISS_GUARD(handle_guard);
  pending_handles_.insert(handle);
  return SUCCESS;
}

Status UbMemEngine::SubmitMemcpySync(bool is_get, uint32_t list_num, const HixlOneSideOpDesc *desc_list,
                                     uint32_t timeout_ms) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  void *raw_handle = nullptr;
  HIXL_CHK_STATUS_RET(SubmitMemcpyAsync(is_get, list_num, desc_list, &raw_handle),
                      "[UbMemEngine] SubmitMemcpyAsync failed");
  auto *complete_handle = static_cast<UbMemCompleteHandle *>(raw_handle);
  const uint32_t slot_index = complete_handle->slot->slot_index;
  HIXL_DISMISSABLE_GUARD(handle_guard, ([this, raw_handle]() {
                           ReleaseUbMemCompleteHandle(static_cast<UbMemCompleteHandle *>(raw_handle));
                         }));
  while (true) {
    HixlCompleteStatus status = HixlCompleteStatus::HIXL_COMPLETE_STATUS_WAITING;
    HIXL_CHK_STATUS_RET(CheckStatus(raw_handle, status), "[UbMemEngine] CheckStatus failed, handle=%p", raw_handle);
    if (status == HixlCompleteStatus::HIXL_COMPLETE_STATUS_FAILED) {
      HIXL_DISMISS_GUARD(handle_guard);
      HIXL_LOGE(FAILED, "[UbMemEngine] memcpy transfer failed, is_get=%d, slot=%u, timeout_ms=%u",
                static_cast<int32_t>(is_get), slot_index, timeout_ms);
      return FAILED;
    }
    if (status == HixlCompleteStatus::HIXL_COMPLETE_STATUS_COMPLETED) {
      HIXL_DISMISS_GUARD(handle_guard);
      return SUCCESS;
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      if (complete_handle->slot->err_flag_host_addr != nullptr) {
        *complete_handle->slot->err_flag_host_addr = 1U;
      }
      HIXL_LOGE(TIMEOUT, "[UbMemEngine] memcpy transfer timeout after %u ms, slot=%u", timeout_ms, slot_index);
      return TIMEOUT;
    }
    std::this_thread::sleep_for(std::chrono::microseconds(kUbMemSyncQueryIntervalUs));
  }
}

Status UbMemEngine::SubmitAicpuAsync(bool is_get, uint32_t list_num, const HixlOneSideOpDesc *desc_list,
                                     void **query_handle, uint32_t timeout_ms) {
  std::vector<HixlOneSideOpDesc> translated;
  HIXL_CHK_STATUS_RET(TranslateUbMemOpDescs(list_num, desc_list, translated),
                      "[UbMemEngine] TranslateUbMemOpDescs failed, list_num=%u", list_num);
  const HixlOneSideOpDesc *op_descs = translated.empty() ? desc_list : translated.data();
  const uint32_t op_num = translated.empty() ? list_num : static_cast<uint32_t>(translated.size());

  std::shared_ptr<TransferPool::SlotHandle> slot;
  HIXL_CHK_STATUS_RET(AcquireUbMemSlot(slot), "[UbMemEngine] AcquireUbMemSlot failed");
  HIXL_DISMISSABLE_GUARD(slot_guard, ([this, &slot]() { ReleaseUbMemSlot(slot); }));
  void *host_flag = nullptr;
  HIXL_CHK_STATUS_RET(AllocateUbMemHostFlag(host_flag), "[UbMemEngine] AllocateUbMemHostFlag failed");
  HIXL_DISMISSABLE_GUARD(flag_guard, ([&host_flag]() {
                           if (host_flag != nullptr) {
                             (void)aclrtFreeHost(host_flag);
                           }
                         }));
  UbMemCompleteHandle *handle = nullptr;
  HIXL_CHK_STATUS_RET(AllocateUbMemCompleteHandle(handle), "[UbMemEngine] AllocateUbMemCompleteHandle failed");
  HIXL_DISMISSABLE_GUARD(handle_guard, ([this, handle]() { ReleaseUbMemCompleteHandle(handle); }));
  handle->slot = slot;
  handle->host_flag = host_flag;
  HIXL_DISMISS_GUARD(slot_guard);
  HIXL_DISMISS_GUARD(flag_guard);

  {
    hixl::TemporaryRtContext ctx_guard(handle->slot->ctx);
    HIXL_CHK_STATUS_RET(PrepareAicpuDescBuf(*handle, is_get, op_num, op_descs),
                        "[UbMemEngine] PrepareAicpuDescBuf failed, op_num=%u", op_num);
    const uint32_t kernel_count =
        CalculateAicpuKernelCount(handle->desc_count, global_config_.MaxTransferCountPerBatch());
    HIXL_CHK_STATUS_RET(AllocateUbMemStatusBuf(kernel_count, handle->status_buf),
                        "[UbMemEngine] AllocateUbMemStatusBuf failed, kernel_count=%u", kernel_count);
    handle->status_count = kernel_count;
    HIXL_CHK_STATUS_RET(LaunchAicpuChunks(is_get, *handle, timeout_ms),
                        "[UbMemEngine] LaunchAicpuChunks failed, op_num=%u, timeout_ms=%u", op_num, timeout_ms);
    HIXL_CHK_ACL_RET(aclrtMemcpyAsync(handle->host_flag, sizeof(uint64_t), handle->slot->dev_const_one,
                                      sizeof(uint64_t), ACL_MEMCPY_DEVICE_TO_HOST, handle->slot->stream),
                     "[UbMemEngine] aclrtMemcpyAsync completion flag failed, dst=%p, src=%p, size=%zu bytes",
                     handle->host_flag, handle->slot->dev_const_one, sizeof(uint64_t));
  }

  *query_handle = handle;
  HIXL_DISMISS_GUARD(handle_guard);
  pending_handles_.insert(handle);
  return SUCCESS;
}

Status UbMemEngine::SubmitAicpuSync(bool is_get, uint32_t list_num, const HixlOneSideOpDesc *desc_list,
                                    uint32_t timeout_ms) {
  void *raw_handle = nullptr;
  HIXL_CHK_STATUS_RET(SubmitAicpuAsync(is_get, list_num, desc_list, &raw_handle, timeout_ms),
                      "[UbMemEngine] SubmitAicpuAsync failed");
  auto *handle = static_cast<UbMemCompleteHandle *>(raw_handle);
  HIXL_DISMISSABLE_GUARD(handle_guard, ([this, handle]() { ReleaseUbMemCompleteHandle(handle); }));
  const aclError sync_ret = aclrtSynchronizeStreamWithTimeout(handle->slot->stream, timeout_ms);
  if (sync_ret != ACL_SUCCESS) {
    if (handle->slot->err_flag_host_addr != nullptr) {
      *handle->slot->err_flag_host_addr = 1U;
    }
    HIXL_CHK_ACL_RET(sync_ret, "[UbMemEngine] aclrtSynchronizeStreamWithTimeout failed, timeout_ms=%u", timeout_ms);
  }
  HIXL_CHK_STATUS_RET(CheckAicpuStatus(*handle), "[UbMemEngine] CheckAicpuStatus failed, timeout_ms=%u", timeout_ms);
  HIXL_DISMISS_GUARD(handle_guard);
  return SUCCESS;
}

Status UbMemEngine::PrepareAicpuDescBuf(UbMemCompleteHandle &handle, bool is_get, uint32_t list_num,
                                        const HixlOneSideOpDesc *desc_list) const {
  std::vector<UbMemAicpuTransferDesc> descs;
  HIXL_CHK_STATUS_RET(BuildUbMemTransferDescs(is_get, desc_list, list_num, descs),
                      "[UbMemEngine] BuildUbMemTransferDescs failed, list_num=%u", list_num);
  HIXL_CHK_BOOL_RET_STATUS(descs.size() <= std::numeric_limits<uint32_t>::max(), PARAM_INVALID,
                           "[UbMemEngine] descriptor count exceeds uint32 max, desc_count=%zu", descs.size());
  const size_t desc_buf_size = descs.size() * sizeof(UbMemAicpuTransferDesc);
  HIXL_CHK_ACL_RET(aclrtMalloc(&handle.desc_buf, desc_buf_size, ACL_MEM_MALLOC_HUGE_ONLY),
                   "[UbMemEngine] aclrtMalloc desc_buf failed, size=%zu bytes", desc_buf_size);
  HIXL_CHK_ACL_RET(aclrtMemcpy(handle.desc_buf, desc_buf_size, descs.data(), desc_buf_size, ACL_MEMCPY_HOST_TO_DEVICE),
                   "[UbMemEngine] aclrtMemcpy desc_buf failed, size=%zu bytes", desc_buf_size);
  handle.desc_count = static_cast<uint32_t>(descs.size());
  return SUCCESS;
}

Status UbMemEngine::LaunchAicpuChunks(bool is_get, UbMemCompleteHandle &handle, uint32_t timeout_ms) const {
  auto *pool = TransferPool::GetInstance(handle.slot->device_id);
  HIXL_CHECK_NOTNULL(pool);
  HIXL_CHK_STATUS_RET(pool->EnsureUbMemStream(*handle.slot), "[UbMemEngine] EnsureUbMemStream failed, slot=%u",
                      handle.slot->slot_index);
  aclrtFuncHandle func = pool->GetDeviceKernelFunc(is_get, local_endpoint_.GetEndpoint().protocol);
  const char *kernel_name = is_get ? kUbMemFuncRead : kUbMemFuncWrite;
  HIXL_CHK_BOOL_RET_STATUS(func != nullptr, UNSUPPORTED, "[UbMemEngine] kernel is unavailable, kernel=%s", kernel_name);

  const uint32_t batch_size = global_config_.MaxTransferCountPerBatch();
  uint32_t chunk_offset = 0U;
  uint32_t status_offset = 0U;
  uint32_t batch_remaining = batch_size;
  while (chunk_offset < handle.desc_count) {
    const uint32_t chunk_list_num =
        std::min({kUbMemMaxKernelBatchSize, batch_remaining, handle.desc_count - chunk_offset});
    batch_remaining -= chunk_list_num;
    const bool need_notify_wait = batch_remaining == 0U || chunk_offset + chunk_list_num == handle.desc_count;
    UbMemAicpuKernelParam param{};
    HIXL_CHK_STATUS_RET(FillUbMemKernelParam(*handle.slot, handle.desc_buf, chunk_offset, chunk_list_num, is_get,
                                             need_notify_wait, timeout_ms, param),
                        "[UbMemEngine] FillUbMemKernelParam failed, chunk_offset=%u", chunk_offset);
    if (handle.status_buf != nullptr) {
      HIXL_CHK_BOOL_RET_STATUS(status_offset < handle.status_count, PARAM_INVALID,
                               "[UbMemEngine] AICPU status buffer index exceeds capacity, status_offset:%u, "
                               "status_count:%u",
                               status_offset, handle.status_count);
      param.status_addr =
          static_cast<uint64_t>(reinterpret_cast<uintptr_t>(handle.status_buf) + status_offset * sizeof(uint32_t));
      ++status_offset;
    }
    HIXL_CHK_STATUS_RET(LaunchUbMemKernel(func, kernel_name, handle, &param, sizeof(param), need_notify_wait),
                        "[UbMemEngine] LaunchUbMemKernel failed, chunk_offset=%u", chunk_offset);
    handle.slot->launched_tasks += chunk_list_num;
    chunk_offset += chunk_list_num;
    if (batch_remaining == 0U) {
      batch_remaining = batch_size;
    }
  }
  return SUCCESS;
}

Status UbMemEngine::CheckAicpuStatus(UbMemCompleteHandle &handle) const {
  if (handle.status_buf == nullptr || handle.status_count == 0U) {
    return SUCCESS;
  }
  std::vector<uint32_t> statuses(handle.status_count, 0U);
  const size_t status_bytes = handle.status_count * sizeof(uint32_t);
  const aclError copy_ret =
      aclrtMemcpy(statuses.data(), status_bytes, handle.status_buf, status_bytes, ACL_MEMCPY_DEVICE_TO_HOST);
  HIXL_CHK_ACL_RET(copy_ret, "[UbMemEngine] aclrtMemcpy download status_buf failed, size:%zu bytes", status_bytes);
  for (size_t i = 0U; i < statuses.size(); ++i) {
    HIXL_CHK_BOOL_RET_STATUS(statuses[i] == 0U, FAILED,
                             "[UbMemEngine] AICPU kernel failed, launch_index:%zu, status:%u.", i, statuses[i]);
  }
  return SUCCESS;
}

Status UbMemEngine::LaunchUbMemKernel(aclrtFuncHandle func, const char *kernel_name, UbMemCompleteHandle &handle,
                                      void *args, size_t args_size, bool wait_notify) const {
  constexpr uint32_t block_dim = 1U;
  aclrtArgsHandle args_handle = nullptr;
  HIXL_CHK_ACL_RET(aclrtKernelArgsInit(func, &args_handle), "[UbMemEngine] aclrtKernelArgsInit failed, kernel=%s",
                   kernel_name);
  aclrtParamHandle param_handle = nullptr;
  HIXL_CHK_ACL_RET(aclrtKernelArgsAppend(args_handle, args, args_size, &param_handle),
                   "[UbMemEngine] aclrtKernelArgsAppend failed, kernel=%s", kernel_name);
  HIXL_CHK_ACL_RET(aclrtKernelArgsFinalize(args_handle), "[UbMemEngine] aclrtKernelArgsFinalize failed, kernel=%s",
                   kernel_name);
  aclrtLaunchKernelCfg cfg{};
  aclrtLaunchKernelAttr attr{};
  attr.id = ACL_RT_LAUNCH_KERNEL_ATTR_TIMEOUT;
  attr.value.timeout = kUbMemKernelTimeoutS;
  cfg.numAttrs = 1U;
  cfg.attrs = &attr;
  HIXL_CHK_ACL_RET(aclrtLaunchKernelWithConfig(func, block_dim, handle.slot->stream, &cfg, args_handle, nullptr),
                   "[UbMemEngine] aclrtLaunchKernelWithConfig failed, kernel=%s, slot=%u", kernel_name,
                   handle.slot->slot_index);
  if (wait_notify) {
    HIXL_CHK_ACL_RET(aclrtWaitAndResetNotify(handle.slot->notify, handle.slot->stream, kUbMemNotifyTimeoutMs),
                     "[UbMemEngine] aclrtWaitAndResetNotify failed, kernel=%s, slot=%u", kernel_name,
                     handle.slot->slot_index);
  }
  return SUCCESS;
}

Status UbMemEngine::CheckStatus(void *query_handle, HixlCompleteStatus &status) {
  HIXL_CHECK_NOTNULL(query_handle);
  auto *handle = static_cast<UbMemCompleteHandle *>(query_handle);
  HIXL_CHK_BOOL_RET_STATUS(handle->magic == kUbMemCompleteMagic, PARAM_INVALID, "[UbMemEngine] bad handle magic=0x%X",
                           handle->magic);
  HIXL_CHECK_NOTNULL(handle->slot.get());
  HIXL_CHECK_NOTNULL(handle->host_flag);
  if (*(static_cast<volatile uint64_t *>(handle->host_flag)) == kUbMemFlagDoneValue) {
    const Status launch_status = CheckAicpuStatus(*handle);
    if (launch_status != SUCCESS) {
      status = HixlCompleteStatus::HIXL_COMPLETE_STATUS_FAILED;
      ReleaseUbMemCompleteHandle(handle);
      return launch_status;
    }
    status = HixlCompleteStatus::HIXL_COMPLETE_STATUS_COMPLETED;
    ReleaseUbMemCompleteHandle(handle);
    return SUCCESS;
  }
  if (handle->slot->err_flag_host_addr != nullptr && *handle->slot->err_flag_host_addr != 0U) {
    status = HixlCompleteStatus::HIXL_COMPLETE_STATUS_FAILED;
    ReleaseUbMemCompleteHandle(handle);
    return SUCCESS;
  }
  status = HixlCompleteStatus::HIXL_COMPLETE_STATUS_WAITING;
  return SUCCESS;
}

void UbMemEngine::AbortUbMemSlot(const TransferPool::SlotHandle &slot) const {
  auto *pool = TransferPool::GetInstance(slot.device_id);
  if (pool != nullptr) {
    pool->Abort(slot);
  }
}

void UbMemEngine::AbortPendingHandles() {
  std::vector<UbMemCompleteHandle *> pending(pending_handles_.begin(), pending_handles_.end());
  pending_handles_.clear();
  std::unordered_set<uint32_t> aborted_slots;
  for (UbMemCompleteHandle *handle : pending) {
    if (handle == nullptr) {
      continue;
    }
    if (handle->slot != nullptr && aborted_slots.insert(handle->slot->slot_index).second) {
      AbortUbMemSlot(*handle->slot);
    }
    ReleaseUbMemCompleteHandle(handle);
  }
}

void UbMemEngine::Finalize() {
  AbortPendingHandles();
  if (active_slot_ != nullptr) {
    AbortUbMemSlot(*active_slot_);
    active_slot_.reset();
  }
}

}  // namespace hixl
