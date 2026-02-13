/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HIXL_SRC_HIXL_SRC_ENGINE_CLIENT_MANAGER_H_
#define HIXL_SRC_HIXL_SRC_ENGINE_CLIENT_MANAGER_H_

#include <mutex>
#include <map>
#include <vector>
#include "hixl_client.h"
#include "common/hixl_inner_types.h"

namespace hixl {
using ClientPtr = std::shared_ptr<HixlClient>;
class ClientManager {
 public:
  ClientManager() = default;
  ~ClientManager() = default;
  Status Initialize();
  Status Finalize();
  Status CreateClient(const std::vector<EndpointConfig> &endpoint_list,
                      const std::string &remote_engine,
                      ClientPtr &client_ptr);
  ClientPtr GetClient(const std::string &remote_engine);
  Status DestroyClient(const std::string &remote_engine);

 private:
  std::mutex mutex_;
  std::map<std::string, ClientPtr> clients_;
};
}  // namespace hixl

#endif  // HIXL_SRC_HIXL_SRC_ENGINE_CLIENT_MANAGER_H_