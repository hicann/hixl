/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <cstdlib>
#include <gtest/gtest.h>
#include "engine/hixl_options.h"
#include "hixl/hixl_types.h"
#include "adxl/adxl_types.h"
#include "slog_stub.h"

namespace hixl {

class HixlOptionsUTest : public ::testing::Test {
 protected:
  void SetUp() override {
    unsetenv("HCCL_RDMA_TC");
    unsetenv("HCCL_RDMA_SL");
  }

  void TearDown() override {
    unsetenv("HCCL_RDMA_TC");
    unsetenv("HCCL_RDMA_SL");
  }
};

TEST_F(HixlOptionsUTest, ParseEmptyOptions) {
  std::map<AscendString, AscendString> options;
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
  EXPECT_FALSE(result.RdmaTrafficClass().has_value());
  EXPECT_FALSE(result.RdmaServiceLevel().has_value());
  EXPECT_FALSE(result.LocalCommRes().has_value());
  EXPECT_FALSE(result.EnableFabricMem().has_value());
  EXPECT_FALSE(result.AutoConnect().has_value());
  EXPECT_FALSE(result.GlobalResourceCfg().has_value());
}

TEST_F(HixlOptionsUTest, ParseRdmaTrafficClassHixlPrefix) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_RDMA_TRAFFIC_CLASS] = "132";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
  ASSERT_TRUE(result.RdmaTrafficClass().has_value());
  EXPECT_EQ(*result.RdmaTrafficClass(), 132);
}

TEST_F(HixlOptionsUTest, ParseRdmaTrafficClassAdxlPrefix) {
  std::map<AscendString, AscendString> options;
  options[adxl::OPTION_RDMA_TRAFFIC_CLASS] = "128";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
  ASSERT_TRUE(result.RdmaTrafficClass().has_value());
  EXPECT_EQ(*result.RdmaTrafficClass(), 128);
}

TEST_F(HixlOptionsUTest, ParseRdmaTrafficClassHixlPrefixTakesPrecedence) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_RDMA_TRAFFIC_CLASS] = "132";
  options[adxl::OPTION_RDMA_TRAFFIC_CLASS] = "128";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
  ASSERT_TRUE(result.RdmaTrafficClass().has_value());
  EXPECT_EQ(*result.RdmaTrafficClass(), 132);
}

TEST_F(HixlOptionsUTest, ParseRdmaTrafficClassFromEnv) {
  setenv("HCCL_RDMA_TC", "136", 1);
  std::map<AscendString, AscendString> options;
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
  ASSERT_TRUE(result.RdmaTrafficClass().has_value());
  EXPECT_EQ(*result.RdmaTrafficClass(), 136);
}

TEST_F(HixlOptionsUTest, ParseRdmaTrafficClassOptionOverridesEnv) {
  setenv("HCCL_RDMA_TC", "136", 1);
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_RDMA_TRAFFIC_CLASS] = "132";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
  ASSERT_TRUE(result.RdmaTrafficClass().has_value());
  EXPECT_EQ(*result.RdmaTrafficClass(), 132);
}

TEST_F(HixlOptionsUTest, ParseRdmaTrafficClassInvalidValue) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_RDMA_TRAFFIC_CLASS] = "256";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(HixlOptionsUTest, ParseRdmaTrafficClassNotMultipleOf4) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_RDMA_TRAFFIC_CLASS] = "130";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(HixlOptionsUTest, ParseRdmaServiceLevelHixlPrefix) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_RDMA_SERVICE_LEVEL] = "4";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
  ASSERT_TRUE(result.RdmaServiceLevel().has_value());
  EXPECT_EQ(*result.RdmaServiceLevel(), 4);
}

TEST_F(HixlOptionsUTest, ParseRdmaServiceLevelFromEnv) {
  setenv("HCCL_RDMA_SL", "7", 1);
  std::map<AscendString, AscendString> options;
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
  ASSERT_TRUE(result.RdmaServiceLevel().has_value());
  EXPECT_EQ(*result.RdmaServiceLevel(), 7);
}

TEST_F(HixlOptionsUTest, ParseRdmaServiceLevelInvalidValue) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_RDMA_SERVICE_LEVEL] = "8";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(HixlOptionsUTest, ParseLocalCommResHixlPrefix) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_LOCAL_COMM_RES] = R"({"version":"1.3"})";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
  ASSERT_TRUE(result.LocalCommRes().has_value());
  EXPECT_EQ(*result.LocalCommRes(), R"({"version":"1.3"})");
}

TEST_F(HixlOptionsUTest, ParseLocalCommResAdxlPrefix) {
  std::map<AscendString, AscendString> options;
  options[adxl::OPTION_LOCAL_COMM_RES] = R"({"version":"1.2"})";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
  ASSERT_TRUE(result.LocalCommRes().has_value());
  EXPECT_EQ(*result.LocalCommRes(), R"({"version":"1.2"})");
}

TEST_F(HixlOptionsUTest, ParseBufferPool) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_BUFFER_POOL] = "0:0";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
}

TEST_F(HixlOptionsUTest, ParseBufferPoolNonZero) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_BUFFER_POOL] = "4:8";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
}

TEST_F(HixlOptionsUTest, ParseEnableFabricMemTrue) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_ENABLE_USE_FABRIC_MEM] = "1";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
  ASSERT_TRUE(result.EnableFabricMem().has_value());
  EXPECT_TRUE(*result.EnableFabricMem());
}

TEST_F(HixlOptionsUTest, ParseEnableFabricMemFalse) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_ENABLE_USE_FABRIC_MEM] = "0";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
  ASSERT_TRUE(result.EnableFabricMem().has_value());
  EXPECT_FALSE(*result.EnableFabricMem());
}

TEST_F(HixlOptionsUTest, ParseEnableFabricMemInvalid) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_ENABLE_USE_FABRIC_MEM] = "2";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(HixlOptionsUTest, ParseAutoConnectTrue) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_AUTO_CONNECT] = "1";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
  ASSERT_TRUE(result.AutoConnect().has_value());
  EXPECT_TRUE(*result.AutoConnect());
}

TEST_F(HixlOptionsUTest, ParseAutoConnectEmpty) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_AUTO_CONNECT] = "";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(HixlOptionsUTest, ParseGlobalResourceConfigFabricMemory) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] =
      R"({"fabric_memory":{"max_capacity":"10","start_address":"50","task_stream_num":"4"}})";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
  ASSERT_TRUE(result.GlobalResourceCfg().has_value());
  auto grc = *result.GlobalResourceCfg();
  ASSERT_TRUE(grc.fabric_memory.max_capacity.has_value());
  EXPECT_EQ(*grc.fabric_memory.max_capacity, 10U);
  ASSERT_TRUE(grc.fabric_memory.start_address.has_value());
  EXPECT_EQ(*grc.fabric_memory.start_address, 50U);
  ASSERT_TRUE(grc.fabric_memory.task_stream_num.has_value());
  EXPECT_EQ(*grc.fabric_memory.task_stream_num, 4U);
}

TEST_F(HixlOptionsUTest, ParseGlobalResourceConfigConnectPool) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] =
      R"({"connect_pool.thread_num":"4","connect_pool.task_queue_capacity":"256"})";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
  ASSERT_TRUE(result.GlobalResourceCfg().has_value());
  auto grc = *result.GlobalResourceCfg();
  ASSERT_TRUE(grc.connect_pool.thread_num.has_value());
  EXPECT_EQ(*grc.connect_pool.thread_num, 4);
  ASSERT_TRUE(grc.connect_pool.task_queue_capacity.has_value());
  EXPECT_EQ(*grc.connect_pool.task_queue_capacity, 256);
}

TEST_F(HixlOptionsUTest, ParseGlobalResourceConfigProtocolDesc) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] =
      R"({"comm_resource_config.protocol_desc":["uboe:device"]})";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
  ASSERT_TRUE(result.GlobalResourceCfg().has_value());
  auto grc = *result.GlobalResourceCfg();
  ASSERT_TRUE(grc.comm_resource_config.protocol_desc.has_value());
  EXPECT_EQ(grc.comm_resource_config.protocol_desc->size(), 1U);
  EXPECT_EQ((*grc.comm_resource_config.protocol_desc)[0], "uboe:device");
}

TEST_F(HixlOptionsUTest, ParseGlobalResourceConfigInvalidJson) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = "not json";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(HixlOptionsUTest, ParseGlobalResourceConfigNotObject) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = "[]";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(HixlOptionsUTest, ParseFabricMemCapacityOutOfRange) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] =
      R"({"fabric_memory":{"max_capacity":"2048"}})";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(HixlOptionsUTest, ParseFabricMemStartAddressOutOfRange) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] =
      R"({"fabric_memory":{"start_address":"250"}})";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(HixlOptionsUTest, ParseFabricMemTaskStreamNumOutOfRange) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] =
      R"({"fabric_memory":{"task_stream_num":"16"}})";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(HixlOptionsUTest, CheckSupportedOptionsAllSupported) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_RDMA_TRAFFIC_CLASS] = "132";
  options[hixl::OPTION_LOCAL_COMM_RES] = "{}";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
  std::unordered_set<std::string> whitelist = {
      hixl::OPTION_RDMA_TRAFFIC_CLASS, hixl::OPTION_LOCAL_COMM_RES};
  EXPECT_EQ(result.CheckSupportedOptions(whitelist), SUCCESS);
}

TEST_F(HixlOptionsUTest, CheckSupportedOptionsUnsupported) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_RDMA_TRAFFIC_CLASS] = "132";
  options[hixl::OPTION_ENABLE_USE_FABRIC_MEM] = "1";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
  std::unordered_set<std::string> whitelist = {hixl::OPTION_RDMA_TRAFFIC_CLASS};
  EXPECT_EQ(result.CheckSupportedOptions(whitelist), PARAM_INVALID);
}

TEST_F(HixlOptionsUTest, CheckSupportedOptionsEmpty) {
  std::map<AscendString, AscendString> options;
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
  std::unordered_set<std::string> whitelist = {hixl::OPTION_RDMA_TRAFFIC_CLASS};
  EXPECT_EQ(result.CheckSupportedOptions(whitelist), SUCCESS);
}

TEST_F(HixlOptionsUTest, RawOptionsPreserved) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_RDMA_TRAFFIC_CLASS] = "132";
  options[hixl::OPTION_LOCAL_COMM_RES] = "test_value";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
  EXPECT_EQ(result.RawOptions().size(), 2U);
  EXPECT_EQ(result.RawOptions().at(hixl::OPTION_RDMA_TRAFFIC_CLASS).GetString(), std::string("132"));
}

}  // namespace hixl
