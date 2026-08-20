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
   * @brief 按 SoC 类型分发生成默认 endpoint 列表（A5: ub_rtp/uboe + ub_ctp；V2/V3: roce/hccs）
   *
   * 调用前需通过 aclrtSetDevice 设置目标 device（内部经 aclrtGetDevice 获取 device_id）。
   *
   * @param [in] options 已解析的 HixlOptions（含 protocol_desc）
   * @param [in] local_engine 本地监听地址（V2/V3 生成 net_instance_id 用；A5 传空即可）
   * @param [out] endpoint_list 生成的端点列表
   * @param [in] topo_path 拓扑文件路径；空表示使用默认 topo 查找
   * @return 成功: SUCCESS, 失败: 其它错误码
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
