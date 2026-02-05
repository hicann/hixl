/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "ctrl_msg_plugin.h"
#include <netinet/tcp.h>
#include <csignal>
#include <chrono>
#include <sys/poll.h>
#include "securec.h"
#include "hixl_utils.h"
#include "hixl_checker.h"
#include "scope_guard.h"

namespace hixl {
void CtrlMsgPlugin::Initialize() {
  (void) std::signal(SIGPIPE, SIG_IGN);
}

Status CtrlMsgPlugin::GetAiFamily(const std::string &ip, int32_t &ai_family) {
  struct sockaddr_in ipv4_addr;
  struct sockaddr_in6 ipv6_addr;
  (void)memset_s(&ipv4_addr, sizeof(ipv4_addr), 0, sizeof(ipv4_addr));
  if (inet_pton(AF_INET, ip.c_str(), &ipv4_addr.sin_addr) == 1) {
    ai_family = AF_INET;
    return SUCCESS;
  }

  (void)memset_s(&ipv6_addr, sizeof(ipv6_addr), 0, sizeof(ipv6_addr));
  if (inet_pton(AF_INET6, ip.c_str(), &ipv6_addr.sin6_addr) == 1) {
    ai_family = AF_INET6;
    return SUCCESS;
  }
  HIXL_LOGE(PARAM_INVALID, "Failed to get ai_family, ip:%s, error msg:%s, errno:%d",
            ip.c_str(), strerror(errno), errno);
  return PARAM_INVALID;
}

Status CtrlMsgPlugin::Connect(const std::string &ip, uint32_t port, int32_t &conn_fd,
                              int32_t timeout) {
  HIXL_EVENT("connect to server %s:%u begin", ip.c_str(), port);
  auto start = std::chrono::high_resolution_clock::now();
  struct ::addrinfo hints;
  struct ::addrinfo *result = nullptr;
  struct ::addrinfo *rp = nullptr;
  (void)memset_s(&hints, sizeof(hints), 0, sizeof(hints));
  HIXL_CHK_STATUS_RET(GetAiFamily(ip, hints.ai_family), "Failed to get ai_family, ip:%s", ip.c_str());
  hints.ai_socktype = SOCK_STREAM;

  auto socket_ret = getaddrinfo(ip.c_str(), std::to_string(port).c_str(), &hints, &result);
  HIXL_CHK_BOOL_RET_STATUS(socket_ret == 0,
                           PARAM_INVALID,
                           "Failed to get IP address of peer %s:%u, please check addr and port, "
                           "socket_ret:%d, error msg:%s, errno:%d",
                           ip.c_str(), port, socket_ret, strerror(errno), errno);
  HIXL_MAKE_GUARD(free_addr, ([result]() {
    freeaddrinfo(result);
  }));

  Status ret = SUCCESS;
  int32_t err_no = 0; // for last error record
  for (rp = result; rp != nullptr; rp = rp->ai_next) {
    ret = DoConnect(rp, conn_fd, err_no, timeout);
    if (ret == SUCCESS) {
      HIXL_EVENT("connect to server %s:%u success, conn_fd:%d", ip.c_str(), port, conn_fd);
      break;
    }
    auto now = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start);
    HIXL_CHK_BOOL_RET_STATUS(elapsed.count() < timeout, TIMEOUT,
                             "connect to the peer %s:%u timed out, timeout:%d ms.",
                             ip.c_str(), port, timeout);
  }
  if (ret != SUCCESS) {
    HIXL_LOGE(ret, "Failed to connect peer %s:%u, error msg:%s, errno:%d",
              ip.c_str(), port, strerror(err_no), err_no);
  }
  return ret;
}

Status CtrlMsgPlugin::DoConnect(struct ::addrinfo *addr, int32_t &conn_fd, int32_t &err_no,
                                int32_t timeout) {
  int32_t on = 1;
  HIXL_LOGI("Attempting to create socket with family:%d, type:%d, protocol:%d",
            addr->ai_family, addr->ai_socktype, addr->ai_protocol);
  HIXL_DISMISSABLE_GUARD(record_err, ([&err_no]() {
    err_no = errno;
  }));
  conn_fd = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
  HIXL_CHK_BOOL_RET_SPECIAL_STATUS(conn_fd == -1, FAILED,
                                   "Try to create socket, error msg:%s, errno:%d", strerror(errno), errno);

  HIXL_DISMISSABLE_GUARD(close_fd, ([conn_fd]() {
    (void)close(conn_fd);
  }));
  auto socket_ret = setsockopt(conn_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
  HIXL_CHK_BOOL_RET_SPECIAL_STATUS(socket_ret != 0, SUCCESS,
                                   "Try to setsockopt(SO_REUSEADDR), socket_ret:%d, error msg:%s, errno:%d",
                                   socket_ret, strerror(errno), errno);
  constexpr int64_t kTimeInSec = 1000;
  struct timeval socket_timeout;
  socket_timeout.tv_sec = static_cast<int64_t>(timeout) / kTimeInSec;
  socket_timeout.tv_usec = (static_cast<int64_t>(timeout) % kTimeInSec) * kTimeInSec;
  socket_ret = setsockopt(conn_fd, SOL_SOCKET, SO_RCVTIMEO,  &socket_timeout, sizeof(socket_timeout));
  HIXL_CHK_BOOL_RET_SPECIAL_STATUS(socket_ret != 0, FAILED,
                                   "Try to setsockopt(SO_RCVTIMEO), socket_ret:%d, error msg:%s, errno:%d",
                                   socket_ret, strerror(errno), errno);
  int32_t flag = 1;
  socket_ret = setsockopt(conn_fd, IPPROTO_TCP, TCP_NODELAY,  &flag, sizeof(flag));
  HIXL_CHK_BOOL_RET_SPECIAL_STATUS(socket_ret != 0, FAILED,
                                   "Try to setsockopt(TCP_NODELAY), socket_ret:%d, error msg:%s, errno:%d",
                                   socket_ret, strerror(errno), errno);
  socket_ret = setsockopt(conn_fd, SOL_SOCKET, SO_SNDTIMEO,  &socket_timeout, sizeof(socket_timeout));
  HIXL_CHK_BOOL_RET_SPECIAL_STATUS(socket_ret != 0, FAILED,
                                   "Try to setsockopt(SO_SNDTIMEO), socket_ret:%d, error msg:%s, errno:%d",
                                   socket_ret, strerror(errno), errno);
  socket_ret = connect(conn_fd, addr->ai_addr, addr->ai_addrlen);
  HIXL_CHK_BOOL_RET_SPECIAL_STATUS(socket_ret != 0, FAILED,
                                   "Try to socket connect, socket_ret:%d, error msg:%s, errno:%d",
                                   socket_ret, strerror(errno), errno);
  HIXL_DISMISS_GUARD(close_fd);
  HIXL_DISMISS_GUARD(record_err);
  return SUCCESS;
}

Status CtrlMsgPlugin::SockAddrInit(const std::string &ip, uint32_t listen_port, int32_t ai_family,
                                   struct sockaddr_storage &server_addr, socklen_t &addr_len) {
  if (ai_family == AF_INET) {
    struct sockaddr_in* addr = reinterpret_cast<struct sockaddr_in*>(&server_addr);
    (void)memset_s(addr, sizeof(*addr), 0, sizeof(*addr));
    addr->sin_family = AF_INET;
    addr->sin_port = htons(listen_port);
    (void) inet_pton(AF_INET, ip.c_str(), &addr->sin_addr);
    addr_len = sizeof(*addr);
  } else {
    struct sockaddr_in6* addr = reinterpret_cast<struct sockaddr_in6*>(&server_addr);
    (void)memset_s(addr, sizeof(*addr), 0, sizeof(*addr));
    addr->sin6_family = AF_INET6;
    addr->sin6_port = htons(listen_port);
    (void) inet_pton(AF_INET6, ip.c_str(), &addr->sin6_addr);
    addr_len = sizeof(*addr);
  }
  return SUCCESS;
}

Status CtrlMsgPlugin::Listen(const std::string &ip, uint32_t listen_port, int32_t backlog, int32_t &listen_fd) {
  int32_t ai_family = 0;
  HIXL_CHK_STATUS_RET(GetAiFamily(ip, ai_family), "Failed to get ai_family, ip:%s", ip.c_str());
  listen_fd = socket(ai_family, SOCK_STREAM | SOCK_NONBLOCK, 0);
  HIXL_CHK_BOOL_RET_STATUS(listen_fd >= 0, FAILED, "Failed to create socket.");
  HIXL_DISMISSABLE_GUARD(fail_guard, ([listen_fd]() {
    (void)close(listen_fd);
  }));

  struct sockaddr_storage server_addr;
  socklen_t addr_len;
  HIXL_CHK_STATUS_RET(SockAddrInit(ip, listen_port, ai_family, server_addr, addr_len),
                     "Failed to init sockaddr, ip:%s", ip.c_str());

  if (ai_family == AF_INET6) {
    int v6only = 1;
    (void) setsockopt(listen_fd, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only));
  }
  struct timeval timeout;
  timeout.tv_sec = 1;
  timeout.tv_usec = 0;
  auto socket_ret = setsockopt(listen_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  HIXL_CHK_BOOL_RET_STATUS(socket_ret == 0, FAILED,
                           "Failed to set socket opt timeout, socket_ret:%d, error msg:%s, errno:%d",
                           socket_ret, strerror(errno), errno);
  int32_t on = 1;
  socket_ret = setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
  HIXL_CHK_BOOL_RET_STATUS(socket_ret == 0, FAILED,
                           "Failed to set socket opt SO_REUSEADDR, socket_ret:%d, error msg:%s, errno:%d",
                           socket_ret, strerror(errno), errno);
  HIXL_CHK_BOOL_RET_STATUS(bind(listen_fd, reinterpret_cast<sockaddr *>(&server_addr),
                                addr_len) >= 0,
                           FAILED, "Failed to bind port:%u, error msg:%s, errno:%d.",
                           listen_port, strerror(errno), errno);
  socket_ret = listen(listen_fd, backlog);
  HIXL_CHK_BOOL_RET_STATUS(socket_ret == 0, FAILED, "Failed to listen, socket_ret:%d, error msg:%s, errno:%d",
                           socket_ret, strerror(errno), errno);
  HIXL_DISMISS_GUARD(fail_guard);
  HIXL_EVENT("listen on %s:%d success, listen_fd:%d", ip.c_str(), listen_port, listen_fd);
  return SUCCESS;
}

Status CtrlMsgPlugin::AddFdToEpoll(int32_t &epoll_fd, int32_t fd, uint32_t events) {
  HIXL_CHK_BOOL_RET_STATUS(fd >= 0, FAILED, "Invalid fd:%d", fd);
  if (epoll_fd < 0) {
    epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    HIXL_CHK_BOOL_RET_STATUS(epoll_fd >= 0, FAILED, "Create epoll failed, errno:%d, msg:%s",
                              errno, strerror(errno));
    HIXL_EVENT("Create epoll success, epoll_fd:%d", epoll_fd);
  }

  struct epoll_event ev;
  (void)memset_s(&ev, sizeof(ev), 0, sizeof(ev));
  ev.events = events;
  ev.data.fd = fd;

  int32_t ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev);
  HIXL_CHK_BOOL_RET_STATUS(ret == 0, FAILED,
                            "Add fd to epoll failed, fd:%d, ret:%d, errno:%d, msg:%s",
                            fd, ret, errno, strerror(errno));

  HIXL_LOGI("Add fd:%d to epoll success, events:0x%x\n", fd, events);
  return SUCCESS;
}

Status CtrlMsgPlugin::Accept(int32_t listen_fd, int32_t &conn_fd) {
  HIXL_LOGI("Socket accept begin, listen_fd:%d", listen_fd);
  struct sockaddr_storage addr;
  socklen_t addr_len = sizeof(addr);
  conn_fd = accept(listen_fd, reinterpret_cast<sockaddr *>(&addr), &addr_len);
  if (conn_fd < 0) {
    HIXL_CHK_BOOL_RET_STATUS(errno == EWOULDBLOCK || errno == EINTR || errno == ECONNABORTED, FAILED,
                             "Failed to accept, error msg=%s, errno=%d",
                             strerror(errno), errno);
  }
  HIXL_EVENT("accept success, listen_fd:%d, conn_fd:%d", listen_fd, conn_fd);
  return SUCCESS;
}

Status CtrlMsgPlugin::Send(int32_t fd, const void *buf, size_t len) {
  HIXL_LOGI("Socket write begin: %zu bytes, fd:%d", len, fd);
  const char *pos = static_cast<const char *>(buf);
  auto nbytes = static_cast<ssize_t>(len);
  while (nbytes > 0) {
    auto rc = write(fd, pos, static_cast<size_t>(nbytes));
    if (rc < 0 && (errno == EAGAIN || errno == EINTR)) {
      HIXL_LOGI("Socket write need to eagain");
      continue;
    } else if (rc <= 0) {
      HIXL_LOGE(FAILED, "Socket write failed, error msg:%s, errno:%d", strerror(errno), errno);
      return FAILED;
    }
    pos += rc;
    nbytes -= rc;
  }
  HIXL_LOGI("Socket write completed: %zu bytes, fd:%d", len, fd);
  return SUCCESS;
}

Status CtrlMsgPlugin::Recv(int32_t fd, void *buf, size_t len, uint32_t timeout_ms) {
  HIXL_LOGI("Socket read begin: %zu bytes, fd:%d", len, fd);
  auto pos = static_cast<uint8_t *>(buf);
  auto nbytes = static_cast<ssize_t>(len);
  auto start_time = std::chrono::steady_clock::now();
  while (nbytes > 0) {
    auto now = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();
        int64_t remaining_ms = static_cast<int64_t>(timeout_ms) - elapsed_ms;
    if (remaining_ms <= 0) {
      HIXL_LOGE(TIMEOUT, "Socket read timeout! Target: %zu bytes, Left: %zd bytes, Elapsed: %ld ms",
                len, nbytes, elapsed_ms);
      return TIMEOUT;
    }
    struct pollfd pfd = {};
    pfd.fd = fd;
    pfd.events = POLLIN;
    int ret = poll(&pfd, 1, static_cast<int>(remaining_ms));
    if (ret == 0) {
      HIXL_LOGE(TIMEOUT, "Socket read poll timeout! Waited %ld ms", remaining_ms);
      return TIMEOUT;
    } else if (ret < 0) {
      if (errno == EINTR) {
        continue;
      }
      HIXL_LOGE(FAILED, "Socket poll failed, errno:%d, msg:%s", errno, strerror(errno));
      return FAILED;
    }

    auto rc = read(fd, pos, static_cast<size_t>(nbytes));
    if (rc < 0 && (errno == EAGAIN || errno == EINTR)) {
      HIXL_LOGI("Socket read need to eagain");
      continue;
    } else if (rc <= 0) {
      HIXL_LOGE(FAILED, "Socket read failed, error msg:%s, errno:%d", strerror(errno), errno);
      return FAILED;
    }
    pos += rc;
    nbytes -= rc;
  }
  HIXL_LOGI("Socket read completed: %zu bytes, fd:%d", len, fd);
  return SUCCESS;
}
}  // namespace hixl
