/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "fabric_mem/fabric_mem_remote_memory.h"

#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/scope_guard.h"
#include "fabric_mem/virtual_memory_manager.h"

namespace hixl {

FabricMemRemoteMemory::~FabricMemRemoteMemory() {
  Finalize();
}

Status FabricMemRemoteMemory::Import(const std::vector<ShareHandleInfo> &remote_share_handles, int32_t device_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  ClearLocked();
  HIXL_DISMISSABLE_GUARD(fail_guard, ([this]() { ClearLocked(); }));
  for (const auto &remote_share_handle_info : remote_share_handles) {
    uintptr_t remote_va_addr = 0;
    aclrtDrvMemHandle remote_pa_handle = nullptr;
    HIXL_CHK_STATUS_RET(VirtualMemoryManager::GetInstance().ReserveMemory(remote_share_handle_info.len, remote_va_addr),
                        "Reserve memory for remote share handle failed.");
    HIXL_DISMISSABLE_GUARD(free_mem_guard, ([&remote_va_addr, &remote_pa_handle]() {
                             if (remote_va_addr != 0) {
                               (void)VirtualMemoryManager::GetInstance().ReleaseMemory(remote_va_addr);
                             }
                             if (remote_pa_handle != nullptr) {
                               HIXL_CHK_ACL(aclrtFreePhysical(remote_pa_handle),
                                            "Free imported remote pa handle failed.");
                             }
                           }));

    auto share_handle = remote_share_handle_info.share_handle;
    HIXL_CHK_ACL_RET(
        aclrtMemImportFromShareableHandleV2(&share_handle, ACL_MEM_SHARE_HANDLE_TYPE_FABRIC, 0U, &remote_pa_handle),
        "Import remote fabric share handle failed.");
    HIXL_CHK_ACL_RET(
        aclrtMapMem(reinterpret_cast<void *>(remote_va_addr), remote_share_handle_info.len, 0, remote_pa_handle, 0),
                     "Map remote imported memory failed.");
    remote_pa_handles_.emplace_back(remote_pa_handle);
    new_va_to_old_va_[remote_va_addr] = {remote_share_handle_info.va_addr, remote_share_handle_info.len};
    HIXL_DISMISS_GUARD(free_mem_guard);
    HIXL_LOGI("Imported remote fabric mem, old va:%lu, mapped va:%lu, len:%zu, handle:%p, device:%d.",
              remote_share_handle_info.va_addr, remote_va_addr, remote_share_handle_info.len, remote_pa_handle,
              device_id);
  }
  HIXL_DISMISS_GUARD(fail_guard);
  return SUCCESS;
}

void FabricMemRemoteMemory::Finalize() {
  std::lock_guard<std::mutex> lock(mutex_);
  ClearLocked();
}

void FabricMemRemoteMemory::ClearLocked() {
  for (const auto &it : new_va_to_old_va_) {
    HIXL_LOGI("Unmap remote fabric mem:%lu.", it.first);
    HIXL_CHK_ACL(aclrtUnmapMem(reinterpret_cast<void *>(it.first)), "Unmap remote fabric mem failed.");
    (void)VirtualMemoryManager::GetInstance().ReleaseMemory(it.first);
  }
  new_va_to_old_va_.clear();
  for (auto &remote_pa_handle : remote_pa_handles_) {
    HIXL_CHK_ACL(aclrtFreePhysical(remote_pa_handle), "Free imported remote pa handle failed.");
    HIXL_LOGI("Free imported remote handle:%p.", remote_pa_handle);
  }
  remote_pa_handles_.clear();
}

const std::unordered_map<uintptr_t, VaInfo> &FabricMemRemoteMemory::GetNewVaToOldVa() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return new_va_to_old_va_;
}
}  // namespace hixl
