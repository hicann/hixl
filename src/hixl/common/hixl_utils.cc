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
#include <cinttypes>
#include <endian.h>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include "securec.h"
#include "acl/acl.h"
#include "mmpa/mmpa_api.h"
#include "hixl_log.h"
#include "hixl_checker.h"
#include "hixl_inner_types.h"

namespace hixl {
namespace {
constexpr uint32_t kBufferMaxSize = 128U;
constexpr const char kHccnConfPath[] = "/etc/hccn.conf";
constexpr const char kHccnToolPath[] = "/usr/local/Ascend/driver/tools/hccn_tool";
constexpr const char kHccnConfIpv4KeyPrefix[] = "address_";
constexpr const char kHccnConfIpv6KeyPrefix[] = "IPv6address_";
constexpr const char kHccnToolIpv4Query[] = "-ip -g";
constexpr const char kHccnToolIpv6Query[] = "-ip -inet6 -g";
constexpr size_t kValidHccnConfItemNum = 2U;

const std::set<std::string> kSocV2 = {"Ascend910B1", "Ascend910B2",  "Ascend910B3",
                                      "Ascend910B4", "Ascend910B2C", "Ascend910B4-1"};
const std::set<std::string> kSocV3 = {"Ascend910_9391", "Ascend910_9381", "Ascend910_9392",
                                      "Ascend910_9382", "Ascend910_9372", "Ascend910_9362"};

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

Status QueryIpAddressFromHccnTool(const std::string &hccn_tool_path, uint32_t phy_device_id,
                                  const std::string &query_option, std::string &ip) {
  ip.clear();
  const std::string command = hccn_tool_path + " -i " + std::to_string(phy_device_id) + " " + query_option;
  std::string output;
  HIXL_CHK_STATUS_RET(GetHccnOutput(command, output), "Getting hccn output failed.");
  ExtractIpAddress(output, ip);
  return SUCCESS;
}

Status GetIpAddressFromHccnTool(uint32_t phy_device_id, std::string &ip) {
  auto hccn_tool_path = GetHccnToolPath();
  if (hccn_tool_path.empty()) {
    HIXL_EVENT("hccn_tool is not found in default path or PATH, skip querying device ip by tool.");
    return SUCCESS;
  }
  HIXL_CHK_STATUS_RET(QueryIpAddressFromHccnTool(hccn_tool_path, phy_device_id, kHccnToolIpv4Query, ip),
                      "Getting IPv4 address from hccn_tool failed.");
  if (ip.empty()) {
    HIXL_CHK_STATUS_RET(QueryIpAddressFromHccnTool(hccn_tool_path, phy_device_id, kHccnToolIpv6Query, ip),
                        "Getting IPv6 address from hccn_tool failed.");
  }
  if (ip.empty()) {
    HIXL_LOGW("Please make sure device ip is set correctly.");
  }
  return SUCCESS;
}

Status ReadDeviceIpFromHccnConf(std::ifstream &file, const std::string &target_key, std::string &device_ip) {
  file.clear();
  file.seekg(0, std::ios::beg);
  std::string line;
  while (std::getline(file, line)) {
    if (line.find(target_key) != 0) {
      continue;
    }

    const auto address_val = Split(line, '=');
    HIXL_CHK_BOOL_RET_STATUS(address_val.size() == kValidHccnConfItemNum, FAILED,
                             "address format is invalid: %s, expect %s${device_ip}", line.c_str(), target_key.c_str());
    device_ip = address_val.back();
    HIXL_CHK_STATUS_RET(CheckIp(device_ip), "device ip:%s is invalid.", device_ip.c_str());
    return SUCCESS;
  }
  return SUCCESS;
}

}  // namespace
Status ConvertHcommErrorToStatus(HcclResult ret) {
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
  device_ip.clear();
  char resolved_path[MMPA_MAX_PATH] = {};
  auto mm_ret = mmRealPath(kHccnConfPath, resolved_path, MMPA_MAX_PATH);
  if (mm_ret == EN_OK) {
    HIXL_CHK_BOOL_RET_STATUS(mmAccess(resolved_path) == EN_OK, FAILED, "Can not access file:%s, reason:%s",
                             resolved_path, strerror(errno));

    std::ifstream file(resolved_path);
    HIXL_CHK_BOOL_RET_STATUS(file.is_open(), FAILED, "Failed to open file:%s", kHccnConfPath);

    const std::string ipv4_key = kHccnConfIpv4KeyPrefix + std::to_string(phy_device_id) + "=";
    HIXL_CHK_STATUS_RET(ReadDeviceIpFromHccnConf(file, ipv4_key, device_ip), "Getting IPv4 address failed.");
    if (!device_ip.empty()) {
      return SUCCESS;
    }

    const std::string ipv6_key = kHccnConfIpv6KeyPrefix + std::to_string(phy_device_id) + "=";
    HIXL_CHK_STATUS_RET(ReadDeviceIpFromHccnConf(file, ipv6_key, device_ip), "Getting IPv6 address failed.");
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

Status GetBondIpAddress(int32_t dev_logic_id, uint32_t slot_id, std::string &ip) {
  // query command is 'hccn_tool -g -ip -i 0 -d bond0'
  const std::string bond_name = "bond" + std::to_string(slot_id);
  auto hccn_tool_path = GetHccnToolPath();
  if (hccn_tool_path.empty()) {
    HIXL_EVENT("querying bond ip failed as hccn_tool not found.");
    return FAILED;
  }

  const std::string command = hccn_tool_path + " -g -ip -i " + std::to_string(dev_logic_id) + " -d " + bond_name;
  std::string output;
  if (GetHccnOutput(command, output) != SUCCESS) {
    HIXL_EVENT("Getting hccn output for bond ip failed, command=%s.", command.c_str());
    return FAILED;
  }
  ExtractIpAddress(output, ip);
  if (ip.empty()) {
    HIXL_EVENT("query device=%d bond ip is empty, please make sure bond ip is set correctly, query command=%s.",
               dev_logic_id, command.c_str());
    return FAILED;
  }
  HIXL_LOGI("get bond ip from device[%d]=%s", dev_logic_id, ip.c_str());
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

std::string FormatCommAddr(const CommAddr &addr) {
  switch (addr.type) {
    case COMM_ADDR_TYPE_IP_V4: {
      char buf[INET_ADDRSTRLEN] = {};
      (void)inet_ntop(AF_INET, &addr.addr, buf, sizeof(buf));
      return std::string("IPv4:") + buf;
    }
    case COMM_ADDR_TYPE_IP_V6: {
      char buf[INET6_ADDRSTRLEN] = {};
      (void)inet_ntop(AF_INET6, &addr.addr6, buf, sizeof(buf));
      return std::string("IPv6:") + buf;
    }
    case COMM_ADDR_TYPE_ID: {
      char buf[32] = {};  // "ID:0x" + 8 hex digits + '\0' = 14 bytes max, 32 is safe
      (void)snprintf_s(buf, sizeof(buf), sizeof(buf) - 1, "ID:0x%x", addr.id);
      return std::string(buf);
    }
    case COMM_ADDR_TYPE_EID: {
      // EID is 16 bytes: first 8 bytes = subnetPrefix, last 8 bytes = interfaceId (network byte order)
      uint64_t subnet_prefix = 0;
      uint64_t interface_id = 0;
      (void)memcpy_s(&subnet_prefix, sizeof(subnet_prefix), addr.eid, sizeof(subnet_prefix));
      (void)memcpy_s(&interface_id, sizeof(interface_id), addr.eid + sizeof(subnet_prefix), sizeof(interface_id));
      subnet_prefix = be64toh(subnet_prefix);
      interface_id = be64toh(interface_id);
      char buf[64] = {};  // "EID[" + 16 hex + ":" + 16 hex + "]" + '\0' = 38 bytes max, 64 is safe
      (void)snprintf_s(buf, sizeof(buf), sizeof(buf) - 1, "EID[%016" PRIx64 ":%016" PRIx64 "]", subnet_prefix,
                       interface_id);
      return std::string(buf);
    }
    default:
      return "UNKNOWN";
  }
}

std::string ProtocolToString(CommProtocol protocol) {
  switch (protocol) {
    case COMM_PROTOCOL_HCCS:
      return kProtocolHccs;
    case COMM_PROTOCOL_ROCE:
      return kProtocolRoce;
    case COMM_PROTOCOL_UBC_CTP:
      return kProtocolUbCtp;
    case COMM_PROTOCOL_UBOE:
      return kProtocolUboe;
    case COMM_PROTOCOL_UBG:
      return kProtocolUbRtp;
    default:
      return "UNKNOWN(" + std::to_string(static_cast<int32_t>(protocol)) + ")";
  }
}

std::string EndpointToString(const EndpointDesc &ep) {
  std::string result = "protocol=" + ProtocolToString(ep.protocol) + ", addr=" + FormatCommAddr(ep.commAddr);
  if (ep.loc.locType == ENDPOINT_LOC_TYPE_DEVICE) {
    result += ", devPhyId=" + std::to_string(ep.loc.device.devPhyId);
  } else if (ep.loc.locType == ENDPOINT_LOC_TYPE_HOST) {
    result += ", hostId=" + std::to_string(ep.loc.host.id);
  }
  return result;
}

TemporaryRtContext::TemporaryRtContext(aclrtContext context) {
  (void)aclrtGetCurrentContext(&prev_context_);
  HIXL_LOGI("Get current aclrt ctx:%p", prev_context_);
  if (context != nullptr && prev_context_ != context) {
    HIXL_LOGI("Set new current aclrt ctx:%p", context);
    HIXL_CHK_ACL(aclrtSetCurrentContext(context));
  }
}

TemporaryRtContext::~TemporaryRtContext() {
  if (prev_context_ != nullptr) {
    HIXL_LOGI("Restore previous aclrt ctx:%p", prev_context_);
    HIXL_CHK_STATUS(aclrtSetCurrentContext(prev_context_));
  }
}

bool IsIntraRoceEnabled() {
  const char *env = std::getenv("HCCL_INTRA_ROCE_ENABLE");
  return env != nullptr && std::string(env) == "1";
}

const char *IntraRoceEnableStatusStr() {
  return IsIntraRoceEnabled() ? "HCCL_INTRA_ROCE_ENABLE is set" : "HCCL_INTRA_ROCE_ENABLE is not set";
}

Status GetSocName(std::string &soc_name) {
  const char *soc_name_cstr = aclrtGetSocName();
  HIXL_CHK_BOOL_RET_STATUS(soc_name_cstr != nullptr, FAILED, "aclrtGetSocName returned nullptr");
  soc_name = soc_name_cstr;
  HIXL_CHK_BOOL_RET_STATUS(!soc_name.empty(), FAILED, "soc_name is empty");
  return SUCCESS;
}

SocType GetSocTypeByName(const std::string &soc_name) {
  if (kSocV2.find(soc_name) != kSocV2.end()) {
    return SocType::kV2;
  }

  if (kSocV3.find(soc_name) != kSocV3.end()) {
    return SocType::kV3;
  }

  if (soc_name.find("Ascend950") != std::string::npos) {
    return SocType::kV5;
  }

  return SocType::kOther;
}

Status GetSocType(SocType &soc_type) {
  std::string soc_name;
  HIXL_CHK_STATUS_RET(GetSocName(soc_name), "GetSocName failed");
  soc_type = GetSocTypeByName(soc_name);
  return SUCCESS;
}

}  // namespace hixl
