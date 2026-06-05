/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HIXL_TESTS_CPP_HIXL_ENGINE_HIXL_TEST_HELPERS_H
#define HIXL_TESTS_CPP_HIXL_ENGINE_HIXL_TEST_HELPERS_H

#include <arpa/inet.h>
#include <cerrno>
#include <sys/socket.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include "hixl/hixl.h"
#include "../common/async_transfer_test_helpers.h"

namespace hixl {
namespace test_helpers {

inline bool CheckIpv6Supported() {
  int fd = socket(AF_INET6, SOCK_STREAM, 0);
  if (fd < 0) {
    return false;
  }
  struct sockaddr_in6 addr {};
  addr.sin6_family = AF_INET6;
  (void)inet_pton(AF_INET6, "::1", &addr.sin6_addr);
  addr.sin6_port = htons(0U);
  bool ok = (connect(fd, (sockaddr *)&addr, sizeof(addr)) != -1 || errno != EADDRNOTAVAIL);
  close(fd);
  return ok;
}

inline void RegisterInt32DeviceMem(Hixl &engine, int32_t &value, MemHandle &handle) {
  MemDesc mem{};
  mem.addr = reinterpret_cast<uintptr_t>(&value);
  mem.len = sizeof(int32_t);
  EXPECT_EQ(engine.RegisterMem(mem, MEM_DEVICE, handle), SUCCESS);
}

inline void WaitForAllAsyncTransfers(Hixl &engine, TransferReq *req_list, int req_count, int poll_interval_ms = 10,
                                     int max_wait_sec = 5) {
  test_helpers::WaitForAllAsyncTransfers<Hixl, TransferStatus>(engine, req_list, req_count, poll_interval_ms,
                                                               max_wait_sec);
}

}  // namespace test_helpers
}  // namespace hixl

#endif  // HIXL_TESTS_CPP_HIXL_ENGINE_HIXL_TEST_HELPERS_H
