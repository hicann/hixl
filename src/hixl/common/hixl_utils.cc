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
#include <cstdio>
#include <cstring>
#include <fstream>
#include <unordered_set>
#include "securec.h"
#include "mmpa/mmpa_api.h"
#include "nlohmann/json.hpp"
#include "hixl_log.h"
#include "hixl_checker.h"

namespace hixl {
namespace {
constexpr uint32_t kBufferMaxSize = 128U;
constexpr const char kHccnConfPath[] = "/etc/hccn.conf";
constexpr const char kHccnToolPath[] = "/usr/local/Ascend/driver/tools/hccn_tool";

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
  std::string command;
  std::string output;
  if (mmAccess(kHccnToolPath) == EN_OK) {
    command = std::string(kHccnToolPath) + " -i " + std::to_string(phy_device_id) + " -ip -g";
  } else {
    std::string check_cmd = "command -v hccn_tool > /dev/null 2>&1";
    if (system(check_cmd.c_str()) != 0) {
      HIXL_LOGI("hccn_tool is not found in default path or PATH, skip querying device ip by tool.");
      return SUCCESS;
    }
    command = "hccn_tool -i " + std::to_string(phy_device_id) + " -ip -g";
  }

  HIXL_CHK_STATUS_RET(GetHccnOutput(command, output), "Getting hccn output failed.");
  ExtractIpAddress(output, ip);
  if (ip.empty()) {
    HIXL_LOGW("Please make sure device ip is set correctly.");
  }
  return SUCCESS;
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

Status ParseIpAddress(const std::string &ip_str, CommAddr &addr) {
  struct in_addr ipv4_addr;
  (void)memset_s(&ipv4_addr, sizeof(ipv4_addr), 0, sizeof(ipv4_addr));
  if (inet_pton(AF_INET, ip_str.c_str(), &ipv4_addr) == 1) {
    addr.type = COMM_ADDR_TYPE_IP_V4;
    addr.addr = ipv4_addr;
    return SUCCESS;
  }

  struct in6_addr ipv6_addr;
  (void)memset_s(&ipv6_addr, sizeof(ipv6_addr), 0, sizeof(ipv6_addr));
  if (inet_pton(AF_INET6, ip_str.c_str(), &ipv6_addr) == 1) {
    addr.type = COMM_ADDR_TYPE_IP_V6;
    addr.addr6 = ipv6_addr;
    return SUCCESS;
  }

  HIXL_LOGE(PARAM_INVALID, "Invalid IP address: %s", ip_str.c_str());
  return PARAM_INVALID;
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

Status CheckOptions(const std::map<AscendString, AscendString> &options) {
  static std::unordered_set<std::string> kOptionsFields = {hixl::OPTION_LOCAL_COMM_RES, hixl::OPTION_BUFFER_POOL, 
                                                           adxl::OPTION_LOCAL_COMM_RES, adxl::OPTION_BUFFER_POOL};
  for (const auto &pair : options) {
    HIXL_CHK_BOOL_RET_SPECIAL_STATUS(kOptionsFields.find(pair.first.GetString()) == kOptionsFields.end(), 
                                     PARAM_INVALID, 
                                     "Invalid options, options for hixl engine only support "
                                     "OPTION_LOCAL_COMM_RES and OPTION_BUFFER_POOL");
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

Status ParseEidAddress(const std::string &eid_str, CommAddr &addr) {
  // 检查字符串长度是否为32
  if (eid_str.length() != 32) {
    HIXL_LOGE(PARAM_INVALID, "Invalid EID format: %s. Expected 32 hexadecimal characters without colons.",
              eid_str.c_str());
    return PARAM_INVALID;
  }

  // 检查字符串是否只包含十六进制字符
  if (!std::all_of(eid_str.begin(), eid_str.end(), [](unsigned char c) { return std::isxdigit(c); })) {
    HIXL_LOGE(PARAM_INVALID, "Invalid EID: %s. Only hexadecimal characters are allowed.", eid_str.c_str());
    return PARAM_INVALID;
  }

  (void)memset_s(addr.eid, COMM_ADDR_EID_LEN, 0, COMM_ADDR_EID_LEN);
  // 每两个字符转换为一个uint8_t值
  for (size_t i = 0; i < COMM_ADDR_EID_LEN; ++i) {
    std::string segment = eid_str.substr(i * 2, 2);
    try {
      unsigned long value = std::stoul(segment, nullptr, 16);
      if (value > UINT8_MAX) {
        HIXL_LOGE(PARAM_INVALID, "Invalid segment %zu in EID: %s. Maximum value is 0xFF.", i, segment.c_str());
        return PARAM_INVALID;
      }
      addr.eid[i] = static_cast<uint8_t>(value);
    } catch (const std::invalid_argument &) {
      HIXL_LOGE(PARAM_INVALID, "Failed to convert segment %zu of EID: %s to integer.", i, segment.c_str());
      return PARAM_INVALID;
    } catch (const std::out_of_range &) {
      HIXL_LOGE(PARAM_INVALID, "Segment %zu of EID: %s is out of range.", i, segment.c_str());
      return PARAM_INVALID;
    }
  }
  addr.type = COMM_ADDR_TYPE_EID;
  return SUCCESS;
}

Status ConvertToEndpointDesc(const EndpointConfig &endpoint_config, EndpointDesc &endpoint, uint32_t dev_phy_id) {
  static const std::map<std::string, EndpointLocType> placement_map = {{kPlacementHost, ENDPOINT_LOC_TYPE_HOST},
                                                                       {kPlacementDevice, ENDPOINT_LOC_TYPE_DEVICE}};

  static const std::map<std::string, CommProtocol> protocol_map = {{kProtocolRoce, COMM_PROTOCOL_ROCE},
                                                                   {kProtocolUbCtp, COMM_PROTOCOL_UBC_CTP},
                                                                   {kProtocolUbTp, COMM_PROTOCOL_UBC_TP}};

  // 处理placement
  auto placement_it = placement_map.find(endpoint_config.placement);
  if (placement_it == placement_map.end()) {
    HIXL_LOGE(PARAM_INVALID, "Unsupported placement: %s", endpoint_config.placement.c_str());
    return PARAM_INVALID;
  }
  endpoint.loc.locType = placement_it->second;

  // 处理protocol
  auto protocol_it = protocol_map.find(endpoint_config.protocol);
  if (protocol_it == protocol_map.end()) {
    HIXL_LOGE(PARAM_INVALID, "Unsupported protocol: %s", endpoint_config.protocol.c_str());
    return PARAM_INVALID;
  }
  endpoint.protocol = protocol_it->second;

  // 处理ROCE协议的comm_id
  if (endpoint_config.protocol == kProtocolRoce) {
    HIXL_CHK_STATUS_RET(ParseIpAddress(endpoint_config.comm_id, endpoint.commAddr), "ParseIpAddress failed");
    return SUCCESS;
  }

  // 处理UB协议的comm_id
  if (endpoint_config.protocol == kProtocolUbCtp || endpoint_config.protocol == kProtocolUbTp) {
    HIXL_CHK_STATUS_RET(ParseEidAddress(endpoint_config.comm_id, endpoint.commAddr), "ParseEidAddress failed");
    // placement 为device则需要填写device结构体中的物理id
    if (endpoint.loc.locType == ENDPOINT_LOC_TYPE_DEVICE) {
      endpoint.loc.device.devPhyId = dev_phy_id;
    }
    return SUCCESS;
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

Status SerializeEndpointConfigList(const std::vector<EndpointConfig> &list, std::string &msg_str) {
  nlohmann::json j = nlohmann::json::array();
  try {
    for (const auto &ep : list) {
      nlohmann::json item;
      item["protocol"] = ep.protocol;
      item["comm_id"] = ep.comm_id;
      item["placement"] = ep.placement;
      item["plane"] = ep.plane;
      item["dst_eid"] = ep.dst_eid;
      item["net_instance_id"] = ep.net_instance_id;
      j.push_back(item);
    }
    msg_str = j.dump();
  } catch (const nlohmann::json::exception &e) {
    HIXL_LOGE(PARAM_INVALID, "Failed to dump endpoint list, exception:%s", e.what());
    return PARAM_INVALID;
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
}  // namespace hixl
