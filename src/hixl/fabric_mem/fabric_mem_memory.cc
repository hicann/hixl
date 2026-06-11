/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "fabric_mem/fabric_mem_memory.h"

#include <limits>
#include <map>

#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/hixl_utils.h"
#include "common/scope_guard.h"
#include "fabric_mem/fabric_mem_allocator.h"
#include "fabric_mem/virtual_memory_manager.h"

namespace hixl {
namespace {
Status BuildRegisteredAddrInfo(uintptr_t addr, size_t len, MemType type, AddrInfo &addr_info) {
  HIXL_CHK_BOOL_RET_STATUS(len > 0, PARAM_INVALID, "Invalid fabric mem registration range.");
  const auto max_addr = std::numeric_limits<uintptr_t>::max();
  HIXL_CHK_BOOL_RET_STATUS(addr <= max_addr - len, PARAM_INVALID,
                           "Fabric mem range overflow, addr:%p, size:%lu.", reinterpret_cast<void *>(addr), len);
  addr_info = AddrInfo{addr, addr + len, type};
  return SUCCESS;
}

bool IsRangeContained(uintptr_t old_addr, size_t len, uintptr_t base, size_t size) {
  if (old_addr < base) {
    return false;
  }
  const uintptr_t offset = old_addr - base;
  return (offset <= size) && (len <= size - offset);
}

void CleanupRegisterMemFailure(aclrtDrvMemHandle pa_handle, bool is_retained, uintptr_t imported_va,
                               aclrtDrvMemHandle imported_pa_handle) {
  if (is_retained && pa_handle != nullptr) {
    HIXL_CHK_ACL(aclrtFreePhysical(pa_handle), "Free retained handle after register failure failed.");
  }
  if (imported_va != 0) {
    HIXL_CHK_ACL(aclrtUnmapMem(reinterpret_cast<void *>(imported_va)),
                 "Unmap local import after register failure failed.");
    (void)VirtualMemoryManager::GetInstance().ReleaseMemory(imported_va);
  }
  if (imported_pa_handle != nullptr) {
    HIXL_CHK_ACL(aclrtFreePhysical(imported_pa_handle), "Free imported handle after register failure failed.");
  }
}
}  // namespace

FabricMemLocalMemory::~FabricMemLocalMemory() {
  Finalize();
}

Status FabricMemLocalMemory::ImportHostMemoryForRegister(const MemDesc &mem, aclrtMemFabricHandle &share_handle,
                                                         aclrtDrvMemHandle &imported_pa_handle,
                                                         uintptr_t &imported_va) {
  HIXL_CHK_STATUS_RET(VirtualMemoryManager::GetInstance().ReserveMemory(mem.len, imported_va),
                      "Reserve local host fabric mapping failed.");
  HIXL_CHK_ACL_RET(
      aclrtMemImportFromShareableHandleV2(&share_handle, ACL_MEM_SHARE_HANDLE_TYPE_FABRIC, 0U, &imported_pa_handle),
      "Import local host fabric share handle failed.");
  HIXL_CHK_ACL_RET(aclrtMapMem(reinterpret_cast<void *>(imported_va), mem.len, 0, imported_pa_handle, 0),
                   "Map local host fabric memory failed.");
  return SUCCESS;
}

Status FabricMemLocalMemory::FindExistingHandleForOverlap(const MemDesc &mem, MemType type, MemHandle &mem_handle,
                                                          bool &is_duplicate) {
  AddrInfo cur_info{};
  HIXL_CHK_STATUS_RET(BuildRegisteredAddrInfo(mem.addr, mem.len, type, cur_info),
                      "Invalid fabric mem registration range.");
  std::lock_guard<std::mutex> lock(share_handle_mutex_);
  std::map<MemHandle, AddrInfo> addr_map;
  for (const auto &item : share_handles_) {
    AddrInfo registered_info{};
    HIXL_CHK_STATUS_RET(
        BuildRegisteredAddrInfo(item.second.va_addr, item.second.len, item.second.mem_type, registered_info),
        "Registered fabric mem range is invalid.");
    addr_map[item.first] = registered_info;
  }
  MemHandle existing_handle = nullptr;
  HIXL_CHK_STATUS_RET(CheckAddrOverlap(cur_info, addr_map, is_duplicate, existing_handle),
                      "Failed to check fabric mem address overlap.");
  if (is_duplicate) {
    mem_handle = existing_handle;
  }
  return SUCCESS;
}

Status FabricMemLocalMemory::RegisterMem(const MemDesc &mem, MemType type, MemHandle &mem_handle) {
  HIXL_CHK_BOOL_RET_STATUS(mem.addr != 0 && mem.len > 0, PARAM_INVALID, "Invalid fabric mem registration range.");
  bool is_duplicate = false;
  HIXL_CHK_STATUS_RET(FindExistingHandleForOverlap(mem, type, mem_handle, is_duplicate),
                      "Failed to check fabric mem address overlap.");
  if (is_duplicate) {
    return SUCCESS;
  }
  aclrtDrvMemHandle pa_handle = nullptr;
  bool is_retained = false;
  if (FabricMemAllocator::GetPaHandleFromVa(mem.addr, pa_handle) != SUCCESS) {
    HIXL_CHK_ACL_RET(aclrtMemRetainAllocationHandle(reinterpret_cast<void *>(mem.addr), &pa_handle),
                     "Retain allocation handle failed.");
    is_retained = true;
  }
  aclrtDrvMemHandle imported_pa_handle = nullptr;
  uintptr_t imported_va = 0;
  HIXL_DISMISSABLE_GUARD(fail_guard, ([&]() {
                             CleanupRegisterMemFailure(pa_handle, is_retained, imported_va, imported_pa_handle);
                           }));

  aclrtMemFabricHandle share_handle = {};
  HIXL_CHK_ACL_RET(aclrtMemExportToShareableHandleV2(pa_handle, ACL_RT_VMM_EXPORT_FLAG_DISABLE_PID_VALIDATION,
                                                     ACL_MEM_SHARE_HANDLE_TYPE_FABRIC, &share_handle),
                   "Export fabric share handle failed.");
  if (type == MEM_HOST) {
    HIXL_CHK_STATUS_RET(ImportHostMemoryForRegister(mem, share_handle, imported_pa_handle, imported_va),
                        "Import host memory for register failed.");
  }

  {
    std::lock_guard<std::mutex> lock(share_handle_mutex_);
    share_handles_[pa_handle] = {mem.addr, mem.len, share_handle, imported_pa_handle, imported_va, is_retained, type};
  }
  if (type == MEM_HOST) {
    has_host_memory_.store(true);
  }
  mem_handle = pa_handle;
  HIXL_LOGI("Register fabric mem success, type:%s, addr:%lu, len:%zu, retained:%d, handle:%p.",
            MemTypeToString(type).c_str(), mem.addr, mem.len, static_cast<int32_t>(is_retained), mem_handle);
  HIXL_DISMISS_GUARD(fail_guard);
  return SUCCESS;
}

Status FabricMemLocalMemory::DeregisterMem(MemHandle mem_handle) {
  std::lock_guard<std::mutex> lock(share_handle_mutex_);
  const auto it = share_handles_.find(static_cast<aclrtDrvMemHandle>(mem_handle));
  if (it == share_handles_.end()) {
    HIXL_LOGW("Fabric mem handle:%p is not registered.", mem_handle);
    return SUCCESS;
  }
  const auto info = it->second;
  if (info.imported_va != 0) {
    HIXL_CHK_ACL(aclrtUnmapMem(reinterpret_cast<void *>(info.imported_va)), "Unmap local host mapping failed.");
    (void)VirtualMemoryManager::GetInstance().ReleaseMemory(info.imported_va);
  }
  if (info.imported_handle != nullptr) {
    HIXL_CHK_ACL(aclrtFreePhysical(info.imported_handle), "Free imported local handle failed.");
  }
  if (info.is_retained) {
    HIXL_CHK_ACL(aclrtFreePhysical(static_cast<aclrtDrvMemHandle>(mem_handle)), "Free retained handle failed.");
  }
  share_handles_.erase(it);
  HIXL_LOGI("Deregister fabric mem success, handle:%p.", mem_handle);
  return SUCCESS;
}

std::vector<ShareHandleInfo> FabricMemLocalMemory::GetShareHandles() const {
  std::lock_guard<std::mutex> lock(share_handle_mutex_);
  std::vector<ShareHandleInfo> share_handles;
  share_handles.reserve(share_handles_.size());
  for (const auto &share_handle : share_handles_) {
    share_handles.emplace_back(share_handle.second);
  }
  return share_handles;
}

bool FabricMemLocalMemory::HasHostMemory() const {
  return has_host_memory_.load();
}

bool FabricMemLocalMemory::FindLocalHostRegisteredAddrLocked(uintptr_t old_addr, size_t len,
                                                             uintptr_t &new_addr) const {
  for (const auto &item : share_handles_) {
    const auto &info = item.second;
    if (info.imported_va == 0) {
      continue;
    }
    if (!IsRangeContained(old_addr, len, info.va_addr, info.len)) {
      continue;
    }
    new_addr = info.imported_va + (old_addr - info.va_addr);
    return true;
  }
  return false;
}

Status FabricMemLocalMemory::TranslateLocalHostOpAddrs(std::vector<TransferOpDesc> &op_descs) const {
  std::lock_guard<std::mutex> lock(share_handle_mutex_);
  for (auto &op : op_descs) {
    HIXL_CHK_BOOL_RET_STATUS(FindLocalHostRegisteredAddrLocked(op.local_addr, op.len, op.local_addr), PARAM_INVALID,
                             "Local host fabric mem address:%lu, len:%zu is not registered.", op.local_addr, op.len);
  }
  return SUCCESS;
}

void FabricMemLocalMemory::Finalize() {
  std::lock_guard<std::mutex> lock(share_handle_mutex_);
  for (auto &share_handle : share_handles_) {
    const auto &info = share_handle.second;
    if (info.imported_va != 0) {
      HIXL_CHK_ACL(aclrtUnmapMem(reinterpret_cast<void *>(info.imported_va)), "Unmap local host mapping failed.");
      (void)VirtualMemoryManager::GetInstance().ReleaseMemory(info.imported_va);
    }
    if (info.imported_handle != nullptr) {
      HIXL_CHK_ACL(aclrtFreePhysical(info.imported_handle), "Free imported local handle failed.");
    }
    if (info.is_retained) {
      HIXL_CHK_ACL(aclrtFreePhysical(share_handle.first), "Free retained handle failed.");
    }
  }
  share_handles_.clear();
}

FabricMemRemoteMemory::~FabricMemRemoteMemory() {
  Finalize();
}

Status FabricMemRemoteMemory::Import(const std::vector<ShareHandleInfo> &remote_share_handles, int32_t device_id) {
  std::lock_guard<std::mutex> lock(mutex_);
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

void FabricMemRemoteMemory::Finalize() {
  std::lock_guard<std::mutex> lock(mutex_);
  ClearLocked();
}

std::unordered_map<uintptr_t, VaInfo> FabricMemRemoteMemory::GetNewVaToOldVa() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return new_va_to_old_va_;
}
}  // namespace hixl
