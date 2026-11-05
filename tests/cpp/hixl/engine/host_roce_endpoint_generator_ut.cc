/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <unistd.h>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "gtest/gtest.h"

#include "acl/acl.h"
#include "ascendcl_stub.h"
#include "depends/dcmi/src/dcmi_stub.h"
#include "depends/ibverbs/src/ibverbs_stub.h"
#include "engine/endpoint_generator/endpoint_generator.h"
#include "engine/endpoint_generator/host_roce_endpoint_generator.h"
#include "engine/hixl_options.h"
#include "hixl/hixl_types.h"
#include "test_mmpa_utils.h"

namespace hixl {
namespace {

std::string WriteTempJson(const std::string &content) {
  return test::CreateTempFileWithContent("/tmp/hixl_nic_topo_XXXXXX", content);
}

HixlOptions ParseGlobalConfig(const std::string &json) {
  std::map<AscendString, AscendString> options;
  options[OPTION_GLOBAL_RESOURCE_CONFIG] = AscendString(json.c_str());
  HixlOptions parsed;
  EXPECT_EQ(HixlOptions::Parse(options, parsed), SUCCESS);
  return parsed;
}

class ScopedAclRuntimeStub {
 public:
  explicit ScopedAclRuntimeStub(const std::shared_ptr<llm::AclRuntimeStub> &stub) {
    llm::AclRuntimeStub::SetInstance(stub);
  }
  ~ScopedAclRuntimeStub() {
    llm::AclRuntimeStub::Reset();
  }

  ScopedAclRuntimeStub(const ScopedAclRuntimeStub &) = delete;
  ScopedAclRuntimeStub &operator=(const ScopedAclRuntimeStub &) = delete;
};

// aclrtGetDevice 返回 user id 1，logic id 为 7。
class OffsetLogicIdAclStub : public llm::AclRuntimeStub {
 public:
  aclError aclrtGetDevice(int32_t *deviceId) override {
    if (deviceId == nullptr) {
      return ACL_ERROR_INVALID_PARAM;
    }
    *deviceId = 1;
    return ACL_ERROR_NONE;
  }

  aclError aclrtGetLogicDevIdByUserDevId(const int32_t userDevid, int32_t *const logicDevId) override {
    if (logicDevId == nullptr) {
      return ACL_ERROR_INVALID_PARAM;
    }
    *logicDevId = userDevid + 6;
    return ACL_ERROR_NONE;
  }
};

class LogicIdAclStub : public llm::AclRuntimeStub {
 public:
  explicit LogicIdAclStub(int32_t logic_id) : logic_id_(logic_id) {}

  aclError aclrtGetLogicDevIdByUserDevId(const int32_t userDevid, int32_t *const logicDevId) override {
    (void)userDevid;
    if (logicDevId == nullptr) {
      return ACL_ERROR_INVALID_PARAM;
    }
    *logicDevId = logic_id_;
    return ACL_ERROR_NONE;
  }

 private:
  int32_t logic_id_;
};

class FailLogicIdAclStub : public llm::AclRuntimeStub {
 public:
  aclError aclrtGetLogicDevIdByUserDevId(const int32_t userDevid, int32_t *const logicDevId) override {
    (void)userDevid;
    (void)logicDevId;
    return ACL_ERROR_RT_INTERNAL_ERROR;
  }
};

class HostRoceEndpointGeneratorUTest : public ::testing::Test {
 protected:
  void SetUp() override {
    unsetenv("HCCL_INTRA_ROCE_ENABLE");
    IbverbsStubReset();
    DcmiStubResetNicTopo();
    DcmiStubSetSuperPodId(0, 0);
  }

  void TearDown() override {
    IbverbsStubReset();
    DcmiStubResetNicTopo();
  }
};

TEST_F(HostRoceEndpointGeneratorUTest, GenerateAcceptsMultiNpuTopoFile) {
  const std::string path = WriteTempJson(R"({"npu:0":[["mlx0"],[]],"npu:1":[["mlx1"],[]]})");
  ASSERT_FALSE(path.empty());
  IbverbsStubAddIpv4Device("mlx0", 10, 1, 2, 3);
  DcmiStubSetSuperPodId(7, 0);
  const std::string grc = std::string(R"({"comm_resource_config.protocol_desc":["roce:host"],)") +
                          R"("comm_resource_config.nic_topo_path":")" + path + R"("})";
  HixlOptions options = ParseGlobalConfig(grc);
  std::vector<EndpointConfig> endpoint_list;
  EXPECT_EQ(HostRoceEndpointGenerator::GenerateHostRoceEndpoints(options, endpoint_list), SUCCESS);
  ASSERT_EQ(endpoint_list.size(), 1U);
  EXPECT_EQ(endpoint_list[0].comm_id, "10.1.2.3");
  unlink(path.c_str());
}

TEST_F(HostRoceEndpointGeneratorUTest, GenerateRejectsMultiplePreferred) {
  const std::string path = WriteTempJson(R"({"npu:0":[["mlx0","mlx1"],[]]})");
  ASSERT_FALSE(path.empty());
  const std::string grc = std::string(R"({"comm_resource_config.protocol_desc":["roce:host"],)") +
                          R"("comm_resource_config.nic_topo_path":")" + path + R"("})";
  HixlOptions options = ParseGlobalConfig(grc);
  std::vector<EndpointConfig> endpoint_list;
  EXPECT_EQ(HostRoceEndpointGenerator::GenerateHostRoceEndpoints(options, endpoint_list), PARAM_INVALID);
  EXPECT_TRUE(endpoint_list.empty());
  unlink(path.c_str());
}

TEST_F(HostRoceEndpointGeneratorUTest, GenerateRejectsNonEmptyAvail) {
  const std::string path = WriteTempJson(R"({"npu:0":[["mlx0"],["mlx2"]]})");
  ASSERT_FALSE(path.empty());
  const std::string grc = std::string(R"({"comm_resource_config.protocol_desc":["roce:host"],)") +
                          R"("comm_resource_config.nic_topo_path":")" + path + R"("})";
  HixlOptions options = ParseGlobalConfig(grc);
  std::vector<EndpointConfig> endpoint_list;
  EXPECT_EQ(HostRoceEndpointGenerator::GenerateHostRoceEndpoints(options, endpoint_list), PARAM_INVALID);
  EXPECT_TRUE(endpoint_list.empty());
  unlink(path.c_str());
}

TEST_F(HostRoceEndpointGeneratorUTest, GenerateRejectsEmptyPreferred) {
  const std::string path = WriteTempJson(R"({"npu:0":[[],[]]})");
  ASSERT_FALSE(path.empty());
  const std::string grc = std::string(R"({"comm_resource_config.protocol_desc":["roce:host"],)") +
                          R"("comm_resource_config.nic_topo_path":")" + path + R"("})";
  HixlOptions options = ParseGlobalConfig(grc);
  std::vector<EndpointConfig> endpoint_list;
  EXPECT_EQ(HostRoceEndpointGenerator::GenerateHostRoceEndpoints(options, endpoint_list), PARAM_INVALID);
  EXPECT_TRUE(endpoint_list.empty());
  unlink(path.c_str());
}

TEST_F(HostRoceEndpointGeneratorUTest, GenerateRejectsTooLongNicName) {
  const std::string nic(64, 'a');
  const std::string path = WriteTempJson(std::string(R"({"npu:0":[[")") + nic + R"("],[]]})");
  ASSERT_FALSE(path.empty());
  const std::string grc = std::string(R"({"comm_resource_config.protocol_desc":["roce:host"],)") +
                          R"("comm_resource_config.nic_topo_path":")" + path + R"("})";
  HixlOptions options = ParseGlobalConfig(grc);
  std::vector<EndpointConfig> endpoint_list;
  EXPECT_EQ(HostRoceEndpointGenerator::GenerateHostRoceEndpoints(options, endpoint_list), PARAM_INVALID);
  EXPECT_TRUE(endpoint_list.empty());
  unlink(path.c_str());
}

TEST_F(HostRoceEndpointGeneratorUTest, ManualTopoGeneratesHostRoceEdge) {
  const std::string path = WriteTempJson(R"({"npu:0":[["mlx0"],[]]})");
  ASSERT_FALSE(path.empty());
  IbverbsStubAddIpv4Device("mlx0", 10, 1, 2, 3);
  DcmiStubSetSuperPodId(7, 0);
  const std::string grc = std::string(R"({"comm_resource_config.protocol_desc":["roce:host"],)") +
                          R"("comm_resource_config.nic_topo_path":")" + path + R"("})";
  HixlOptions options = ParseGlobalConfig(grc);
  std::vector<EndpointConfig> endpoint_list;
  EXPECT_EQ(HostRoceEndpointGenerator::GenerateHostRoceEndpoints(options, endpoint_list), SUCCESS);
  ASSERT_EQ(endpoint_list.size(), 1U);
  EXPECT_EQ(endpoint_list[0].protocol, kProtocolRoce);
  EXPECT_EQ(endpoint_list[0].placement, kPlacementHost);
  EXPECT_EQ(endpoint_list[0].comm_id, "10.1.2.3");
  EXPECT_EQ(endpoint_list[0].net_instance_id, "superpod_7");
  unlink(path.c_str());
}

TEST_F(HostRoceEndpointGeneratorUTest, StrictMissingNpuReturnsParamInvalid) {
  const std::string path = WriteTempJson(R"({"npu:1":[["mlx0"],[]]})");
  ASSERT_FALSE(path.empty());
  const std::string grc = std::string(R"({"comm_resource_config.protocol_desc":["roce:host"],)") +
                          R"("comm_resource_config.nic_topo_path":")" + path + R"("})";
  HixlOptions options = ParseGlobalConfig(grc);
  std::vector<EndpointConfig> endpoint_list;
  EXPECT_EQ(HostRoceEndpointGenerator::GenerateHostRoceEndpoints(options, endpoint_list), PARAM_INVALID);
  EXPECT_TRUE(endpoint_list.empty());
  unlink(path.c_str());
}

TEST_F(HostRoceEndpointGeneratorUTest, GenerateUsesNicTopoEvenWithoutProtocolDesc) {
  const std::string path = WriteTempJson(R"({"npu:0":[["mlx0"],[]]})");
  ASSERT_FALSE(path.empty());
  IbverbsStubAddIpv4Device("mlx0", 10, 1, 2, 3);
  DcmiStubSetSuperPodId(7, 0);
  const std::string grc = std::string(R"({"comm_resource_config.nic_topo_path":")") + path + R"("})";
  HixlOptions options = ParseGlobalConfig(grc);
  std::vector<EndpointConfig> endpoint_list;
  EXPECT_EQ(HostRoceEndpointGenerator::GenerateHostRoceEndpoints(options, endpoint_list), SUCCESS);
  ASSERT_EQ(endpoint_list.size(), 1U);
  EXPECT_EQ(endpoint_list[0].comm_id, "10.1.2.3");
  unlink(path.c_str());
}

TEST_F(HostRoceEndpointGeneratorUTest, GenerateWithoutNicFailsWhenIbverbsListEmpty) {
  HixlOptions options = ParseGlobalConfig(R"({})");
  std::vector<EndpointConfig> endpoint_list;
  EXPECT_EQ(HostRoceEndpointGenerator::GenerateHostRoceEndpoints(options, endpoint_list), PARAM_INVALID);
  EXPECT_TRUE(endpoint_list.empty());
}

TEST_F(HostRoceEndpointGeneratorUTest, StrictRoceHostFailsWhenDcmiNotSupport) {
  IbverbsStubAddIpv4Device("mlx0", 10, 0, 0, 1);
  HixlOptions options = ParseGlobalConfig(R"({"comm_resource_config.protocol_desc":["roce:host"]})");
  std::vector<EndpointConfig> endpoint_list;
  EXPECT_EQ(HostRoceEndpointGenerator::GenerateHostRoceEndpoints(options, endpoint_list), PARAM_INVALID);
  EXPECT_TRUE(endpoint_list.empty());
}

TEST_F(HostRoceEndpointGeneratorUTest, DcmiAffinityQueriesLogicId) {
  ScopedAclRuntimeStub acl_stub(std::make_shared<OffsetLogicIdAclStub>());
  IbverbsStubAddIpv4Device("mlx0", 10, 0, 0, 1);
  DcmiStubSetNicTopo(0, 8);
  DcmiStubSetSuperPodId(3, 0);
  HixlOptions options = ParseGlobalConfig(R"({"comm_resource_config.protocol_desc":["roce:host"]})");
  std::vector<EndpointConfig> endpoint_list;
  EXPECT_EQ(HostRoceEndpointGenerator::GenerateHostRoceEndpoints(options, endpoint_list), SUCCESS);
  ASSERT_EQ(endpoint_list.size(), 1U);
  EXPECT_EQ(endpoint_list[0].net_instance_id, "superpod_3");
  EXPECT_EQ(DcmiStubLastNicTopoDeviceId(), 7);
  EXPECT_EQ(DcmiStubLastNicTopoNameLen(), 4);
}

TEST_F(HostRoceEndpointGeneratorUTest, ManualTopoStillSelectsByUserDeviceId) {
  ScopedAclRuntimeStub acl_stub(std::make_shared<OffsetLogicIdAclStub>());
  const std::string path = WriteTempJson(R"({"npu:1":[["mlx0"],[]]})");
  ASSERT_FALSE(path.empty());
  IbverbsStubAddIpv4Device("mlx0", 10, 1, 2, 3);
  DcmiStubSetSuperPodId(7, 0);
  const std::string grc = std::string(R"({"comm_resource_config.protocol_desc":["roce:host"],)") +
                          R"("comm_resource_config.nic_topo_path":")" + path + R"("})";
  HixlOptions options = ParseGlobalConfig(grc);
  std::vector<EndpointConfig> endpoint_list;
  EXPECT_EQ(HostRoceEndpointGenerator::GenerateHostRoceEndpoints(options, endpoint_list), SUCCESS);
  ASSERT_EQ(endpoint_list.size(), 1U);
  EXPECT_EQ(endpoint_list[0].comm_id, "10.1.2.3");
  EXPECT_EQ(DcmiStubLastNicTopoDeviceId(), -1);
  unlink(path.c_str());
}

TEST_F(HostRoceEndpointGeneratorUTest, GenerateFailsWhenLogicIdLookupFails) {
  ScopedAclRuntimeStub acl_stub(std::make_shared<FailLogicIdAclStub>());
  HixlOptions options = ParseGlobalConfig(R"({"comm_resource_config.protocol_desc":["roce:host"]})");
  std::vector<EndpointConfig> endpoint_list;
  EXPECT_EQ(HostRoceEndpointGenerator::GenerateHostRoceEndpoints(options, endpoint_list), PARAM_INVALID);
  EXPECT_TRUE(endpoint_list.empty());
  EXPECT_EQ(DcmiStubLastNicTopoDeviceId(), -1);
}

TEST_F(HostRoceEndpointGeneratorUTest, DcmiAffineNicsAreSorted) {
  IbverbsStubAddIpv4Device("zed0", 10, 0, 0, 2);
  IbverbsStubAddIpv4Device("abc0", 10, 0, 0, 1);
  DcmiStubSetNicTopo(0, 8);
  DcmiStubSetSuperPodId(1, 0);
  ScopedAclRuntimeStub acl_stub(std::make_shared<LogicIdAclStub>(0));
  HixlOptions options = ParseGlobalConfig(R"({"comm_resource_config.protocol_desc":["roce:host"]})");
  std::vector<EndpointConfig> endpoint_list;
  EXPECT_EQ(HostRoceEndpointGenerator::GenerateHostRoceEndpoints(options, endpoint_list), SUCCESS);
  ASSERT_EQ(endpoint_list.size(), 1U);
  EXPECT_EQ(endpoint_list[0].comm_id, "10.0.0.1");
  EXPECT_EQ(endpoint_list[0].net_instance_id, "superpod_1");
}

TEST_F(HostRoceEndpointGeneratorUTest, DcmiSelectsNicByLogicIdModulo) {
  for (int32_t i = 0; i < 8; ++i) {
    const std::string name = "hrn5_" + std::to_string(i);
    IbverbsStubAddIpv4Device(name.c_str(), 10, 0, 0, static_cast<uint8_t>(i));
  }
  DcmiStubSetNicTopo(0, 8);
  DcmiStubSetSuperPodId(1, 0);
  ScopedAclRuntimeStub acl_stub(std::make_shared<LogicIdAclStub>(6));
  HixlOptions options = ParseGlobalConfig(R"({"comm_resource_config.protocol_desc":["roce:host"]})");
  std::vector<EndpointConfig> endpoint_list;
  EXPECT_EQ(HostRoceEndpointGenerator::GenerateHostRoceEndpoints(options, endpoint_list), SUCCESS);
  ASSERT_EQ(endpoint_list.size(), 1U);
  EXPECT_EQ(endpoint_list[0].comm_id, "10.0.0.6");
}

TEST_F(HostRoceEndpointGeneratorUTest, DcmiModuloSkipsAffineNicWithoutIpv4) {
  IbverbsStubAddIpv4Device("hrn5_0", 10, 0, 0, 1);
  IbverbsStubAddNameOnly("hrn5_1");
  IbverbsStubAddIpv4Device("hrn5_2", 10, 0, 0, 2);
  DcmiStubSetNicTopo(0, 8);
  DcmiStubSetSuperPodId(1, 0);
  ScopedAclRuntimeStub acl_stub(std::make_shared<LogicIdAclStub>(1));
  HixlOptions options = ParseGlobalConfig(R"({"comm_resource_config.protocol_desc":["roce:host"]})");
  std::vector<EndpointConfig> endpoint_list;
  EXPECT_EQ(HostRoceEndpointGenerator::GenerateHostRoceEndpoints(options, endpoint_list), SUCCESS);
  ASSERT_EQ(endpoint_list.size(), 1U);
  EXPECT_EQ(endpoint_list[0].comm_id, "10.0.0.2");
}

TEST_F(HostRoceEndpointGeneratorUTest, AutoGenKeepsOnlyHostRoceWhenProtocolIsRoceHost) {
  const std::string path = WriteTempJson(R"({"npu:0":[["mlx0"],[]]})");
  ASSERT_FALSE(path.empty());
  IbverbsStubAddIpv4Device("mlx0", 192, 168, 1, 10);
  DcmiStubSetSuperPodId(4, 0);
  const std::string grc = std::string(R"({"comm_resource_config.protocol_desc":["roce:host"],)") +
                          R"("comm_resource_config.nic_topo_path":")" + path + R"("})";
  HixlOptions options = ParseGlobalConfig(grc);
  std::vector<EndpointConfig> endpoint_list;
  EXPECT_EQ(EndpointGenerator::AutoGenEndpointList(options, "", endpoint_list), SUCCESS);
  ASSERT_EQ(endpoint_list.size(), 1U);
  EXPECT_EQ(endpoint_list[0].protocol, kProtocolRoce);
  EXPECT_EQ(endpoint_list[0].placement, kPlacementHost);
  EXPECT_EQ(endpoint_list[0].comm_id, "192.168.1.10");
  unlink(path.c_str());
}

TEST_F(HostRoceEndpointGeneratorUTest, AutoGenReturnsHostRoceErrorWhenRoceHostFails) {
  HixlOptions options = ParseGlobalConfig(R"({"comm_resource_config.protocol_desc":["roce:host"]})");
  std::vector<EndpointConfig> endpoint_list;
  EXPECT_EQ(EndpointGenerator::AutoGenEndpointList(options, "", endpoint_list), PARAM_INVALID);
}

TEST_F(HostRoceEndpointGeneratorUTest, GenerateWithoutNicFailsForUbCtpProtocol) {
  HixlOptions options = ParseGlobalConfig(R"({"comm_resource_config.protocol_desc":["ub_ctp:device"]})");
  std::vector<EndpointConfig> endpoint_list;
  EXPECT_EQ(HostRoceEndpointGenerator::GenerateHostRoceEndpoints(options, endpoint_list), PARAM_INVALID);
  EXPECT_TRUE(endpoint_list.empty());
}

TEST_F(HostRoceEndpointGeneratorUTest, GenerateEntersHostRoceWhenMixedWithUbCtp) {
  HixlOptions options = ParseGlobalConfig(R"({"comm_resource_config.protocol_desc":["ub_ctp:device","roce:host"]})");
  std::vector<EndpointConfig> endpoint_list;
  EXPECT_EQ(HostRoceEndpointGenerator::GenerateHostRoceEndpoints(options, endpoint_list), PARAM_INVALID);
  EXPECT_TRUE(endpoint_list.empty());
}

TEST_F(HostRoceEndpointGeneratorUTest, NonUbAffineNicIsSkipped) {
  IbverbsStubAddIpv4Device("mlx0", 10, 0, 0, 1);
  DcmiStubSetNicTopo(0, 1);
  HixlOptions options = ParseGlobalConfig(R"({"comm_resource_config.protocol_desc":["roce:host"]})");
  std::vector<EndpointConfig> endpoint_list;
  EXPECT_EQ(HostRoceEndpointGenerator::GenerateHostRoceEndpoints(options, endpoint_list), PARAM_INVALID);
  EXPECT_TRUE(endpoint_list.empty());
}

TEST_F(HostRoceEndpointGeneratorUTest, GenerateUsesNicTopoWhenProtocolIsUbCtp) {
  const std::string path = WriteTempJson(R"({"npu:0":[["mlx0"],[]]})");
  ASSERT_FALSE(path.empty());
  IbverbsStubAddIpv4Device("mlx0", 10, 1, 2, 3);
  DcmiStubSetSuperPodId(4, 0);
  const std::string grc = std::string(R"({"comm_resource_config.protocol_desc":["ub_ctp:device"],)") +
                          R"("comm_resource_config.nic_topo_path":")" + path + R"("})";
  HixlOptions options = ParseGlobalConfig(grc);
  std::vector<EndpointConfig> endpoint_list;
  EXPECT_EQ(HostRoceEndpointGenerator::GenerateHostRoceEndpoints(options, endpoint_list), SUCCESS);
  ASSERT_EQ(endpoint_list.size(), 1U);
  EXPECT_EQ(endpoint_list[0].placement, kPlacementHost);
  unlink(path.c_str());
}

}  // namespace
}  // namespace hixl
