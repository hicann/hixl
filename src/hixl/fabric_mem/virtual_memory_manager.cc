/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "fabric_mem/virtual_memory_manager.h"

#include <set>
#include <string>

#include "acl/acl.h"
#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "fabric_mem/acl_compat.h"

namespace hixl {
namespace {
constexpr size_t kBlockSize = 1024UL * 1024UL * 1024UL;
constexpr size_t kDefaultNumBlocks = 64UL * 1024UL;
constexpr size_t kDefaultGlobalVirtualMemorySize = kBlockSize * kDefaultNumBlocks;
constexpr size_t kGlobalVirtualMemoryStartAddr = kBlockSize * 1024UL * 40UL;
constexpr uint64_t kReserveFlagHugePage = 1UL;
constexpr size_t kBytesPerTB = 1024UL * 1024UL * 1024UL * 1024UL;
constexpr size_t kMinGlobalStartAddrTB = 40UL;
constexpr size_t kMaxGlobalStartAddrTB = 220UL;

const std::set<std::string> kSocV3 = {"Ascend910_9391", "Ascend910_9381", "Ascend910_9392",
                                      "Ascend910_9382", "Ascend910_9372", "Ascend910_9362"};

bool IsA3Soc() {
  const char *soc_name_cstr = aclrtGetSocName();
  if (soc_name_cstr == nullptr) {
    return false;
  }
  return kSocV3.find(soc_name_cstr) != kSocV3.end();
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
  HIXL_CHK_BOOL_RET_STATUS(start_addr_in_tb >= kMinGlobalStartAddrTB && start_addr_in_tb <= kMaxGlobalStartAddrTB,
                           PARAM_INVALID, "global start address must be in [%zu, %zu] TB, got %zu.",
                           kMinGlobalStartAddrTB, kMaxGlobalStartAddrTB, start_addr_in_tb);
  const uintptr_t requested = start_addr_in_tb * kBytesPerTB;
  if (initialized_) {
    HIXL_CHK_BOOL_RET_STATUS(global_start_va_ == requested, PARAM_INVALID,
                             "VirtualMemoryManager already initialized, cannot set global start address.");
    return SUCCESS;
  }
  global_start_va_ = requested;
  HIXL_LOGI("Set global virtual memory start address to %zu TB, bytes:%lu.", start_addr_in_tb, global_start_va_);
  return SUCCESS;
}

Status VirtualMemoryManager::ReserveMemAddress(void *&virtual_address, size_t size) {
  const uintptr_t start_va = (global_start_va_ != 0) ? global_start_va_ : kGlobalVirtualMemoryStartAddr;
  void *global_start_va = reinterpret_cast<void *>(start_va);
  if (IsA3Soc() && &aclrtReserveMemAddressNoUCMemory != nullptr) {
    auto ret = aclrtReserveMemAddressNoUCMemory(&virtual_address, size, 0, global_start_va, kReserveFlagHugePage);
    if (ret == ACL_ERROR_NONE) {
      HIXL_LOGI("Reserve virtual memory without UC memory on A3, size:%zu.", size);
      return SUCCESS;
    }
    if (ret != ACL_ERROR_RT_FEATURE_NOT_SUPPORT) {
      HIXL_LOGE(FAILED, "aclrtReserveMemAddressNoUCMemory failed, ret:%d.", ret);
      return FAILED;
    }
  }
  HIXL_CHK_ACL_RET(aclrtReserveMemAddress(&virtual_address, size, 0, nullptr, kReserveFlagHugePage),
                   "Reserve virtual memory failed.");
  HIXL_LOGI("Reserve virtual memory with aclrtReserveMemAddress, size:%zu.", size);
  return SUCCESS;
}

Status VirtualMemoryManager::Initialize() {
  std::lock_guard<std::mutex> lock(global_virtual_memory_mutex_);
  if (initialized_) {
    return SUCCESS;
  }
  HIXL_CHK_STATUS_RET(InitProcess(), "Failed to initialize virtual memory process.");
  return SUCCESS;
}

Status VirtualMemoryManager::InitProcess() {
  if (vm_size_ == 0) {
    vm_size_ = kDefaultGlobalVirtualMemorySize;
    num_blocks_ = kDefaultNumBlocks;
  }
  HIXL_CHK_STATUS_RET(ReserveMemAddress(global_virtual_memory_, vm_size_), "Failed to reserve global virtual memory.");
  global_virtual_memory_addr_ = reinterpret_cast<uintptr_t>(global_virtual_memory_);
  bitmap_.assign(num_blocks_, false);
  allocations_.clear();
  initialized_ = true;
  HIXL_LOGI("VirtualMemoryManager initialized, base virtual address:%lu.", global_virtual_memory_addr_);
  return SUCCESS;
}

void VirtualMemoryManager::Finalize() {
  std::lock_guard<std::mutex> lock(global_virtual_memory_mutex_);
  if (!initialized_) {
    return;
  }
  bitmap_.clear();
  allocations_.clear();
  initialized_ = false;
  if (global_virtual_memory_ != nullptr) {
    HIXL_CHK_ACL(aclrtReleaseMemAddress(global_virtual_memory_), "Release global virtual memory failed.");
    global_virtual_memory_ = nullptr;
    global_virtual_memory_addr_ = 0;
  }
}

Status VirtualMemoryManager::ReserveMemory(size_t size, uintptr_t &mem_addr) {
  std::lock_guard<std::mutex> lock(global_virtual_memory_mutex_);
  HIXL_CHK_BOOL_RET_STATUS(size > 0, PARAM_INVALID, "ReserveMemory size cannot be zero.");
  if (!initialized_) {
    HIXL_CHK_STATUS_RET(InitProcess(), "Failed to initialize virtual memory process.");
  }
  const size_t blocks_needed = (size + kBlockSize - 1) / kBlockSize;
  HIXL_CHK_BOOL_RET_STATUS(blocks_needed <= num_blocks_, RESOURCE_EXHAUSTED,
                           "Requested size %zu exceeds virtual memory capacity.", size);

  size_t start_block = 0;
  size_t contiguous_free = 0;
  for (size_t i = 0; i < num_blocks_; ++i) {
    if (!bitmap_[i]) {
      if (contiguous_free == 0) {
        start_block = i;
      }
      ++contiguous_free;
      if (contiguous_free >= blocks_needed) {
        break;
      }
      continue;
    }
    contiguous_free = 0;
  }
  HIXL_CHK_BOOL_RET_STATUS(contiguous_free >= blocks_needed, RESOURCE_EXHAUSTED,
                           "Insufficient contiguous virtual memory blocks, needed:%zu.", blocks_needed);

  for (size_t i = start_block; i < start_block + blocks_needed; ++i) {
    bitmap_[i] = true;
  }
  mem_addr = global_virtual_memory_addr_ + start_block * kBlockSize;
  allocations_[mem_addr] = blocks_needed;
  HIXL_LOGI("Reserved %zu bytes, blocks:%zu, address:%lu.", size, blocks_needed, mem_addr);
  return SUCCESS;
}

Status VirtualMemoryManager::ReleaseMemory(uintptr_t mem_addr) {
  std::lock_guard<std::mutex> lock(global_virtual_memory_mutex_);
  HIXL_CHK_BOOL_RET_STATUS(initialized_, PARAM_INVALID, "VirtualMemoryManager is not initialized.");
  HIXL_CHK_BOOL_RET_STATUS(mem_addr >= global_virtual_memory_addr_ && mem_addr < global_virtual_memory_addr_ + vm_size_,
                           PARAM_INVALID, "Address %lu is outside managed virtual memory range.", mem_addr);
  HIXL_CHK_BOOL_RET_STATUS((mem_addr - global_virtual_memory_addr_) % kBlockSize == 0, PARAM_INVALID,
                           "Address %lu is not 1GB aligned.", mem_addr);
  const auto it = allocations_.find(mem_addr);
  HIXL_CHK_BOOL_RET_STATUS(it != allocations_.end(), PARAM_INVALID, "Address %lu is not allocated or already released.",
                           mem_addr);

  const size_t block_index = (mem_addr - global_virtual_memory_addr_) / kBlockSize;
  for (size_t i = block_index; i < block_index + it->second; ++i) {
    bitmap_[i] = false;
  }
  HIXL_LOGI("Released %zu blocks from address:%lu.", it->second, mem_addr);
  allocations_.erase(it);
  return SUCCESS;
}
}  // namespace hixl
