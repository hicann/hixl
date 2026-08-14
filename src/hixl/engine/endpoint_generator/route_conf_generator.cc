/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

/**
 * @file route_conf_generator.cc
 * @brief host_route.json 序列化实现
 */

#include "route_conf_generator.h"
#include <string>
#include <nlohmann/json.hpp>
#include "common/hixl_inner_types.h"
#include "common/hixl_log.h"

namespace hixl {

int32_t RouteConfGenerator::SerializeHostRouteJson(const HostRouteData &data, std::string &json_str) {
  nlohmann::json j;
  j["device_num"] = data.devices.size();
  nlohmann::json devices = nlohmann::json::array();
  for (const auto &dev : data.devices) {
    nlohmann::json item;
    item["device_id"] = dev.device_id;
    item["local_eid"] = dev.local_eid;
    item["remote_eid"] = dev.remote_eid;
    devices.push_back(item);
  }
  j["devices"] = devices;

  try {
    json_str = j.dump(2);
  } catch (const nlohmann::json::exception &e) {
    HIXL_LOGE(FAILED, "[SerializeHostRouteJson] Failed to dump JSON: %s", e.what());
    return FAILED;
  }
  return SUCCESS;
}

}  // namespace hixl
