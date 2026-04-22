/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hixl_utils.h"

#include <arpa/inet.h>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <unordered_set>
#include "nlohmann/json.hpp"
#include "securec.h"
#include "acl/acl.h"
#include "mmpa/mmpa_api.h"
#include "hixl_log.h"
#include "hixl_checker.h"

namespace hixl {
namespace {
constexpr uint32_t kBufferMaxSize = 128U;
constexpr const char kHccnConfPath[] = "/etc/hccn.conf";
constexpr const char kHccnToolPath[] = "/usr/local/Ascend/driver/tools/hccn_tool";

std::string GetHccnToolPath() {
  if (mmAccess(kHccnToolPath) == EN_OK) {
    return kHccnToolPath;
  }
  std::string check_cmd = "command -v hccn_tool > /dev/null 2>&1";
  if (system(check_cmd.c_str()) != 0) {
    HIXL_EVENT("hccn_tool is not found in default path or PATH.");
    return "";
  }
  return "hccn_tool";
}

void ExtractIpAddress(const std::string &output_str, std::string &ip) {
  const std::string prefix = "ipaddr:";
  auto pos = output_str.find(prefix);
  if (pos == std::string::npos) {
    return;
  }
  pos += prefix.length();
  auto end = output_str.find("\n", pos);
  ip = output_str.substr(pos, end - pos);
}

Status GetHccnOutput(const std::string &command, std::string &result) {
  std::string command_with_stderr = command + " 2>&1";
  std::array<char, kBufferMaxSize> buffer{};
  std::unique_ptr<FILE, int (*)(FILE *)> pipe(popen(command_with_stderr.c_str(), "r"), pclose);
  if (!pipe) {
    HIXL_LOGE(FAILED, "calling command %s failed, cannot create subprocess.", command_with_stderr.c_str());
    return FAILED;
  }

  while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
    result += buffer.data();
  }
  return SUCCESS;
}

Status GetIpAddressFromHccnTool(uint32_t phy_device_id, std::string &ip) {
  auto hccn_tool_path = GetHccnToolPath();
  if (hccn_tool_path.empty()) {
    HIXL_EVENT("hccn_tool is not found in default path or PATH, skip querying device ip by tool.");
    return SUCCESS;
  }
  std::string command = hccn_tool_path + " -i " + std::to_string(phy_device_id) + " -ip -g";
  std::string output;
  HIXL_CHK_STATUS_RET(GetHccnOutput(command, output), "Getting hccn output failed.");
  ExtractIpAddress(output, ip);
  if (ip.empty()) {
    HIXL_LOGW("Please make sure device ip is set correctly.");
  }
  return SUCCESS;
}

struct CommResourceConfig {
  std::vector<std::string> protocol_desc;
};

void from_json(const nlohmann::json &j, CommResourceConfig &config) {
  auto json_protocol_desc = j.at("comm_resource_config.protocol_desc");
  if (json_protocol_desc.is_array()) {
    json_protocol_desc.get_to(config.protocol_desc);
  }
}

Status ParseCommResourceConfig(const std::string &json_str, CommResourceConfig &config) {
  try {
    auto j = nlohmann::json::parse(json_str);
    j.get_to(config);
    return SUCCESS;
  } catch (const nlohmann::json::exception &e) {
    HIXL_LOGE(PARAM_INVALID, "parse CommResourceConfig json failed, json=%s, exception=%s", json_str.c_str(), e.what());
    return PARAM_INVALID;
  }
}
}  // namespace
Status HcclError2Status(HcclResult ret) {
  static const std::map<HcclResult, Status> result2status = {
      {HCCL_SUCCESS, SUCCESS},
      {HCCL_E_PARA, PARAM_INVALID},
      {HCCL_E_TIMEOUT, TIMEOUT},
      {HCCL_E_NOT_SUPPORT, UNSUPPORTED},
  };
  const auto &it = result2status.find(ret);
  if (it != result2status.cend()) {
    return it->second;
  }
  return FAILED;
}

Status CheckIp(const std::string &ip) {
  struct in_addr addr;
  struct sockaddr_in6 ipv6_addr;
  HIXL_CHK_BOOL_RET_STATUS(
      inet_pton(AF_INET, ip.c_str(), &addr) == 1 || inet_pton(AF_INET6, ip.c_str(), &ipv6_addr.sin6_addr) == 1,
      hixl::PARAM_INVALID, "%s is not a valid ip address", ip.c_str());
  return hixl::SUCCESS;
}

Status GetDeviceIp(int32_t phy_device_id, std::string &device_ip) {
  char resolved_path[MMPA_MAX_PATH] = {};
  auto mm_ret = mmRealPath(kHccnConfPath, resolved_path, MMPA_MAX_PATH);
  if (mm_ret == EN_OK) {
    HIXL_CHK_BOOL_RET_STATUS(mmAccess(resolved_path) == EN_OK, FAILED, "Can not access file:%s, reason:%s",
                             resolved_path, strerror(errno));

    std::ifstream file(resolved_path);
    HIXL_CHK_BOOL_RET_STATUS(file.is_open(), FAILED, "Failed to open file:%s", kHccnConfPath);

    std::string line;
    std::string target_key = "address_" + std::to_string(phy_device_id) + "=";
    constexpr size_t kValidItemNum = 2U;
    while (std::getline(file, line)) {
      if (line.find(target_key) != 0) {
        continue;
      }

      const auto address_val = Split(line, '=');
      HIXL_CHK_BOOL_RET_STATUS(address_val.size() == kValidItemNum, FAILED,
                               "address format is invalid: %s, expect address_${phy_device_id}=${device_ip}",
                               line.c_str());
      device_ip = address_val.back();
      HIXL_CHK_STATUS_RET(CheckIp(device_ip), "device ip:%s is invalid.", device_ip.c_str());
      return SUCCESS;
    }
  } else {
    HIXL_LOGI("%s does not exist, trying to use hccn_tool to get device_ip.", kHccnConfPath);
    std::string ip;
    HIXL_CHK_STATUS_RET(GetIpAddressFromHccnTool(static_cast<uint32_t>(phy_device_id), ip),
                        "Getting ip from hccn tool failed.");
    if (!ip.empty()) {
      device_ip = ip;
      HIXL_CHK_STATUS_RET(CheckIp(device_ip), "device ip:%s is invalid.", device_ip.c_str());
    }
  }

  return SUCCESS;
}

Status GetBondIpAddress(int32_t phy_device_id, std::string &ip) {
  // query command is 'hccn_tool -g -ip -i 0 -d bond0'
  const std::string bond_name = "bond" + std::to_string(phy_device_id);
  auto hccn_tool_path = GetHccnToolPath();
  HIXL_CHK_BOOL_RET_STATUS(!hccn_tool_path.empty(), FAILED, "querying bond ip failed as hccn_tool not found.");

  const std::string command = hccn_tool_path + " -g -ip -i " + std::to_string(phy_device_id) + " -d " + bond_name;
  std::string output;
  HIXL_CHK_STATUS_RET(GetHccnOutput(command, output), "Getting hccn output for bond ip failed, command=%s.",
                      command.c_str());
  ExtractIpAddress(output, ip);
  HIXL_CHK_BOOL_RET_STATUS(
      !ip.empty(), FAILED,
      "query device=%d bond ip is empty, please make sure bond ip is set correctly, query command=%s.", phy_device_id,
      command.c_str());
  HIXL_LOGI("get bond ip from device[%d]=%s", phy_device_id, ip.c_str());
  return SUCCESS;
}

Status CheckOptions(const std::map<AscendString, AscendString> &options) {
  static std::unordered_set<std::string> kOptionsFields = {hixl::OPTION_LOCAL_COMM_RES, hixl::OPTION_BUFFER_POOL, 
                                                           adxl::OPTION_LOCAL_COMM_RES, adxl::OPTION_BUFFER_POOL,
                                                           hixl::OPTION_RDMA_TRAFFIC_CLASS, adxl::OPTION_RDMA_TRAFFIC_CLASS,
                                                           hixl::OPTION_RDMA_SERVICE_LEVEL, adxl::OPTION_RDMA_SERVICE_LEVEL,
                                                           hixl::OPTION_GLOBAL_RESOURCE_CONFIG};
  for (const auto &pair : options) {
    HIXL_CHK_BOOL_RET_SPECIAL_STATUS(kOptionsFields.find(pair.first.GetString()) == kOptionsFields.end(), 
                                     PARAM_INVALID, 
                                     "Invalid option '%s' is not supported, options for hixl engine only support "
                                     "OPTION_LOCAL_COMM_RES, OPTION_BUFFER_POOL, OPTION_RDMA_TRAFFIC_CLASS and OPTION_RDMA_SERVICE_LEVEL",
                                     pair.first.GetString());
    if ((pair.first == hixl::OPTION_BUFFER_POOL) || (pair.first == adxl::OPTION_BUFFER_POOL)) {
      HIXL_CHK_BOOL_RET_STATUS(pair.second.GetString() == std::string("0:0"), 
                               PARAM_INVALID, 
                               "Invalid option fields, OPTION_BUFFER_POOL for hixl engine only supports 0:0");
    }
  }
  return SUCCESS;
}

std::vector<std::string, std::allocator<std::string>> Split(const std::string &str, const char delim) {
  std::vector<std::string, std::allocator<std::string>> elems;
  if (str.empty()) {
    (void)elems.emplace_back("");
    return elems;
  }

  std::stringstream ss(str);
  std::string item;
  while (getline(ss, item, delim)) {
    (void)elems.push_back(item);
  }

  const auto str_size = str.size();
  if ((str_size > 0U) && (str[str_size - 1U] == delim)) {
    (void)elems.emplace_back("");
  }
  return elems;
}

Status ParseListenInfo(const std::string &listen_info, std::string &listen_ip, int32_t &listen_port) {
  std::vector<std::string> listen_infos;
  size_t left = listen_info.find('[');
  size_t right = listen_info.find(']');
  if (left != std::string::npos && right != std::string::npos && left < right) {
    listen_infos.emplace_back(listen_info.substr(left + 1, right - left - 1));
    size_t colon = listen_info.find(':', right);
    if (colon != std::string::npos) {
      listen_infos.emplace_back(listen_info.substr(colon + 1));
    }
  } else {
    listen_infos = Split(listen_info, ':');
  }
  HIXL_CHK_BOOL_RET_STATUS(listen_infos.size() >= 1U, PARAM_INVALID,
                           "listen info is invalid: %s, expect ${ip}:${port} or ${ip}", listen_info.c_str());
  listen_ip = listen_infos[0];
  HIXL_CHK_STATUS_RET(CheckIp(listen_ip), "IP is invalid: %s, listen info = %s", listen_ip.c_str(),
                      listen_info.c_str());
  if (listen_infos.size() > 1U) {
    HIXL_CHK_STATUS_RET(ToNumber(listen_infos[1], listen_port), "Port:%s is invalid.", listen_infos[1].c_str());
  }
  return SUCCESS;
}

Status CheckAddrOverlap(const AddrInfo &cur_info, const std::map<MemHandle, AddrInfo> &addr_map, bool &is_duplicate,
                        MemHandle &existing_handle) {
  is_duplicate = false;
  for (const auto &it : addr_map) {
    const AddrInfo &info = it.second;
    // 检查地址范围是否重叠且内存类型相同
    if (!((cur_info.end_addr <= info.start_addr) || (cur_info.start_addr >= info.end_addr)) &&
        (cur_info.mem_type == info.mem_type)) {
      if (info.start_addr == cur_info.start_addr && info.end_addr == cur_info.end_addr) {
        // 完全相同的内存区域，标记为重复注册
        is_duplicate = true;
        existing_handle = it.first;
        return SUCCESS;
      }
      HIXL_LOGE(PARAM_INVALID,
                "Mem addr range overlap with existing registered mem, "
                "new mem range:[0x%lx, 0x%lx), existing mem range:[0x%lx, 0x%lx).",
                cur_info.start_addr, cur_info.end_addr, info.start_addr, info.end_addr);
      return PARAM_INVALID;
    }
  }
  return SUCCESS;
}

std::string MemTypeToString(MemType type) {
  switch (type) {
    case MEM_DEVICE:
      return "device";
    case MEM_HOST:
      return "host";
    default:
      return "unknown";
  }
}

std::string TransferOpToString(TransferOp op) {
  switch (op) {
    case READ:
      return "read";
    case WRITE:
      return "write";
    default:
      return "unknown";
  }
}

TemporaryRtContext::TemporaryRtContext(aclrtContext context) {
  (void)aclrtGetCurrentContext(&prev_context_);
  HIXL_LOGI("Get current aclrt ctx:%p", prev_context_);
  if (context != nullptr && prev_context_ != context) {
    HIXL_CHK_ACL(aclrtSetCurrentContext(context));
    HIXL_LOGI("Set current aclrt ctx:%p", prev_context_);
  }
}

TemporaryRtContext::~TemporaryRtContext() {
  if (prev_context_ != nullptr) {
    HIXL_CHK_STATUS(aclrtSetCurrentContext(prev_context_));
    HIXL_LOGI("Restore current aclrt ctx:%p", prev_context_);
  }
}

Status ParseConfigProtocolDesc(const std::map<AscendString, AscendString> &options,
                               std::vector<std::string> &protocol_desc) {
  auto find_ret = options.find(OPTION_GLOBAL_RESOURCE_CONFIG);
  if (find_ret != options.cend()) {
    HIXL_LOGD("option[%s] config value=%s.", OPTION_GLOBAL_RESOURCE_CONFIG, find_ret->second.GetString());
    CommResourceConfig config{};
    HIXL_CHK_STATUS_RET(ParseCommResourceConfig(find_ret->second.GetString(), config),
                        "Parse comm resource config failed.");
    protocol_desc = std::move(config.protocol_desc);
  }
  return SUCCESS;
}
}  // namespace hixl
