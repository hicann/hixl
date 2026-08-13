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
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <gtest/gtest.h>
#include "engine/hixl_options.h"
#include "hixl/hixl_types.h"
#include "adxl/adxl_types.h"
#include "slog_stub.h"
#include "test_mmpa_utils.h"

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

TEST_F(HixlOptionsUTest, ParseAutoConnectFalse) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_AUTO_CONNECT] = "0";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
  ASSERT_TRUE(result.AutoConnect().has_value());
  EXPECT_FALSE(*result.AutoConnect());
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
      R"({"fabric_memory":{"max_capacity":"10","start_address":"50","task_stream_num":"4","enable_aicpu_unfold":false}})";
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
  ASSERT_TRUE(grc.fabric_memory.enable_aicpu_unfold.has_value());
  EXPECT_FALSE(*grc.fabric_memory.enable_aicpu_unfold);
}

TEST_F(HixlOptionsUTest, ParseGlobalResourceConfigDefaultAicpuUnfoldRejectsNonOneTaskStreamNum) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"fabric_memory":{"task_stream_num":"4"}})";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(HixlOptionsUTest, ParseGlobalResourceConfigFabricMemoryAicpuUnfold) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"fabric_memory":{"enable_aicpu_unfold":true}})";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
  ASSERT_TRUE(result.GlobalResourceCfg().has_value());
  ASSERT_TRUE(result.GlobalResourceCfg()->fabric_memory.enable_aicpu_unfold.has_value());
  EXPECT_TRUE(*result.GlobalResourceCfg()->fabric_memory.enable_aicpu_unfold);
}

TEST_F(HixlOptionsUTest, ParseGlobalResourceConfigFabricMemoryAicpuUnfoldRejectsNonBoolean) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"fabric_memory.enable_aicpu_unfold":"true"})";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(HixlOptionsUTest, ParseGlobalResourceConfigAicpuUnfoldRejectsNonOneTaskStreamNum) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] =
      R"({"fabric_memory":{"enable_aicpu_unfold":true,"task_stream_num":"4"}})";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(HixlOptionsUTest, ParseGlobalResourceConfigAicpuUnfoldAcceptsTaskStreamNumOne) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] =
      R"({"fabric_memory":{"enable_aicpu_unfold":true,"task_stream_num":"1"}})";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
  ASSERT_TRUE(result.GlobalResourceCfg().has_value());
  EXPECT_TRUE(*result.GlobalResourceCfg()->fabric_memory.enable_aicpu_unfold);
  EXPECT_EQ(*result.GlobalResourceCfg()->fabric_memory.task_stream_num, 1U);
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

TEST_F(HixlOptionsUTest, ParseGlobalResourceConfigConnectPoolThreadNumMinBoundary) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"connect_pool.thread_num":"1"})";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
  ASSERT_TRUE(result.GlobalResourceCfg().has_value());
  EXPECT_EQ(*result.GlobalResourceCfg()->connect_pool.thread_num, 1);
}

TEST_F(HixlOptionsUTest, ParseGlobalResourceConfigConnectPoolThreadNumMaxBoundary) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"connect_pool.thread_num":"64"})";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
  ASSERT_TRUE(result.GlobalResourceCfg().has_value());
  EXPECT_EQ(*result.GlobalResourceCfg()->connect_pool.thread_num, 64);
}

TEST_F(HixlOptionsUTest, ParseGlobalResourceConfigConnectPoolThreadNumZeroInvalid) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"connect_pool.thread_num":"0"})";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(HixlOptionsUTest, ParseGlobalResourceConfigConnectPoolThreadNumAboveMaxInvalid) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"connect_pool.thread_num":"65"})";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(HixlOptionsUTest, ParseGlobalResourceConfigConnectPoolThreadNumNegativeInvalid) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"connect_pool.thread_num":"-1"})";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(HixlOptionsUTest, ParseGlobalResourceConfigConnectPoolTaskQueueCapacityMinBoundary) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"connect_pool.task_queue_capacity":"1"})";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
  ASSERT_TRUE(result.GlobalResourceCfg().has_value());
  EXPECT_EQ(*result.GlobalResourceCfg()->connect_pool.task_queue_capacity, 1);
}

TEST_F(HixlOptionsUTest, ParseGlobalResourceConfigConnectPoolTaskQueueCapacityMaxBoundary) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"connect_pool.task_queue_capacity":"65535"})";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
  ASSERT_TRUE(result.GlobalResourceCfg().has_value());
  EXPECT_EQ(*result.GlobalResourceCfg()->connect_pool.task_queue_capacity, 65535);
}

TEST_F(HixlOptionsUTest, ParseGlobalResourceConfigConnectPoolTaskQueueCapacityZeroInvalid) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"connect_pool.task_queue_capacity":"0"})";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(HixlOptionsUTest, ParseGlobalResourceConfigConnectPoolTaskQueueCapacityAboveMaxInvalid) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"connect_pool.task_queue_capacity":"65536"})";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(HixlOptionsUTest, ParseGlobalResourceConfigConnectPoolTaskQueueCapacityNegativeInvalid) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"connect_pool.task_queue_capacity":"-1"})";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(HixlOptionsUTest, ParseGlobalResourceConfigConnectPoolThreadNumNumericType) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"connect_pool.thread_num":4})";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
  ASSERT_TRUE(result.GlobalResourceCfg().has_value());
  EXPECT_EQ(*result.GlobalResourceCfg()->connect_pool.thread_num, 4);
}

TEST_F(HixlOptionsUTest, ParseGlobalResourceConfigConnectPoolThreadNumTypeInvalid) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"connect_pool.thread_num":"invalid"})";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(HixlOptionsUTest, ParseGlobalResourceConfigConnectPoolTaskQueueCapacityNumericType) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"connect_pool.task_queue_capacity":256})";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
  ASSERT_TRUE(result.GlobalResourceCfg().has_value());
  EXPECT_EQ(*result.GlobalResourceCfg()->connect_pool.task_queue_capacity, 256);
}

TEST_F(HixlOptionsUTest, ParseGlobalResourceConfigConnectPoolTaskQueueCapacityTypeInvalid) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"connect_pool.task_queue_capacity":"invalid"})";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(HixlOptionsUTest, ParseGlobalResourceConfigProtocolDesc) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"comm_resource_config.protocol_desc":["uboe:device"]})";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
  ASSERT_TRUE(result.GlobalResourceCfg().has_value());
  auto grc = *result.GlobalResourceCfg();
  ASSERT_TRUE(grc.comm_resource_config.protocol_desc.has_value());
  EXPECT_EQ(grc.comm_resource_config.protocol_desc->size(), 1U);
  EXPECT_EQ((*grc.comm_resource_config.protocol_desc)[0], "uboe:device");
  ASSERT_EQ(result.GetProtocolDesc().size(), 1U);
  EXPECT_EQ(result.GetProtocolDesc()[0], "uboe:device");
}

TEST_F(HixlOptionsUTest, ParseGlobalResourceConfigProtocolDescString) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"comm_resource_config.protocol_desc":"roce:device"})";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
  ASSERT_TRUE(result.GlobalResourceCfg().has_value());
  auto grc = *result.GlobalResourceCfg();
  ASSERT_TRUE(grc.comm_resource_config.protocol_desc.has_value());
  ASSERT_EQ(grc.comm_resource_config.protocol_desc->size(), 1U);
  EXPECT_EQ((*grc.comm_resource_config.protocol_desc)[0], "roce:device");
  ASSERT_EQ(result.GetProtocolDesc().size(), 1U);
  EXPECT_EQ(result.GetProtocolDesc()[0], "roce:device");
}

TEST_F(HixlOptionsUTest, ParseGlobalResourceConfigListenPort) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"comm_resource_config.listen_port":26300})";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
  ASSERT_TRUE(result.GlobalResourceCfg().has_value());
  auto grc = *result.GlobalResourceCfg();
  ASSERT_TRUE(grc.comm_resource_config.listen_port.has_value());
  EXPECT_EQ(*grc.comm_resource_config.listen_port, 26300U);
}

TEST_F(HixlOptionsUTest, ParseGlobalResourceConfigListenPortString) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"comm_resource_config.listen_port":"26300"})";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
  ASSERT_TRUE(result.GlobalResourceCfg().has_value());
  auto grc = *result.GlobalResourceCfg();
  ASSERT_TRUE(grc.comm_resource_config.listen_port.has_value());
  EXPECT_EQ(*grc.comm_resource_config.listen_port, 26300U);
}

TEST_F(HixlOptionsUTest, ParseGlobalResourceConfigMaxActiveChannels) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"comm_resource_config.max_active_channels":8192})";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
  ASSERT_TRUE(result.GlobalResourceCfg().has_value());
  auto grc = *result.GlobalResourceCfg();
  ASSERT_TRUE(grc.comm_resource_config.max_active_channels.has_value());
  EXPECT_EQ(*grc.comm_resource_config.max_active_channels, 8192U);
}

TEST_F(HixlOptionsUTest, ParseGlobalResourceConfigMaxActiveChannelsString) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"comm_resource_config.max_active_channels":"8192"})";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
  ASSERT_TRUE(result.GlobalResourceCfg().has_value());
  auto grc = *result.GlobalResourceCfg();
  ASSERT_TRUE(grc.comm_resource_config.max_active_channels.has_value());
  EXPECT_EQ(*grc.comm_resource_config.max_active_channels, 8192U);
}

TEST_F(HixlOptionsUTest, GetProtocolDescReturnsEmptyWhenNotConfigured) {
  std::map<AscendString, AscendString> options;
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
  EXPECT_TRUE(result.GetProtocolDesc().empty());
}

TEST_F(HixlOptionsUTest, ParseGlobalResourceConfigProtocolDescInvalidType) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"comm_resource_config.protocol_desc":123})";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(HixlOptionsUTest, ParseGlobalResourceConfigListenPortZeroInvalid) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"comm_resource_config.listen_port":0})";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(HixlOptionsUTest, ParseGlobalResourceConfigListenPortOutOfRangeInvalid) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"comm_resource_config.listen_port":65536})";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(HixlOptionsUTest, ParseGlobalResourceConfigListenPortNegativeInvalid) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"comm_resource_config.listen_port":-1})";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(HixlOptionsUTest, ParseGlobalResourceConfigListenPortNegativeStringInvalid) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"comm_resource_config.listen_port":"-1"})";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(HixlOptionsUTest, ParseGlobalResourceConfigListenPortTypeInvalid) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"comm_resource_config.listen_port":"invalid"})";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(HixlOptionsUTest, ParseGlobalResourceConfigMaxActiveChannelsZeroInvalid) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"comm_resource_config.max_active_channels":0})";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(HixlOptionsUTest, ParseGlobalResourceConfigMaxActiveChannelsNegativeInvalid) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"comm_resource_config.max_active_channels":-1})";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(HixlOptionsUTest, ParseGlobalResourceConfigMaxActiveChannelsOutOfUint32RangeInvalid) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"comm_resource_config.max_active_channels":4294967296})";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(HixlOptionsUTest, ParseGlobalResourceConfigMaxActiveChannelsTypeInvalid) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"comm_resource_config.max_active_channels":"invalid"})";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
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
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"fabric_memory":{"max_capacity":"2048"}})";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(HixlOptionsUTest, ParseFabricMemStartAddressOutOfRange) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"fabric_memory":{"start_address":"1025"}})";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(HixlOptionsUTest, ParseFabricMemTaskStreamNumOutOfRange) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"fabric_memory":{"task_stream_num":"16"}})";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(HixlOptionsUTest, CheckSupportedOptionsAllSupported) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_RDMA_TRAFFIC_CLASS] = "132";
  options[hixl::OPTION_LOCAL_COMM_RES] = "{}";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
  std::unordered_set<std::string> whitelist = {hixl::OPTION_RDMA_TRAFFIC_CLASS, hixl::OPTION_LOCAL_COMM_RES};
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

TEST_F(HixlOptionsUTest, ParseConfigQosDefault) {
  std::map<ge::AscendString, ge::AscendString> options;

  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
  EXPECT_FALSE(result.GlobalResourceCfg().has_value());
}

TEST_F(HixlOptionsUTest, ParseConfigQosMin) {
  std::map<ge::AscendString, ge::AscendString> options;
  std::string json_str = R"({"comm_resource_config.qos": 0})";
  options[ge::AscendString(hixl::OPTION_GLOBAL_RESOURCE_CONFIG)] = ge::AscendString(json_str.c_str());

  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
  EXPECT_TRUE(result.GlobalResourceCfg().has_value());
  EXPECT_EQ(static_cast<uint32_t>(result.GlobalResourceCfg()->comm_resource_config.qos.value()), 0U);
}

TEST_F(HixlOptionsUTest, ParseConfigQosMax) {
  std::map<ge::AscendString, ge::AscendString> options;
  std::string json_str = R"({"comm_resource_config.qos": 7})";
  options[ge::AscendString(hixl::OPTION_GLOBAL_RESOURCE_CONFIG)] = ge::AscendString(json_str.c_str());

  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
  EXPECT_TRUE(result.GlobalResourceCfg().has_value());
  EXPECT_EQ(static_cast<uint32_t>(result.GlobalResourceCfg()->comm_resource_config.qos.value()), 7U);
}

TEST_F(HixlOptionsUTest, ParseConfigQosInValidMin) {
  std::map<ge::AscendString, ge::AscendString> options;
  std::string json_str = R"({"comm_resource_config.qos": -1})";
  options[ge::AscendString(hixl::OPTION_GLOBAL_RESOURCE_CONFIG)] = ge::AscendString(json_str.c_str());

  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(HixlOptionsUTest, ParseConfigQosInValidMax) {
  std::map<ge::AscendString, ge::AscendString> options;
  std::string json_str = R"({"comm_resource_config.qos": 8})";
  options[ge::AscendString(hixl::OPTION_GLOBAL_RESOURCE_CONFIG)] = ge::AscendString(json_str.c_str());

  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(HixlOptionsUTest, ParseConfigQosInValidInt8) {
  std::map<ge::AscendString, ge::AscendString> options;
  std::string json_str = R"({"comm_resource_config.qos": 8888})";
  options[ge::AscendString(hixl::OPTION_GLOBAL_RESOURCE_CONFIG)] = ge::AscendString(json_str.c_str());

  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(HixlOptionsUTest, ParseConfigQosInValidPartString) {
  std::map<ge::AscendString, ge::AscendString> options;
  std::string json_str = R"({"comm_resource_config.qos": "0invalid"})";
  options[ge::AscendString(hixl::OPTION_GLOBAL_RESOURCE_CONFIG)] = ge::AscendString(json_str.c_str());

  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(HixlOptionsUTest, ParseConfigQosInValidAllString) {
  std::map<ge::AscendString, ge::AscendString> options;
  std::string json_str = R"({"comm_resource_config.qos": "invalid"})";
  options[ge::AscendString(hixl::OPTION_GLOBAL_RESOURCE_CONFIG)] = ge::AscendString(json_str.c_str());

  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(HixlOptionsUTest, ParseLocalCommResFromFilePath) {
  const std::string content = R"({"version":"1.3","net_instance_id":"from_file"})";
  const std::string file_path = test::CreateTempFileWithContent("/tmp/hixl_lcr_XXXXXX", content);
  ASSERT_FALSE(file_path.empty());

  const std::string grc = std::string(R"({"local_comm_res_path":")") + file_path + "\"}";
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = AscendString(grc.c_str());
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
  ASSERT_TRUE(result.LocalCommRes().has_value());
  EXPECT_EQ(*result.LocalCommRes(), content);
  ASSERT_TRUE(result.GlobalResourceCfg().has_value());
  ASSERT_TRUE(result.GlobalResourceCfg()->local_comm_res_path.has_value());
  EXPECT_EQ(*result.GlobalResourceCfg()->local_comm_res_path, file_path);
  unlink(file_path.c_str());
}

TEST_F(HixlOptionsUTest, ParseLocalCommResOptionTakesPrecedenceOverFilePath) {
  const std::string option_content = R"({"version":"1.3","net_instance_id":"from_option"})";
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_LOCAL_COMM_RES] = AscendString(option_content.c_str());
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"local_comm_res_path":"/tmp/hixl_lcr_ignored.json"})";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
  ASSERT_TRUE(result.LocalCommRes().has_value());
  EXPECT_EQ(*result.LocalCommRes(), option_content);
}

TEST_F(HixlOptionsUTest, ParseEmptyLocalCommResOptionFromFilePath) {
  const std::string content = R"({"version":"1.3","net_instance_id":"from_file"})";
  const std::string file_path = test::CreateTempFileWithContent("/tmp/hixl_lcr_XXXXXX", content);
  ASSERT_FALSE(file_path.empty());

  const std::string grc = std::string(R"({"local_comm_res_path":")") + file_path + "\"}";
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_LOCAL_COMM_RES] = "";
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = AscendString(grc.c_str());
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
  ASSERT_TRUE(result.LocalCommRes().has_value());
  EXPECT_EQ(*result.LocalCommRes(), content);
  unlink(file_path.c_str());
}

TEST_F(HixlOptionsUTest, ParseAdxlLocalCommResOptionTakesPrecedenceOverFilePath) {
  const std::string option_content = R"({"version":"1.3","net_instance_id":"from_adxl_option"})";
  std::map<AscendString, AscendString> options;
  options[adxl::OPTION_LOCAL_COMM_RES] = AscendString(option_content.c_str());
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"local_comm_res_path":"/tmp/hixl_lcr_ignored.json"})";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
  ASSERT_TRUE(result.LocalCommRes().has_value());
  EXPECT_EQ(*result.LocalCommRes(), option_content);
}

TEST_F(HixlOptionsUTest, ParseLocalCommResFilePathNotExistInvalid) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"local_comm_res_path":"/tmp/hixl_lcr_not_exist_12345.json"})";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(HixlOptionsUTest, ParseLocalCommResFilePathEmptyInvalid) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"local_comm_res_path":""})";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(HixlOptionsUTest, ParseLocalCommResFilePathTypeInvalid) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"local_comm_res_path":123})";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(HixlOptionsUTest, ParseLocalCommResDirectoryPathInvalid) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"local_comm_res_path":"/tmp"})";
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(HixlOptionsUTest, ParseLocalCommResFifoPathInvalid) {
  const std::string fifo_path = test::CreateTempFileWithContent("/tmp/hixl_lcr_fifo_XXXXXX", "");
  ASSERT_FALSE(fifo_path.empty());
  ASSERT_EQ(unlink(fifo_path.c_str()), 0);
  ASSERT_EQ(mkfifo(fifo_path.c_str(), S_IRUSR | S_IWUSR), 0);

  const std::string grc = std::string(R"({"local_comm_res_path":")") + fifo_path + "\"}";
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = AscendString(grc.c_str());
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
  unlink(fifo_path.c_str());
}

TEST_F(HixlOptionsUTest, ParseLocalCommResEmptyFileInvalid) {
  const std::string file_path = test::CreateTempFileWithContent("/tmp/hixl_lcr_empty_XXXXXX", "");
  ASSERT_FALSE(file_path.empty());

  const std::string grc = std::string(R"({"local_comm_res_path":")") + file_path + "\"}";
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = AscendString(grc.c_str());
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
  unlink(file_path.c_str());
}

TEST_F(HixlOptionsUTest, ParseLocalCommResOversizedFileInvalid) {
  constexpr off_t kOversizedFileSize = 1024U * 1024U + 1U;
  const std::string file_path = test::CreateTempFileWithContent("/tmp/hixl_lcr_large_XXXXXX", "");
  ASSERT_FALSE(file_path.empty());
  ASSERT_EQ(truncate(file_path.c_str(), kOversizedFileSize), 0);

  const std::string grc = std::string(R"({"local_comm_res_path":")") + file_path + "\"}";
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = AscendString(grc.c_str());
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
  unlink(file_path.c_str());
}
}  // namespace hixl
