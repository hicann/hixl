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

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include "adxl/adxl_types.h"
#include "ascendcl_stub.h"
#include "engine/endpoint_test_utils.h"
#include "test_mmpa_utils.h"
#include "depends/mmpa/src/mmpa_stub.h"

#define private public
#include "engine/endpoint_generator.h"
#undef private

namespace hixl {
namespace {
using MockLocCommResMmpaStub = test::TestMmpaStub;
using MockLocCommResAclRuntimeStub = endpoint_test::MockAclRuntimeStub;
constexpr const char kHccnToolPath[] = "/usr/local/Ascend/driver/tools/hccn_tool";

class UboeMmpaStub : public test::TestMmpaStub {
 public:
  INT32 Access(const CHAR *path_name) override {
    std::string path_str(path_name);
    if (path_str == kHccnToolPath) {
      return EN_ERROR;
    }
    return test::TestMmpaStub::Access(path_name);
  }
};

std::string CreateExecutableScript(const std::string &name, const std::string &content) {
  std::string path = "/tmp/" + name;
  std::ofstream ofs(path);
  EXPECT_TRUE(ofs.is_open());
  ofs << content;
  ofs.close();
  chmod(path.c_str(), 0755);
  return path;
}

void SetUboeProtocolDescOption(std::map<AscendString, AscendString> &options) {
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"(
    {
      "comm_resource_config.protocol_desc": ["uboe:device"]
    }
  )";
}

void ExpectSingleUboeEndpoint(const std::vector<EndpointConfig> &endpoint_list, const std::string &uboe_comm_id) {
  ASSERT_EQ(endpoint_list.size(), 1U);
  EXPECT_EQ(endpoint_list[0].protocol, kProtocolUboe);
  EXPECT_EQ(endpoint_list[0].comm_id, uboe_comm_id);
  EXPECT_EQ(endpoint_list[0].placement, kPlacementDevice);
  EXPECT_EQ(endpoint_list[0].net_instance_id, "default_superpod1_1");
}

void ExpectRoceHccsUboeEndpoints(const std::vector<EndpointConfig> &endpoint_list,
                                 const std::string &uboe_comm_id) {
  ASSERT_EQ(endpoint_list.size(), 3U);
  EXPECT_EQ(endpoint_list[0].protocol, kProtocolRoce);
  EXPECT_EQ(endpoint_list[1].protocol, kProtocolHccs);
  EXPECT_EQ(endpoint_list[2].protocol, kProtocolUboe);
  EXPECT_EQ(endpoint_list[2].comm_id, uboe_comm_id);
}
}  // namespace

class EndpointGeneratorUTest : public ::testing::Test {
 protected:
  void SetUp() override {
    acl_stub_ = endpoint_test::CreateAclRuntimeStub("Ascend910_9391", 0, 3, 0, 8);
    llm::AclRuntimeStub::SetInstance(acl_stub_);

    mmpa_stub_ = std::make_shared<MockLocCommResMmpaStub>();
    llm::MmpaStub::GetInstance().SetImpl(mmpa_stub_);

    const char *old_path = std::getenv("PATH");
    old_path_ = (old_path == nullptr) ? "" : old_path;
    old_intra_roce_enable_ = std::getenv("HCCL_INTRA_ROCE_ENABLE");
    unsetenv("HCCL_INTRA_ROCE_ENABLE");
  }

  void TearDown() override {
    llm::AclRuntimeStub::Reset();
    llm::MmpaStub::GetInstance().Reset();

    if (!old_path_.empty()) {
      setenv("PATH", old_path_.c_str(), 1);
    }
    if (old_intra_roce_enable_ != nullptr) {
      setenv("HCCL_INTRA_ROCE_ENABLE", old_intra_roce_enable_, 1);
    } else {
      unsetenv("HCCL_INTRA_ROCE_ENABLE");
    }
  }

  std::shared_ptr<MockLocCommResAclRuntimeStub> acl_stub_;
  std::shared_ptr<MockLocCommResMmpaStub> mmpa_stub_;
  std::string old_path_;
  const char *old_intra_roce_enable_ = nullptr;
};

TEST_F(EndpointGeneratorUTest, BuildHccsEndpointSuccess) {
  EndpointGenerator::EndpointInfo endpoint{};
  EXPECT_EQ(EndpointGenerator::BuildHccsEndpoint(12, endpoint), SUCCESS);
  EXPECT_EQ(endpoint.protocol, "hccs");
  EXPECT_EQ(endpoint.comm_id, "12");
  EXPECT_EQ(endpoint.placement, "device");
}

TEST_F(EndpointGeneratorUTest, GetSocTypeByNameRecognizesAllA2SocNames) {
  EXPECT_EQ(EndpointGenerator::GetSocTypeByName("Ascend910B1"), EndpointGenerator::SocType::kV2);
  EXPECT_EQ(EndpointGenerator::GetSocTypeByName("Ascend910B2"), EndpointGenerator::SocType::kV2);
  EXPECT_EQ(EndpointGenerator::GetSocTypeByName("Ascend910B3"), EndpointGenerator::SocType::kV2);
  EXPECT_EQ(EndpointGenerator::GetSocTypeByName("Ascend910B4"), EndpointGenerator::SocType::kV2);
  EXPECT_EQ(EndpointGenerator::GetSocTypeByName("Ascend910B2C"), EndpointGenerator::SocType::kV2);
  EXPECT_EQ(EndpointGenerator::GetSocTypeByName("Ascend910B4-1"), EndpointGenerator::SocType::kV2);
}

TEST_F(EndpointGeneratorUTest, BuildNetInstanceIdForV3Success) {
  acl_stub_->soc_name_ = "Ascend910_9391";
  acl_stub_->super_pod_id_ = 12345;

  std::string net_instance_id;
  EXPECT_EQ(EndpointGenerator::BuildNetInstanceId(0, "127.0.0.1:26000", net_instance_id), SUCCESS);
  EXPECT_EQ(net_instance_id, "12345");
}

TEST_F(EndpointGeneratorUTest, BuildNetInstanceIdForV2Success) {
  acl_stub_->soc_name_ = "Ascend910B1";

  std::string net_instance_id;
  EXPECT_EQ(EndpointGenerator::BuildNetInstanceId(0, "192.168.1.8:26000", net_instance_id), SUCCESS);
  EXPECT_EQ(net_instance_id, "192.168.1.8");
}

TEST_F(EndpointGeneratorUTest, BuildNetInstanceIdForAllA2SocNamesSuccess) {
  const std::vector<std::string> soc_names = {"Ascend910B1", "Ascend910B2", "Ascend910B3",
                                              "Ascend910B4", "Ascend910B2C", "Ascend910B4-1"};
  for (const auto &soc_name : soc_names) {
    acl_stub_->soc_name_ = soc_name;
    std::string net_instance_id;
    EXPECT_EQ(EndpointGenerator::BuildNetInstanceId(0, "0.0.0.0:26000", net_instance_id), SUCCESS);
    EXPECT_EQ(net_instance_id, "0.0.0.0");
  }
}

TEST_F(EndpointGeneratorUTest, BuildNetInstanceIdForV2WildcardSuccess) {
  acl_stub_->soc_name_ = "Ascend910B1";

  std::string net_instance_id;
  EXPECT_EQ(EndpointGenerator::BuildNetInstanceId(0, "0.0.0.0:26000", net_instance_id), SUCCESS);
  EXPECT_EQ(net_instance_id, "0.0.0.0");
}

TEST_F(EndpointGeneratorUTest, BuildNetInstanceIdForOtherSocFailed) {
  acl_stub_->soc_name_ = "OtherSoc";

  std::string net_instance_id;
  EXPECT_EQ(EndpointGenerator::BuildNetInstanceId(0, "127.0.0.1:26000", net_instance_id), PARAM_INVALID);
}

TEST_F(EndpointGeneratorUTest, GetDeviceIpFromHccnConfSuccess) {
  const std::string file_path = test::CreateTempFileWithContent(
      "/tmp/loc_comm_res_ut_XXXXXX",
      "address_3=192.168.100.8\n"
      "address_4=192.168.100.9\n");

  mmpa_stub_->real_path_ok_ = true;
  mmpa_stub_->access_ok_ = true;
  mmpa_stub_->fake_real_path_ = file_path;

  std::string device_ip;
  EXPECT_EQ(EndpointGenerator::GetDeviceIp(3, device_ip), SUCCESS);
  EXPECT_EQ(device_ip, "192.168.100.8");

  (void)remove(file_path.c_str());
}

TEST_F(EndpointGeneratorUTest, GetDeviceIpFromHccnConfInvalidFormatFailed) {
  const std::string file_path =
      test::CreateTempFileWithContent("/tmp/loc_comm_res_ut_XXXXXX", "address_3=192.168.100.8=extra\n");

  mmpa_stub_->real_path_ok_ = true;
  mmpa_stub_->access_ok_ = true;
  mmpa_stub_->fake_real_path_ = file_path;

  std::string device_ip;
  EXPECT_EQ(EndpointGenerator::GetDeviceIp(3, device_ip), FAILED);

  (void)remove(file_path.c_str());
}

TEST_F(EndpointGeneratorUTest, GetDeviceIpFromHccnConfInvalidIpFailed) {
  const std::string file_path =
      test::CreateTempFileWithContent("/tmp/loc_comm_res_ut_XXXXXX", "address_3=not_an_ip\n");

  mmpa_stub_->real_path_ok_ = true;
  mmpa_stub_->access_ok_ = true;
  mmpa_stub_->fake_real_path_ = file_path;

  std::string device_ip;
  EXPECT_EQ(EndpointGenerator::GetDeviceIp(3, device_ip), PARAM_INVALID);

  (void)remove(file_path.c_str());
}

TEST_F(EndpointGeneratorUTest, BuildRoceEndpointSuccess) {
  const std::string file_path =
      test::CreateTempFileWithContent("/tmp/loc_comm_res_ut_XXXXXX", "address_3=10.10.10.3\n");

  mmpa_stub_->real_path_ok_ = true;
  mmpa_stub_->access_ok_ = true;
  mmpa_stub_->fake_real_path_ = file_path;

  EndpointGenerator::EndpointInfo endpoint{};
  EXPECT_EQ(EndpointGenerator::BuildRoceEndpoint(3, endpoint), SUCCESS);
  EXPECT_EQ(endpoint.protocol, "roce");
  EXPECT_EQ(endpoint.comm_id, "10.10.10.3");
  EXPECT_EQ(endpoint.placement, "device");

  (void)remove(file_path.c_str());
}

TEST_F(EndpointGeneratorUTest, BuildRoceEndpointEmptyIpFailed) {
  mmpa_stub_->real_path_ok_ = false;
  mmpa_stub_->access_ok_ = false;
  setenv("PATH", "/tmp/loc_comm_res_empty_path", 1);
  mkdir("/tmp/loc_comm_res_empty_path", 0755);

  EndpointGenerator::EndpointInfo endpoint{};
  EXPECT_EQ(EndpointGenerator::BuildRoceEndpoint(3, endpoint), FAILED);
}

TEST_F(EndpointGeneratorUTest, BuildEndpointListSuccess) {
  const std::string file_path =
      test::CreateTempFileWithContent("/tmp/loc_comm_res_ut_XXXXXX", "address_3=10.10.10.3\n");

  mmpa_stub_->real_path_ok_ = true;
  mmpa_stub_->access_ok_ = true;
  mmpa_stub_->fake_real_path_ = file_path;

  std::vector<EndpointGenerator::EndpointInfo> endpoint_list;
  EXPECT_EQ(EndpointGenerator::BuildEndpointList(3, endpoint_list), SUCCESS);
  ASSERT_EQ(endpoint_list.size(), 2U);
  EXPECT_EQ(endpoint_list[0].protocol, "roce");
  EXPECT_EQ(endpoint_list[0].comm_id, "10.10.10.3");
  EXPECT_EQ(endpoint_list[1].protocol, "hccs");
  EXPECT_EQ(endpoint_list[1].comm_id, "3");

  (void)remove(file_path.c_str());
}

TEST_F(EndpointGeneratorUTest, BuildEndpointListWithIntraRoceEnabledOnlyReturnsRoce) {
  const std::string file_path =
      test::CreateTempFileWithContent("/tmp/loc_comm_res_ut_XXXXXX", "address_3=10.10.10.3\n");

  mmpa_stub_->real_path_ok_ = true;
  mmpa_stub_->access_ok_ = true;
  mmpa_stub_->fake_real_path_ = file_path;
  setenv("HCCL_INTRA_ROCE_ENABLE", "1", 1);

  std::vector<EndpointGenerator::EndpointInfo> endpoint_list;
  EXPECT_EQ(EndpointGenerator::BuildEndpointList(3, endpoint_list), SUCCESS);
  ASSERT_EQ(endpoint_list.size(), 1U);
  EXPECT_EQ(endpoint_list[0].protocol, "roce");

  (void)remove(file_path.c_str());
}

TEST_F(EndpointGeneratorUTest, GenerateInfoSuccessForV3) {
  const std::string file_path =
      test::CreateTempFileWithContent("/tmp/loc_comm_res_ut_XXXXXX", "address_3=10.10.10.3\n");

  acl_stub_->soc_name_ = "Ascend910_9391";
  acl_stub_->phy_device_id_ = 3;
  acl_stub_->super_pod_id_ = 88;
  mmpa_stub_->real_path_ok_ = true;
  mmpa_stub_->access_ok_ = true;
  mmpa_stub_->fake_real_path_ = file_path;

  EndpointGenerator::LocCommResInfo info{};
  EXPECT_EQ(EndpointGenerator::GenerateInfo(0, "127.0.0.1:26000", info), SUCCESS);
  EXPECT_EQ(info.version, "1.3");
  EXPECT_EQ(info.net_instance_id, "88");
  ASSERT_EQ(info.endpoint_list.size(), 2U);

  (void)remove(file_path.c_str());
}

TEST_F(EndpointGeneratorUTest, BuildEndpointListFromOptionsParsesManualJsonAndFillsDeviceInfo) {
  acl_stub_->soc_name_ = "Ascend910_9391";
  acl_stub_->device_id_ = 1;
  acl_stub_->phy_device_id_ = 23;
  acl_stub_->super_device_id_ = 45;
  acl_stub_->super_pod_id_ = 67;

  const std::string local_comm_res = R"(
  {
    "net_instance_id": "sp_v3",
    "endpoint_list": [
      {
        "protocol": "roce",
        "comm_id": "127.0.0.1",
        "placement": "host"
      },
      {
        "protocol": "hccs",
        "comm_id": "7",
        "placement": "device"
      }
    ],
    "version": "1.3"
  })";

  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_LOCAL_COMM_RES] = AscendString(local_comm_res.c_str());
  std::string parsed_local_comm_res;
  std::vector<EndpointConfig> endpoint_list;
  EXPECT_EQ(EndpointGenerator::BuildEndpointListFromOptions(options, "127.0.0.1:26000", parsed_local_comm_res,
                                                            endpoint_list),
            SUCCESS);
  EXPECT_EQ(parsed_local_comm_res, local_comm_res);
  ASSERT_EQ(endpoint_list.size(), 2U);
  EXPECT_EQ(endpoint_list[0].placement, kPlacementHost);
  EXPECT_EQ(endpoint_list[0].device_info.phy_device_id, -1);
  EXPECT_EQ(endpoint_list[1].placement, kPlacementDevice);
  EXPECT_EQ(endpoint_list[1].net_instance_id, "sp_v3");
  EXPECT_EQ(endpoint_list[1].device_info.phy_device_id, 23);
  EXPECT_EQ(endpoint_list[1].device_info.super_device_id, 45);
  EXPECT_EQ(endpoint_list[1].device_info.super_pod_id, 67);
}

TEST_F(EndpointGeneratorUTest, BuildEndpointListFromOptionsAutoGeneratesForA2) {
  acl_stub_->soc_name_ = "Ascend910B4-1";
  acl_stub_->device_id_ = 0;
  acl_stub_->phy_device_id_ = 3;

  const std::string file_path =
      test::CreateTempFileWithContent("/tmp/loc_comm_res_ut_XXXXXX", "address_3=10.10.10.3\n");
  mmpa_stub_->real_path_ok_ = true;
  mmpa_stub_->access_ok_ = true;
  mmpa_stub_->fake_real_path_ = file_path;

  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_LOCAL_COMM_RES] = AscendString(R"({"version":"1.3"})");
  std::string local_comm_res;
  std::vector<EndpointConfig> endpoint_list;
  EXPECT_EQ(EndpointGenerator::BuildEndpointListFromOptions(options, "192.168.1.8:26000", local_comm_res,
                                                            endpoint_list),
            SUCCESS);
  EXPECT_EQ(local_comm_res, R"({"version":"1.3"})");
  ASSERT_EQ(endpoint_list.size(), 2U);
  EXPECT_EQ(endpoint_list[0].protocol, kProtocolRoce);
  EXPECT_EQ(endpoint_list[0].comm_id, "10.10.10.3");
  EXPECT_EQ(endpoint_list[0].net_instance_id, "192.168.1.8");
  EXPECT_EQ(endpoint_list[0].device_info.phy_device_id, 3);
  EXPECT_EQ(endpoint_list[0].device_info.super_device_id, -1);
  EXPECT_EQ(endpoint_list[0].device_info.super_pod_id, -1);
  EXPECT_EQ(endpoint_list[1].protocol, kProtocolHccs);
  EXPECT_EQ(endpoint_list[1].comm_id, "3");

  (void)remove(file_path.c_str());
}

TEST_F(EndpointGeneratorUTest, BuildEndpointListFromOptionsAutoGeneratesForA2WithoutVersion) {
  acl_stub_->soc_name_ = "Ascend910B4-1";
  acl_stub_->device_id_ = 0;
  acl_stub_->phy_device_id_ = 3;

  const std::string file_path =
      test::CreateTempFileWithContent("/tmp/loc_comm_res_ut_XXXXXX", "address_3=10.10.10.3\n");
  mmpa_stub_->real_path_ok_ = true;
  mmpa_stub_->access_ok_ = true;
  mmpa_stub_->fake_real_path_ = file_path;

  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_LOCAL_COMM_RES] = AscendString(R"({})");
  std::string local_comm_res;
  std::vector<EndpointConfig> endpoint_list;
  EXPECT_EQ(EndpointGenerator::BuildEndpointListFromOptions(options, "192.168.1.8:26000", local_comm_res,
                                                            endpoint_list),
            SUCCESS);
  EXPECT_EQ(local_comm_res, R"({})");
  ASSERT_EQ(endpoint_list.size(), 2U);
  EXPECT_EQ(endpoint_list[0].protocol, kProtocolRoce);
  EXPECT_EQ(endpoint_list[0].comm_id, "10.10.10.3");
  EXPECT_EQ(endpoint_list[0].net_instance_id, "192.168.1.8");
  EXPECT_EQ(endpoint_list[1].protocol, kProtocolHccs);
  EXPECT_EQ(endpoint_list[1].comm_id, "3");

  (void)remove(file_path.c_str());
}

TEST_F(EndpointGeneratorUTest, BuildEndpointListFromOptionsAutoGeneratesForA2WithoutVersionAndWithNetInstanceId) {
  acl_stub_->soc_name_ = "Ascend910B4-1";
  acl_stub_->device_id_ = 0;
  acl_stub_->phy_device_id_ = 3;

  const std::string file_path =
      test::CreateTempFileWithContent("/tmp/loc_comm_res_ut_XXXXXX", "address_3=10.10.10.3\n");
  mmpa_stub_->real_path_ok_ = true;
  mmpa_stub_->access_ok_ = true;
  mmpa_stub_->fake_real_path_ = file_path;

  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_LOCAL_COMM_RES] = AscendString(R"({"net_instance_id":"manual_input"})");
  std::string local_comm_res;
  std::vector<EndpointConfig> endpoint_list;
  EXPECT_EQ(EndpointGenerator::BuildEndpointListFromOptions(options, "192.168.1.8:26000", local_comm_res,
                                                            endpoint_list),
            SUCCESS);
  EXPECT_EQ(local_comm_res, R"({"net_instance_id":"manual_input"})");
  ASSERT_EQ(endpoint_list.size(), 2U);
  EXPECT_EQ(endpoint_list[0].protocol, kProtocolRoce);
  EXPECT_EQ(endpoint_list[0].comm_id, "10.10.10.3");
  EXPECT_EQ(endpoint_list[0].net_instance_id, "192.168.1.8");
  EXPECT_EQ(endpoint_list[1].protocol, kProtocolHccs);
  EXPECT_EQ(endpoint_list[1].comm_id, "3");

  (void)remove(file_path.c_str());
}

TEST_F(EndpointGeneratorUTest, BuildEndpointListFromOptionsAutoGeneratesForA2RegardlessOfVersionValue) {
  acl_stub_->soc_name_ = "Ascend910B4-1";
  acl_stub_->device_id_ = 0;
  acl_stub_->phy_device_id_ = 3;

  const std::string file_path =
      test::CreateTempFileWithContent("/tmp/loc_comm_res_ut_XXXXXX", "address_3=10.10.10.3\n");
  mmpa_stub_->real_path_ok_ = true;
  mmpa_stub_->access_ok_ = true;
  mmpa_stub_->fake_real_path_ = file_path;

  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_LOCAL_COMM_RES] = AscendString(R"({"version":"legacy"})");
  std::string local_comm_res;
  std::vector<EndpointConfig> endpoint_list;
  EXPECT_EQ(EndpointGenerator::BuildEndpointListFromOptions(options, "192.168.1.8:26000", local_comm_res,
                                                            endpoint_list),
            SUCCESS);
  EXPECT_EQ(local_comm_res, R"({"version":"legacy"})");
  ASSERT_EQ(endpoint_list.size(), 2U);
  EXPECT_EQ(endpoint_list[0].protocol, kProtocolRoce);
  EXPECT_EQ(endpoint_list[0].comm_id, "10.10.10.3");
  EXPECT_EQ(endpoint_list[0].net_instance_id, "192.168.1.8");
  EXPECT_EQ(endpoint_list[1].protocol, kProtocolHccs);
  EXPECT_EQ(endpoint_list[1].comm_id, "3");

  (void)remove(file_path.c_str());
}

TEST_F(EndpointGeneratorUTest, BuildEndpointListFromOptionsAutoGeneratesForA2AndAppendsUboe) {
  acl_stub_->soc_name_ = "Ascend910B4-1";
  acl_stub_->device_id_ = 0;
  acl_stub_->phy_device_id_ = 3;
  llm::MmpaStub::GetInstance().SetImpl(std::make_shared<UboeMmpaStub>());

  const std::string script_path =
      CreateExecutableScript("hccn_tool", "#!/bin/sh\necho \"ipaddr:192.168.100.200\"\n");
  setenv("PATH", "/tmp", 1);

  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_LOCAL_COMM_RES] = AscendString(R"({"version":"1.3"})");
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"(
    {
      "comm_resource_config.protocol_desc": ["uboe:device"]
    }
  )";

  std::string local_comm_res;
  std::vector<EndpointConfig> endpoint_list;
  EXPECT_EQ(EndpointGenerator::BuildEndpointListFromOptions(options, "192.168.1.8:26000", local_comm_res,
                                                            endpoint_list),
            SUCCESS);
  EXPECT_EQ(local_comm_res, R"({"version":"1.3"})");
  ASSERT_EQ(endpoint_list.size(), 3U);
  EXPECT_EQ(endpoint_list[0].protocol, kProtocolRoce);
  EXPECT_EQ(endpoint_list[1].protocol, kProtocolHccs);
  EXPECT_EQ(endpoint_list[2].protocol, kProtocolUboe);
  EXPECT_EQ(endpoint_list[2].comm_id, "192.168.100.200");
  EXPECT_EQ(endpoint_list[2].placement, kPlacementDevice);
  EXPECT_EQ(endpoint_list[2].net_instance_id, "default_superpod1_1");

  (void)remove(script_path.c_str());
}

TEST_F(EndpointGeneratorUTest, BuildEndpointListFromOptionsPrefersHixlOptionOverAdxl) {
  const std::string hixl_local_comm_res =
      R"({"version":"1.3","net_instance_id":"hixl_sp","endpoint_list":[{"protocol":"roce","comm_id":"127.0.0.1","placement":"host"}]})";
  const std::string adxl_local_comm_res =
      R"({"version":"1.3","net_instance_id":"adxl_sp","endpoint_list":[{"protocol":"roce","comm_id":"127.0.0.2","placement":"host"}]})";

  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_LOCAL_COMM_RES] = AscendString(hixl_local_comm_res.c_str());
  options[adxl::OPTION_LOCAL_COMM_RES] = AscendString(adxl_local_comm_res.c_str());

  std::string local_comm_res;
  std::vector<EndpointConfig> endpoint_list;
  EXPECT_EQ(EndpointGenerator::BuildEndpointListFromOptions(options, "127.0.0.1:26000", local_comm_res,
                                                            endpoint_list),
            SUCCESS);
  EXPECT_EQ(local_comm_res, hixl_local_comm_res);
  ASSERT_EQ(endpoint_list.size(), 1U);
  EXPECT_EQ(endpoint_list[0].net_instance_id, "hixl_sp");
  EXPECT_EQ(endpoint_list[0].comm_id, "127.0.0.1");
}

TEST_F(EndpointGeneratorUTest, BuildEndpointListFromOptionsRejectsEmptyLocalCommRes) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_LOCAL_COMM_RES] = AscendString("");

  std::string local_comm_res;
  std::vector<EndpointConfig> endpoint_list;
  EXPECT_EQ(EndpointGenerator::BuildEndpointListFromOptions(options, "127.0.0.1:26000", local_comm_res,
                                                            endpoint_list),
            PARAM_INVALID);
}

TEST_F(EndpointGeneratorUTest, BuildEndpointListFromOptionsAutoGeneratesBaseEndpointsAndUboeWhenLocalCommResMissing) {
  acl_stub_->soc_name_ = "Ascend910_9391";
  acl_stub_->device_id_ = 0;
  acl_stub_->phy_device_id_ = 3;
  acl_stub_->super_pod_id_ = 88;
  auto uboe_mmpa_stub = std::make_shared<UboeMmpaStub>();
  uboe_mmpa_stub->real_path_ok_ = true;
  uboe_mmpa_stub->access_ok_ = true;
  const std::string file_path =
      test::CreateTempFileWithContent("/tmp/loc_comm_res_ut_XXXXXX", "address_3=10.10.10.3\n");
  uboe_mmpa_stub->fake_real_path_ = file_path;
  llm::MmpaStub::GetInstance().SetImpl(uboe_mmpa_stub);
  const std::string script_path = CreateExecutableScript("hccn_tool", "#!/bin/sh\necho \"ipaddr:192.168.100.200\"\n");
  setenv("PATH", "/tmp", 1);

  std::map<AscendString, AscendString> options;
  SetUboeProtocolDescOption(options);

  std::string local_comm_res;
  std::vector<EndpointConfig> endpoint_list;
  EXPECT_EQ(EndpointGenerator::BuildEndpointListFromOptions(options, "127.0.0.1:26000", local_comm_res,
                                                            endpoint_list),
            SUCCESS);
  EXPECT_TRUE(local_comm_res.empty());
  ASSERT_EQ(endpoint_list.size(), 3U);
  EXPECT_EQ(endpoint_list[0].protocol, kProtocolRoce);
  EXPECT_EQ(endpoint_list[0].comm_id, "10.10.10.3");
  EXPECT_EQ(endpoint_list[0].net_instance_id, "88");
  EXPECT_EQ(endpoint_list[1].protocol, kProtocolHccs);
  EXPECT_EQ(endpoint_list[1].comm_id, "3");
  EXPECT_EQ(endpoint_list[1].net_instance_id, "88");
  EXPECT_EQ(endpoint_list[2].protocol, kProtocolUboe);
  EXPECT_EQ(endpoint_list[2].comm_id, "192.168.100.200");
  EXPECT_EQ(endpoint_list[2].placement, kPlacementDevice);
  EXPECT_EQ(endpoint_list[2].net_instance_id, "default_superpod1_1");

  (void)remove(file_path.c_str());
  (void)remove(script_path.c_str());
}

TEST_F(EndpointGeneratorUTest, BuildEndpointListFromOptionsGeneratesOnlyUboeWhenLocalCommResMissingOnOtherSoc) {
  acl_stub_->soc_name_ = "Ascend310P";
  llm::MmpaStub::GetInstance().SetImpl(std::make_shared<UboeMmpaStub>());
  const std::string script_path = CreateExecutableScript("hccn_tool", "#!/bin/sh\necho \"ipaddr:192.168.100.205\"\n");
  setenv("PATH", "/tmp", 1);

  std::map<AscendString, AscendString> options;
  SetUboeProtocolDescOption(options);

  std::string local_comm_res;
  std::vector<EndpointConfig> endpoint_list;
  EXPECT_EQ(EndpointGenerator::BuildEndpointListFromOptions(options, "127.0.0.1:26000", local_comm_res,
                                                            endpoint_list),
            SUCCESS);
  EXPECT_TRUE(local_comm_res.empty());
  ExpectSingleUboeEndpoint(endpoint_list, "192.168.100.205");

  (void)remove(script_path.c_str());
}

TEST_F(EndpointGeneratorUTest, BuildEndpointListFromOptionsDoesNotAppendUboeWhenLocalCommResHasEndpointList) {
  llm::MmpaStub::GetInstance().SetImpl(std::make_shared<UboeMmpaStub>());
  const std::string script_path = CreateExecutableScript("hccn_tool", "#!/bin/sh\necho \"ipaddr:192.168.100.203\"\n");
  setenv("PATH", "/tmp", 1);

  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_LOCAL_COMM_RES] =
      R"({"version":"1.3","net_instance_id":"hixl_sp","endpoint_list":[{"protocol":"roce","comm_id":"127.0.0.1","placement":"host"}]})";
  SetUboeProtocolDescOption(options);

  std::string local_comm_res;
  std::vector<EndpointConfig> endpoint_list;
  EXPECT_EQ(EndpointGenerator::BuildEndpointListFromOptions(options, "127.0.0.1:26000", local_comm_res,
                                                            endpoint_list),
            SUCCESS);
  ASSERT_EQ(endpoint_list.size(), 1U);
  EXPECT_EQ(endpoint_list[0].protocol, kProtocolRoce);
  EXPECT_EQ(endpoint_list[0].comm_id, "127.0.0.1");

  (void)remove(script_path.c_str());
}

TEST_F(EndpointGeneratorUTest, BuildEndpointListFromOptionsGeneratesUboeWhenLocalCommResEndpointListIsEmpty) {
  llm::MmpaStub::GetInstance().SetImpl(std::make_shared<UboeMmpaStub>());
  acl_stub_->soc_name_ = "Ascend910B4-1";
  acl_stub_->device_id_ = 0;
  acl_stub_->phy_device_id_ = 3;
  const std::string script_path = CreateExecutableScript("hccn_tool", "#!/bin/sh\necho \"ipaddr:192.168.100.204\"\n");
  setenv("PATH", "/tmp", 1);
  const std::string file_path =
      test::CreateTempFileWithContent("/tmp/loc_comm_res_ut_XXXXXX", "address_3=10.10.10.3\n");
  mmpa_stub_->real_path_ok_ = true;
  mmpa_stub_->access_ok_ = true;
  mmpa_stub_->fake_real_path_ = file_path;

  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_LOCAL_COMM_RES] = R"(
    {
      "version": "1.3",
      "net_instance_id": "empty_list_case",
      "endpoint_list": []
    }
  )";
  SetUboeProtocolDescOption(options);

  std::string local_comm_res;
  std::vector<EndpointConfig> endpoint_list;
  EXPECT_EQ(EndpointGenerator::BuildEndpointListFromOptions(options, "127.0.0.1:26000", local_comm_res,
                                                            endpoint_list),
            SUCCESS);
  ASSERT_EQ(endpoint_list.size(), 3U);
  EXPECT_EQ(endpoint_list[0].protocol, kProtocolRoce);
  EXPECT_EQ(endpoint_list[0].net_instance_id, "127.0.0.1");
  EXPECT_EQ(endpoint_list[1].protocol, kProtocolHccs);
  EXPECT_EQ(endpoint_list[1].comm_id, "3");
  EXPECT_EQ(endpoint_list[2].protocol, kProtocolUboe);
  EXPECT_EQ(endpoint_list[2].comm_id, "192.168.100.204");
  EXPECT_EQ(endpoint_list[2].net_instance_id, "default_superpod1_1");

  (void)remove(file_path.c_str());
  (void)remove(script_path.c_str());
}

TEST_F(EndpointGeneratorUTest, BuildEndpointListFromOptionsAcceptsMixedProtocolDescWhenUboeFirst) {
  llm::MmpaStub::GetInstance().SetImpl(std::make_shared<UboeMmpaStub>());
  const std::string script_path = CreateExecutableScript("hccn_tool", "#!/bin/sh\necho \"ipaddr:192.168.100.201\"\n");
  setenv("PATH", "/tmp", 1);

  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"(
    {
      "comm_resource_config.protocol_desc": ["uboe:device", "roce:device"]
    }
  )";

  std::string local_comm_res;
  std::vector<EndpointConfig> endpoint_list;
  EXPECT_EQ(EndpointGenerator::BuildEndpointListFromOptions(options, "127.0.0.1:26000", local_comm_res,
                                                            endpoint_list),
            SUCCESS);
  ExpectRoceHccsUboeEndpoints(endpoint_list, "192.168.100.201");

  (void)remove(script_path.c_str());
}

TEST_F(EndpointGeneratorUTest, BuildEndpointListFromOptionsAcceptsMixedProtocolDescWhenUboeNotFirst) {
  llm::MmpaStub::GetInstance().SetImpl(std::make_shared<UboeMmpaStub>());
  const std::string script_path = CreateExecutableScript("hccn_tool", "#!/bin/sh\necho \"ipaddr:192.168.100.202\"\n");
  setenv("PATH", "/tmp", 1);

  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"(
    {
      "comm_resource_config.protocol_desc": ["roce:device", "uboe:device"]
    }
  )";

  std::string local_comm_res;
  std::vector<EndpointConfig> endpoint_list;
  EXPECT_EQ(EndpointGenerator::BuildEndpointListFromOptions(options, "127.0.0.1:26000", local_comm_res,
                                                            endpoint_list),
            SUCCESS);
  ExpectRoceHccsUboeEndpoints(endpoint_list, "192.168.100.202");

  (void)remove(script_path.c_str());
}

TEST_F(EndpointGeneratorUTest, ConvertToEndpointDescDeviceRoceUseDeviceInfoTest) {
  EndpointConfig ep;
  ep.protocol = kProtocolRoce;
  ep.comm_id = "127.0.0.1";
  ep.placement = kPlacementDevice;
  ep.device_info.phy_device_id = 3;
  ep.device_info.super_device_id = 7;
  ep.device_info.super_pod_id = 9;

  EndpointDesc endpoint{};
  Status st = EndpointGenerator::ConvertToEndpointDesc(ep, endpoint, 11U);
  EXPECT_EQ(st, SUCCESS);
  EXPECT_EQ(endpoint.protocol, COMM_PROTOCOL_ROCE);
  EXPECT_EQ(endpoint.loc.locType, ENDPOINT_LOC_TYPE_DEVICE);
  EXPECT_EQ(endpoint.loc.device.devPhyId, 3U);
  EXPECT_EQ(endpoint.loc.device.superDevId, 7U);
  EXPECT_EQ(endpoint.loc.device.superPodIdx, 9U);
  EXPECT_EQ(endpoint.loc.device.serverIdx, 0U);
}

TEST_F(EndpointGeneratorUTest, ConvertToEndpointDescDeviceRoceFallbackPhyIdTest) {
  EndpointConfig ep;
  ep.protocol = kProtocolRoce;
  ep.comm_id = "127.0.0.1";
  ep.placement = kPlacementDevice;
  ep.device_info.phy_device_id = -1;
  ep.device_info.super_device_id = -1;
  ep.device_info.super_pod_id = -1;

  EndpointDesc endpoint{};
  Status st = EndpointGenerator::ConvertToEndpointDesc(ep, endpoint, 15U);
  EXPECT_EQ(st, SUCCESS);
  EXPECT_EQ(endpoint.protocol, COMM_PROTOCOL_ROCE);
  EXPECT_EQ(endpoint.loc.locType, ENDPOINT_LOC_TYPE_DEVICE);
  EXPECT_EQ(endpoint.loc.device.devPhyId, 15U);
  EXPECT_EQ(endpoint.loc.device.superDevId, 0U);
  EXPECT_EQ(endpoint.loc.device.superPodIdx, 0U);
  EXPECT_EQ(endpoint.loc.device.serverIdx, 0U);
}

TEST_F(EndpointGeneratorUTest, ConvertToEndpointDescDeviceRoceInvalidIpTest) {
  EndpointConfig ep;
  ep.protocol = kProtocolRoce;
  ep.comm_id = "not_an_ip";
  ep.placement = kPlacementDevice;

  EndpointDesc endpoint{};
  EXPECT_EQ(EndpointGenerator::ConvertToEndpointDesc(ep, endpoint, 11U), PARAM_INVALID);
}

TEST_F(EndpointGeneratorUTest, ConvertToEndpointDescDeviceUboeUseDeviceInfoTest) {
  EndpointConfig ep;
  ep.protocol = kProtocolUboe;
  ep.comm_id = "127.0.0.2";
  ep.placement = kPlacementDevice;
  ep.device_info.phy_device_id = 4;
  ep.device_info.super_device_id = 6;
  ep.device_info.super_pod_id = 8;

  EndpointDesc endpoint{};
  Status st = EndpointGenerator::ConvertToEndpointDesc(ep, endpoint, 12U);
  EXPECT_EQ(st, SUCCESS);
  EXPECT_EQ(endpoint.protocol, COMM_PROTOCOL_UBOE);
  EXPECT_EQ(endpoint.loc.locType, ENDPOINT_LOC_TYPE_DEVICE);
  EXPECT_EQ(endpoint.loc.device.devPhyId, 4U);
  EXPECT_EQ(endpoint.loc.device.superDevId, 6U);
  EXPECT_EQ(endpoint.loc.device.superPodIdx, 8U);
  EXPECT_EQ(endpoint.loc.device.serverIdx, 0U);
}

TEST_F(EndpointGeneratorUTest, ConvertToEndpointDescDeviceRoceSuperDeviceIdOutOfRangeTest) {
  EndpointConfig ep;
  ep.protocol = kProtocolRoce;
  ep.comm_id = "127.0.0.1";
  ep.placement = kPlacementDevice;
  ep.device_info.phy_device_id = 3;
  ep.device_info.super_device_id = static_cast<int64_t>(std::numeric_limits<uint32_t>::max()) + 1;
  ep.device_info.super_pod_id = 9;

  EndpointDesc endpoint{};
  Status st = EndpointGenerator::ConvertToEndpointDesc(ep, endpoint, 11U);
  EXPECT_EQ(st, PARAM_INVALID);
}

TEST_F(EndpointGeneratorUTest, ConvertToEndpointDescDeviceHccsUseDeviceInfoTest) {
  EndpointConfig ep;
  ep.protocol = kProtocolHccs;
  ep.comm_id = "5";
  ep.placement = kPlacementDevice;
  ep.device_info.phy_device_id = 2;
  ep.device_info.super_device_id = 4;
  ep.device_info.super_pod_id = 8;

  EndpointDesc endpoint{};
  Status st = EndpointGenerator::ConvertToEndpointDesc(ep, endpoint, 10U);
  EXPECT_EQ(st, SUCCESS);
  EXPECT_EQ(endpoint.protocol, COMM_PROTOCOL_HCCS);
  EXPECT_EQ(endpoint.loc.locType, ENDPOINT_LOC_TYPE_DEVICE);
  EXPECT_EQ(endpoint.commAddr.id, 5U);
  EXPECT_EQ(endpoint.loc.device.devPhyId, 2U);
  EXPECT_EQ(endpoint.loc.device.superDevId, 4U);
  EXPECT_EQ(endpoint.loc.device.superPodIdx, 8U);
  EXPECT_EQ(endpoint.loc.device.serverIdx, 0U);
}

TEST_F(EndpointGeneratorUTest, ConvertToEndpointDescDeviceHccsInvalidCommIdTest) {
  EndpointConfig ep;
  ep.protocol = kProtocolHccs;
  ep.comm_id = "abc";
  ep.placement = kPlacementDevice;
  ep.device_info.phy_device_id = 2;
  ep.device_info.super_device_id = 4;
  ep.device_info.super_pod_id = 8;

  EndpointDesc endpoint{};
  EXPECT_EQ(EndpointGenerator::ConvertToEndpointDesc(ep, endpoint, 10U), PARAM_INVALID);
}

TEST_F(EndpointGeneratorUTest, ConvertToEndpointDescDeviceUbParsesEidTest) {
  EndpointConfig ep;
  ep.protocol = kProtocolUbCtp;
  ep.comm_id = "00010002000300040005000600070008";
  ep.placement = kPlacementDevice;
  ep.device_info.phy_device_id = 123;
  ep.device_info.super_device_id = 456;
  ep.device_info.super_pod_id = 789;

  EndpointDesc endpoint{};
  Status st = EndpointGenerator::ConvertToEndpointDesc(ep, endpoint, 6U);
  EXPECT_EQ(st, SUCCESS);
  EXPECT_EQ(endpoint.protocol, COMM_PROTOCOL_UBC_CTP);
  EXPECT_EQ(endpoint.loc.locType, ENDPOINT_LOC_TYPE_DEVICE);
  EXPECT_EQ(endpoint.commAddr.type, COMM_ADDR_TYPE_EID);
  EXPECT_EQ(endpoint.commAddr.eid[0], 0x00);
  EXPECT_EQ(endpoint.commAddr.eid[1], 0x01);
  EXPECT_EQ(endpoint.commAddr.eid[14], 0x00);
  EXPECT_EQ(endpoint.commAddr.eid[15], 0x08);
  EXPECT_EQ(endpoint.loc.device.devPhyId, 6U);
  EXPECT_EQ(endpoint.loc.device.superDevId, 0U);
  EXPECT_EQ(endpoint.loc.device.superPodIdx, 0U);
  EXPECT_EQ(endpoint.loc.device.serverIdx, 0U);
}

TEST_F(EndpointGeneratorUTest, ConvertToEndpointDescDeviceUbParsesMixedCaseEidTest) {
  EndpointConfig ep;
  ep.protocol = kProtocolUbTp;
  ep.comm_id = "aBcD1234567890EfAbCdEf1234567890";
  ep.placement = kPlacementDevice;

  EndpointDesc endpoint{};
  Status st = EndpointGenerator::ConvertToEndpointDesc(ep, endpoint, 9U);
  EXPECT_EQ(st, SUCCESS);
  EXPECT_EQ(endpoint.protocol, COMM_PROTOCOL_UBC_TP);
  EXPECT_EQ(endpoint.commAddr.type, COMM_ADDR_TYPE_EID);
  EXPECT_EQ(endpoint.commAddr.eid[0], 0xAB);
  EXPECT_EQ(endpoint.commAddr.eid[1], 0xCD);
  EXPECT_EQ(endpoint.commAddr.eid[7], 0xEF);
  EXPECT_EQ(endpoint.commAddr.eid[15], 0x90);
}

TEST_F(EndpointGeneratorUTest, ConvertToEndpointDescDeviceUbRejectsShortEidTest) {
  EndpointConfig ep;
  ep.protocol = kProtocolUbCtp;
  ep.comm_id = "0001000200030004000500060007";
  ep.placement = kPlacementDevice;

  EndpointDesc endpoint{};
  EXPECT_EQ(EndpointGenerator::ConvertToEndpointDesc(ep, endpoint, 6U), PARAM_INVALID);
}

TEST_F(EndpointGeneratorUTest, ConvertToEndpointDescDeviceUbRejectsLongEidTest) {
  EndpointConfig ep;
  ep.protocol = kProtocolUbCtp;
  ep.comm_id = "000100020003000400050006000700080009";
  ep.placement = kPlacementDevice;

  EndpointDesc endpoint{};
  EXPECT_EQ(EndpointGenerator::ConvertToEndpointDesc(ep, endpoint, 6U), PARAM_INVALID);
}

TEST_F(EndpointGeneratorUTest, ConvertToEndpointDescDeviceUbRejectsNonHexEidTest) {
  EndpointConfig ep;
  ep.protocol = kProtocolUbCtp;
  ep.comm_id = "0001000200030004000500060007000g";
  ep.placement = kPlacementDevice;

  EndpointDesc endpoint{};
  EXPECT_EQ(EndpointGenerator::ConvertToEndpointDesc(ep, endpoint, 6U), PARAM_INVALID);
}

TEST_F(EndpointGeneratorUTest, ConvertToEndpointDescDeviceUbRejectsEmptyEidTest) {
  EndpointConfig ep;
  ep.protocol = kProtocolUbCtp;
  ep.comm_id = "";
  ep.placement = kPlacementDevice;

  EndpointDesc endpoint{};
  EXPECT_EQ(EndpointGenerator::ConvertToEndpointDesc(ep, endpoint, 6U), PARAM_INVALID);
}

TEST_F(EndpointGeneratorUTest, SerializeAndDeserializeEndpointConfigListSuccess) {
  EndpointConfig ep = endpoint_test::BuildSampleDeviceRoceEndpoint();

  std::string msg_str;
  EXPECT_EQ(EndpointGenerator::SerializeEndpointConfigList(std::vector<EndpointConfig>{ep}, msg_str), SUCCESS);

  std::vector<EndpointConfig> output_list;
  EXPECT_EQ(EndpointGenerator::DeserializeEndpointConfigList(msg_str, output_list), SUCCESS);
  ASSERT_EQ(output_list.size(), 1U);
  EXPECT_EQ(output_list[0].protocol, kProtocolRoce);
  EXPECT_EQ(output_list[0].comm_id, "127.0.0.1");
  EXPECT_EQ(output_list[0].placement, kPlacementDevice);
  EXPECT_EQ(output_list[0].plane, "plane-a");
  EXPECT_EQ(output_list[0].dst_eid, "00010002000300040005000600070008");
  EXPECT_EQ(output_list[0].net_instance_id, "superpod_1");
  EXPECT_EQ(output_list[0].device_info.phy_device_id, 3);
  EXPECT_EQ(output_list[0].device_info.super_device_id, 7);
  EXPECT_EQ(output_list[0].device_info.super_pod_id, 9);
}

TEST_F(EndpointGeneratorUTest, DeserializeOldFormatWithoutDeviceInfoSuccess) {
  const std::string json_str = endpoint_test::BuildLegacyEndpointListJson();

  std::vector<EndpointConfig> endpoint_list;
  EXPECT_EQ(EndpointGenerator::DeserializeEndpointConfigList(json_str, endpoint_list), SUCCESS);
  ASSERT_EQ(endpoint_list.size(), 1U);
  EXPECT_EQ(endpoint_list[0].device_info.phy_device_id, -1);
  EXPECT_EQ(endpoint_list[0].device_info.super_device_id, -1);
  EXPECT_EQ(endpoint_list[0].device_info.super_pod_id, -1);
}

TEST_F(EndpointGeneratorUTest, GetDeviceIpFromHccnToolSuccess) {
  mmpa_stub_->real_path_ok_ = false;
  mmpa_stub_->access_ok_ = false;

  const std::string tool_path = CreateExecutableScript(
      "hccn_tool",
      "#!/bin/sh\n"
      "echo \"ipaddr:172.16.1.20\"\n");

  setenv("PATH", "/tmp", 1);

  std::string device_ip;
  EXPECT_EQ(EndpointGenerator::GetDeviceIp(3, device_ip), SUCCESS);
  EXPECT_EQ(device_ip, "172.16.1.20");

  (void)remove(tool_path.c_str());
}

TEST_F(EndpointGeneratorUTest, GetDeviceIpFromHccnToolInvalidIpFailed) {
  mmpa_stub_->real_path_ok_ = false;
  mmpa_stub_->access_ok_ = false;

  const std::string tool_path = CreateExecutableScript(
      "hccn_tool",
      "#!/bin/sh\n"
      "echo \"ipaddr:not_an_ip\"\n");

  setenv("PATH", "/tmp", 1);

  std::string device_ip;
  EXPECT_EQ(EndpointGenerator::GetDeviceIp(3, device_ip), PARAM_INVALID);

  (void)remove(tool_path.c_str());
}

TEST_F(EndpointGeneratorUTest, GetDeviceIpFromHccnToolEmptyIpFailed) {
  mmpa_stub_->real_path_ok_ = false;
  mmpa_stub_->access_ok_ = false;

  const std::string tool_path = CreateExecutableScript(
      "hccn_tool",
      "#!/bin/sh\n"
      "echo \"no_ip_here\"\n");

  setenv("PATH", "/tmp", 1);

  std::string device_ip;
  EXPECT_EQ(EndpointGenerator::GetDeviceIp(3, device_ip), FAILED);

  (void)remove(tool_path.c_str());
}
}  // namespace hixl
