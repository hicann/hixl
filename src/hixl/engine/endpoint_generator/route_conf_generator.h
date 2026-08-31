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
 * @file route_conf_generator.h
 * @brief host_route.json serialization and file-write module
 *
 * JSON format (replaces the old route.conf key=value format):
 * {
 *   "device_num": 8,
 *   "devices": [
 *     { "device_id": 0, "local_eid": "...", "remote_eid": "..." },
 *     ...
 *   ]
 * }
 */

#ifndef CANN_HIXL_SRC_HIXL_ENGINE_ENDPOINT_GENERATOR_ROUTE_CONF_GENERATOR_H_
#define CANN_HIXL_SRC_HIXL_ENGINE_ENDPOINT_GENERATOR_ROUTE_CONF_GENERATOR_H_

#include <string>
#include <vector>

#include "hixl/hixl_types.h"

namespace hixl {

/**
 * @brief Single device record in host_route.json (raw JSON semantics)
 */
struct HostRouteEntry {
  int32_t device_id = 0;
  std::string local_eid;   // Host/CPU-side EID (2-port PG)
  std::string remote_eid;  // NPU-side EID
};

/**
 * @brief Full host_route.json data
 */
struct HostRouteData {
  std::vector<HostRouteEntry> devices;
};

/**
 * @brief host_route.json serializer
 *
 * Serializes host_route.json and writes it to file.
 * Callers use static methods; no instance is needed.
 */
class RouteConfGenerator {
 public:
  RouteConfGenerator() = delete;

  /**
   * @brief Serialize HostRouteData to a JSON string
   * @param [in] data host_route data
   * @param [out] json_str Output JSON string (2-space indent)
   * @return SUCCESS on success, other error codes on failure
   */
  static Status SerializeHostRouteJson(const HostRouteData &data, std::string &json_str);
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_ENGINE_ENDPOINT_GENERATOR_ROUTE_CONF_GENERATOR_H_
