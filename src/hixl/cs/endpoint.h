/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_CS_ENDPOINT_H_
#define CANN_HIXL_SRC_HIXL_CS_ENDPOINT_H_

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include "cs/hixl_cs.h"
#include "hixl/hixl_types.h"
#include "common/ctrl_msg.h"
#include "cs/global_config.h"
#include "channel.h"

namespace hixl {

class Endpoint;
using EndpointPtr = std::shared_ptr<Endpoint>;

// Protocol-agnostic integration layer for a transport endpoint. Shared bookkeeping (registration table,
// channel table, port, host VA mapping) lives here; HcommEndpoint and UbMemEndpoint fill in the transport
// specifics. Create() picks the concrete type from the endpoint protocol.
class Endpoint {
 public:
  static EndpointPtr Create(const EndpointDesc &endpoint, const GlobalConfig &global_config = {});
  static EndpointPtr Create(const EndpointDesc &local_endpoint, const EndpointDesc &remote_endpoint,
                            const GlobalConfig &global_config = {});
  static EndpointPtr Create(const EndpointDesc &endpoint, bool need_host_va_mapping,
                            const GlobalConfig &global_config = {});

  virtual ~Endpoint() = default;

  Endpoint(const Endpoint &) = delete;
  Endpoint &operator=(const Endpoint &) = delete;

  Status Initialize();
  Status Finalize();

  EndpointHandle GetHandle() const;
  const EndpointDesc &GetEndpoint() const;
  bool NeedHostVaMapping() const;

  Status RegisterMem(const char *mem_tag, const CommMem &mem, MemHandle &mem_handle);
  Status DeregisterMem(MemHandle mem_handle);
  Status ExportMem(std::vector<HixlMemDesc> &mem_descs);
  Status CreateChannel(const ChannelDesc &channel_desc, ChannelHandle &channel_handle, uint32_t timeout_ms);
  Status DestroyChannel(ChannelHandle channel_handle);
  Status GetMemDesc(MemHandle mem_handle, HixlMemDesc &desc) const;
  Status ImportMem(const void *mem_desc, uint32_t desc_len, CommMem &out_buf);
  Status UnimportMem(const void *mem_desc, uint32_t desc_len);
  // Reports the transport's listening port. Transports without their own listening socket return
  // UNSUPPORTED, which the server only warns about.
  Status GetListenPort(uint32_t &port) const;
  void SetPort(uint32_t port);
  uint32_t GetPort() const;

  // Rewrites peer-side addresses onto the locally mapped addresses. Protocols that can address peer
  // memory directly (Hcomm) keep the default, which hands the descriptors back untouched.
  virtual Status TranslateRemoteDescs(uint32_t list_num, const HixlOneSideOpDesc *src,
                                      std::vector<HixlOneSideOpDesc> &dst) {
    (void)list_num;
    (void)src;
    dst.clear();
    return SUCCESS;
  }

 protected:
  explicit Endpoint(const EndpointDesc &endpoint);
  Endpoint(const EndpointDesc &local_endpoint, const EndpointDesc &remote_endpoint);
  Endpoint(const EndpointDesc &endpoint, bool need_host_va_mapping);

  // Transport-specific endpoint lifecycle. The endpoint owns the handle these operate on, so only
  // EndpointCreate takes it, and it takes it as the value to produce. EndpointDestroy must release
  // whatever EndpointCreate allocated; the shared Finalize unregisters memory and destroys channels
  // first.
  virtual HcclResult EndpointCreate(EndpointHandle &handle) = 0;
  virtual HcclResult EndpointDestroy() = 0;
  // Default: the transport has no listening socket of its own, so there is no port to report. The
  // server treats HCCL_E_NOT_SUPPORT as "no port" and only warns.
  virtual HcclResult EndpointGetListenPort(uint32_t &port) const {
    (void)port;
    return HCCL_E_NOT_SUPPORT;
  }

  // Transport-specific memory registration/export/import. The public API does the shared bookkeeping
  // (validation, registration table, locking) and then calls these. None of them takes the endpoint
  // handle: the implementation reads it with GetHandle().
  virtual HcclResult MemReg(const char *mem_tag, const CommMem *mem, HcommMemHandle *mem_handle) = 0;
  virtual HcclResult MemUnreg(HcommMemHandle mem_handle) = 0;
  virtual HcclResult MemExport(HcommMemHandle mem_handle, void **mem_desc, uint32_t *mem_desc_len) = 0;
  virtual HcclResult MemImport(const void *mem_desc, uint32_t desc_len, CommMem *out_mem) = 0;
  virtual HcclResult MemUnimport(const void *mem_desc, uint32_t desc_len) = 0;

 private:
  CommEngine SelectEngine() const;

  mutable std::mutex mutex_;
  EndpointDesc endpoint_{};
  EndpointHandle handle_ = nullptr;
  std::map<MemHandle, HixlMemDesc> reg_mems_;
  std::map<ChannelHandle, ChannelPtr> channels_;
  uint32_t port_ = 0;
  bool need_host_va_mapping_{false};
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_CS_ENDPOINT_H_
