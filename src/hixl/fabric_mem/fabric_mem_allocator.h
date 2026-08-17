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
  // Allocates VMM memory without exporting. ExportToShareableHandle performs ACL export at most
  // once per allocation and caches the handle for later callers (including RegisterMem).
  static Status MallocMem(MemType type, size_t size, void **ptr);
  static Status FreeMem(void *ptr);
  // First call exports; later calls return the cached handle (ACL export is once per allocation).
  static Status ExportToShareableHandle(uintptr_t va_addr, aclrtMemFabricHandle &share_handle);
  static Status GetPaHandleFromVa(uintptr_t va_addr, aclrtDrvMemHandle &pa_handle);
  static Status AllocatePhysicalMemory(MemType type, size_t total_size, int32_t logic_device_id,
                                       aclrtDrvMemHandle &handle);
};
}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_FABRIC_MEM_FABRIC_MEM_ALLOCATOR_H_
