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

#include <optional>
#include <string>

#include "nlohmann/json.hpp"
#include "engine/client_handler_config_helper.h"

namespace hixl {
namespace {
HandlerCreateArgs MakeArgs(std::optional<uint8_t> qos, std::optional<uint8_t> tc, std::optional<uint8_t> sl) {
  return HandlerCreateArgs{"127.0.0.1", 26666U, tc, sl, HandlerCreateArgs::HandlerType::DIRECT, {}, qos, {}};
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
  const auto json = nlohmann::json::parse(config);
  EXPECT_FALSE(json.contains("comm_resource_config.qos"));
}

TEST(ClientHandlerConfigHelperUT, QosUnconfiguredSlConfigured) {
  const auto args = MakeArgs(std::nullopt, std::nullopt, 4U);
  const auto config = ClientHandlerConfigHelper::BuildGlobalResourceConfig(args);
  const auto json = nlohmann::json::parse(config);
  EXPECT_FALSE(json.contains("comm_resource_config.qos"));
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
}  // namespace hixl
