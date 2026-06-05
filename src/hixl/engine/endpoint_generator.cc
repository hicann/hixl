/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "endpoint_generator.h"

#include <algorithm>
#include <arpa/inet.h>
#include <cctype>
#include <cstdlib>
#include <limits>
#include <map>
#include <set>
#include <string>

#include "dsmi_proxy.h"
#include "adxl/adxl_types.h"
#include "nlohmann/json.hpp"
#include "securec.h"
#include "acl/acl.h"
#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/hixl_utils.h"
#include "engine/local_comm_res_generator_v1.h"

namespace hixl {
namespace {
constexpr const char kConfigVersion[] = "1.3";
constexpr const char kUboeProtocolDesc[] = "uboe:device";
constexpr const char kDefaultUboeNetInstanceId[] = "default_superpod1_1";
constexpr size_t kEidHexStrLen = COMM_ADDR_EID_LEN * 2U;

const std::set<std::string> kSocV2 = {"Ascend910B1", "Ascend910B2",  "Ascend910B3",
                                      "Ascend910B4", "Ascend910B2C", "Ascend910B4-1"};
const std::set<std::string> kSocV3 = {"Ascend910_9391", "Ascend910_9381", "Ascend910_9392",
                                      "Ascend910_9382", "Ascend910_9372", "Ascend910_9362"};

bool IsIntraRoceEnabled() {
  const char *env_ret = std::getenv("HCCL_INTRA_ROCE_ENABLE");
  HIXL_CHK_BOOL_RET_SPECIAL_STATUS(env_ret == nullptr, false, "HCCL_INTRA_ROCE_ENABLE is not set");
  return std::string(env_ret) == "1";
}

Status GetUboeIp(std::string &ip) {
  int32_t dev_id = 0;
  HIXL_CHK_ACL_RET(aclrtGetDevice(&dev_id));

  int32_t dev_logic_id = dev_id;
  HIXL_CHK_ACL_RET(aclrtGetLogicDevIdByUserDevId(dev_id, &dev_logic_id));
  HIXL_LOGI("current dev_id=%d, dev_logic_id=%d", dev_logic_id, dev_logic_id);

  uint32_t slot_id = 0;
  auto get_ret = DsmiProxy::GetDevSlotId(dev_logic_id, slot_id);
  if (get_ret != SUCCESS) {
    HIXL_LOGW("can't find dev slot_id, use dev_logic_id instead, ret=%u, dev_logic_id=%d.", get_ret, dev_logic_id);
    slot_id = static_cast<uint32_t>(dev_logic_id);
  }
  return GetBondIpAddress(dev_logic_id, slot_id, ip);
}

Status GenDefaultUboeEndpointConfig(EndpointConfig &endpoint_config) {
  std::string uboe_ip;
  HIXL_CHK_STATUS_RET(GetUboeIp(uboe_ip), "get uboe ip failed");
  endpoint_config.protocol = kProtocolUboe;
  endpoint_config.comm_id = uboe_ip;
  endpoint_config.placement = kPlacementDevice;
  endpoint_config.net_instance_id = kDefaultUboeNetInstanceId;
  return SUCCESS;
}

Status ParseRequiredJsonField(const nlohmann::json &json_obj, const std::string &field_name, std::string &field_value) {
  if (!json_obj.contains(field_name)) {
    HIXL_LOGE(PARAM_INVALID, "Missing required field '%s' in EndpointConfig", field_name.c_str());
    return PARAM_INVALID;
  }

  try {
    field_value = json_obj[field_name].get<std::string>();
    return SUCCESS;
  } catch (const nlohmann::json::exception &e) {
    HIXL_LOGE(PARAM_INVALID, "Failed to parse field '%s', exception: %s", field_name.c_str(), e.what());
    return PARAM_INVALID;
  }
}

Status ParseOptionalJsonField(const nlohmann::json &json_obj, const std::string &field_name, std::string &field_value) {
  if (!json_obj.contains(field_name)) {
    return SUCCESS;
  }

  try {
    field_value = json_obj[field_name].get<std::string>();
    return SUCCESS;
  } catch (const nlohmann::json::exception &e) {
    HIXL_LOGE(PARAM_INVALID, "Failed to parse field '%s', exception: %s", field_name.c_str(), e.what());
    return PARAM_INVALID;
  }
}

void ParseDeviceInfo(const nlohmann::json &item, EndpointConfig &endpoint) {
  if (!item.contains("device_info") || !item["device_info"].is_object()) {
    return;
  }

  const auto &device_info = item["device_info"];
  if (device_info.contains("phy_device_id") && device_info["phy_device_id"].is_number_integer()) {
    endpoint.device_info.phy_device_id = device_info["phy_device_id"].get<int32_t>();
  }
  if (device_info.contains("super_device_id") && device_info["super_device_id"].is_number_integer()) {
    endpoint.device_info.super_device_id = device_info["super_device_id"].get<int64_t>();
  }
  if (device_info.contains("super_pod_id") && device_info["super_pod_id"].is_number_integer()) {
    endpoint.device_info.super_pod_id = device_info["super_pod_id"].get<int64_t>();
  }
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

Status ParseHccsCommId(const std::string &comm_id_str, uint32_t &device_id) {
  constexpr size_t kMaxHccsCommIdLen = 10U;
  if (comm_id_str.empty() || comm_id_str.length() > kMaxHccsCommIdLen) {
    HIXL_LOGE(PARAM_INVALID, "Invalid hccs comm_id length: %zu, max allowed: %zu", comm_id_str.length(),
              kMaxHccsCommIdLen);
    return PARAM_INVALID;
  }
  if (!std::all_of(comm_id_str.begin(), comm_id_str.end(), [](unsigned char c) { return std::isdigit(c); })) {
    HIXL_LOGE(PARAM_INVALID, "Invalid hccs comm_id: %s", comm_id_str.c_str());
    return PARAM_INVALID;
  }

  uint64_t parsed = 0;
  for (const unsigned char c : comm_id_str) {
    parsed = parsed * 10U + static_cast<uint64_t>(c - '0');
    if (parsed > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())) {
      HIXL_LOGE(PARAM_INVALID, "hccs comm_id out of range: %s", comm_id_str.c_str());
      return PARAM_INVALID;
    }
  }
  device_id = static_cast<uint32_t>(parsed);
  return SUCCESS;
}

Status ParseEidAddress(const std::string &eid_str, CommAddr &addr) {
  if (eid_str.length() != kEidHexStrLen) {
    HIXL_LOGE(PARAM_INVALID, "Invalid EID format: %s. Expected %zu hexadecimal characters without colons.",
              eid_str.c_str(), kEidHexStrLen);
    return PARAM_INVALID;
  }

  if (!std::all_of(eid_str.begin(), eid_str.end(), [](unsigned char c) { return std::isxdigit(c); })) {
    HIXL_LOGE(PARAM_INVALID, "Invalid EID: %s. Only hexadecimal characters are allowed.", eid_str.c_str());
    return PARAM_INVALID;
  }

  (void)memset_s(addr.eid, COMM_ADDR_EID_LEN, 0, COMM_ADDR_EID_LEN);
  for (size_t i = 0; i < COMM_ADDR_EID_LEN; ++i) {
    const std::string segment = eid_str.substr(i * 2, 2);
    try {
      const unsigned long value = std::stoul(segment, nullptr, 16);
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

Status FillDeviceLocInfo(const EndpointConfig &endpoint_config, EndpointDesc &endpoint, uint32_t dev_phy_id) {
  endpoint.loc.device.devPhyId = (endpoint_config.device_info.phy_device_id >= 0)
                                     ? static_cast<uint32_t>(endpoint_config.device_info.phy_device_id)
                                     : dev_phy_id;
  // for hccs, superDevId is invalid only when it's 0xFFFFFFFF
  endpoint.loc.device.superDevId = (endpoint.protocol == COMM_PROTOCOL_HCCS) ? static_cast<uint32_t>(-1) : 0;
  endpoint.loc.device.superPodIdx = 0U;
  endpoint.loc.device.serverIdx = 0U;
  if (endpoint_config.device_info.super_device_id >= 0) {
    HIXL_CHK_BOOL_RET_STATUS(
        endpoint_config.device_info.super_device_id <= static_cast<int64_t>(std::numeric_limits<uint32_t>::max()),
        PARAM_INVALID, "super_device_id out of range: %" PRId64, endpoint_config.device_info.super_device_id);
    endpoint.loc.device.superDevId = static_cast<uint32_t>(endpoint_config.device_info.super_device_id);
  }
  if (endpoint_config.device_info.super_pod_id >= 0) {
    HIXL_CHK_BOOL_RET_STATUS(
        endpoint_config.device_info.super_pod_id <= static_cast<int64_t>(std::numeric_limits<uint32_t>::max()),
        PARAM_INVALID, "super_pod_id out of range: %" PRId64, endpoint_config.device_info.super_pod_id);
    endpoint.loc.device.superPodIdx = static_cast<uint32_t>(endpoint_config.device_info.super_pod_id);
  }
  return SUCCESS;
}

Status ParseEndpointPlacement(const EndpointConfig &endpoint_config, EndpointDesc &endpoint) {
  static const std::map<std::string, EndpointLocType> kPlacementMap = {{kPlacementHost, ENDPOINT_LOC_TYPE_HOST},
                                                                       {kPlacementDevice, ENDPOINT_LOC_TYPE_DEVICE}};

  const auto placement_it = kPlacementMap.find(endpoint_config.placement);
  if (placement_it == kPlacementMap.end()) {
    HIXL_LOGE(PARAM_INVALID, "Unsupported placement: %s", endpoint_config.placement.c_str());
    return PARAM_INVALID;
  }
  endpoint.loc.locType = placement_it->second;
  return SUCCESS;
}

Status ParseEndpointProtocol(const EndpointConfig &endpoint_config, EndpointDesc &endpoint) {
  static const std::map<std::string, CommProtocol> kProtocolMap = {{kProtocolRoce, COMM_PROTOCOL_ROCE},
                                                                   {kProtocolUbCtp, COMM_PROTOCOL_UBC_CTP},
                                                                   {kProtocolUbTp, COMM_PROTOCOL_UBC_TP},
                                                                   {kProtocolUboe, COMM_PROTOCOL_UBOE},
                                                                   {kProtocolHccs, COMM_PROTOCOL_HCCS}};

  const auto protocol_it = kProtocolMap.find(endpoint_config.protocol);
  if (protocol_it == kProtocolMap.end()) {
    HIXL_LOGE(PARAM_INVALID, "Unsupported protocol: %s", endpoint_config.protocol.c_str());
    return PARAM_INVALID;
  }
  endpoint.protocol = protocol_it->second;
  return SUCCESS;
}

std::string BuildProtocolDescKey(const std::string &protocol, const std::string &placement) {
  return protocol + ":" + placement;
}

bool IsSupportedProtocolDesc(const std::string &protocol, const std::string &placement) {
  static const std::set<std::string> kSupportedProtocols = {kProtocolRoce, kProtocolHccs, kProtocolUbCtp,
                                                            kProtocolUbTp, kProtocolUboe};
  static const std::set<std::string> kSupportedPlacements = {kPlacementHost, kPlacementDevice};
  return kSupportedProtocols.find(protocol) != kSupportedProtocols.end() &&
         kSupportedPlacements.find(placement) != kSupportedPlacements.end();
}

Status ParseProtocolDesc(const std::vector<std::string> &protocol_desc, std::set<std::string> &desc_set) {
  desc_set.clear();
  for (const auto &desc : protocol_desc) {
    const auto split_pos = desc.find(':');
    if (split_pos == std::string::npos || split_pos == 0U || split_pos + 1U >= desc.size() ||
        desc.find(':', split_pos + 1U) != std::string::npos) {
      HIXL_LOGE(PARAM_INVALID, "Invalid protocol_desc: %s, expected format protocol:placement", desc.c_str());
      return PARAM_INVALID;
    }
    const std::string protocol = desc.substr(0, split_pos);
    const std::string placement = desc.substr(split_pos + 1U);
    if (!IsSupportedProtocolDesc(protocol, placement)) {
      HIXL_LOGE(PARAM_INVALID, "Unsupported protocol_desc: %s", desc.c_str());
      return PARAM_INVALID;
    }
    desc_set.insert(BuildProtocolDescKey(protocol, placement));
  }
  return SUCCESS;
}

Status IsProtocolDescMatched(const HixlOptions &options, const std::string &protocol, const std::string &placement,
                             bool &matched) {
  std::vector<std::string> protocol_desc = options.GetProtocolDesc();
  if (protocol_desc.empty()) {
    matched = true;
    return SUCCESS;
  }

  std::set<std::string> desc_set;
  HIXL_CHK_STATUS_RET(ParseProtocolDesc(protocol_desc, desc_set), "ParseProtocolDesc failed");
  matched = desc_set.find(BuildProtocolDescKey(protocol, placement)) != desc_set.end();
  return SUCCESS;
}

Status IsA5UbAutoGenNeeded(const HixlOptions &options, bool &needed) {
  if (options.GetProtocolDesc().empty()) {
    needed = true;
    return SUCCESS;
  }
  bool device_ub_needed = false;
  bool host_ub_needed = false;
  HIXL_CHK_STATUS_RET(IsProtocolDescMatched(options, kProtocolUbCtp, kPlacementDevice, device_ub_needed),
                      "IsProtocolDescMatched failed");
  HIXL_CHK_STATUS_RET(IsProtocolDescMatched(options, kProtocolUbCtp, kPlacementHost, host_ub_needed),
                      "IsProtocolDescMatched failed");
  needed = device_ub_needed || host_ub_needed;
  return SUCCESS;
}

}  // namespace

Status EndpointGenerator::ParseEndpointListFromLocalCommRes(const HixlOptions &options,
                                                            std::string &local_comm_res,
                                                            std::vector<EndpointConfig> &endpoint_list) {
  endpoint_list.clear();

  auto lcr = options.LocalCommRes();
  const char *local_comm_res_cstr = lcr.has_value() ? lcr->c_str() : nullptr;
  if (local_comm_res_cstr == nullptr || local_comm_res_cstr[0] == '\0') {
    local_comm_res.clear();
    return SUCCESS;
  }

  local_comm_res = local_comm_res_cstr;
  try {
    nlohmann::json config = nlohmann::json::parse(local_comm_res);
    bool has_valid_endpoint_list = config.contains("net_instance_id") && config["net_instance_id"].is_string() &&
                                   config.contains("endpoint_list") && config["endpoint_list"].is_array() &&
                                   !config["endpoint_list"].empty();
    if (!has_valid_endpoint_list) {
      return SUCCESS;
    }
    HIXL_CHK_STATUS_RET(ParseLocalCommRes(config, endpoint_list), "ParseLocalCommRes failed");
    HIXL_CHK_STATUS_RET(FilterEndpointListByProtocolDesc(options, endpoint_list),
                        "FilterEndpointListByProtocolDesc failed");
  } catch (const nlohmann::json::exception &e) {
    HIXL_LOGE(PARAM_INVALID, "Parse local_comm_res failed, exception:%s", e.what());
    return PARAM_INVALID;
  }
  return SUCCESS;
}

Status EndpointGenerator::GenEndpointFromProtocolDesc(const HixlOptions &options,
                                                      std::vector<EndpointConfig> &endpoint_list) {
  endpoint_list.clear();

  std::vector<std::string> protocol_desc = options.GetProtocolDesc();
  if (protocol_desc.empty()) {
    return SUCCESS;
  }

  std::set<std::string> desc_set;
  HIXL_CHK_STATUS_RET(ParseProtocolDesc(protocol_desc, desc_set), "ParseProtocolDesc failed");

  if (desc_set.find(kUboeProtocolDesc) != desc_set.end()) {
    EndpointConfig uboe_endpoint{};
    HIXL_CHK_STATUS_RET(GenDefaultUboeEndpointConfig(uboe_endpoint), "GenDefaultUboeEndpointConfig failed");
    endpoint_list.emplace_back(uboe_endpoint);
  }
  return SUCCESS;
}

Status EndpointGenerator::FilterEndpointListByProtocolDesc(const HixlOptions &options,
                                                           std::vector<EndpointConfig> &endpoint_list) {
  std::vector<std::string> protocol_desc = options.GetProtocolDesc();
  if (protocol_desc.empty()) {
    return SUCCESS;
  }

  std::set<std::string> desc_set;
  HIXL_CHK_STATUS_RET(ParseProtocolDesc(protocol_desc, desc_set), "ParseProtocolDesc failed");
  std::vector<EndpointConfig> filtered;
  filtered.reserve(endpoint_list.size());
  for (auto &ep : endpoint_list) {
    if (desc_set.find(BuildProtocolDescKey(ep.protocol, ep.placement)) != desc_set.end()) {
      filtered.emplace_back(std::move(ep));
    }
  }
  endpoint_list = std::move(filtered);
  HIXL_CHK_BOOL_RET_STATUS(!endpoint_list.empty(), PARAM_INVALID,
                           "endpoint_list is empty after filtering by protocol_desc");
  return SUCCESS;
}

Status EndpointGenerator::BuildEndpointList(const HixlOptions &options,
                                             const std::string &local_engine, std::string &local_comm_res,
                                             std::vector<EndpointConfig> &endpoint_list) {
  endpoint_list.clear();

  // Step 1: Parse endpoint_list from localCommRes
  HIXL_CHK_STATUS_RET(ParseEndpointListFromLocalCommRes(options, local_comm_res, endpoint_list),
                      "ParseEndpointListFromLocalCommRes failed");
  if (!endpoint_list.empty()) {
    HIXL_CHK_STATUS_RET(FillDeviceInfoIfNeeded(endpoint_list), "FillDeviceInfoIfNeeded failed");
    return SUCCESS;
  }

  // Step 2: Build default endpoint list based on soc type
  HIXL_CHK_STATUS_RET(AutoGenEndpointList(options, local_engine, endpoint_list), "AutoGenEndpointList failed");
  HIXL_CHK_BOOL_RET_STATUS(!endpoint_list.empty(), PARAM_INVALID,
                           "[HixlEngine] endpoint_list is empty after all generation attempts");
  HIXL_CHK_STATUS_RET(FillDeviceInfoIfNeeded(endpoint_list), "FillDeviceInfoIfNeeded failed");
  return SUCCESS;
}

Status EndpointGenerator::AutoGenEndpointList(const HixlOptions &options,
                                              const std::string &local_engine,
                                              std::vector<EndpointConfig> &endpoint_list) {
  SocType soc_type = SocType::kOther;
  HIXL_CHK_STATUS_RET(GetSocType(soc_type), "GetSocType failed");
  endpoint_list.clear();

  if (soc_type == SocType::kV5) {
    int32_t device_id = 0;
    HIXL_CHK_ACL_RET(aclrtGetDevice(&device_id));
    int32_t phy_id = 0;
    HIXL_CHK_ACL_RET(aclrtGetPhyDevIdByLogicDevId(device_id, &phy_id));
    bool ub_auto_gen_needed = false;
    HIXL_CHK_STATUS_RET(IsA5UbAutoGenNeeded(options, ub_auto_gen_needed), "IsA5UbAutoGenNeeded failed");
    if (ub_auto_gen_needed) {
      HIXL_LOGI("[AutoGenEndpointList] A5 UB auto-generate: logic_id=%d, phy_id=%d", device_id, phy_id);
      hixl::LocalCommRes local_comm_res;
      HIXL_CHK_STATUS_RET(hixl::GenerateLocalCommRes(phy_id, local_comm_res),
                          "[AutoGenEndpointList] GenerateLocalCommRes failed");
      endpoint_list = std::move(local_comm_res.endpoint_list);
    }
    std::vector<EndpointConfig> protocol_desc_endpoints;
    HIXL_CHK_STATUS_RET(GenEndpointFromProtocolDesc(options, protocol_desc_endpoints),
                        "GenEndpointFromProtocolDesc failed");
    endpoint_list.insert(endpoint_list.end(), protocol_desc_endpoints.begin(), protocol_desc_endpoints.end());
    HIXL_LOGI("[AutoGenEndpointList] kA5 generated %zu endpoints", endpoint_list.size());
  } else if (soc_type == SocType::kV2 || soc_type == SocType::kV3) {
    int32_t device_id = 0;
    HIXL_CHK_ACL_RET(aclrtGetDevice(&device_id));
    LocCommResInfo loc_comm_res_info{};
    HIXL_CHK_STATUS_RET(GenerateInfo(device_id, local_engine, loc_comm_res_info), "GenerateInfo failed");
    ConvertLocCommResInfoToEndpointList(loc_comm_res_info, endpoint_list);
  }

  if (endpoint_list.empty()) {
    return SUCCESS;
  }
  return FilterEndpointListByProtocolDesc(options, endpoint_list);
}

Status EndpointGenerator::ConvertToEndpointDesc(const EndpointConfig &endpoint_config, EndpointDesc &endpoint,
                                                uint32_t dev_phy_id) {
  HIXL_CHK_STATUS_RET(ParseEndpointPlacement(endpoint_config, endpoint), "ParseEndpointPlacement failed");
  HIXL_CHK_STATUS_RET(ParseEndpointProtocol(endpoint_config, endpoint), "ParseEndpointProtocol failed");
  if (endpoint_config.protocol == kProtocolRoce || endpoint_config.protocol == kProtocolUboe) {
    HIXL_CHK_STATUS_RET(ParseIpAddress(endpoint_config.comm_id, endpoint.commAddr), "ParseIpAddress failed");
    if (endpoint.loc.locType == ENDPOINT_LOC_TYPE_DEVICE) {
      HIXL_CHK_STATUS_RET(FillDeviceLocInfo(endpoint_config, endpoint, dev_phy_id), "FillDeviceLocInfo failed");
    }
    return SUCCESS;
  }

  if (endpoint_config.protocol == kProtocolHccs) {
    uint32_t device_id = 0;
    HIXL_CHK_STATUS_RET(ParseHccsCommId(endpoint_config.comm_id, device_id), "ParseHccsCommId failed");
    endpoint.commAddr.type = COMM_ADDR_TYPE_ID;
    endpoint.commAddr.id = device_id;
    if (endpoint.loc.locType == ENDPOINT_LOC_TYPE_DEVICE) {
      HIXL_CHK_STATUS_RET(FillDeviceLocInfo(endpoint_config, endpoint, dev_phy_id), "FillDeviceLocInfo failed");
    }
    return SUCCESS;
  }

  if (endpoint_config.protocol == kProtocolUbCtp || endpoint_config.protocol == kProtocolUbTp) {
    HIXL_CHK_STATUS_RET(ParseEidAddress(endpoint_config.comm_id, endpoint.commAddr), "ParseEidAddress failed");
    if (endpoint.loc.locType == ENDPOINT_LOC_TYPE_DEVICE) {
      endpoint.loc.device.devPhyId = dev_phy_id;
    }
  }
  return SUCCESS;
}

Status EndpointGenerator::SerializeEndpointConfigList(const std::vector<EndpointConfig> &list, std::string &msg_str) {
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

      nlohmann::json device_info;
      device_info["phy_device_id"] = ep.device_info.phy_device_id;
      device_info["super_device_id"] = ep.device_info.super_device_id;
      device_info["super_pod_id"] = ep.device_info.super_pod_id;
      item["device_info"] = device_info;

      j.push_back(item);
    }
    msg_str = j.dump();
  } catch (const nlohmann::json::exception &e) {
    HIXL_LOGE(PARAM_INVALID, "Failed to dump endpoint list, exception:%s", e.what());
    return PARAM_INVALID;
  }
  return SUCCESS;
}

Status EndpointGenerator::DeserializeEndpointConfigList(const std::string &json_str,
                                                        std::vector<EndpointConfig> &endpoint_list) {
  nlohmann::json j;
  try {
    j = nlohmann::json::parse(json_str);
  } catch (const nlohmann::json::exception &e) {
    HIXL_LOGE(PARAM_INVALID, "Failed to parse json, exception:%s", e.what());
    return PARAM_INVALID;
  }
  if (!j.is_array()) {
    HIXL_LOGE(PARAM_INVALID, "Invalid json format, expect array");
    return PARAM_INVALID;
  }

  endpoint_list.clear();
  for (const auto &item : j) {
    EndpointConfig endpoint{};
    HIXL_CHK_STATUS_RET(ParseRequiredJsonField(item, "protocol", endpoint.protocol), "Failed to parse protocol");
    HIXL_CHK_STATUS_RET(ParseRequiredJsonField(item, "comm_id", endpoint.comm_id), "Failed to parse comm_id");
    HIXL_CHK_STATUS_RET(ParseRequiredJsonField(item, "net_instance_id", endpoint.net_instance_id),
                        "Failed to parse net_instance_id");
    HIXL_CHK_STATUS_RET(ParseRequiredJsonField(item, "placement", endpoint.placement), "Failed to parse placement");
    HIXL_CHK_STATUS_RET(ParseRequiredJsonField(item, "plane", endpoint.plane), "Failed to parse plane");
    HIXL_CHK_STATUS_RET(ParseRequiredJsonField(item, "dst_eid", endpoint.dst_eid), "Failed to parse dst_eid");
    ParseDeviceInfo(item, endpoint);
    endpoint_list.emplace_back(std::move(endpoint));
  }
  return SUCCESS;
}

Status EndpointGenerator::GenerateInfo(int32_t device_id, const std::string &local_engine,
                                       EndpointGenerator::LocCommResInfo &loc_comm_res_info) {
  int32_t phy_device_id = 0;
  HIXL_CHK_ACL_RET(aclrtGetPhyDevIdByLogicDevId(device_id, &phy_device_id),
                   "device_id:%d, failed to get physical device id", device_id);

  loc_comm_res_info = {};
  loc_comm_res_info.version = kConfigVersion;

  HIXL_CHK_STATUS_RET(BuildNetInstanceId(device_id, local_engine, loc_comm_res_info.net_instance_id),
                      "BuildNetInstanceId failed, device_id:%d, local_engine:%s", device_id, local_engine.c_str());
  HIXL_CHK_STATUS_RET(BuildEndpointList(phy_device_id, loc_comm_res_info.endpoint_list),
                      "BuildEndpointList failed, phy_device_id:%d", phy_device_id);

  return SUCCESS;
}

void EndpointGenerator::ConvertLocCommResInfoToEndpointList(const EndpointGenerator::LocCommResInfo &loc_comm_res_info,
                                                            std::vector<EndpointConfig> &endpoint_list) {
  endpoint_list.clear();
  for (const auto &ep_info : loc_comm_res_info.endpoint_list) {
    EndpointConfig ep;
    ep.protocol = ep_info.protocol;
    ep.comm_id = ep_info.comm_id;
    ep.placement = ep_info.placement;
    ep.net_instance_id = loc_comm_res_info.net_instance_id;
    endpoint_list.emplace_back(ep);
  }
}

Status EndpointGenerator::GetSocName(std::string &soc_name) {
  const char *soc_name_cstr = aclrtGetSocName();
  HIXL_CHK_BOOL_RET_STATUS(soc_name_cstr != nullptr, FAILED, "aclrtGetSocName returned nullptr");
  soc_name = soc_name_cstr;
  HIXL_CHK_BOOL_RET_STATUS(!soc_name.empty(), FAILED, "soc_name is empty");
  return SUCCESS;
}

EndpointGenerator::SocType EndpointGenerator::GetSocTypeByName(const std::string &soc_name) {
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

Status EndpointGenerator::GetSocType(EndpointGenerator::SocType &soc_type) {
  std::string soc_name;
  HIXL_CHK_STATUS_RET(GetSocName(soc_name), "GetSocName failed");
  soc_type = GetSocTypeByName(soc_name);
  return SUCCESS;
}

Status EndpointGenerator::BuildNetInstanceId(int32_t device_id, const std::string &local_engine,
                                             std::string &net_instance_id) {
  SocType soc_type = SocType::kOther;
  HIXL_CHK_STATUS_RET(GetSocType(soc_type), "GetSocType failed");

  if (soc_type == SocType::kV3) {
    int64_t super_pod_id = 0;
    HIXL_CHK_ACL_RET(aclrtGetDeviceInfo(static_cast<uint32_t>(device_id), ACL_DEV_ATTR_SUPER_POD_ID, &super_pod_id),
                     "device_id:%d, failed to get super pod id", device_id);
    net_instance_id = std::to_string(super_pod_id);
    return SUCCESS;
  }

  HIXL_CHK_BOOL_RET_STATUS(soc_type == SocType::kV2, PARAM_INVALID,
                           "Unsupported soc_type:%d for auto-generated net_instance_id",
                           static_cast<int32_t>(soc_type));

  return GetHostIpFromLocalEngine(local_engine, net_instance_id);
}

Status EndpointGenerator::ParseLocalCommRes(const nlohmann::json &config, std::vector<EndpointConfig> &endpoint_list) {
  HIXL_CHK_BOOL_RET_STATUS(config.contains("net_instance_id") && config["net_instance_id"].is_string(), PARAM_INVALID,
                           "local_comm_res missing net_instance_id");
  HIXL_CHK_BOOL_RET_STATUS(config.contains("endpoint_list") && config["endpoint_list"].is_array(), PARAM_INVALID,
                           "local_comm_res missing endpoint_list");

  const std::string net_instance_id = config["net_instance_id"].get<std::string>();
  endpoint_list.clear();
  for (const auto &item : config["endpoint_list"]) {
    EndpointConfig endpoint{};
    HIXL_CHK_STATUS_RET(ParseRequiredJsonField(item, "protocol", endpoint.protocol), "Failed to parse protocol");
    HIXL_CHK_STATUS_RET(ParseRequiredJsonField(item, "comm_id", endpoint.comm_id), "Failed to parse comm_id");
    HIXL_CHK_STATUS_RET(ParseRequiredJsonField(item, "placement", endpoint.placement), "Failed to parse placement");
    HIXL_CHK_STATUS_RET(ParseOptionalJsonField(item, "plane", endpoint.plane), "Failed to parse plane");
    HIXL_CHK_STATUS_RET(ParseOptionalJsonField(item, "dst_eid", endpoint.dst_eid), "Failed to parse dst_eid");
    endpoint.net_instance_id = net_instance_id;
    ParseDeviceInfo(item, endpoint);
    endpoint_list.emplace_back(std::move(endpoint));
  }
  HIXL_CHK_BOOL_RET_STATUS(!endpoint_list.empty(), PARAM_INVALID,
                           "[HixlEngine] endpoint_list is empty, please check local_comm_res");
  return SUCCESS;
}

Status EndpointGenerator::FillDeviceInfoIfNeeded(std::vector<EndpointConfig> &endpoint_list) {
  if (endpoint_list.empty()) {
    return SUCCESS;
  }

  SocType soc_type = SocType::kOther;
  HIXL_CHK_STATUS_RET(GetSocType(soc_type), "GetSocType failed");
  if (soc_type == SocType::kOther || soc_type == SocType::kV5) {
    return SUCCESS;
  }

  int32_t device_id = 0;
  HIXL_CHK_ACL_RET(aclrtGetDevice(&device_id));
  int32_t phy_device_id = -1;
  aclError acl_ret = aclrtGetPhyDevIdByLogicDevId(device_id, &phy_device_id);
  if (acl_ret != ACL_SUCCESS) {
    HIXL_LOGE(FAILED, "aclrtGetPhyDevIdByLogicDevId failed, device_id=%d, acl_ret=%d", device_id, acl_ret);
    return FAILED;
  }

  int64_t super_device_id = -1;
  int64_t super_pod_id = -1;
  if (soc_type == SocType::kV3) {
    acl_ret = aclrtGetDeviceInfo(static_cast<uint32_t>(device_id), ACL_DEV_ATTR_SUPER_POD_ID, &super_pod_id);
    if (acl_ret != ACL_SUCCESS) {
      HIXL_LOGE(FAILED, "aclrtGetDeviceInfo SUPER_POD_ID failed, device_id=%d, acl_ret=%d", device_id, acl_ret);
      return FAILED;
    }
    acl_ret = aclrtGetDeviceInfo(static_cast<uint32_t>(device_id), ACL_DEV_ATTR_SUPER_POD_DEVIDE_ID, &super_device_id);
    if (acl_ret != ACL_SUCCESS) {
      HIXL_LOGE(FAILED, "aclrtGetDeviceInfo SUPER_DEVICE_ID failed, device_id=%d, acl_ret=%d", device_id, acl_ret);
      return FAILED;
    }
  }

  for (auto &ep : endpoint_list) {
    if (ep.placement != kPlacementDevice) {
      continue;
    }
    ep.device_info.phy_device_id = phy_device_id;
    ep.device_info.super_device_id = super_device_id;
    ep.device_info.super_pod_id = super_pod_id;
  }
  return SUCCESS;
}

Status EndpointGenerator::BuildEndpointList(int32_t phy_device_id,
                                            std::vector<EndpointGenerator::EndpointInfo> &endpoint_list) {
  endpoint_list.clear();

  EndpointInfo roce_endpoint{};
  HIXL_CHK_STATUS_RET(BuildRoceEndpoint(phy_device_id, roce_endpoint), "BuildRoceEndpoint failed, phy_device_id:%d",
                      phy_device_id);
  endpoint_list.emplace_back(roce_endpoint);

  if (IsIntraRoceEnabled()) {
    HIXL_LOGI("HCCL_INTRA_ROCE_ENABLE=1, only generate ROCE endpoint");
    return SUCCESS;
  }

  EndpointInfo hccs_endpoint{};
  HIXL_CHK_STATUS_RET(BuildHccsEndpoint(phy_device_id, hccs_endpoint), "BuildHccsEndpoint failed, phy_device_id:%d",
                      phy_device_id);
  endpoint_list.emplace_back(hccs_endpoint);

  return SUCCESS;
}

Status EndpointGenerator::BuildRoceEndpoint(int32_t phy_device_id, EndpointGenerator::EndpointInfo &endpoint) {
  std::string device_ip;
  HIXL_CHK_STATUS_RET(GetDeviceIp(phy_device_id, device_ip), "GetDeviceIp failed, phy_device_id:%d", phy_device_id);
  HIXL_CHK_BOOL_RET_STATUS(!device_ip.empty(), FAILED, "Failed to get device ip, phy_device_id:%d", phy_device_id);

  endpoint.protocol = kProtocolRoce;
  endpoint.comm_id = device_ip;
  endpoint.placement = kPlacementDevice;
  return SUCCESS;
}

Status EndpointGenerator::BuildHccsEndpoint(int32_t phy_device_id, EndpointGenerator::EndpointInfo &endpoint) {
  endpoint.protocol = kProtocolHccs;
  endpoint.comm_id = std::to_string(phy_device_id);
  endpoint.placement = kPlacementDevice;
  return SUCCESS;
}

Status EndpointGenerator::GetHostIpFromLocalEngine(const std::string &local_engine, std::string &host_ip) {
  int32_t host_port = 0;
  HIXL_CHK_STATUS_RET(ParseListenInfo(local_engine, host_ip, host_port), "Failed to parse host ip from local_engine:%s",
                      local_engine.c_str());
  HIXL_CHK_BOOL_RET_STATUS(!host_ip.empty(), FAILED, "Failed to get host ip from local_engine:%s",
                           local_engine.c_str());
  return SUCCESS;
}

Status EndpointGenerator::GetDeviceIp(int32_t phy_device_id, std::string &device_ip) {
  HIXL_CHK_STATUS_RET(hixl::GetDeviceIp(phy_device_id, device_ip), "GetDeviceIp failed, phy_device_id:%d",
                      phy_device_id);
  HIXL_CHK_BOOL_RET_STATUS(!device_ip.empty(), FAILED,
                           "Failed to get device ip from hccn.conf and hccn_tool, phy_device_id:%d", phy_device_id);
  return SUCCESS;
}
}  // namespace hixl
