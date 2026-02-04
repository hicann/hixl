/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "endpoint_store.h"
#include "common/hixl_checker.h"
#include "common/hixl_utils.h"

namespace hixl {
Status EndpointStore::CreateEndpoint(const EndpointDesc &endpoint, EndpointHandle &endpoint_handle) {
  auto ep = MakeShared<Endpoint>(endpoint);
  HIXL_CHECK_NOTNULL(ep);
  HIXL_CHK_STATUS_RET(ep->Initialize(), "Failed to Initialize endpoint.");
  endpoint_handle = ep->GetHandle();
  std::lock_guard<std::mutex> lock(mutex_);
  endpoints_[endpoint_handle] = ep;
  return SUCCESS;
}

EndpointPtr EndpointStore::GetEndpoint(EndpointHandle endpoint_handle) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = endpoints_.find(endpoint_handle);
  if (it == endpoints_.end()) {
    return nullptr;
  }
  return it->second;
}

std::vector<EndpointHandle> EndpointStore::GetAllEndpointHandles() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<EndpointHandle> handles;
  for (auto &it : endpoints_) {
    handles.push_back(it.first);
  }
  return handles;
}

inline bool operator == (const EndpointDesc& lhs, const EndpointDesc& rhs) {
  if (lhs.protocol != rhs.protocol) {
    return false;
  }

  if (lhs.protocol == COMM_PROTOCOL_HCCS) {
    return lhs.commAddr.id == rhs.commAddr.id;
  }
  return true;
}

EndpointPtr EndpointStore::MatchEndpoint(const EndpointDesc &endpoint, EndpointHandle &endpoint_handle) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto &it : endpoints_) {
    if (it.second->GetEndpoint() == endpoint) {
      endpoint_handle = it.first;
      HIXL_LOGI("Match endpoint success, handle:%p.", endpoint_handle);
      return it.second;
    }
  }
  HIXL_LOGE(PARAM_INVALID, "Failed to match endpoint");
  return nullptr;
}

Status EndpointStore::Finalize() {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto &it : endpoints_) {
    HIXL_CHK_STATUS_RET(it.second->Finalize(), "Failed to finalize endpoint.");
  }
  endpoints_.clear();
  return SUCCESS;
}


}  // namespace hixl
