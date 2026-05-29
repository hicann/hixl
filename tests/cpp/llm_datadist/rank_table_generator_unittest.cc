/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <memory>
#include <set>
#include <string>

#include <gtest/gtest.h>
#include "nlohmann/json.hpp"

#include "common/rank_table_generator.h"
#include "common/rank_table_device_json.h"
#include "common/rank_table_generator_v1.h"
#include "common/rank_table_generator_v2.h"
#include "depends/llm_datadist/src/data_cache_engine_test_helper.h"
#include "depends/mmpa/src/mmpa_stub.h"

namespace llm {
namespace {
constexpr char kServerId[] = "node_0";
constexpr int32_t kDeviceId = 0;
constexpr uint32_t kDevicePort = 23456U;

nlohmann::json GetOnlyDevice(const std::string &comm_res) {
  auto json = nlohmann::json::parse(comm_res);
  return json.at("server_list").at(0).at("device").at(0);
}

std::set<std::string> CollectDevicePorts(const nlohmann::json &rank_table_json) {
  std::set<std::string> device_ports;
  for (const auto &server : rank_table_json.at("server_list")) {
    for (const auto &device : server.at("device")) {
      device_ports.emplace(device.at("device_port").get<std::string>());
    }
  }
  return device_ports;
}

class AutoCommResV1RuntimeMock : public AutoCommResRuntimeMock {
 public:
  const char *aclrtGetSocName() override {
    return "Ascend910B1";
  }
};
}  // namespace

class RankTableGeneratorUnitTest : public ::testing::Test {
 protected:
  void SetUp() override {
    MockMmpaForHcclApi::Install();
    AutoCommResRuntimeMock::Install();
    AutoCommResRuntimeMock::SetDevice(kDeviceId);
  }

  void TearDown() override {
    AutoCommResRuntimeMock::Reset();
    MockMmpaForHcclApi::Reset();
  }
};

TEST_F(RankTableGeneratorUnitTest, GenerateV2LocalCommResWritesConfiguredDevicePort) {
  std::string local_comm_res;
  ASSERT_EQ(LocalCommResGenerator::Generate(kServerId, kDeviceId, local_comm_res, kDevicePort), ge::SUCCESS);

  const auto device = GetOnlyDevice(local_comm_res);
  EXPECT_EQ(device.at("device_port").get<std::string>(), std::to_string(kDevicePort));
}

TEST_F(RankTableGeneratorUnitTest, GenerateV1LocalCommResWritesConfiguredDevicePort) {
  AclRuntimeStub::SetInstance(std::make_shared<AutoCommResV1RuntimeMock>());

  std::string local_comm_res;
  ASSERT_EQ(LocalCommResGenerator::Generate(kServerId, kDeviceId, local_comm_res, kDevicePort), ge::SUCCESS);

  const auto device = GetOnlyDevice(local_comm_res);
  EXPECT_EQ(device.at("device_port").get<std::string>(), std::to_string(kDevicePort));
}

TEST_F(RankTableGeneratorUnitTest, GenerateLocalCommResOmitsDevicePortWhenUnset) {
  std::string local_comm_res;
  ASSERT_EQ(LocalCommResGenerator::Generate(kServerId, kDeviceId, local_comm_res), ge::SUCCESS);

  const auto device = GetOnlyDevice(local_comm_res);
  EXPECT_FALSE(device.contains("device_port"));
}

TEST_F(RankTableGeneratorUnitTest, MergeRankTableKeepsDevicePorts) {
  const std::string local_comm_res = R"({
      "version": "1.2",
      "server_list": [
        {"server_id": "node_0", "device": [
          {"device_id": "0", "super_device_id": "0", "device_ip": "1.1.1.0", "device_port": "20000"}
        ]}
      ]
    })";
  const std::string peer_comm_res = R"({
      "version": "1.2",
      "server_list": [
        {"server_id": "node_1", "device": [
          {"device_id": "1", "super_device_id": "1", "device_ip": "1.1.1.1", "device_port": "20001"}
        ]}
      ]
    })";

  auto generator = RankTableGeneratorFactory::Create(local_comm_res, peer_comm_res);
  ASSERT_NE(generator, nullptr);

  std::string rank_table;
  ASSERT_EQ(generator->Generate(kDeviceId, rank_table), ge::SUCCESS);

  const auto device_ports = CollectDevicePorts(nlohmann::json::parse(rank_table));
  EXPECT_EQ(device_ports.count("20000"), 1U);
  EXPECT_EQ(device_ports.count("20001"), 1U);
}

TEST_F(RankTableGeneratorUnitTest, CreateRejectsOversizedLocalCommRes) {
  const std::string valid_comm_res = R"({
      "version": "1.2",
      "server_list": [
        {"server_id": "node_1", "device": [
          {"device_id": "1", "super_device_id": "1", "device_ip": "1.1.1.1", "device_port": "20001"}
        ]}
      ]
    })";
  const std::string oversized_comm_res(rank_table_json::kMaxCommResSizeInBytes + 1U, ' ');

  EXPECT_EQ(RankTableGeneratorFactory::Create(oversized_comm_res, valid_comm_res), nullptr);
}

TEST_F(RankTableGeneratorUnitTest, CreateRejectsOversizedPeerCommRes) {
  const std::string valid_comm_res = R"({
      "version": "1.2",
      "server_list": [
        {"server_id": "node_0", "device": [
          {"device_id": "0", "super_device_id": "0", "device_ip": "1.1.1.0", "device_port": "20000"}
        ]}
      ]
    })";
  const std::string oversized_comm_res(rank_table_json::kMaxCommResSizeInBytes + 1U, ' ');

  EXPECT_EQ(RankTableGeneratorFactory::Create(valid_comm_res, oversized_comm_res), nullptr);
}

TEST_F(RankTableGeneratorUnitTest, GenerateV2RejectsOversizedPeerCommRes) {
  const std::string local_comm_res = R"({
      "version": "1.2",
      "server_list": [
        {"server_id": "node_0", "device": [
          {"device_id": "0", "super_device_id": "0", "device_ip": "1.1.1.0", "device_port": "20000"}
        ]}
      ]
    })";
  const std::string oversized_comm_res(rank_table_json::kMaxCommResSizeInBytes + 1U, ' ');

  RankTableGeneratorV2 generator(local_comm_res, oversized_comm_res);
  std::string rank_table;
  EXPECT_EQ(generator.Generate(kDeviceId, rank_table), ge::LLM_PARAM_INVALID);
}

TEST_F(RankTableGeneratorUnitTest, GenerateV1RejectsOversizedPeerCommRes) {
  AclRuntimeStub::SetInstance(std::make_shared<AutoCommResV1RuntimeMock>());

  const std::string local_comm_res = R"({
      "version": "1.0",
      "server_list": [
        {"server_id": "node_0", "device": [
          {"device_id": "0", "device_ip": "1.1.1.0", "device_port": "20000"}
        ]}
      ]
    })";
  const std::string oversized_comm_res(rank_table_json::kMaxCommResSizeInBytes + 1U, ' ');

  RankTableGeneratorV1 generator(local_comm_res, oversized_comm_res);
  std::string rank_table;
  EXPECT_EQ(generator.Generate(kDeviceId, rank_table), ge::LLM_PARAM_INVALID);
}
}  // namespace llm
