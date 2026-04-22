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
#include "hixl_engine.h"
#include "adxl/adxl_inner_engine.h"
#include "hixl/hixl_types.h"
#include "adxl/adxl_types.h"
#include "common/hixl_log.h"

namespace hixl {
namespace {
bool UseUboe(const std::map<AscendString, AscendString> &options) {
  std::vector<std::string> protocol_desc;
  (void)ParseConfigProtocolDesc(options, protocol_desc);
  return !protocol_desc.empty() &&
         std::find(protocol_desc.begin(), protocol_desc.end(), "uboe:device") != protocol_desc.end();
}
}  // namespace
std::unique_ptr<Engine> EngineFactory::CreateEngine(const std::string local_engine,
                                                    const std::map<AscendString, AscendString> &options) {
  bool config_use_uboe = UseUboe(options);
  // uboe must use hixl
  bool use_hixl = config_use_uboe;
  if (!use_hixl) {
    const auto hixl_it = options.find(hixl::OPTION_LOCAL_COMM_RES);
    const auto adxl_it = options.find(adxl::OPTION_LOCAL_COMM_RES);
    if ((hixl_it == options.end()) && (adxl_it == options.end())) {
      return std::make_unique<AdxlEngine>(AscendString(local_engine.c_str()));
    }
    const auto &it = hixl_it == options.end() ? adxl_it : hixl_it;
    std::string local_comm_res = it->second.GetString();
    try {
      if (!local_comm_res.empty()) {
        use_hixl = nlohmann::json::parse(local_comm_res)["version"] == "1.3";
      }
    } catch (const nlohmann::json::exception &e) {
      HIXL_LOGE(PARAM_INVALID, "Invalid json, exception:%s", e.what());
      return nullptr;
    }
  }
  if (use_hixl) {
    return std::make_unique<HixlEngine>(AscendString(local_engine.c_str()));
  }
  return std::make_unique<AdxlEngine>(AscendString(local_engine.c_str()));
}
}  // namespace hixl
