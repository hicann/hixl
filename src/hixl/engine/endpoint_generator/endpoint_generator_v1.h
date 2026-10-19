/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_ENGINE_ENDPOINT_GENERATOR_ENDPOINT_GENERATOR_V1_H_
#define CANN_HIXL_SRC_HIXL_ENGINE_ENDPOINT_GENERATOR_ENDPOINT_GENERATOR_V1_H_

#include <string>
#include <vector>

#include "common/hixl_inner_types.h"
#include "endpoint_generator.h"
#include "endpoint_generator_base.h"
#include "engine/hixl_options.h"

namespace hixl {

// Legacy automatic endpoint generation: RoCE + HCCS device endpoints derived from the local
// net_instance_id. Used for the legacy SoC categories (SocType::kGen1 / SocType::kGen2).
class EndpointGeneratorV1 : public EndpointGeneratorBase {
 public:
  Status AutoGen(const HixlOptions &options, const std::string &local_engine,
                 std::vector<EndpointConfig> &endpoint_list) const override;

 private:
  struct LocCommResInfo {
    std::string version;
    std::string net_instance_id;
    std::vector<EndpointGenerator::EndpointInfo> endpoint_list;
  };

  static Status GenerateInfo(int32_t device_id, const std::string &local_engine,
                             const std::vector<std::string> &protocol_desc, LocCommResInfo &loc_comm_res_info);
  static void ConvertLocCommResInfoToEndpointList(const LocCommResInfo &loc_comm_res_info,
                                                  std::vector<EndpointConfig> &endpoint_list);
  static Status BuildNetInstanceId(int32_t device_id, const std::string &local_engine, std::string &net_instance_id);
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_ENGINE_ENDPOINT_GENERATOR_ENDPOINT_GENERATOR_V1_H_
