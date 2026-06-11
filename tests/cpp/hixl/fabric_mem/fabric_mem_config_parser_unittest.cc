/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <map>
#include <string>

#include <gtest/gtest.h>

#include "engine/hixl_options.h"
#include "hixl/hixl_types.h"

namespace hixl {
namespace {
std::map<AscendString, AscendString> MakeOptions() {
  std::map<AscendString, AscendString> options;
  options[OPTION_ENABLE_USE_FABRIC_MEM] = AscendString("1");
  return options;
}

std::map<AscendString, AscendString> MakeOptionsWithJson(const std::string &json) {
  auto options = MakeOptions();
  options[OPTION_GLOBAL_RESOURCE_CONFIG] = AscendString(json.c_str());
  return options;
}
}  // namespace

class FabricMemConfigParserUTest : public ::testing::Test {};

TEST_F(FabricMemConfigParserUTest, DisabledByDefaultWhenOptionMissing) {
  std::map<AscendString, AscendString> options;
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
  EXPECT_FALSE(result.EnableFabricMem().value_or(false));
}

TEST_F(FabricMemConfigParserUTest, DisabledWhenEnableOptionEmpty) {
  std::map<AscendString, AscendString> options;
  options[OPTION_ENABLE_USE_FABRIC_MEM] = AscendString("");
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
  EXPECT_FALSE(result.EnableFabricMem().value_or(false));
}

TEST_F(FabricMemConfigParserUTest, DisabledWhenEnableOptionZero) {
  std::map<AscendString, AscendString> options;
  options[OPTION_ENABLE_USE_FABRIC_MEM] = AscendString("0");
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
  EXPECT_FALSE(result.EnableFabricMem().value_or(false));
}

TEST_F(FabricMemConfigParserUTest, EnableRejectsNonBinaryValue) {
  std::map<AscendString, AscendString> options;
  options[OPTION_ENABLE_USE_FABRIC_MEM] = AscendString("2");
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(FabricMemConfigParserUTest, EnableRejectsNonNumericValue) {
  std::map<AscendString, AscendString> options;
  options[OPTION_ENABLE_USE_FABRIC_MEM] = AscendString("abc");
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(FabricMemConfigParserUTest, AllFieldsParsedCorrectly) {
  const std::string json = R"({
    "fabric_memory": {
      "max_capacity": 64,
      "start_address": 100,
      "task_stream_num": 4
    }
  })";
  auto options = MakeOptionsWithJson(json);
  options[OPTION_AUTO_CONNECT] = AscendString("1");

  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
  EXPECT_TRUE(result.EnableFabricMem().value_or(false));
  EXPECT_TRUE(result.AutoConnect().value_or(false));
  auto grc = result.GlobalResourceCfg();
  ASSERT_TRUE(grc.has_value());
  EXPECT_EQ(grc->fabric_memory.max_capacity.value(), 64UL);
  EXPECT_EQ(grc->fabric_memory.start_address.value(), 100UL);
  EXPECT_EQ(grc->fabric_memory.task_stream_num.value(), 4U);
}

TEST_F(FabricMemConfigParserUTest, InvalidJsonReturnsError) {
  auto options = MakeOptionsWithJson("{invalid json");
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(FabricMemConfigParserUTest, JsonArrayReturnsError) {
  auto options = MakeOptionsWithJson("[1, 2, 3]");
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(FabricMemConfigParserUTest, CapacityZeroRejected) {
  auto options = MakeOptionsWithJson(R"({"fabric_memory": {"max_capacity": 0}})");
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(FabricMemConfigParserUTest, CapacityBoundaryMinAccepted) {
  auto options = MakeOptionsWithJson(R"({"fabric_memory": {"max_capacity": 1}})");
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
  auto grc = result.GlobalResourceCfg();
  ASSERT_TRUE(grc.has_value());
  EXPECT_EQ(grc->fabric_memory.max_capacity.value(), 1UL);
}

TEST_F(FabricMemConfigParserUTest, CapacityBoundaryMaxAccepted) {
  auto options = MakeOptionsWithJson(R"({"fabric_memory": {"max_capacity": 1024}})");
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
  auto grc = result.GlobalResourceCfg();
  ASSERT_TRUE(grc.has_value());
  EXPECT_EQ(grc->fabric_memory.max_capacity.value(), 1024UL);
}

TEST_F(FabricMemConfigParserUTest, CapacityAboveMaxRejected) {
  auto options = MakeOptionsWithJson(R"({"fabric_memory": {"max_capacity": 1025}})");
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(FabricMemConfigParserUTest, CapacityNonNumericRejected) {
  auto options = MakeOptionsWithJson(R"({"fabric_memory": {"max_capacity": "abc"}})");
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(FabricMemConfigParserUTest, StartAddressBelowMinRejected) {
  auto options = MakeOptionsWithJson(R"({"fabric_memory": {"start_address": 39}})");
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(FabricMemConfigParserUTest, StartAddressBoundaryMinAccepted) {
  auto options = MakeOptionsWithJson(R"({"fabric_memory": {"start_address": 40}})");
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
  auto grc = result.GlobalResourceCfg();
  ASSERT_TRUE(grc.has_value());
  EXPECT_EQ(grc->fabric_memory.start_address.value(), 40UL);
}

TEST_F(FabricMemConfigParserUTest, StartAddressBoundaryMaxAccepted) {
  auto options = MakeOptionsWithJson(R"({"fabric_memory": {"start_address": 220}})");
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
  auto grc = result.GlobalResourceCfg();
  ASSERT_TRUE(grc.has_value());
  EXPECT_EQ(grc->fabric_memory.start_address.value(), 220UL);
}

TEST_F(FabricMemConfigParserUTest, StartAddressAboveMaxRejected) {
  auto options = MakeOptionsWithJson(R"({"fabric_memory": {"start_address": 221}})");
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(FabricMemConfigParserUTest, TaskStreamNumZeroRejected) {
  auto options = MakeOptionsWithJson(R"({"fabric_memory": {"task_stream_num": 0}})");
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(FabricMemConfigParserUTest, TaskStreamNumBoundaryMinMaxAccepted) {
  auto options_min = MakeOptionsWithJson(R"({"fabric_memory": {"task_stream_num": 1}})");
  HixlOptions result_min;
  EXPECT_EQ(HixlOptions::Parse(options_min, result_min), SUCCESS);
  EXPECT_EQ(result_min.GlobalResourceCfg()->fabric_memory.task_stream_num.value(), 1U);

  auto options_max = MakeOptionsWithJson(R"({"fabric_memory": {"task_stream_num": 8}})");
  HixlOptions result_max;
  EXPECT_EQ(HixlOptions::Parse(options_max, result_max), SUCCESS);
  EXPECT_EQ(result_max.GlobalResourceCfg()->fabric_memory.task_stream_num.value(), 8U);
}

TEST_F(FabricMemConfigParserUTest, TaskStreamNumAboveMaxRejected) {
  auto options = MakeOptionsWithJson(R"({"fabric_memory": {"task_stream_num": 9}})");
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(FabricMemConfigParserUTest, MissingSubFieldsKeepDefaults) {
  auto options = MakeOptionsWithJson(R"({"fabric_memory": {}})");
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
  EXPECT_TRUE(result.EnableFabricMem().value_or(false));
  auto grc = result.GlobalResourceCfg();
  ASSERT_TRUE(grc.has_value());
  EXPECT_FALSE(grc->fabric_memory.max_capacity.has_value());
  EXPECT_FALSE(grc->fabric_memory.start_address.has_value());
  EXPECT_FALSE(grc->fabric_memory.task_stream_num.has_value());
}

TEST_F(FabricMemConfigParserUTest, NonFabricMemoryJsonKeysPassThrough) {
  auto options = MakeOptionsWithJson(R"({"other_group": {"key": "value"}, "plain_key": 42})");
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
  auto grc = result.GlobalResourceCfg();
  ASSERT_TRUE(grc.has_value());
  EXPECT_FALSE(grc->fabric_memory.max_capacity.has_value());
  EXPECT_FALSE(grc->fabric_memory.start_address.has_value());
  EXPECT_FALSE(grc->fabric_memory.task_stream_num.has_value());
}

TEST_F(FabricMemConfigParserUTest, AutoConnectEmptyValueRejected) {
  auto options = MakeOptions();
  options[OPTION_AUTO_CONNECT] = AscendString("");
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(FabricMemConfigParserUTest, AutoConnectZeroAndOneAccepted) {
  auto options_zero = MakeOptions();
  options_zero[OPTION_AUTO_CONNECT] = AscendString("0");
  HixlOptions result_zero;
  EXPECT_EQ(HixlOptions::Parse(options_zero, result_zero), SUCCESS);
  EXPECT_FALSE(result_zero.AutoConnect().value_or(true));

  auto options_one = MakeOptions();
  options_one[OPTION_AUTO_CONNECT] = AscendString("1");
  HixlOptions result_one;
  EXPECT_EQ(HixlOptions::Parse(options_one, result_one), SUCCESS);
  EXPECT_TRUE(result_one.AutoConnect().value_or(false));
}

TEST_F(FabricMemConfigParserUTest, AutoConnectNonBinaryRejected) {
  auto options = MakeOptions();
  options[OPTION_AUTO_CONNECT] = AscendString("2");
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(FabricMemConfigParserUTest, AutoConnectNonNumericRejected) {
  auto options = MakeOptions();
  options[OPTION_AUTO_CONNECT] = AscendString("abc");
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(FabricMemConfigParserUTest, EnabledSkipsParsingWhenDisabled) {
  std::map<AscendString, AscendString> options;
  options[OPTION_ENABLE_USE_FABRIC_MEM] = AscendString("0");
  options[OPTION_AUTO_CONNECT] = AscendString("invalid_should_fail_if_parsed");
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

}  // namespace hixl
