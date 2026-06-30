/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_ENGINE_CLIENT_HANDLER_CONFIG_HELPER_H_
#define CANN_HIXL_SRC_HIXL_ENGINE_CLIENT_HANDLER_CONFIG_HELPER_H_

#include <optional>
#include <string>

#include "nlohmann/json.hpp"
#include "engine/client_handler_factory.h"

namespace hixl {

class ClientHandlerConfigHelper {
 public:
  static std::string BuildGlobalResourceConfig(const HandlerCreateArgs &args) {
    // force return "", default json construction will dump to "null" which not as expect
    if (!args.qos.has_value() && !args.max_active_channels.has_value()) {
      return "";
    }
    nlohmann::json json;
    if (args.qos.has_value()) {
      json["comm_resource_config.qos"] = args.qos.value();
    }
    if (args.max_active_channels.has_value()) {
      json["comm_resource_config.max_active_channels"] = args.max_active_channels.value();
    }
    return json.dump();
  }
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_ENGINE_CLIENT_HANDLER_CONFIG_HELPER_H_
