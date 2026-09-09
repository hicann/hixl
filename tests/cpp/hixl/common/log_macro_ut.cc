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
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "securec.h"
#include "common/hixl_checker.h"
#include "common/hixl_version.h"
#include "hixl_log.h"
#include "common/llm_log.h"
#include "slog_stub.h"

namespace {
struct CapturedLog {
  int32_t module_id = -1;
  int32_t level = -1;
  std::string message;
};

std::string FormatMessage(const char *fmt, va_list args) {
  char buffer[2048] = {};
  const int32_t ret = vsnprintf_truncated_s(buffer, sizeof(buffer), fmt, args);
  EXPECT_GE(ret, 0);
  return ret < 0 ? "" : buffer;
}

class CapturingSlogStub : public llm::SlogStub {
 public:
  void Log(int module_id, int level, const char *fmt, va_list args) override {
    logs.push_back({module_id, level, FormatMessage(fmt, args)});
  }

  std::vector<CapturedLog> logs;
};

class DiscardingSlogStub : public llm::SlogStub {
 public:
  void Log(int, int, const char *, va_list) override {}
};

class LogMacroUt : public testing::Test {
 protected:
  void SetUp() override {
    slog_ = std::make_shared<CapturingSlogStub>();
    llm::SlogStub::SetInstance(slog_);
    slog_->SetLevel(DLOG_ERROR);
    slog_->SetEventLevel(0);
    llm::ResetLogCallStats();
  }

  void TearDown() override {
    llm::SlogStub::SetInstance(nullptr);
  }

  std::shared_ptr<CapturingSlogStub> slog_;
};

hixl::Status CheckHcommFailureWithoutContext(HcclResult ret) {
  HIXL_CHK_HCCL_RET(ret);
  return hixl::SUCCESS;
}

hixl::Status CheckHcommFailureWithContext(HcclResult ret) {
  HIXL_CHK_HCCL_RET(ret, "additional context:%d", 7);
  return hixl::SUCCESS;
}

TEST_F(LogMacroUt, HcommCheckWithoutContextLogsBaseFailure) {
  EXPECT_EQ(CheckHcommFailureWithoutContext(HCCL_E_INTERNAL), hixl::FAILED);

  ASSERT_EQ(slog_->logs.size(), 1U);
  EXPECT_NE(slog_->logs[0].message.find("Call hccl api:ret failed."), std::string::npos);
}

TEST_F(LogMacroUt, HcommCheckWithContextAppendsLog) {
  EXPECT_EQ(CheckHcommFailureWithContext(HCCL_E_PARA), hixl::PARAM_INVALID);

  ASSERT_EQ(slog_->logs.size(), 1U);
  EXPECT_NE(slog_->logs[0].message.find("Call hccl api:ret failed."), std::string::npos);
  EXPECT_NE(slog_->logs[0].message.find("additional context:7"), std::string::npos);
}

TEST_F(LogMacroUt, HixlAclRecordPreservesContext) {
  slog_->SetLevel(DLOG_INFO);
  const int32_t acl_line = __LINE__ + 1;
  HIXL_LOGI("acl value:%d", 7);
  ASSERT_EQ(slog_->logs.size(), 1U);
  EXPECT_EQ(slog_->logs[0].module_id, GE);
  EXPECT_EQ(slog_->logs[0].level, DLOG_INFO);
  EXPECT_NE(slog_->logs[0].message.find("[log_macro_ut.cc:" + std::to_string(acl_line) + "]"), std::string::npos);
  EXPECT_NE(slog_->logs[0].message.find("acl value:7"), std::string::npos);
  const llm::LogCallStats stats = llm::GetLogCallStats();
  EXPECT_EQ(stats.acl_record_count, 1);
  EXPECT_EQ(stats.slog_record_count, 0);
}

TEST_F(LogMacroUt, DisabledHixlRegularLogsDoNotEvaluateArguments) {
  int32_t evaluations = 0;

  HIXL_LOGW("value:%d", ++evaluations);
  HIXL_LOGI("value:%d", ++evaluations);
  HIXL_LOGD("value:%d", ++evaluations);

  EXPECT_EQ(evaluations, 0);
}

TEST_F(LogMacroUt, EventUsesMaskedAclRecordForNewRuntime) {
  slog_->SetEventLevel(0);
  int32_t evaluations = 0;

  const int32_t event_line = __LINE__ + 1;
  HIXL_EVENT("event value:%d", ++evaluations);

  const llm::LogCallStats stats = llm::GetLogCallStats();
  EXPECT_EQ(evaluations, 1);
  ASSERT_EQ(slog_->logs.size(), 1U);
  EXPECT_NE(slog_->logs[0].message.find("[log_macro_ut.cc:" + std::to_string(event_line) + "]"), std::string::npos);
  EXPECT_NE(slog_->logs[0].message.find("event value:1"), std::string::npos);
  EXPECT_EQ(HixlGetRunLogRecord(), acllogRecord);
  EXPECT_EQ(stats.acl_record_count, 1);
  EXPECT_EQ(stats.slog_record_count, 0);
  EXPECT_EQ(stats.run_check_count, 0);
  EXPECT_EQ(stats.last_module_id,
            static_cast<int32_t>(static_cast<uint32_t>(RUN_LOG_MASK) | static_cast<uint32_t>(GE)));
  EXPECT_EQ(stats.last_level, DLOG_INFO);
}

TEST_F(LogMacroUt, RunLogVersionSelectionHandlesBoundaryAndFailures) {
  const auto query_version = [](char *, int32_t *version_num) -> int32_t {
    *version_num = 90200000;  // 9.2.0
    return 0;
  };
  const auto query_new_version = [](char *, int32_t *version_num) -> int32_t {
    *version_num = 90200701;  // 9.2.1-alpha.1
    return 0;
  };
  const auto query_failure = [](char *, int32_t *) -> int32_t { return -1; };

  EXPECT_EQ(HIXL_ACL_RUN_LOG_VERSION_THRESHOLD, 90200000);
  EXPECT_FALSE(HixlIsAclRunLogSupported(nullptr));
  EXPECT_FALSE(HixlIsAclRunLogSupported(query_version));
  EXPECT_TRUE(HixlIsAclRunLogSupported(query_new_version));
  EXPECT_FALSE(HixlIsAclRunLogSupported(query_failure));
  EXPECT_EQ(HixlResolveRunLogRecord(acllogRecord, query_version), DlogRecord);
  EXPECT_EQ(HixlResolveRunLogRecord(acllogRecord, query_new_version), acllogRecord);
  EXPECT_EQ(HixlResolveRunLogRecord(nullptr, query_new_version), DlogRecord);
}

TEST_F(LogMacroUt, CachedCheckFunctionUsesCurrentLogLevel) {
  EXPECT_EQ(HixlGetLogCheckLevel(), acllogCheckDebugLevel);
  slog_->SetLevel(DLOG_ERROR);
  EXPECT_FALSE(HixlCheckLogLevel(GE, DLOG_INFO));
  EXPECT_FALSE(LlmIsLogEnable(GE, DLOG_INFO));
  slog_->SetLevel(DLOG_INFO);
  EXPECT_TRUE(HixlCheckLogLevel(GE, DLOG_INFO));
  EXPECT_TRUE(LlmIsLogEnable(GE, DLOG_INFO));
}

TEST_F(LogMacroUt, LlmMacroUsesAclRecord) {
  slog_->SetLevel(DLOG_WARN);
  LLMLOGW("acl warning:%d", 3);
  ASSERT_EQ(slog_->logs.size(), 1U);
  EXPECT_EQ(slog_->logs[0].module_id, GE);
  EXPECT_EQ(slog_->logs[0].level, DLOG_WARN);
  EXPECT_NE(slog_->logs[0].message.find("acl warning:3"), std::string::npos);
  const llm::LogCallStats stats = llm::GetLogCallStats();
  EXPECT_EQ(stats.acl_record_count, 1);
  EXPECT_EQ(stats.slog_record_count, 0);
}

TEST_F(LogMacroUt, DisabledLlmRegularLogsDoNotEvaluateArguments) {
  int32_t evaluations = 0;

  LLMLOGW("value:%d", ++evaluations);
  LLMLOGI("value:%d", ++evaluations);
  LLMLOGD("value:%d", ++evaluations);

  EXPECT_EQ(evaluations, 0);
}

TEST_F(LogMacroUt, LlmEventUsesMaskedAclRecordForNewRuntime) {
  slog_->SetEventLevel(0);
  const int32_t event_line = __LINE__ + 1;
  LLMEVENT("event value:%d", 11);

  const llm::LogCallStats stats = llm::GetLogCallStats();
  ASSERT_EQ(slog_->logs.size(), 1U);
  EXPECT_NE(slog_->logs[0].message.find("[log_macro_ut.cc:" + std::to_string(event_line) + "]"), std::string::npos);
  EXPECT_NE(slog_->logs[0].message.find("event value:11"), std::string::npos);
  EXPECT_EQ(stats.acl_record_count, 1);
  EXPECT_EQ(stats.slog_record_count, 0);
  EXPECT_EQ(stats.run_check_count, 0);
  EXPECT_EQ(stats.last_module_id,
            static_cast<int32_t>(static_cast<uint32_t>(RUN_LOG_MASK) | static_cast<uint32_t>(GE)));
  EXPECT_EQ(stats.last_level, DLOG_INFO);
}

TEST_F(LogMacroUt, VerStringFallsBackToUnknownWhenNotInjected) {
  const std::string ver = HIXL_VER_STRING;
  EXPECT_FALSE(ver.empty());
  const bool is_unknown = ver == "unknown";
  const bool is_commit = ver.size() >= 7U && ver.find_first_not_of("0123456789abcdef") == std::string::npos;
  EXPECT_TRUE(is_unknown || is_commit);
}

TEST_F(LogMacroUt, LogCallStatsAreThreadSafe) {
  constexpr int32_t kThreadCount = 8;
  constexpr int32_t kCallsPerThread = 100;
  llm::SlogStub::SetInstance(std::make_shared<DiscardingSlogStub>());
  llm::ResetLogCallStats();

  std::vector<std::thread> threads;
  threads.reserve(kThreadCount);
  for (int32_t thread_index = 0; thread_index < kThreadCount; ++thread_index) {
    threads.emplace_back([]() {
      for (int32_t call_index = 0; call_index < kCallsPerThread; ++call_index) {
        acllogRecord(GE, DLOG_INFO, "concurrent log");
      }
    });
  }
  for (auto &thread : threads) {
    thread.join();
  }

  const llm::LogCallStats stats = llm::GetLogCallStats();
  EXPECT_EQ(stats.acl_record_count, kThreadCount * kCallsPerThread);
  EXPECT_EQ(stats.slog_record_count, 0);
}
}  // namespace
