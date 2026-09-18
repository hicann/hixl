/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE. See LICENSE
 * in the root of the software repository for the full text of the License.
 */

#include <arpa/inet.h>
#include <fcntl.h>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <memory>

#define private public
#include "common/msg_handler_plugin.h"
#undef private

namespace {
int g_accepted_fd = -1;

extern "C" int __real_accept(int socket_fd, struct sockaddr *address, socklen_t *address_length);

extern "C" int __wrap_accept(int socket_fd, struct sockaddr *address, socklen_t *address_length) {
  g_accepted_fd = __real_accept(socket_fd, address, address_length);
  return g_accepted_fd;
}

int CreateListener(uint16_t &port) {
  const int listener_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (listener_fd < 0) {
    return -1;
  }

  int reuse = 1;
  if (setsockopt(listener_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) != 0) {
    close(listener_fd);
    return -1;
  }

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (bind(listener_fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0 || listen(listener_fd, 1) != 0) {
    close(listener_fd);
    return -1;
  }

  socklen_t address_length = sizeof(address);
  if (getsockname(listener_fd, reinterpret_cast<sockaddr *>(&address), &address_length) != 0) {
    close(listener_fd);
    return -1;
  }
  port = ntohs(address.sin_port);
  return listener_fd;
}

int ConnectClient(const uint16_t port) {
  const int client_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (client_fd < 0) {
    return -1;
  }

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(port);
  if (connect(client_fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0) {
    close(client_fd);
    return -1;
  }
  return client_fd;
}

bool IsOpen(const int fd) {
  errno = 0;
  return fd >= 0 && fcntl(fd, F_GETFD) != -1;
}
}  // namespace

TEST(MsgHandlerPluginTest, ClosesAcceptedFdWhenTaskSubmissionFails) {
  uint16_t port = 0;
  const int listener_fd = CreateListener(port);
  ASSERT_GE(listener_fd, 0);
  const int client_fd = ConnectClient(port);
  ASSERT_GE(client_fd, 0);

  llm::MsgHandlerPlugin plugin;
  plugin.thread_pool_ = std::make_unique<llm::LLMThreadPool>("test", 1U);
  plugin.thread_pool_->Destroy();
  plugin.listen_fd_ = listener_fd;
  plugin.listener_running_ = true;
  g_accepted_fd = -1;

  EXPECT_EQ(plugin.DoAccept(), ge::SUCCESS);
  ASSERT_GE(g_accepted_fd, 0);
  EXPECT_FALSE(IsOpen(g_accepted_fd));

  plugin.listener_running_ = false;
  plugin.listen_fd_ = -1;
  close(client_fd);
  close(listener_fd);
}
