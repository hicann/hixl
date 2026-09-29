/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_CS_UBMEM_UBMEM_ENDPOINT_H_
#define CANN_HIXL_SRC_HIXL_CS_UBMEM_UBMEM_ENDPOINT_H_

#include <cstdint>
#include <map>
#include <vector>

#include "cs/endpoint.h"
#include "cs/global_config.h"
#include "cs/ubmem/ubmem_memory.h"
#include "cs/ubmem/ubmem_types.h"

namespace hixl {

// Maps a HIXL status onto the Hccl result the transport-facing API reports. Shared with UbMemChannel.
HcclResult ToHccl(Status status);

// UB_MEM transport endpoint. Each endpoint owns the memory it exports and the peer memory it maps, so no
// process-wide registry takes part: registration goes through this endpoint's UbMemMemory, and the
// peer's handles are imported into that same memory object, which also holds the translation table used
// to rewrite peer addresses before a copy. The virtual address pool is the only process-wide resource
// and lives until the VirtualMemoryManager singleton is destroyed.
class UbMemEndpoint : public Endpoint {
 public:
  explicit UbMemEndpoint(const EndpointDesc &endpoint, const GlobalConfig &global_config = {})
      : Endpoint(endpoint), global_config_(global_config) {}
  UbMemEndpoint(const EndpointDesc &local_endpoint, const EndpointDesc &remote_endpoint,
                const GlobalConfig &global_config = {})
      : Endpoint(local_endpoint, remote_endpoint), global_config_(global_config) {}
  UbMemEndpoint(const EndpointDesc &endpoint, bool need_host_va_mapping, const GlobalConfig &global_config = {})
      : Endpoint(endpoint, need_host_va_mapping), global_config_(global_config) {}
  ~UbMemEndpoint() override = default;

  Status TranslateRemoteDescs(uint32_t list_num, const HixlOneSideOpDesc *src,
                              std::vector<HixlOneSideOpDesc> &dst) override;

 protected:
  HcclResult EndpointCreate(EndpointHandle &handle) override;
  HcclResult EndpointDestroy() override;
  // UB_MEM has no listening socket of its own; port 0 tells the peer the CS channel is the only link.
  HcclResult EndpointGetListenPort(uint32_t &port) const override {
    port = 0U;
    return HCCL_SUCCESS;
  }
  HcclResult MemReg(const char *mem_tag, const CommMem *mem, HcommMemHandle *mem_handle) override;
  HcclResult MemUnreg(HcommMemHandle mem_handle) override;
  HcclResult MemExport(HcommMemHandle mem_handle, void **mem_desc, uint32_t *mem_desc_len) override;
  HcclResult MemImport(const void *mem_desc, uint32_t desc_len, CommMem *out_mem) override;
  HcclResult MemUnimport(const void *mem_desc, uint32_t desc_len) override;

 private:
  // Export descriptor buffer handed to the peer, owned here and released with the registration.
  struct ExportEntry {
    void *desc{nullptr};
    uint32_t len{0U};
  };

  // Export descriptor wire codec. The descriptor is a JSON array of ShareHandleInfo; the caller owns the
  // buffer returned by EncodeShareHandles and releases it with FreeExportDesc. Static because it only
  // encodes data, never endpoint state.
  static Status EncodeShareHandles(const std::vector<ShareHandleInfo> &handles, void *&export_desc,
                                   uint32_t &export_len);
  static Status DecodeShareHandles(const void *export_desc, uint32_t export_len, std::vector<ShareHandleInfo> &handles);
  static void FreeExportDesc(void *&export_desc);

  // Rewrites peer-side addresses onto the locally mapped addresses of imported segments. A range
  // inside one import is offset arithmetic on a single entry; only a range spanning several imports
  // (or a partially released one) is split into one descriptor per mapped range.
  static Status TranslateRemoteOpDescs(const UbMemRemoteIndex &index, uint32_t list_num, const HixlOneSideOpDesc *src,
                                       std::vector<HixlOneSideOpDesc> &dst);

  Status EncodeRegExport(MemHandle mem_handle, ExportEntry &entry);

  // Applies the configured capacity/start address to the process-wide VMM and initializes it on first use.
  Status InitializeProcessWideVaPool();

  std::map<MemHandle, ExportEntry> exports_;
  mutable UbMemMemory memory_;
  // The whole config, not just the fabric_memory slice: the endpoint keeps whatever the client or server
  // was created with, so a new UB_MEM setting does not have to be threaded through every layer.
  GlobalConfig global_config_{};
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_CS_UBMEM_UBMEM_ENDPOINT_H_
