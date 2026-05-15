/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_FABRIC_MEM_FABRIC_MEM_CONTROL_H_
#define CANN_HIXL_SRC_HIXL_FABRIC_MEM_FABRIC_MEM_CONTROL_H_

#include <atomic>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include "fabric_mem/fabric_mem_types.h"
#include "hixl/hixl_types.h"

namespace hixl {
using FabricMemShareHandleProvider = std::function<Status(std::vector<ShareHandleInfo> &)>;

class FabricMemControlServer {
 public:
  FabricMemControlServer() = default;
  ~FabricMemControlServer();
  FabricMemControlServer(const FabricMemControlServer &) = delete;
  FabricMemControlServer &operator=(const FabricMemControlServer &) = delete;
  FabricMemControlServer(FabricMemControlServer &&) = delete;
  FabricMemControlServer &operator=(FabricMemControlServer &&) = delete;

  Status Start(const std::string &listen_info, FabricMemShareHandleProvider provider);
  void Stop();
  Status DequeueNotifies(std::vector<NotifyDesc> &notifies);

 private:
  void Run();
  Status HandleConnection(int32_t fd);
  Status HandleGetFabricMemInfo(int32_t fd);
  Status HandleSendNotify(const std::string &payload);
  Status HandleGetNotifies(int32_t fd);
  Status SendShareHandleResponse(int32_t fd, Status result, const std::vector<ShareHandleInfo> &share_handles);

  std::mutex mutex_;
  std::mutex notify_mutex_;
  std::atomic<bool> running_{false};
  int32_t listen_fd_{-1};
  std::thread worker_;
  FabricMemShareHandleProvider provider_;
  std::vector<NotifyDesc> notify_queue_;
};

class FabricMemControlClient {
 public:
  static Status Fetch(const std::string &remote_engine, int32_t timeout_ms,
                      std::vector<ShareHandleInfo> &share_handles);
  static Status SendNotify(const std::string &remote_engine, const NotifyDesc &notify, int32_t timeout_ms);
  static Status FetchNotifies(const std::string &remote_engine, int32_t timeout_ms, std::vector<NotifyDesc> &notifies);
};
}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_FABRIC_MEM_FABRIC_MEM_CONTROL_H_
