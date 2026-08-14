/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

/**
 * @file route_conf_generator_ut.cc
 * @brief RouteConfGenerator 类单元测试
 *
 * 测试覆盖：
 * - SerializeHostRouteJson 序列化（成功/空/多设备/单设备）
 */

#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "route_conf_generator.h"
#include "common/hixl_inner_types.h"

namespace hixl {
namespace test {

namespace {

HostRouteData MakeSampleHostRouteData() {
  HostRouteData data;
  HostRouteEntry e0;
  e0.device_id = 0;
  e0.local_eid = "cpu_pg_eid_000";
  e0.remote_eid = "npu_eid_000";
  data.devices.push_back(e0);
  HostRouteEntry e1;
  e1.device_id = 1;
  e1.local_eid = "cpu_pg_eid_001";
  e1.remote_eid = "npu_eid_001";
  data.devices.push_back(e1);
  return data;
}

}  // namespace

// ============ SerializeHostRouteJson ============

TEST(RouteConfGeneratorTest, SerializeSuccess) {
  HostRouteData data = MakeSampleHostRouteData();
  std::string json_str;
  int32_t ret = RouteConfGenerator::SerializeHostRouteJson(data, json_str);
  EXPECT_EQ(ret, SUCCESS);
  EXPECT_NE(json_str.find("\"device_num\""), std::string::npos);
  EXPECT_NE(json_str.find("\"devices\""), std::string::npos);
  EXPECT_NE(json_str.find("npu_eid_000"), std::string::npos);
  EXPECT_NE(json_str.find("cpu_pg_eid_000"), std::string::npos);
}

TEST(RouteConfGeneratorTest, SerializeEmptyDevices) {
  HostRouteData data;
  std::string json_str;
  int32_t ret = RouteConfGenerator::SerializeHostRouteJson(data, json_str);
  EXPECT_EQ(ret, SUCCESS);
  EXPECT_NE(json_str.find("\"device_num\": 0"), std::string::npos);
}

TEST(RouteConfGeneratorTest, SerializeDeviceNumMatchesDevicesSize) {
  HostRouteData data = MakeSampleHostRouteData();
  std::string json_str;
  int32_t ret = RouteConfGenerator::SerializeHostRouteJson(data, json_str);
  EXPECT_EQ(ret, SUCCESS);
  EXPECT_NE(json_str.find("\"device_num\": 2"), std::string::npos);
}

TEST(RouteConfGeneratorTest, SerializeSingleDevice) {
  HostRouteData data;
  HostRouteEntry e;
  e.device_id = 7;
  e.local_eid = "cpu_pg_eid_007";
  e.remote_eid = "npu_eid_007";
  data.devices.push_back(e);
  std::string json_str;
  int32_t ret = RouteConfGenerator::SerializeHostRouteJson(data, json_str);
  EXPECT_EQ(ret, SUCCESS);
  EXPECT_NE(json_str.find("\"device_num\": 1"), std::string::npos);
  EXPECT_NE(json_str.find("\"device_id\": 7"), std::string::npos);
  EXPECT_NE(json_str.find("cpu_pg_eid_007"), std::string::npos);
  EXPECT_NE(json_str.find("npu_eid_007"), std::string::npos);
}

}  // namespace test
}  // namespace hixl
