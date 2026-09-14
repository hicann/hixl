/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "global_config.h"

#include "nlohmann/json.hpp"
#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/hixl_inner_types.h"
#include "common/json_utils.h"
#include "common/transfer_config.h"

namespace hixl {
namespace {
constexpr const char *kListenPort = "comm_resource_config.listen_port";
constexpr const char *kMaxActiveChannelsName = "comm_resource_config.max_active_channels";
constexpr int64_t kMinListenPort = 1;
constexpr int64_t kMaxListenPort = 65535;
constexpr int64_t kMinActiveChannels = 1;
constexpr int64_t kMaxActiveChannels = 8192;
constexpr const char *kMaxTransferCountPerBatchKey = "transfer_config.max_transfer_count_per_batch";

Status ParseListenPort(const nlohmann::json &json, CommResourceConfig &config) {
  const auto it = json.find(kListenPort);
  if (it == json.end()) {
    return SUCCESS;
  }

  const auto val = JsonToNumber<int64_t>(*it);
  if (val < kMinListenPort || val > kMaxListenPort) {
    HIXL_LOGE(PARAM_INVALID, "[GlobalConfig] listen_port out of range: %ld, must be in [%ld, %ld]", val, kMinListenPort,
              kMaxListenPort);
    return PARAM_INVALID;
  }

  config.listen_port = static_cast<uint32_t>(val);
  HIXL_LOGI("[GlobalConfig] listen_port=%u", *config.listen_port);
  return SUCCESS;
}

Status ParseQos(const nlohmann::json &json, CommResourceConfig &config) {
  const auto it = json.find(kQosName);
  if (it == json.end()) {
    return SUCCESS;
  }

  const auto val = JsonToNumber<int64_t>(*it);
  if (val < static_cast<int64_t>(kQosMin) || val > static_cast<int64_t>(kQosMax)) {
    HIXL_LOGE(PARAM_INVALID, "[GlobalConfig] qos out of range: %ld, must be in [%u, %u]", val, kQosMin, kQosMax);
    return PARAM_INVALID;
  }

  config.qos = static_cast<uint8_t>(val);
  HIXL_LOGI("[GlobalConfig] qos=%u", *config.qos);
  return SUCCESS;
}

Status ParseMaxActiveChannels(const nlohmann::json &json, CommResourceConfig &config) {
  const auto it = json.find(kMaxActiveChannelsName);
  if (it == json.end()) {
    return SUCCESS;
  }

  const auto val = JsonToNumber<int64_t>(*it);
  if (val < kMinActiveChannels || val > kMaxActiveChannels) {
    HIXL_LOGE(PARAM_INVALID, "[GlobalConfig] max_active_channels out of range: %ld, must be in [%ld, %ld]", val,
              kMinActiveChannels, kMaxActiveChannels);
    return PARAM_INVALID;
  }

  config.max_active_channels = static_cast<uint32_t>(val);
  HIXL_LOGI("[GlobalConfig] max_active_channels=%u", *config.max_active_channels);
  return SUCCESS;
}

Status ParseTransferConfig(const nlohmann::json &json, TransferConfigDesc &config) {
  const auto it = json.find(kMaxTransferCountPerBatchKey);
  if (it == json.end()) {
    return SUCCESS;
  }
  HIXL_CHK_BOOL_RET_STATUS((it->is_number_integer() || it->is_number_unsigned()) || it->is_string(), PARAM_INVALID,
                           "[GlobalConfig] %s must be an integer or decimal integer string",
                           kMaxTransferCountPerBatchKey);
  const auto val = JsonToNumber<int64_t>(*it);
  if (val < 1 || val > static_cast<int64_t>(kMaxTransferCountPerBatch)) {
    HIXL_LOGE(PARAM_INVALID, "[GlobalConfig] max_transfer_count_per_batch out of range: %ld, must be in [1, %u]", val,
              kMaxTransferCountPerBatch);
    return PARAM_INVALID;
  }
  config.max_transfer_count_per_batch = static_cast<uint32_t>(val);
  HIXL_LOGI("[GlobalConfig] max_transfer_count_per_batch=%u", config.max_transfer_count_per_batch);
  return SUCCESS;
}

Status ParseCommResourceConfig(const nlohmann::json &json, CommResourceConfig &config,
                               GlobalConfig::ParseTarget target) {
  if (target == GlobalConfig::ParseTarget::kAll || target == GlobalConfig::ParseTarget::kServer) {
    Status ret = ParseListenPort(json, config);
    if (ret != SUCCESS) {
      HIXL_LOGE(ret, "[GlobalConfig] Failed to parse listen_port");
      return ret;
    }
  }

  if (target == GlobalConfig::ParseTarget::kAll || target == GlobalConfig::ParseTarget::kClient) {
    Status ret = ParseQos(json, config);
    if (ret != SUCCESS) {
      HIXL_LOGE(ret, "[GlobalConfig] Failed to parse qos");
      return ret;
    }
  }
  HIXL_CHK_STATUS_RET(ParseMaxActiveChannels(json, config), "[GlobalConfig] Failed to parse max_active_channels");
  return SUCCESS;
}
}  // namespace

Status GlobalConfig::Parse(const char *config_str, GlobalConfig &result) {
  return Parse(config_str, result, ParseTarget::kAll);
}

Status GlobalConfig::Parse(const char *config_str, GlobalConfig &result, ParseTarget target) {
  if (config_str == nullptr || config_str[0] == '\0') {
    return SUCCESS;
  }

  try {
    auto json = nlohmann::json::parse(config_str);
    if (!json.is_object()) {
      HIXL_LOGE(PARAM_INVALID, "[GlobalConfig] config must be a JSON object");
      return PARAM_INVALID;
    }

    Status ret = ParseCommResourceConfig(json, result.comm_resource_config_, target);
    if (ret != SUCCESS) {
      HIXL_LOGE(ret, "[GlobalConfig] Failed to parse comm_resource_config");
      return ret;
    }
    HIXL_CHK_STATUS_RET(ParseTransferConfig(json, result.transfer_config_),
                        "[GlobalConfig] Failed to parse transfer_config");
    return SUCCESS;
  } catch (const nlohmann::json::exception &e) {
    HIXL_LOGE(PARAM_INVALID, "[GlobalConfig] Failed to parse config: %s", e.what());
    return PARAM_INVALID;
  }
}

std::optional<uint32_t> GlobalConfig::ListenPort() const {
  return comm_resource_config_.listen_port;
}

std::optional<uint8_t> GlobalConfig::Qos() const {
  return comm_resource_config_.qos;
}

std::optional<uint32_t> GlobalConfig::MaxActiveChannels() const {
  return comm_resource_config_.max_active_channels;
}

uint32_t GlobalConfig::MaxTransferCountPerBatch() const {
  return transfer_config_.max_transfer_count_per_batch;
}
}  // namespace hixl
