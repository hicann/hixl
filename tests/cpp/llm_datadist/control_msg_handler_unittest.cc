/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING
 * BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE. See LICENSE in the root of
 * the software repository for the full text of the License.
 */

#include <gtest/gtest.h>
#include "adxl/control_msg_handler.h"

using namespace std;
using namespace ::testing;

namespace adxl {
class ControMsgHandlerUnitTest : public ::testing::Test {
protected:
  void SetUp() override {
  }

  void TearDown() override {
  }
};

TEST_F(ControMsgHandlerUnitTest, TransferTypeToStringAllTypes) {
  EXPECT_EQ(TransferTypeToString(TransferType::kWriteH2RH), "WriteH2RH");
  EXPECT_EQ(TransferTypeToString(TransferType::kReadRH2H), "ReadRH2H");
  EXPECT_EQ(TransferTypeToString(TransferType::kWriteH2RD), "WriteH2RD");
  EXPECT_EQ(TransferTypeToString(TransferType::kReadRH2D), "ReadRH2D");
  EXPECT_EQ(TransferTypeToString(TransferType::kWriteD2RH), "WriteD2RH");
  EXPECT_EQ(TransferTypeToString(TransferType::kReadRD2H), "ReadRD2H");
  EXPECT_EQ(TransferTypeToString(TransferType::kWriteD2RD), "WriteD2RD");
  EXPECT_EQ(TransferTypeToString(TransferType::kReadRD2D), "ReadRD2D");
  EXPECT_EQ(TransferTypeToString(TransferType::kEnd), "End");
}
} // namespace adxl