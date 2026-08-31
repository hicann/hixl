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

#include <string>
#include <vector>

#include "nlohmann/json.hpp"
#include "cs/hixl_cs.h"
#include "hixl/hixl_types.h"
#include "engine/hixl_options.h"
#include "common/hixl_inner_types.h"
#include "common/hixl_utils.h"
#include "local_comm_res_generator_v1.h"

namespace hixl {

class EndpointGenerator {
 public:
  static Status BuildEndpointList(const HixlOptions &options, const std::string &local_engine,
                                  std::string &local_comm_res, std::vector<EndpointConfig> &endpoint_list);
  static Status ConvertToEndpointDesc(const EndpointConfig &endpoint_config, EndpointDesc &endpoint);
  static Status SerializeEndpointConfigList(const std::vector<EndpointConfig> &list, std::string &msg_str);
  static Status DeserializeEndpointConfigList(const std::string &json_str, std::vector<EndpointConfig> &endpoint_list);

  /**
   * @brief Dispatch default endpoint generation by SoC type (A5: ub_rtp/uboe + ub_ctp; V2/V3: roce/hccs)
   *
   * Caller must set the target device with aclrtSetDevice first (device_id is read via aclrtGetDevice).
   *
   * @param [in] options Parsed HixlOptions (includes protocol_desc)
   * @param [in] local_engine Local listen address (used for V2/V3 net_instance_id; A5 may pass empty)
   * @param [out] endpoint_list Generated endpoint list
   * @param [in] topo_path Topology file path; empty means default topo lookup
   * @return SUCCESS on success, other error codes on failure
   */
  static Status AutoGenEndpointList(const HixlOptions &options, const std::string &local_engine,
                                    std::vector<EndpointConfig> &endpoint_list, const std::string &topo_path = "");

 private:
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
  static void ConvertLocCommResInfoToEndpointList(const LocCommResInfo &loc_comm_res_info,
                                                  std::vector<EndpointConfig> &endpoint_list);
  static Status BuildNetInstanceId(int32_t device_id, const std::string &local_engine, std::string &net_instance_id);
  static Status ParseEndpointListFromLocalCommRes(const HixlOptions &options, std::string &local_comm_res,
                                                  std::vector<EndpointConfig> &endpoint_list);
  static Status GenEndpointFromProtocolDesc(const HixlOptions &options, std::vector<EndpointConfig> &endpoint_list);
  static Status FilterEndpointListByProtocolDesc(const HixlOptions &options,
                                                 std::vector<EndpointConfig> &endpoint_list);
  static Status AutoGenA5EndpointList(const HixlOptions &options, std::vector<EndpointConfig> &endpoint_list,
                                      const std::string &topo_path = "");
  static Status ParseLocalCommRes(const nlohmann::json &config, std::vector<EndpointConfig> &endpoint_list);
  static bool HasDeviceEndpoint(const std::vector<EndpointConfig> &endpoint_list);
  static Status PopulateLocalDeviceInfo(std::vector<EndpointConfig> &endpoint_list);
  static Status BuildDefaultDeviceEndpointInfoList(int32_t phy_device_id, std::vector<EndpointInfo> &endpoint_list);
  static Status BuildRoceEndpoint(int32_t phy_device_id, EndpointInfo &endpoint);
  static Status BuildHccsEndpoint(int32_t phy_device_id, EndpointInfo &endpoint);
  static Status GetHostIpFromLocalEngine(const std::string &local_engine, std::string &host_ip);
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_ENGINE_ENDPOINT_GENERATOR_H_
