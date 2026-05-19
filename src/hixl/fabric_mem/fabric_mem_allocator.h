/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_FABRIC_MEM_FABRIC_MEM_ALLOCATOR_H_
#define CANN_HIXL_SRC_HIXL_FABRIC_MEM_FABRIC_MEM_ALLOCATOR_H_

#include <cstddef>
#include <cstdint>

#include "acl/acl.h"
#include "hixl/hixl_types.h"

namespace hixl {
class FabricMemAllocator {
 public:
  static Status MallocMem(MemType type, size_t size, void **ptr);
  static Status FreeMem(void *ptr);
  static Status GetPaHandleFromVa(uintptr_t va_addr, aclrtDrvMemHandle &pa_handle);
  static void AddVaToPaMapping(uintptr_t va_addr, aclrtDrvMemHandle pa_handle);
  static void RemoveVaToPaMapping(uintptr_t va_addr);
  static Status AllocatePhysicalMemory(MemType type, size_t total_size, aclrtDrvMemHandle &handle);
};
}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_FABRIC_MEM_FABRIC_MEM_ALLOCATOR_H_
