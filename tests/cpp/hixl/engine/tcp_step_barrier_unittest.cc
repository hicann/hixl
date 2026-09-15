/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <unistd.h>

#include <atomic>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "tcp_client_server.h"

namespace {
constexpr uint32_t kPeerWaitSec = 10U;
constexpr uint32_t kConnectTimeoutMs = 5000U;
constexpr int kClientCount = 3;
constexpr int kStepCount = 2;
constexpr uint64_t kMemAddr = 0x1000ULL;

uint16_t UniqueTestPort() {
  return static_cast<uint16_t>(19000 + (getpid() % 1000));
}

bool RunInitiator(uint16_t port) {
  TCPClient client;
  if (!client.ConnectToServer("127.0.0.1", port, kConnectTimeoutMs)) {
    return false;
  }
  uint64_t addr = 0;
  if (!client.ReceiveUint64(&addr) || addr != kMemAddr) {
    return false;
  }
  if (!client.ReceiveTaskStatus()) {
    return false;
  }
  for (int i = 0; i < kStepCount; ++i) {
    if (!client.StepBarrier()) {
      return false;
    }
  }
  return client.SendFinished();
}
}  // namespace

TEST(TcpStepBarrier, ThreePeersTwoAlignedSteps) {
  const uint16_t port = UniqueTestPort();
  TcpServerSession server(port, kPeerWaitSec, static_cast<uint32_t>(kClientCount));
  std::atomic<int> server_ok{0};
  std::thread server_thread([&server, &server_ok]() {
    if (!server.WaitForPeers()) {
      return;
    }
    if (!server.SendAddrToPeers(kMemAddr)) {
      return;
    }
    if (!server.RunStepBarrierUntilFinished()) {
      return;
    }
    server_ok.store(1);
  });

  std::atomic<int> client_ok{0};
  std::vector<std::thread> clients;
  clients.reserve(static_cast<size_t>(kClientCount));
  for (int i = 0; i < kClientCount; ++i) {
    clients.emplace_back([port, &client_ok]() {
      if (RunInitiator(port)) {
        client_ok.fetch_add(1);
      }
    });
  }
  for (auto &t : clients) {
    t.join();
  }
  server_thread.join();
  EXPECT_EQ(client_ok.load(), kClientCount);
  EXPECT_EQ(server_ok.load(), 1);
}
