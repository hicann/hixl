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

#include "nlohmann/json.hpp"
#include "fabric_mem_engine.h"
#include "hixl_engine.h"
#include "adxl/adxl_inner_engine.h"
#include "hixl/hixl_types.h"
#include "adxl/adxl_types.h"
#include "common/hixl_log.h"
#include "common/hixl_utils.h"

namespace hixl {
namespace {
bool UseProtocolDesc(const HixlOptions &options) {
  auto grc = options.GlobalResourceCfg();
  if (!grc.has_value()) {
    return false;
  }
  auto desc = grc->comm_resource_config.protocol_desc;
  return desc.has_value() && !desc->empty();
}

void LogSelectedEngine(const char *engine, const char *reason, const std::string &local_engine) {
  HIXL_EVENT("[EngineFactory] selected engine:%s, reason:%s, %s, local_engine:%s", engine, reason,
             IntraRoceEnableStatusStr(), local_engine.c_str());
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
    LogSelectedEngine("fabric_mem", "EnableFabricMem is true", local_engine);
    return std::make_unique<FabricMemEngine>(AscendString(local_engine.c_str()));
  }
  auto lcr = parsed_options.LocalCommRes();
  if (lcr.has_value() && !lcr->empty()) {
    try {
      auto json = nlohmann::json::parse(*lcr);
      if (json.contains("version") && json["version"] == "1.3") {
        LogSelectedEngine("hixl_cs", "LocalCommRes version is 1.3", local_engine);
        return std::make_unique<HixlEngine>(AscendString(local_engine.c_str()));
      }
    } catch (const nlohmann::json::exception &e) {
      HIXL_LOGE(PARAM_INVALID, "Invalid json, exception:%s", e.what());
      return nullptr;
    }
    LogSelectedEngine("comm", "LocalCommRes version is not 1.3", local_engine);
    return std::make_unique<CommEngine>(AscendString(local_engine.c_str()));
  }
  if (UseProtocolDesc(parsed_options)) {
    LogSelectedEngine("hixl_cs", "protocol_desc is configured", local_engine);
    return std::make_unique<HixlEngine>(AscendString(local_engine.c_str()));
  }
  SocType soc_type = SocType::kOther;
  if (GetSocType(soc_type) == SUCCESS && soc_type == SocType::kV5) {
    LogSelectedEngine("hixl_cs", "SoC type matched hixl_cs", local_engine);
    return std::make_unique<HixlEngine>(AscendString(local_engine.c_str()));
  }
  LogSelectedEngine("comm", "no hixl_cs selector matched", local_engine);
  return std::make_unique<CommEngine>(AscendString(local_engine.c_str()));
}
}  // namespace hixl
