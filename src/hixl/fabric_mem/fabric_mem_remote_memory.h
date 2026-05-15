/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_FABRIC_MEM_FABRIC_MEM_REMOTE_MEMORY_H_
#define CANN_HIXL_SRC_HIXL_FABRIC_MEM_FABRIC_MEM_REMOTE_MEMORY_H_

#include <mutex>
#include <unordered_map>
#include <vector>

#include "fabric_mem/fabric_mem_types.h"
#include "hixl/hixl_types.h"

namespace hixl {
class FabricMemRemoteMemory {
 public:
  FabricMemRemoteMemory() = default;
  ~FabricMemRemoteMemory();
  FabricMemRemoteMemory(const FabricMemRemoteMemory &) = delete;
  FabricMemRemoteMemory &operator=(const FabricMemRemoteMemory &) = delete;
  FabricMemRemoteMemory(FabricMemRemoteMemory &&) = delete;
  FabricMemRemoteMemory &operator=(FabricMemRemoteMemory &&) = delete;

  Status Import(const std::vector<ShareHandleInfo> &remote_share_handles, int32_t device_id);
  void Finalize();
  // Engine mutex_ serializes Import/Transfer/Finalize; internal lock_guard here provides
  // defense-in-depth against future callers that may not hold the engine lock.
  const std::unordered_map<uintptr_t, VaInfo> &GetNewVaToOldVa() const;

 private:
  void ClearLocked();

  mutable std::mutex mutex_;
  std::unordered_map<uintptr_t, VaInfo> new_va_to_old_va_;
  std::vector<aclrtDrvMemHandle> remote_pa_handles_;
};
}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_FABRIC_MEM_FABRIC_MEM_REMOTE_MEMORY_H_
