/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <gtest/gtest.h>
#include "common/llm_utils.h"
#include "common/msg_handler_plugin.h"
#include "common/llm_checker.h"
#include "common/hixl_utils.h"
#include "llm_datadist/llm_engine_types.h"

using namespace std;
using namespace ::testing;

namespace llm {
class LLMUtilsTest : public ::testing::Test {
 protected:
  void SetUp() override {}
  // 在测试类中进行清理工作，如果需要的话
  void TearDown() override {}
};

TEST_F(LLMUtilsTest, CalcTensorMemSize) {
  int64_t mem_size = -1;
  EXPECT_EQ(LLMUtils::CalcTensorMemSize({1}, ge::DT_INT32, mem_size), ge::SUCCESS);
  EXPECT_EQ(mem_size, 4);
  EXPECT_EQ(LLMUtils::CalcTensorMemSize({1}, ge::DT_STRING, mem_size), ge::SUCCESS);
  EXPECT_EQ(mem_size, 16);
  EXPECT_EQ(LLMUtils::CalcTensorMemSize({1}, ge::DT_INT4, mem_size), ge::SUCCESS);
  EXPECT_EQ(mem_size, 1);
  EXPECT_EQ(LLMUtils::CalcTensorMemSize({3}, ge::DT_INT4, mem_size), ge::SUCCESS);
  EXPECT_EQ(mem_size, 2);
  EXPECT_EQ(LLMUtils::CalcTensorMemSize({3}, ge::DT_UNDEFINED, mem_size), ge::LLM_PARAM_INVALID);
}

TEST_F(LLMUtilsTest, SplitSuccess) {
  auto ret = hixl::Split("", ',');
  EXPECT_EQ(ret.size(), 1);
  EXPECT_EQ(ret[0], "");
  ret = hixl::Split("abcd", 'b');
  EXPECT_EQ(ret.size(), 2);
  EXPECT_EQ(ret[0], "a");
  EXPECT_EQ(ret[1], "cd");
  ret = hixl::Split("abcd", 'd');
  EXPECT_EQ(ret.size(), 2);
  EXPECT_EQ(ret[0], "abc");
  EXPECT_EQ(ret[1], "");
}

TEST_F(LLMUtilsTest, CheckMultiplyOverflowInt64) {
  // Test case 1: a > 0, b > 0, no overflow
  EXPECT_FALSE(LLMUtils::CheckMultiplyOverflowInt64(100, 200));
  // Test case 2: a > 0, b > 0, overflow
  EXPECT_TRUE(LLMUtils::CheckMultiplyOverflowInt64(std::numeric_limits<int64_t>::max() / 2 + 1, 2));
  // Test case 3: a > 0, b < 0, no overflow
  EXPECT_FALSE(LLMUtils::CheckMultiplyOverflowInt64(100, -200));
  // Test case 4: a > 0, b < 0, overflow
  EXPECT_TRUE(LLMUtils::CheckMultiplyOverflowInt64(std::numeric_limits<int64_t>::max(), -2));
  // Test case 5: a < 0, b > 0, no overflow
  EXPECT_FALSE(LLMUtils::CheckMultiplyOverflowInt64(-100, 200));
  // Test case 6: a < 0, b > 0, overflow
  EXPECT_TRUE(LLMUtils::CheckMultiplyOverflowInt64(std::numeric_limits<int64_t>::min(), 2));
  // Test case 7: a < 0, b < 0, no overflow
  EXPECT_FALSE(LLMUtils::CheckMultiplyOverflowInt64(-100, -200));
  // Test case 8: a < 0, b < 0, overflow (a != 0, b < max/a)
  EXPECT_TRUE(LLMUtils::CheckMultiplyOverflowInt64(std::numeric_limits<int64_t>::min(), -1));
  // Test case 9: a == 0 (special case in last branch)
  EXPECT_FALSE(LLMUtils::CheckMultiplyOverflowInt64(0, std::numeric_limits<int64_t>::min()));
}

TEST_F(LLMUtilsTest, TestParserOptions) {
    // 构建选项参数
  const std::map<ge::AscendString, ge::AscendString> llm_options = {
      {"ge.socVersion", "Ascend910B1"},
      {"ge.graphRunMode", "0"},
      {llm::LLM_OPTION_ROLE, "decoder"},
      {"ge.distributed_cluster_build", "1"},
      {"RESOURCE_CONFIG_PATH", "/tmp/numa_config_path"},
      {llm::LLM_OPTION_SYNC_KV_CACHE_WAIT_TIME, "20"},
  };

  DecoderWaitTimeInfo wait_time_info;
  ge::Status ret;
  ret = LLMUtils::ParserWaitTimeInfo(llm_options, wait_time_info);
  EXPECT_EQ(ret, ge::SUCCESS);

  std::vector<int32_t> device_ids;
  ret = LLMUtils::ParseDeviceId(llm_options, device_ids, ge::OPTION_EXEC_DEVICE_ID);
  EXPECT_EQ(ret, ge::SUCCESS);
}

TEST_F(LLMUtilsTest, TestMsgHandlerPlugin) {
  int32_t fd = -1;
  char data[1];
  size_t len = 1;
  ssize_t s = 0;
  s = MsgHandlerPlugin::Read(fd, data, len);
  EXPECT_TRUE(s < 0);

  s = MsgHandlerPlugin::Write(fd, data, len);
  EXPECT_TRUE(s < 0);
}

static ge::Status TestLogTooLong() {
  std::string testlog(MSG_LENGTH * 2, 'c');
  LLM_ASSERT_TRUE(false, "TestLogTooLong:%s", testlog.c_str());
  return ge::SUCCESS;
}

TEST_F(LLMUtilsTest, TestLogTooLong) {
  ge::Status ret = TestLogTooLong();
  EXPECT_NE(ret, ge::SUCCESS);
}
}  // namespace llm
