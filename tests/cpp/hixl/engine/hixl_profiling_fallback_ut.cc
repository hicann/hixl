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
#include <string>

#include <gtest/gtest.h>

#include "acl/acl_base.h"
#include "aprof_pub.h"
#include "hixl_log.h"
#include "profiling/prof_reporter.h"
#include "depends/msprof/src/msprof_stub.h"

extern "C" int32_t CheckLogLevel(int32_t, int32_t) {
  return 1;
}

extern "C" void DlogRecord(int32_t, int32_t, const char *, ...) {}

extern "C" int32_t aclsysGetVersionNum(char *pkgName, int32_t *versionNum) {
  if ((pkgName == nullptr) || (versionNum == nullptr) || (std::string(pkgName) != "runtime")) {
    return -1;
  }
  *versionNum = 90200000;
  return 0;
}

namespace hixl {
namespace {
constexpr uint64_t kAclProfHixlApi = 0x0001U;
constexpr uint32_t kStartProfiling = 1U;

int32_t StartMsprofProfiling() {
  ProfCommandHandle cb = GetHixlProfCallback();
  if (cb == nullptr) {
    return ACL_ERROR_INVALID_PARAM;
  }
  MsprofCommandHandle cfg{};
  cfg.profSwitch = kAclProfHixlApi;
  cfg.type = kStartProfiling;
  cfg.devIdList[0] = 0;
  cfg.devNums = 1;
  return cb(PROF_CTRL_SWITCH, &cfg, sizeof(cfg));
}
}  // namespace

TEST(HixlProfilingFallbackUt, UsesMsprofWhenRuntimeAtThreshold) {
  EXPECT_EQ(HixlGetAclLogRecord(), nullptr);
  ASSERT_EQ(StartMsprofProfiling(), ACL_SUCCESS);
  const uint64_t report_count = GetMsprofReportApiCount();
  { HixlProfilingReporter reporter(HixlProfType::HixlOpBatchRead); }
  EXPECT_EQ(GetMsprofReportApiCount(), report_count + 1U);
}

TEST(HixlProfilingFallbackUt, AsyncRangeDestructorCancelsReport) {
  ASSERT_EQ(StartMsprofProfiling(), ACL_SUCCESS);
  const uint64_t report_count = GetMsprofReportApiCount();
  { auto prof_start = GetProfStart(HixlProfType::HixlOpBatchRead); }
  EXPECT_EQ(GetMsprofReportApiCount(), report_count);
}

TEST(HixlProfilingFallbackUt, AsyncRangeMacroReportsOnce) {
  ASSERT_EQ(StartMsprofProfiling(), ACL_SUCCESS);
  const uint64_t report_count = GetMsprofReportApiCount();
  auto prof_start = GetProfStart(HixlProfType::HixlOpBatchWrite);
  { HIXL_API_PROFILING_WITH_PROF_START(prof_start); }
  EXPECT_EQ(GetMsprofReportApiCount(), report_count + 1U);
  prof_start.reset();
  EXPECT_EQ(GetMsprofReportApiCount(), report_count + 1U);
}
}  // namespace hixl
