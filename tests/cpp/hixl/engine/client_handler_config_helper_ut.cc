/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software: you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <gtest/gtest.h>

#include <cstddef>
#include <optional>
#include <string>

#include "nlohmann/json.hpp"
#include "engine/client_handler_config_helper.h"

namespace hixl {
namespace {
HandlerCreateArgs MakeArgs(std::optional<uint8_t> qos, std::optional<uint8_t> tc, std::optional<uint8_t> sl) {
  HandlerCreateArgs args{};
  args.server_ip = "127.0.0.1";
  args.server_port = 26666U;
  args.rdma_tc = tc;
  args.rdma_sl = sl;
  args.handler_type = HandlerCreateArgs::HandlerType::DIRECT;
  args.qos = qos;
  return args;
}
}  // namespace

TEST(ClientHandlerConfigHelperUT, QosAndTcSlBothConfigured) {
  const auto args = MakeArgs(7U, 132U, 4U);
  const auto config = ClientHandlerConfigHelper::BuildGlobalResourceConfig(args);
  const auto json = nlohmann::json::parse(config);
  ASSERT_TRUE(json.contains("comm_resource_config.qos"));
  EXPECT_EQ(json["comm_resource_config.qos"].get<uint8_t>(), 7U);
}

TEST(ClientHandlerConfigHelperUT, QosAndTcSlBothUnconfigured) {
  const auto args = MakeArgs(std::nullopt, std::nullopt, std::nullopt);
  const auto config = ClientHandlerConfigHelper::BuildGlobalResourceConfig(args);
  const auto json = nlohmann::json::parse(config);
  ASSERT_TRUE(json.contains("comm_resource_config.qos"));
  EXPECT_EQ(json["comm_resource_config.qos"].get<uint8_t>(), kQosDefault);
}

TEST(ClientHandlerConfigHelperUT, QosConfiguredTcSlUnconfigured) {
  const auto args = MakeArgs(5U, std::nullopt, std::nullopt);
  const auto config = ClientHandlerConfigHelper::BuildGlobalResourceConfig(args);
  const auto json = nlohmann::json::parse(config);
  ASSERT_TRUE(json.contains("comm_resource_config.qos"));
  EXPECT_EQ(json["comm_resource_config.qos"].get<uint8_t>(), 5U);
}

TEST(ClientHandlerConfigHelperUT, QosUnconfiguredTcConfigured) {
  const auto args = MakeArgs(std::nullopt, 132U, std::nullopt);
  const auto config = ClientHandlerConfigHelper::BuildGlobalResourceConfig(args);
  EXPECT_TRUE(config.empty());
}

TEST(ClientHandlerConfigHelperUT, QosUnconfiguredSlConfigured) {
  const auto args = MakeArgs(std::nullopt, std::nullopt, 4U);
  const auto config = ClientHandlerConfigHelper::BuildGlobalResourceConfig(args);
  EXPECT_TRUE(config.empty());
}

TEST(ClientHandlerConfigHelperUT, QosUnconfiguredTcSlConfiguredWithMaxActiveChannels) {
  auto args = MakeArgs(std::nullopt, 132U, 4U);
  args.max_active_channels = 16U;
  const auto config = ClientHandlerConfigHelper::BuildGlobalResourceConfig(args);
  const auto json = nlohmann::json::parse(config);
  EXPECT_FALSE(json.contains("comm_resource_config.qos"));
  ASSERT_TRUE(json.contains("comm_resource_config.max_active_channels"));
  EXPECT_EQ(json["comm_resource_config.max_active_channels"].get<uint32_t>(), 16U);
}

TEST(ClientHandlerConfigHelperUT, ForwardsUbMemoryConfig) {
  auto args = MakeArgs(std::nullopt, std::nullopt, std::nullopt);
  args.fabric_memory.max_capacity = 10U;
  args.fabric_memory.start_address = 40U;
  args.fabric_memory.task_stream_num = 4U;
  args.fabric_memory.enable_aicpu_unfold = false;
  const auto config = ClientHandlerConfigHelper::BuildGlobalResourceConfig(args);
  const auto json = nlohmann::json::parse(config);
  ASSERT_TRUE(json.contains("fabric_memory.max_capacity"));
  EXPECT_EQ(json["fabric_memory.max_capacity"].get<size_t>(), 10U);
  ASSERT_TRUE(json.contains("fabric_memory.start_address"));
  EXPECT_EQ(json["fabric_memory.start_address"].get<size_t>(), 40U);
  ASSERT_TRUE(json.contains("fabric_memory.task_stream_num"));
  EXPECT_EQ(json["fabric_memory.task_stream_num"].get<size_t>(), 4U);
  ASSERT_TRUE(json.contains("fabric_memory.enable_aicpu_unfold"));
  EXPECT_FALSE(json["fabric_memory.enable_aicpu_unfold"].get<bool>());
}

TEST(ClientHandlerConfigHelperUT, QosUnconfiguredTcSlUnconfiguredWithMaxActiveChannels) {
  auto args = MakeArgs(std::nullopt, std::nullopt, std::nullopt);
  args.max_active_channels = 16U;
  const auto config = ClientHandlerConfigHelper::BuildGlobalResourceConfig(args);
  const auto json = nlohmann::json::parse(config);
  ASSERT_TRUE(json.contains("comm_resource_config.qos"));
  EXPECT_EQ(json["comm_resource_config.qos"].get<uint8_t>(), kQosDefault);
  ASSERT_TRUE(json.contains("comm_resource_config.max_active_channels"));
  EXPECT_EQ(json["comm_resource_config.max_active_channels"].get<uint32_t>(), 16U);
}

TEST(ClientHandlerConfigHelperUT, TransferConfigIsForwardedOnlyWhenExplicitlyConfigured) {
  auto args = MakeArgs(std::nullopt, 132U, 4U);
  auto config = ClientHandlerConfigHelper::BuildGlobalResourceConfig(args);
  EXPECT_TRUE(config.empty());

  args.max_transfer_count_per_batch = 1024U;
  args.max_transfer_count_per_batch_configured = true;
  config = ClientHandlerConfigHelper::BuildGlobalResourceConfig(args);
  auto json = nlohmann::json::parse(config);
  ASSERT_TRUE(json.contains("transfer_config.max_transfer_count_per_batch"));
  EXPECT_EQ(json["transfer_config.max_transfer_count_per_batch"].get<uint32_t>(), 1024U);
}
}  // namespace hixl
