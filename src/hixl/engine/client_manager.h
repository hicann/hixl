/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HIXL_SRC_HIXL_ENGINE_CLIENT_MANAGER_H_
#define HIXL_SRC_HIXL_ENGINE_CLIENT_MANAGER_H_

#include <atomic>
#include <condition_variable>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>
#include "hixl_client.h"
#include "common/hixl_inner_types.h"

namespace hixl {
using ClientPtr = std::shared_ptr<HixlClient>;
struct TransferReqInfo {
  TransferReq req = nullptr;
  ClientPtr client = nullptr;
  const void *user_data = nullptr;
};

class ClientManager {
 public:
  ClientManager() = default;
  ~ClientManager() = default;
  Status Initialize(bool auto_connect);
  Status Finalize();
  Status GetOrCreateClient(const ClientConfig &config, const std::vector<MemHandleInfo> &mem_info_list,
                           ClientPtr &client_ptr);
  ClientPtr GetClient(const std::string &remote_engine);
  ClientPtr GetClientByReq(const TransferReq &req);
  Status DestroyClient(const std::string &remote_engine);
  void RegisterTransferReq(const TransferReq &req, const ClientPtr &client, const void *user_data);
  void EraseTransferReq(const TransferReq &req);
  std::vector<TransferReqInfo> GetOrderedReqs(size_t max_count);
  bool IsEmpty();

 private:
  struct ReqOwner {
    std::weak_ptr<HixlClient> client;
    const void *user_data = nullptr;
    std::list<TransferReq>::iterator order_it;
  };

  Status StartHeartbeat();
  Status CreateClient(const ClientConfig &config, ClientPtr &client_ptr);
  std::shared_ptr<std::mutex> GetClientMutex(const std::string &remote_engine);
  void DestroyClientMutex(const std::string &remote_engine);
  void SendHeartbeat();
  void EraseTransferReqLocked(const TransferReq &req);
  void EraseReqIndexByClient(const ClientPtr &client);

  std::mutex mutex_;
  std::map<std::string, ClientPtr> clients_;
  bool finalized_ = false;
  std::mutex client_mutexes_mutex_;
  std::unordered_map<std::string, std::shared_ptr<std::mutex>> client_mutexes_;

  std::thread heartbeat_sender_;
  std::atomic<bool> stop_signal_{false};
  std::mutex cv_mutex_;
  std::condition_variable cv_;
  bool auto_connect_{false};

  std::mutex req_index_mutex_;
  std::list<TransferReq> ordered_reqs_;
  std::unordered_map<TransferReq, ReqOwner> req_to_client_;
};
}  // namespace hixl

#endif  // HIXL_SRC_HIXL_ENGINE_CLIENT_MANAGER_H_
