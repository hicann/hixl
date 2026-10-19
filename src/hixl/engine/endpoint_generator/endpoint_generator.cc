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
#include <cinttypes>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>

#include "nlohmann/json.hpp"
#include "securec.h"
#include "acl/acl.h"
#include "acl/acl_rt.h"
#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/hixl_utils.h"
#include "common/json_utils.h"
#include "common/soc_type.h"
#include "endpoint_generator_base.h"

namespace hixl {
namespace {

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
  if (device_info.contains("server_id") && device_info["server_id"].is_number_integer()) {
    endpoint.device_info.server_id = device_info["server_id"].get<int64_t>();
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
  constexpr uint32_t kAsciiDigitZero = 48U;  // ASCII code of character '0'
  HIXL_CHK_BOOL_RET_STATUS(!comm_id_str.empty() && comm_id_str.length() <= kMaxHccsCommIdLen, PARAM_INVALID,
                           "Invalid hccs comm_id length:%zu, max:%zu", comm_id_str.length(), kMaxHccsCommIdLen);
  HIXL_CHK_BOOL_RET_STATUS(
      std::all_of(comm_id_str.begin(), comm_id_str.end(), [](unsigned char c) { return std::isdigit(c); }),
      PARAM_INVALID, "Invalid hccs comm_id:%s", comm_id_str.c_str());

  uint64_t parsed = 0;
  for (const unsigned char c : comm_id_str) {
    parsed = parsed * 10U + static_cast<uint64_t>(c - kAsciiDigitZero);
    HIXL_CHK_BOOL_RET_STATUS(parsed <= static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()), PARAM_INVALID,
                             "hccs comm_id out of range:%s, max:%u", comm_id_str.c_str(),
                             static_cast<unsigned int>(std::numeric_limits<uint32_t>::max()));
  }
  device_id = static_cast<uint32_t>(parsed);
  return SUCCESS;
}

Status ParseEidAddress(const std::string &eid_str, CommAddr &addr) {
  constexpr size_t kEidHexStrLen = COMM_ADDR_EID_LEN * 2U;
  HIXL_CHK_BOOL_RET_STATUS(eid_str.length() == kEidHexStrLen, PARAM_INVALID,
                           "Invalid EID format:%s, expected %zu hexadecimal characters without colons", eid_str.c_str(),
                           kEidHexStrLen);
  HIXL_CHK_BOOL_RET_STATUS(
      std::all_of(eid_str.begin(), eid_str.end(), [](unsigned char c) { return std::isxdigit(c); }), PARAM_INVALID,
      "Invalid EID:%s, only hexadecimal characters are allowed", eid_str.c_str());

  (void)memset_s(addr.eid, COMM_ADDR_EID_LEN, 0, COMM_ADDR_EID_LEN);
  for (size_t i = 0; i < COMM_ADDR_EID_LEN; ++i) {
    const std::string segment = eid_str.substr(i * 2, 2);
    try {
      const unsigned long value = std::stoul(segment, nullptr, 16);
      HIXL_CHK_BOOL_RET_STATUS(value <= UINT8_MAX, PARAM_INVALID,
                               "Invalid segment:%zu in EID:%s, maximum value is 0xFF", i, segment.c_str());
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

Status FillEndpointDeviceLocation(const EndpointConfig &endpoint_config, EndpointDesc &endpoint) {
  HIXL_CHK_BOOL_RET_STATUS(endpoint_config.device_info.phy_device_id >= 0, PARAM_INVALID,
                           "device endpoint requires phy_device_id");
  endpoint.loc.device.devPhyId = static_cast<uint32_t>(endpoint_config.device_info.phy_device_id);
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
  if (endpoint_config.device_info.server_id >= 0) {
    HIXL_CHK_BOOL_RET_STATUS(
        endpoint_config.device_info.server_id <= static_cast<int64_t>(std::numeric_limits<uint32_t>::max()),
        PARAM_INVALID, "server_id out of range: %" PRId64, endpoint_config.device_info.server_id);
    endpoint.loc.device.serverIdx = static_cast<uint32_t>(endpoint_config.device_info.server_id);
  }
  return SUCCESS;
}

Status ParseEndpointPlacement(const EndpointConfig &endpoint_config, EndpointDesc &endpoint) {
  static const std::map<std::string, EndpointLocType> kPlacementMap = {{kPlacementHost, ENDPOINT_LOC_TYPE_HOST},
                                                                       {kPlacementDevice, ENDPOINT_LOC_TYPE_DEVICE}};

  const auto placement_it = kPlacementMap.find(endpoint_config.placement);
  HIXL_CHK_BOOL_RET_STATUS(placement_it != kPlacementMap.end(), PARAM_INVALID, "Unsupported placement:%s",
                           endpoint_config.placement.c_str());
  endpoint.loc.locType = placement_it->second;
  return SUCCESS;
}

Status ParseEndpointProtocol(const EndpointConfig &endpoint_config, EndpointDesc &endpoint) {
  static const std::map<std::string, CommProtocol> kProtocolMap = {
      {kProtocolRoce, COMM_PROTOCOL_ROCE}, {kProtocolUbCtp, COMM_PROTOCOL_UBC_CTP},
      {kProtocolUboe, COMM_PROTOCOL_UBOE}, {kProtocolUbRtp, COMM_PROTOCOL_UBG},
      {kProtocolHccs, COMM_PROTOCOL_HCCS}, {kProtocolUbmem, COMM_PROTOCOL_UB_MEM}};

  const auto protocol_it = kProtocolMap.find(endpoint_config.protocol);
  HIXL_CHK_BOOL_RET_STATUS(protocol_it != kProtocolMap.end(), PARAM_INVALID, "Unsupported protocol:%s",
                           endpoint_config.protocol.c_str());
  endpoint.protocol = protocol_it->second;
  return SUCCESS;
}

void LogEndpointList(const char *source, const std::vector<EndpointConfig> &endpoint_list) {
  HIXL_LOGI("[EndpointGenerator] %s, count:%zu", source, endpoint_list.size());
  for (size_t i = 0; i < endpoint_list.size(); ++i) {
    HIXL_LOGI("[EndpointGenerator] endpoint[%zu]: %s", i, endpoint_list[i].ToString().c_str());
  }
}

// Decide which device endpoints to auto-generate: empty protocol_desc keeps the default (roce + hccs).
Status ResolveDeviceEndpointNeedByProtocolDesc(const std::vector<std::string> &protocol_desc, bool &roce_needed,
                                               bool &hccs_needed) {
  roce_needed = true;
  hccs_needed = true;
  if (protocol_desc.empty()) {
    return SUCCESS;
  }
  std::set<std::string> desc_set;
  HIXL_CHK_STATUS_RET(EndpointGeneratorBase::ParseProtocolDesc(protocol_desc, desc_set), "ParseProtocolDesc failed");
  roce_needed = desc_set.count(EndpointGeneratorBase::BuildProtocolDescKey(kProtocolRoce, kPlacementDevice)) > 0;
  hccs_needed = desc_set.count(EndpointGeneratorBase::BuildProtocolDescKey(kProtocolHccs, kPlacementDevice)) > 0;
  return SUCCESS;
}

Status AppendUbMemEndpoint(std::vector<EndpointConfig> &endpoint_list, const std::string &placement,
                           int32_t phy_device_id) {
  for (const auto &ep : endpoint_list) {
    if (ep.protocol == kProtocolUbmem) {
      return SUCCESS;
    }
  }
  EndpointConfig ub{};
  ub.protocol = kProtocolUbmem;
  ub.placement = placement;
  if (!endpoint_list.empty()) {
    const EndpointConfig *src = &endpoint_list[0];
    for (const auto &ep : endpoint_list) {
      if (ep.protocol == kProtocolHccs) {
        src = &ep;
        break;
      }
    }
    ub.comm_id = src->comm_id;
    ub.net_instance_id = src->net_instance_id;
    ub.server_id = src->server_id;
    ub.device_info = src->device_info;
  } else {
    HIXL_CHK_BOOL_RET_STATUS(phy_device_id >= 0, PARAM_INVALID,
                             "[EndpointGenerator] cannot append ubmem endpoint without a valid device id");
  }
  endpoint_list.emplace_back(std::move(ub));
  HIXL_LOGI("[EndpointGenerator] appended ubmem endpoint, placement=%s, comm_id=%s", placement.c_str(),
            endpoint_list.back().comm_id.c_str());
  return SUCCESS;
}

Status AppendUbMemIfRequested(const std::vector<std::string> &protocol_desc, const char *placement,
                              std::vector<EndpointConfig> &endpoint_list, int32_t phy_device_id) {
  if (std::find(protocol_desc.begin(), protocol_desc.end(), kProtocolUbmem) == protocol_desc.end()) {
    return SUCCESS;
  }
  return AppendUbMemEndpoint(endpoint_list, placement, phy_device_id);
}

}  // namespace

Status EndpointGenerator::ParseEndpointListFromLocalCommRes(const HixlOptions &options, std::string &local_comm_res,
                                                            std::vector<EndpointConfig> &endpoint_list) {
  endpoint_list.clear();

  auto lcr = options.LocalCommRes();
  if (!lcr.has_value() || lcr->empty()) {
    local_comm_res.clear();
    return SUCCESS;
  }

  local_comm_res = lcr.value();
  try {
    nlohmann::json config = nlohmann::json::parse(local_comm_res);
    std::string server_id;
    HIXL_CHK_STATUS_RET(ParseJsonField(config, "server_id", server_id, false), "Failed to parse server_id");
    bool has_valid_endpoint_list = config.contains("net_instance_id") && config["net_instance_id"].is_string() &&
                                   config.contains("endpoint_list") && config["endpoint_list"].is_array() &&
                                   !config["endpoint_list"].empty();
    if (!has_valid_endpoint_list) {
      return SUCCESS;
    }
    HIXL_CHK_STATUS_RET(ParseLocalCommRes(config, server_id, endpoint_list), "ParseLocalCommRes failed");
    HIXL_CHK_STATUS_RET(FilterEndpointListByProtocolDesc(options, endpoint_list),
                        "FilterEndpointListByProtocolDesc failed");
  } catch (const nlohmann::json::exception &e) {
    HIXL_LOGE(PARAM_INVALID, "Parse local_comm_res failed, exception:%s", e.what());
    return PARAM_INVALID;
  }
  return SUCCESS;
}

Status EndpointGenerator::FilterEndpointListByProtocolDesc(const HixlOptions &options,
                                                           std::vector<EndpointConfig> &endpoint_list) {
  std::vector<std::string> protocol_desc = options.GetProtocolDesc();
  if (protocol_desc.empty()) {
    return SUCCESS;
  }

  LogEndpointList("parsed or generated endpoint list", endpoint_list);
  HIXL_CHK_STATUS_RET(AppendUbMemIfRequested(protocol_desc, kPlacementDevice, endpoint_list, -1),
                      "AppendUbMemIfRequested failed");
  HIXL_CHK_STATUS_RET(EndpointGeneratorBase::FilterEndpointsByProtocolDescList(protocol_desc, endpoint_list),
                      "FilterEndpointsByProtocolDescList failed");
  LogEndpointList("endpoint list after protocol_desc filter", endpoint_list);
  return SUCCESS;
}

Status EndpointGenerator::BuildEndpointList(const HixlOptions &options, const std::string &local_engine,
                                            std::string &local_comm_res, std::vector<EndpointConfig> &endpoint_list) {
  endpoint_list.clear();

  // Step 1: Parse endpoint_list from localCommRes
  HIXL_CHK_STATUS_RET(ParseEndpointListFromLocalCommRes(options, local_comm_res, endpoint_list),
                      "ParseEndpointListFromLocalCommRes failed");
  if (!endpoint_list.empty()) {
    if (IsIntraRoceEnabled()) {
      HIXL_LOGI("HCCL_INTRA_ROCE_ENABLE=1, filter to RoCE only");
      endpoint_list.erase(std::remove_if(endpoint_list.begin(), endpoint_list.end(),
                                         [](const EndpointConfig &ep) { return ep.protocol != kProtocolRoce; }),
                          endpoint_list.end());
    }
    HIXL_CHK_STATUS_RET(PopulateLocalDeviceInfo(endpoint_list), "PopulateLocalDeviceInfo failed");
    return SUCCESS;
  }

  uint32_t device_count = 0;
  HIXL_CHK_ACL_RET(aclrtGetDeviceCount(&device_count), "aclrtGetDeviceCount failed");
  HIXL_CHK_BOOL_RET_STATUS(device_count > 0U, PARAM_INVALID,
                           "LocalCommRes with endpoint_list is required when no local NPU device exists; "
                           "auto generation is not supported on generic server");

  // Step 2: Build default endpoint list based on soc type
  HIXL_CHK_STATUS_RET(AutoGenEndpointList(options, local_engine, endpoint_list), "AutoGenEndpointList failed");
  HIXL_CHK_BOOL_RET_STATUS(!endpoint_list.empty(), PARAM_INVALID,
                           "[HixlEngine] endpoint_list is empty after all generation attempts");
  HIXL_CHK_STATUS_RET(PopulateLocalDeviceInfo(endpoint_list), "PopulateLocalDeviceInfo failed");
  return SUCCESS;
}

Status EndpointGenerator::AutoGenEndpointList(const HixlOptions &options, const std::string &local_engine,
                                              std::vector<EndpointConfig> &endpoint_list) {
  SocType soc_type = SocType::kDefault;
  HIXL_CHK_STATUS_RET(SocTypeUtil::GetSocType(soc_type), "[AutoGenEndpointList] GetSocType failed");
  auto generator = EndpointGeneratorBase::Create(soc_type);
  HIXL_CHK_BOOL_RET_STATUS(generator != nullptr, FAILED, "[AutoGenEndpointList] failed to create generator");
  HIXL_CHK_STATUS_RET(generator->AutoGen(options, local_engine, endpoint_list), "[AutoGenEndpointList] AutoGen failed");
  HIXL_EVENT("[AutoGenEndpointList] generated %zu endpoints", endpoint_list.size());
  return SUCCESS;
}

Status EndpointGenerator::ConvertToEndpointDesc(const EndpointConfig &endpoint_config, EndpointDesc &endpoint) {
  HIXL_CHK_STATUS_RET(ParseEndpointPlacement(endpoint_config, endpoint), "ParseEndpointPlacement failed");
  HIXL_CHK_STATUS_RET(ParseEndpointProtocol(endpoint_config, endpoint), "ParseEndpointProtocol failed");
  if (endpoint.loc.locType == ENDPOINT_LOC_TYPE_DEVICE) {
    HIXL_CHK_STATUS_RET(FillEndpointDeviceLocation(endpoint_config, endpoint), "FillEndpointDeviceLocation failed");
  }

  if (endpoint_config.protocol == kProtocolRoce || endpoint_config.protocol == kProtocolUboe) {
    HIXL_CHK_STATUS_RET(ParseIpAddress(endpoint_config.comm_id, endpoint.commAddr), "ParseIpAddress failed");
    return SUCCESS;
  }

  if (endpoint_config.protocol == kProtocolHccs) {
    uint32_t device_id = 0;
    HIXL_CHK_STATUS_RET(ParseHccsCommId(endpoint_config.comm_id, device_id), "ParseHccsCommId failed");
    endpoint.commAddr.type = COMM_ADDR_TYPE_ID;
    endpoint.commAddr.id = device_id;
    return SUCCESS;
  }

  if (endpoint_config.protocol == kProtocolUbmem) {
    uint32_t device_id = 0U;
    const std::string &comm_id = endpoint_config.comm_id;
    const bool numeric =
        !comm_id.empty() && comm_id.size() <= 10U &&
        std::all_of(comm_id.begin(), comm_id.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
    if (numeric) {
      HIXL_CHK_STATUS_RET(ParseHccsCommId(comm_id, device_id), "Parse ubmem comm_id failed");
    } else if (endpoint_config.device_info.phy_device_id >= 0) {
      device_id = static_cast<uint32_t>(endpoint_config.device_info.phy_device_id);
    }
    endpoint.commAddr.type = COMM_ADDR_TYPE_ID;
    endpoint.commAddr.id = device_id;
    return SUCCESS;
  }

  if (endpoint_config.protocol == kProtocolUbCtp || endpoint_config.protocol == kProtocolUbRtp) {
    HIXL_CHK_STATUS_RET(ParseEidAddress(endpoint_config.comm_id, endpoint.commAddr), "ParseEidAddress failed");
  }
  return SUCCESS;
}

namespace {
constexpr size_t kMaxDstEidListSize = 32U;
constexpr size_t kMaxDstEidExpansionCount = 32U;
constexpr int kMaxConfigLogPreviewLength = 128;

Status ParseDstEidList(const std::string &dst_eid, std::vector<std::string> &dst_eids) {
  if (dst_eid.empty()) {
    dst_eids = {""};
    return SUCCESS;
  }

  const size_t separator_count = static_cast<size_t>(std::count(dst_eid.cbegin(), dst_eid.cend(), ';'));
  HIXL_CHK_BOOL_RET_STATUS(
      separator_count < kMaxDstEidListSize, PARAM_INVALID,
      "dst_eid list has too many members, separator_count:%zu, max_member_count:%zu, dst_eid_length:%zu, "
      "dst_eid:%.*s",
      separator_count, kMaxDstEidListSize, dst_eid.size(), kMaxConfigLogPreviewLength, dst_eid.c_str());

  std::vector<std::string> parsed;
  std::set<std::string> seen;
  for (auto item : Split(dst_eid, ';')) {
    const auto first = item.find_first_not_of(" \t\n\r\f\v");
    HIXL_CHK_BOOL_RET_STATUS(first != std::string::npos, PARAM_INVALID,
                             "dst_eid list contains an empty member, dst_eid_length:%zu, dst_eid:%.*s", dst_eid.size(),
                             kMaxConfigLogPreviewLength, dst_eid.c_str());
    const auto last = item.find_last_not_of(" \t\n\r\f\v");
    item = item.substr(first, last - first + 1U);
    if (seen.insert(item).second) {
      parsed.emplace_back(std::move(item));
    }
  }
  dst_eids = std::move(parsed);
  return SUCCESS;
}

Status NormalizeDstEidList(std::string &dst_eid) {
  std::vector<std::string> dst_eids;
  HIXL_CHK_STATUS_RET(ParseDstEidList(dst_eid, dst_eids), "Failed to parse dst_eid list");
  std::ostringstream normalized;
  for (size_t i = 0; i < dst_eids.size(); ++i) {
    if (i != 0U) {
      normalized << ';';
    }
    normalized << dst_eids[i];
  }
  dst_eid = normalized.str();
  return SUCCESS;
}

Status GetExchangeDstEids(const EndpointConfig &endpoint, std::vector<std::string> &dst_eids) {
  if (endpoint.protocol != kProtocolUbCtp) {
    dst_eids = {endpoint.dst_eid};
    return SUCCESS;
  }
  return ParseDstEidList(endpoint.dst_eid, dst_eids);
}

Status UpdateDstEidExpansionCount(size_t dst_eid_count, size_t &expansion_count) {
  const size_t additional_count = dst_eid_count > 0U ? dst_eid_count - 1U : 0U;
  HIXL_CHK_BOOL_RET_STATUS(
      expansion_count <= kMaxDstEidExpansionCount && additional_count <= kMaxDstEidExpansionCount - expansion_count,
      PARAM_INVALID,
      "dst_eid lists expand to too many additional endpoints, current_count:%zu, additional_count:%zu, "
      "max_count:%zu",
      expansion_count, additional_count, kMaxDstEidExpansionCount);
  expansion_count += additional_count;
  return SUCCESS;
}

void AppendEndpointJson(const EndpointConfig &endpoint, const std::string &dst_eid, nlohmann::json &list) {
  nlohmann::json item;
  item["protocol"] = endpoint.protocol;
  item["comm_id"] = endpoint.comm_id;
  item["placement"] = endpoint.placement;
  item["plane"] = endpoint.plane;
  item["dst_eid"] = dst_eid;
  item["net_instance_id"] = endpoint.net_instance_id;
  item["server_id"] = endpoint.server_id;
  item["device_info"] = {{"phy_device_id", endpoint.device_info.phy_device_id},
                         {"super_device_id", endpoint.device_info.super_device_id},
                         {"super_pod_id", endpoint.device_info.super_pod_id},
                         {"server_id", endpoint.device_info.server_id}};
  list.push_back(std::move(item));
}
}  // namespace

Status EndpointGenerator::SerializeEndpointConfigList(const std::vector<EndpointConfig> &list, std::string &msg_str) {
  nlohmann::json j = nlohmann::json::array();
  size_t expansion_count = 0U;
  try {
    for (const auto &ep : list) {
      std::vector<std::string> dst_eids;
      HIXL_CHK_STATUS_RET(
          GetExchangeDstEids(ep, dst_eids),
          "Failed to parse endpoint dst_eid list, comm_id_length:%zu, comm_id:%.*s, dst_eid_length:%zu, "
          "dst_eid:%.*s",
          ep.comm_id.size(), kMaxConfigLogPreviewLength, ep.comm_id.c_str(), ep.dst_eid.size(),
          kMaxConfigLogPreviewLength, ep.dst_eid.c_str());
      HIXL_CHK_STATUS_RET(
          UpdateDstEidExpansionCount(dst_eids.size(), expansion_count),
          "Failed to expand endpoint dst_eid list, comm_id_length:%zu, comm_id:%.*s, dst_eid_length:%zu, "
          "dst_eid:%.*s",
          ep.comm_id.size(), kMaxConfigLogPreviewLength, ep.comm_id.c_str(), ep.dst_eid.size(),
          kMaxConfigLogPreviewLength, ep.dst_eid.c_str());
      for (const auto &dst_eid : dst_eids) {
        AppendEndpointJson(ep, dst_eid, j);
      }
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
  size_t expansion_count = 0U;
  for (const auto &item : j) {
    EndpointConfig endpoint{};
    HIXL_CHK_STATUS_RET(ParseJsonField(item, "protocol", endpoint.protocol), "Failed to parse protocol");
    HIXL_CHK_STATUS_RET(ParseJsonField(item, "comm_id", endpoint.comm_id), "Failed to parse comm_id");
    HIXL_CHK_STATUS_RET(ParseJsonField(item, "net_instance_id", endpoint.net_instance_id),
                        "Failed to parse net_instance_id");
    HIXL_CHK_STATUS_RET(ParseJsonField(item, "placement", endpoint.placement), "Failed to parse placement");
    HIXL_CHK_STATUS_RET(ParseJsonField(item, "plane", endpoint.plane), "Failed to parse plane");
    HIXL_CHK_STATUS_RET(ParseJsonField(item, "dst_eid", endpoint.dst_eid), "Failed to parse dst_eid");
    HIXL_CHK_STATUS_RET(ParseJsonField(item, "server_id", endpoint.server_id, false), "Failed to parse server_id");
    ParseDeviceInfo(item, endpoint);
    std::vector<std::string> dst_eids;
    HIXL_CHK_STATUS_RET(GetExchangeDstEids(endpoint, dst_eids),
                        "Failed to parse endpoint dst_eid list, comm_id_length:%zu, comm_id:%.*s, dst_eid_length:%zu, "
                        "dst_eid:%.*s",
                        endpoint.comm_id.size(), kMaxConfigLogPreviewLength, endpoint.comm_id.c_str(),
                        endpoint.dst_eid.size(), kMaxConfigLogPreviewLength, endpoint.dst_eid.c_str());
    HIXL_CHK_STATUS_RET(UpdateDstEidExpansionCount(dst_eids.size(), expansion_count),
                        "Failed to expand endpoint dst_eid list, comm_id_length:%zu, comm_id:%.*s, dst_eid_length:%zu, "
                        "dst_eid:%.*s",
                        endpoint.comm_id.size(), kMaxConfigLogPreviewLength, endpoint.comm_id.c_str(),
                        endpoint.dst_eid.size(), kMaxConfigLogPreviewLength, endpoint.dst_eid.c_str());
    for (auto &dst_eid : dst_eids) {
      EndpointConfig edge = endpoint;
      edge.dst_eid = std::move(dst_eid);
      endpoint_list.emplace_back(std::move(edge));
    }
  }
  return SUCCESS;
}

Status EndpointGenerator::ParseLocalCommRes(const nlohmann::json &config, const std::string &server_id,
                                            std::vector<EndpointConfig> &endpoint_list) {
  HIXL_CHK_BOOL_RET_STATUS(config.contains("net_instance_id") && config["net_instance_id"].is_string(), PARAM_INVALID,
                           "local_comm_res missing net_instance_id");
  HIXL_CHK_BOOL_RET_STATUS(config.contains("endpoint_list") && config["endpoint_list"].is_array(), PARAM_INVALID,
                           "local_comm_res missing endpoint_list");

  const std::string net_instance_id = config["net_instance_id"].get<std::string>();
  endpoint_list.clear();
  bool has_ubg = false;
  bool has_uboe = false;
  for (const auto &item : config["endpoint_list"]) {
    EndpointConfig endpoint{};
    HIXL_CHK_STATUS_RET(ParseJsonField(item, "protocol", endpoint.protocol), "Failed to parse protocol");
    HIXL_CHK_STATUS_RET(ParseJsonField(item, "comm_id", endpoint.comm_id), "Failed to parse comm_id");
    HIXL_CHK_STATUS_RET(ParseJsonField(item, "placement", endpoint.placement), "Failed to parse placement");
    HIXL_CHK_BOOL_RET_STATUS(EndpointGeneratorBase::IsSupportedProtocolDesc(endpoint.protocol, endpoint.placement),
                             PARAM_INVALID, "Unsupported endpoint protocol or placement: %s:%s",
                             endpoint.protocol.c_str(), endpoint.placement.c_str());
    if (endpoint.protocol == kProtocolUbRtp) {
      has_ubg = true;
    } else if (endpoint.protocol == kProtocolUboe) {
      has_uboe = true;
    }
    HIXL_CHK_STATUS_RET(ParseJsonField(item, "plane", endpoint.plane, false), "Failed to parse plane");
    HIXL_CHK_STATUS_RET(ParseJsonField(item, "dst_eid", endpoint.dst_eid, false), "Failed to parse dst_eid");
    if (endpoint.protocol == kProtocolUbCtp) {
      HIXL_CHK_STATUS_RET(
          NormalizeDstEidList(endpoint.dst_eid),
          "Failed to normalize dst_eid, comm_id_length:%zu, comm_id:%.*s, dst_eid_length:%zu, dst_eid:%.*s",
          endpoint.comm_id.size(), kMaxConfigLogPreviewLength, endpoint.comm_id.c_str(), endpoint.dst_eid.size(),
          kMaxConfigLogPreviewLength, endpoint.dst_eid.c_str());
    }
    endpoint.net_instance_id = net_instance_id;
    endpoint.server_id = server_id;
    ParseDeviceInfo(item, endpoint);
    endpoint_list.emplace_back(std::move(endpoint));
  }
  HIXL_CHK_BOOL_RET_STATUS(!(has_ubg && has_uboe), PARAM_INVALID,
                           "endpoint_list cannot contain both ub_rtp and uboe protocols");
  HIXL_CHK_BOOL_RET_STATUS(!endpoint_list.empty(), PARAM_INVALID,
                           "[HixlEngine] endpoint_list is empty, please check local_comm_res");
  return SUCCESS;
}

bool EndpointGenerator::HasDeviceEndpoint(const std::vector<EndpointConfig> &endpoint_list) {
  return std::any_of(endpoint_list.cbegin(), endpoint_list.cend(),
                     [](const EndpointConfig &ep) { return ep.placement == kPlacementDevice; });
}

Status EndpointGenerator::PopulateLocalDeviceInfo(std::vector<EndpointConfig> &endpoint_list) {
  if (!HasDeviceEndpoint(endpoint_list)) {
    return SUCCESS;
  }

  uint32_t device_count = 0;
  HIXL_CHK_ACL_RET(aclrtGetDeviceCount(&device_count), "aclrtGetDeviceCount failed");
  HIXL_CHK_BOOL_RET_STATUS(device_count > 0U, PARAM_INVALID,
                           "device endpoint requires local NPU device, but device count is 0");

  SocType soc_type = SocType::kDefault;
  HIXL_CHK_STATUS_RET(SocTypeUtil::GetSocType(soc_type), "PopulateLocalDeviceInfo GetSocType failed");
  int32_t logic_device_id = 0;
  HIXL_CHK_ACL_RET(aclrtGetDevice(&logic_device_id));
  int32_t phy_device_id = -1;
  HIXL_CHK_ACL_RET(aclrtGetPhyDevIdByLogicDevId(logic_device_id, &phy_device_id));

  int64_t super_device_id = -1;
  int64_t super_pod_id = -1;
  int64_t server_id = -1;
  if (soc_type == SocType::kGen2) {
    HIXL_CHK_ACL_RET(
        aclrtGetDeviceInfo(static_cast<uint32_t>(logic_device_id), ACL_DEV_ATTR_SUPER_POD_ID, &super_pod_id));
    HIXL_CHK_ACL_RET(
        aclrtGetDeviceInfo(static_cast<uint32_t>(logic_device_id), ACL_DEV_ATTR_SUPER_POD_DEVIDE_ID, &super_device_id));
    int64_t acl_server_id = -1;
    auto acl_ret =
        aclrtGetDeviceInfo(static_cast<uint32_t>(logic_device_id), ACL_DEV_ATTR_SUPER_POD_SERVER_ID, &acl_server_id);
    if (acl_ret == ACL_SUCCESS && acl_server_id != kInvalidSuperPodServerId) {
      server_id = acl_server_id;
    } else {
      HIXL_EVENT(
          "[PopulateLocalDeviceInfo] no valid super pod server id, skip server_id, acl_ret=%d, "
          "value=%" PRId64 ", logic_device_id=%d",
          static_cast<int>(acl_ret), acl_server_id, logic_device_id);
    }
  }

  for (auto &ep : endpoint_list) {
    if (ep.placement != kPlacementDevice) {
      continue;
    }
    ep.device_info.phy_device_id = phy_device_id;
    ep.device_info.super_device_id = super_device_id;
    ep.device_info.super_pod_id = super_pod_id;
    if (server_id >= 0) {
      ep.device_info.server_id = server_id;
    }
  }
  return SUCCESS;
}

Status EndpointGenerator::BuildDefaultDeviceEndpointInfoList(
    int32_t phy_device_id, const std::vector<std::string> &protocol_desc,
    std::vector<EndpointGenerator::EndpointInfo> &endpoint_list) {
  endpoint_list.clear();

  bool roce_needed = false;
  bool hccs_needed = false;
  HIXL_CHK_STATUS_RET(ResolveDeviceEndpointNeedByProtocolDesc(protocol_desc, roce_needed, hccs_needed),
                      "ResolveDeviceEndpointNeedByProtocolDesc failed");

  // ubmem has the highest priority: generate it before RoCE and the intra-RoCE return.
  if (std::find(protocol_desc.begin(), protocol_desc.end(), kProtocolUbmem) != protocol_desc.end()) {
    EndpointInfo ubmem_endpoint{};
    ubmem_endpoint.protocol = kProtocolUbmem;
    ubmem_endpoint.comm_id = std::to_string(phy_device_id);
    ubmem_endpoint.placement = kPlacementDevice;
    endpoint_list.emplace_back(std::move(ubmem_endpoint));
  }

  if (roce_needed) {
    const bool roce_required = !protocol_desc.empty() || IsIntraRoceEnabled();
    EndpointInfo roce_endpoint{};
    HIXL_CHK_STATUS_RET(BuildRoceEndpoint(phy_device_id, roce_required, roce_endpoint),
                        "BuildRoceEndpoint failed, phy_device_id:%d", phy_device_id);
    if (!roce_endpoint.comm_id.empty()) {
      endpoint_list.emplace_back(std::move(roce_endpoint));
    }
  }

  if (IsIntraRoceEnabled()) {
    HIXL_LOGI("HCCL_INTRA_ROCE_ENABLE=1, skip HCCS endpoint generation");
    return SUCCESS;
  }

  if (hccs_needed) {
    EndpointInfo hccs_endpoint{};
    HIXL_CHK_STATUS_RET(BuildHccsEndpoint(phy_device_id, hccs_endpoint), "BuildHccsEndpoint failed, phy_device_id:%d",
                        phy_device_id);
    endpoint_list.emplace_back(std::move(hccs_endpoint));
  }

  return SUCCESS;
}

Status EndpointGenerator::BuildRoceEndpoint(int32_t phy_device_id, bool required,
                                            EndpointGenerator::EndpointInfo &endpoint) {
  std::string device_ip;
  HIXL_CHK_STATUS_RET(GetDeviceIp(phy_device_id, device_ip), "GetDeviceIp failed, phy_device_id:%d", phy_device_id);
  if (device_ip.empty()) {
    HIXL_CHK_BOOL_RET_STATUS(!required, FAILED, "Device roce endpoint is unavailable, phy_device_id:%d", phy_device_id);
    HIXL_EVENT("[EndpointGenerator] Device roce endpoint is unavailable, skip roce endpoint, phy_device_id:%d",
               phy_device_id);
    return SUCCESS;
  }

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
  // hixl::GetDeviceIp already reports "ip unavailable" as SUCCESS with an empty string;
  // any real error keeps interrupting the flow here.
  HIXL_CHK_STATUS_RET(hixl::GetDeviceIp(phy_device_id, device_ip), "GetDeviceIp failed, phy_device_id:%d",
                      phy_device_id);
  return SUCCESS;
}
}  // namespace hixl
