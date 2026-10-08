/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "cs/ubmem/ubmem_allocator.h"

#include <cstring>
#include <map>
#include <mutex>
#include <unordered_map>

#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/scope_guard.h"
#include "cs/ubmem/ubmem_virtual_memory_manager.h"

namespace hixl {
namespace {
constexpr int32_t kDevicesPerChip = 4;
constexpr int32_t kNumaNodeStep = 2;
// aclrtMemSetAccess count: number of aclrtMemAccessDesc entries.
constexpr size_t kMemAccessDescCount = 1U;
// aclsysGetVersionNum encodes runtime 9.2.0 as 90200000. From that version, the caller device id
// is already the id SetAccess expects.
constexpr int32_t kRuntimeVersionSkipLogicIdConvert = 90200000;

bool RuntimeSkipsUserToLogicConvert() {
  if (aclsysGetVersionNum == nullptr) {
    return false;
  }
  char pkg_name[] = "runtime";
  int32_t version_num = 0;
  const int32_t ret = aclsysGetVersionNum(pkg_name, &version_num);
  if (ret != 0) {
    HIXL_LOGW("aclsysGetVersionNum(runtime) failed, ret:%d. Convert user device id before SetAccess.", ret);
    return false;
  }
  return version_num >= kRuntimeVersionSkipLogicIdConvert;
}

// ACL fabric export may be performed only once per physical allocation. MallocMem records va→pa
// without exporting; the first ExportToShareableHandle (from the caller or from RegisterMem) does
// the ACL export and caches the handle for later reuse.
struct AllocationRecord {
  aclrtDrvMemHandle pa_handle = nullptr;
  aclrtMemFabricHandle share_handle{};
  bool exported = false;
};

// Identity of one exported foreign physical block. Keyed by the block base and length returned by
// aclrtMemGetAddressRange, so different user registrations that fall inside the same physical block
// share one exported handle.
struct ForeignShareHandleKey {
  uintptr_t block_addr = 0;
  size_t block_len = 0;
};

bool operator<(const ForeignShareHandleKey &lhs, const ForeignShareHandleKey &rhs) {
  if (lhs.block_addr != rhs.block_addr) {
    return lhs.block_addr < rhs.block_addr;
  }
  return lhs.block_len < rhs.block_len;
}

std::mutex g_allocation_mutex;
std::unordered_map<uintptr_t, AllocationRecord> g_allocations;

// Process-wide cache of foreign block share handles. A cached handle is not a keep-alive reference:
// HIXL never releases an exported share handle explicitly, and the handle stays valid as long as the
// backing physical memory is alive. Physical memory lifetime is owned by the per-registration PA
// handle retain/free refcount of every caller. Entries are intentionally not removed when a
// registration deregisters: the process exported this block once, so later registrations must reuse
// the same handle instead of exporting again. Guarded by g_allocation_mutex.
std::map<ForeignShareHandleKey, aclrtMemFabricHandle> g_foreign_share_handles;

// Exported range identity: the same triple that pairs an import with its unimport.
struct ExportedRangeKey {
  uintptr_t va_addr = 0;
  size_t len = 0;
  aclrtMemFabricHandle share_handle{};
};

bool operator<(const ExportedRangeKey &lhs, const ExportedRangeKey &rhs) {
  const int handle_cmp = memcmp(lhs.share_handle.data, rhs.share_handle.data, sizeof(lhs.share_handle.data));
  if (handle_cmp != 0) {
    return handle_cmp < 0;
  }
  if (lhs.va_addr != rhs.va_addr) {
    return lhs.va_addr < rhs.va_addr;
  }
  return lhs.len < rhs.len;
}

// Ranges this process exported, with the number of registrations holding each of them: one export is
// cached per allocation, so several registrations can share the same range. Guarded by g_allocation_mutex.
std::map<ExportedRangeKey, uint32_t> g_exported_ranges;

ExportedRangeKey MakeExportedRangeKey(const ShareHandleInfo &info) {
  return ExportedRangeKey{info.va_addr, info.len, info.share_handle};
}

void AddAllocation(uintptr_t va_addr, aclrtDrvMemHandle pa_handle) {
  std::lock_guard<std::mutex> lock(g_allocation_mutex);
  g_allocations[va_addr] = AllocationRecord{pa_handle, {}, false};
}

void RemoveAllocation(uintptr_t va_addr) {
  std::lock_guard<std::mutex> lock(g_allocation_mutex);
  g_allocations.erase(va_addr);
}

aclrtPhysicalMemProp BuildDefaultPhysicalMemProp() {
  aclrtPhysicalMemProp prop = {};
  prop.handleType = ACL_MEM_HANDLE_TYPE_NONE;
  prop.allocationType = ACL_MEM_ALLOCATION_TYPE_PINNED;
  prop.reserve = 0;
  return prop;
}

// Host VMM Map only binds the address for the host; grant DEVICE READWRITE so NPU/AIV can access it,
// matching memfabric's HalMemSetAccess after host Map. runtime >= 9.2.0 uses the caller device id.
// Older runtime converts that user id to a driver logical id before SetAccess.
Status SetDeviceAccessForHostMappedVa(void *va_ptr, size_t size, int32_t device_id) {
  int32_t access_device_id = device_id;
  if (!RuntimeSkipsUserToLogicConvert()) {
    HIXL_CHK_ACL_RET(aclrtGetLogicDevIdByUserDevId(device_id, &access_device_id),
                     "Convert user device id to driver logical id failed.");
  }
  aclrtMemAccessDesc desc{};
  desc.flags = ACL_RT_MEM_ACCESS_FLAGS_READWRITE;
  desc.location.type = ACL_MEM_LOCATION_TYPE_DEVICE;
  desc.location.id = static_cast<uint32_t>(access_device_id);
  HIXL_CHK_ACL_RET(aclrtMemSetAccess(va_ptr, size, &desc, kMemAccessDescCount),
                   "Set fabric memory device access failed.");
  return SUCCESS;
}
}  // namespace

Status UbMemAllocator::MallocMem(MemType type, size_t size, void **ptr) {
  HIXL_CHK_BOOL_RET_STATUS(type == MemType::MEM_HOST || type == MemType::MEM_DEVICE, PARAM_INVALID,
                           "Only support malloc host or device fabric memory.");
  HIXL_CHK_BOOL_RET_STATUS(size > 0, PARAM_INVALID, "Fabric memory size should be greater than zero.");
  HIXL_CHECK_NOTNULL(ptr);

  int32_t logic_device_id = 0;
  HIXL_CHK_ACL_RET(aclrtGetDevice(&logic_device_id), "Get current device failed.");

  aclrtDrvMemHandle pa_handle = nullptr;
  uintptr_t virtual_addr = 0;
  HIXL_CHK_STATUS_RET(AllocatePhysicalMemory(type, size, logic_device_id, pa_handle),
                      "Failed to allocate physical memory.");
  HIXL_DISMISSABLE_GUARD(
      free_pa_guard, ([&pa_handle]() { HIXL_CHK_ACL(aclrtFreePhysical(pa_handle), "Free physical memory failed."); }));
  HIXL_CHK_STATUS_RET(VirtualMemoryManager::GetInstance().ReserveMemory(size, virtual_addr),
                      "Failed to reserve virtual memory.");
  HIXL_DISMISSABLE_GUARD(
      release_va_guard, ([&virtual_addr]() { (void)VirtualMemoryManager::GetInstance().ReleaseMemory(virtual_addr); }));
  const auto va_ptr = reinterpret_cast<void *>(virtual_addr);
  HIXL_CHK_ACL_RET(aclrtMapMem(va_ptr, size, 0, pa_handle, 0), "Map fabric memory failed.");
  HIXL_DISMISSABLE_GUARD(unmap_guard,
                         ([va_ptr]() { HIXL_CHK_ACL(aclrtUnmapMem(va_ptr), "Unmap fabric memory failed."); }));
  if (type == MemType::MEM_HOST) {
    HIXL_CHK_STATUS_RET(SetDeviceAccessForHostMappedVa(va_ptr, size, logic_device_id),
                        "Failed to set device access for host fabric memory.");
  }

  AddAllocation(virtual_addr, pa_handle);
  *ptr = va_ptr;
  HIXL_DISMISS_GUARD(unmap_guard);
  HIXL_DISMISS_GUARD(release_va_guard);
  HIXL_DISMISS_GUARD(free_pa_guard);
  HIXL_LOGI("MallocUbMemory success, va:%lu, size:%zu.", virtual_addr, size);
  return SUCCESS;
}

Status UbMemAllocator::FreeMem(void *ptr) {
  HIXL_CHK_BOOL_RET_STATUS(ptr != nullptr, PARAM_INVALID, "Fabric memory address cannot be nullptr.");
  const auto va_addr = reinterpret_cast<uintptr_t>(ptr);
  aclrtDrvMemHandle pa_handle = nullptr;
  HIXL_CHK_STATUS_RET(GetPaHandleFromVa(va_addr, pa_handle), "Failed to get physical memory handle.");

  RemoveAllocation(va_addr);
  HIXL_CHK_ACL(aclrtUnmapMem(ptr), "Unmap fabric memory failed.");
  (void)VirtualMemoryManager::GetInstance().ReleaseMemory(va_addr);
  HIXL_CHK_ACL(aclrtFreePhysical(pa_handle), "Free physical memory failed.");
  HIXL_LOGI("FreeUbMemory success, va:%lu.", va_addr);
  return SUCCESS;
}

Status UbMemAllocator::AllocatePhysicalMemory(MemType type, size_t total_size, int32_t logic_device_id,
                                              aclrtDrvMemHandle &handle) {
  HIXL_CHK_BOOL_RET_STATUS(type == MemType::MEM_HOST || type == MemType::MEM_DEVICE, PARAM_INVALID,
                           "Invalid fabric memory type:%d.", static_cast<int32_t>(type));
  aclrtPhysicalMemProp prop = BuildDefaultPhysicalMemProp();
  if (type == MemType::MEM_DEVICE) {
    prop.memAttr = ACL_HBM_MEM_HUGE;
    prop.location.type = ACL_MEM_LOCATION_TYPE_DEVICE;
    prop.location.id = logic_device_id;
    HIXL_CHK_ACL_RET(aclrtMallocPhysical(&handle, total_size, &prop, 0), "Allocate device physical memory failed.");
    return SUCCESS;
  }

  int32_t physical_device_id = 0;
  HIXL_CHK_ACL_RET(aclrtGetPhyDevIdByLogicDevId(logic_device_id, &physical_device_id),
                   "Get physical device id failed.");
  prop.memAttr = ACL_MEM_P2P_HUGE1G;
  prop.location.type = ACL_MEM_LOCATION_TYPE_HOST_NUMA;
  prop.location.id = (physical_device_id / kDevicesPerChip) * kNumaNodeStep;
  HIXL_LOGI("Malloc host memory for numa:%d.", prop.location.id);
  auto ret = aclrtMallocPhysical(&handle, total_size, &prop, 0);
  if (ret == ACL_ERROR_NONE) {
    return SUCCESS;
  }
  HIXL_LOGI("Try common host allocation instead of numa:%d.", prop.location.id);
  prop.location.type = ACL_MEM_LOCATION_TYPE_HOST;
  prop.location.id = 0;
  ret = aclrtMallocPhysical(&handle, total_size, &prop, 0);
  if (ret == ACL_ERROR_NONE) {
    return SUCCESS;
  }
  HIXL_LOGI("Try smaller page instead of 1G page.");
  prop.memAttr = ACL_MEM_P2P_HUGE;
  HIXL_CHK_ACL_RET(aclrtMallocPhysical(&handle, total_size, &prop, 0), "Allocate physical memory failed.");
  return SUCCESS;
}

Status UbMemAllocator::GetPaHandleFromVa(uintptr_t va_addr, aclrtDrvMemHandle &pa_handle) {
  std::lock_guard<std::mutex> lock(g_allocation_mutex);
  const auto it = g_allocations.find(va_addr);
  if (it == g_allocations.end()) {
    return FAILED;
  }
  pa_handle = it->second.pa_handle;
  return SUCCESS;
}

bool UbMemAllocator::IsAllocatedByMallocMem(uintptr_t va_addr) {
  std::lock_guard<std::mutex> lock(g_allocation_mutex);
  return g_allocations.find(va_addr) != g_allocations.end();
}

Status UbMemAllocator::ExportToShareableHandle(uintptr_t va_addr, aclrtMemFabricHandle &share_handle) {
  std::lock_guard<std::mutex> lock(g_allocation_mutex);
  const auto it = g_allocations.find(va_addr);
  HIXL_CHK_BOOL_RET_STATUS(it != g_allocations.end(), PARAM_INVALID,
                           "Address:%lu was not allocated by fabric MallocMem or is already freed.", va_addr);
  if (it->second.exported) {
    share_handle = it->second.share_handle;
    return SUCCESS;
  }
  HIXL_CHK_ACL_RET(
      aclrtMemExportToShareableHandleV2(it->second.pa_handle, ACL_RT_VMM_EXPORT_FLAG_DISABLE_PID_VALIDATION,
                                        ACL_MEM_SHARE_HANDLE_TYPE_FABRIC, &share_handle),
      "Export fabric share handle failed.");
  it->second.share_handle = share_handle;
  it->second.exported = true;
  return SUCCESS;
}

Status UbMemAllocator::GetOrExportForeignShareHandle(uintptr_t block_addr, size_t block_len,
                                                     aclrtDrvMemHandle pa_handle, aclrtMemFabricHandle &share_handle) {
  std::lock_guard<std::mutex> lock(g_allocation_mutex);
  const ForeignShareHandleKey key{block_addr, block_len};
  bool found = false;
  if (g_exported_ranges.count(ExportedRangeKey{block_addr, block_len, {}}) == 0U) {
    const auto cached = g_foreign_share_handles.find(key);
    if (cached != g_foreign_share_handles.end()) {
      share_handle = cached->second;
      found = true;
    }
  }
  if (found) {
    return SUCCESS;
  }
  HIXL_CHK_ACL_RET(aclrtMemExportToShareableHandleV2(pa_handle, ACL_RT_VMM_EXPORT_FLAG_DISABLE_PID_VALIDATION,
                                                     ACL_MEM_SHARE_HANDLE_TYPE_FABRIC, &share_handle),
                   "Export foreign fabric share handle failed, block_addr:%lu, block_len:%zu.", block_addr, block_len);
  // Only a successful export is cached. A failed export leaves no entry so a later retry can export
  // again. The cache stores the handle bytes only and holds no PA reference.
  g_foreign_share_handles.emplace(key, share_handle);
  return SUCCESS;
}

void UbMemAllocator::RecordExportedRange(const ShareHandleInfo &info) {
  std::lock_guard<std::mutex> lock(g_allocation_mutex);
  ++g_exported_ranges[MakeExportedRangeKey(info)];
}

void UbMemAllocator::ReleaseExportedRange(const ShareHandleInfo &info) {
  std::lock_guard<std::mutex> lock(g_allocation_mutex);
  const auto it = g_exported_ranges.find(MakeExportedRangeKey(info));
  if (it == g_exported_ranges.end()) {
    return;
  }
  if (it->second > 0U) {
    --it->second;
  }
  if (it->second == 0U) {
    // The last registration for this physical block is gone; the backing memory may be freed and
    // later reused at the same address. Drop the cached share handle so the next registration
    // exports a fresh handle instead of returning one tied to freed memory.
    const ForeignShareHandleKey foreign_key{it->first.va_addr, it->first.len};
    g_exported_ranges.erase(it);
    g_foreign_share_handles.erase(foreign_key);
  }
}

bool UbMemAllocator::IsRangeExportedHere(const ShareHandleInfo &info) {
  std::lock_guard<std::mutex> lock(g_allocation_mutex);
  return g_exported_ranges.find(MakeExportedRangeKey(info)) != g_exported_ranges.end();
}

void UbMemAllocator::ResetForeignShareHandleCacheForTest() {
  std::lock_guard<std::mutex> lock(g_allocation_mutex);
  g_foreign_share_handles.clear();
}
}  // namespace hixl
