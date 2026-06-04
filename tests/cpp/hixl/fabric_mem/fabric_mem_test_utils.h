/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_TESTS_CPP_HIXL_FABRIC_MEM_FABRIC_MEM_TEST_UTILS_H_
#define CANN_HIXL_TESTS_CPP_HIXL_FABRIC_MEM_FABRIC_MEM_TEST_UTILS_H_

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>

namespace hixl::test {
namespace {
constexpr int32_t kPortRangeStart = 28000;
constexpr int32_t kPortRangeEnd = 28999;
constexpr int32_t kMaxTcpPort = 65535;
}  // namespace

inline bool IsTcpPortAvailable(int32_t port) {
  if (port <= 0 || port > kMaxTcpPort) {
    return false;
  }
  const int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    return false;
  }
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(static_cast<uint16_t>(port));
  const bool available = (bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0);
  (void)close(fd);
  return available;
}

inline int32_t AllocateFabricMemTestPort() {
  static std::atomic<uint32_t> port_seq{0U};
  constexpr uint32_t kPortRangeSize = static_cast<uint32_t>(kPortRangeEnd - kPortRangeStart + 1);
  for (uint32_t attempt = 0U; attempt < kPortRangeSize; ++attempt) {
    const uint32_t offset = port_seq.fetch_add(1U, std::memory_order_relaxed);
    const int32_t port = kPortRangeStart + static_cast<int32_t>(offset % kPortRangeSize);
    if (IsTcpPortAvailable(port)) {
      return port;
    }
  }
  const int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    return -1;
  }
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  if (bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
    (void)close(fd);
    return -1;
  }
  socklen_t addr_len = sizeof(addr);
  if (getsockname(fd, reinterpret_cast<sockaddr *>(&addr), &addr_len) != 0) {
    (void)close(fd);
    return -1;
  }
  const int32_t port = ntohs(addr.sin_port);
  (void)close(fd);
  return port;
}
}  // namespace hixl::test

#endif  // CANN_HIXL_TESTS_CPP_HIXL_FABRIC_MEM_FABRIC_MEM_TEST_UTILS_H_
