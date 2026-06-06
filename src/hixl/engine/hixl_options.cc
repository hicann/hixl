/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hixl_options.h"

#include <cstdlib>
#include <string>

#include "nlohmann/json.hpp"
#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/hixl_utils.h"

namespace hixl {
namespace {
constexpr size_t kMaxCapacityTB = 1024UL;
constexpr size_t kMinStartAddressTB = 40UL;
constexpr size_t kMaxStartAddressTB = 220UL;
constexpr size_t kMinTaskStreamNum = 1U;
constexpr size_t kMaxTaskStreamNum = 8U;
constexpr uint32_t kMinListenPort = 1U;
constexpr uint32_t kMaxListenPort = 65535U;

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

void from_json(const nlohmann::json &j, FabricMemoryConfig &cfg) {
  if (j.contains("max_capacity")) {
    cfg.max_capacity = JsonToNumber<size_t>(j.at("max_capacity"));
  }
  if (j.contains("start_address")) {
    cfg.start_address = JsonToNumber<size_t>(j.at("start_address"));
  }
  if (j.contains("task_stream_num")) {
    cfg.task_stream_num = JsonToNumber<size_t>(j.at("task_stream_num"));
  }
}

void from_json(const nlohmann::json &j, ConnectPoolConfig &cfg) {
  if (j.contains("connect_pool.thread_num")) {
    cfg.thread_num = JsonToNumber<int32_t>(j.at("connect_pool.thread_num"));
  }
  if (j.contains("connect_pool.task_queue_capacity")) {
    cfg.task_queue_capacity = JsonToNumber<int32_t>(j.at("connect_pool.task_queue_capacity"));
  }
}

void from_json(const nlohmann::json &j, CommResourceConfigDesc &cfg) {
  if (j.contains("comm_resource_config.protocol_desc")) {
    const auto &protocol_desc = j.at("comm_resource_config.protocol_desc");
    if (protocol_desc.is_string()) {
      cfg.protocol_desc = std::vector<std::string>{protocol_desc.get<std::string>()};
      return;
    }
    cfg.protocol_desc = protocol_desc.get<std::vector<std::string>>();
  }
  if (j.contains("comm_resource_config.listen_port")) {
    cfg.listen_port = JsonToNumber<uint32_t>(j.at("comm_resource_config.listen_port"));
  }
}

void from_json(const nlohmann::json &j, GlobalResourceConfig &cfg) {
  if (j.contains("fabric_memory") && j.at("fabric_memory").is_object()) {
    from_json(j.at("fabric_memory"), cfg.fabric_memory);
  }
  // Also support flat key format: "fabric_memory.start_address", etc.
  if (j.contains("fabric_memory.max_capacity")) {
    cfg.fabric_memory.max_capacity = JsonToNumber<size_t>(j.at("fabric_memory.max_capacity"));
  }
  if (j.contains("fabric_memory.start_address")) {
    cfg.fabric_memory.start_address = JsonToNumber<size_t>(j.at("fabric_memory.start_address"));
  }
  if (j.contains("fabric_memory.task_stream_num")) {
    cfg.fabric_memory.task_stream_num = JsonToNumber<size_t>(j.at("fabric_memory.task_stream_num"));
  }
  from_json(j, cfg.connect_pool);
  from_json(j, cfg.comm_resource_config);
}
}  // namespace

Status HixlOptions::Parse(const std::map<AscendString, AscendString> &options, HixlOptions &result) {
  result.raw_options_ = options;
  for (const auto &pair : options) {
    result.parsed_keys_.insert(pair.first.GetString());
  }
  HIXL_CHK_STATUS_RET(result.ParseRdmaOptions(options), "Failed to parse RDMA options.");
  HIXL_CHK_STATUS_RET(result.ParseEndpointOptions(options), "Failed to parse endpoint options.");
  HIXL_CHK_STATUS_RET(result.ParseFabricMemOptions(options), "Failed to parse FabricMem options.");
  HIXL_CHK_STATUS_RET(result.ParseAutoConnectOptions(options), "Failed to parse AutoConnect options.");
  HIXL_CHK_STATUS_RET(result.ParseGlobalResourceConfig(options), "Failed to parse GlobalResourceConfig.");
  return SUCCESS;
}

Status HixlOptions::CheckSupportedOptions(const std::unordered_set<std::string> &supported_keys) const {
  for (const auto &key : parsed_keys_) {
    HIXL_CHK_BOOL_RET_SPECIAL_STATUS(
        supported_keys.count(key) == 0, PARAM_INVALID,
        "Unsupported option '%s' for this engine", key.c_str());
  }
  return SUCCESS;
}

std::vector<std::string> HixlOptions::GetProtocolDesc() const {
  if (!global_resource_config_.has_value() ||
      !global_resource_config_->comm_resource_config.protocol_desc.has_value()) {
    return {};
  }
  return *global_resource_config_->comm_resource_config.protocol_desc;
}

Status HixlOptions::ParseRdmaOptions(const std::map<AscendString, AscendString> &options) {
  std::string traffic_class_str;
  const auto &hixl_tc_it = options.find(hixl::OPTION_RDMA_TRAFFIC_CLASS);
  const auto &adxl_tc_it = options.find(adxl::OPTION_RDMA_TRAFFIC_CLASS);
  auto tc_it = (hixl_tc_it != options.cend()) ? hixl_tc_it : adxl_tc_it;
  if (tc_it != options.cend()) {
    traffic_class_str = tc_it->second.GetString();
  }
  if (traffic_class_str.empty()) {
    const char *env_ret = std::getenv("HCCL_RDMA_TC");
    if (env_ret != nullptr) {
      traffic_class_str = env_ret;
    }
  }
  if (!traffic_class_str.empty()) {
    int32_t traffic_class = 0;
    HIXL_CHK_STATUS_RET(ToNumber(traffic_class_str, traffic_class),
                        "Traffic class is invalid, value = %s", traffic_class_str.c_str());
    HIXL_CHK_BOOL_RET_STATUS(traffic_class >= 0 && traffic_class <= 255 && (traffic_class % 4 == 0),
                             PARAM_INVALID,
                             "Traffic class is invalid, value = %d, must be between 0-255 and a multiple of 4",
                             traffic_class);
    rdma_traffic_class_ = static_cast<uint8_t>(traffic_class);
    HIXL_LOGI("Set rdma traffic class to %d.", traffic_class);
  }

  std::string service_level_str;
  const auto &hixl_sl_it = options.find(hixl::OPTION_RDMA_SERVICE_LEVEL);
  const auto &adxl_sl_it = options.find(adxl::OPTION_RDMA_SERVICE_LEVEL);
  auto sl_it = (hixl_sl_it != options.cend()) ? hixl_sl_it : adxl_sl_it;
  if (sl_it != options.cend()) {
    service_level_str = sl_it->second.GetString();
  }
  if (service_level_str.empty()) {
    const char *env_ret = std::getenv("HCCL_RDMA_SL");
    if (env_ret != nullptr) {
      service_level_str = env_ret;
    }
  }
  if (!service_level_str.empty()) {
    int32_t service_level = 0;
    HIXL_CHK_STATUS_RET(ToNumber(service_level_str, service_level),
                        "Service level is invalid, value = %s", service_level_str.c_str());
    HIXL_CHK_BOOL_RET_STATUS(service_level >= 0 && service_level <= 7, PARAM_INVALID,
                             "service_level must be in [0, 7], value = %d", service_level);
    rdma_service_level_ = static_cast<uint8_t>(service_level);
    HIXL_LOGI("Set rdma service level to %d.", service_level);
  }
  return SUCCESS;
}

Status HixlOptions::ParseEndpointOptions(const std::map<AscendString, AscendString> &options) {
  const auto &hixl_lcr_it = options.find(hixl::OPTION_LOCAL_COMM_RES);
  const auto &adxl_lcr_it = options.find(adxl::OPTION_LOCAL_COMM_RES);
  auto lcr_it = (hixl_lcr_it != options.cend()) ? hixl_lcr_it : adxl_lcr_it;
  if (lcr_it != options.cend()) {
    local_comm_res_ = std::string(lcr_it->second.GetString());
  }
  return SUCCESS;
}

Status HixlOptions::ParseFabricMemOptions(const std::map<AscendString, AscendString> &options) {
  const auto &efm_it = options.find(hixl::OPTION_ENABLE_USE_FABRIC_MEM);
  if (efm_it != options.end() && !std::string(efm_it->second.GetString()).empty()) {
    uint32_t enabled = 0U;
    HIXL_CHK_STATUS_RET(ToNumber(std::string(efm_it->second.GetString()), enabled),
                        "%s is invalid, value = %s",
                        hixl::OPTION_ENABLE_USE_FABRIC_MEM, efm_it->second.GetString());
    HIXL_CHK_BOOL_RET_STATUS(enabled == 0U || enabled == 1U, PARAM_INVALID,
                             "%s is invalid, should be zero or one.", hixl::OPTION_ENABLE_USE_FABRIC_MEM);
    enable_fabric_mem_ = (enabled == 1U);
    HIXL_LOGI("Set %s to %u.", hixl::OPTION_ENABLE_USE_FABRIC_MEM, enabled);
  }

  return SUCCESS;
}

Status HixlOptions::ParseAutoConnectOptions(const std::map<AscendString, AscendString> &options) {
  const auto &ac_it = options.find(hixl::OPTION_AUTO_CONNECT);
  if (ac_it == options.end()) {
    return SUCCESS;
  }

  std::string auto_connect_str = ac_it->second.GetString();
  HIXL_CHK_BOOL_RET_STATUS(!auto_connect_str.empty(), PARAM_INVALID,
                           "%s value is empty, should be zero or one.", hixl::OPTION_AUTO_CONNECT);
  uint32_t auto_connect = 0U;
  HIXL_CHK_STATUS_RET(ToNumber(auto_connect_str, auto_connect),
                      "%s is invalid, value = %s", hixl::OPTION_AUTO_CONNECT, auto_connect_str.c_str());
  HIXL_CHK_BOOL_RET_STATUS(auto_connect == 0U || auto_connect == 1U, PARAM_INVALID,
                           "%s is invalid, should be zero or one.", hixl::OPTION_AUTO_CONNECT);
  auto_connect_ = (auto_connect == 1U);
  HIXL_LOGI("Set %s to %u.", hixl::OPTION_AUTO_CONNECT, auto_connect);
  return SUCCESS;
}

Status HixlOptions::ParseGlobalResourceConfig(const std::map<AscendString, AscendString> &options) {
  const auto &config_it = options.find(hixl::OPTION_GLOBAL_RESOURCE_CONFIG);
  if (config_it == options.end()) {
    return SUCCESS;
  }
  std::string config_str = config_it->second.GetString();
  if (config_str.empty()) {
    return SUCCESS;
  }
  try {
    auto json = nlohmann::json::parse(config_str);
    if (!json.is_object()) {
      HIXL_LOGE(PARAM_INVALID, "GlobalResourceConfig must be a JSON object.");
      return PARAM_INVALID;
    }
    GlobalResourceConfig cfg{};
    from_json(json, cfg);

    if (cfg.fabric_memory.max_capacity.has_value()) {
      size_t val = *cfg.fabric_memory.max_capacity;
      HIXL_CHK_BOOL_RET_STATUS(val > 0 && val <= kMaxCapacityTB, PARAM_INVALID,
                               "fabric_memory.max_capacity must be in (0, %zu] TB, got %zu", kMaxCapacityTB, val);
    }
    if (cfg.fabric_memory.start_address.has_value()) {
      size_t val = *cfg.fabric_memory.start_address;
      HIXL_CHK_BOOL_RET_STATUS(val >= kMinStartAddressTB && val <= kMaxStartAddressTB, PARAM_INVALID,
                               "fabric_memory.start_address must be in [%zu, %zu] TB, got %zu",
                               kMinStartAddressTB, kMaxStartAddressTB, val);
    }
    if (cfg.fabric_memory.task_stream_num.has_value()) {
      size_t val = *cfg.fabric_memory.task_stream_num;
      HIXL_CHK_BOOL_RET_STATUS(val >= kMinTaskStreamNum && val <= kMaxTaskStreamNum, PARAM_INVALID,
                               "fabric_memory.task_stream_num must be between %zu and %zu, got %zu",
                               kMinTaskStreamNum, kMaxTaskStreamNum, val);
    }
    if (cfg.comm_resource_config.listen_port.has_value()) {
      uint32_t val = *cfg.comm_resource_config.listen_port;
      HIXL_CHK_BOOL_RET_STATUS(val >= kMinListenPort && val <= kMaxListenPort, PARAM_INVALID,
                               "comm_resource_config.listen_port must be in [%u, %u], got %u",
                               kMinListenPort, kMaxListenPort, val);
    }

    global_resource_config_ = std::move(cfg);
    return SUCCESS;
  } catch (const nlohmann::json::exception &e) {
    HIXL_LOGE(PARAM_INVALID, "Failed to parse GlobalResourceConfig json, exception:%s", e.what());
    return PARAM_INVALID;
  }
}

}  // namespace hixl
