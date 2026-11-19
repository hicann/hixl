/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "cs/ubmem/ubmem_virtual_memory_manager.h"

#include <algorithm>
#include <utility>

#include "acl/acl.h"
#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/hixl_utils.h"
#include "common/scope_guard.h"
#include "common/soc_type.h"
#include "cs/ubmem/acl_compat.h"
#include "cs/ubmem/ubmem_types.h"

namespace hixl {
namespace {
constexpr size_t kBlockSize = 1024UL * 1024UL * 1024UL;
constexpr size_t kDefaultNumBlocks = 32UL * 1024UL;
constexpr size_t kDefaultGlobalVirtualMemorySize = kBlockSize * kDefaultNumBlocks;
constexpr uintptr_t kGlobalVirtualMemoryStartAddr = kBlockSize * 1024UL * 40UL;
constexpr uint64_t kReserveFlagHugePage = 1UL;
constexpr size_t kBytesPerTB = 1024UL * 1024UL * 1024UL * 1024UL;
constexpr size_t kFallbackChunkSize = 2UL * kBytesPerTB;
// On A3 (Gen2 / SVM v2), default dynamic VA starts at 40 TB and older drivers truncate heap_idx to u16 when VA >= 80
// TB.
constexpr uintptr_t kGen2SafeVaUpperBound = 80UL * kBytesPerTB;

bool ExceedsVaUpperBound(void *ptr, size_t size, uintptr_t upper_bound) {
  if (ptr == nullptr) {
    return true;
  }
  const uintptr_t va = reinterpret_cast<uintptr_t>(ptr);
  return size > upper_bound || va > upper_bound - size;
}

Status CheckVaUpperBoundAndRelease(void *&virtual_address, size_t size, uintptr_t va_upper_bound) {
  if (va_upper_bound == 0U || !ExceedsVaUpperBound(virtual_address, size, va_upper_bound)) {
    return SUCCESS;
  }
  HIXL_LOGW("Reserved virtual memory exceeds upper bound, va:%p, size:%zu bytes, bound:%lu bytes; releasing.",
            virtual_address, size, va_upper_bound);
  const auto release_ret = aclrtReleaseMemAddress(virtual_address);
  if (release_ret == ACL_SUCCESS) {
    virtual_address = nullptr;
  } else {
    HIXL_CHK_ACL(release_ret, "Release out-of-range virtual memory failed.");
  }
  return FAILED;
}

Status ReserveSingleVaBlock(void *&virtual_address, size_t size, void *expect_ptr, SocType soc_type,
                            uintptr_t va_upper_bound, bool log_error) {
  if (soc_type == SocType::kGen2 && &aclrtReserveMemAddressNoUCMemory != nullptr) {
    const auto ret = aclrtReserveMemAddressNoUCMemory(&virtual_address, size, 0, expect_ptr, kReserveFlagHugePage);
    if (ret == ACL_SUCCESS) {
      if (CheckVaUpperBoundAndRelease(virtual_address, size, va_upper_bound) != SUCCESS) {
        if (log_error) {
          HIXL_REPORT_ERR_MSG("E19999", "Reserved VA exceeded upper bound, size:%zu bytes.", size);
          HIXL_LOGE(FAILED, "Reserved VA exceeded upper bound, size:%zu bytes.", size);
        }
        return FAILED;
      }
      HIXL_LOGI("Reserve virtual memory without UC memory, va:%p, size:%zu bytes.", virtual_address, size);
      return SUCCESS;
    }
    virtual_address = nullptr;
    if (ret != ACL_ERROR_RT_FEATURE_NOT_SUPPORT) {
      if (log_error) {
        HIXL_REPORT_ERR_MSG("E19999",
                            "Call api:aclrtReserveMemAddressNoUCMemory failed, ret:%d, expect:%p, size:%zu bytes", ret,
                            expect_ptr, size);
        HIXL_LOGE(FAILED, "Call api:aclrtReserveMemAddressNoUCMemory failed, ret:%d, expect:%p, size:%zu bytes", ret,
                  expect_ptr, size);
      } else {
        HIXL_LOGW("Call api:aclrtReserveMemAddressNoUCMemory failed, ret:%d, expect:%p, size:%zu bytes.", ret,
                  expect_ptr, size);
      }
      return FAILED;
    }
  }
  // aclrtReserveMemAddress only supports nullptr or [16 TB, 24 TB) on SVM v2 (and nullptr only on newer SoCs);
  // never forward >= 24 TB dynamic-window hints here.
  const auto ret = aclrtReserveMemAddress(&virtual_address, size, 0, nullptr, kReserveFlagHugePage);
  if (ret != ACL_SUCCESS) {
    virtual_address = nullptr;
    if (log_error) {
      HIXL_CHK_ACL_RET(ret, "Reserve virtual memory failed.");
    }
    HIXL_LOGW("Call api:aclrtReserveMemAddress failed, ret:%d, size:%zu bytes.", ret, size);
    return FAILED;
  }
  if (CheckVaUpperBoundAndRelease(virtual_address, size, va_upper_bound) != SUCCESS) {
    if (log_error) {
      HIXL_REPORT_ERR_MSG("E19999", "Reserved VA exceeded upper bound, size:%zu bytes.", size);
      HIXL_LOGE(FAILED, "Reserved VA exceeded upper bound, size:%zu bytes.", size);
    }
    return FAILED;
  }
  HIXL_LOGI("Reserve virtual memory with aclrtReserveMemAddress, va:%p, size:%zu bytes.", virtual_address, size);
  return SUCCESS;
}
}  // namespace

VirtualMemoryManager &VirtualMemoryManager::GetInstance() {
  static VirtualMemoryManager instance;
  return instance;
}

VirtualMemoryManager::~VirtualMemoryManager() {
  Finalize();
}

Status VirtualMemoryManager::SetVirtualMemoryCapacity(size_t capacity_in_tb) {
  std::lock_guard<std::mutex> lock(global_virtual_memory_mutex_);
  const size_t requested = capacity_in_tb * kBytesPerTB;
  if (initialized_) {
    HIXL_CHK_BOOL_RET_STATUS(vm_size_ == requested, PARAM_INVALID,
                             "VirtualMemoryManager already initialized, cannot set capacity.");
    return SUCCESS;
  }
  vm_size_ = requested;
  num_blocks_ = vm_size_ / kBlockSize;
  HIXL_LOGI("Set virtual memory capacity to %zu TB, bytes:%zu, blocks:%zu.", capacity_in_tb, vm_size_, num_blocks_);
  return SUCCESS;
}

Status VirtualMemoryManager::SetGlobalStartAddress(size_t start_addr_in_tb) {
  std::lock_guard<std::mutex> lock(global_virtual_memory_mutex_);
  HIXL_CHK_BOOL_RET_STATUS(start_addr_in_tb <= kMaxUbMemStartAddrTB, PARAM_INVALID,
                           "global start address must be in [%zu, %zu] TB, got %zu.", kMinUbMemStartAddrTB,
                           kMaxUbMemStartAddrTB, start_addr_in_tb);
  const uintptr_t requested = start_addr_in_tb * kBytesPerTB;
  if (initialized_) {
    HIXL_CHK_BOOL_RET_STATUS(global_start_va_.value_or(kGlobalVirtualMemoryStartAddr) == requested, PARAM_INVALID,
                             "VirtualMemoryManager already initialized, cannot set global start address.");
    return SUCCESS;
  }
  global_start_va_ = requested;
  HIXL_LOGI("Set global virtual memory start address to %zu TB, bytes:%lu.", start_addr_in_tb, requested);
  return SUCCESS;
}

Status VirtualMemoryManager::ReserveMemAddress(void *&virtual_address, size_t size) const {
  const uintptr_t start_va = global_start_va_.value_or(kGlobalVirtualMemoryStartAddr);
  void *const global_start_va = reinterpret_cast<void *>(start_va);
  SocType soc_type = SocType::kDefault;
  HIXL_CHK_STATUS_RET(SocTypeUtil::GetSocType(soc_type), "ReserveMemAddress GetSocType failed");
  return ReserveSingleVaBlock(virtual_address, size, global_start_va, soc_type, /*va_upper_bound=*/0U,
                              /*log_error=*/true);
}

Status VirtualMemoryManager::TryAppendRegion(size_t size, void *expect_ptr, SocType soc_type, uintptr_t va_upper_bound,
                                             bool log_error) {
  void *raw_ptr = nullptr;
  if (ReserveSingleVaBlock(raw_ptr, size, expect_ptr, soc_type, va_upper_bound, log_error) != SUCCESS) {
    if (raw_ptr != nullptr) {
      pending_release_vas_.push_back(raw_ptr);
    }
    return FAILED;
  }
  ReservedRegion region;
  region.raw_ptr = raw_ptr;
  region.base_addr = reinterpret_cast<uintptr_t>(raw_ptr);
  region.size = size;
  region.num_blocks = size / kBlockSize;
  region.bitmap.assign(region.num_blocks, false);
  regions_.emplace_back(std::move(region));
  return SUCCESS;
}

Status VirtualMemoryManager::ReserveConfiguredRegions(SocType soc_type) {
  const uintptr_t start_va = *global_start_va_;
  if (start_va == 0U) {
    HIXL_CHK_STATUS_RET(ReserveAutoAddressRegions(soc_type), "Failed to reserve auto-addressed virtual memory.");
    if (soc_type == SocType::kGen2) {
      for (const auto &region : regions_) {
        if (ExceedsVaUpperBound(region.raw_ptr, region.size, kGen2SafeVaUpperBound)) {
          HIXL_EVENT(
              "Explicit fabric_memory.start_address=0 reserved VA exceeding default 80 TB safe limit on A3, "
              "va:%p, size:%zu bytes; ensure the driver supports VA >= 80 TB.",
              region.raw_ptr, region.size);
        }
      }
    }
    return SUCCESS;
  }
  void *const expect_ptr = reinterpret_cast<void *>(start_va);
  if (soc_type == SocType::kGen2 && ExceedsVaUpperBound(expect_ptr, vm_size_, kGen2SafeVaUpperBound)) {
    HIXL_EVENT(
        "Explicit fabric_memory.start_address=%lu bytes with capacity=%zu bytes exceeds default 80 TB safe limit on "
        "A3; ensure the driver supports VA >= 80 TB.",
        start_va, vm_size_);
  }
  return TryAppendRegion(vm_size_, expect_ptr, soc_type, /*va_upper_bound=*/0U, /*log_error=*/true);
}

Status VirtualMemoryManager::ReserveBoundedWindowRegions() {
  constexpr size_t kMaxSafeWindowSize = kGen2SafeVaUpperBound - kGlobalVirtualMemoryStartAddr;
  HIXL_CHK_BOOL_RET_STATUS(
      vm_size_ <= kMaxSafeWindowSize, PARAM_INVALID,
      "Requested virtual memory capacity %zu bytes exceeds default safe [40 TB, 80 TB) window (%zu bytes); "
      "set fabric_memory.start_address explicitly if the driver supports VA >= 80 TB.",
      vm_size_, kMaxSafeWindowSize);
  if (TryAppendRegion(vm_size_, reinterpret_cast<void *>(kGlobalVirtualMemoryStartAddr), SocType::kGen2,
                      kGen2SafeVaUpperBound, /*log_error=*/false) == SUCCESS) {
    return SUCCESS;
  }
  HIXL_LOGW(
      "Full virtual memory reserve at default 40 TB failed, size:%zu bytes; falling back to 2 TB chunked reserve in "
      "[40 TB, 80 TB).",
      vm_size_);
  return ReserveChunkedWindowRegions();
}

Status VirtualMemoryManager::ReserveChunkedWindowRegions() {
  size_t remaining = vm_size_;
  uintptr_t candidate =
      (vm_size_ <= kFallbackChunkSize) ? (kGlobalVirtualMemoryStartAddr + vm_size_) : kGlobalVirtualMemoryStartAddr;
  while (remaining > 0U && candidate < kGen2SafeVaUpperBound) {
    const size_t chunk_size = std::min(kFallbackChunkSize, remaining);
    if (chunk_size > kGen2SafeVaUpperBound - candidate) {
      break;
    }
    if (TryAppendRegion(chunk_size, reinterpret_cast<void *>(candidate), SocType::kGen2, kGen2SafeVaUpperBound,
                        /*log_error=*/false) == SUCCESS) {
      remaining -= chunk_size;
    }
    candidate += chunk_size;
  }
  HIXL_CHK_BOOL_RET_STATUS(remaining == 0U, FAILED,
                           "Failed to reserve %zu bytes in [40 TB, 80 TB) window, remaining:%zu bytes.", vm_size_,
                           remaining);
  return SUCCESS;
}

Status VirtualMemoryManager::ReserveAutoAddressRegions(SocType soc_type) {
  const bool can_split = vm_size_ > kFallbackChunkSize;
  if (TryAppendRegion(vm_size_, nullptr, soc_type, /*va_upper_bound=*/0U, /*log_error=*/!can_split) == SUCCESS) {
    return SUCCESS;
  }
  if (!can_split) {
    return FAILED;
  }
  HIXL_LOGW("Full virtual memory reserve failed, size:%zu bytes; falling back to 2 TB chunks.", vm_size_);
  return ReserveChunkedAutoRegions(soc_type);
}

Status VirtualMemoryManager::ReserveChunkedAutoRegions(SocType soc_type) {
  size_t remaining = vm_size_;
  while (remaining > 0U) {
    const size_t chunk_size = std::min(kFallbackChunkSize, remaining);
    HIXL_CHK_STATUS_RET(TryAppendRegion(chunk_size, nullptr, soc_type, /*va_upper_bound=*/0U, /*log_error=*/true),
                        "Failed to reserve virtual memory chunk, chunk_size:%zu bytes, remaining:%zu bytes.",
                        chunk_size, remaining);
    remaining -= chunk_size;
  }
  return SUCCESS;
}

void VirtualMemoryManager::ReleaseAllRegions() {
  std::vector<void *> remaining_pending;
  for (void *va : pending_release_vas_) {
    if (va == nullptr) {
      continue;
    }
    const auto ret = aclrtReleaseMemAddress(va);
    if (ret != ACL_SUCCESS) {
      HIXL_CHK_ACL(ret, "Retry release pending virtual memory failed.");
      remaining_pending.push_back(va);
    }
  }
  pending_release_vas_.swap(remaining_pending);

  for (auto &region : regions_) {
    if (region.raw_ptr != nullptr) {
      const auto ret = aclrtReleaseMemAddress(region.raw_ptr);
      if (ret == ACL_SUCCESS) {
        region.raw_ptr = nullptr;
      } else {
        HIXL_CHK_ACL(ret, "Release global virtual memory failed.");
        pending_release_vas_.push_back(region.raw_ptr);
      }
    }
  }
  regions_.clear();
}

Status VirtualMemoryManager::Initialize() {
  std::lock_guard<std::mutex> lock(global_virtual_memory_mutex_);
  if (!initialized_) {
    HIXL_CHK_STATUS_RET(InitProcess(), "Failed to initialize virtual memory process.");
  }
  return SUCCESS;
}

Status VirtualMemoryManager::InitProcess() {
  if (vm_size_ == 0) {
    vm_size_ = kDefaultGlobalVirtualMemorySize;
    num_blocks_ = kDefaultNumBlocks;
  }
  SocType soc_type = SocType::kDefault;
  HIXL_CHK_STATUS_RET(SocTypeUtil::GetSocType(soc_type), "InitProcess GetSocType failed.");
  HIXL_DISMISSABLE_GUARD(rollback_guard, ([this]() { ReleaseAllRegions(); }));

  if (global_start_va_.has_value()) {
    HIXL_CHK_STATUS_RET(ReserveConfiguredRegions(soc_type), "Failed to reserve configured virtual memory.");
  } else if (soc_type == SocType::kGen2) {
    HIXL_CHK_STATUS_RET(ReserveBoundedWindowRegions(), "Failed to reserve bounded-window virtual memory.");
  } else {
    HIXL_CHK_STATUS_RET(ReserveAutoAddressRegions(soc_type), "Failed to reserve virtual memory.");
  }

  allocations_.clear();
  initialized_ = true;
  HIXL_DISMISS_GUARD(rollback_guard);
  HIXL_LOGI("VirtualMemoryManager initialized, region_count:%zu, total_size:%zu bytes.", regions_.size(), vm_size_);
  return SUCCESS;
}

void VirtualMemoryManager::Finalize() {
  std::lock_guard<std::mutex> lock(global_virtual_memory_mutex_);
  vm_size_ = 0;
  num_blocks_ = 0;
  global_start_va_.reset();
  allocations_.clear();
  initialized_ = false;
  ReleaseAllRegions();
}

bool VirtualMemoryManager::FindFreeBlocksInRegion(const ReservedRegion &region, size_t blocks_needed,
                                                  size_t &start_block) {
  if (blocks_needed > region.num_blocks) {
    return false;
  }
  size_t contiguous_free = 0;
  for (size_t i = 0; i < region.num_blocks; ++i) {
    if (region.bitmap[i]) {
      contiguous_free = 0;
      continue;
    }
    if (contiguous_free == 0) {
      start_block = i;
    }
    ++contiguous_free;
    if (contiguous_free >= blocks_needed) {
      return true;
    }
  }
  return false;
}

size_t VirtualMemoryManager::FindRegionIndex(uintptr_t mem_addr) const {
  for (size_t i = 0; i < regions_.size(); ++i) {
    const auto &region = regions_[i];
    if (mem_addr >= region.base_addr && mem_addr - region.base_addr < region.size) {
      return i;
    }
  }
  return regions_.size();
}

Status VirtualMemoryManager::ReserveMemory(size_t size, uintptr_t &mem_addr) {
  std::lock_guard<std::mutex> lock(global_virtual_memory_mutex_);
  HIXL_CHK_BOOL_RET_STATUS(size > 0, PARAM_INVALID, "ReserveMemory size cannot be zero.");
  if (!initialized_) {
    HIXL_CHK_STATUS_RET(InitProcess(), "Failed to initialize virtual memory process.");
  }
  // Use ceil-division instead of (size + kBlockSize - 1) so a size close to SIZE_MAX cannot wrap around and yield a
  // tiny blocks_needed that bypasses the capacity check below (which would later allow an out-of-bounds access).
  const size_t blocks_needed = size / kBlockSize + ((size % kBlockSize != 0U) ? 1UL : 0UL);
  HIXL_CHK_BOOL_RET_STATUS(blocks_needed <= num_blocks_, RESOURCE_EXHAUSTED,
                           "Requested size %zu exceeds virtual memory capacity.", size);

  for (size_t r = 0; r < regions_.size(); ++r) {
    size_t start_block = 0;
    if (!FindFreeBlocksInRegion(regions_[r], blocks_needed, start_block)) {
      continue;
    }
    for (size_t i = start_block; i < start_block + blocks_needed; ++i) {
      regions_[r].bitmap[i] = true;
    }
    mem_addr = regions_[r].base_addr + start_block * kBlockSize;
    allocations_[mem_addr] = AllocationRecord{r, start_block, blocks_needed};
    HIXL_LOGI("Reserved %zu bytes, blocks:%zu, address:%lu.", size, blocks_needed, mem_addr);
    return SUCCESS;
  }

  HIXL_REPORT_ERR_MSG("E19999", "Insufficient contiguous virtual memory blocks, needed:%zu.", blocks_needed);
  HIXL_LOGE(RESOURCE_EXHAUSTED, "Insufficient contiguous virtual memory blocks, needed:%zu.", blocks_needed);
  return RESOURCE_EXHAUSTED;
}

Status VirtualMemoryManager::ReleaseMemory(uintptr_t mem_addr) {
  std::lock_guard<std::mutex> lock(global_virtual_memory_mutex_);
  HIXL_CHK_BOOL_RET_STATUS(initialized_, PARAM_INVALID, "VirtualMemoryManager is not initialized.");
  const size_t region_idx = FindRegionIndex(mem_addr);
  HIXL_CHK_BOOL_RET_STATUS(region_idx < regions_.size(), PARAM_INVALID,
                           "Address %lu is outside managed virtual memory range.", mem_addr);
  const ReservedRegion &region = regions_[region_idx];
  HIXL_CHK_BOOL_RET_STATUS((mem_addr - region.base_addr) % kBlockSize == 0, PARAM_INVALID,
                           "Address %lu is not 1GB aligned.", mem_addr);
  const auto it = allocations_.find(mem_addr);
  HIXL_CHK_BOOL_RET_STATUS(it != allocations_.end(), PARAM_INVALID, "Address %lu is not allocated or already released.",
                           mem_addr);

  const AllocationRecord record = it->second;
  for (size_t i = record.start_block; i < record.start_block + record.num_blocks; ++i) {
    regions_[record.region_index].bitmap[i] = false;
  }
  HIXL_LOGI("Released %zu blocks from address:%lu.", record.num_blocks, mem_addr);
  allocations_.erase(it);
  return SUCCESS;
}
}  // namespace hixl
