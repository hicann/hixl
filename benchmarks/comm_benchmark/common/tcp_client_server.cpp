/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <cinttypes>
#include <chrono>
#include <thread>
#include <vector>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <algorithm>
#include <cstring>
#include <endian.h>
#include "tcp_client_server.h"
#include "benchmark_log.h"

namespace {

constexpr int kListenPollSliceMs = 250;
constexpr int kRecvNotifyPollTimeoutMs = 30 * 60 * 1000;
constexpr int64_t kMinPollRemainMs = 1LL;
constexpr uint32_t kDefaultTcpConnectTimeoutMs = 60000U;
constexpr uint32_t kConnectRetryIntervalMs = 1000U;
constexpr int kAcceptConnTimeoutMs = 5000;

void CloseClientFds(std::vector<int> &fds) {
  for (int fd : fds) {
    (void)::close(fd);
  }
  fds.clear();
}

bool HandleNewClient(int cfd, std::vector<int> &out_client_fds) {
  out_client_fds.push_back(cfd);
  return true;
}

// Returns 1 when a peer is accepted, 0 on poll timeout, -1 on hard failure.
int AcceptOnePeerInConnectPhase(TCPServer *srv, const std::chrono::steady_clock::time_point &deadline,
                                std::vector<int> &out_client_fds) {
  const auto now = std::chrono::steady_clock::now();
  if (now >= deadline) {
    return 0;
  }
  const int64_t remain_ms = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
  const int poll_ms = static_cast<int>(
      std::min<int64_t>(static_cast<int64_t>(kListenPollSliceMs), std::max<int64_t>(remain_ms, kMinPollRemainMs)));
  int cfd = -1;
  bool timed_out = false;
  if (!srv->AcceptIntoClientFd(&cfd, poll_ms, &timed_out)) {
    if (!timed_out) {
      BENCH_LOGE("AcceptIntoClientFd failed.\n");
      CloseClientFds(out_client_fds);
      return -1;
    }
    return 0;
  }
  if (!HandleNewClient(cfd, out_client_fds)) {
    return -1;
  }
  return 1;
}

bool RunConnectPhaseFill(TCPServer *srv, uint16_t port, uint32_t max_connect_phase_sec, uint32_t expected_peer_count,
                         std::vector<int> &out_client_fds) {
  if (srv == nullptr) {
    return false;
  }
  out_client_fds.clear();
  if (!srv->StartServer(port)) {
    BENCH_LOGE("Failed to start TCP server.\n");
    return false;
  }
  BENCH_LOGI("waiting for %" PRIu32 " peer(s) on TCP port %u, timeout=%" PRIu32 "s\n", expected_peer_count, port,
             max_connect_phase_sec);
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(static_cast<int>(max_connect_phase_sec));

  while (out_client_fds.size() < expected_peer_count) {
    const int accept_status = AcceptOnePeerInConnectPhase(srv, deadline, out_client_fds);
    if (accept_status < 0) {
      return false;
    }
    if (accept_status == 0 && std::chrono::steady_clock::now() >= deadline) {
      break;
    }
  }
  if (out_client_fds.size() < expected_peer_count) {
    const size_t got = out_client_fds.size();
    if (got == 0U) {
      BENCH_LOGE("TCP connect phase: no client within %" PRIu32 " s.\n", max_connect_phase_sec);
    } else {
      BENCH_LOGE("TCP connect phase: timeout after %" PRIu32 " s (expected %" PRIu32 " peers, got %zu).\n",
                 max_connect_phase_sec, expected_peer_count, got);
    }
    CloseClientFds(out_client_fds);
    return false;
  }
  BENCH_LOGI("peer connection ready, count=%zu\n", out_client_fds.size());
  srv->StopServer();
  return true;
}

bool SendAddrToAllPeers(uint64_t mem_addr, std::vector<int> &client_fds) {
  if (client_fds.empty()) {
    return false;
  }
  for (int fd : client_fds) {
    if (!TcpSendUint64(fd, mem_addr)) {
      BENCH_LOGE("TcpSendUint64 failed.\n");
      CloseClientFds(client_fds);
      return false;
    }
    if (!TcpSendTaskStatus(fd)) {
      BENCH_LOGE("TcpSendTaskStatus failed.\n");
      CloseClientFds(client_fds);
      return false;
    }
  }
  return true;
}

bool PollFds(std::vector<struct pollfd> *pf) {
  const int pr = poll(pf->data(), static_cast<nfds_t>(pf->size()), kRecvNotifyPollTimeoutMs);
  if (pr < 0) {
    BENCH_LOGE("RecvNotifyAll poll failed\n");
    return false;
  }
  if (pr == 0) {
    BENCH_LOGE("RecvNotifyAll poll timeout\n");
    return false;
  }
  return true;
}

bool ProcessPollEvent(const struct pollfd &pfd, const std::vector<int> &client_fds, std::vector<char> *done,
                      size_t *ndone) {
  if ((pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
    BENCH_LOGE("RecvNotifyAll socket error\n");
    return false;
  }
  if ((pfd.revents & POLLIN) == 0) {
    return true;
  }
  size_t idx = client_fds.size();
  for (size_t i = 0; i < client_fds.size(); ++i) {
    if ((*done)[i] == 0 && client_fds[i] == pfd.fd) {
      idx = i;
      break;
    }
  }
  if (idx == client_fds.size()) {
    return true;
  }
  if (!TcpRecvTaskStatusOk(client_fds[idx])) {
    return false;
  }
  (*done)[idx] = 1;
  ++(*ndone);
  return true;
}

bool RecvNotifyAllOnFds(const std::vector<int> &client_fds) {
  const size_t n = client_fds.size();
  if (n == 0U) {
    return false;
  }
  std::vector<char> done(n, 0);
  size_t ndone = 0;
  while (ndone < n) {
    std::vector<struct pollfd> pf;
    pf.reserve(n);
    for (size_t i = 0; i < n; ++i) {
      if (done[i] != 0) {
        continue;
      }
      struct pollfd one {};
      one.fd = client_fds[i];
      one.events = POLLIN;
      pf.push_back(one);
    }
    if (pf.empty()) {
      break;
    }
    if (!PollFds(&pf)) {
      return false;
    }
    for (auto &pfd : pf) {
      if (!ProcessPollEvent(pfd, client_fds, &done, &ndone)) {
        return false;
      }
    }
  }
  return ndone == n;
}

}  // namespace

bool TcpSendUint64(int fd, uint64_t data) {
  const uint64_t network_data = htobe64(data);
  if (send(fd, &network_data, sizeof(network_data), 0) < 0) {
    BENCH_LOGE("Send uint64 to tcp peer failed\n");
    return false;
  }
  return true;
}

bool TcpSendTaskStatus(int fd) {
  const bool status = true;
  if (send(fd, &status, sizeof(status), 0) < 0) {
    BENCH_LOGE("Send status to tcp client failed\n");
    return false;
  }
  return true;
}

bool TcpRecvTaskStatusOk(int fd) {
  bool received = false;
  const ssize_t bytes_received = recv(fd, &received, sizeof(received), 0);
  if (bytes_received < 0) {
    BENCH_LOGE("Received status failed\n");
    return false;
  }
  if (bytes_received == 0) {
    BENCH_LOGE("Client connection break\n");
    return false;
  }
  if (received) {
    return true;
  }
  BENCH_LOGE("Tcp server received status failed\n");
  return false;
}

TcpServerSession::TcpServerSession(uint16_t port, uint32_t max_connect_phase_wall_sec, uint32_t expected_peer_count)
    : port_(port), max_wall_sec_(max_connect_phase_wall_sec), expected_peer_count_(expected_peer_count) {}

TcpServerSession::~TcpServerSession() {
  ShutdownClientsAndListen();
}

void TcpServerSession::ShutdownClientsAndListen() {
  for (int fd : client_fds_) {
    (void)::close(fd);
  }
  client_fds_.clear();
  server_.StopServer();
}

bool TcpServerSession::RunConnectPhaseInThread(uint32_t wall_sec) {
  bool ok = false;
  std::thread worker([this, wall_sec, &ok]() {
    ok = RunConnectPhaseFill(&server_, port_, wall_sec, expected_peer_count_, client_fds_);
  });
  worker.join();
  return ok;
}

bool TcpServerSession::WaitForPeers() {
  ShutdownClientsAndListen();
  const bool ok = RunConnectPhaseInThread(max_wall_sec_);
  if (!ok) {
    ShutdownClientsAndListen();
    return false;
  }
  if (client_fds_.empty()) {
    BENCH_LOGE("TCP connect phase: no client connections.\n");
    ShutdownClientsAndListen();
    return false;
  }
  return true;
}

bool TcpServerSession::SendAddrToPeers(uint64_t mem_addr) {
  return SendAddrToAllPeers(mem_addr, client_fds_);
}

bool TcpServerSession::WaitAndSendAddr(uint64_t mem_addr) {
  return WaitForPeers() && SendAddrToPeers(mem_addr);
}

bool TcpServerSession::WaitAllNotify() {
  if (client_fds_.empty()) {
    return false;
  }
  if (!RecvNotifyAllOnFds(client_fds_)) {
    ShutdownClientsAndListen();
    return false;
  }
  ShutdownClientsAndListen();
  return true;
}

TCPClient::TCPClient() = default;

bool TCPClient::ConnectToServer(const std::string &host, uint16_t port, uint32_t timeout_ms) {
  sock_ = socket(AF_INET, SOCK_STREAM, 0);
  if (sock_ == -1) {
    BENCH_LOGE("Create socket failed\n");
    return false;
  }

  server_.sin_family = AF_INET;
  server_.sin_port = htons(port);

  if (inet_addr(host.c_str()) == INADDR_NONE) {
    BENCH_LOGE("Invalid server ip: %s\n", host.c_str());
  } else {
    server_.sin_addr.s_addr = inet_addr(host.c_str());
  }

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<int64_t>(timeout_ms));
  uint32_t attempt = 0U;
  while (std::chrono::steady_clock::now() < deadline) {
    const auto ret = connect(sock_, reinterpret_cast<sockaddr *>(&server_), sizeof(server_));
    if (ret >= 0) {
      return true;
    }
    ++attempt;
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) {
      break;
    }
    const auto remain_ms = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
    const auto sleep_ms = static_cast<int64_t>(kConnectRetryIntervalMs);
    const auto wait_ms = std::min<int64_t>(sleep_ms, std::max<int64_t>(remain_ms, 1LL));
    std::this_thread::sleep_for(std::chrono::milliseconds(wait_ms));
  }
  BENCH_LOGE("Connect to tcp server failed after %u attempt(s), timeout_ms=%u\n", attempt, timeout_ms);
  return false;
}

bool TCPClient::SendUint64(uint64_t data) const {
  // Convert host byte order to network byte order.
  uint64_t network_data = htobe64(data);
  if (send(sock_, &network_data, sizeof(uint64_t), 0) < 0) {
    BENCH_LOGE("Send uint64 to tcp peer failed\n");
    return false;
  }
  return true;
}

bool TCPClient::ReceiveUint64(uint64_t *out) const {
  uint64_t received_data = 0;
  ssize_t bytes_received = recv(sock_, &received_data, sizeof(uint64_t), 0);
  if (bytes_received < 0) {
    BENCH_LOGE("Received uint64 data failed\n");
    return false;
  }
  if (bytes_received == 0) {
    BENCH_LOGE("Tcp peer connection break\n");
    return false;
  }
  if (bytes_received != static_cast<ssize_t>(sizeof(uint64_t))) {
    BENCH_LOGE("Invalid uint64 size, expect: %zu actual: %zd\n", sizeof(uint64_t), bytes_received);
    return false;
  }
  if (out != nullptr) {
    *out = be64toh(received_data);
  }
  return true;
}

bool TCPClient::SendTaskStatus() const {
  bool status = true;
  if (send(sock_, &status, sizeof(status), 0) < 0) {
    BENCH_LOGE("Send status to tcp server failed\n");
    return false;
  }
  return true;
}

bool TCPClient::ReceiveTaskStatus() const {
  bool received = false;
  // Receive the task status flag.
  ssize_t bytes_received = recv(sock_, &received, sizeof(received), 0);
  if (bytes_received < 0) {
    BENCH_LOGE("Received status failed\n");
    return false;
  } else if (bytes_received == 0) {
    BENCH_LOGE("Server connection break\n");
    return false;
  }

  if (received) {
    return true;
  } else {
    BENCH_LOGE("Tcp client received status failed\n");
    return false;
  }
}

void TCPClient::Disconnect() {
  if (sock_ != -1) {
    (void)close(sock_);
    sock_ = -1;
  }
}

TCPClient::~TCPClient() {
  Disconnect();
}

TCPServer::TCPServer() = default;

bool TCPServer::StartServer(uint16_t port, int listen_backlog) {
  server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd_ < 0) {
    BENCH_LOGE("Create socket failed\n");
    return false;
  }

  // Configure socket reuse options before bind.
  if (setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt_, sizeof(opt_))) {
    BENCH_LOGE("Set socket option failed\n");
    return false;
  }

  address_.sin_family = AF_INET;
  address_.sin_addr.s_addr = INADDR_ANY;
  address_.sin_port = htons(port);

  // Bind the socket to the requested port.
  if (bind(server_fd_, reinterpret_cast<sockaddr *>(&address_), sizeof(address_)) < 0) {
    BENCH_LOGE("Bind port failed\n");
    return false;
  }

#if defined(SOMAXCONN)
  const int kSysMax = SOMAXCONN;
#else
  const int kSysMax = 4096;
#endif
  const int backlog = std::max(1, std::min(listen_backlog, kSysMax));
  if (listen(server_fd_, backlog) < 0) {
    BENCH_LOGE("Listen port failed\n");
    return false;
  }

  return true;
}

bool TCPServer::AcceptIntoClientFd(int *out_fd, int poll_timeout_ms, bool *timed_out) {
  if (timed_out != nullptr) {
    *timed_out = false;
  }
  if (out_fd == nullptr || server_fd_ < 0) {
    return false;
  }
  struct pollfd pfd {};
  pfd.fd = server_fd_;
  pfd.events = POLLIN;

  const auto ret = poll(&pfd, static_cast<nfds_t>(1), poll_timeout_ms);
  if (ret < 0) {
    BENCH_LOGE("Poll error\n");
    return false;
  }
  if (ret == 0) {
    if (timed_out != nullptr) {
      *timed_out = true;
    }
    return false;
  }

  sockaddr_in peer{};
  socklen_t peer_len = sizeof(peer);
  const int cfd = accept(server_fd_, reinterpret_cast<sockaddr *>(&peer), &peer_len);
  if (cfd < 0) {
    BENCH_LOGE("Accept connection failed\n");
    return false;
  }

  *out_fd = cfd;
  return true;
}

bool TCPServer::AcceptConnection() {
  bool timed_out = false;
  if (!AcceptIntoClientFd(&client_socket_, kAcceptConnTimeoutMs, &timed_out)) {
    if (timed_out) {
      BENCH_LOGE("Accept connection timeout (no new connection in %d ms)\n", kAcceptConnTimeoutMs);
    }
    return false;
  }
  return true;
}

uint64_t TCPServer::ReceiveUint64() const {
  uint64_t received_data = 0;

  // Receive one uint64_t payload from the connected client.
  ssize_t bytes_received = recv(client_socket_, &received_data, sizeof(uint64_t), 0);
  if (bytes_received < 0) {
    BENCH_LOGE("Received data failed\n");
    return 0;
  } else if (bytes_received == 0) {
    BENCH_LOGE("Client connection break\n");
    return 0;
  } else if (bytes_received != sizeof(uint64_t)) {
    BENCH_LOGE("Invalid data size, expect: %zu Bytes, actual received: %zd Bytes\n", sizeof(uint64_t), bytes_received);
    return 0;
  }

  // Convert network byte order back to host byte order.
  received_data = be64toh(received_data);
  return received_data;
}

bool TCPServer::SendUint64(uint64_t data) const {
  uint64_t network_data = htobe64(data);
  if (send(client_socket_, &network_data, sizeof(uint64_t), 0) < 0) {
    BENCH_LOGE("Send uint64 to tcp peer failed\n");
    return false;
  }
  return true;
}

bool TCPServer::SendTaskStatus() const {
  bool status = true;
  if (send(client_socket_, &status, sizeof(status), 0) < 0) {
    BENCH_LOGE("Send status to tcp client failed\n");
    return false;
  }
  return true;
}

bool TCPServer::ReceiveTaskStatus() const {
  bool received = false;
  // Receive the task status flag.
  ssize_t bytes_received = recv(client_socket_, &received, sizeof(received), 0);
  if (bytes_received < 0) {
    BENCH_LOGE("Received status failed\n");
    return false;
  } else if (bytes_received == 0) {
    BENCH_LOGE("Client connection break\n");
    return false;
  }

  if (received) {
    return true;
  } else {
    BENCH_LOGE("Tcp server received status failed\n");
    return false;
  }
}

void TCPServer::DisConnectClient() {
  if (client_socket_ != -1) {
    (void)close(client_socket_);
    client_socket_ = -1;
  }
}

void TCPServer::StopServer() {
  DisConnectClient();
  if (server_fd_ != -1) {
    (void)close(server_fd_);
    server_fd_ = -1;
  }
}

TCPServer::~TCPServer() {
  StopServer();
}
