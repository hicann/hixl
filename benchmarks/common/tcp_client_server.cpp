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

  if (connect(sock_, reinterpret_cast<sockaddr *>(&server_), sizeof(server_)) < 0) {
    std::cerr << "[ERROR] Connect to tcp server failed" << std::endl;
    return false;
  }

  std::cout << "[INFO] Connect to tcp server success" << std::endl;
  return true;
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
