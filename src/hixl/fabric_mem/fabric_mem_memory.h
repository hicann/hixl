/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_FABRIC_MEM_FABRIC_MEM_MEMORY_H_
#define CANN_HIXL_SRC_HIXL_FABRIC_MEM_FABRIC_MEM_MEMORY_H_

#include <atomic>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "acl/acl.h"
#include "fabric_mem/fabric_mem_types.h"
#include "hixl/hixl_types.h"

namespace hixl {

// Local registered memory (engine-global): exports registered buffers as fabric share handles and
// resolves local host addresses to their imported device-visible mappings during transfers.
class FabricMemLocalMemory {
 public:
  FabricMemLocalMemory() = default;
  ~FabricMemLocalMemory();
  FabricMemLocalMemory(const FabricMemLocalMemory &) = delete;
  FabricMemLocalMemory &operator=(const FabricMemLocalMemory &) = delete;
  FabricMemLocalMemory(FabricMemLocalMemory &&) = delete;
  FabricMemLocalMemory &operator=(FabricMemLocalMemory &&) = delete;

  Status RegisterMem(const MemDesc &mem, MemType type, MemHandle &mem_handle);
  Status DeregisterMem(MemHandle mem_handle);
  std::vector<ShareHandleInfo> GetShareHandles() const;
  bool HasHostMemory() const;
  Status TranslateLocalHostOpAddrs(std::vector<TransferOpDesc> &op_descs) const;
  void Finalize();

 private:
  static Status ImportHostMemoryForRegister(const MemDesc &mem, aclrtMemFabricHandle &share_handle,
                                            aclrtDrvMemHandle &imported_pa_handle, uintptr_t &imported_va);
  Status FindExistingHandleForOverlap(const MemDesc &mem, MemType type, MemHandle &mem_handle,
                                      bool &is_duplicate) const;
  bool FindLocalHostRegisteredAddrLocked(uintptr_t old_addr, size_t len, uintptr_t &new_addr) const;

  mutable std::mutex share_handle_mutex_;
  std::unordered_map<aclrtDrvMemHandle, ShareHandleInfo> share_handles_;
  std::atomic<bool> has_host_memory_{false};
};

// Remote memory for a single channel: imports the peer's fabric share handles and maps them into the
// local virtual address space so transfers can target peer buffers.
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
  std::unordered_map<uintptr_t, VaInfo> GetNewVaToOldVa() const;

 private:
  void ClearLocked();
  mutable std::mutex mutex_;
  std::unordered_map<uintptr_t, VaInfo> new_va_to_old_va_;
  std::vector<aclrtDrvMemHandle> remote_pa_handles_;
};
}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_FABRIC_MEM_FABRIC_MEM_MEMORY_H_
