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
#include "adxl/adxl_engine.h"

namespace adxl {
TEST(AdxlGetCapabilityTest, WithoutInitialize) {
  int32_t value = 0;
  EXPECT_EQ(AdxlEngine::GetCapability(AUTO_CONNECT, value), SUCCESS);
  EXPECT_EQ(value, FEATURE_SUPPORTED);
  EXPECT_EQ(AdxlEngine::GetCapability(CLIENT_SERVER_COMM, value), SUCCESS);
  EXPECT_EQ(value, FEATURE_SUPPORTED);
}

TEST(AdxlGetCapabilityTest, UnknownFeature) {
  int32_t value = 0;
  EXPECT_EQ(AdxlEngine::GetCapability(static_cast<FeatureType>(999), value), UNSUPPORTED);
}

TEST(AdxlGetCapabilityTest, InvalidFeature) {
  int32_t value = 0;
  EXPECT_EQ(AdxlEngine::GetCapability(static_cast<FeatureType>(-1), value), PARAM_INVALID);
}
}  // namespace adxl
