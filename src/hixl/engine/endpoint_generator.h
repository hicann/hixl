/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_ENGINE_ENDPOINT_GENERATOR_H_
#define CANN_HIXL_SRC_HIXL_ENGINE_ENDPOINT_GENERATOR_H_

#include <map>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"
#include "cs/hixl_cs.h"
#include "hixl/hixl_types.h"
#include "common/hixl_inner_types.h"

namespace hixl {
class EndpointGenerator {
 public:
  static Status BuildEndpointListFromOptions(const std::map<AscendString, AscendString> &options,
                                             const std::string &local_engine,
                                             std::string &local_comm_res,
                                             std::vector<EndpointConfig> &endpoint_list);
  static Status ConvertToEndpointDesc(const EndpointConfig &endpoint_config,
                                      EndpointDesc &endpoint,
                                      uint32_t dev_phy_id = 0);
  static Status SerializeEndpointConfigList(const std::vector<EndpointConfig> &list, std::string &msg_str);
  static Status DeserializeEndpointConfigList(const std::string &json_str,
                                              std::vector<EndpointConfig> &endpoint_list);

 private:
  enum class SocType { kV2, kV3, kOther };

  struct EndpointInfo {
    std::string protocol;
    std::string comm_id;
    std::string placement;
  };

  struct LocCommResInfo {
    std::string version;
    std::string net_instance_id;
    std::vector<EndpointInfo> endpoint_list;
  };

  static Status GenerateInfo(int32_t device_id, const std::string &local_engine, LocCommResInfo &loc_comm_res_info);
  static Status GetDeviceIp(int32_t phy_device_id, std::string &device_ip);
  static Status GetSocName(std::string &soc_name);
  static SocType GetSocTypeByName(const std::string &soc_name);
  static Status GetSocType(SocType &soc_type);
  static void ConvertLocCommResInfoToEndpointList(const LocCommResInfo &loc_comm_res_info,
                                                  std::vector<EndpointConfig> &endpoint_list);
  static Status BuildNetInstanceId(int32_t device_id,
                                   const std::string &local_engine,
                                   std::string &net_instance_id);
  static Status BuildEndpointListFromLocalCommRes(const nlohmann::json &config,
                                                  bool has_endpoint_list,
                                                  const std::string &local_engine,
                                                  std::vector<EndpointConfig> &endpoint_list);
  static Status ParseLocalCommRes(const nlohmann::json &config, std::vector<EndpointConfig> &endpoint_list);
  static Status FillDeviceInfoIfNeeded(SocType soc_type, std::vector<EndpointConfig> &endpoint_list);
  static Status BuildEndpointList(int32_t phy_device_id, std::vector<EndpointInfo> &endpoint_list);
  static Status BuildRoceEndpoint(int32_t phy_device_id, EndpointInfo &endpoint);
  static Status BuildHccsEndpoint(int32_t phy_device_id, EndpointInfo &endpoint);
  static Status GetHostIpFromLocalEngine(const std::string &local_engine, std::string &host_ip);
};
}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_ENGINE_ENDPOINT_GENERATOR_H_
