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
#include <memory>
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
  struct LocalMemSegment {
    aclrtDrvMemHandle pa_handle = nullptr;
    // Foreign segments describe the full underlying allocation because aclrtMapMem imports a full PA block.
    ShareHandleInfo info{};
  };

  struct LocalMemRegistration {
    uintptr_t va_addr = 0;
    size_t len = 0;
    MemType mem_type = MEM_DEVICE;
    std::vector<LocalMemSegment> segments;
    // Keys into exported_pas_. Adjacent user ranges may share one retained PA.
    std::vector<uintptr_t> foreign_pa_pages;
  };

  struct ExportedPa {
    LocalMemSegment segment;
    uint32_t refcount = 0;
  };

  static Status ImportHostMemoryForRegister(const MemDesc &mem, aclrtMemFabricHandle &share_handle,
                                            aclrtDrvMemHandle &imported_pa_handle, uintptr_t &imported_va);
  static Status ExportSegment(const MemDesc &mem, MemType type, aclrtDrvMemHandle pa_handle, bool is_retained,
                              LocalMemSegment &segment);
  static void ReleaseSegment(LocalMemSegment &segment);
  void ReleaseRegistration(LocalMemRegistration &registration);
  Status FindExistingHandleForOverlap(const MemDesc &mem, MemType type, MemHandle &mem_handle,
                                      bool &is_duplicate) const;
  Status FindExistingHandleForOverlapLocked(const MemDesc &mem, MemType type, MemHandle &mem_handle,
                                            bool &is_duplicate) const;
  bool FindLocalHostRegisteredAddrLocked(uintptr_t old_addr, uintptr_t &new_addr, size_t &available_len) const;
  Status RegisterOwnMem(const MemDesc &mem, MemType type, aclrtDrvMemHandle pa_handle, MemHandle &mem_handle);
  Status RegisterForeignMem(const MemDesc &mem, MemType type, MemHandle &mem_handle);
  Status BuildForeignSegments(const MemDesc &mem, MemType type, LocalMemRegistration &registration);
  Status CommitRegistration(std::unique_ptr<LocalMemRegistration> &registration, MemHandle candidate_handle,
                            MemHandle &mem_handle, bool &committed);
  Status AttachForeignPaPage(uintptr_t page_addr, size_t page_len, MemType type, LocalMemSegment *owned_segment,
                             LocalMemRegistration &registration);

  mutable std::mutex share_handle_mutex_;
  std::unordered_map<MemHandle, std::unique_ptr<LocalMemRegistration>> registrations_;
  std::unordered_map<uintptr_t, std::unique_ptr<ExportedPa>> exported_pas_;
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
