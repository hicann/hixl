/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_CS_GLOBAL_CONFIG_H_
#define CANN_HIXL_SRC_HIXL_CS_GLOBAL_CONFIG_H_

#include <cstdint>
#include <optional>

#include "hixl/hixl_types.h"

namespace hixl {

struct CommResourceConfig {
  std::optional<uint32_t> listen_port;
};

class GlobalConfig {
 public:
  static Status Parse(const char *config_str, GlobalConfig &result);
  std::optional<uint32_t> ListenPort() const;

 private:
  CommResourceConfig comm_resource_config_;
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_CS_GLOBAL_CONFIG_H_
