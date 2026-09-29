/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm_endpoint.h"

#include "common/hixl_utils.h"
#include "proxy/hcomm_proxy.h"

namespace hixl {

HcclResult HcommEndpoint::EndpointCreate(EndpointHandle &handle) {
  const EndpointDesc &endpoint = GetEndpoint();
  return HcommProxy::EndpointCreate(&endpoint, &handle);
}

HcclResult HcommEndpoint::EndpointDestroy() {
  return HcommProxy::EndpointDestroy(GetHandle());
}

HcclResult HcommEndpoint::EndpointGetListenPort(uint32_t &port) const {
  return HcommProxy::EndpointGetListenPort(GetHandle(), &port);
}

HcclResult HcommEndpoint::MemReg(const char *mem_tag, const CommMem *mem, HcommMemHandle *mem_handle) {
  return HcommProxy::MemReg(GetHandle(), mem_tag, mem, mem_handle);
}

HcclResult HcommEndpoint::MemUnreg(HcommMemHandle mem_handle) {
  return HcommProxy::MemUnreg(GetHandle(), mem_handle);
}

HcclResult HcommEndpoint::MemExport(HcommMemHandle mem_handle, void **mem_desc, uint32_t *mem_desc_len) {
  return HcommProxy::MemExport(GetHandle(), mem_handle, mem_desc, mem_desc_len);
}

HcclResult HcommEndpoint::MemImport(const void *mem_desc, uint32_t desc_len, CommMem *out_mem) {
  return HcommProxy::MemImport(GetHandle(), mem_desc, desc_len, out_mem);
}

HcclResult HcommEndpoint::MemUnimport(const void *mem_desc, uint32_t desc_len) {
  return HcommProxy::MemUnimport(GetHandle(), mem_desc, desc_len);
}

}  // namespace hixl
