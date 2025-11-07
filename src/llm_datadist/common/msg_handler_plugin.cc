/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "msg_handler_plugin.h"
#include <netinet/tcp.h>
#include "common/llm_utils.h"
#include "common/llm_checker.h"
#include "common/llm_scope_guard.h"

namespace llm {
namespace {
constexpr int32_t kListenBacklog = 128;
constexpr int64_t kDefaultSleepTime = 1;
}

ge::Status MsgHandlerPlugin::Connect(const std::string &ip, uint32_t port, int32_t &conn_fd,
                                     int32_t timeout, ge::Status default_err) {
  auto start = std::chrono::high_resolution_clock::now();
  struct ::addrinfo hints;
  struct ::addrinfo *result = nullptr;
  struct ::addrinfo *rp = nullptr;
  (void)memset_s(&hints, sizeof(hints), 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  auto socket_ret = getaddrinfo(ip.c_str(), std::to_string(port).c_str(), &hints, &result);
  LLM_CHK_BOOL_RET_STATUS(socket_ret == 0,
                         ge::LLM_PARAM_INVALID,
                         "Failed to get IP address of peer %s:%u, please check addr and port, "
                         "socket_ret:%d, error msg:%s, errno:%d",
                         ip.c_str(), port, socket_ret, strerror(errno), errno);
  LLM_MAKE_GUARD(free_addr, ([result]() { freeaddrinfo(result); }));

  ge::Status ret = ge::SUCCESS;
  int32_t err_no = 0; // for last error record
  for (rp = result; rp != nullptr; rp = rp->ai_next) {
    ret = DoConnect(rp, conn_fd, err_no, timeout, default_err);
    if (ret == ge::SUCCESS) {
      break;
    }
    LLM_CHK_BOOL_RET_STATUS(!LLMUtils::IsTimeout(start, timeout), ge::LLM_TIMEOUT,
                           "connect to the peer %s:%u timed out, timeout:%d ms.",
                           ip.c_str(), port, timeout);
  }
  if (ret != ge::SUCCESS) {
    LLMLOGE(ret, "Failed to connect peer %s:%u, error msg:%s, errno:%d",
           ip.c_str(), port, strerror(err_no), err_no);
  }
  return ret;
}

ge::Status MsgHandlerPlugin::DoConnect(struct ::addrinfo *addr, int32_t &conn_fd, int32_t &err_no,
                                       int32_t timeout, ge::Status default_err) {
  int32_t on = 1;
  LLMLOGI("Attempting to create socket with family:%d, type:%d, protocol:%d",
         addr->ai_family, addr->ai_socktype, addr->ai_protocol);
  LLM_DISMISSABLE_GUARD(record_err, ([&err_no]() { err_no = errno; }));
  conn_fd = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
  LLM_CHK_BOOL_RET_SPECIAL_STATUS(conn_fd == -1, default_err,
                                 "Try to create socket, error msg:%s, errno:%d", strerror(errno), errno);

  LLM_DISMISSABLE_GUARD(close_fd, ([conn_fd]() { close(conn_fd); }));
  auto socket_ret = setsockopt(conn_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
  LLM_CHK_BOOL_RET_SPECIAL_STATUS(socket_ret != 0, default_err,
                                 "Try to setsockopt(SO_REUSEADDR), socket_ret:%d, error msg:%s, errno:%d",
                                 socket_ret, strerror(errno), errno);
  constexpr int32_t kTimeInSec = 1000;
  struct timeval socket_timeout;
  socket_timeout.tv_sec = timeout / kTimeInSec;
  socket_timeout.tv_usec = (timeout % kTimeInSec) * kTimeInSec;
  socket_ret = setsockopt(conn_fd, SOL_SOCKET, SO_RCVTIMEO,  &socket_timeout, sizeof(socket_timeout));
  LLM_CHK_BOOL_RET_SPECIAL_STATUS(socket_ret != 0, default_err,
                                 "Try to setsockopt(SO_RCVTIMEO), socket_ret:%d, error msg:%s, errno:%d",
                                 socket_ret, strerror(errno), errno);
  int32_t flag = 1;
  socket_ret = setsockopt(conn_fd, IPPROTO_TCP, TCP_NODELAY,  &flag, sizeof(flag));
  LLM_CHK_BOOL_RET_SPECIAL_STATUS(socket_ret != 0, default_err,
                                 "Try to setsockopt(TCP_NODELAY), socket_ret:%d, error msg:%s, errno:%d",
                                 socket_ret, strerror(errno), errno);
  socket_ret = setsockopt(conn_fd, SOL_SOCKET, SO_SNDTIMEO,  &socket_timeout, sizeof(socket_timeout));
  LLM_CHK_BOOL_RET_SPECIAL_STATUS(socket_ret != 0, default_err,
                                 "Try to setsockopt(SO_SNDTIMEO), socket_ret:%d, error msg:%s, errno:%d",
                                 socket_ret, strerror(errno), errno);
  socket_ret = connect(conn_fd, addr->ai_addr, addr->ai_addrlen);
  LLM_CHK_BOOL_RET_SPECIAL_STATUS(socket_ret != 0, default_err,
                                 "Try to socket connect, socket_ret:%d, error msg:%s, errno:%d",
                                 socket_ret, strerror(errno), errno);
  LLM_DISMISS_GUARD(close_fd);
  LLM_DISMISS_GUARD(record_err);
  return ge::SUCCESS;
}

void MsgHandlerPlugin::Disconnect(int32_t conn_fd) {
  close(conn_fd);
}

void MsgHandlerPlugin::ListenClose() {
  if (listen_fd_ >= 0) {
    close(listen_fd_);
    listen_fd_ = -1;
  }
}

ssize_t MsgHandlerPlugin::Write(int32_t fd, const void *buf, size_t len) {
  const char *pos = static_cast<const char *>(buf);
  size_t nbytes = len;
  while (nbytes > 0U) {
    auto rc = write(fd, pos, nbytes);
    if (rc < 0 && (errno == EAGAIN || errno == EINTR)) {
      continue;
    } else if (rc < 0) {
      LLMLOGE(ge::FAILED, "Socket write failed, error msg:%s, errno:%d", strerror(errno), errno);
      return rc;
    } else if (rc == 0) {
      LLMLOGW("Socket write incompleted: expected %zu bytes, actual %zu bytes", len, len - nbytes);
      return static_cast<ssize_t>(len - nbytes);
    }
    pos += rc;
    nbytes -= rc;
  }
  LLMLOGI("Socket write completed: %zu bytes", len);
  return static_cast<ssize_t>(len);
}

ssize_t MsgHandlerPlugin::Read(int32_t fd, void *buf, size_t len) {
  auto pos = static_cast<uint8_t *>(buf);
  size_t nbytes = len;
  while (nbytes > 0U) {
    auto rc = read(fd, pos, nbytes);
    if (rc < 0 && (errno == EAGAIN || errno == EINTR)) {
      continue;
    } else if (rc < 0) {
      LLMLOGE(ge::FAILED, "Socket read failed, error msg:%s, errno:%d", strerror(errno), errno);
      return rc;
    } else if (rc == 0) {
      LLMLOGW("Socket read incompleted: expected %zu bytes, actual %zu bytes", len, len - nbytes);
      return static_cast<ssize_t>(len - nbytes);
    }
    pos += rc;
    nbytes -= rc;
  }
  return static_cast<ssize_t>(len);
}

void MsgHandlerPlugin::RegisterConnectedProcess(ConnectedProcess proc) {
  connected_process_ = proc;
}

ge::Status MsgHandlerPlugin::DoConnectedProcess(int32_t conn_fd) {
  LLM_DISMISSABLE_GUARD(close_fd, ([conn_fd]() { close(conn_fd); }));
  LLM_CHK_BOOL_RET_STATUS(rtCtxSetCurrent(rt_context_) == RT_ERROR_NONE, ge::LLM_PARAM_INVALID,
                         "Set runtime context failed.");
  constexpr int32_t kTimeInSec = 60;
  struct timeval timeout;
  timeout.tv_sec = kTimeInSec;
  timeout.tv_usec = 0;
  auto socket_ret = setsockopt(conn_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  LLM_CHK_BOOL_RET_STATUS(socket_ret == 0, ge::FAILED,
                         "Failed to setsockopt(SO_RCVTIMEO), socket_ret:%d, error msg:%s, errno:%d",
                         socket_ret, strerror(errno), errno);
  int32_t flag = 1;
  socket_ret = setsockopt(conn_fd, IPPROTO_TCP, TCP_NODELAY,  &flag, sizeof(flag));
  LLM_CHK_BOOL_RET_SPECIAL_STATUS(socket_ret != 0, ge::FAILED,
                                 "Try to setsockopt(TCP_NODELAY), socket_ret:%d, error msg:%s, errno:%d",
                                 socket_ret, strerror(errno), errno);
  bool keep_fd = false;
  connected_process_(conn_fd, keep_fd);
  if (keep_fd) {
    LLM_DISMISS_GUARD(close_fd);
    return ge::SUCCESS;
  }
  socket_ret = shutdown(conn_fd, SHUT_RDWR);
  LLM_CHK_BOOL_RET_STATUS(socket_ret == 0, ge::FAILED,
                         "Failed to shutdown conn_fd, connection may be incomplete, "
                         "socket_ret:%d, error msg:%s, errno:%d",
                         socket_ret, strerror(errno), errno);
  // Wait for the client to close the connection
  char byte;
  auto rc = read(conn_fd, &byte, sizeof(byte));
  LLM_CHK_BOOL_RET_STATUS(rc == 0U,
                         ge::FAILED, "Failed to wait client close, byte = %d, rc = %zu",
                         static_cast<int32_t>(byte), static_cast<size_t>(rc));
  return ge::SUCCESS;
}

ge::Status MsgHandlerPlugin::DoAccept() {
  sockaddr_in addr;
  socklen_t addr_len = sizeof(sockaddr_in);
  auto conn_fd = accept(listen_fd_, reinterpret_cast<sockaddr *>(&addr), &addr_len);
  if (conn_fd < 0) {
    LLM_CHK_BOOL_RET_STATUS(errno == EWOULDBLOCK || errno == EINTR || errno == ECONNABORTED, ge::FAILED,
                           "Failed to accept, error msg=%s, errno=%d",
                           strerror(errno), errno);
    return ge::SUCCESS;
  }
  LLM_DISMISSABLE_GUARD(close_fd, ([conn_fd]() { close(conn_fd); }));
  LLMLOGI("accept success, fd:%d, addr.sin_family:%d", conn_fd, addr.sin_family);
  if (addr.sin_family == AF_INET || addr.sin_family == AF_INET6) {
    (void)thread_pool_->commit([this, conn_fd]() -> void { (void)DoConnectedProcess(conn_fd); });
    LLM_DISMISS_GUARD(close_fd);
  }
  return ge::SUCCESS;
}

ge::Status MsgHandlerPlugin::StartDaemon(uint32_t listen_port) {
  LLM_ASSERT_RT_OK(rtCtxGetCurrent(&rt_context_));
  sockaddr_in bind_address;
  int32_t on = 1;
  (void)memset_s(&bind_address, sizeof(sockaddr_in), 0, sizeof(sockaddr_in));
  bind_address.sin_family = AF_INET;
  bind_address.sin_port = htons(listen_port);
  bind_address.sin_addr.s_addr = INADDR_ANY;

  listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
  LLM_CHK_BOOL_RET_STATUS(listen_fd_ >= 0, ge::FAILED, "Failed to create socket.");

  LLM_DISMISSABLE_GUARD(fail_guard, ([this]() { ListenClose(); }));

  struct timeval timeout;
  timeout.tv_sec = 1;
  timeout.tv_usec = 0;
  auto socket_ret = setsockopt(listen_fd_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  LLM_CHK_BOOL_RET_STATUS(socket_ret == 0, ge::FAILED,
                         "Failed to set socket opt timeout, socket_ret:%d, error msg:%s, errno:%d",
                         socket_ret, strerror(errno), errno);
  socket_ret = setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
  LLM_CHK_BOOL_RET_STATUS(socket_ret == 0, ge::FAILED,
                         "Failed to set socket opt SO_REUSEADDR, socket_ret:%d, error msg:%s, errno:%d",
                         socket_ret, strerror(errno), errno);
  LLM_CHK_BOOL_RET_STATUS(bind(listen_fd_, reinterpret_cast<sockaddr *>(&bind_address),
                              sizeof(sockaddr_in)) >= 0,
                         ge::FAILED, "Failed to bind port:%u, error msg:%s, errno:%d.",
                         listen_port, strerror(errno), errno);
  socket_ret = listen(listen_fd_, kListenBacklog);
  LLM_CHK_BOOL_RET_STATUS(socket_ret == 0, ge::FAILED, "Failed to listen, socket_ret:%d, error msg:%s, errno:%d",
                         socket_ret, strerror(errno), errno);
  constexpr uint32_t kThreadPoolSize = 16U;
  thread_pool_ = MakeUnique<LLMThreadPool>("ge_llm_mhp", kThreadPoolSize);
  LLM_CHECK_NOTNULL(thread_pool_);
  listener_running_ = true;
  listener_ = std::thread([this]() {
    while (listener_running_) {
      auto ret = DoAccept();
      if (ret != ge::SUCCESS) {
        std::this_thread::sleep_for(std::chrono::seconds(kDefaultSleepTime));
      }
    }
    return;
  });

  LLM_DISMISS_GUARD(fail_guard);
  return ge::SUCCESS;
}

ge::Status MsgHandlerPlugin::SendMsg(int32_t fd, int32_t msg_type, const std::string &msg_str) {
  uint64_t length = msg_str.size() + sizeof(msg_type);
  auto len = Write(fd, &length, sizeof(length));
  LLM_CHK_BOOL_RET_STATUS(len == static_cast<ssize_t>(sizeof(length)), ge::FAILED,
                         "Failed to send msg len:%zu, expect write len:%zu, actually write len:%zd",
                         length, sizeof(length), len);
  len = Write(fd, &msg_type, sizeof(msg_type));
  LLM_CHK_BOOL_RET_STATUS(len == static_cast<ssize_t>(sizeof(msg_type)),
                         ge::FAILED, "Failed to send msg type:%d, expect write len:%zu, actually write len:%zd",
                         msg_type, sizeof(msg_type), len);
  len = Write(fd, msg_str.c_str(), msg_str.size());
  LLM_CHK_BOOL_RET_STATUS(len == static_cast<ssize_t>(msg_str.size()),
                         ge::FAILED, "Failed to send msg:%s, expect write len:%zu, actually write len:%zd",
                         msg_str.c_str(), msg_str.size(), len);
  return ge::SUCCESS;
}

ge::Status MsgHandlerPlugin::RecvMsg(int32_t fd, int32_t &msg_type, std::vector<char> &msg) {
  const static size_t kMaxLength = 1ULL << 20;
  uint64_t length = 0;
  auto n = Read(fd, &length, sizeof(length));
  LLM_CHK_BOOL_RET_STATUS(n == static_cast<ssize_t>(sizeof(length)),
                         ge::FAILED, "Failed to recv msg len:%zd, expect len:%zu", n, sizeof(length));
  LLM_CHK_BOOL_RET_STATUS(length <= kMaxLength && length > sizeof(int32_t),
                         ge::FAILED, "Failed to check msg len:%lu, must in range: (%zu, %zu]",
                         length, sizeof(int32_t), kMaxLength);
  int32_t type = 0;
  n = Read(fd, &type, sizeof(type));
  LLM_CHK_BOOL_RET_STATUS(n == static_cast<ssize_t>(sizeof(type)),
                         ge::FAILED, "Failed to recv msg type len:%zd, expect len:%zu", n, sizeof(type));
  msg_type = type;
  size_t msg_len = static_cast<size_t>(length) - sizeof(int32_t);
  msg.resize(msg_len + 1U);
  n = Read(fd, msg.data(), msg_len);
  LLM_CHK_BOOL_RET_STATUS(n == static_cast<ssize_t>(msg_len),
                         ge::FAILED, "Failed to check recv msg type:%d, recv msg len:%zd, expect len:%zu",
                         type, n, msg_len);
  msg[msg_len] = '\0';
  return ge::SUCCESS;
}

ge::Status MsgHandlerPlugin::RecvMsg(int32_t fd, int32_t &msg_type, std::vector<char> &msg, uint64_t length) {
  int32_t type = 0;
  auto n = Read(fd, &type, sizeof(type));
  LLM_CHK_BOOL_RET_STATUS(n == static_cast<ssize_t>(sizeof(type)),
                         ge::FAILED, "Failed to recv msg type len:%zd, expect len:%zu", n, sizeof(type));
  msg_type = type;
  size_t msg_len = static_cast<size_t>(length) - sizeof(int32_t);
  msg.resize(msg_len + 1U);
  n = Read(fd, msg.data(), msg_len);
  LLM_CHK_BOOL_RET_STATUS(n == static_cast<ssize_t>(msg_len),
                         ge::FAILED, "Failed to check recv msg type:%d, recv msg len:%zd, expect len:%zu",
                         type, n, msg_len);
  msg[msg_len] = '\0';
  return ge::SUCCESS;
}

MsgHandlerPlugin::~MsgHandlerPlugin() {
  if (listener_running_) {
    Finalize();
  }
}

void MsgHandlerPlugin::Finalize() {
  ListenClose();
  if (listener_running_) {
    listener_running_ = false;
    listener_.join();
  }
}
}  // namespace llm
