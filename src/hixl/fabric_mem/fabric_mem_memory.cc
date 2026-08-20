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

#include <algorithm>
#include <limits>
#include <map>
#include <memory>
#include <unordered_set>

#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/hixl_utils.h"
#include "common/scope_guard.h"
#include "fabric_mem/acl_compat.h"
#include "fabric_mem/fabric_mem_allocator.h"
#include "fabric_mem/virtual_memory_manager.h"

namespace hixl {
namespace {
Status BuildRegisteredAddrInfo(uintptr_t addr, size_t len, MemType type, AddrInfo &addr_info) {
  HIXL_CHK_BOOL_RET_STATUS(len > 0, PARAM_INVALID, "Invalid fabric mem registration range.");
  const auto max_addr = std::numeric_limits<uintptr_t>::max();
  HIXL_CHK_BOOL_RET_STATUS(addr <= max_addr - len, PARAM_INVALID, "Fabric mem range overflow, addr:%p, size:%zu.",
                           reinterpret_cast<void *>(addr), len);
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

Status GetAddressRangeForPtr(uintptr_t addr, MemDesc &range) {
  if (aclrtMemGetAddressRange == nullptr) {
    return UNSUPPORTED;
  }
  void *base = nullptr;
  size_t size = 0;
  HIXL_CHK_ACL_RET(aclrtMemGetAddressRange(reinterpret_cast<void *>(addr), &base, &size),
                   "Get address range failed for ptr:%p.", reinterpret_cast<void *>(addr));
  const auto base_addr = reinterpret_cast<uintptr_t>(base);
  const auto max_addr = std::numeric_limits<uintptr_t>::max();
  HIXL_CHK_BOOL_RET_STATUS(base != nullptr && size > 0 && base_addr <= max_addr - size, FAILED,
                           "Invalid address range returned for ptr:%p, base:%p, size:%zu.",
                           reinterpret_cast<void *>(addr), base, size);
  HIXL_CHK_BOOL_RET_STATUS(IsRangeContained(addr, 1U, base_addr, size), FAILED,
                           "Address range does not contain ptr:%p, base:%p, size:%zu.", reinterpret_cast<void *>(addr),
                           base, size);
  range = {base_addr, size};
  return SUCCESS;
}

bool ResolveImportedHostAddr(const ShareHandleInfo &info, uintptr_t old_addr, uintptr_t &new_addr,
                             size_t &available_len) {
  if (info.imported_va == 0 || old_addr < info.va_addr) {
    return false;
  }
  const uintptr_t offset = old_addr - info.va_addr;
  if (offset >= info.len || info.imported_va > std::numeric_limits<uintptr_t>::max() - offset) {
    return false;
  }
  new_addr = info.imported_va + offset;
  available_len = info.len - offset;
  return true;
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

void FabricMemLocalMemory::ReleaseSegment(LocalMemSegment &segment) {
  auto &info = segment.info;
  if (info.imported_va != 0) {
    HIXL_CHK_ACL(aclrtUnmapMem(reinterpret_cast<void *>(info.imported_va)), "Unmap local host mapping failed.");
    (void)VirtualMemoryManager::GetInstance().ReleaseMemory(info.imported_va);
    info.imported_va = 0;
  }
  if (info.imported_handle != nullptr) {
    HIXL_CHK_ACL(aclrtFreePhysical(info.imported_handle), "Free imported local handle failed.");
    info.imported_handle = nullptr;
  }
  if (info.is_retained && segment.pa_handle != nullptr) {
    HIXL_CHK_ACL(aclrtFreePhysical(segment.pa_handle), "Free retained handle failed.");
    segment.pa_handle = nullptr;
  }
}

void FabricMemLocalMemory::ReleaseRegistration(LocalMemRegistration &registration) {
  std::vector<LocalMemSegment> shared_to_release;
  {
    std::lock_guard<std::mutex> lock(share_handle_mutex_);
    for (const uintptr_t page_addr : registration.foreign_pa_pages) {
      const auto it = exported_pas_.find(page_addr);
      if (it == exported_pas_.end() || it->second == nullptr) {
        continue;
      }
      if (it->second->refcount > 0U) {
        --it->second->refcount;
      }
      if (it->second->refcount == 0U) {
        shared_to_release.emplace_back(std::move(it->second->segment));
        exported_pas_.erase(it);
      }
    }
    registration.foreign_pa_pages.clear();
  }
  for (auto &segment : registration.segments) {
    ReleaseSegment(segment);
  }
  registration.segments.clear();
  for (auto &segment : shared_to_release) {
    ReleaseSegment(segment);
  }
}

Status FabricMemLocalMemory::ExportSegment(const MemDesc &mem, MemType type, aclrtDrvMemHandle pa_handle,
                                           bool is_retained, LocalMemSegment &segment) {
  segment.pa_handle = pa_handle;
  segment.info = {mem.addr, mem.len, {}, nullptr, 0, is_retained, type};
  HIXL_DISMISSABLE_GUARD(fail_guard, ([&segment]() { ReleaseSegment(segment); }));
  if (is_retained) {
    HIXL_CHK_ACL_RET(aclrtMemExportToShareableHandleV2(pa_handle, ACL_RT_VMM_EXPORT_FLAG_DISABLE_PID_VALIDATION,
                                                       ACL_MEM_SHARE_HANDLE_TYPE_FABRIC, &segment.info.share_handle),
                     "Export foreign fabric share handle failed.");
  } else {
    HIXL_CHK_STATUS_RET(FabricMemAllocator::ExportToShareableHandle(mem.addr, segment.info.share_handle),
                        "Export own fabric share handle failed.");
  }
  if (type == MEM_HOST) {
    HIXL_CHK_STATUS_RET(ImportHostMemoryForRegister(mem, segment.info.share_handle, segment.info.imported_handle,
                                                    segment.info.imported_va),
                        "Import host memory for register failed.");
  }
  HIXL_DISMISS_GUARD(fail_guard);
  return SUCCESS;
}

Status FabricMemLocalMemory::FindExistingHandleForOverlapLocked(const MemDesc &mem, MemType type, MemHandle &mem_handle,
                                                                bool &is_duplicate) const {
  AddrInfo cur_info{};
  HIXL_CHK_STATUS_RET(BuildRegisteredAddrInfo(mem.addr, mem.len, type, cur_info),
                      "Invalid fabric mem registration range.");
  std::map<MemHandle, AddrInfo> addr_map;
  for (const auto &item : registrations_) {
    const auto &registration = *item.second;
    AddrInfo registered_info{};
    HIXL_CHK_STATUS_RET(
        BuildRegisteredAddrInfo(registration.va_addr, registration.len, registration.mem_type, registered_info),
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

Status FabricMemLocalMemory::FindExistingHandleForOverlap(const MemDesc &mem, MemType type, MemHandle &mem_handle,
                                                          bool &is_duplicate) const {
  std::lock_guard<std::mutex> lock(share_handle_mutex_);
  return FindExistingHandleForOverlapLocked(mem, type, mem_handle, is_duplicate);
}

Status FabricMemLocalMemory::AttachForeignPaPage(uintptr_t page_addr, size_t page_len, MemType type,
                                                 LocalMemSegment *owned_segment, LocalMemRegistration &registration) {
  LocalMemSegment extra;
  bool release_extra = false;
  {
    std::lock_guard<std::mutex> lock(share_handle_mutex_);
    const auto it = exported_pas_.find(page_addr);
    const bool reusable = it != exported_pas_.end() && it->second != nullptr &&
                          it->second->segment.info.mem_type == type && it->second->segment.info.len == page_len;
    if (reusable) {
      ++it->second->refcount;
      registration.foreign_pa_pages.push_back(page_addr);
      if (owned_segment != nullptr) {
        extra = std::move(*owned_segment);
        release_extra = true;
      }
    } else {
      HIXL_CHK_BOOL_RET_STATUS(owned_segment != nullptr, FAILED, "Foreign fabric mem PA page:0x%lx is not exported.",
                               page_addr);
      auto exported = std::make_unique<ExportedPa>();
      exported->segment = std::move(*owned_segment);
      exported->refcount = 1U;
      exported_pas_[page_addr] = std::move(exported);
      registration.foreign_pa_pages.push_back(page_addr);
    }
  }
  if (release_extra) {
    ReleaseSegment(extra);
  }
  return SUCCESS;
}

Status FabricMemLocalMemory::BuildForeignSegments(const MemDesc &mem, MemType type,
                                                  LocalMemRegistration &registration) {
  const uintptr_t end = mem.addr + mem.len;
  uintptr_t cursor = mem.addr;
  while (cursor < end) {
    MemDesc block{};
    HIXL_CHK_STATUS_RET(GetAddressRangeForPtr(cursor, block),
                        "Failed to resolve address range while registering foreign fabric mem.");
    bool reused = false;
    {
      std::lock_guard<std::mutex> lock(share_handle_mutex_);
      const auto it = exported_pas_.find(block.addr);
      if (it != exported_pas_.end() && it->second != nullptr && it->second->segment.info.mem_type == type &&
          it->second->segment.info.len == block.len) {
        ++it->second->refcount;
        registration.foreign_pa_pages.push_back(block.addr);
        reused = true;
      }
    }
    if (reused) {
      cursor = block.addr + block.len;
      continue;
    }
    aclrtDrvMemHandle pa_handle = nullptr;
    HIXL_DISMISSABLE_GUARD(retain_guard, ([&pa_handle]() {
                             if (pa_handle != nullptr) {
                               HIXL_CHK_ACL(aclrtFreePhysical(pa_handle), "Free retained handle failed.");
                             }
                           }));
    HIXL_CHK_ACL_RET(aclrtMemRetainAllocationHandle(reinterpret_cast<void *>(block.addr), &pa_handle),
                     "Retain allocation handle failed for block base:%p.", reinterpret_cast<void *>(block.addr));
    HIXL_DISMISS_GUARD(retain_guard);
    LocalMemSegment segment{};
    HIXL_CHK_STATUS_RET(ExportSegment(block, type, pa_handle, true, segment),
                        "Export foreign fabric mem block failed.");
    HIXL_CHK_STATUS_RET(AttachForeignPaPage(block.addr, block.len, type, &segment, registration),
                        "Attach foreign fabric mem PA page failed.");
    cursor = block.addr + block.len;
  }
  HIXL_CHK_BOOL_RET_STATUS(!registration.foreign_pa_pages.empty(), FAILED,
                           "Foreign fabric mem registration produced no segments.");
  return SUCCESS;
}

Status FabricMemLocalMemory::CommitRegistration(std::unique_ptr<LocalMemRegistration> &registration,
                                                MemHandle candidate_handle, MemHandle &mem_handle, bool &committed) {
  const MemDesc mem{registration->va_addr, registration->len};
  bool is_duplicate = false;
  std::lock_guard<std::mutex> lock(share_handle_mutex_);
  HIXL_CHK_STATUS_RET(FindExistingHandleForOverlapLocked(mem, registration->mem_type, mem_handle, is_duplicate),
                      "Failed to recheck fabric mem address overlap.");
  if (is_duplicate) {
    return SUCCESS;
  }
  HIXL_CHK_BOOL_RET_STATUS(registrations_.find(candidate_handle) == registrations_.end(), FAILED,
                           "Fabric mem handle collision, handle:%p.", candidate_handle);
  registrations_.emplace(candidate_handle, std::move(registration));
  mem_handle = candidate_handle;
  committed = true;
  return SUCCESS;
}

Status FabricMemLocalMemory::RegisterOwnMem(const MemDesc &mem, MemType type, aclrtDrvMemHandle pa_handle,
                                            MemHandle &mem_handle) {
  auto registration = std::make_unique<LocalMemRegistration>();
  registration->va_addr = mem.addr;
  registration->len = mem.len;
  registration->mem_type = type;
  HIXL_DISMISSABLE_GUARD(fail_guard, ([this, &registration]() {
                           if (registration != nullptr) {
                             ReleaseRegistration(*registration);
                           }
                         }));
  LocalMemSegment segment{};
  HIXL_CHK_STATUS_RET(ExportSegment(mem, type, pa_handle, false, segment), "Export own fabric mem segment failed.");
  registration->segments.emplace_back(std::move(segment));
  bool committed = false;
  HIXL_CHK_STATUS_RET(CommitRegistration(registration, pa_handle, mem_handle, committed),
                      "Commit own fabric mem registration failed.");
  if (!committed) {
    return SUCCESS;
  }
  if (type == MEM_HOST) {
    has_host_memory_.store(true);
  }
  HIXL_DISMISS_GUARD(fail_guard);
  HIXL_LOGI("Register fabric mem success, type:%s, addr:%lu, len:%zu, retained:0, handle:%p.",
            MemTypeToString(type).c_str(), mem.addr, mem.len, mem_handle);
  return SUCCESS;
}

Status FabricMemLocalMemory::RegisterForeignMem(const MemDesc &mem, MemType type, MemHandle &mem_handle) {
  auto registration = std::make_unique<LocalMemRegistration>();
  registration->va_addr = mem.addr;
  registration->len = mem.len;
  registration->mem_type = type;
  HIXL_DISMISSABLE_GUARD(fail_guard, ([this, &registration]() {
                           if (registration != nullptr) {
                             ReleaseRegistration(*registration);
                           }
                         }));
  HIXL_CHK_STATUS_RET(BuildForeignSegments(mem, type, *registration), "Build foreign fabric mem segments failed.");
  const size_t segment_count = registration->foreign_pa_pages.size();
  // A foreign registration can own multiple PA handles, so use the registration object's stable identity.
  const MemHandle candidate_handle = registration.get();
  bool committed = false;
  HIXL_CHK_STATUS_RET(CommitRegistration(registration, candidate_handle, mem_handle, committed),
                      "Commit foreign fabric mem registration failed.");
  if (!committed) {
    return SUCCESS;
  }
  if (type == MEM_HOST) {
    has_host_memory_.store(true);
  }
  HIXL_DISMISS_GUARD(fail_guard);
  HIXL_LOGI("Register foreign fabric mem success, type:%s, addr:%lu, len:%zu, segments:%zu, handle:%p.",
            MemTypeToString(type).c_str(), mem.addr, mem.len, segment_count, mem_handle);
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
  if (FabricMemAllocator::GetPaHandleFromVa(mem.addr, pa_handle) == SUCCESS) {
    return RegisterOwnMem(mem, type, pa_handle, mem_handle);
  }
  HIXL_CHK_STATUS_RET(RegisterForeignMem(mem, type, mem_handle), "Register foreign fabric mem failed.");
  return SUCCESS;
}

Status FabricMemLocalMemory::DeregisterMem(MemHandle mem_handle) {
  std::unique_ptr<LocalMemRegistration> registration;
  {
    std::lock_guard<std::mutex> lock(share_handle_mutex_);
    const auto it = registrations_.find(mem_handle);
    if (it == registrations_.end()) {
      HIXL_LOGW("Fabric mem handle:%p is not registered.", mem_handle);
      return SUCCESS;
    }
    registration = std::move(it->second);
    registrations_.erase(it);
  }
  ReleaseRegistration(*registration);
  HIXL_LOGI("Deregister fabric mem success, handle:%p.", mem_handle);
  return SUCCESS;
}

std::vector<ShareHandleInfo> FabricMemLocalMemory::GetShareHandles() const {
  std::lock_guard<std::mutex> lock(share_handle_mutex_);
  std::vector<ShareHandleInfo> share_handles;
  std::unordered_set<uintptr_t> emitted_pages;
  for (const auto &item : registrations_) {
    for (const auto &segment : item.second->segments) {
      share_handles.emplace_back(segment.info);
    }
    for (const uintptr_t page_addr : item.second->foreign_pa_pages) {
      if (!emitted_pages.insert(page_addr).second) {
        continue;
      }
      const auto pa_it = exported_pas_.find(page_addr);
      if (pa_it != exported_pas_.end() && pa_it->second != nullptr) {
        share_handles.emplace_back(pa_it->second->segment.info);
      }
    }
  }
  return share_handles;
}

bool FabricMemLocalMemory::HasHostMemory() const {
  return has_host_memory_.load();
}

bool FabricMemLocalMemory::FindLocalHostRegisteredAddrLocked(uintptr_t old_addr, uintptr_t &new_addr,
                                                             size_t &available_len) const {
  for (const auto &item : registrations_) {
    for (const auto &segment : item.second->segments) {
      if (ResolveImportedHostAddr(segment.info, old_addr, new_addr, available_len)) {
        return true;
      }
    }
  }
  for (const auto &item : exported_pas_) {
    if (item.second != nullptr &&
        ResolveImportedHostAddr(item.second->segment.info, old_addr, new_addr, available_len)) {
      return true;
    }
  }
  return false;
}

Status FabricMemLocalMemory::TranslateLocalHostOpAddrs(std::vector<TransferOpDesc> &op_descs) const {
  std::lock_guard<std::mutex> lock(share_handle_mutex_);
  std::vector<TransferOpDesc> translated;
  translated.reserve(op_descs.size());
  for (const auto &op : op_descs) {
    HIXL_CHK_BOOL_RET_STATUS(op.len > 0, PARAM_INVALID, "Local host fabric mem transfer size must be non-zero.");
    const auto max_addr = std::numeric_limits<uintptr_t>::max();
    HIXL_CHK_BOOL_RET_STATUS(op.local_addr <= max_addr - op.len && op.remote_addr <= max_addr - op.len, PARAM_INVALID,
                             "Fabric mem transfer address overflow.");
    size_t offset = 0;
    while (offset < op.len) {
      const uintptr_t old_local_addr = op.local_addr + offset;
      uintptr_t new_local_addr = 0;
      size_t available_len = 0;
      HIXL_CHK_BOOL_RET_STATUS(FindLocalHostRegisteredAddrLocked(old_local_addr, new_local_addr, available_len),
                               PARAM_INVALID, "Local host fabric mem address:%lu, remaining len:%zu is not registered.",
                               old_local_addr, op.len - offset);
      const size_t chunk_len = std::min(op.len - offset, available_len);
      translated.emplace_back(TransferOpDesc{new_local_addr, op.remote_addr + offset, chunk_len});
      offset += chunk_len;
    }
  }
  op_descs.swap(translated);
  return SUCCESS;
}

void FabricMemLocalMemory::Finalize() {
  std::unordered_map<MemHandle, std::unique_ptr<LocalMemRegistration>> registrations;
  {
    std::lock_guard<std::mutex> lock(share_handle_mutex_);
    registrations.swap(registrations_);
  }
  for (auto &item : registrations) {
    ReleaseRegistration(*item.second);
  }
  has_host_memory_.store(false);
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
