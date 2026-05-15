/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_FABRIC_MEM_VIRTUAL_MEMORY_MANAGER_H_
#define CANN_HIXL_SRC_HIXL_FABRIC_MEM_VIRTUAL_MEMORY_MANAGER_H_

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "hixl/hixl_types.h"

namespace hixl {
class VirtualMemoryManager {
 public:
  static VirtualMemoryManager &GetInstance();
  ~VirtualMemoryManager();
  VirtualMemoryManager(const VirtualMemoryManager &) = delete;
  VirtualMemoryManager(const VirtualMemoryManager &&) = delete;
  VirtualMemoryManager &operator=(const VirtualMemoryManager &) = delete;
  VirtualMemoryManager &operator=(const VirtualMemoryManager &&) = delete;

  Status Initialize();
  void Finalize();
  Status ReserveMemory(size_t size, uintptr_t &mem_addr);
  Status ReleaseMemory(uintptr_t mem_addr);
  Status SetVirtualMemoryCapacity(size_t capacity_in_tb);
  Status SetGlobalStartAddress(size_t start_addr_in_tb);
  Status ReserveMemAddress(void *&virtual_address, size_t size);

 private:
  VirtualMemoryManager() = default;
  Status InitProcess();

  std::vector<bool> bitmap_;
  std::unordered_map<uintptr_t, size_t> allocations_;
  bool initialized_ = false;
  std::mutex global_virtual_memory_mutex_;
  void *global_virtual_memory_ = nullptr;
  uintptr_t global_virtual_memory_addr_ = 0;
  size_t vm_size_ = 0;
  size_t num_blocks_ = 0;
  uintptr_t global_start_va_ = 0;
};
}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_FABRIC_MEM_VIRTUAL_MEMORY_MANAGER_H_
