/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HIXL_TCP_CLIENT_SERVER_H
#define HIXL_TCP_CLIENT_SERVER_H

#include <cstdint>
#include <string>
#include <vector>
#include <netinet/in.h>

bool TcpSendUint64(int fd, uint64_t data);

bool TcpSendTaskStatus(int fd);

bool TcpRecvTaskStatusOk(int fd);

class TCPServer {
 public:
  TCPServer();

  bool StartServer(uint16_t port, int listen_backlog = 128);

  bool AcceptConnection();

  /// On success returns true and sets *out_fd (caller must close). On poll timeout returns false and sets *timed_out
  /// true. On failure returns false and *timed_out false.
  bool AcceptIntoClientFd(int *out_fd, int poll_timeout_ms, bool *timed_out);

  uint64_t ReceiveUint64() const;

  bool SendUint64(uint64_t data) const;

  bool SendTaskStatus() const;

  bool ReceiveTaskStatus() const;

  void DisConnectClient();

  void StopServer();

  ~TCPServer();

 private:
  int32_t server_fd_ = -1;
  int32_t client_socket_ = -1;
  sockaddr_in address_{};
  int32_t opt_ = 1;
};

class TCPClient {
 public:
  TCPClient();

  bool ConnectToServer(const std::string &host, uint16_t port);

  bool SendUint64(uint64_t data) const;

  bool ReceiveUint64(uint64_t *out) const;

  bool SendTaskStatus() const;

  bool ReceiveTaskStatus() const;

  void Disconnect();

  ~TCPClient();

 private:
  int32_t sock_ = -1;
  sockaddr_in server_{};
};

/// Owns `TCPServer`, per-client fds, and the connect-phase worker thread. Used by benchmark server: pass memory
/// address for coordination, then call `WaitAllNotify()` after clients finish transfers.
class TcpServerSession {
 public:
  /// `expected_peer_count`: accept until this many TCP peers complete handshake (each receives mem_addr + status).
  TcpServerSession(uint16_t port, uint32_t max_connect_phase_wall_sec, uint32_t expected_peer_count = 1U);
  ~TcpServerSession();

  TcpServerSession(const TcpServerSession &) = delete;
  TcpServerSession &operator=(const TcpServerSession &) = delete;

  /// Starts internal thread: listen, accept until `expected_peer_count` is reached or `max_wall_sec_` elapses.
  /// Joins thread before return. On failure cleans up listen/clients.
  bool WaitAndSendAddr(uint64_t mem_addr);

  /// Waits for one task-status message per connected peer (same protocol as `TCPClient::SendTaskStatus`).
  /// Closes all client fds and stops listen on success or failure.
  bool WaitAllNotify();

  size_t ConnectedPeerCount() const { return client_fds_.size(); }

 private:
  void ShutdownClientsAndListen();

  bool RunConnectPhaseInThread(uint64_t mem_addr, uint32_t wall_sec);

  uint16_t port_;
  uint32_t max_wall_sec_;
  uint32_t expected_peer_count_;
  TCPServer server_;
  std::vector<int> client_fds_;
};

#endif  // HIXL_TCP_CLIENT_SERVER_H
