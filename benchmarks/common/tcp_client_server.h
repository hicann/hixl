/**
* This program is free software, you can redistribute it and/or modify it.
* Copyright (c) 2025 Huawei Technologies Co., Ltd.
* This file is a part of the CANN Open Software.
* Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
* Please refer to the License for details. You may not use this file except in compliance with the License.
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
* See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HIXL_TCP_CLIENT_SERVER_H
#define HIXL_TCP_CLIENT_SERVER_H

#include <string>
#include <netinet/in.h>

class TCPClient {
 public:
  TCPClient();

  bool ConnectToServer(const std::string &host, uint16_t port);

  bool SendUint64(uint64_t data) const;

  void Disconnect();

  ~TCPClient();

 private:
  int32_t sock_ = -1;
  sockaddr_in server_{};
};

class TCPServer {
 public:
  TCPServer();

  bool StartServer(uint16_t port);

  bool AcceptConnection();

  uint64_t ReceiveUint64() const;

  void DisConnectClient();

  void StopServer();

  ~TCPServer();

 private:
  int32_t server_fd_ = -1;
  int32_t request_num = 3;
  int32_t client_socket_ = -1;
  sockaddr_in address_{};
  int32_t opt_ = 1;
  int32_t addrlen_ = sizeof(address_);
};

#endif  // HIXL_TCP_CLIENT_SERVER_H
