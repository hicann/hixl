/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hixl_engine.h"
#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/hixl_utils.h"
#include "client_manager.h"

namespace hixl {
Status ClientManager::CreateClient(const std::vector<EndpointConfig> &endpoint_list,
                                   const std::string &remote_engine,
                                   ClientPtr &client_ptr) {
  std::string ip;
  int32_t port = 0;
  HIXL_CHK_STATUS_RET(ParseListenInfo(remote_engine, ip, port), "Failed to parse ip, remote_engine:%s", remote_engine.c_str());
  client_ptr = MakeShared<HixlClient>(ip, static_cast<uint32_t>(port));
  HIXL_CHECK_NOTNULL(client_ptr, "Failed to create HixlClient, ip:%s, port:%u", ip.c_str(), port);
  HIXL_CHK_STATUS_RET(client_ptr->Initialize(endpoint_list), "Failed to initialize HixlClient, ip:%s, port:%u",
                      ip.c_str(), port);
  std::lock_guard<std::mutex> lock(mutex_);
  clients_.emplace(remote_engine, client_ptr);
  return SUCCESS;
}

ClientPtr ClientManager::GetClient(const std::string &remote_engine) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto &it = clients_.find(remote_engine);
  if (it != clients_.cend()) {
    return it->second;
  }
  return nullptr;
}

Status ClientManager::DestroyClient(const std::string &remote_engine) {
  auto ret = SUCCESS;
  std::lock_guard<std::mutex> lock(mutex_);
  const auto &it = clients_.find(remote_engine);
  if (it != clients_.end()) {
    auto client = it->second;
    ret = client->Finalize();
    clients_.erase(it);
    HIXL_LOGI("Destroy client end, remote_engine=%s", remote_engine.c_str());
  }
  return ret;
}

Status ClientManager::Finalize() {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto &it : clients_) {
    if (it.second->Finalize() != SUCCESS) {
      HIXL_LOGE(FAILED, "Failed to finalize client, remote_engine:%s", it.first.c_str());
    }
  }
  clients_.clear();
  return SUCCESS;
}
}  // namespace hixl