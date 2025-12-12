/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_MSG_HANDLER_PLUGIN_H_
#define CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_MSG_HANDLER_PLUGIN_H_

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include "runtime/rt.h"
#include "llm_datadist/llm_error_codes.h"
#include "common/llm_thread_pool.h"

namespace llm {
using ConnectedProcess = std::function<void(int32_t conn_fd, bool &keep_fd)>;
class MsgHandlerPlugin {
 public:
  MsgHandlerPlugin() = default;
  ~MsgHandlerPlugin();
  void Initialize();
  void Finalize();
  ge::Status StartDaemon(const std::string &ip, uint32_t listen_port);
  void RegisterConnectedProcess(ConnectedProcess proc);
  static ge::Status Connect(const std::string &ip, uint32_t port, int32_t &conn_fd, int32_t timeout,
                            ge::Status default_err);
  static ssize_t Read(int32_t fd, void *buf, size_t len);
  static ssize_t Write(int32_t fd, const void *buf, size_t len);
  static void Disconnect(int32_t conn_fd);
  static ge::Status SendMsg(int32_t fd, int32_t msg_type, const std::string &msg_str);
  static ge::Status RecvMsg(int32_t fd, int32_t &msg_type, std::vector<char> &msg);
  static ge::Status RecvMsg(int32_t fd, int32_t &msg_type, std::vector<char> &msg, uint64_t length);

 private:
  void ListenClose();
  ge::Status DoConnectedProcess(int32_t conn_fd);
  ge::Status DoAccept();
  static ge::Status DoConnect(struct ::addrinfo *addr, int32_t &conn_fd, int32_t &err_no, int32_t timeout,
                              ge::Status default_err);
  static ge::Status GetAiFamily(const std::string &ip, int32_t &ai_family);
  ge::Status SockAddrInit(const std::string &ip, uint32_t listen_port, int32_t ai_family,
                          struct sockaddr_storage &server_addr, socklen_t &addr_len);

  int32_t listen_fd_ = -1;
  std::atomic<bool> listener_running_{false};
  std::thread listener_;
  std::unique_ptr<LLMThreadPool> thread_pool_ = nullptr;
  ConnectedProcess connected_process_;
  rtContext_t rt_context_ = nullptr;
};
}  // namespace llm
#endif  // CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_MSG_HANDLER_PLUGIN_H_
