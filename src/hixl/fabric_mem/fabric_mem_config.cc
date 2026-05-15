/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "fabric_mem/fabric_mem_config.h"

#include <string>

#include "nlohmann/json.hpp"
#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/hixl_utils.h"

namespace hixl {
namespace {
constexpr const char *kMaxCapacityKey = "fabric_memory.max_capacity";
constexpr const char *kStartAddressKey = "fabric_memory.start_address";
constexpr const char *kTaskStreamNumKey = "fabric_memory.task_stream_num";
constexpr const char *kFabricMemoryGroup = "fabric_memory";
constexpr const char *kMaxCapacityField = "max_capacity";
constexpr const char *kStartAddressField = "start_address";
constexpr const char *kTaskStreamNumField = "task_stream_num";
constexpr size_t kMaxCapacityTB = 1024UL;
constexpr size_t kMinStartAddressTB = 40UL;
constexpr size_t kMaxStartAddressTB = 220UL;
constexpr size_t kMinTaskStreamNum = 1U;
constexpr size_t kMaxTaskStreamNum = 8U;

std::string JsonValueToString(const nlohmann::json &value) {
  if (value.is_string()) {
    return value.get<std::string>();
  }
  return value.dump();
}

Status PutIfPresent(const nlohmann::json &json, const char *json_field, const char *option_key,
                    std::map<std::string, std::string> &json_options) {
  if (!json.contains(json_field)) {
    return SUCCESS;
  }
  json_options[option_key] = JsonValueToString(json.at(json_field));
  return SUCCESS;
}

Status ParseGlobalResourceConfig(const std::map<AscendString, AscendString> &options,
                                 std::map<std::string, std::string> &json_options) {
  const auto config_it = options.find(hixl::OPTION_GLOBAL_RESOURCE_CONFIG);
  if (config_it == options.end()) {
    return SUCCESS;
  }
  try {
    auto json = nlohmann::json::parse(config_it->second.GetString());
    if (!json.is_object()) {
      HIXL_LOGE(PARAM_INVALID, "GlobalResourceConfig must be a JSON object.");
      return PARAM_INVALID;
    }
    for (auto it = json.begin(); it != json.end(); ++it) {
      if (it.key() == kFabricMemoryGroup && it.value().is_object()) {
        HIXL_CHK_STATUS_RET(PutIfPresent(it.value(), kMaxCapacityField, kMaxCapacityKey, json_options),
                            "Failed to read FabricMem capacity config.");
        HIXL_CHK_STATUS_RET(PutIfPresent(it.value(), kStartAddressField, kStartAddressKey, json_options),
                            "Failed to read FabricMem start address config.");
        HIXL_CHK_STATUS_RET(PutIfPresent(it.value(), kTaskStreamNumField, kTaskStreamNumKey, json_options),
                            "Failed to read FabricMem task stream config.");
        continue;
      }
      json_options[it.key()] = JsonValueToString(it.value());
    }
    return SUCCESS;
  } catch (const nlohmann::json::exception &e) {
    HIXL_LOGE(PARAM_INVALID, "Failed to parse GlobalResourceConfig json, exception:%s", e.what());
    return PARAM_INVALID;
  }
}

template <typename T>
Status ParseNumberOption(const std::map<std::string, std::string> &options, const char *key, T &value, bool &found) {
  const auto it = options.find(key);
  if (it == options.end()) {
    found = false;
    return SUCCESS;
  }
  found = true;
  HIXL_CHK_STATUS_RET(ToNumber(it->second, value), "Invalid %s: %s", key, it->second.c_str());
  return SUCCESS;
}

Status ParseEnableFabricMem(const std::map<AscendString, AscendString> &options, FabricMemConfig &config) {
  const auto it = options.find(hixl::OPTION_ENABLE_USE_FABRIC_MEM);
  if (it == options.end() || std::string(it->second.GetString()).empty()) {
    return SUCCESS;
  }
  uint32_t enabled = 0U;
  HIXL_CHK_STATUS_RET(ToNumber(std::string(it->second.GetString()), enabled), "%s is invalid, value = %s",
                      hixl::OPTION_ENABLE_USE_FABRIC_MEM, it->second.GetString());
  HIXL_CHK_BOOL_RET_STATUS(enabled == 0U || enabled == 1U, PARAM_INVALID, "%s is invalid, should be zero or one.",
                           hixl::OPTION_ENABLE_USE_FABRIC_MEM);
  config.enabled = (enabled == 1U);
  HIXL_LOGI("Set %s to %u.", hixl::OPTION_ENABLE_USE_FABRIC_MEM, enabled);
  return SUCCESS;
}

Status ParseAutoConnect(const std::map<AscendString, AscendString> &options, FabricMemConfig &config) {
  const auto it = options.find(hixl::OPTION_AUTO_CONNECT);
  if (it == options.end()) {
    return SUCCESS;
  }
  std::string auto_connect_str = it->second.GetString();
  if (auto_connect_str.empty()) {
    HIXL_LOGE(PARAM_INVALID, "%s value is empty, should be zero or one.", hixl::OPTION_AUTO_CONNECT);
    return PARAM_INVALID;
  }
  uint32_t auto_connect = 0U;
  HIXL_CHK_STATUS_RET(ToNumber(auto_connect_str, auto_connect), "%s is invalid, value = %s",
                      hixl::OPTION_AUTO_CONNECT, auto_connect_str.c_str());
  HIXL_CHK_BOOL_RET_STATUS(auto_connect == 0U || auto_connect == 1U, PARAM_INVALID,
                           "%s is invalid, should be zero or one.", hixl::OPTION_AUTO_CONNECT);
  config.auto_connect = (auto_connect == 1U);
  HIXL_LOGI("Set %s to %u.", hixl::OPTION_AUTO_CONNECT, auto_connect);
  return SUCCESS;
}

Status ParseCapacity(const std::map<std::string, std::string> &options, FabricMemConfig &config) {
  bool found = false;
  size_t capacity_tb = 0;
  HIXL_CHK_STATUS_RET(ParseNumberOption(options, kMaxCapacityKey, capacity_tb, found), "Failed to parse %s.",
                      kMaxCapacityKey);
  if (!found) {
    return SUCCESS;
  }
  HIXL_CHK_BOOL_RET_STATUS(capacity_tb > 0 && capacity_tb <= kMaxCapacityTB, PARAM_INVALID,
                           "%s must be in (0, %zu] TB, got %zu", kMaxCapacityKey, kMaxCapacityTB, capacity_tb);
  config.has_capacity_tb = true;
  config.capacity_tb = capacity_tb;
  return SUCCESS;
}

Status ParseStartAddress(const std::map<std::string, std::string> &options, FabricMemConfig &config) {
  bool found = false;
  size_t start_address_tb = 0;
  HIXL_CHK_STATUS_RET(ParseNumberOption(options, kStartAddressKey, start_address_tb, found), "Failed to parse %s.",
                      kStartAddressKey);
  if (!found) {
    return SUCCESS;
  }
  HIXL_CHK_BOOL_RET_STATUS(start_address_tb >= kMinStartAddressTB && start_address_tb <= kMaxStartAddressTB,
                           PARAM_INVALID, "%s must be in [%zu, %zu] TB, got %zu", kStartAddressKey,
                           kMinStartAddressTB, kMaxStartAddressTB, start_address_tb);
  config.has_start_address_tb = true;
  config.start_address_tb = start_address_tb;
  return SUCCESS;
}

Status ParseTaskStreamNum(const std::map<std::string, std::string> &options, FabricMemConfig &config) {
  bool found = false;
  size_t task_stream_num = 0;
  HIXL_CHK_STATUS_RET(ParseNumberOption(options, kTaskStreamNumKey, task_stream_num, found), "Failed to parse %s.",
                      kTaskStreamNumKey);
  if (!found) {
    return SUCCESS;
  }
  HIXL_CHK_BOOL_RET_STATUS(task_stream_num >= kMinTaskStreamNum && task_stream_num <= kMaxTaskStreamNum, PARAM_INVALID,
                           "%s must be between %zu and %zu, got %zu", kTaskStreamNumKey, kMinTaskStreamNum,
                           kMaxTaskStreamNum, task_stream_num);
  config.task_stream_num = task_stream_num;
  return SUCCESS;
}
}  // namespace

Status FabricMemConfigParser::Parse(const std::map<AscendString, AscendString> &options, FabricMemConfig &config) {
  HIXL_CHK_STATUS_RET(ParseEnableFabricMem(options, config), "Failed to parse FabricMem enable option.");
  if (!config.enabled) {
    return SUCCESS;
  }
  std::map<std::string, std::string> json_options;
  HIXL_CHK_STATUS_RET(ParseGlobalResourceConfig(options, json_options), "Failed to parse GlobalResourceConfig.");
  HIXL_CHK_STATUS_RET(ParseCapacity(json_options, config), "Failed to parse FabricMem capacity.");
  HIXL_CHK_STATUS_RET(ParseStartAddress(json_options, config), "Failed to parse FabricMem start address.");
  HIXL_CHK_STATUS_RET(ParseTaskStreamNum(json_options, config), "Failed to parse FabricMem task stream num.");
  HIXL_CHK_STATUS_RET(ParseAutoConnect(options, config), "Failed to parse FabricMem auto connect.");
  return SUCCESS;
}
}  // namespace hixl
