/**
* This program is free software, you can redistribute it and/or modify it.
* Copyright (c) 2025 Huawei Technologies Co., Ltd.
* This file is a part of the CANN Open Software.
* Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
* Please refer to the License for details. You may not use this file except in compliance with the License.
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
* See LICENSE in the root of the software repository for the full text of the License.
 */

#include <iostream>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <thread>
#include <cstring>
#include "tcp_client_server.h"

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
    std::cerr << "[ERROR] Send data to tcp server failed" << std::endl;
    return false;
  }
  std::cout << "[INFO] Send data to tcp server success" << std::endl;
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

bool TCPServer::StartServer(uint16_t port) {
  // 创建socket文件描述符
  if ((server_fd_ = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
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

  // 开始监听端口
  if (listen(server_fd_, request_num) < 0) {
    std::cerr << "[ERROR] Listen port failed" << std::endl;
    return false;
  }

  std::cout << "[INFO] Tcp server is listening port: " << port << "..." << std::endl;
  return true;
}

bool TCPServer::AcceptConnection() {
  int timeout_ms= 5000;
  struct pollfd pfd;
  pfd.fd = server_fd_;
  pfd.events = POLLIN;

  auto ret = poll(&pfd, static_cast<nfds_t>(1), timeout_ms);
  if (ret < 0) {
    std::cerr << "[ERROR] Poll error" << std::endl;
    return false;
  }

  if (ret == 0) {
    std::cerr << "[ERROR] Accept connection timeout (no new connection in " << timeout_ms << " ms)" << std::endl;
    return false;
  }

  if ((client_socket_ =
           accept(server_fd_, reinterpret_cast<sockaddr *>(&address_), reinterpret_cast<socklen_t *>(&addrlen_))) < 0) {
    std::cerr << "[ERROR] Accept connection failed" << std::endl;
    return false;
  }

  std::cout << "[INFO] Tcp server accept connection success" << std::endl;
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
