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
#include <arpa/inet.h>
#include <cstdlib>
#include <cstring>
#include <experimental/filesystem>
#include "securec.h"
#include "hcomm/hcomm_res_defs.h"
namespace fs = std::experimental::filesystem;
#include <fstream>
#include <sys/stat.h>
#include "common/hixl_utils.h"
#include "depends/mmpa/src/mmpa_stub.h"
#include "graph/ascend_string.h"
#include "hixl/hixl_types.h"

using namespace ::testing;

namespace hixl {
namespace {
constexpr const char kHccnConfPath[] = "/etc/hccn.conf";

class DeviceIpMmpaStub : public llm::MmpaStubApiGe {
 public:
  DeviceIpMmpaStub(std::string conf_path, bool conf_exists)
      : conf_path_(std::move(conf_path)), conf_exists_(conf_exists) {}

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
    if (conf_exists_ && std::string(path_name) == conf_path_ && fs::exists(conf_path_)) {
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
    temp_dir_ = fs::path("/tmp/hixl_utils_unittest");
    fs::remove_all(temp_dir_);
    fs::create_directories(temp_dir_);
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
    fs::remove_all(temp_dir_);
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

  fs::path temp_dir_;
  fs::path conf_path_;
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
  EXPECT_EQ(GetBondIpAddress(0, 0, bond_ip), SUCCESS);
  EXPECT_EQ(bond_ip, "192.168.1.111");
}

TEST_F(HixlUtilsUTest, EndpointToStringRoceIpv4DeviceTest) {
  EndpointDesc ep{};
  ep.protocol = COMM_PROTOCOL_ROCE;
  ep.commAddr.type = COMM_ADDR_TYPE_IP_V4;
  ep.commAddr.addr.s_addr = htonl((192U << 24) | (168U << 16) | (1U << 8) | 10U);  // 192.168.1.10
  ep.loc.locType = ENDPOINT_LOC_TYPE_DEVICE;
  ep.loc.device.devPhyId = 3;

  const std::string text = EndpointToString(ep);
  EXPECT_THAT(text, HasSubstr("protocol=roce"));
  EXPECT_THAT(text, HasSubstr("addr=IPv4:192.168.1.10"));
  EXPECT_THAT(text, HasSubstr("devPhyId=3"));
}

TEST_F(HixlUtilsUTest, EndpointToStringRoceIpv6DeviceTest) {
  EndpointDesc ep{};
  ep.protocol = COMM_PROTOCOL_ROCE;
  ep.commAddr.type = COMM_ADDR_TYPE_IP_V6;
  // fe80::1 = {0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}
  const uint8_t ipv6_bytes[16] = {0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
  (void)memcpy_s(ep.commAddr.addr6.s6_addr, sizeof(ep.commAddr.addr6.s6_addr), ipv6_bytes, sizeof(ipv6_bytes));
  ep.loc.locType = ENDPOINT_LOC_TYPE_DEVICE;
  ep.loc.device.devPhyId = 0;

  const std::string text = EndpointToString(ep);
  EXPECT_THAT(text, HasSubstr("protocol=roce"));
  EXPECT_THAT(text, HasSubstr("addr=IPv6:fe80::1"));
  EXPECT_THAT(text, HasSubstr("devPhyId=0"));
}

TEST_F(HixlUtilsUTest, EndpointToStringHccsIdDeviceTest) {
  EndpointDesc ep{};
  ep.protocol = COMM_PROTOCOL_HCCS;
  ep.commAddr.type = COMM_ADDR_TYPE_ID;
  ep.commAddr.id = 0x1a2b;
  ep.loc.locType = ENDPOINT_LOC_TYPE_DEVICE;
  ep.loc.device.devPhyId = 1;

  const std::string text = EndpointToString(ep);
  EXPECT_THAT(text, HasSubstr("protocol=hccs"));
  EXPECT_THAT(text, HasSubstr("addr=ID:0x1a2b"));
  EXPECT_THAT(text, HasSubstr("devPhyId=1"));
}

TEST_F(HixlUtilsUTest, EndpointToStringUbTpEidDeviceTest) {
  EndpointDesc ep{};
  ep.protocol = COMM_PROTOCOL_UBC_TP;
  ep.commAddr.type = COMM_ADDR_TYPE_EID;
  const uint8_t eid_bytes[COMM_ADDR_EID_LEN] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                                0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
  (void)memcpy_s(ep.commAddr.eid, COMM_ADDR_EID_LEN, eid_bytes, COMM_ADDR_EID_LEN);
  ep.loc.locType = ENDPOINT_LOC_TYPE_DEVICE;
  ep.loc.device.devPhyId = 2;

  const std::string text = EndpointToString(ep);
  EXPECT_THAT(text, HasSubstr("protocol=ub_tp"));
  EXPECT_THAT(text, HasSubstr("addr=EID[0001020304050607:08090a0b0c0d0e0f]"));
  EXPECT_THAT(text, HasSubstr("devPhyId=2"));
}

TEST_F(HixlUtilsUTest, EndpointToStringUboeIpv4HostTest) {
  EndpointDesc ep{};
  ep.protocol = COMM_PROTOCOL_UBOE;
  ep.commAddr.type = COMM_ADDR_TYPE_IP_V4;
  ep.commAddr.addr.s_addr = htonl((10U << 24) | (0U << 16) | (0U << 8) | 1U);  // 10.0.0.1
  ep.loc.locType = ENDPOINT_LOC_TYPE_HOST;
  ep.loc.host.id = 42;

  const std::string text = EndpointToString(ep);
  EXPECT_THAT(text, HasSubstr("protocol=uboe"));
  EXPECT_THAT(text, HasSubstr("addr=IPv4:10.0.0.1"));
  EXPECT_THAT(text, HasSubstr("hostId=42"));
  EXPECT_THAT(text, Not(HasSubstr("devPhyId")));
}

TEST_F(HixlUtilsUTest, EndpointToStringUnknownProtocolTest) {
  EndpointDesc ep{};
  ep.protocol = static_cast<CommProtocol>(99);
  ep.commAddr.type = COMM_ADDR_TYPE_ID;
  ep.commAddr.id = 0;
  ep.loc.locType = ENDPOINT_LOC_TYPE_DEVICE;
  ep.loc.device.devPhyId = 0;

  const std::string text = EndpointToString(ep);
  EXPECT_THAT(text, HasSubstr("protocol=UNKNOWN(99)"));
}

TEST_F(HixlUtilsUTest, EndpointToStringUnknownAddrTypeTest) {
  EndpointDesc ep{};
  ep.protocol = COMM_PROTOCOL_ROCE;
  ep.commAddr.type = static_cast<CommAddrType>(99);
  ep.loc.locType = ENDPOINT_LOC_TYPE_DEVICE;
  ep.loc.device.devPhyId = 0;

  const std::string text = EndpointToString(ep);
  EXPECT_THAT(text, HasSubstr("addr=UNKNOWN"));
}

TEST_F(HixlUtilsUTest, EndpointToStringReservedLocNoLocInfoTest) {
  EndpointDesc ep{};
  ep.protocol = COMM_PROTOCOL_UBC_CTP;
  ep.commAddr.type = COMM_ADDR_TYPE_ID;
  ep.commAddr.id = 5;
  ep.loc.locType = ENDPOINT_LOC_TYPE_RESERVED;

  const std::string text = EndpointToString(ep);
  EXPECT_THAT(text, HasSubstr("protocol=ub_ctp"));
  EXPECT_THAT(text, HasSubstr("addr=ID:0x5"));
  EXPECT_THAT(text, Not(HasSubstr("devPhyId")));
  EXPECT_THAT(text, Not(HasSubstr("hostId")));
}

}  // namespace hixl
