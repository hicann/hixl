/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_COMMON_JSON_UTILS_H_
#define CANN_HIXL_SRC_HIXL_COMMON_JSON_UTILS_H_

#include "nlohmann/json.hpp"
#include "common/hixl_utils.h"

namespace hixl {

template <typename T>
T JsonToNumber(const nlohmann::json &val) {
  if (val.is_string()) {
    T result{};
    Status ret = ToNumber(val.get<std::string>(), result);
    if (ret != SUCCESS) {
      throw nlohmann::json::type_error::create(0, "Failed to convert string to number: " + val.get<std::string>(),
                                               nullptr);
    }
    return result;
  }
  return val.get<T>();
}

template <typename T>
Status ParseJsonField(const nlohmann::json &json_obj, const std::string &field_name, T &field_value,
                      bool required = true) {
  if (!json_obj.contains(field_name)) {
    if (!required) {
      return SUCCESS;
    }
    HIXL_LOGE(PARAM_INVALID, "Missing required field '%s'", field_name.c_str());
    return PARAM_INVALID;
  }
  try {
    field_value = json_obj[field_name].get<T>();
    return SUCCESS;
  } catch (const nlohmann::json::exception &e) {
    HIXL_LOGE(PARAM_INVALID, "Failed to parse field '%s', exception: %s", field_name.c_str(), e.what());
    return PARAM_INVALID;
  }
}

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_COMMON_JSON_UTILS_H_
