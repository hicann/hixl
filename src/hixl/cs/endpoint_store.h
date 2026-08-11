/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_ENDPOINT_STORE_H_
#define CANN_HIXL_SRC_ENDPOINT_STORE_H_

#include <mutex>
#include <map>
#include "cs/hixl_cs.h"
#include "hixl/hixl_types.h"
#include "endpoint.h"

namespace hixl {
class EndpointStore {
 public:
  EndpointStore() = default;

  ~EndpointStore() = default;

  Status CreateEndpoint(const EndpointDesc &endpoint, EndpointHandle &endpoint_handle);
  Status CreateEndpoint(const EndpointDesc &endpoint, EndpointHandle &endpoint_handle, bool need_host_va_mapping);

  EndpointPtr GetEndpoint(EndpointHandle endpoint_handle) const;

  std::vector<EndpointHandle> GetAllEndpointHandles() const;

  EndpointPtr MatchEndpoint(const EndpointDesc &endpoint, EndpointHandle &endpoint_handle) const;

  Status Finalize();

 private:
  mutable std::mutex mutex_;
  std::map<EndpointHandle, EndpointPtr> endpoints_;

  Status StoreEndpoint(const EndpointPtr &endpoint, EndpointHandle &endpoint_handle);
};
}  // namespace hixl

#endif  // CANN_HIXL_SRC_ENDPOINT_STORE_H_
