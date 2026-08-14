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
 * @brief host_route.json 序列化与文件写入模块
 *
 * JSON 格式（替代原 route.conf 的 key=value 格式）：
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

namespace hixl {

/**
 * @brief host_route.json 中的单条设备记录（JSON 原始语义）
 */
struct HostRouteEntry {
  int32_t device_id = 0;
  std::string local_eid;   // Host/CPU 侧 EID（2 口 PG）
  std::string remote_eid;  // NPU 侧 EID
};

/**
 * @brief host_route.json 完整数据
 */
struct HostRouteData {
  std::vector<HostRouteEntry> devices;
};

/**
 * @brief host_route.json 序列化器
 *
 * 提供 host_route.json 的序列化和文件写入能力。
 * 外部通过静态方法调用，无需创建实例。
 */
class RouteConfGenerator {
 public:
  RouteConfGenerator() = delete;

  /**
   * @brief 将 HostRouteData 序列化为 JSON 字符串
   * @param [in] data host_route 数据
   * @param [out] json_str 输出的 JSON 字符串（2 空格缩进）
   * @return 成功: SUCCESS, 失败: 其它错误码
   */
  static int32_t SerializeHostRouteJson(const HostRouteData &data, std::string &json_str);
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_ENGINE_ENDPOINT_GENERATOR_ROUTE_CONF_GENERATOR_H_
