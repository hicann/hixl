/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "engine_factory.h"

#include <algorithm>

#include "nlohmann/json.hpp"
#include "fabric_mem_engine.h"
#include "hixl_engine.h"
#include "adxl/adxl_inner_engine.h"
#include "hixl/hixl_types.h"
#include "adxl/adxl_types.h"
#include "common/hixl_log.h"

namespace hixl {
namespace {
bool UseUboe(const HixlOptions &options) {
  auto grc = options.GlobalResourceCfg();
  if (!grc.has_value()) {
    return false;
  }
  auto desc = grc->comm_resource_config.protocol_desc;
  return desc.has_value() && !desc->empty() &&
         std::find(desc->begin(), desc->end(), "uboe:device") != desc->end();
}
}  // namespace
std::unique_ptr<Engine> EngineFactory::CreateEngine(const std::string local_engine,
                                                    const std::map<AscendString, AscendString> &options,
                                                    HixlOptions &parsed_options) {
  Status ret = HixlOptions::Parse(options, parsed_options);
  if (ret != SUCCESS) {
    HIXL_LOGE(ret, "[EngineFactory] Failed to parse options");
    return nullptr;
  }

  if (parsed_options.EnableFabricMem().value_or(false)) {
    return std::make_unique<FabricMemEngine>(AscendString(local_engine.c_str()));
  }
  bool config_use_uboe = UseUboe(parsed_options);
  bool use_hixl = config_use_uboe;
  if (!use_hixl) {
    auto lcr = parsed_options.LocalCommRes();
    if (!lcr.has_value() || lcr->empty()) {
      return std::make_unique<CommEngine>(AscendString(local_engine.c_str()));
    }
    try {
      auto json = nlohmann::json::parse(*lcr);
      use_hixl = json["version"] == "1.3";
    } catch (const nlohmann::json::exception &e) {
      HIXL_LOGE(PARAM_INVALID, "Invalid json, exception:%s", e.what());
      return nullptr;
    }
  }
  if (use_hixl) {
    return std::make_unique<HixlEngine>(AscendString(local_engine.c_str()));
  }
  return std::make_unique<CommEngine>(AscendString(local_engine.c_str()));
}
}  // namespace hixl
