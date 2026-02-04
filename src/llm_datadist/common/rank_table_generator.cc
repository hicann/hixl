/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "rank_table_generator.h"
#include <atomic>
#include <set>
#include <fstream>
#include "mmpa/mmpa_api.h"
#include "nlohmann/json.hpp"
#include "acl/acl.h"
#include "llm_datadist/llm_datadist.h"
#include "common/hixl_utils.h"
#include "common/llm_utils.h"
#include "common/mem_utils.h"
#include "rank_table_generator_v1.h"
#include "rank_table_generator_v2.h"

namespace llm {
namespace {
const std::string kConfigVersion = "version";
constexpr const char kConfigVersionV1[] = "1.0";
constexpr const char kConfigVersionV2[] = "1.2";
constexpr uint32_t kVersionMaxLen = 32U;
constexpr uint32_t kBufferMaxSize = 128U;
}

std::unique_ptr<RankTableGenerator> RankTableGeneratorFactory::Create(const std::string &local_comm_res,
                                                                      const std::string &peer_comm_res) {
  nlohmann::json local_res;
  nlohmann::json peer_res;
  std::string local_version;
  std::string peer_version;
  try {
    local_res = nlohmann::json::parse(local_comm_res);
    peer_res = nlohmann::json::parse(peer_comm_res);
    LLMUtils::AssignRequired(local_version, kConfigVersion, local_res);
    LLMUtils::AssignRequired(peer_version, kConfigVersion, peer_res);
  } catch (const nlohmann::json::exception &e) {
    LLMLOGE(ge::LLM_PARAM_INVALID, "Invalid json, exception:%s", e.what());
    return nullptr;
  }

  if ((local_version != kConfigVersionV1 && local_version != kConfigVersionV2) ||
      (peer_version != kConfigVersionV1 && peer_version != kConfigVersionV2)) {
    LLMLOGE(ge::LLM_PARAM_INVALID, "version in option:%s only support %s and %s, local version:%s, peer version:%s.",
           llm_datadist::OPTION_LOCAL_COMM_RES, kConfigVersionV1, kConfigVersionV2,
           local_version.c_str(), peer_version.c_str());
    return nullptr;
  }

  auto version = std::min(local_version, peer_version);
  if (version == kConfigVersionV1) {
    return MakeUnique<RankTableGeneratorV1>(local_comm_res, peer_comm_res);
  }
  return MakeUnique<RankTableGeneratorV2>(local_comm_res, peer_comm_res);
}

ge::Status LocalCommResGenerator::Generate(const std::string &server_id,
                                           int32_t device_id,
                                           std::string &local_comm_res) {
  const static std::set<std::string> kV2Version = {
      "Ascend910_9391", "Ascend910_9381", "Ascend910_9392", "Ascend910_9382", "Ascend910_9372", "Ascend910_9362"
  };
  const char *version = aclrtGetSocName();
  LLM_CHECK_NOTNULL(version, "aclrt get soc name");
  const auto &it = kV2Version.find(version);
  if (it != kV2Version.cend()) {
    LLM_CHK_STATUS_RET(RankTableGeneratorV2::GenerateLocalCommRes(server_id, device_id, local_comm_res),
                      "Failed to generate local comm res, server_id:%s, device_id:%d.",
                      server_id.c_str(), device_id);
  } else {
    LLM_CHK_STATUS_RET(RankTableGeneratorV1::GenerateLocalCommRes(server_id, device_id, local_comm_res),
                      "Failed to generate local comm res, server_id:%s, device_id:%d.",
                      server_id.c_str(), device_id);
  }
  return ge::SUCCESS;
}

void LocalCommResGenerator::ExtractIpAddress(const std::string &output_str, std::string &ip) {
  const std::string prefix = "ipaddr:";
  auto pos = output_str.find(prefix);
  if (pos != std::string::npos) {
    pos += prefix.length();
    auto end = output_str.find("\n", pos);
    ip = output_str.substr(pos, end - pos);
  }
}

ge::Status LocalCommResGenerator::GetHccnOutput(const std::string &command, std::string &result) {
  std::string command_with_stderr = command + " 2>&1";
  std::array<char, kBufferMaxSize> buffer;
  std::unique_ptr<FILE, int (*)(FILE *)> pipe(popen(command_with_stderr.c_str(), "r"), pclose);
  if (!pipe) {
    LLMLOGE(ge::FAILED, "calling command %s failed, cannot create subprocess.", command_with_stderr.c_str());
    return ge::FAILED;
  }
  while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
    result += buffer.data();
  }
  return ge::SUCCESS;
}

ge::Status LocalCommResGenerator::ExecuteCommandAndPassIp(const std::string &command, std::string &output, std::string &ip) {
  LLM_CHK_STATUS_RET(GetHccnOutput(command, output), "Getting hccn output failed.");
  ExtractIpAddress(output, ip);
  if (ip.empty()) {
    LLMEVENT("Please make sure device ip is set correctly.");
  }
  return ge::SUCCESS;
}

ge::Status LocalCommResGenerator::GetIpAddressFromHccnTool(uint32_t phy_device_id, std::string &ip) {
  std::string command;
  std::string output;
  constexpr const char *kHccnToolPath = "/usr/local/Ascend/driver/tools/hccn_tool";
  if (mmAccess(kHccnToolPath) == EN_OK) {
    command = "/usr/local/Ascend/driver/tools/hccn_tool -i " + std::to_string(phy_device_id) + " -ip -g";
  } else {
    std::string cmd = "command -v hccn_tool > /dev/null 2>&1";
    if (system(cmd.c_str()) != 0) {
      LLMEVENT("Please add hccn_tool install path to PATH environment variable.");
      return ge::SUCCESS;
    }
    command = "hccn_tool -i " + std::to_string(phy_device_id) + " -ip -g";
  }
  LLM_CHK_STATUS_RET(ExecuteCommandAndPassIp(command, output, ip), "Getting ip address from hccn_tool failed.");
  return ge::SUCCESS;
}

ge::Status LocalCommResGenerator::GetDeviceIp(int32_t phy_device_id, std::string &device_ip) {
  constexpr const char *kFilePath = "/etc/hccn.conf";
  char_t resolved_path[MMPA_MAX_PATH] = {};
  auto mm_ret = mmRealPath(kFilePath, &(resolved_path[0U]), MMPA_MAX_PATH);
  if (mm_ret == EN_OK) {
    LLM_CHK_BOOL_RET_STATUS(mmAccess(resolved_path) == EN_OK,
                            ge::FAILED, "Can not access file:%s, reason:%s", resolved_path, strerror(errno));

    std::ifstream file(resolved_path);
    LLM_CHK_BOOL_RET_STATUS(file.is_open(), ge::FAILED, "Faile to open file:%s", kFilePath);

    std::string line;
    std::string target_key = "address_" + std::to_string(phy_device_id) + "=";
    constexpr size_t kValidItemNum = 2U;

    while (std::getline(file, line)) {
      LLMLOGI("read file:%s, get line:%s", resolved_path, line.c_str());
      if (line.find(target_key) != 0) {
        continue;
      }
      const auto addess_val = hixl::Split(line, '=');
      LLM_CHK_BOOL_RET_STATUS(addess_val.size() == kValidItemNum, ge::FAILED,
                            "address format is invalid: %s, expect address_${phy_device_id}=${device_ip}",
                            line.c_str());
      device_ip = addess_val.back();
      LLM_CHK_BOOL_RET_STATUS(hixl::CheckIp(device_ip) == hixl::SUCCESS, ge::LLM_PARAM_INVALID,
                              "device ip:%s is invalid.", device_ip.c_str());
      return ge::SUCCESS;
    }
  } else {
    LLMEVENT("/etc/hccn.conf does not exist, trying to use hccn_tool to get device_ip.");
    std::string ip;
    LLM_CHK_STATUS_RET(GetIpAddressFromHccnTool(phy_device_id, ip), "Getting ip from hccn tool failed.");
    if (!ip.empty()) {
      device_ip = ip;
      LLM_CHK_BOOL_RET_STATUS(hixl::CheckIp(device_ip) == hixl::SUCCESS, ge::LLM_PARAM_INVALID,
                              "device ip:%s is invalid.", device_ip.c_str());
      return ge::SUCCESS;
    }
  }

  // hccs does not require device_ip, device_ip will be empty
  return ge::SUCCESS;
}
}  // namespace llm

