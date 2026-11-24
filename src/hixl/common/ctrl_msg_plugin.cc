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
#include <cerrno>
#include <cstring>
#include <csignal>
#include <chrono>
#include <limits>
#include <algorithm>
#include <fcntl.h>
#include <sys/poll.h>
#include "securec.h"
#include "hixl_checker.h"
#include "scope_guard.h"
#include "common/tls/tls_manager.h"

namespace hixl {
namespace {
std::once_flag g_signal_initialized;
constexpr int32_t kKeepAlive = 1;      // 启用 keepalive 机制
constexpr int32_t kTcpKeepIdle = 60;   // 连接空闲 60 秒后开始发送 keepalive 探测包
constexpr int32_t kTcpKeepIntvl = 10;  // keepalive 探测包发送间隔
constexpr int32_t kTcpKeepCnt = 6;     // 连续 6 次探测失败后认为连接断开
constexpr size_t kMaxTransferSize = static_cast<size_t>(std::numeric_limits<ssize_t>::max());

std::string SocketAddress(int32_t fd, bool peer) {
  sockaddr_storage address{};
  socklen_t size = sizeof(address);
  auto *socket_address = reinterpret_cast<sockaddr *>(&address);
  const int ret = peer ? getpeername(fd, socket_address, &size) : getsockname(fd, socket_address, &size);
  if (ret != 0 || (address.ss_family != AF_INET && address.ss_family != AF_INET6)) {
    return "unknown";
  }
  char host[NI_MAXHOST] = {};
  char service[NI_MAXSERV] = {};
  if (getnameinfo(socket_address, size, host, sizeof(host), service, sizeof(service),
                  NI_NUMERICHOST | NI_NUMERICSERV) != 0) {
    return "unknown";
  }
  if (address.ss_family == AF_INET6) {
    return "[" + std::string(host) + "]:" + service;
  }
  return std::string(host) + ":" + service;
}

Status WaitSocketReady(int32_t fd, short events, int32_t timeout_ms, bool &ready) {
  ready = false;
  pollfd event{fd, events, 0};
  const int ret = poll(&event, 1, timeout_ms);
  if (ret < 0 && errno == EINTR) {
    return SUCCESS;
  }
  HIXL_CHK_BOOL_RET_STATUS(ret >= 0, FAILED, "Call api:poll failed, fd:%d, ret:%d, errno:%d, error_msg:%s", fd, ret,
                           errno, strerror(errno));
  ready = ret > 0;
  return SUCCESS;
}

Status SendPlain(int32_t fd, const void *buf, size_t len, uint32_t timeout_ms, int32_t &err_no) {
  HIXL_LOGD("Socket write begin: %zu bytes, fd:%d, timeout:%u ms", len, fd, timeout_ms);
  const char *pos = static_cast<const char *>(buf);
  auto nbytes = static_cast<ssize_t>(len);
  const auto start = std::chrono::steady_clock::now();
  while (nbytes > 0) {
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    HIXL_CHK_BOOL_RET_STATUS(elapsed < static_cast<int64_t>(timeout_ms), TIMEOUT,
                             "Call api:send timed out, fd:%d, size:%zu bytes, remaining_size:%ld bytes, "
                             "elapsed:%ld ms, timeout:%u ms",
                             fd, len, nbytes, elapsed, timeout_ms);
    const auto rc = send(fd, pos, static_cast<size_t>(nbytes), MSG_DONTWAIT | MSG_NOSIGNAL);
    const int saved_errno = errno;
    if (rc < 0 && saved_errno == EINTR) {
      continue;
    }
    if (rc < 0 && (saved_errno == EAGAIN || saved_errno == EWOULDBLOCK)) {
      bool ready = false;
      // POLLOUT may require more free buffer space than this small control message needs.
      const auto wait_ms = static_cast<int32_t>(std::min<int64_t>(100, static_cast<int64_t>(timeout_ms) - elapsed));
      HIXL_CHK_STATUS_RET(WaitSocketReady(fd, POLLOUT, wait_ms, ready), "Failed to wait for socket send, fd:%d", fd);
      continue;
    } else if (rc <= 0) {
      err_no = saved_errno;
      HIXL_CHK_BOOL_RET_STATUS(false, FAILED,
                               "Call api:send failed, ret:%ld, fd:%d, remaining_size:%ld bytes, errno:%d, "
                               "error_msg:%s",
                               rc, fd, nbytes, err_no, strerror(err_no));
    }
    pos += rc;
    nbytes -= rc;
  }
  HIXL_LOGD("Socket write completed: %zu bytes, fd:%d", len, fd);
  return SUCCESS;
}

Status RecvPlain(int32_t fd, void *buf, size_t len, uint32_t timeout_ms) {
  HIXL_LOGI("Socket read begin: %zu bytes, fd:%d", len, fd);
  auto pos = static_cast<uint8_t *>(buf);
  auto nbytes = static_cast<ssize_t>(len);
  auto start_time = std::chrono::steady_clock::now();
  while (nbytes > 0) {
    auto now = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();
    int64_t remaining_ms = static_cast<int64_t>(timeout_ms) - elapsed_ms;
    if (remaining_ms <= 0) {
      HIXL_CHK_BOOL_RET_STATUS(false, TIMEOUT,
                               "Call api:read timed out, fd:%d, size:%zu bytes, remaining_size:%ld bytes, "
                               "elapsed:%ld ms, timeout:%u ms",
                               fd, len, nbytes, elapsed_ms, timeout_ms);
    }
    struct pollfd pfd = {};
    pfd.fd = fd;
    pfd.events = POLLIN;
    int ret = poll(&pfd, 1, static_cast<int>(remaining_ms));
    if (ret == 0) {
      HIXL_CHK_BOOL_RET_STATUS(false, TIMEOUT, "Call api:poll timed out, ret:%d, fd:%d, remaining_timeout:%ld ms", ret,
                               fd, remaining_ms);
    } else if (ret < 0) {
      const int32_t saved_errno = errno;
      if (saved_errno == EINTR) {
        continue;
      }
      HIXL_CHK_BOOL_RET_STATUS(false, FAILED,
                               "Call api:poll failed, ret:%d, fd:%d, remaining_timeout:%ld ms, errno:%d, "
                               "error_msg:%s",
                               ret, fd, remaining_ms, saved_errno, strerror(saved_errno));
    }

    auto rc = read(fd, pos, static_cast<size_t>(nbytes));
    const int32_t saved_errno = errno;
    if (rc < 0 && (saved_errno == EAGAIN || saved_errno == EINTR)) {
      HIXL_LOGI("Socket read returned EAGAIN/EINTR, need to retry");
      continue;
    } else if (rc <= 0) {
      HIXL_CHK_BOOL_RET_STATUS(false, FAILED,
                               "Call api:read failed, ret:%ld, fd:%d, remaining_size:%ld bytes, errno:%d, "
                               "error_msg:%s",
                               rc, fd, nbytes, saved_errno, strerror(saved_errno));
    }
    pos += rc;
    nbytes -= rc;
  }
  HIXL_LOGI("Socket read completed: %zu bytes, fd:%d", len, fd);
  return SUCCESS;
}

}  // namespace
CtrlMsgPlugin::~CtrlMsgPlugin() {
  (void)Close();
}

Status CtrlMsgPlugin::Initialize() {
  std::unique_lock<std::shared_mutex> lock(lifecycle_mutex_);
  std::call_once(g_signal_initialized, []() { (void)std::signal(SIGPIPE, SIG_IGN); });
  HIXL_CHK_BOOL_RET_STATUS(!initialized_, FAILED, "[CtrlMsgPlugin] Already initialized, fd:%d", fd_);
  HIXL_CHK_STATUS_RET(TlsManager::GetInstance().GetTlsEnabled(tls_enabled_), "Failed to get process TLS mode");
  local_address_ = "unknown";
  peer_address_ = "unknown";
  initialized_ = true;
  return SUCCESS;
}

int32_t CtrlMsgPlugin::Fd() const {
  std::shared_lock<std::shared_mutex> lock(lifecycle_mutex_);
  return fd_;
}

std::string CtrlMsgPlugin::Desc() const {
  std::shared_lock<std::shared_mutex> lock(lifecycle_mutex_);
  return "fd:" + std::to_string(fd_) + ", local:" + local_address_ + ", peer:" + peer_address_;
}

void CtrlMsgPlugin::UpdateSocketAddresses() {
  // Address discovery is best effort and must not change I/O error handling or emit extra failure logs.
  const int saved_errno = errno;
  local_address_ = SocketAddress(fd_, false);
  peer_address_ = SocketAddress(fd_, true);
  errno = saved_errno;
}

Status CtrlMsgPlugin::CheckReady(std::shared_ptr<TlsSession> &session) const {
  HIXL_CHK_BOOL_RET_STATUS(initialized_ && fd_ >= 0, FAILED, "[CtrlMsgPlugin] Invalid socket, fd:%d", fd_);
  session = tls_session_;
  HIXL_CHK_BOOL_RET_STATUS(!tls_enabled_ || (session != nullptr && session->IsUsable()), FAILED,
                           "[CtrlMsgPlugin] TLS session is not usable, fd:%d", fd_);
  return SUCCESS;
}

Status CtrlMsgPlugin::Connect(const std::string &ip, uint32_t port, int32_t timeout) {
  std::unique_lock<std::shared_mutex> lock(lifecycle_mutex_);
  HIXL_CHK_BOOL_RET_STATUS(initialized_, FAILED,
                           "[CtrlMsgPlugin] Cannot connect before initialization or after close, fd:%d", fd_);
  HIXL_CHK_BOOL_RET_STATUS(fd_ < 0, FAILED, "[CtrlMsgPlugin] Socket already exists for connect, fd:%d", fd_);
  const Status ret = ConnectSocket(ip, port, timeout);
  if (ret != SUCCESS) {
    (void)CloseLocked();
  }
  return ret;
}

Status CtrlMsgPlugin::Listen(const std::string &ip, uint32_t port, int32_t backlog) {
  std::unique_lock<std::shared_mutex> lock(lifecycle_mutex_);
  HIXL_CHK_BOOL_RET_STATUS(initialized_, FAILED,
                           "[CtrlMsgPlugin] Cannot listen before initialization or after close, fd:%d", fd_);
  HIXL_CHK_BOOL_RET_STATUS(fd_ < 0, FAILED, "[CtrlMsgPlugin] Socket already exists for listen, fd:%d", fd_);
  const Status ret = ListenSocket(ip, port, backlog);
  if (ret != SUCCESS) {
    (void)CloseLocked();
  }
  return ret;
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
  HIXL_LOGE(PARAM_INVALID, "Failed to get ai_family, ip:%s, error msg:%s, errno:%d", ip.c_str(), strerror(errno),
            errno);
  return PARAM_INVALID;
}

Status CtrlMsgPlugin::ConnectSocket(const std::string &ip, uint32_t port, int32_t timeout) {
  auto &conn_fd = fd_;
  HIXL_EVENT("connect to server %s:%u begin", ip.c_str(), port);
  auto start = std::chrono::high_resolution_clock::now();
  struct ::addrinfo hints;
  struct ::addrinfo *result = nullptr;
  struct ::addrinfo *rp = nullptr;
  (void)memset_s(&hints, sizeof(hints), 0, sizeof(hints));
  HIXL_CHK_STATUS_RET(GetAiFamily(ip, hints.ai_family), "Failed to get ai_family, ip:%s", ip.c_str());
  hints.ai_socktype = SOCK_STREAM;

  auto socket_ret = getaddrinfo(ip.c_str(), std::to_string(port).c_str(), &hints, &result);
  HIXL_CHK_BOOL_RET_STATUS(socket_ret == 0, PARAM_INVALID,
                           "Failed to get IP address of peer %s:%u, please check addr and port, "
                           "socket_ret:%d, error msg:%s, errno:%d",
                           ip.c_str(), port, socket_ret, strerror(errno), errno);
  HIXL_MAKE_GUARD(free_addr, ([result]() { freeaddrinfo(result); }));

  Status ret = SUCCESS;
  int32_t err_no = 0;  // for last error record
  for (rp = result; rp != nullptr; rp = rp->ai_next) {
    ret = DoConnect(rp, conn_fd, err_no, timeout);
    if (ret == SUCCESS) {
      HIXL_EVENT("connect to server %s:%u success, conn_fd:%d", ip.c_str(), port, conn_fd);
      break;
    }
    auto now = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start);
    HIXL_CHK_BOOL_RET_STATUS(elapsed.count() < timeout, TIMEOUT, "connect to the peer %s:%u timed out, timeout:%d ms.",
                             ip.c_str(), port, timeout);
  }
  if (ret != SUCCESS) {
    HIXL_LOGE(ret, "Failed to connect peer %s:%u, error msg:%s, errno:%d", ip.c_str(), port, strerror(err_no), err_no);
    return ret;
  }
  UpdateSocketAddresses();
  HIXL_CHK_STATUS_RET(Handshake(false, timeout), "[CtrlMsgPlugin] Client handshake failed, fd:%d", fd_);
  return SUCCESS;
}

Status CtrlMsgPlugin::DoConnect(const struct ::addrinfo *addr, int32_t &conn_fd, int32_t &err_no, int32_t timeout) {
  const int32_t on = 1;
  HIXL_LOGI("Attempting to create socket with family:%d, type:%d, protocol:%d", addr->ai_family, addr->ai_socktype,
            addr->ai_protocol);
  HIXL_DISMISSABLE_GUARD(record_err, ([&err_no]() { err_no = errno; }));
  conn_fd = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
  HIXL_CHK_BOOL_RET_SPECIAL_STATUS(conn_fd == -1, FAILED, "Try to create socket, error msg:%s, errno:%d",
                                   strerror(errno), errno);

  HIXL_DISMISSABLE_GUARD(close_fd, ([&conn_fd]() {
                           (void)close(conn_fd);
                           conn_fd = -1;
                         }));
  auto socket_ret = setsockopt(conn_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
  HIXL_CHK_BOOL_RET_SPECIAL_STATUS(socket_ret != 0, FAILED,
                                   "Try to setsockopt(SO_REUSEADDR), socket_ret:%d, error msg:%s, errno:%d", socket_ret,
                                   strerror(errno), errno);
  constexpr int64_t kTimeInSec = 1000;
  struct timeval socket_timeout;
  socket_timeout.tv_sec = static_cast<int64_t>(timeout) / kTimeInSec;
  socket_timeout.tv_usec = (static_cast<int64_t>(timeout) % kTimeInSec) * kTimeInSec;
  socket_ret = setsockopt(conn_fd, SOL_SOCKET, SO_RCVTIMEO, &socket_timeout, sizeof(socket_timeout));
  HIXL_CHK_BOOL_RET_SPECIAL_STATUS(socket_ret != 0, FAILED,
                                   "Try to setsockopt(SO_RCVTIMEO), socket_ret:%d, error msg:%s, errno:%d", socket_ret,
                                   strerror(errno), errno);
  const int32_t flag = 1;
  socket_ret = setsockopt(conn_fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
  HIXL_CHK_BOOL_RET_SPECIAL_STATUS(socket_ret != 0, FAILED,
                                   "Try to setsockopt(TCP_NODELAY), socket_ret:%d, error msg:%s, errno:%d", socket_ret,
                                   strerror(errno), errno);
  socket_ret = setsockopt(conn_fd, SOL_SOCKET, SO_SNDTIMEO, &socket_timeout, sizeof(socket_timeout));
  HIXL_CHK_BOOL_RET_SPECIAL_STATUS(socket_ret != 0, FAILED,
                                   "Try to setsockopt(SO_SNDTIMEO), socket_ret:%d, error msg:%s, errno:%d", socket_ret,
                                   strerror(errno), errno);
  socket_ret = connect(conn_fd, addr->ai_addr, addr->ai_addrlen);
  HIXL_CHK_BOOL_RET_SPECIAL_STATUS(socket_ret != 0, FAILED,
                                   "Try to socket connect, socket_ret:%d, error msg:%s, errno:%d", socket_ret,
                                   strerror(errno), errno);
  HIXL_DISMISS_GUARD(close_fd);
  HIXL_DISMISS_GUARD(record_err);
  return SUCCESS;
}

Status CtrlMsgPlugin::SockAddrInit(const std::string &ip, uint32_t listen_port, int32_t ai_family,
                                   struct sockaddr_storage &server_addr, socklen_t &addr_len) {
  if (ai_family == AF_INET) {
    struct sockaddr_in *addr = reinterpret_cast<struct sockaddr_in *>(&server_addr);
    (void)memset_s(addr, sizeof(*addr), 0, sizeof(*addr));
    addr->sin_family = AF_INET;
    addr->sin_port = htons(listen_port);
    (void)inet_pton(AF_INET, ip.c_str(), &addr->sin_addr);
    addr_len = sizeof(*addr);
  } else {
    struct sockaddr_in6 *addr = reinterpret_cast<struct sockaddr_in6 *>(&server_addr);
    (void)memset_s(addr, sizeof(*addr), 0, sizeof(*addr));
    addr->sin6_family = AF_INET6;
    addr->sin6_port = htons(listen_port);
    (void)inet_pton(AF_INET6, ip.c_str(), &addr->sin6_addr);
    addr_len = sizeof(*addr);
  }
  return SUCCESS;
}

Status CtrlMsgPlugin::ListenSocket(const std::string &ip, uint32_t listen_port, int32_t backlog) {
  auto &listen_fd = fd_;
  int32_t ai_family = 0;
  HIXL_CHK_STATUS_RET(GetAiFamily(ip, ai_family), "Failed to get ai_family, ip:%s", ip.c_str());
  const int32_t fd = socket(ai_family, SOCK_STREAM | SOCK_NONBLOCK, 0);
  HIXL_CHK_BOOL_RET_STATUS(fd >= 0, FAILED, "Failed to create socket.");
  HIXL_DISMISSABLE_GUARD(fail_guard, ([fd]() { (void)close(fd); }));

  struct sockaddr_storage server_addr;
  socklen_t addr_len;
  HIXL_CHK_STATUS_RET(SockAddrInit(ip, listen_port, ai_family, server_addr, addr_len), "Failed to init sockaddr, ip:%s",
                      ip.c_str());

  if (ai_family == AF_INET6) {
    int v6only = 1;
    (void)setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only));
  }
  struct timeval timeout;
  timeout.tv_sec = 1;
  timeout.tv_usec = 0;
  auto socket_ret = setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  HIXL_CHK_BOOL_RET_STATUS(socket_ret == 0, FAILED,
                           "Failed to set socket opt timeout, socket_ret:%d, error msg:%s, errno:%d", socket_ret,
                           strerror(errno), errno);
  int32_t on = 1;
  socket_ret = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
  HIXL_CHK_BOOL_RET_STATUS(socket_ret == 0, FAILED,
                           "Failed to set socket opt SO_REUSEADDR, socket_ret:%d, error msg:%s, errno:%d", socket_ret,
                           strerror(errno), errno);
  HIXL_CHK_BOOL_RET_STATUS(bind(fd, reinterpret_cast<sockaddr *>(&server_addr), addr_len) >= 0, FAILED,
                           "Failed to bind port:%u, error msg:%s, errno:%d.", listen_port, strerror(errno), errno);
  socket_ret = listen(fd, backlog);
  HIXL_CHK_BOOL_RET_STATUS(socket_ret == 0, FAILED, "Failed to listen, socket_ret:%d, error msg:%s, errno:%d",
                           socket_ret, strerror(errno), errno);
  HIXL_DISMISS_GUARD(fail_guard);
  listen_fd = fd;
  UpdateSocketAddresses();
  HIXL_EVENT("listen on %s:%d success, listen_fd:%d", ip.c_str(), listen_port, fd);
  return SUCCESS;
}

Status CtrlMsgPlugin::AddToEpoll(int32_t &epoll_fd, uint32_t events) {
  std::shared_lock<std::shared_mutex> lock(lifecycle_mutex_);
  const auto fd = fd_;
  HIXL_CHK_BOOL_RET_STATUS(fd >= 0, FAILED, "Invalid fd:%d", fd);
  if (epoll_fd < 0) {
    epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    HIXL_CHK_BOOL_RET_STATUS(epoll_fd >= 0, FAILED, "Create epoll failed, errno:%d, msg:%s", errno, strerror(errno));
    HIXL_EVENT("Create epoll success, epoll_fd:%d", epoll_fd);
  }

  struct epoll_event ev;
  (void)memset_s(&ev, sizeof(ev), 0, sizeof(ev));
  ev.events = events;
  ev.data.fd = fd;

  int32_t ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev);
  HIXL_CHK_BOOL_RET_STATUS(ret == 0, FAILED, "Add fd to epoll failed, fd:%d, ret:%d, errno:%d, msg:%s", fd, ret, errno,
                           strerror(errno));

  HIXL_LOGI("added fd:%d to epoll successfully, events:0x%x", fd, events);
  return SUCCESS;
}

Status CtrlMsgPlugin::Accept(CtrlMsgPluginPtr &connection) {
  std::shared_lock<std::shared_mutex> lock(lifecycle_mutex_);
  connection.reset();
  HIXL_CHK_BOOL_RET_STATUS(fd_ >= 0, FAILED, "[CtrlMsgPlugin] Invalid listener socket, fd:%d", fd_);
  struct sockaddr_storage addr;
  socklen_t addr_len = sizeof(addr);
  const int32_t fd = accept(fd_, reinterpret_cast<sockaddr *>(&addr), &addr_len);
  if (fd < 0) {
    const int saved_errno = errno;
    HIXL_CHK_BOOL_RET_STATUS(saved_errno == EWOULDBLOCK || saved_errno == EINTR || saved_errno == ECONNABORTED, FAILED,
                             "Call api:accept failed, fd:%d, errno:%d, error_msg:%s", fd_, saved_errno,
                             strerror(saved_errno));
    return SUCCESS;
  }
  HIXL_DISMISSABLE_GUARD(close_fd, ([fd]() { (void)close(fd); }));
  CtrlMsgPluginPtr accepted_connection(new (std::nothrow) CtrlMsgPlugin());
  HIXL_CHECK_NOTNULL(accepted_connection);
  HIXL_CHK_STATUS_RET(accepted_connection->Initialize(), "Failed to initialize accepted connection");
  accepted_connection->fd_ = fd;
  accepted_connection->UpdateSocketAddresses();
  HIXL_DISMISS_GUARD(close_fd);
  connection = std::move(accepted_connection);
  return SUCCESS;
}

Status CtrlMsgPlugin::Send(const void *buf, size_t len, uint32_t timeout_ms) {
  int32_t err_no = 0;
  return Send(buf, len, timeout_ms, err_no);
}

Status CtrlMsgPlugin::Send(const void *buf, size_t len, uint32_t timeout_ms, int32_t &err_no) {
  std::shared_lock<std::shared_mutex> lock(lifecycle_mutex_);
  const auto fd = fd_;
  err_no = 0;
  HIXL_CHK_BOOL_RET_STATUS(len <= kMaxTransferSize, PARAM_INVALID,
                           "Socket write length exceeds ssize_t max, len:%zu bytes, max:%zu bytes", len,
                           kMaxTransferSize);
  std::shared_ptr<TlsSession> session;
  HIXL_CHK_STATUS_RET(CheckReady(session), "[CtrlMsgPlugin] Connection is not ready for I/O");
  HIXL_CHK_BOOL_RET_STATUS(buf != nullptr || len == 0U, PARAM_INVALID, "Invalid I/O buffer, size:%zu bytes", len);
  if (session != nullptr) {
    const Status ret = session->Send(buf, len, timeout_ms, err_no);
    if (!session->IsUsable()) {
      HIXL_CHK_STATUS(ShutdownSocketLocked(), "Failed to interrupt unusable TLS connection, fd:%d", fd_);
    }
    return ret;
  }
  return SendPlain(fd, buf, len, timeout_ms, err_no);
}

Status CtrlMsgPlugin::Recv(void *buf, size_t len, uint32_t timeout_ms) {
  std::lock_guard<std::mutex> receive_lock(receive_mutex_);
  std::shared_lock<std::shared_mutex> lock(lifecycle_mutex_);
  const auto fd = fd_;
  HIXL_CHK_BOOL_RET_STATUS(len <= kMaxTransferSize, PARAM_INVALID,
                           "Socket read length exceeds ssize_t max, len:%zu bytes, max:%zu bytes", len,
                           kMaxTransferSize);
  std::shared_ptr<TlsSession> session;
  HIXL_CHK_STATUS_RET(CheckReady(session), "[CtrlMsgPlugin] Connection is not ready for I/O");
  HIXL_CHK_BOOL_RET_STATUS(buf != nullptr || len == 0U, PARAM_INVALID, "Invalid I/O buffer, size:%zu bytes", len);
  if (session != nullptr) {
    return session->Recv(buf, len, timeout_ms);
  }
  return RecvPlain(fd, buf, len, timeout_ms);
}

Status CtrlMsgPlugin::SetTcpNoDelay() {
  std::shared_lock<std::shared_mutex> lock(lifecycle_mutex_);
  const auto fd = fd_;
  HIXL_CHK_BOOL_RET_STATUS(fd >= 0, FAILED, "Invalid fd:%d", fd);
  int32_t flag = 1;
  HIXL_CHK_BOOL_RET_STATUS(setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) == 0, FAILED,
                           "Failed to set TCP_NODELAY, fd:%d, errno:%d, msg:%s", fd, errno, strerror(errno));
  HIXL_EVENT("[HixlServer] set tcp nodelay success, fd:%d", fd);
  return SUCCESS;
}

Status CtrlMsgPlugin::SetTcpKeepAlive() {
  std::shared_lock<std::shared_mutex> lock(lifecycle_mutex_);
  const auto fd = fd_;
  HIXL_CHK_BOOL_RET_STATUS(setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &kKeepAlive, sizeof(kKeepAlive)) == 0, FAILED,
                           "Failed to set SO_KEEPALIVE, fd:%d, errno:%d", fd, errno);
  HIXL_CHK_BOOL_RET_STATUS(setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &kTcpKeepIdle, sizeof(kTcpKeepIdle)) == 0, FAILED,
                           "Failed to set TCP_KEEPIDLE, fd:%d, errno:%d", fd, errno);
  HIXL_CHK_BOOL_RET_STATUS(setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &kTcpKeepIntvl, sizeof(kTcpKeepIntvl)) == 0,
                           FAILED, "Failed to set TCP_KEEPINTVL, fd:%d, errno:%d", fd, errno);
  HIXL_CHK_BOOL_RET_STATUS(setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &kTcpKeepCnt, sizeof(kTcpKeepCnt)) == 0, FAILED,
                           "Failed to set TCP_KEEPCNT, fd:%d, errno:%d", fd, errno);

  HIXL_EVENT("[HixlServer] set tcp keep alive success, fd:%d, keep_idle:%d, keep_intvl:%d, keep_cnt:%d", fd,
             kTcpKeepIdle, kTcpKeepIntvl, kTcpKeepCnt);

  return SUCCESS;
}

Status CtrlMsgPlugin::SetSendTimeout(uint32_t timeout_ms) {
  std::shared_lock<std::shared_mutex> lock(lifecycle_mutex_);
  HIXL_CHK_BOOL_RET_STATUS(fd_ >= 0, FAILED, "Invalid fd:%d", fd_);
  constexpr int64_t kMillisPerSecond = 1000;
  timeval socket_timeout{};
  socket_timeout.tv_sec = static_cast<int64_t>(timeout_ms) / kMillisPerSecond;
  socket_timeout.tv_usec = (static_cast<int64_t>(timeout_ms) % kMillisPerSecond) * kMillisPerSecond;
  const auto ret = setsockopt(fd_, SOL_SOCKET, SO_SNDTIMEO, &socket_timeout, sizeof(socket_timeout));
  const int saved_errno = errno;
  HIXL_CHK_BOOL_RET_STATUS(ret == 0, FAILED,
                           "Call api:setsockopt failed, option:SO_SNDTIMEO, ret:%d, fd:%d, timeout:%u ms, "
                           "errno:%d, error_msg:%s",
                           ret, fd_, timeout_ms, saved_errno, strerror(saved_errno));
  return SUCCESS;
}

Status CtrlMsgPlugin::Handshake(bool server, int32_t timeout_ms) {
  if (!tls_enabled_) {
    return SUCCESS;
  }
  auto ctx = TlsManager::GetInstance().GetContext();
  HIXL_CHK_BOOL_RET_STATUS(ctx != nullptr, FAILED, "[CtrlMsgPlugin] TLS context is missing, fd:%d", fd_);
  std::shared_ptr<TlsSession> session;
  HIXL_CHK_STATUS_RET(TlsContext::CreateSession(ctx, fd_, server, session), "Failed to create TLS session, fd:%d", fd_);
  const Status ret = server ? session->HandshakeAsServer(timeout_ms) : session->HandshakeAsClient(timeout_ms);
  HIXL_CHK_STATUS_RET(ret, "TLS handshake failed, fd:%d", fd_);
  tls_session_ = std::move(session);
  return SUCCESS;
}

Status CtrlMsgPlugin::HandshakeServer() {
  std::unique_lock<std::shared_mutex> lock(lifecycle_mutex_);
  HIXL_CHK_BOOL_RET_STATUS(fd_ >= 0, FAILED, "[CtrlMsgPlugin] Invalid socket for server handshake, fd:%d", fd_);
  HIXL_CHK_BOOL_RET_STATUS(tls_session_ == nullptr, FAILED,
                           "[CtrlMsgPlugin] TLS session already exists for server handshake, fd:%d", fd_);
  const Status ret = Handshake(true, kServerHandshakeTimeoutMs);
  if (ret != SUCCESS) {
    (void)CloseLocked();
  }
  return ret;
}

Status CtrlMsgPlugin::HandshakeServerStep(TlsHandshakeState &state) {
  std::unique_lock<std::shared_mutex> lock(lifecycle_mutex_);
  HIXL_CHK_BOOL_RET_STATUS(initialized_ && fd_ >= 0, FAILED,
                           "[CtrlMsgPlugin] Invalid socket for server handshake, fd:%d", fd_);
  state = TlsHandshakeState::kComplete;
  if (!tls_enabled_) {
    return SUCCESS;
  }
  if (tls_session_ == nullptr) {
    HIXL_CHK_STATUS_RET(TlsContext::CreateSession(TlsManager::GetInstance().GetContext(), fd_, true, tls_session_),
                        "Failed to create server TLS session, fd:%d", fd_);
  }
  const Status ret = tls_session_->HandshakeAsServerStep(state);
  if (ret != SUCCESS) {
    (void)CloseLocked();
  }
  return ret;
}

Status CtrlMsgPlugin::Recv(void *buf, size_t len, ssize_t &n) {
  std::lock_guard<std::mutex> receive_lock(receive_mutex_);
  std::shared_lock<std::shared_mutex> lock(lifecycle_mutex_);
  n = -1;
  std::shared_ptr<TlsSession> session;
  HIXL_CHK_STATUS_RET(CheckReady(session), "[CtrlMsgPlugin] Connection is not ready for receive");
  if (session != nullptr) {
    return session->RecvOnce(buf, len, n);
  }
  n = recv(fd_, buf, len, 0);
  return SUCCESS;
}

bool CtrlMsgPlugin::HasPending() {
  std::shared_lock<std::shared_mutex> lock(lifecycle_mutex_);
  if (fd_ < 0) {
    return false;
  }
  auto session = tls_session_;
  return session != nullptr && session->HasPending();
}

bool CtrlMsgPlugin::IsUsable() const {
  std::shared_lock<std::shared_mutex> lock(lifecycle_mutex_);
  return initialized_ && fd_ >= 0 && (!tls_enabled_ || (tls_session_ != nullptr && tls_session_->IsUsable()));
}

Status CtrlMsgPlugin::GetPeerCertificateFingerprint(std::string &fingerprint) const {
  std::shared_lock<std::shared_mutex> lock(lifecycle_mutex_);
  fingerprint.clear();
  if (!tls_enabled_ || tls_session_ == nullptr) {
    return UNSUPPORTED;
  }
  return tls_session_->GetPeerCertificateFingerprint(fingerprint);
}

Status CtrlMsgPlugin::InvalidateTlsSession() {
  std::shared_lock<std::shared_mutex> lock(lifecycle_mutex_);
  if (tls_session_ == nullptr) {
    return SUCCESS;
  }
  tls_session_->Invalidate();
  return ShutdownSocketLocked();
}

Status CtrlMsgPlugin::ShutdownSocket() {
  std::shared_lock<std::shared_mutex> lock(lifecycle_mutex_);
  return ShutdownSocketLocked();
}

Status CtrlMsgPlugin::ShutdownSocketLocked() const {
  if (fd_ < 0) {
    return SUCCESS;
  }
  const int ret = shutdown(fd_, SHUT_RDWR);
  const int saved_errno = errno;
  HIXL_CHK_BOOL_RET_STATUS(ret == 0 || saved_errno == ENOTCONN, FAILED,
                           "Call api:shutdown failed, ret:%d, fd:%d, errno:%d, error_msg:%s", ret, fd_, saved_errno,
                           strerror(saved_errno));
  return SUCCESS;
}

Status CtrlMsgPlugin::CloseLocked() {
  if (tls_session_ != nullptr) {
    tls_session_->Shutdown();
    tls_session_.reset();
  }
  initialized_ = false;
  tls_enabled_ = false;
  const int32_t fd = fd_;
  fd_ = -1;
  if (fd < 0) {
    return SUCCESS;
  }
  const int ret = close(fd);
  HIXL_CHK_BOOL_RET_STATUS(ret == 0, FAILED, "Call api:close failed, ret:%d, fd:%d, errno:%d, error_msg:%s", ret, fd,
                           errno, strerror(errno));
  return SUCCESS;
}

Status CtrlMsgPlugin::Close() {
  std::unique_lock<std::shared_mutex> lock(lifecycle_mutex_);
  return CloseLocked();
}
}  // namespace hixl
