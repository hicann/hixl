/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "fabric_mem/fabric_mem_allocator.h"

#include <mutex>
#include <unordered_map>

#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "fabric_mem/virtual_memory_manager.h"

namespace hixl {
namespace {
constexpr int32_t kDevicesPerChip = 4;
constexpr int32_t kNumaNodeStep = 2;

std::mutex g_va_to_pa_mutex;
std::unordered_map<uintptr_t, aclrtDrvMemHandle> g_va_to_pa_handle_map;

void *ValueToPtr(uintptr_t value) {
  return reinterpret_cast<void *>(value);
}

uintptr_t PtrToValue(const void *ptr) {
  return reinterpret_cast<uintptr_t>(ptr);
}
}  // namespace

Status FabricMemAllocator::MallocMem(MemType type, size_t size, void **ptr) {
  HIXL_CHK_BOOL_RET_STATUS(type == MemType::MEM_HOST, PARAM_INVALID, "Only support malloc host fabric memory.");
  HIXL_CHK_BOOL_RET_STATUS(size > 0, PARAM_INVALID, "Fabric memory size should be greater than zero.");
  HIXL_CHECK_NOTNULL(ptr);

  aclrtDrvMemHandle pa_handle = nullptr;
  uintptr_t virtual_addr = 0;
  HIXL_CHK_STATUS_RET(AllocatePhysicalMemory(size, pa_handle), "Failed to allocate physical memory.");
  HIXL_CHK_STATUS_RET(VirtualMemoryManager::GetInstance().ReserveMemory(size, virtual_addr),
                      "Failed to reserve virtual memory.");
  const auto va_ptr = ValueToPtr(virtual_addr);
  auto map_ret = aclrtMapMem(va_ptr, size, 0, pa_handle, 0);
  if (map_ret != ACL_ERROR_NONE) {
    (void)VirtualMemoryManager::GetInstance().ReleaseMemory(virtual_addr);
    HIXL_CHK_ACL(aclrtFreePhysical(pa_handle), "Free physical memory after map failure failed.");
    HIXL_LOGE(static_cast<Status>(map_ret), "aclrtMapMem failed, ret:%d.", map_ret);
    return static_cast<Status>(map_ret);
  }

  AddVaToPaMapping(virtual_addr, pa_handle);
  *ptr = va_ptr;
  HIXL_LOGI("MallocFabricMemory success, va:%lu, size:%zu.", virtual_addr, size);
  return SUCCESS;
}

Status FabricMemAllocator::FreeMem(void *ptr) {
  HIXL_CHK_BOOL_RET_STATUS(ptr != nullptr, PARAM_INVALID, "Fabric memory address cannot be nullptr.");
  const auto va_addr = PtrToValue(ptr);
  aclrtDrvMemHandle pa_handle = nullptr;
  HIXL_CHK_STATUS_RET(GetPaHandleFromVa(va_addr, pa_handle), "Failed to get physical memory handle.");

  RemoveVaToPaMapping(va_addr);
  HIXL_CHK_ACL(aclrtUnmapMem(ptr), "Unmap fabric memory failed.");
  (void)VirtualMemoryManager::GetInstance().ReleaseMemory(va_addr);
  HIXL_CHK_ACL(aclrtFreePhysical(pa_handle), "Free physical memory failed.");
  HIXL_LOGI("FreeFabricMemory success, va:%lu.", va_addr);
  return SUCCESS;
}

Status FabricMemAllocator::AllocatePhysicalMemory(size_t total_size, aclrtDrvMemHandle &handle) {
  int32_t logic_device_id = 0;
  HIXL_CHK_ACL_RET(aclrtGetDevice(&logic_device_id), "Get current device failed.");
  int32_t physical_device_id = 0;
  HIXL_CHK_ACL_RET(aclrtGetPhyDevIdByLogicDevId(logic_device_id, &physical_device_id),
                   "Get physical device id failed.");

  aclrtPhysicalMemProp prop = {};
  prop.handleType = ACL_MEM_HANDLE_TYPE_NONE;
  prop.allocationType = ACL_MEM_ALLOCATION_TYPE_PINNED;
  prop.memAttr = ACL_MEM_P2P_HUGE1G;
  prop.location.type = ACL_MEM_LOCATION_TYPE_HOST_NUMA;
  prop.location.id = (physical_device_id / kDevicesPerChip) * kNumaNodeStep;
  prop.reserve = 0;
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

Status FabricMemAllocator::GetPaHandleFromVa(uintptr_t va_addr, aclrtDrvMemHandle &pa_handle) {
  std::lock_guard<std::mutex> lock(g_va_to_pa_mutex);
  const auto it = g_va_to_pa_handle_map.find(va_addr);
  if (it == g_va_to_pa_handle_map.end()) {
    return FAILED;
  }
  pa_handle = it->second;
  return SUCCESS;
}

void FabricMemAllocator::AddVaToPaMapping(uintptr_t va_addr, aclrtDrvMemHandle pa_handle) {
  std::lock_guard<std::mutex> lock(g_va_to_pa_mutex);
  g_va_to_pa_handle_map[va_addr] = pa_handle;
}

void FabricMemAllocator::RemoveVaToPaMapping(uintptr_t va_addr) {
  std::lock_guard<std::mutex> lock(g_va_to_pa_mutex);
  g_va_to_pa_handle_map.erase(va_addr);
}
}  // namespace hixl
