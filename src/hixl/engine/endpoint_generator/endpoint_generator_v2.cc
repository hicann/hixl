/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "endpoint_generator_v2.h"

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <set>
#include <sstream>
#include <string>

#include "acl/acl.h"
#include "acl/acl_rt.h"
#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/hixl_utils.h"
#include "dcmi_proxy.h"
#include "dsmi_proxy.h"
#include "endpoint_generator_base.h"
#include "local_comm_res_generator_v1.h"

namespace hixl {
namespace {

constexpr size_t kUbgEidMarkerByteIndex = 7U;
constexpr uint8_t kUbgEidMarkerMask = 0xC0U;
constexpr uint8_t kUbgEidMarkerValue = 0x80U;
constexpr size_t kEidHexStrLen = COMM_ADDR_EID_LEN * 2U;
constexpr int32_t kSetwWidth = 2;

// protocol_desc parsing mode, only relevant to the default (ScaleOut) generation flow.
enum class ProtocolDescMode { kNone, kUboe, kUbg, kConflict };

constexpr uint32_t kInterconTypeUboeOverSwitch = 0U;  // SWITCH David -> UB_RTP -> 5808 UBoE superplane.
constexpr uint32_t kInterconTypeRoceOverNpu = 1U;     // NPU 1825 RoCE, also the driver default.
constexpr uint32_t kInterconTypeUboeOverNpu = 2U;     // NPU David UBoE.
constexpr uint32_t kInterconTypeRoceOverCpu = 3U;     // CPU Host NIC RoCE.
constexpr uint32_t kInterconTypeUbgOverNpu = 4U;      // NPU David UB_RTP.

bool IsRoceInterconType(uint32_t intercon_type) {
  return intercon_type == kInterconTypeRoceOverNpu || intercon_type == kInterconTypeRoceOverCpu;
}

bool IsUboeInterconType(uint32_t intercon_type) {
  return intercon_type == kInterconTypeUboeOverSwitch || intercon_type == kInterconTypeUboeOverNpu;
}

bool IsUbgInterconType(uint32_t intercon_type) {
  return intercon_type == kInterconTypeUbgOverNpu;
}

ProtocolDescMode ParseProtocolDescMode(const std::vector<std::string> &protocol_desc) {
  if (protocol_desc.empty()) {
    return ProtocolDescMode::kNone;
  }
  const bool has_uboe = std::find(protocol_desc.begin(), protocol_desc.end(), kUboeProtocolDesc) != protocol_desc.end();
  const bool has_ub_rtp =
      std::find(protocol_desc.begin(), protocol_desc.end(), kUbRtpProtocolDesc) != protocol_desc.end();
  if (has_uboe && has_ub_rtp) {
    return ProtocolDescMode::kConflict;
  }
  if (has_uboe) {
    return ProtocolDescMode::kUboe;
  }
  if (has_ub_rtp) {
    return ProtocolDescMode::kUbg;
  }
  return ProtocolDescMode::kNone;
}

Status GetScaleOutNetInstanceId(int32_t logic_dev_id, std::string &net_instance_id) {
  DcmiSpodInfo spod_info = {};
  uint32_t buf_size = sizeof(DcmiSpodInfo);
  HIXL_CHK_STATUS_RET(DcmiProxy::GetDeviceInfo(static_cast<uint32_t>(logic_dev_id), kDcmiMainCmdChipInf,
                                               kDcmiSubCmdSpodInfo, &spod_info, &buf_size),
                      "Failed to get spod info for ScaleOut net_instance_id, logic_dev_id=%d", logic_dev_id);
  net_instance_id = std::string(kSuperPodNetInstancePrefix) + std::to_string(spod_info.super_pod_id);
  return SUCCESS;
}

std::string ConvertEidToString(const unsigned char *raw, size_t len) {
  std::ostringstream oss;
  oss << std::hex << std::setfill('0');
  for (size_t i = 0; i < len; ++i) {
    oss << std::setw(kSetwWidth) << static_cast<uint32_t>(raw[i]);
  }
  return oss.str();
}

bool IsUbgEid(const DcmiUrmaEidInfo &eid_info) {
  // Example: ...0a80... has marker byte 0x80. High two bits 10 means UB_RTP, 11 means UBoE.
  return (eid_info.eid.raw[kUbgEidMarkerByteIndex] & kUbgEidMarkerMask) == kUbgEidMarkerValue;
}

Status GetUbgEidFromDcmi(uint32_t logic_id, std::string &eid) {
  HIXL_LOGI("[GetUbgEidFromDcmi] start, logic_id=%u", logic_id);
  uint32_t dev_cnt = 0;
  HIXL_CHK_STATUS_RET(DcmiProxy::GetUrmaDeviceCnt(logic_id, &dev_cnt), "GetUrmaDeviceCnt failed, logic_id=%u",
                      logic_id);
  for (uint32_t dev_index = 0; dev_index < dev_cnt; ++dev_index) {
    DcmiUrmaEidInfo eid_list[kMaxEidPerUe];
    int32_t eid_cnt = kMaxEidPerUe;
    HIXL_CHK_STATUS_RET(DcmiProxy::GetEidList(logic_id, static_cast<int32_t>(dev_index), eid_list, &eid_cnt),
                        "GetEidList failed, logic_id=%u, urma_dev_index=%u", logic_id, dev_index);
    HIXL_CHK_BOOL_RET_STATUS(eid_cnt >= 0 && eid_cnt <= kMaxEidPerUe, FAILED,
                             "GetEidList returned invalid eid_cnt=%d, logic_id=%u, urma_dev_index=%u", eid_cnt,
                             logic_id, dev_index);
    for (int32_t eid_index = 0; eid_index < eid_cnt; ++eid_index) {
      if (std::all_of(eid_list[eid_index].eid.raw, eid_list[eid_index].eid.raw + kDcmiUrmaEidSize,
                      [](uint8_t byte) { return byte == 0; })) {
        continue;
      }
      if (IsUbgEid(eid_list[eid_index])) {
        eid = ConvertEidToString(eid_list[eid_index].eid.raw, sizeof(eid_list[eid_index].eid.raw));
        HIXL_LOGI("[GetUbgEidFromDcmi] found UB_RTP EID, logic_id=%u, eid=%s", logic_id, eid.c_str());
        return SUCCESS;
      }
    }
  }
  HIXL_EVENT("[EndpointGeneratorV2] UB_RTP EID not found, logic_id=%u, dev_cnt=%u", logic_id, dev_cnt);
  return FAILED;
}

Status GetUboeIp(std::string &ip) {
  int32_t dev_id = 0;
  HIXL_CHK_ACL_RET(aclrtGetDevice(&dev_id));

  int32_t dev_logic_id = dev_id;
  HIXL_CHK_ACL_RET(aclrtGetLogicDevIdByUserDevId(dev_id, &dev_logic_id));

  uint32_t slot_id = 0;
  auto get_ret = DsmiProxy::GetDevSlotId(dev_logic_id, slot_id);
  if (get_ret != SUCCESS) {
    HIXL_LOGW("can't find dev slot_id, use dev_logic_id instead, ret=%u, dev_logic_id=%d.", get_ret, dev_logic_id);
    slot_id = static_cast<uint32_t>(dev_logic_id);
  }
  return GetBondIpAddress(dev_logic_id, slot_id, ip);
}

Status GenDefaultUboeEndpointConfig(int32_t logic_dev_id, EndpointConfig &endpoint_config) {
  std::string uboe_ip;
  if (GetUboeIp(uboe_ip) != SUCCESS) {
    return FAILED;
  }
  endpoint_config.protocol = kProtocolUboe;
  endpoint_config.comm_id = uboe_ip;
  endpoint_config.placement = kPlacementDevice;
  HIXL_CHK_STATUS_RET(GetScaleOutNetInstanceId(logic_dev_id, endpoint_config.net_instance_id),
                      "GetScaleOutNetInstanceId failed");
  HIXL_EVENT("[EndpointGeneratorV2] GenDefaultUboeEndpointConfig, ip=%s, net_instance_id=%s", uboe_ip.c_str(),
             endpoint_config.net_instance_id.c_str());
  return SUCCESS;
}

Status GenDefaultUbgEndpointConfig(int32_t logic_dev_id, EndpointConfig &endpoint_config) {
  uint32_t logic_id = static_cast<uint32_t>(logic_dev_id);
  std::string eid;
  if (GetUbgEidFromDcmi(logic_id, eid) != SUCCESS) {
    return FAILED;
  }
  endpoint_config.protocol = kProtocolUbRtp;
  endpoint_config.comm_id = eid;
  endpoint_config.placement = kPlacementDevice;
  HIXL_CHK_STATUS_RET(GetScaleOutNetInstanceId(logic_dev_id, endpoint_config.net_instance_id),
                      "GetScaleOutNetInstanceId failed");
  HIXL_EVENT("[EndpointGeneratorV2] GenDefaultUbRtpEndpointConfig, eid=%s, net_instance_id=%s", eid.c_str(),
             endpoint_config.net_instance_id.c_str());
  return SUCCESS;
}

Status GenerateScaleOutEndpointByInterconType(int32_t user_dev_id, std::vector<EndpointConfig> &endpoint_list) {
  int32_t logic_dev_id = 0;
  HIXL_CHK_ACL_RET(aclrtGetLogicDevIdByUserDevId(user_dev_id, &logic_dev_id));
  // When DSMI InterconType is not ready, fall back to UB auto-gen; empty endpoint_list is handled by the caller.
  if (!DsmiProxy::IsInterconTypeSupported()) {
    HIXL_LOGW(
        "[EndpointGeneratorV2] DSMI InterconType not supported yet, fallback to existing UB generation, "
        "user_dev_id=%d, logic_dev_id=%d",
        user_dev_id, logic_dev_id);
    return SUCCESS;
  }
  uint32_t intercon_type = 0U;
  HIXL_CHK_STATUS_RET(DsmiProxy::GetInterconType(logic_dev_id, intercon_type), "GetInterconType failed");
  HIXL_EVENT("[EndpointGeneratorV2] DSMI InterconType=%u for user_dev_id=%d, logic_dev_id=%d", intercon_type,
             user_dev_id, logic_dev_id);
  if (IsUbgInterconType(intercon_type)) {
    EndpointConfig ubg_endpoint{};
    if (GenDefaultUbgEndpointConfig(logic_dev_id, ubg_endpoint) != SUCCESS) {
      HIXL_EVENT("[EndpointGeneratorV2] UB_RTP endpoint generation failed, skip ScaleOut, fallback to UB");
      return SUCCESS;
    }
    endpoint_list.emplace_back(std::move(ubg_endpoint));
    return SUCCESS;
  }
  if (IsUboeInterconType(intercon_type)) {
    EndpointConfig uboe_endpoint{};
    if (GenDefaultUboeEndpointConfig(logic_dev_id, uboe_endpoint) != SUCCESS) {
      HIXL_EVENT("[EndpointGeneratorV2] UBoE endpoint generation failed, skip ScaleOut, fallback to UB");
      return SUCCESS;
    }
    endpoint_list.emplace_back(std::move(uboe_endpoint));
    return SUCCESS;
  }
  if (IsRoceInterconType(intercon_type)) {
    HIXL_EVENT("[EndpointGeneratorV2] InterconType=%u is RoCE, keep existing UB generation", intercon_type);
    return SUCCESS;
  }
  HIXL_LOGE(FAILED, "Unsupported DSMI InterconType=%u for ScaleOut auto endpoint generation", intercon_type);
  return FAILED;
}

Status GenScaleOutByProtocolDesc(int32_t device_id, const std::vector<std::string> &protocol_desc,
                                 std::vector<EndpointConfig> &endpoint_list) {
  const ProtocolDescMode mode = ParseProtocolDescMode(protocol_desc);
  HIXL_CHK_BOOL_RET_STATUS(mode != ProtocolDescMode::kConflict, PARAM_INVALID,
                           "protocol_desc cannot contain both %s and %s", kUbRtpProtocolDesc, kUboeProtocolDesc);
  if (mode != ProtocolDescMode::kUboe && mode != ProtocolDescMode::kUbg) {
    return SUCCESS;
  }
  int32_t logic_dev_id = 0;
  HIXL_CHK_ACL_RET(aclrtGetLogicDevIdByUserDevId(device_id, &logic_dev_id));
  if (DsmiProxy::IsInterconTypeSupported()) {
    uint32_t intercon_type = 0U;
    HIXL_CHK_STATUS_RET(DsmiProxy::GetInterconType(logic_dev_id, intercon_type), "GetInterconType failed");
    const bool match =
        (mode == ProtocolDescMode::kUbg) ? IsUbgInterconType(intercon_type) : IsUboeInterconType(intercon_type);
    const char *desc = (mode == ProtocolDescMode::kUbg) ? kUbRtpProtocolDesc : kUboeProtocolDesc;
    HIXL_CHK_BOOL_RET_STATUS(match, FAILED, "protocol_desc=%s conflicts with InterconType=%u", desc, intercon_type);
  }
  EndpointConfig endpoint{};
  if (mode == ProtocolDescMode::kUbg) {
    HIXL_CHK_STATUS_RET(GenDefaultUbgEndpointConfig(logic_dev_id, endpoint), "GenDefaultUbgEndpointConfig failed");
  } else {
    HIXL_CHK_STATUS_RET(GenDefaultUboeEndpointConfig(logic_dev_id, endpoint), "GenDefaultUboeEndpointConfig failed");
  }
  endpoint_list.emplace_back(std::move(endpoint));
  return SUCCESS;
}

// Generate ScaleOut endpoints from protocol_desc (auto-select by InterconType when empty).
Status GenAutoScaleOutEndpoints(int32_t device_id, const std::vector<std::string> &protocol_desc,
                                std::vector<EndpointConfig> &endpoint_list) {
  if (protocol_desc.empty()) {
    return GenerateScaleOutEndpointByInterconType(device_id, endpoint_list);
  }
  return GenScaleOutByProtocolDesc(device_id, protocol_desc, endpoint_list);
}

Status ResolveUbCtpNeedAndMode(const std::vector<std::string> &protocol_desc, bool &ub_needed,
                               LocalCommResGenerateMode &mode) {
  ub_needed = false;
  mode = LocalCommResGenerateMode::kDeviceOnly;
  if (protocol_desc.empty()) {
    ub_needed = true;
    return SUCCESS;
  }
  std::set<std::string> desc_set;
  HIXL_CHK_STATUS_RET(EndpointGeneratorBase::ParseProtocolDesc(protocol_desc, desc_set), "ParseProtocolDesc failed");
  const std::string device_key = EndpointGeneratorBase::BuildProtocolDescKey(kProtocolUbCtp, kPlacementDevice);
  const std::string host_key = EndpointGeneratorBase::BuildProtocolDescKey(kProtocolUbCtp, kPlacementHost);
  ub_needed = desc_set.count(device_key) > 0 || desc_set.count(host_key) > 0;
  if (desc_set.count(host_key) > 0) {
    mode = LocalCommResGenerateMode::kDeviceAndHost;
  }
  return SUCCESS;
}

struct UbCtpAppendInput {
  int32_t phy_dev_id;
  const std::string &topo_path;
  const std::vector<std::string> &protocol_desc;
  const std::string &user_local_comm_res;
};

// Generate ub_ctp endpoints on demand and merge them into endpoint_list.
Status AppendUbCtpEndpoints(const UbCtpAppendInput &input, std::vector<EndpointConfig> &endpoint_list,
                            std::string &net_instance_id) {
  bool ub_needed = false;
  LocalCommResGenerateMode ub_mode = LocalCommResGenerateMode::kDeviceOnly;
  HIXL_CHK_STATUS_RET(ResolveUbCtpNeedAndMode(input.protocol_desc, ub_needed, ub_mode),
                      "ResolveUbCtpNeedAndMode failed");
  if (!ub_needed) {
    return SUCCESS;
  }
  LocalCommRes ub_res;
  if (input.topo_path.empty()) {
    HIXL_CHK_STATUS_RET(GenerateLocalCommRes(input.phy_dev_id, ub_mode, input.user_local_comm_res, ub_res),
                        "GenerateLocalCommRes failed");
  } else {
    HIXL_CHK_STATUS_RET(
        GenerateLocalCommRes(input.phy_dev_id, input.topo_path, ub_mode, input.user_local_comm_res, ub_res),
        "GenerateLocalCommRes failed");
  }
  if (net_instance_id.empty()) {
    net_instance_id = ub_res.net_instance_id;
  }
  for (auto &ep : ub_res.endpoint_list) {
    ep.server_id = ub_res.server_id;
    endpoint_list.emplace_back(std::move(ep));
  }
  return SUCCESS;
}

// Fill net_instance_id from generated endpoints when it is still empty.
void FillNetInstanceIdIfEmpty(std::vector<EndpointConfig> &endpoint_list, std::string &net_instance_id) {
  if (!net_instance_id.empty()) {
    return;
  }
  for (const auto &ep : endpoint_list) {
    if (!ep.net_instance_id.empty()) {
      net_instance_id = ep.net_instance_id;
      break;
    }
  }
}

}  // namespace

Status EndpointGeneratorV2::AutoGen(const HixlOptions &options, const std::string & /* local_engine */,
                                    std::vector<EndpointConfig> &endpoint_list) const {
  int32_t device_id = 0;
  HIXL_CHK_ACL_RET(aclrtGetDevice(&device_id));
  int32_t phy_id = 0;
  HIXL_CHK_ACL_RET(aclrtGetPhyDevIdByLogicDevId(device_id, &phy_id));

  if (IsIntraRoceEnabled()) {
    HIXL_LOGI("[EndpointGeneratorV2] HCCL_INTRA_ROCE_ENABLE=1, skip ScaleOut and UB generation");
    return SUCCESS;
  }

  HIXL_LOGI("[EndpointGeneratorV2] auto-generate: device_id=%d, phy_id=%d", device_id, phy_id);
  std::string net_instance_id;
  const std::vector<std::string> protocol_desc = options.GetProtocolDesc();
  const std::string user_local_comm_res = options.LocalCommRes().value_or("");
  const std::string resolved_topo_path = options.TopoFilePath().value_or("");
  AutoGenInput input{device_id, phy_id, resolved_topo_path, protocol_desc, user_local_comm_res};
  HIXL_CHK_STATUS_RET(AutoGenCore(input, endpoint_list, net_instance_id), "[EndpointGeneratorV2] AutoGenCore failed");
  return SUCCESS;
}

Status EndpointGeneratorV2::AutoGenCore(const EndpointGeneratorV2::AutoGenInput &input,
                                        std::vector<EndpointConfig> &endpoint_list, std::string &net_instance_id) {
  endpoint_list.clear();
  net_instance_id.clear();

  HIXL_CHK_STATUS_RET(GenAutoScaleOutEndpoints(input.device_id, input.protocol_desc, endpoint_list),
                      "GenAutoScaleOutEndpoints failed");

  UbCtpAppendInput ub_input{input.phy_dev_id, input.topo_path, input.protocol_desc, input.user_local_comm_res};
  HIXL_CHK_STATUS_RET(AppendUbCtpEndpoints(ub_input, endpoint_list, net_instance_id), "AppendUbCtpEndpoints failed");

  HIXL_CHK_STATUS_RET(EndpointGeneratorBase::FilterEndpointsByProtocolDescList(input.protocol_desc, endpoint_list),
                      "FilterEndpointsByProtocolDescList failed");

  FillNetInstanceIdIfEmpty(endpoint_list, net_instance_id);
  return SUCCESS;
}

}  // namespace hixl
