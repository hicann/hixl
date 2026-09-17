/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <atomic>
#include <string>
#include <thread>
#include <vector>
#include <gtest/gtest.h>

#include "common/statistic_utils.h"
#include "adxl/statistic_manager.h"

namespace adxl {
namespace {
constexpr char kChannelId[] = "test";
const auto kClientChannelId = StatisticManager::GetClientStatisticChannelId(kChannelId);
const auto kServerChannelId = StatisticManager::GetServerStatisticChannelId(kChannelId);
constexpr uint64_t kCost = 100;
constexpr uint64_t kBytes = 1024;

void RemoveClientAndServerStatisticChannels(const std::string &peer_channel_id) {
  auto &sm = StatisticManager::GetInstance();
  sm.RemoveStatisticChannel(peer_channel_id, true);
  sm.RemoveStatisticChannel(peer_channel_id, false);
}

void UpdateTransfer(StatisticManager &manager, const std::string &channel_id, bool direct, uint64_t cost,
                    uint64_t total_bytes) {
  if (direct) {
    manager.UpdateDirectTransferCost(channel_id, cost, total_bytes, 1U);
  } else {
    manager.UpdateBufferTransferCost(channel_id, cost, total_bytes, 1U);
  }
}

void RunConcurrentTransferUpdates(StatisticManager &manager, const std::string &channel_id, bool direct,
                                  uint64_t worker_count, uint64_t updates_per_worker, uint64_t cost,
                                  uint64_t total_bytes) {
  std::vector<std::thread> workers;
  workers.reserve(worker_count);
  for (uint64_t worker = 0U; worker < worker_count; ++worker) {
    workers.emplace_back([&manager, &channel_id, direct, updates_per_worker, cost, total_bytes]() {
      for (uint64_t i = 0U; i < updates_per_worker; ++i) {
        UpdateTransfer(manager, channel_id, direct, cost, total_bytes);
        if ((i & 0x3FFU) == 0U) {
          std::this_thread::yield();
        }
      }
    });
  }
  for (auto &worker : workers) {
    worker.join();
  }
}

bool IsConsistent(const TransferStatisticSnapshot &snapshot, uint64_t cost, uint64_t total_bytes) {
  const uint64_t times = snapshot.transfer.times;
  if (times == 0U) {
    return snapshot.transfer.max_cost == 0U && snapshot.transfer.total_cost == 0U && snapshot.total_bytes == 0U &&
           snapshot.total_op_desc_count == 0U;
  }
  return snapshot.transfer.max_cost == cost && snapshot.transfer.total_cost == times * cost &&
         snapshot.total_bytes == times * total_bytes && snapshot.total_op_desc_count == times;
}

}  // namespace
class StatisticManagerUTest : public ::testing::Test {
 protected:
  void SetUp() override {
    StatisticManager::GetInstance().RegisterChannel(kClientChannelId);
    StatisticManager::GetInstance().RegisterChannel(kServerChannelId);
  }
  void TearDown() override {
    RemoveClientAndServerStatisticChannels(kChannelId);
  }
};

TEST_F(StatisticManagerUTest, TestDump) {
  StatisticManager::GetInstance().UpdateBufferTransferCost(kClientChannelId, kCost, kBytes, 1U);
  StatisticManager::GetInstance().Dump();
}

TEST_F(StatisticManagerUTest, TestDirectTransferDump) {
  StatisticManager::GetInstance().UpdateDirectTransferCost(kClientChannelId, kCost, kBytes, 1U);
  StatisticManager::GetInstance().Dump();
}

TEST_F(StatisticManagerUTest, TestDumpStartsNewTransferWindow) {
  auto &manager = StatisticManager::GetInstance();
  manager.UpdateBufferTransferCost(kClientChannelId, kCost, kBytes, 1U);
  manager.UpdateDirectTransferCost(kClientChannelId, kCost, kBytes, 1U);

  manager.Dump();

  auto snapshot = manager.GetStatisticInfoSnapshot(kClientChannelId);
  EXPECT_EQ(snapshot.buffer_transfer_statistic_info.transfer.times, 0U);
  EXPECT_EQ(snapshot.direct_transfer_statistic_info.transfer.times, 0U);

  manager.UpdateBufferTransferCost(kClientChannelId, kCost, kBytes, 1U);
  manager.UpdateDirectTransferCost(kClientChannelId, kCost, kBytes, 1U);
  snapshot = manager.GetStatisticInfoSnapshot(kClientChannelId);
  EXPECT_EQ(snapshot.buffer_transfer_statistic_info.transfer.times, 1U);
  EXPECT_EQ(snapshot.direct_transfer_statistic_info.transfer.times, 1U);
}

TEST_F(StatisticManagerUTest, TestConnectStatisticSnapshot) {
  StatisticManager::GetInstance().UpdateConnectTotalCost(kClientChannelId, kCost * 4U);
  StatisticManager::GetInstance().UpdateTcpConnectCost(kClientChannelId, kCost);
  StatisticManager::GetInstance().UpdateHcclTotalCost(kClientChannelId, kCost * 3U);
  StatisticManager::GetInstance().UpdateHcclCommInitCost(kClientChannelId, kCost);
  StatisticManager::GetInstance().UpdateHcclCommBindMemCost(kClientChannelId, kCost);
  StatisticManager::GetInstance().UpdateHcclCommPrepareCost(kClientChannelId, kCost);
  StatisticManager::GetInstance().UpdateConnectTotalCost(kServerChannelId, kCost * 2U);
  StatisticManager::GetInstance().UpdateHcclTotalCost(kServerChannelId, kCost);

  const auto client_snapshot = StatisticManager::GetInstance().GetStatisticInfoSnapshot(kClientChannelId);
  EXPECT_EQ(client_snapshot.connect_statistic_info.connect_total.times, 1UL);
  EXPECT_EQ(client_snapshot.connect_statistic_info.connect_total.total_cost, kCost * 4U);
  EXPECT_EQ(client_snapshot.connect_statistic_info.tcp_connect.total_cost, kCost);
  EXPECT_EQ(client_snapshot.connect_statistic_info.hccl_total.total_cost, kCost * 3U);
  EXPECT_EQ(client_snapshot.connect_statistic_info.hccl_comm_init.total_cost, kCost);
  EXPECT_EQ(client_snapshot.connect_statistic_info.hccl_comm_bind_mem.total_cost, kCost);
  EXPECT_EQ(client_snapshot.connect_statistic_info.hccl_comm_prepare.total_cost, kCost);
  const auto server_snapshot = StatisticManager::GetInstance().GetStatisticInfoSnapshot(kServerChannelId);
  EXPECT_EQ(server_snapshot.connect_statistic_info.connect_total.total_cost, kCost * 2U);
  EXPECT_EQ(server_snapshot.connect_statistic_info.tcp_connect.total_cost, 0UL);
  EXPECT_EQ(server_snapshot.connect_statistic_info.hccl_total.total_cost, kCost);
}

TEST_F(StatisticManagerUTest, TestTransferStatisticSnapshot) {
  StatisticManager::GetInstance().UpdateBufferTransferCost(kClientChannelId, kCost, kBytes, 2U);
  StatisticManager::GetInstance().UpdateDirectTransferCost(kClientChannelId, kCost * 2U, kBytes * 2U, 4U);

  const auto snapshot = StatisticManager::GetInstance().GetStatisticInfoSnapshot(kClientChannelId);
  EXPECT_EQ(snapshot.buffer_transfer_statistic_info.transfer.total_cost, kCost);
  EXPECT_EQ(snapshot.buffer_transfer_statistic_info.total_bytes, kBytes);
  EXPECT_EQ(snapshot.buffer_transfer_statistic_info.total_op_desc_count, 2U);
  EXPECT_EQ(snapshot.direct_transfer_statistic_info.transfer.total_cost, kCost * 2U);
  EXPECT_EQ(snapshot.direct_transfer_statistic_info.total_bytes, kBytes * 2U);
  EXPECT_EQ(snapshot.direct_transfer_statistic_info.total_op_desc_count, 4U);
}

TEST_F(StatisticManagerUTest, TestConcurrentTransferKeepsFieldsConsistent) {
  auto &manager = StatisticManager::GetInstance();
  constexpr uint64_t kWorkerCount = 4U;
  constexpr uint64_t kUpdatesPerWorker = hixl::statistic::kResetTimes / kWorkerCount + 1U;
  constexpr uint64_t kTotalUpdates = kWorkerCount * kUpdatesPerWorker;
  constexpr uint64_t kConcurrentCost = 7U;
  constexpr uint64_t kConcurrentBytes = 4096U;

  RunConcurrentTransferUpdates(manager, kClientChannelId, false, kWorkerCount, kUpdatesPerWorker, kConcurrentCost,
                               kConcurrentBytes);

  const auto buffer_snapshot = manager.GetStatisticInfoSnapshot(kClientChannelId);
  EXPECT_EQ(buffer_snapshot.buffer_transfer_statistic_info.transfer.times, kTotalUpdates);
  EXPECT_EQ(buffer_snapshot.buffer_transfer_statistic_info.transfer.max_cost, kConcurrentCost);
  EXPECT_EQ(buffer_snapshot.buffer_transfer_statistic_info.transfer.total_cost, kTotalUpdates * kConcurrentCost);
  EXPECT_EQ(buffer_snapshot.buffer_transfer_statistic_info.total_bytes, kTotalUpdates * kConcurrentBytes);
  EXPECT_EQ(buffer_snapshot.buffer_transfer_statistic_info.total_op_desc_count, kTotalUpdates);

  RunConcurrentTransferUpdates(manager, kClientChannelId, true, kWorkerCount, kUpdatesPerWorker, kConcurrentCost,
                               kConcurrentBytes);

  const auto direct_snapshot = manager.GetStatisticInfoSnapshot(kClientChannelId);
  EXPECT_EQ(direct_snapshot.direct_transfer_statistic_info.transfer.times, kTotalUpdates);
  EXPECT_EQ(direct_snapshot.direct_transfer_statistic_info.transfer.max_cost, kConcurrentCost);
  EXPECT_EQ(direct_snapshot.direct_transfer_statistic_info.transfer.total_cost, kTotalUpdates * kConcurrentCost);
  EXPECT_EQ(direct_snapshot.direct_transfer_statistic_info.total_bytes, kTotalUpdates * kConcurrentBytes);
  EXPECT_EQ(direct_snapshot.direct_transfer_statistic_info.total_op_desc_count, kTotalUpdates);
}

TEST_F(StatisticManagerUTest, TestConcurrentDumpStartsNewTransferWindow) {
  auto &manager = StatisticManager::GetInstance();
  constexpr uint64_t kWorkerCount = 8U;
  constexpr uint64_t kUpdatesPerWorker = hixl::statistic::kResetTimes / kWorkerCount + 1U;
  constexpr uint64_t kConcurrentCost = 7U;
  constexpr uint64_t kConcurrentBytes = 4096U;

  RunConcurrentTransferUpdates(manager, kClientChannelId, false, kWorkerCount, kUpdatesPerWorker, kConcurrentCost,
                               kConcurrentBytes);
  RunConcurrentTransferUpdates(manager, kClientChannelId, true, kWorkerCount, kUpdatesPerWorker, kConcurrentCost,
                               kConcurrentBytes);

  manager.Dump();
  auto snapshot = manager.GetStatisticInfoSnapshot(kClientChannelId);
  EXPECT_EQ(snapshot.buffer_transfer_statistic_info.transfer.times, 0U);
  EXPECT_EQ(snapshot.direct_transfer_statistic_info.transfer.times, 0U);

  manager.UpdateBufferTransferCost(kClientChannelId, kConcurrentCost, kConcurrentBytes, 1U);
  manager.UpdateDirectTransferCost(kClientChannelId, kConcurrentCost, kConcurrentBytes, 1U);
  snapshot = manager.GetStatisticInfoSnapshot(kClientChannelId);
  EXPECT_TRUE(IsConsistent(snapshot.buffer_transfer_statistic_info, kConcurrentCost, kConcurrentBytes));
  EXPECT_TRUE(IsConsistent(snapshot.direct_transfer_statistic_info, kConcurrentCost, kConcurrentBytes));
}

TEST_F(StatisticManagerUTest, TestRemoveStatisticChannelsAndDump) {
  StatisticManager::GetInstance().UpdateConnectTotalCost(kClientChannelId, kCost);
  StatisticManager::GetInstance().UpdateTcpConnectCost(kClientChannelId, kCost);
  StatisticManager::GetInstance().UpdateBufferTransferCost(kClientChannelId, kCost, kBytes, 1U);
  StatisticManager::GetInstance().Dump();
  RemoveClientAndServerStatisticChannels(kChannelId);

  const auto client_snapshot = StatisticManager::GetInstance().GetStatisticInfoSnapshot(kClientChannelId);
  const auto server_snapshot = StatisticManager::GetInstance().GetStatisticInfoSnapshot(kServerChannelId);
  EXPECT_EQ(client_snapshot.connect_statistic_info.connect_total.times, 0UL);
  EXPECT_EQ(server_snapshot.connect_statistic_info.connect_total.times, 0UL);
}

// Client disconnect must not clear server-side stats for the same peer id.
TEST_F(StatisticManagerUTest, TestRemoveStatisticChannelClientPreservesServer) {
  StatisticManager::GetInstance().UpdateConnectTotalCost(kClientChannelId, kCost);
  StatisticManager::GetInstance().UpdateConnectTotalCost(kServerChannelId, kCost * 2U);
  StatisticManager::GetInstance().RemoveStatisticChannel(kChannelId, true);

  const auto client_snapshot = StatisticManager::GetInstance().GetStatisticInfoSnapshot(kClientChannelId);
  const auto server_snapshot = StatisticManager::GetInstance().GetStatisticInfoSnapshot(kServerChannelId);
  EXPECT_EQ(client_snapshot.connect_statistic_info.connect_total.times, 0UL);
  EXPECT_EQ(server_snapshot.connect_statistic_info.connect_total.times, 1UL);
  EXPECT_EQ(server_snapshot.connect_statistic_info.connect_total.total_cost, kCost * 2U);
}

}  // namespace adxl
