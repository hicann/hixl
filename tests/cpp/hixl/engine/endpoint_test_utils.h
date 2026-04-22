/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_TESTS_CPP_HIXL_ENGINE_ENDPOINT_TEST_UTILS_H_
#define CANN_HIXL_TESTS_CPP_HIXL_ENGINE_ENDPOINT_TEST_UTILS_H_

#include <memory>
#include <sstream>
#include <string>

#include "ascendcl_stub.h"
#include "common/hixl_inner_types.h"

namespace hixl {
namespace endpoint_test {
class MockAclRuntimeStub : public llm::AclRuntimeStub {
 public:
  std::string soc_name_ = "Ascend910B1";
  int32_t device_id_ = 0;
  int32_t phy_device_id_ = 0;
  int64_t super_device_id_ = 9;
  int64_t super_pod_id_ = 8;
  bool return_null_soc_name_ = false;
  bool phy_dev_failed_ = false;
  bool device_info_failed_ = false;

  const char *aclrtGetSocName() override {
    if (return_null_soc_name_) {
      return nullptr;
    }
    return soc_name_.c_str();
  }

  aclError aclrtGetDevice(int32_t *deviceId) override {
    if (deviceId == nullptr) {
      return ACL_ERROR_FAILURE;
    }
    *deviceId = device_id_;
    return ACL_SUCCESS;
  }

  aclError aclrtGetPhyDevIdByLogicDevId(const int32_t logicDevId, int32_t *const phyDevId) override {
    (void)logicDevId;
    if (phy_dev_failed_ || phyDevId == nullptr) {
      return ACL_ERROR_FAILURE;
    }
    *phyDevId = phy_device_id_;
    return ACL_SUCCESS;
  }

  aclError aclrtGetDeviceInfo(uint32_t deviceId, aclrtDevAttr attr, int64_t *value) override {
    (void)deviceId;
    if (device_info_failed_ || value == nullptr) {
      return ACL_ERROR_FAILURE;
    }
    if (attr == ACL_DEV_ATTR_SUPER_POD_ID) {
      *value = super_pod_id_;
      return ACL_SUCCESS;
    }
    if (attr == ACL_DEV_ATTR_SUPER_POD_DEVIDE_ID) {
      *value = super_device_id_;
      return ACL_SUCCESS;
    }
    *value = 0;
    return ACL_SUCCESS;
  }
};

inline std::shared_ptr<MockAclRuntimeStub> CreateAclRuntimeStub(const std::string &soc_name, int32_t device_id,
                                                                int32_t phy_device_id, int64_t super_device_id,
                                                                int64_t super_pod_id) {
  auto stub = std::make_shared<MockAclRuntimeStub>();
  stub->soc_name_ = soc_name;
  stub->device_id_ = device_id;
  stub->phy_device_id_ = phy_device_id;
  stub->super_device_id_ = super_device_id;
  stub->super_pod_id_ = super_pod_id;
  return stub;
}

inline EndpointConfig BuildSampleDeviceRoceEndpoint(const std::string &net_instance_id = "superpod_1") {
  EndpointConfig ep{};
  ep.protocol = kProtocolRoce;
  ep.comm_id = "127.0.0.1";
  ep.placement = kPlacementDevice;
  ep.plane = "plane-a";
  ep.dst_eid = "00010002000300040005000600070008";
  ep.net_instance_id = net_instance_id;
  ep.device_info.phy_device_id = 3;
  ep.device_info.super_device_id = 7;
  ep.device_info.super_pod_id = 9;
  return ep;
}

inline std::string BuildLegacyEndpointListJson(const std::string &net_instance_id = "superpod_legacy") {
  std::ostringstream oss;
  oss << "[\n";
  oss << "  {\n";
  oss << "    \"protocol\": \"roce\",\n";
  oss << "    \"comm_id\": \"127.0.0.1\",\n";
  oss << "    \"placement\": \"device\",\n";
  oss << "    \"plane\": \"plane-a\",\n";
  oss << "    \"dst_eid\": \"00010002000300040005000600070008\",\n";
  oss << "    \"net_instance_id\": \"" << net_instance_id << "\"\n";
  oss << "  }\n";
  oss << "]";
  return oss.str();
}
}  // namespace endpoint_test
}  // namespace hixl

#endif  // CANN_HIXL_TESTS_CPP_HIXL_ENGINE_ENDPOINT_TEST_UTILS_H_
