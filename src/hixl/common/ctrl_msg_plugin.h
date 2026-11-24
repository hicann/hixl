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

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include "hixl/hixl_types.h"
#include "common/tls/tls_session.h"
#include <mutex>
#include <shared_mutex>

namespace hixl {
class CtrlMsgPlugin {
 public:
  CtrlMsgPlugin() = default;
  ~CtrlMsgPlugin();
  CtrlMsgPlugin(const CtrlMsgPlugin &) = delete;
  CtrlMsgPlugin &operator=(const CtrlMsgPlugin &) = delete;
  CtrlMsgPlugin(CtrlMsgPlugin &&) = delete;
  CtrlMsgPlugin &operator=(CtrlMsgPlugin &&) = delete;

  Status Initialize();
  Status Connect(const std::string &ip, uint32_t port, int32_t timeout);
  Status Listen(const std::string &ip, uint32_t port, int32_t backlog);
  Status Accept(std::shared_ptr<CtrlMsgPlugin> &connection);
  Status HandshakeServer();
  Status HandshakeServerStep(TlsHandshakeState &state);
  static constexpr uint32_t kDefaultCtrlTimeoutMs = 1000U;
  static constexpr int32_t kServerHandshakeTimeoutMs = 10000;
  // The third argument is the timeout; errno follows timeout_ms when needed.
  Status Send(const void *buf, size_t len, uint32_t timeout_ms = kDefaultCtrlTimeoutMs);
  Status Send(const void *buf, size_t len, uint32_t timeout_ms, int32_t &err_no);
  Status Send(const void *buf, size_t len, int32_t &) = delete;
  // Receive exactly len bytes, or return an error/timeout. The third argument is an input timeout in milliseconds.
  Status Recv(void *buf, size_t len, uint32_t timeout_ms);
  // Attempt one receive. The third argument is an output byte count; inspect errno when n < 0.
  Status Recv(void *buf, size_t len, ssize_t &n);
  Status AddToEpoll(int32_t &epoll_fd, uint32_t events = EPOLLIN | EPOLLRDHUP);
  Status SetTcpNoDelay();
  Status SetTcpKeepAlive();
  Status SetSendTimeout(uint32_t timeout_ms);
  bool HasPending();
  bool IsUsable() const;
  // SHA-256 fingerprint of the peer TLS certificate. UNSUPPORTED when TLS is not in use.
  Status GetPeerCertificateFingerprint(std::string &fingerprint) const;
  // Abandon an incomplete TLS message and wake I/O; the connection owner still closes the descriptor.
  Status InvalidateTlsSession();
  // Wake in-flight I/O without closing or reusing the descriptor.
  Status ShutdownSocket();
  Status Close();
  int32_t Fd() const;
  // Return a snapshot; cached socket addresses remain available after Close().
  std::string Desc() const;

 private:
  Status Handshake(bool server, int32_t timeout_ms);
  // Caller holds lifecycle_mutex_ throughout the check and subsequent I/O.
  Status CheckReady(std::shared_ptr<TlsSession> &session) const;
  Status ConnectSocket(const std::string &ip, uint32_t port, int32_t timeout);
  Status ListenSocket(const std::string &ip, uint32_t port, int32_t backlog);
  Status DoConnect(const struct ::addrinfo *addr, int32_t &conn_fd, int32_t &err_no, int32_t timeout);
  Status GetAiFamily(const std::string &ip, int32_t &ai_family);
  Status SockAddrInit(const std::string &ip, uint32_t port, int32_t family, struct sockaddr_storage &addr,
                      socklen_t &len);
  Status CloseLocked();
  Status ShutdownSocketLocked() const;
  // Caller holds lifecycle_mutex_, or owns an accepted connection that has not been published yet.
  void UpdateSocketAddresses();

  mutable std::shared_mutex lifecycle_mutex_;
  std::mutex receive_mutex_;
  int32_t fd_ = -1;
  bool initialized_ = false;
  bool tls_enabled_ = false;
  std::shared_ptr<TlsSession> tls_session_;
  std::string local_address_ = "unknown";
  std::string peer_address_ = "unknown";
};
using CtrlMsgPluginPtr = std::shared_ptr<CtrlMsgPlugin>;
}  // namespace hixl
#endif  // CANN_HIXL_SRC_HIXL_COMMON_CTRL_MSG_PLUGIN_H_
