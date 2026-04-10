/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <string>
#include <vector>
#include <gtest/gtest.h>

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
  StatisticManager::GetInstance().UpdateFabricMemCosts(kClientChannelId, kCost * 3U, kCost, kBytes * 3U, 3U);

  const auto snapshot = StatisticManager::GetInstance().GetStatisticInfoSnapshot(kClientChannelId);
  EXPECT_EQ(snapshot.buffer_transfer_statistic_info.transfer.total_cost, kCost);
  EXPECT_EQ(snapshot.buffer_transfer_statistic_info.total_bytes, kBytes);
  EXPECT_EQ(snapshot.buffer_transfer_statistic_info.total_op_desc_count, 2U);
  EXPECT_EQ(snapshot.direct_transfer_statistic_info.transfer.total_cost, kCost * 2U);
  EXPECT_EQ(snapshot.direct_transfer_statistic_info.total_bytes, kBytes * 2U);
  EXPECT_EQ(snapshot.direct_transfer_statistic_info.total_op_desc_count, 4U);
  EXPECT_EQ(snapshot.fabric_mem_transfer_statistic_info.transfer.total_cost, kCost * 3U);
  EXPECT_EQ(snapshot.fabric_mem_transfer_statistic_info.total_bytes, kBytes * 3U);
  EXPECT_EQ(snapshot.fabric_mem_transfer_statistic_info.total_op_desc_count, 3U);
}

TEST_F(StatisticManagerUTest, TestRemoveStatisticChannelsAndFabricMemDump) {
  StatisticManager::GetInstance().SetEnableUseFabricMem(true);
  StatisticManager::GetInstance().UpdateConnectTotalCost(kClientChannelId, kCost);
  StatisticManager::GetInstance().UpdateTcpConnectCost(kClientChannelId, kCost);
  StatisticManager::GetInstance().UpdateFabricMemCosts(kClientChannelId, kCost, kCost, kBytes, 1U);
  StatisticManager::GetInstance().Dump();
  RemoveClientAndServerStatisticChannels(kChannelId);

  const auto client_snapshot = StatisticManager::GetInstance().GetStatisticInfoSnapshot(kClientChannelId);
  const auto server_snapshot = StatisticManager::GetInstance().GetStatisticInfoSnapshot(kServerChannelId);
  EXPECT_EQ(client_snapshot.connect_statistic_info.connect_total.times, 0UL);
  EXPECT_EQ(server_snapshot.connect_statistic_info.connect_total.times, 0UL);
  StatisticManager::GetInstance().SetEnableUseFabricMem(false);
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
