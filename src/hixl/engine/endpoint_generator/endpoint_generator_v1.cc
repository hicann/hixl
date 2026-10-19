/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "endpoint_generator_v1.h"

#include "acl/acl.h"
#include "acl/acl_rt.h"
#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/soc_type.h"
#include "endpoint_generator.h"

namespace hixl {
namespace {
constexpr const char kConfigVersion[] = "1.3";
}  // namespace

Status EndpointGeneratorV1::AutoGen(const HixlOptions &options, const std::string &local_engine,
                                    std::vector<EndpointConfig> &endpoint_list) const {
  int32_t device_id = 0;
  HIXL_CHK_ACL_RET(aclrtGetDevice(&device_id));
  LocCommResInfo loc_comm_res_info{};
  HIXL_CHK_STATUS_RET(GenerateInfo(device_id, local_engine, options.GetProtocolDesc(), loc_comm_res_info),
                      "GenerateInfo failed");
  ConvertLocCommResInfoToEndpointList(loc_comm_res_info, endpoint_list);
  HIXL_CHK_STATUS_RET(EndpointGenerator::FilterEndpointListByProtocolDesc(options, endpoint_list),
                      "FilterEndpointListByProtocolDesc failed");
  return SUCCESS;
}

Status EndpointGeneratorV1::GenerateInfo(int32_t device_id, const std::string &local_engine,
                                         const std::vector<std::string> &protocol_desc,
                                         EndpointGeneratorV1::LocCommResInfo &loc_comm_res_info) {
  int32_t phy_device_id = 0;
  HIXL_CHK_ACL_RET(aclrtGetPhyDevIdByLogicDevId(device_id, &phy_device_id),
                   "device_id:%d, failed to get physical device id", device_id);

  loc_comm_res_info = {};
  loc_comm_res_info.version = kConfigVersion;

  HIXL_CHK_STATUS_RET(BuildNetInstanceId(device_id, local_engine, loc_comm_res_info.net_instance_id),
                      "BuildNetInstanceId failed, device_id:%d, local_engine:%s", device_id, local_engine.c_str());
  HIXL_CHK_STATUS_RET(EndpointGenerator::BuildDefaultDeviceEndpointInfoList(phy_device_id, protocol_desc,
                                                                            loc_comm_res_info.endpoint_list),
                      "BuildDefaultDeviceEndpointInfoList failed, phy_device_id:%d", phy_device_id);

  return SUCCESS;
}

void EndpointGeneratorV1::ConvertLocCommResInfoToEndpointList(
    const EndpointGeneratorV1::LocCommResInfo &loc_comm_res_info, std::vector<EndpointConfig> &endpoint_list) {
  endpoint_list.clear();
  for (const auto &ep_info : loc_comm_res_info.endpoint_list) {
    EndpointConfig ep;
    ep.protocol = ep_info.protocol;
    ep.comm_id = ep_info.comm_id;
    ep.placement = ep_info.placement;
    ep.net_instance_id = loc_comm_res_info.net_instance_id;
    endpoint_list.emplace_back(ep);
  }
}

Status EndpointGeneratorV1::BuildNetInstanceId(int32_t device_id, const std::string &local_engine,
                                               std::string &net_instance_id) {
  SocType soc_type = SocType::kDefault;
  HIXL_CHK_STATUS_RET(SocTypeUtil::GetSocType(soc_type), "BuildNetInstanceId GetSocType failed");

  if (soc_type == SocType::kGen2) {
    int64_t super_pod_id = 0;
    HIXL_CHK_ACL_RET(aclrtGetDeviceInfo(static_cast<uint32_t>(device_id), ACL_DEV_ATTR_SUPER_POD_ID, &super_pod_id),
                     "device_id:%d, failed to get super pod id", device_id);
    net_instance_id = std::to_string(super_pod_id);
    return SUCCESS;
  }

  HIXL_CHK_BOOL_RET_STATUS(soc_type == SocType::kGen1, PARAM_INVALID,
                           "Unsupported soc_type:%d for auto-generated net_instance_id",
                           static_cast<int32_t>(soc_type));

  return EndpointGenerator::GetHostIpFromLocalEngine(local_engine, net_instance_id);
}

}  // namespace hixl
