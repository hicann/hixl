/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "cs/ubmem/ubmem_memory.h"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/hixl_utils.h"
#include "common/scope_guard.h"
#include "cs/ubmem/acl_compat.h"
#include "cs/ubmem/ubmem_allocator.h"
#include "cs/ubmem/ubmem_virtual_memory_manager.h"

namespace hixl {
namespace {

// True when both describe the same exported PA range: same share handle bytes, address and length. The
// share handle carries the exporter's identity, so a range exported by another process never matches
// even when the two addresses coincide. This is the identity used to pair an import with its unimport.
bool IsSameExportedRange(const ShareHandleInfo &lhs, const ShareHandleInfo &rhs) {
  return lhs.va_addr == rhs.va_addr && lhs.len == rhs.len &&
         memcmp(lhs.share_handle.data, rhs.share_handle.data, sizeof(lhs.share_handle.data)) == 0;
}

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

// Peer address span covered by one export. The handles of one export tile a contiguous range, so the
// importer reserves that span once and lays every PA block at its peer offset inside it. An empty or
// overflowing handle list is rejected here rather than at map time.
Status ComputeRemoteSpan(const std::vector<ShareHandleInfo> &handles, uintptr_t &base, size_t &len) {
  HIXL_CHK_BOOL_RET_STATUS(!handles.empty(), PARAM_INVALID, "Remote share handle list is empty.");
  const auto max_addr = std::numeric_limits<uintptr_t>::max();
  uintptr_t start = max_addr;
  uintptr_t end = 0;
  for (const auto &handle : handles) {
    HIXL_CHK_BOOL_RET_STATUS(handle.len > 0U, PARAM_INVALID, "Remote fabric mem range length must be non-zero.");
    HIXL_CHK_BOOL_RET_STATUS(handle.va_addr <= max_addr - handle.len, PARAM_INVALID,
                             "Remote fabric mem range overflows, va:%lu, len:%zu.", handle.va_addr, handle.len);
    start = std::min(start, handle.va_addr);
    end = std::max(end, handle.va_addr + handle.len);
  }
  HIXL_CHK_BOOL_RET_STATUS(end > start, PARAM_INVALID, "Remote fabric mem span is empty, va:%lu.", start);
  base = start;
  len = static_cast<size_t>(end - start);
  return SUCCESS;
}
}  // namespace

class UbMemMemory::Impl {
 public:
  Impl();
  ~Impl();
  Impl(const Impl &) = delete;
  Impl &operator=(const Impl &) = delete;
  Impl(Impl &&) = delete;
  Impl &operator=(Impl &&) = delete;

  Status RegisterMem(const MemDesc &mem, MemType type, MemHandle &mem_handle);
  Status DeregisterMem(MemHandle mem_handle);
  Status GetShareHandles(MemHandle mem_handle, std::vector<ShareHandleInfo> &out) const;
  Status Import(const std::vector<ShareHandleInfo> &handles);
  Status Unimport(const std::vector<ShareHandleInfo> &handles);
  void Finalize();
  std::shared_ptr<const UbMemRemoteIndex> GetTranslationIndex() const;

 private:
  // Local registered memory of one endpoint: exports registered buffers as fabric share handles for the
  // peer. Transfers use the original local address. Own host NPU access comes from MallocMem SetAccess;
  // peer memory is imported and mapped, never SetAccess on the peer's original address.
  class LocalMemory;
  // Remote memory of one endpoint: imports the peer's fabric share handles and maps them into the local
  // virtual address space so transfers can target peer buffers. Handles exported by this endpoint are
  // identity-bound to their original addresses.
  class RemoteMemory;

  LocalMemory &Local() {
    return *local_;
  }
  LocalMemory &Local() const {
    return *local_;
  }
  RemoteMemory &Remote() {
    return *remote_;
  }
  RemoteMemory &Remote() const {
    return *remote_;
  }
  std::unique_ptr<LocalMemory> local_;
  std::unique_ptr<RemoteMemory> remote_;
};

UbMemMemory::Impl::Impl() : local_(std::make_unique<LocalMemory>()), remote_(std::make_unique<RemoteMemory>()) {}

class UbMemMemory::Impl::LocalMemory {
 public:
  LocalMemory() = default;
  ~LocalMemory();
  LocalMemory(const LocalMemory &) = delete;
  LocalMemory &operator=(const LocalMemory &) = delete;
  LocalMemory(LocalMemory &&) = delete;
  LocalMemory &operator=(LocalMemory &&) = delete;

  Status RegisterMem(const MemDesc &mem, MemType type, MemHandle &mem_handle);
  Status DeregisterMem(MemHandle mem_handle);
  Status GetShareHandles(MemHandle mem_handle, std::vector<ShareHandleInfo> &out) const;
  // Classifies the peer's decoded handles in one pass: `owned` holds the ranges this endpoint
  // exported, `remote` the ones that have to be imported. The lock is taken once for the whole batch.
  void SplitOwnedHandles(const std::vector<ShareHandleInfo> &handles, std::vector<ShareHandleInfo> &owned,
                         std::vector<ShareHandleInfo> &remote) const;
  // True when this endpoint exported the range the handle names, compared on the share handle bytes
  // plus va/len. A hit means the peer's buffer is directly addressable here, so it is bound to its
  // original address instead of being imported. The comparison never looks at address numbers alone: two
  // processes can own the same address range, while an ACL share handle identifies its exporter.
  bool OwnsHandle(const ShareHandleInfo &info) const;
  void Finalize();

 private:
  struct LocalMemSegment {
    aclrtDrvMemHandle pa_handle = nullptr;
    // Foreign segments describe the full underlying allocation because aclrtMapMem imports a full PA block.
    ShareHandleInfo info{};
  };

  struct LocalMemRegistration {
    uintptr_t va_addr = 0;
    size_t len = 0;
    MemType mem_type = MEM_DEVICE;
    std::vector<LocalMemSegment> segments;
    // Keys into exported_pas_. Adjacent user ranges may share one retained PA.
    std::vector<uintptr_t> foreign_pa_pages;
  };

  struct ExportedPa {
    LocalMemSegment segment;
    uint32_t refcount = 0;
  };

  static Status ExportSegment(const MemDesc &mem, MemType type, aclrtDrvMemHandle pa_handle, bool is_retained,
                              LocalMemSegment &segment);
  static void ReleaseSegment(LocalMemSegment &segment);
  void ReleaseRegistration(LocalMemRegistration &registration);
  bool OwnsHandleLocked(const ShareHandleInfo &info) const;
  Status FindExistingHandleForOverlap(const MemDesc &mem, MemType type, MemHandle &mem_handle,
                                      bool &is_duplicate) const;
  Status FindExistingHandleForOverlapLocked(const MemDesc &mem, MemType type, MemHandle &mem_handle,
                                            bool &is_duplicate) const;
  Status RegisterOwnMem(const MemDesc &mem, MemType type, aclrtDrvMemHandle pa_handle, MemHandle &mem_handle);
  Status RegisterForeignMem(const MemDesc &mem, MemType type, MemHandle &mem_handle);
  Status BuildForeignSegments(const MemDesc &mem, MemType type, LocalMemRegistration &registration);
  Status CommitRegistration(std::unique_ptr<LocalMemRegistration> &registration, MemHandle candidate_handle,
                            MemHandle &mem_handle, bool &committed);
  Status AttachForeignPaPage(uintptr_t page_addr, size_t page_len, MemType type, LocalMemSegment *owned_segment,
                             LocalMemRegistration &registration);

  mutable std::mutex share_handle_mutex_;
  std::unordered_map<MemHandle, std::unique_ptr<LocalMemRegistration>> registrations_;
  std::unordered_map<uintptr_t, std::unique_ptr<ExportedPa>> exported_pas_;
};

void UbMemMemory::Impl::LocalMemory::ReleaseSegment(LocalMemSegment &segment) {
  if (segment.info.is_retained && segment.pa_handle != nullptr) {
    HIXL_CHK_ACL(aclrtFreePhysical(segment.pa_handle), "Free retained handle failed.");
    segment.pa_handle = nullptr;
  }
  // Every released segment was recorded by ExportSegment, so the export leaves the process ledger here.
  UbMemAllocator::ReleaseExportedRange(segment.info);
}

void UbMemMemory::Impl::LocalMemory::ReleaseRegistration(LocalMemRegistration &registration) {
  for (const uintptr_t page_addr : registration.foreign_pa_pages) {
    std::unique_ptr<ExportedPa> exported;
    {
      std::lock_guard<std::mutex> lock(share_handle_mutex_);
      const auto it = exported_pas_.find(page_addr);
      if (it == exported_pas_.end() || it->second == nullptr) {
        continue;
      }
      if (it->second->refcount > 0U) {
        --it->second->refcount;
      }
      if (it->second->refcount == 0U) {
        exported = std::move(it->second);
        exported_pas_.erase(it);
      }
    }
    if (exported != nullptr) {
      ReleaseSegment(exported->segment);
    }
  }
  registration.foreign_pa_pages.clear();
  for (auto &segment : registration.segments) {
    ReleaseSegment(segment);
  }
  registration.segments.clear();
}

Status UbMemMemory::Impl::LocalMemory::ExportSegment(const MemDesc &mem, MemType type, aclrtDrvMemHandle pa_handle,
                                                     bool is_retained, LocalMemSegment &segment) {
  segment.pa_handle = pa_handle;
  segment.info = {mem.addr, mem.len, {}, is_retained, type};
  HIXL_DISMISSABLE_GUARD(fail_guard, ([&segment]() { ReleaseSegment(segment); }));
  if (is_retained) {
    HIXL_CHK_STATUS_RET(
        UbMemAllocator::GetOrExportForeignShareHandle(mem.addr, mem.len, pa_handle, segment.info.share_handle),
        "Export foreign fabric share handle failed.");
  } else {
    HIXL_CHK_STATUS_RET(UbMemAllocator::ExportToShareableHandle(mem.addr, segment.info.share_handle),
                        "Export own fabric share handle failed.");
  }
  // The range is exported as of here, so any endpoint of this process may address it at its original VA.
  UbMemAllocator::RecordExportedRange(segment.info);
  HIXL_DISMISS_GUARD(fail_guard);
  return SUCCESS;
}

Status UbMemMemory::Impl::LocalMemory::FindExistingHandleForOverlapLocked(const MemDesc &mem, MemType type,
                                                                          MemHandle &mem_handle,
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

Status UbMemMemory::Impl::LocalMemory::FindExistingHandleForOverlap(const MemDesc &mem, MemType type,
                                                                    MemHandle &mem_handle, bool &is_duplicate) const {
  std::lock_guard<std::mutex> lock(share_handle_mutex_);
  return FindExistingHandleForOverlapLocked(mem, type, mem_handle, is_duplicate);
}

Status UbMemMemory::Impl::LocalMemory::AttachForeignPaPage(uintptr_t page_addr, size_t page_len, MemType type,
                                                           LocalMemSegment *owned_segment,
                                                           LocalMemRegistration &registration) {
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

Status UbMemMemory::Impl::LocalMemory::BuildForeignSegments(const MemDesc &mem, MemType type,
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

Status UbMemMemory::Impl::LocalMemory::CommitRegistration(std::unique_ptr<LocalMemRegistration> &registration,
                                                          MemHandle candidate_handle, MemHandle &mem_handle,
                                                          bool &committed) {
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

Status UbMemMemory::Impl::LocalMemory::RegisterOwnMem(const MemDesc &mem, MemType type, aclrtDrvMemHandle pa_handle,
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
  HIXL_DISMISS_GUARD(fail_guard);
  HIXL_LOGI("Register fabric mem success, type:%s, addr:%lu, len:%zu, retained:0, handle:%p.",
            MemTypeToString(type).c_str(), mem.addr, mem.len, mem_handle);
  return SUCCESS;
}

Status UbMemMemory::Impl::LocalMemory::RegisterForeignMem(const MemDesc &mem, MemType type, MemHandle &mem_handle) {
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
  HIXL_DISMISS_GUARD(fail_guard);
  HIXL_LOGI("Register foreign fabric mem success, type:%s, addr:%lu, len:%zu, segments:%zu, handle:%p.",
            MemTypeToString(type).c_str(), mem.addr, mem.len, segment_count, mem_handle);
  return SUCCESS;
}

Status UbMemMemory::Impl::LocalMemory::RegisterMem(const MemDesc &mem, MemType type, MemHandle &mem_handle) {
  HIXL_CHK_BOOL_RET_STATUS(mem.addr != 0 && mem.len > 0, PARAM_INVALID, "Invalid fabric mem registration range.");
  bool is_duplicate = false;
  HIXL_CHK_STATUS_RET(FindExistingHandleForOverlap(mem, type, mem_handle, is_duplicate),
                      "Failed to check fabric mem address overlap.");
  if (is_duplicate) {
    return SUCCESS;
  }
  aclrtDrvMemHandle pa_handle = nullptr;
  if (UbMemAllocator::GetPaHandleFromVa(mem.addr, pa_handle) == SUCCESS) {
    return RegisterOwnMem(mem, type, pa_handle, mem_handle);
  }
  HIXL_CHK_STATUS_RET(RegisterForeignMem(mem, type, mem_handle), "Register foreign fabric mem failed.");
  return SUCCESS;
}

Status UbMemMemory::Impl::LocalMemory::DeregisterMem(MemHandle mem_handle) {
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

Status UbMemMemory::Impl::LocalMemory::GetShareHandles(MemHandle mem_handle, std::vector<ShareHandleInfo> &out) const {
  out.clear();
  std::lock_guard<std::mutex> lock(share_handle_mutex_);
  const auto it = registrations_.find(mem_handle);
  HIXL_CHK_BOOL_RET_STATUS(it != registrations_.end(), PARAM_INVALID, "Fabric mem handle:%p is not registered.",
                           mem_handle);
  std::unordered_set<uintptr_t> emitted_pages;
  for (const auto &segment : it->second->segments) {
    out.emplace_back(segment.info);
  }
  for (const uintptr_t page_addr : it->second->foreign_pa_pages) {
    if (!emitted_pages.insert(page_addr).second) {
      continue;
    }
    const auto pa_it = exported_pas_.find(page_addr);
    if (pa_it != exported_pas_.end() && pa_it->second != nullptr) {
      out.emplace_back(pa_it->second->segment.info);
    }
  }
  HIXL_CHK_BOOL_RET_STATUS(!out.empty(), PARAM_INVALID, "Fabric mem handle:%p has no share handles.", mem_handle);
  return SUCCESS;
}

bool UbMemMemory::Impl::LocalMemory::OwnsHandleLocked(const ShareHandleInfo &info) const {
  for (const auto &item : registrations_) {
    const auto &registration = *item.second;
    for (const auto &segment : registration.segments) {
      if (IsSameExportedRange(segment.info, info)) {
        return true;
      }
    }
    for (const uintptr_t page_addr : registration.foreign_pa_pages) {
      const auto it = exported_pas_.find(page_addr);
      if (it != exported_pas_.end() && it->second != nullptr && IsSameExportedRange(it->second->segment.info, info)) {
        return true;
      }
    }
  }
  // A range another endpoint of this process exported is addressable here too, and ACL refuses to import
  // an export of this process, so it must be identity-bound. This is the only evidence available when the
  // importer skipped its own registration, as a ubmem client does.
  return UbMemAllocator::IsRangeExportedHere(info);
}

bool UbMemMemory::Impl::LocalMemory::OwnsHandle(const ShareHandleInfo &info) const {
  std::lock_guard<std::mutex> lock(share_handle_mutex_);
  return OwnsHandleLocked(info);
}

void UbMemMemory::Impl::LocalMemory::SplitOwnedHandles(const std::vector<ShareHandleInfo> &handles,
                                                       std::vector<ShareHandleInfo> &owned,
                                                       std::vector<ShareHandleInfo> &remote) const {
  owned.clear();
  remote.clear();
  std::lock_guard<std::mutex> lock(share_handle_mutex_);
  for (const auto &handle : handles) {
    if (OwnsHandleLocked(handle)) {
      owned.emplace_back(handle);
    } else {
      remote.emplace_back(handle);
    }
  }
}

void UbMemMemory::Impl::LocalMemory::Finalize() {
  std::unordered_map<MemHandle, std::unique_ptr<LocalMemRegistration>> registrations;
  {
    std::lock_guard<std::mutex> lock(share_handle_mutex_);
    registrations.swap(registrations_);
  }
  for (auto &item : registrations) {
    ReleaseRegistration(*item.second);
  }
}

UbMemMemory::Impl::LocalMemory::~LocalMemory() {
  Finalize();
}

class UbMemMemory::Impl::RemoteMemory {
 public:
  RemoteMemory() = default;
  ~RemoteMemory();
  RemoteMemory(const RemoteMemory &) = delete;
  RemoteMemory &operator=(const RemoteMemory &) = delete;
  RemoteMemory(RemoteMemory &&) = delete;
  RemoteMemory &operator=(RemoteMemory &&) = delete;

  Status Import(const std::vector<ShareHandleInfo> &remote_share_handles);
  // Binds the peer's original addresses as an identity map when the handles are this endpoint's own
  // exports: no ACL import/map and no VMM reservation, so teardown must not release them.
  Status BindLocalAddrs(const std::vector<ShareHandleInfo> &remote_share_handles);
  // Releases exactly the ranges named by handles, matched on share handle identity instead of on address,
  // so it also works when the local registration that classified them is already gone. Identity-bound
  // ranges are dropped without touching ACL or the VMM. Handles that are not mapped here are skipped
  // and counted, never treated as an error: this runs on teardown paths.
  // Every range matching a handle is released, not just the first. Two registrations can share one
  // peer PA and therefore export the same handle, so a partial release would leave a live mapping
  // behind for a range the caller believes is gone.
  Status Unimport(const std::vector<ShareHandleInfo> &handles);
  void Finalize();
  std::shared_ptr<const UbMemRemoteIndex> GetTranslationIndex() const;

 private:
  struct RemoteRange {
    // The peer's handle as it arrived: peer address, length and share handle bytes. Kept so Unimport can
    // find this range again by identity rather than by an address that may collide across processes.
    ShareHandleInfo info{};
    // Local address the range is addressed by: the mapped address for an import, the peer address for a bind.
    uintptr_t local_va = 0;
    // False for identity-bound local addresses: they were neither mapped nor reserved by this object.
    bool imported = false;
    // Only set while imported, and owned by this range so a targeted release frees just this one.
    aclrtDrvMemHandle pa_handle = nullptr;
    // Reserved address block this range was mapped into. Every range of one Import call shares it, and the
    // block goes back to the VMM only once the last of them is released. Zero for identity binds.
    uintptr_t import_base = 0;
  };

  // One VMM reservation shared by all ranges of an Import call. `live_ranges` counts the ranges still
  // mapped into it, so a partial Unimport cannot hand a block back while a sibling range still uses it.
  struct ImportedBlock {
    size_t len = 0;
    size_t live_ranges = 0;
  };

  void ClearLocked();
  Status ImportOneRangeLocked(const ShareHandleInfo &info, uintptr_t peer_base, uintptr_t local_base,
                              std::vector<uintptr_t> &added_vas);
  void RollbackImportBatchLocked(uintptr_t local_base, const std::vector<uintptr_t> &added_vas);
  // Releases what range still owns: unmaps and returns the VMM block once its last range is gone, then
  // frees its PA handle. Identity-bound ranges have nothing to release and are only dropped.
  void ReleaseRangeLocked(RemoteRange &range);
  // Releases every range matching handle and returns how many were released.
  size_t ReleaseByHandleLocked(const ShareHandleInfo &handle);
  void RebuildTranslationIndexLocked();
  mutable std::mutex mutex_;
  std::unordered_map<uintptr_t, RemoteRange> va_mappings_;
  std::unordered_map<uintptr_t, ImportedBlock> imported_blocks_;
  std::shared_ptr<const UbMemRemoteIndex> translation_index_;
};

Status UbMemMemory::Impl::Import(const std::vector<ShareHandleInfo> &handles) {
  if (handles.empty()) {
    return SUCCESS;
  }
  std::vector<ShareHandleInfo> owned;
  std::vector<ShareHandleInfo> remote;
  Local().SplitOwnedHandles(handles, owned, remote);
  HIXL_CHK_STATUS_RET(Remote().Import(remote), "Import remote fabric mem failed.");
  HIXL_CHK_STATUS_RET(Remote().BindLocalAddrs(owned), "Bind local fabric mem failed.");
  if (!owned.empty()) {
    HIXL_LOGI("Bound %zu peer handle(s) to local addresses without import.", owned.size());
  }
  return SUCCESS;
}

Status UbMemMemory::Impl::RegisterMem(const MemDesc &mem, MemType type, MemHandle &mem_handle) {
  return Local().RegisterMem(mem, type, mem_handle);
}

Status UbMemMemory::Impl::DeregisterMem(MemHandle mem_handle) {
  return Local().DeregisterMem(mem_handle);
}

Status UbMemMemory::Impl::GetShareHandles(MemHandle mem_handle, std::vector<ShareHandleInfo> &out) const {
  return Local().GetShareHandles(mem_handle, out);
}

Status UbMemMemory::Impl::Unimport(const std::vector<ShareHandleInfo> &handles) {
  return Remote().Unimport(handles);
}

void UbMemMemory::Impl::Finalize() {
  Remote().Finalize();
  Local().Finalize();
}

std::shared_ptr<const UbMemRemoteIndex> UbMemMemory::Impl::GetTranslationIndex() const {
  return Remote().GetTranslationIndex();
}

UbMemMemory::Impl::~Impl() = default;

UbMemMemory::UbMemMemory() : impl_(std::make_unique<Impl>()) {}

UbMemMemory::~UbMemMemory() {
  Finalize();
}

Status UbMemMemory::RegisterMem(const MemDesc &mem, MemType type, MemHandle &mem_handle) {
  return impl_->RegisterMem(mem, type, mem_handle);
}

Status UbMemMemory::DeregisterMem(MemHandle mem_handle) {
  return impl_->DeregisterMem(mem_handle);
}

Status UbMemMemory::GetShareHandles(MemHandle mem_handle, std::vector<ShareHandleInfo> &out) const {
  return impl_->GetShareHandles(mem_handle, out);
}

Status UbMemMemory::Import(const std::vector<ShareHandleInfo> &handles) {
  return impl_->Import(handles);
}

Status UbMemMemory::Unimport(const std::vector<ShareHandleInfo> &handles) {
  return impl_->Unimport(handles);
}

void UbMemMemory::Finalize() {
  impl_->Finalize();
}

std::shared_ptr<const UbMemRemoteIndex> UbMemMemory::GetTranslationIndex() const {
  return impl_->GetTranslationIndex();
}

UbMemMemory::Impl::RemoteMemory::~RemoteMemory() {
  Finalize();
}

void UbMemMemory::Impl::RemoteMemory::RollbackImportBatchLocked(uintptr_t local_base,
                                                                const std::vector<uintptr_t> &added_vas) {
  for (const uintptr_t local_va : added_vas) {
    const auto it = va_mappings_.find(local_va);
    if (it == va_mappings_.end()) {
      continue;
    }
    ReleaseRangeLocked(it->second);
    va_mappings_.erase(it);
  }
  const auto block_it = imported_blocks_.find(local_base);
  if (block_it != imported_blocks_.end()) {
    (void)VirtualMemoryManager::GetInstance().ReleaseMemory(local_base);
    imported_blocks_.erase(block_it);
  }
}

Status UbMemMemory::Impl::RemoteMemory::ImportOneRangeLocked(const ShareHandleInfo &info, uintptr_t peer_base,
                                                             uintptr_t local_base, std::vector<uintptr_t> &added_vas) {
  const uintptr_t local_va = local_base + (info.va_addr - peer_base);
  HIXL_CHK_BOOL_RET_STATUS(va_mappings_.find(local_va) == va_mappings_.end(), PARAM_INVALID,
                           "Duplicate remote fabric mem mapping, va:%lu.", local_va);
  aclrtDrvMemHandle remote_pa_handle = nullptr;
  HIXL_DISMISSABLE_GUARD(pa_guard, ([&remote_pa_handle]() {
                           if (remote_pa_handle != nullptr) {
                             HIXL_CHK_ACL(aclrtFreePhysical(remote_pa_handle),
                                          "Free imported remote pa handle failed.");
                           }
                         }));
  auto share_handle = info.share_handle;
  HIXL_CHK_ACL_RET(
      aclrtMemImportFromShareableHandleV2(&share_handle, ACL_MEM_SHARE_HANDLE_TYPE_FABRIC, 0U, &remote_pa_handle),
      "Import remote fabric share handle failed.");
  HIXL_CHK_ACL_RET(aclrtMapMem(reinterpret_cast<void *>(local_va), info.len, 0, remote_pa_handle, 0),
                   "Map remote imported memory failed.");
  // Ownership of the PA handle moves into the table here; Unimport or Finalize releases it from
  // there, so the rollback guard below must not free it again.
  va_mappings_[local_va] = RemoteRange{info, local_va, /*imported=*/true, remote_pa_handle, local_base};
  added_vas.push_back(local_va);
  HIXL_DISMISS_GUARD(pa_guard);
  HIXL_LOGI("Imported remote fabric mem, old va:%lu, mapped va:%lu, len:%zu, handle:%p.", info.va_addr, local_va,
            info.len, remote_pa_handle);
  return SUCCESS;
}

Status UbMemMemory::Impl::RemoteMemory::Import(const std::vector<ShareHandleInfo> &remote_share_handles) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (remote_share_handles.empty()) {
    return SUCCESS;
  }

  // One export covers a contiguous peer address range, but its physical backing may be several PA blocks.
  // Reserve that span once and map every block at its peer offset, so the mapped addresses stay contiguous:
  // the offset the peer used still holds locally, and one user op needs one descriptor.
  uintptr_t peer_base = 0;
  size_t span = 0;
  HIXL_CHK_STATUS_RET(ComputeRemoteSpan(remote_share_handles, peer_base, span), "Remote span is invalid.");
  uintptr_t local_base = 0;
  HIXL_CHK_STATUS_RET(VirtualMemoryManager::GetInstance().ReserveMemory(span, local_base),
                      "Reserve memory for remote share handles failed.");
  // Roll back only this batch on failure so previously imported mappings stay intact.
  imported_blocks_[local_base] = ImportedBlock{span, remote_share_handles.size()};
  std::vector<uintptr_t> added_vas;
  added_vas.reserve(remote_share_handles.size());
  HIXL_DISMISSABLE_GUARD(batch_guard,
                         ([this, local_base, &added_vas]() { RollbackImportBatchLocked(local_base, added_vas); }));

  for (const auto &info : remote_share_handles) {
    HIXL_CHK_STATUS_RET(ImportOneRangeLocked(info, peer_base, local_base, added_vas),
                        "Import remote fabric mem range failed, va:%lu.", info.va_addr);
  }
  RebuildTranslationIndexLocked();
  HIXL_DISMISS_GUARD(batch_guard);
  return SUCCESS;
}

Status UbMemMemory::Impl::RemoteMemory::BindLocalAddrs(const std::vector<ShareHandleInfo> &remote_share_handles) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (remote_share_handles.empty()) {
    return SUCCESS;
  }
  for (const auto &info : remote_share_handles) {
    va_mappings_[info.va_addr] = RemoteRange{info, info.va_addr, /*imported=*/false, nullptr, 0U};
  }
  RebuildTranslationIndexLocked();
  HIXL_LOGI("Bind local fabric mem without import, handle_count:%zu, va:%lu, len:%zu.", remote_share_handles.size(),
            remote_share_handles.front().va_addr, remote_share_handles.front().len);
  return SUCCESS;
}

void UbMemMemory::Impl::RemoteMemory::RebuildTranslationIndexLocked() {
  std::vector<const RemoteRange *> ranges;
  ranges.reserve(va_mappings_.size());
  for (const auto &mapping : va_mappings_) {
    ranges.push_back(&mapping.second);
  }
  std::sort(ranges.begin(), ranges.end(),
            [](const RemoteRange *lhs, const RemoteRange *rhs) { return lhs->info.va_addr < rhs->info.va_addr; });

  auto index = std::make_shared<UbMemRemoteIndex>();
  for (const auto *range : ranges) {
    if (!index->empty()) {
      const auto last = std::prev(index->end());
      // Ranges that are adjacent on both sides keep that adjacency after the rewrite, so they collapse
      // into one entry and a transfer spanning them is not split. Identity binds and imports are never
      // merged: their local addresses live in different address spaces that may happen to coincide.
      const bool peer_adjacent = last->first + last->second.len == range->info.va_addr;
      const bool local_adjacent = last->second.va_addr + last->second.len == range->local_va;
      if (peer_adjacent && local_adjacent && last->second.imported == range->imported) {
        last->second.len += range->info.len;
        continue;
      }
    }
    index->emplace(range->info.va_addr, VaInfo{range->local_va, range->info.len, range->imported});
  }
  translation_index_ = std::move(index);
}

void UbMemMemory::Impl::RemoteMemory::ReleaseRangeLocked(RemoteRange &range) {
  if (!range.imported) {
    // Identity-bound local address: nothing was mapped or reserved here, so nothing to release.
    return;
  }
  HIXL_LOGI("Unmap remote fabric mem:%lu.", range.local_va);
  HIXL_CHK_ACL(aclrtUnmapMem(reinterpret_cast<void *>(range.local_va)), "Unmap remote fabric mem failed.");
  if (range.pa_handle != nullptr) {
    HIXL_CHK_ACL(aclrtFreePhysical(range.pa_handle), "Free imported remote pa handle failed.");
    HIXL_LOGI("Free imported remote handle:%p.", range.pa_handle);
    range.pa_handle = nullptr;
  }
  // The reserved block is shared by the whole Import batch, so it goes back to the VMM only after the
  // last range mapped into it is gone.
  const auto it = imported_blocks_.find(range.import_base);
  if (it == imported_blocks_.end()) {
    return;
  }
  if (it->second.live_ranges > 0U) {
    --it->second.live_ranges;
  }
  if (it->second.live_ranges == 0U) {
    HIXL_LOGI("Release remote fabric mem block, va:%lu, len:%zu.", it->first, it->second.len);
    (void)VirtualMemoryManager::GetInstance().ReleaseMemory(it->first);
    imported_blocks_.erase(it);
  }
}

size_t UbMemMemory::Impl::RemoteMemory::ReleaseByHandleLocked(const ShareHandleInfo &handle) {
  size_t released = 0U;
  for (auto it = va_mappings_.begin(); it != va_mappings_.end();) {
    if (!IsSameExportedRange(it->second.info, handle)) {
      ++it;
      continue;
    }
    ReleaseRangeLocked(it->second);
    it = va_mappings_.erase(it);
    ++released;
  }
  return released;
}

Status UbMemMemory::Impl::RemoteMemory::Unimport(const std::vector<ShareHandleInfo> &handles) {
  std::lock_guard<std::mutex> lock(mutex_);
  size_t released = 0U;
  size_t missing = 0U;
  for (const auto &handle : handles) {
    const size_t erased = ReleaseByHandleLocked(handle);
    if (erased == 0U) {
      ++missing;
      continue;
    }
    released += erased;
  }
  // Rebuild even when nothing matched: the index stays valid for callers that still hold it, and a
  // later transfer to a released range fails as an unregistered range instead of a stale hit.
  RebuildTranslationIndexLocked();
  if (missing != 0U) {
    HIXL_LOGW("Unimport remote fabric mem, %zu of %zu handle(s) are not mapped here, released:%zu, remaining:%zu.",
              missing, handles.size(), released, va_mappings_.size());
    return SUCCESS;
  }
  HIXL_LOGI("Unimport remote fabric mem, requested:%zu, released:%zu, remaining:%zu.", handles.size(), released,
            va_mappings_.size());
  return SUCCESS;
}

void UbMemMemory::Impl::RemoteMemory::ClearLocked() {
  translation_index_.reset();
  for (auto &mapping : va_mappings_) {
    ReleaseRangeLocked(mapping.second);
  }
  va_mappings_.clear();
  // A block whose ranges were all released is already gone. Any block left here belongs to an Import
  // that stopped before every range was registered, and still owes the VMM a release.
  for (const auto &block : imported_blocks_) {
    (void)VirtualMemoryManager::GetInstance().ReleaseMemory(block.first);
  }
  imported_blocks_.clear();
}

void UbMemMemory::Impl::RemoteMemory::Finalize() {
  std::lock_guard<std::mutex> lock(mutex_);
  ClearLocked();
}

std::shared_ptr<const UbMemRemoteIndex> UbMemMemory::Impl::RemoteMemory::GetTranslationIndex() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return translation_index_;
}
}  // namespace hixl
