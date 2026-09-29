/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_CS_HCOMM_ENDPOINT_H_
#define CANN_HIXL_SRC_HIXL_CS_HCOMM_ENDPOINT_H_

#include "cs/endpoint.h"

namespace hixl {

// Hcomm transport endpoint: the RoCE / HCCS / UB group path. Every transport operation forwards to
// HcommProxy; the shared Endpoint base owns the registration and channel bookkeeping.
class HcommEndpoint : public Endpoint {
 public:
  explicit HcommEndpoint(const EndpointDesc &endpoint) : Endpoint(endpoint) {}
  HcommEndpoint(const EndpointDesc &local_endpoint, const EndpointDesc &remote_endpoint)
      : Endpoint(local_endpoint, remote_endpoint) {}
  HcommEndpoint(const EndpointDesc &endpoint, bool need_host_va_mapping) : Endpoint(endpoint, need_host_va_mapping) {}
  ~HcommEndpoint() override = default;

 protected:
  HcclResult EndpointCreate(EndpointHandle &handle) override;
  HcclResult EndpointDestroy() override;
  HcclResult EndpointGetListenPort(uint32_t &port) const override;
  HcclResult MemReg(const char *mem_tag, const CommMem *mem, HcommMemHandle *mem_handle) override;
  HcclResult MemUnreg(HcommMemHandle mem_handle) override;
  HcclResult MemExport(HcommMemHandle mem_handle, void **mem_desc, uint32_t *mem_desc_len) override;
  HcclResult MemImport(const void *mem_desc, uint32_t desc_len, CommMem *out_mem) override;
  HcclResult MemUnimport(const void *mem_desc, uint32_t desc_len) override;
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_CS_HCOMM_ENDPOINT_H_
