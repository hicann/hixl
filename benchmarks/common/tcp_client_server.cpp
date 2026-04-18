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
#include <cstdio>
#include <chrono>
#include <iostream>
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

namespace {

constexpr int kListenPollSliceMs = 250;
constexpr int kRecvNotifyPollTimeoutMs = 30 * 60 * 1000;

void CloseClientFds(std::vector<int> *fds) {
  for (int fd : *fds) {
    (void)::close(fd);
  }
  fds->clear();
}

bool HandleNewClient(int cfd, uint64_t addr_to_send, std::vector<int> *out_client_fds) {
  if (!TcpSendUint64(cfd, addr_to_send)) {
    (void)::close(cfd);
    std::printf("[ERROR] TcpSendUint64 failed.\n");
    CloseClientFds(out_client_fds);
    return false;
  }
  if (!TcpSendTaskStatus(cfd)) {
    (void)::close(cfd);
    std::printf("[ERROR] TcpSendTaskStatus failed.\n");
    CloseClientFds(out_client_fds);
    return false;
  }
  out_client_fds->push_back(cfd);
  std::printf("[INFO] TCP handshake done, peer count=%zu\n", out_client_fds->size());
  return true;
}

bool RunConnectPhaseFill(TCPServer *srv, uint16_t port, uint64_t addr_to_send, uint32_t max_connect_phase_sec,
                         uint32_t expected_peer_count, std::vector<int> *out_client_fds) {
  if (srv == nullptr || out_client_fds == nullptr) {
    return false;
  }
  out_client_fds->clear();
  if (!srv->StartServer(port)) {
    std::printf("[ERROR] Failed to start TCP server.\n");
    return false;
  }
  std::printf(
      "[INFO] TCP server started (connect phase, expect %" PRIu32 " peer(s), max %" PRIu32 " s wall time).\n",
      expected_peer_count, max_connect_phase_sec);
  const auto t0 = std::chrono::steady_clock::now();
  const auto budget = std::chrono::seconds(static_cast<int>(max_connect_phase_sec));

  while (out_client_fds->size() < expected_peer_count) {
    const auto now = std::chrono::steady_clock::now();
    if (now >= t0 + budget) {
      const size_t got = out_client_fds->size();
      if (got == 0U) {
        std::printf("[ERROR] TCP connect phase: no client within %" PRIu32 " s.\n", max_connect_phase_sec);
      } else {
        std::printf(
            "[ERROR] TCP connect phase: timeout after %" PRIu32 " s (expected %" PRIu32 " peers, got %zu).\n",
            max_connect_phase_sec, expected_peer_count, got);
      }
      CloseClientFds(out_client_fds);
      return false;
    }
    const int64_t remain_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>((t0 + budget) - now).count();
    const int poll_ms = static_cast<int>(
        std::min<int64_t>(static_cast<int64_t>(kListenPollSliceMs), std::max<int64_t>(remain_ms, 1LL)));

    int cfd = -1;
    bool timed_out = false;
    if (!srv->AcceptIntoClientFd(&cfd, poll_ms, &timed_out)) {
      if (!timed_out) {
        std::printf("[ERROR] AcceptIntoClientFd failed.\n");
        CloseClientFds(out_client_fds);
        return false;
      }
      continue;
    }
    if (!HandleNewClient(cfd, addr_to_send, out_client_fds)) {
      return false;
    }
  }
  std::printf("[INFO] TCP connect phase finished, N=%zu (expected=%" PRIu32 ")\n", out_client_fds->size(),
              expected_peer_count);
  return true;
}

bool PollFds(std::vector<struct pollfd> *pf) {
  const int pr = poll(pf->data(), static_cast<nfds_t>(pf->size()), kRecvNotifyPollTimeoutMs);
  if (pr < 0) {
    std::printf("[ERROR] RecvNotifyAll poll failed\n");
    return false;
  }
  if (pr == 0) {
    std::printf("[ERROR] RecvNotifyAll poll timeout\n");
    return false;
  }
  return true;
}

bool ProcessPollEvent(const struct pollfd &pfd, const std::vector<int> &client_fds, std::vector<char> *done,
                      size_t *ndone) {
  if ((pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
    std::printf("[ERROR] RecvNotifyAll socket error\n");
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
    std::cerr << "[ERROR] Send uint64 to tcp peer failed" << std::endl;
    return false;
  }
  std::cout << "[INFO] Send uint64 to tcp peer success" << std::endl;
  return true;
}

bool TcpSendTaskStatus(int fd) {
  const bool status = true;
  if (send(fd, &status, sizeof(status), 0) < 0) {
    std::cerr << "[ERROR] Send status to tcp client failed" << std::endl;
    return false;
  }
  std::cout << "[INFO] Send status to tcp client success" << std::endl;
  return true;
}

bool TcpRecvTaskStatusOk(int fd) {
  bool received = false;
  const ssize_t bytes_received = recv(fd, &received, sizeof(received), 0);
  if (bytes_received < 0) {
    std::cerr << "[ERROR] Received status failed" << std::endl;
    return false;
  }
  if (bytes_received == 0) {
    std::cout << "[INFO] Client connection break" << std::endl;
    return false;
  }
  if (received) {
    std::cout << "[INFO] Tcp server received status success" << std::endl;
    return true;
  }
  std::cout << "[ERROR] Tcp server received status failed" << std::endl;
  return false;
}

TcpServerSession::TcpServerSession(uint16_t port, uint32_t max_connect_phase_wall_sec, uint32_t expected_peer_count)
    : port_(port), max_wall_sec_(max_connect_phase_wall_sec), expected_peer_count_(expected_peer_count) {}

TcpServerSession::~TcpServerSession() { ShutdownClientsAndListen(); }

void TcpServerSession::ShutdownClientsAndListen() {
  for (int fd : client_fds_) {
    (void)::close(fd);
  }
  client_fds_.clear();
  server_.StopServer();
}

bool TcpServerSession::RunConnectPhaseInThread(uint64_t mem_addr, uint32_t wall_sec) {
  bool ok = false;
  std::thread worker([this, mem_addr, wall_sec, &ok]() {
    ok = RunConnectPhaseFill(&server_, port_, mem_addr, wall_sec, expected_peer_count_, &client_fds_);
  });
  worker.join();
  return ok;
}

bool TcpServerSession::WaitAndSendAddr(uint64_t mem_addr) {
  ShutdownClientsAndListen();
  const bool ok = RunConnectPhaseInThread(mem_addr, max_wall_sec_);
  if (!ok) {
    ShutdownClientsAndListen();
    return false;
  }
  if (client_fds_.empty()) {
    std::printf("[ERROR] TCP connect phase: no client connections.\n");
    ShutdownClientsAndListen();
    return false;
  }
  return true;
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

bool TCPClient::ConnectToServer(const std::string &host, uint16_t port) {
  sock_ = socket(AF_INET, SOCK_STREAM, 0);
  if (sock_ == -1) {
    std::cerr << "[ERROR] Create socket failed" << std::endl;
    return false;
  }

  server_.sin_family = AF_INET;
  server_.sin_port = htons(port);

  if (inet_addr(host.c_str()) == INADDR_NONE) {
    std::cerr << "[ERROR] Invalid server ip: " << host << std::endl;
  } else {
    server_.sin_addr.s_addr = inet_addr(host.c_str());
  }

  uint16_t retry_times = 5;
  uint16_t i = 0;
  while (i < retry_times) {
    auto ret = connect(sock_, reinterpret_cast<sockaddr *>(&server_), sizeof(server_));
    if (ret < 0) {
      std::cout << "[WARNING] Connect to tcp server failed, retry_times: " << i << std::endl;
      i++;
      std::this_thread::sleep_for(std::chrono::seconds(1));
    } else {
      std::cout << "[INFO] Connect to tcp server success" << std::endl;
      return true;
    }
  }
  std::cerr << "[ERROR] Connect to tcp server failed" << std::endl;
  return false;
}

bool TCPClient::SendUint64(uint64_t data) const {
  // 将主机字节序转换为网络字节序
  uint64_t network_data = htobe64(data);
  if (send(sock_, &network_data, sizeof(uint64_t), 0) < 0) {
    std::cerr << "[ERROR] Send uint64 to tcp peer failed" << std::endl;
    return false;
  }
  std::cout << "[INFO] Send uint64 to tcp peer success" << std::endl;
  return true;
}

bool TCPClient::ReceiveUint64(uint64_t *out) const {
  uint64_t received_data = 0;
  ssize_t bytes_received = recv(sock_, &received_data, sizeof(uint64_t), 0);
  if (bytes_received < 0) {
    std::cerr << "[ERROR] Received uint64 data failed" << std::endl;
    return false;
  }
  if (bytes_received == 0) {
    std::cout << "[INFO] Tcp peer connection break" << std::endl;
    return false;
  }
  if (bytes_received != static_cast<ssize_t>(sizeof(uint64_t))) {
    std::cerr << "[ERROR] Invalid uint64 size, expect: " << sizeof(uint64_t) << " actual: " << bytes_received
              << std::endl;
    return false;
  }
  if (out != nullptr) {
    *out = be64toh(received_data);
  }
  std::cout << "[INFO] Tcp client received uint64 data success" << std::endl;
  return true;
}

bool TCPClient::SendTaskStatus() const {
  bool status = true;
  if (send(sock_, &status, sizeof(status), 0) < 0) {
    std::cerr << "[ERROR] Send status to tcp server failed" << std::endl;
    return false;
  }
  std::cout << "[INFO] Send status to tcp server success" << std::endl;
  return true;
}

bool TCPClient::ReceiveTaskStatus() const {
  bool received = false;
  // 接收数据
  ssize_t bytes_received = recv(sock_, &received, sizeof(received), 0);
  if (bytes_received < 0) {
    std::cerr << "[ERROR] Received status failed" << std::endl;
    return false;
  } else if (bytes_received == 0) {
    std::cout << "[INFO] Server connection break" << std::endl;
    return false;
  } 

  if (received) {
    std::cout << "[INFO] Tcp client received status success" << std::endl;
    return true;
  } else {
    std::cout << "[ERROR] Tcp client received status failed" << std::endl;
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
    std::cerr << "[ERROR] Create socket failed" << std::endl;
    return false;
  }

  // 设置socket选项
  if (setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt_, sizeof(opt_))) {
    std::cerr << "[ERROR] Set socket option failed" << std::endl;
    return false;
  }

  address_.sin_family = AF_INET;
  address_.sin_addr.s_addr = INADDR_ANY;
  address_.sin_port = htons(port);

  // 绑定socket到端口
  if (bind(server_fd_, reinterpret_cast<sockaddr *>(&address_), sizeof(address_)) < 0) {
    std::cerr << "[ERROR] Bind port failed" << std::endl;
    return false;
  }

#if defined(SOMAXCONN)
  const int kSysMax = SOMAXCONN;
#else
  const int kSysMax = 4096;
#endif
  const int backlog = std::max(1, std::min(listen_backlog, kSysMax));
  if (listen(server_fd_, backlog) < 0) {
    std::cerr << "[ERROR] Listen port failed" << std::endl;
    return false;
  }

  std::cout << "[INFO] Tcp server is listening port: " << port << " backlog=" << backlog << "..." << std::endl;
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
    std::cerr << "[ERROR] Poll error" << std::endl;
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
    std::cerr << "[ERROR] Accept connection failed" << std::endl;
    return false;
  }

  *out_fd = cfd;
  std::cout << "[INFO] Tcp server accept connection success" << std::endl;
  return true;
}

bool TCPServer::AcceptConnection() {
  bool timed_out = false;
  if (!AcceptIntoClientFd(&client_socket_, 5000, &timed_out)) {
    if (timed_out) {
      std::cerr << "[ERROR] Accept connection timeout (no new connection in 5000 ms)" << std::endl;
    }
    return false;
  }
  return true;
}

uint64_t TCPServer::ReceiveUint64() const {
  uint64_t received_data = 0;

  // 接收uint64_t数据
  ssize_t bytes_received = recv(client_socket_, &received_data, sizeof(uint64_t), 0);
  if (bytes_received < 0) {
    std::cerr << "[ERROR] Received data failed" << std::endl;
    return 0;
  } else if (bytes_received == 0) {
    std::cout << "[INFO] Client connection break" << std::endl;
    return 0;
  } else if (bytes_received != sizeof(uint64_t)) {
    std::cerr << "[ERROR] Invalid data size, expect: " << sizeof(uint64_t)
              << "Bytes, actual received: " << bytes_received << "Bytes" << std::endl;
    return 0;
  }

  // 网络字节序转换为主机字节序
  received_data = be64toh(received_data);
  std::cout << "[INFO] Tcp server received uint64 data success" << std::endl;
  return received_data;
}

bool TCPServer::SendUint64(uint64_t data) const {
  uint64_t network_data = htobe64(data);
  if (send(client_socket_, &network_data, sizeof(uint64_t), 0) < 0) {
    std::cerr << "[ERROR] Send uint64 to tcp peer failed" << std::endl;
    return false;
  }
  std::cout << "[INFO] Send uint64 to tcp peer success" << std::endl;
  return true;
}

bool TCPServer::SendTaskStatus() const {
  bool status = true;
  if (send(client_socket_, &status, sizeof(status), 0) < 0) {
    std::cerr << "[ERROR] Send status to tcp client failed" << std::endl;
    return false;
  }
  std::cout << "[INFO] Send status to tcp client success" << std::endl;
  return true;
}

bool TCPServer::ReceiveTaskStatus() const {
  bool received = false;
  // 接收数据
  ssize_t bytes_received = recv(client_socket_, &received, sizeof(received), 0);
  if (bytes_received < 0) {
    std::cerr << "[ERROR] Received status failed" << std::endl;
    return false;
  } else if (bytes_received == 0) {
    std::cout << "[INFO] Client connection break" << std::endl;
    return false;
  } 

  if (received) {
    std::cout << "[INFO] Tcp server received status success" << std::endl;
    return true;
  } else {
    std::cout << "[ERROR] Tcp server received status failed" << std::endl;
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
