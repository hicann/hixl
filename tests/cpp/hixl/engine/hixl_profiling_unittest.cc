/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <gtest/gtest.h>
#include "hixl/hixl_types.h"
#include "profiling/prof_api_reg.h"
#include "depends/msprof/src/msprof_stub.h"


#define kAclProfHixlApi     0x0001U
#define kStartProfiling       1U
#define kStopProfiling        2U

namespace hixl {
class HixlProfilingTest : public ::testing::Test {
 protected:
  void SetUp() override {}
  void TearDown() override {}
};

// 测试1：全局回调已自动注册
TEST(HixlProfilingTest, AutoRegisterCallback) {
  ProfCommandHandle cb = GetHixlProfCallback();
  ASSERT_NE(cb, nullptr);
}

// 测试2：正常Start → 返回0
TEST(HixlProfilingTest, StartProfilingOK) {
  ProfCommandHandle cb = GetHixlProfCallback();
  MsprofCommandHandle cfg{};
  cfg.profSwitch = kAclProfHixlApi;
  cfg.type = kStartProfiling;
  cfg.devIdList[0] = 0;
  cfg.devNums = 1;

  int32_t ret = cb(PROF_CTRL_SWITCH, &cfg, sizeof(cfg));
  EXPECT_EQ(ret, ACL_SUCCESS); // 成功
}

// 测试3：正常Stop → 返回0
TEST(HixlProfilingTest, StopProfilingOK) {
  ProfCommandHandle cb = GetHixlProfCallback();
  MsprofCommandHandle cfg{};
  cfg.profSwitch = kAclProfHixlApi;
  cfg.type = kStopProfiling;
  cfg.devIdList[0] = 0;
  cfg.devNums = 1;

  int32_t ret = cb(PROF_CTRL_SWITCH, &cfg, sizeof(cfg));
  EXPECT_EQ(ret, ACL_SUCCESS);
}

// 测试6：数据指针为空 → 返回-1
TEST(HixlProfilingTest, DataNullReturnErr) {
  ProfCommandHandle cb = GetHixlProfCallback();
  int32_t ret = cb(PROF_CTRL_SWITCH, nullptr, 100);
  EXPECT_EQ(ret, ACL_ERROR_INVALID_PARAM);
}

// 测试7：数据长度太短 → 返回-1
TEST(HixlProfilingTest, DataLenTooSmallReturnErr) {
  ProfCommandHandle cb = GetHixlProfCallback();
  int dummy = 0;
  int32_t ret = cb(PROF_CTRL_SWITCH, &dummy, 4);
  EXPECT_EQ(ret, ACL_ERROR_INVALID_PARAM);
}

// 测试8：不支持的data_type → 返回-1
TEST(HixlProfilingTest, InvalidTypeReturnOK) {
  ProfCommandHandle cb = GetHixlProfCallback();
  MsprofCommandHandle cfg{};
  cfg.profSwitch = kAclProfHixlApi;
  cfg.type = kStartProfiling;
  cfg.devIdList[0] = 0;
  cfg.devNums = 1;
  int32_t ret = cb(0xFFFF, &cfg, 0);
  EXPECT_EQ(ret, ACL_SUCCESS);
}

// 测试9：Reporter开启路径（prof_run=true）
TEST(HixlProfilingTest, ReporterEnableRun) {
  ProfCommandHandle cb = GetHixlProfCallback();
  MsprofCommandHandle cfg{};
  cfg.profSwitch = kAclProfHixlApi;
  cfg.type = kStartProfiling;
  cfg.devIdList[0] = 0;
  cfg.devNums = 1;
  cb(PROF_CTRL_SWITCH, &cfg, sizeof(cfg));

  HixlProfilingReporter reporter(HixlProfType::HixlOpBatchRead);
  // 无崩溃 = 覆盖成功
}

// 测试10：Reporter关闭路径（prof_run=false）
TEST(HixlProfilingTest, ReporterDisableNoRun) {
  HixlProfilingReporter reporter(HixlProfType::HixlOpBatchWrite);
  // 无崩溃 = 覆盖成功
}
}