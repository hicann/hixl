/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>

#include <gtest/gtest.h>

#include "securec.h"
#include "hixl_log.h"
#include "common/llm_log.h"

namespace {
struct LogCall {
  int32_t module_id = -1;
  int32_t level = -1;
  int32_t record_count = 0;
  std::string message;
};

LogCall g_log_call;
int32_t g_log_level = DLOG_ERROR;
int32_t g_run_check_count = 0;

class LogFallbackUt : public testing::Test {
 protected:
  void SetUp() override {
    g_log_call = {};
    g_log_level = DLOG_ERROR;
    g_run_check_count = 0;
  }
};
}  // namespace

extern "C" int32_t CheckLogLevel(int32_t module_id, int32_t log_level) {
  if ((static_cast<uint32_t>(module_id) & static_cast<uint32_t>(RUN_LOG_MASK)) != 0U) {
    ++g_run_check_count;
    return 0;
  }
  return log_level >= g_log_level ? 1 : 0;
}

extern "C" void DlogRecord(int32_t module_id, int32_t level, const char *fmt, ...) {
  char buffer[2048] = {};
  va_list args;
  va_start(args, fmt);
  const int32_t ret = vsnprintf_truncated_s(buffer, sizeof(buffer), fmt, args);
  va_end(args);
  EXPECT_GE(ret, 0);
  g_log_call.module_id = module_id;
  g_log_call.level = level;
  ++g_log_call.record_count;
  g_log_call.message = ret < 0 ? "" : buffer;
}

extern "C" int32_t aclsysGetVersionNum(char *pkg_name, int32_t *version_num) {
  if ((pkg_name == nullptr) || (version_num == nullptr) || (std::string(pkg_name) != "runtime")) {
    return -1;
  }
  *version_num = 90200000;  // 9.2.0
  return 0;
}

namespace {
TEST(HixlGetFileNameUt, ExtractsFileNameFromSupportedPaths) {
  EXPECT_STREQ(HixlGetFileName("hixl_log.h"), "hixl_log.h");
  EXPECT_STREQ(HixlGetFileName("src/hixl/common/hixl_log.h"), "hixl_log.h");
  EXPECT_STREQ(HixlGetFileName("src\\hixl\\common\\hixl_log.h"), "hixl_log.h");
  EXPECT_STREQ(HixlGetFileName(nullptr), "");
}

TEST_F(LogFallbackUt, AclSymbolsAreAbsent) {
  EXPECT_EQ(HixlGetAclLogCheckLevel(), nullptr);
  EXPECT_EQ(HixlGetAclLogRecord(), nullptr);
  EXPECT_EQ(HixlGetLogCheckLevel(), CheckLogLevel);
  EXPECT_EQ(HixlGetLogRecord(), DlogRecord);
}

TEST_F(LogFallbackUt, HixlLogFallsBackToDlog) {
  g_log_level = DLOG_INFO;
  const int32_t line = __LINE__ + 1;
  HIXL_LOGI("fallback value:%d", 7);

  EXPECT_EQ(g_log_call.record_count, 1);
  EXPECT_EQ(g_log_call.module_id, GE);
  EXPECT_EQ(g_log_call.level, DLOG_INFO);
  EXPECT_NE(g_log_call.message.find("[log_fallback_ut.cc:" + std::to_string(line) + "]"), std::string::npos);
  EXPECT_NE(g_log_call.message.find("fallback value:7"), std::string::npos);
}

TEST_F(LogFallbackUt, LlmLogFallsBackToDlog) {
  g_log_level = DLOG_WARN;
  const int32_t line = __LINE__ + 1;
  LLMLOGW("fallback warning:%d", 8);

  EXPECT_EQ(g_log_call.record_count, 1);
  EXPECT_EQ(g_log_call.module_id, GE);
  EXPECT_EQ(g_log_call.level, DLOG_WARN);
  EXPECT_NE(g_log_call.message.find("[log_fallback_ut.cc:" + std::to_string(line) + "]"), std::string::npos);
  EXPECT_NE(g_log_call.message.find("fallback warning:8"), std::string::npos);
}

TEST_F(LogFallbackUt, MissingAclCheckFallsBackToCheckLogLevel) {
  g_log_level = DLOG_WARN;

  EXPECT_FALSE(HixlCheckLogLevel(GE, DLOG_INFO));
  EXPECT_TRUE(HixlCheckLogLevel(GE, DLOG_WARN));
  EXPECT_FALSE(LlmIsLogEnable(GE, DLOG_INFO));
  EXPECT_TRUE(LlmIsLogEnable(GE, DLOG_WARN));
}

TEST_F(LogFallbackUt, RuntimeVersionAtThresholdUsesDlogForEvent) {
  const int32_t event_line = __LINE__ + 1;
  HIXL_EVENT("event value:%d", 9);

  EXPECT_EQ(HixlGetRunLogRecord(), DlogRecord);
  EXPECT_EQ(g_log_call.record_count, 1);
  EXPECT_EQ(g_run_check_count, 0);
  EXPECT_EQ(g_log_call.module_id,
            static_cast<int32_t>(static_cast<uint32_t>(RUN_LOG_MASK) | static_cast<uint32_t>(GE)));
  EXPECT_EQ(g_log_call.level, DLOG_INFO);
  EXPECT_NE(g_log_call.message.find("[log_fallback_ut.cc:" + std::to_string(event_line) + "]"), std::string::npos);
  EXPECT_NE(g_log_call.message.find("event value:9"), std::string::npos);
}

TEST_F(LogFallbackUt, DisabledRegularLogsDoNotEvaluateArguments) {
  int32_t evaluations = 0;

  HIXL_LOGW("value:%d", ++evaluations);
  HIXL_LOGI("value:%d", ++evaluations);
  HIXL_LOGD("value:%d", ++evaluations);
  LLMLOGW("value:%d", ++evaluations);
  LLMLOGI("value:%d", ++evaluations);
  LLMLOGD("value:%d", ++evaluations);

  EXPECT_EQ(evaluations, 0);
  EXPECT_EQ(g_log_call.record_count, 0);
  EXPECT_EQ(g_run_check_count, 0);
}
}  // namespace
