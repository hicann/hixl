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
#include <gmock/gmock.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sys/stat.h>
#include "common/hixl_utils.h"
#include "depends/mmpa/src/mmpa_stub.h"

using namespace ::testing;

namespace hixl {
namespace {
constexpr const char kHccnConfPath[] = "/etc/hccn.conf";

class DeviceIpMmpaStub : public llm::MmpaStubApiGe {
 public:
  DeviceIpMmpaStub(std::string conf_path, bool conf_exists) : conf_path_(std::move(conf_path)), conf_exists_(conf_exists) {}

  int32_t RealPath(const CHAR *path, CHAR *realPath, INT32 realPathLen) override {
    if (std::string(path) != kHccnConfPath || !conf_exists_) {
      return EN_ERROR;
    }
    if (realPath == nullptr || realPathLen <= 0) {
        return EN_ERROR;
    }
    size_t destMax = static_cast<size_t>(realPathLen);
    int ret = snprintf_s(realPath, destMax, destMax - 1, "%s", conf_path_.c_str());
    return (ret < 0 || ret >= realPathLen) ? EN_ERROR : EN_OK;
  }

  INT32 Access(const CHAR *path_name) override {
    if (conf_exists_ && std::string(path_name) == conf_path_ && std::filesystem::exists(conf_path_)) {
      return EN_OK;
    }
    return EN_ERROR;
  }

 private:
  std::string conf_path_;
  bool conf_exists_;
};
}  // namespace

class HixlUtilsUTest : public ::testing::Test {
 protected:
  void SetUp() override {
    temp_dir_ = std::filesystem::path("/tmp/hixl_utils_unittest");
    std::filesystem::remove_all(temp_dir_);
    std::filesystem::create_directories(temp_dir_);
    conf_path_ = temp_dir_ / "hccn.conf";
    old_path_ = getenv("PATH") == nullptr ? "" : getenv("PATH");
    llm::MmpaStub::GetInstance().Reset();
  }

  void TearDown() override {
    if (old_path_.empty()) {
      unsetenv("PATH");
    } else {
      setenv("PATH", old_path_.c_str(), 1);
    }
    llm::MmpaStub::GetInstance().Reset();
    std::filesystem::remove_all(temp_dir_);
  }

  void WriteHccnConf(const std::string &content) const {
    std::ofstream file(conf_path_);
    ASSERT_TRUE(file.is_open());
    file << content;
  }

  void InstallConfStub(bool conf_exists) const {
    llm::MmpaStub::GetInstance().SetImpl(std::make_shared<DeviceIpMmpaStub>(conf_path_.string(), conf_exists));
  }

  void CreateHccnTool(const std::string &tool_output) const {
    const auto tool_path = temp_dir_ / "hccn_tool";
    std::ofstream file(tool_path);
    ASSERT_TRUE(file.is_open());
    file << "#!/bin/sh\n";
    file << "echo \"" << tool_output << "\"\n";
    file.close();
    ASSERT_EQ(chmod(tool_path.c_str(), 0755), 0);
    const auto new_path = temp_dir_.string() + ":" + old_path_;
    setenv("PATH", new_path.c_str(), 1);
  }

  std::filesystem::path temp_dir_;
  std::filesystem::path conf_path_;
  std::string old_path_;
};

TEST_F(HixlUtilsUTest, EndpointConfigToStringContainsDeviceInfoTest) {
  EndpointConfig ep;
  ep.protocol = kProtocolRoce;
  ep.comm_id = "127.0.0.1";
  ep.placement = kPlacementDevice;
  ep.plane = "plane-a";
  ep.dst_eid = "00010002000300040005000600070008";
  ep.net_instance_id = "superpod_1";
  ep.device_info.phy_device_id = 3;
  ep.device_info.super_device_id = 7;
  ep.device_info.super_pod_id = 9;

  const std::string text = ep.ToString();
  EXPECT_THAT(text, HasSubstr("protocol: roce"));
  EXPECT_THAT(text, HasSubstr("comm_id: 127.0.0.1"));
  EXPECT_THAT(text, HasSubstr("placement: device"));
  EXPECT_THAT(text, HasSubstr("net_instance_id: superpod_1"));
  EXPECT_THAT(text, HasSubstr("device_info: DeviceInfoConfig{"));
  EXPECT_THAT(text, HasSubstr("phy_device_id: 3"));
  EXPECT_THAT(text, HasSubstr("super_device_id: 7"));
  EXPECT_THAT(text, HasSubstr("super_pod_id: 9"));
}

TEST_F(HixlUtilsUTest, GetDeviceIpFromHccnConfSuccessTest) {
  WriteHccnConf("address_0=192.168.1.10\naddress_1=192.168.1.11\n");
  InstallConfStub(true);

  std::string device_ip;
  EXPECT_EQ(GetDeviceIp(1, device_ip), SUCCESS);
  EXPECT_EQ(device_ip, "192.168.1.11");
}

TEST_F(HixlUtilsUTest, GetDeviceIpInvalidIpInHccnConfTest) {
  WriteHccnConf("address_0=invalid_ip\n");
  InstallConfStub(true);

  std::string device_ip;
  EXPECT_EQ(GetDeviceIp(0, device_ip), PARAM_INVALID);
}

TEST_F(HixlUtilsUTest, GetDeviceIpDoesNotFallbackWhenConfExistsButNoMatchingKeyTest) {
  WriteHccnConf("address_1=192.168.1.11\n");
  InstallConfStub(true);
  CreateHccnTool("ipaddr:10.10.10.10");

  std::string device_ip;
  EXPECT_EQ(GetDeviceIp(0, device_ip), SUCCESS);
  EXPECT_TRUE(device_ip.empty());
}

TEST_F(HixlUtilsUTest, GetDeviceIpFallbackToHccnToolWhenConfMissingTest) {
  InstallConfStub(false);
  CreateHccnTool("ipaddr:10.10.10.10");

  std::string device_ip;
  EXPECT_EQ(GetDeviceIp(0, device_ip), SUCCESS);
  EXPECT_EQ(device_ip, "10.10.10.10");
}

TEST_F(HixlUtilsUTest, GetBondIpAddress) {
  InstallConfStub(false);
  CreateHccnTool("ipaddr:192.168.1.111\n255.255.255.0");

  std::string bond_ip;
  EXPECT_EQ(GetBondIpAddress(0, bond_ip), SUCCESS);
  EXPECT_EQ(bond_ip, "192.168.1.111");
}
}  // namespace hixl
