/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_COMMON_CTRL_MSG_PLUGIN_H_
#define CANN_HIXL_SRC_HIXL_COMMON_CTRL_MSG_PLUGIN_H_

#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include "hixl/hixl_types.h"


namespace hixl {
class CtrlMsgPlugin {
 public:
  CtrlMsgPlugin() = default;
  ~CtrlMsgPlugin() = default;
  static void Initialize();
  static Status Connect(const std::string &ip, uint32_t port, int32_t &conn_fd, int32_t timeout);
  static Status Send(int32_t fd, const void *buf, size_t len);
  static Status Recv(int32_t fd, void *buf, size_t len, uint32_t timeout_ms);
  static Status Listen(const std::string &ip, uint32_t listen_port, int32_t backlog, int32_t &listen_fd);
  static Status AddFdToEpoll(int32_t &epoll_fd, int32_t fd, uint32_t events = EPOLLIN | EPOLLRDHUP);
  static Status Accept(int32_t listen_fd, int32_t &conn_fd);

 private:
  static Status DoConnect(struct ::addrinfo *addr, int32_t &conn_fd, int32_t &err_no, int32_t timeout);
  static Status GetAiFamily(const std::string &ip, int32_t &ai_family);
  static Status SockAddrInit(const std::string &ip, uint32_t listen_port, int32_t ai_family,
                             struct sockaddr_storage &server_addr, socklen_t &addr_len);
};
}  // namespace hixl
#endif  // CANN_HIXL_SRC_HIXL_COMMON_CTRL_MSG_PLUGIN_H_
