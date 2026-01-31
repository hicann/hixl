/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING
 * BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE. See LICENSE in the root of
 * the software repository for the full text of the License.
 */

#include "virtual_memory_manager.h"
#include <cstddef>
#include <mutex>
#include <dlfcn.h>
#include "adxl/adxl_checker.h"
#include "common/def_types.h"
#include "common/llm_scope_guard.h"
#include "common/llm_log.h"

namespace adxl {
namespace {
constexpr const char *kReserveMemNoUcMemoryFunc = "aclrtReserveMemAddressNoUCMemory";
constexpr size_t kBlockSize = 1024UL * 1024UL * 1024UL;  // 1GB
constexpr size_t kDefaultNumBlocks = 64UL * 1024UL;
constexpr size_t kDefaultGlobalVirtualMemorySize = kBlockSize * kDefaultNumBlocks;
constexpr size_t kGlobalVirtualMemoryStartAddr = kBlockSize * 1024UL * 40UL;
constexpr uint64_t kReserveFlagHugePage = 1UL;
}  // namespace
VirtualMemoryManager &VirtualMemoryManager::GetInstance() {
  static VirtualMemoryManager instance;
  return instance;
}

void VirtualMemoryManager::SetSoName(const char *so_name) {
  so_name_ = so_name;
}

void VirtualMemoryManager::SetVirtualMemoryCapacity(size_t capacity_in_tb) {
  std::lock_guard<std::mutex> lock(global_virtual_memory_mutex_);
  if (initialized_) {
    LLMLOGE(PARAM_INVALID, "VirtualMemoryManager already initialized, cannot set capacity");
    return;
  }
  // Convert TB to bytes: 1TB = 1024GB = 1024 * 1024 * 1024 * 1024 bytes
  constexpr size_t kBytesPerTB = 1024UL * 1024UL * 1024UL * 1024UL;
  vm_size_ = capacity_in_tb * kBytesPerTB;
  num_blocks_ = vm_size_ / kBlockSize;
  LLMLOGI("Set virtual memory capacity to %zu TB (%zu bytes, %zu blocks)",
          capacity_in_tb, vm_size_, num_blocks_);
}

Status VirtualMemoryManager::Initialize() {
  std::lock_guard<std::mutex> lock(global_virtual_memory_mutex_);
  if (initialized_) {
    return SUCCESS;
  }
  // Use user-set capacity if already set, otherwise use default
  if (vm_size_ == 0) {
    vm_size_ = kDefaultGlobalVirtualMemorySize;
    num_blocks_ = kDefaultNumBlocks;
  }
  // Since already links against libascendcl.so, use RTLD_NOLOAD to get existing handle
  void *handle = mmDlopen(so_name_.c_str(), static_cast<int32_t>(static_cast<uint32_t>(MMPA_RTLD_LAZY) |
                                                                 static_cast<uint32_t>(RTLD_NOLOAD)));
  ADXL_CHK_BOOL_RET_STATUS(handle != nullptr, FAILED, "Failed to load so:%s.", so_name_.c_str());
  LLM_MAKE_GUARD(close_handle, ([handle]() { (void)mmDlclose(handle); }));

  ReserveMemAddressNoUCMemoryFunc reserve_no_uc_mem_func =
      llm::FunctionLoader<ReserveMemAddressNoUCMemoryFunc>::load(handle, kReserveMemNoUcMemoryFunc);
  if (reserve_no_uc_mem_func == nullptr) {
    // previous version does not support ReserveMemAddressNoUCMemoryFunc
    ADXL_CHK_ACL_RET(aclrtReserveMemAddress(&global_virtual_memory_, vm_size_, 0, nullptr, kReserveFlagHugePage));
    LLMEVENT("Reserve virtual memory size:%zu, aclrtReserveMemAddressNoUCMemory is not defined.", vm_size_);
  } else {
    void *global_start_va = llm::ValueToPtr(kGlobalVirtualMemoryStartAddr);
    int ret = reserve_no_uc_mem_func(&global_virtual_memory_, vm_size_, 0, global_start_va, kReserveFlagHugePage);
    if (ret == ACL_ERROR_RT_FEATURE_NOT_SUPPORT) {
      // A5 does not support ReserveMemAddressNoUCMemoryFunc
      ADXL_CHK_ACL_RET(aclrtReserveMemAddress(&global_virtual_memory_, vm_size_, 0, nullptr, kReserveFlagHugePage));
      LLMEVENT("Reserve virtual memory with aclrtReserveMemAddress, size:%zu.", vm_size_);
    } else {
      ADXL_CHK_BOOL_RET_STATUS(ret == ACL_ERROR_NONE, FAILED, "Failed to reserve mem address.");
      LLMEVENT("Reserve virtual memory with aclrtReserveMemAddressNoUCMemory, size:%zu.", vm_size_);
    }
  }

  global_virtual_memory_addr_ = llm::PtrToValue(global_virtual_memory_);
  // Clear bitmap and allocations map
  bitmap_.resize(num_blocks_, false);
  allocations_.clear();
  initialized_ = true;
  LLMLOGI("VirtualMemoryManager initialized, reserved base virtual mem address: %lu", global_virtual_memory_addr_);
  return SUCCESS;
}

void VirtualMemoryManager::Finalize() {
  std::lock_guard<std::mutex> lock(global_virtual_memory_mutex_);
  if (!initialized_) {
    return;
  }

  // Clear bitmap and allocations map
  bitmap_.clear();
  allocations_.clear();
  initialized_ = false;

  if (global_virtual_memory_ != nullptr) {
    LLM_CHK_ACL(aclrtReleaseMemAddress(global_virtual_memory_));
    global_virtual_memory_ = nullptr;
    global_virtual_memory_addr_ = 0;
  }
}

Status VirtualMemoryManager::ReserveMemory(size_t size, uintptr_t &mem_addr) {
  std::lock_guard<std::mutex> lock(global_virtual_memory_mutex_);

  if (!initialized_) {
    LLMLOGE(PARAM_INVALID, "VirtualMemoryManager not initialized");
    return PARAM_INVALID;
  }

  if (size == 0) {
    LLMLOGE(PARAM_INVALID, "ReserveMemory size cannot be zero");
    return PARAM_INVALID;
  }

  // Calculate number of 1GB blocks needed (round up)
  size_t blocks_needed = (size + kBlockSize - 1) / kBlockSize;
  if (blocks_needed > num_blocks_) {
    LLMLOGE(RESOURCE_EXHAUSTED, "Requested size %zu exceeds total virtual memory capacity", size);
    return RESOURCE_EXHAUSTED;
  }

  // First-fit algorithm: find contiguous free blocks
  size_t start_block = 0;
  size_t contiguous_free = 0;
  for (size_t i = 0; i < num_blocks_; ++i) {
    if (!bitmap_[i]) {
      if (contiguous_free == 0) {
        start_block = i;
      }
      contiguous_free++;
      if (contiguous_free >= blocks_needed) {
        break;
      }
    } else {
      contiguous_free = 0;
    }
  }

  if (contiguous_free < blocks_needed) {
    LLMLOGE(RESOURCE_EXHAUSTED, "Insufficient contiguous virtual memory blocks: needed %zu, found %zu", blocks_needed,
            contiguous_free);
    return RESOURCE_EXHAUSTED;
  }

  // Mark blocks as allocated
  for (size_t i = start_block; i < start_block + blocks_needed; ++i) {
    bitmap_[i] = true;
  }

  // Calculate start address
  uintptr_t start_addr = global_virtual_memory_addr_ + start_block * kBlockSize;
  mem_addr = start_addr;

  // Store allocation metadata
  allocations_[start_addr] = blocks_needed;

  LLMLOGI("Reserved %zu bytes (%zu blocks) at address %lu, start block %zu", size, blocks_needed, start_addr,
          start_block);
  return SUCCESS;
}

Status VirtualMemoryManager::ReleaseMemory(uintptr_t mem_addr) {
  std::lock_guard<std::mutex> lock(global_virtual_memory_mutex_);

  if (!initialized_) {
    LLMLOGE(PARAM_INVALID, "VirtualMemoryManager not initialized");
    return PARAM_INVALID;
  }

  // Validate address is within the global virtual address range
  if (mem_addr < global_virtual_memory_addr_ || mem_addr >= global_virtual_memory_addr_ + vm_size_) {
    LLMLOGE(PARAM_INVALID, "Address %lu is outside the managed virtual address range", mem_addr);
    return PARAM_INVALID;
  }

  // Validate address is 1GB aligned
  if ((mem_addr - global_virtual_memory_addr_) % kBlockSize != 0) {
    LLMLOGE(PARAM_INVALID, "Address %lu is not 1GB aligned", mem_addr);
    return PARAM_INVALID;
  }

  // Find the allocation in the metadata map
  auto it = allocations_.find(mem_addr);
  if (it == allocations_.end()) {
    LLMLOGE(PARAM_INVALID, "Address %lu is not allocated or already released", mem_addr);
    return PARAM_INVALID;
  }

  size_t blocks = it->second;
  size_t block_index = (mem_addr - global_virtual_memory_addr_) / kBlockSize;

  // Clear the bitmap bits for the allocated blocks
  for (size_t i = block_index; i < block_index + blocks; ++i) {
    bitmap_[i] = false;
  }

  // Remove from allocations map
  allocations_.erase(it);

  LLMLOGI("Released %zu blocks starting at address %lu, block index %zu", blocks, mem_addr, block_index);
  return SUCCESS;
}
}  // namespace adxl
