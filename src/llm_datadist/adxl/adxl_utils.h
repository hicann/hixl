/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_ADXL_UTILS_H
#define CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_ADXL_UTILS_H

#include <map>
#include <string>
#include "hccl/hccl_types.h"
#include "acl/acl.h"
#include "adxl/adxl_types.h"
#include "ge_common/ge_api_error_codes.h"
#include "nlohmann/json.hpp"

namespace adxl {
// Load JSON configuration from file and merge into option map
Status LoadJsonConfig(const std::string& file_path, std::map<ge::AscendString, ge::AscendString>& options);
Status HcclError2AdxlStatus(HcclResult ret);
Status AclError2AdxlStatus(aclError ret);
Status LLMError2AdxlStatus(ge::Status ret);

constexpr const char* OPTION_MAX_CHANNEL = "channel_pool.max_channel";
constexpr const char* OPTION_HIGH_WATERLINE = "channel_pool.high_waterline";
constexpr const char* OPTION_LOW_WATERLINE = "channel_pool.low_waterline";
constexpr const char* OPTION_MAX_FABRIC_MEMORY_CAPACITY = "fabric_memory.max_capacity";

constexpr int kDefaultMaxChannel = 512;
bool NeedErrorLog(Status status);
}  // namespace adxl
#endif  // CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_ADXL_UTILS_H
