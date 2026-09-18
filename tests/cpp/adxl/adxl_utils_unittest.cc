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

#include "adxl/adxl_utils.h"

namespace adxl {

TEST(AdxlUtilsUTest, LoadJsonConfigObject) {
  std::map<AscendString, AscendString> options;

  EXPECT_EQ(LoadJsonConfig(R"({"comm_resource_config.listen_port":"26666"})", options), SUCCESS);
  ASSERT_EQ(options.size(), 1U);
  const auto it = options.find(AscendString("comm_resource_config.listen_port"));
  ASSERT_NE(it, options.end());
  EXPECT_EQ(std::string(it->second.GetString()), "26666");
}

TEST(AdxlUtilsUTest, LoadJsonConfigRejectsNonObjectAndInvalidJson) {
  std::map<AscendString, AscendString> options;

  for (const char *json : {"[]", "null", "1", "\"config\""}) {
    options.clear();
    EXPECT_EQ(LoadJsonConfig(json, options), PARAM_INVALID) << json;
    EXPECT_TRUE(options.empty()) << json;
  }
  EXPECT_EQ(LoadJsonConfig("{", options), PARAM_INVALID);
}

}  // namespace adxl
