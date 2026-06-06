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

#include <cstdint>
#include <optional>
#include <string>

#include "nlohmann/json.hpp"

namespace hixl {

class ClientHandlerConfigHelper {
 public:
  static std::string BuildGlobalResourceConfig(const std::optional<uint32_t> &listen_port) {
    if (!listen_port.has_value()) {
      return "";
    }
    nlohmann::json json{{"comm_resource_config.listen_port", *listen_port}};
    return json.dump();
  }
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_ENGINE_CLIENT_HANDLER_CONFIG_HELPER_H_
