/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_CS_UBMEM_VIRTUAL_MEMORY_MANAGER_H_
#define CANN_HIXL_SRC_HIXL_CS_UBMEM_VIRTUAL_MEMORY_MANAGER_H_

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

#include "common/soc_type.h"
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

  // Brings up the process-wide pool on first use. The singleton destructor releases all reserved VA regions.
  Status Initialize();
  void Finalize();
  Status ReserveMemory(size_t size, uintptr_t &mem_addr);
  Status ReleaseMemory(uintptr_t mem_addr);
  Status SetVirtualMemoryCapacity(size_t capacity_in_tb);
  Status SetGlobalStartAddress(size_t start_addr_in_tb);
  Status ReserveMemAddress(void *&virtual_address, size_t size) const;

 private:
  struct ReservedRegion {
    void *raw_ptr = nullptr;
    uintptr_t base_addr = 0;
    size_t size = 0;
    size_t num_blocks = 0;
    std::vector<bool> bitmap;
  };

  struct AllocationRecord {
    size_t region_index = 0;
    size_t start_block = 0;
    size_t num_blocks = 0;
  };

  VirtualMemoryManager() = default;
  Status InitProcess();
  Status ReserveConfiguredRegions(SocType soc_type);
  Status ReserveBoundedWindowRegions();
  Status ReserveAutoAddressRegions(SocType soc_type);
  Status ReserveChunkedWindowRegions();
  Status ReserveChunkedAutoRegions(SocType soc_type);
  Status TryAppendRegion(size_t size, void *expect_ptr, SocType soc_type, uintptr_t va_upper_bound, bool log_error);
  void ReleaseAllRegions();
  size_t FindRegionIndex(uintptr_t mem_addr) const;
  static bool FindFreeBlocksInRegion(const ReservedRegion &region, size_t blocks_needed, size_t &start_block);

  std::vector<ReservedRegion> regions_;
  std::vector<void *> pending_release_vas_;
  std::unordered_map<uintptr_t, AllocationRecord> allocations_;
  bool initialized_ = false;
  std::mutex global_virtual_memory_mutex_;
  size_t vm_size_ = 0;
  size_t num_blocks_ = 0;
  std::optional<uintptr_t> global_start_va_;
};
}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_CS_UBMEM_VIRTUAL_MEMORY_MANAGER_H_
