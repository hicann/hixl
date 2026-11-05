/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "host_roce_endpoint_generator.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <sys/stat.h>
#include <vector>

#include "acl/acl.h"
#include "acl/acl_rt.h"
#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "dcmi_proxy.h"
#include "ibverbs_proxy.h"
#include "nlohmann/json.hpp"

namespace hixl {
namespace {

constexpr const char kNpuKeyPrefix[] = "npu:";
constexpr size_t kMaxNicNameLen = 63U;
constexpr int32_t kDcmiErrCodeNotSupport = -8255;
constexpr int32_t kDcmiTopoTypeUb = 8;
constexpr size_t kMaxNicTopoFileBytes = 1024U * 1024U;

struct NicAffinityEntry {
  std::vector<std::string> preferred;
};
using NicTopoData = std::map<int32_t, NicAffinityEntry>;

struct SkipNicLog {
  int32_t user_id;
  const char *nic;
  const char *msg;
  bool with_dcmi = false;
  int32_t dcmi_ret = 0;
  int32_t topo_type = 0;
};

void LogSkipNic(const SkipNicLog &log) {
  if (log.with_dcmi) {
    HIXL_LOGE(PARAM_INVALID, "%s, user_device_id=%d, nic=%s, dcmi_ret=%d, topo_type=%d", log.msg, log.user_id, log.nic,
              log.dcmi_ret, log.topo_type);
    return;
  }
  HIXL_LOGE(PARAM_INVALID, "%s, user_device_id=%d, nic=%s", log.msg, log.user_id, log.nic);
}

bool IsValidNicName(const nlohmann::json &name) {
  if (!name.is_string()) {
    return false;
  }
  const std::string nic = name.get<std::string>();
  return !nic.empty() && nic.size() <= kMaxNicNameLen;
}

Status ParseNpuKey(const std::string &key, int32_t &user_id) {
  const size_t prefix_len = std::char_traits<char>::length(kNpuKeyPrefix);
  HIXL_CHK_BOOL_RET_STATUS(key.size() > prefix_len && key.compare(0, prefix_len, kNpuKeyPrefix) == 0, PARAM_INVALID,
                           "invalid nic topo key:%s", key.c_str());
  const char *digits = key.c_str() + prefix_len;
  HIXL_CHK_BOOL_RET_STATUS(std::isdigit(static_cast<unsigned char>(digits[0])) != 0, PARAM_INVALID,
                           "invalid nic topo key:%s", key.c_str());
  HIXL_CHK_BOOL_RET_STATUS(digits[0] != '0' || digits[1] == '\0', PARAM_INVALID, "invalid nic topo key:%s",
                           key.c_str());
  for (const char *p = digits; *p != '\0'; ++p) {
    HIXL_CHK_BOOL_RET_STATUS(std::isdigit(static_cast<unsigned char>(*p)) != 0, PARAM_INVALID,
                             "invalid nic topo key:%s", key.c_str());
  }
  const long value = std::strtol(digits, nullptr, 10);
  HIXL_CHK_BOOL_RET_STATUS(value >= 0 && value <= static_cast<long>(INT32_MAX), PARAM_INVALID,
                           "nic topo npu id out of range:%s", key.c_str());
  user_id = static_cast<int32_t>(value);
  return SUCCESS;
}

Status ReadNicTopoText(const std::string &path, std::string &text) {
  struct stat file_stat {};
  HIXL_CHK_BOOL_RET_STATUS(stat(path.c_str(), &file_stat) == 0 && S_ISREG(file_stat.st_mode), PARAM_INVALID,
                           "nic topo path is not a regular file:%s", path.c_str());
  HIXL_CHK_BOOL_RET_STATUS(
      static_cast<size_t>(file_stat.st_size) > 0 && static_cast<size_t>(file_stat.st_size) <= kMaxNicTopoFileBytes,
      PARAM_INVALID, "nic topo file size is invalid, path:%s, size:%lld", path.c_str(),
      static_cast<long long>(file_stat.st_size));
  std::ifstream ifs(path);
  HIXL_CHK_BOOL_RET_STATUS(ifs.is_open(), PARAM_INVALID, "failed to open nic topo file, path:%s", path.c_str());
  text.assign((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
  HIXL_CHK_BOOL_RET_STATUS(!text.empty(), PARAM_INVALID, "nic topo file is empty, path:%s", path.c_str());
  return SUCCESS;
}

Status ParseAffinityValue(const nlohmann::json &value, NicAffinityEntry &entry) {
  HIXL_CHK_BOOL_RET_STATUS(value.is_array() && value.size() == 2U, PARAM_INVALID,
                           "nic affinity must be a two-element array");
  HIXL_CHK_BOOL_RET_STATUS(value[0].is_array() && value[1].is_array(), PARAM_INVALID,
                           "preferred and avail must be arrays");
  HIXL_CHK_BOOL_RET_STATUS(value[0].size() == 1U, PARAM_INVALID,
                           "preferred nic list must contain exactly one name, size=%zu", value[0].size());
  HIXL_CHK_BOOL_RET_STATUS(value[1].empty(), PARAM_INVALID, "avail nic list must be empty, size=%zu", value[1].size());
  HIXL_CHK_BOOL_RET_STATUS(IsValidNicName(value[0][0]), PARAM_INVALID, "invalid preferred nic name");
  entry.preferred.push_back(value[0][0].get<std::string>());
  return SUCCESS;
}

Status ParseNicTopoFile(const std::string &path, NicTopoData &topo) {
  std::string text;
  HIXL_CHK_STATUS_RET(ReadNicTopoText(path, text), "failed to read nic topo file, path:%s", path.c_str());
  nlohmann::json root;
  try {
    root = nlohmann::json::parse(text);
  } catch (const nlohmann::json::exception &e) {
    HIXL_LOGE(PARAM_INVALID, "nic topo file is not json, path:%s, exception:%s", path.c_str(), e.what());
    return PARAM_INVALID;
  }
  HIXL_CHK_BOOL_RET_STATUS(root.is_object(), PARAM_INVALID, "nic topo file must be a json object, path:%s",
                           path.c_str());
  topo.clear();
  for (auto it = root.begin(); it != root.end(); ++it) {
    int32_t user_id = 0;
    NicAffinityEntry entry;
    HIXL_CHK_STATUS_RET(ParseNpuKey(it.key(), user_id), "invalid npu key:%s", it.key().c_str());
    HIXL_CHK_STATUS_RET(ParseAffinityValue(it.value(), entry), "invalid nic affinity for key:%s", it.key().c_str());
    topo.emplace(user_id, std::move(entry));
  }
  return SUCCESS;
}

Status SelectPreferredNic(const NicTopoData &topo, int32_t user_device_id, std::string &nic_name) {
  const auto it = topo.find(user_device_id);
  HIXL_CHK_BOOL_RET_STATUS(it != topo.end() && !it->second.preferred.empty(), PARAM_INVALID,
                           "preferred nic is unavailable, host roce generation failed, user_device_id=%d",
                           user_device_id);
  nic_name = it->second.preferred.front();
  return SUCCESS;
}

Status FilterAffineNics(int32_t user_id, int32_t logic_id, std::vector<std::string> &nic_names) {
  std::vector<std::string> hits;
  for (const auto &nic : nic_names) {
    int32_t dcmi_ret = 0;
    int32_t topo_type = 0;
    if (DcmiProxy::GetTopoInfoByNicName(logic_id, nic, dcmi_ret, topo_type) != SUCCESS) {
      LogSkipNic({user_id, nic.c_str(), "query nic affinity failed, skip this nic", true, dcmi_ret, topo_type});
      continue;
    }
    HIXL_CHK_BOOL_RET_STATUS(dcmi_ret != kDcmiErrCodeNotSupport, PARAM_INVALID,
                             "dcmi nic topo returns not support, host roce generation failed, user_device_id=%d, "
                             "logic_id=%d, nic=%s, dcmi_ret=%d",
                             user_id, logic_id, nic.c_str(), dcmi_ret);
    if (dcmi_ret == 0 && topo_type == kDcmiTopoTypeUb) {
      hits.push_back(nic);
    } else if (dcmi_ret == 0) {
      LogSkipNic({user_id, nic.c_str(), "affine nic is not ub type, skip this nic", true, dcmi_ret, topo_type});
    } else {
      LogSkipNic({user_id, nic.c_str(), "query nic affinity failed, skip this nic", true, dcmi_ret, topo_type});
    }
  }
  nic_names = std::move(hits);
  return SUCCESS;
}

void KeepNicsWithIpv4(int32_t user_id, std::vector<std::string> &nic_names) {
  std::vector<std::string> usable;
  for (const auto &nic : nic_names) {
    std::string ip;
    if (IbverbsProxy::GetNicIpv4ByName(nic, ip) != SUCCESS) {
      LogSkipNic({user_id, nic.c_str(), "affine nic has no ipv4, skip this nic"});
      continue;
    }
    usable.push_back(nic);
  }
  nic_names = std::move(usable);
}

void KeepNicSelectedByLogicId(int32_t logic_id, std::vector<std::string> &nic_names) {
  if (nic_names.empty()) {
    return;
  }
  // Usable NIC count can be smaller than the NPU count. logic_id modulo then wraps,
  // so different NPUs can share the same RoCE IP.
  const size_t index = static_cast<size_t>(logic_id) % nic_names.size();
  std::string selected = std::move(nic_names[index]);
  nic_names.clear();
  nic_names.push_back(std::move(selected));
}

Status CollectDcmiNicNames(int32_t user_id, int32_t logic_id, std::vector<std::string> &nic_names) {
  HIXL_CHK_BOOL_RET_STATUS(DcmiProxy::IsTopoInfoByNicSupported(), PARAM_INVALID,
                           "dcmi nic topo is unsupported, host roce generation failed, user_device_id=%d, logic_id=%d",
                           user_id, logic_id);
  // ListNicNames returns FAILED. Keep the public Host RoCE failure as PARAM_INVALID.
  HIXL_CHK_BOOL_RET_STATUS(IbverbsProxy::ListNicNames(nic_names) == SUCCESS, PARAM_INVALID,
                           "ibverbs device list is unavailable, host roce generation failed, user_device_id=%d, "
                           "logic_id=%d",
                           user_id, logic_id);
  std::sort(nic_names.begin(), nic_names.end());
  HIXL_CHK_STATUS_RET(FilterAffineNics(user_id, logic_id, nic_names),
                      "dcmi nic topo returns not support, host roce generation failed, user_device_id=%d, logic_id=%d",
                      user_id, logic_id);
  KeepNicsWithIpv4(user_id, nic_names);
  KeepNicSelectedByLogicId(logic_id, nic_names);
  return SUCCESS;
}

Status CollectHostRoceNics(const HixlOptions &options, int32_t user_id, int32_t logic_id,
                           std::vector<std::string> &nic_names) {
  nic_names.clear();
  const auto path = options.NicTopoPath();
  if (path.has_value()) {
    NicTopoData topo;
    std::string nic;
    HIXL_CHK_STATUS_RET(ParseNicTopoFile(*path, topo), "ParseNicTopoFile failed, user_device_id=%d", user_id);
    HIXL_CHK_STATUS_RET(SelectPreferredNic(topo, user_id, nic), "SelectPreferredNic failed, user_device_id=%d",
                        user_id);
    nic_names.push_back(std::move(nic));
    return SUCCESS;
  }
  HIXL_CHK_STATUS_RET(CollectDcmiNicNames(user_id, logic_id, nic_names),
                      "CollectDcmiNicNames failed, user_device_id=%d, logic_id=%d", user_id, logic_id);
  return SUCCESS;
}

Status ResolveHostRoceNetInstanceId(int32_t logic_id, std::string &net_instance_id) {
  DcmiSpodInfo spod_info = {};
  uint32_t buf_size = sizeof(DcmiSpodInfo);
  const Status dcmi_ret = DcmiProxy::GetDeviceInfo(static_cast<uint32_t>(logic_id), kDcmiMainCmdChipInf,
                                                   kDcmiSubCmdSpodInfo, &spod_info, &buf_size);
  HIXL_CHK_STATUS_RET(dcmi_ret, "Call api:DcmiProxy::GetDeviceInfo failed, ret=%d, logic_id=%d", dcmi_ret, logic_id);
  net_instance_id = std::string(kSuperPodNetInstancePrefix) + std::to_string(spod_info.super_pod_id);
  return SUCCESS;
}

EndpointConfig MakeHostRoceEndpoint(const std::string &ip, const std::string &net_instance_id) {
  EndpointConfig endpoint{};
  endpoint.protocol = kProtocolRoce;
  endpoint.placement = kPlacementHost;
  endpoint.comm_id = ip;
  endpoint.net_instance_id = net_instance_id;
  return endpoint;
}

struct HostRoceBuildInput {
  int32_t user_id;
  bool manual;
  const std::vector<std::string> &nic_names;
  const std::string &net_instance_id;
};

Status BuildAndAppendHostRoceEdges(const HostRoceBuildInput &input, std::vector<EndpointConfig> &endpoint_list) {
  std::vector<EndpointConfig> created;
  const char *source = input.manual ? "topo" : "dcmi";
  for (const auto &nic : input.nic_names) {
    std::string ip;
    if (IbverbsProxy::GetNicIpv4ByName(nic, ip) != SUCCESS) {
      HIXL_CHK_BOOL_RET_STATUS(!input.manual, PARAM_INVALID,
                               "preferred nic has no ipv4, host roce generation failed, user_device_id=%d, nic=%s",
                               input.user_id, nic.c_str());
      LogSkipNic({input.user_id, nic.c_str(), "affine nic has no ipv4, skip this nic"});
      continue;
    }
    HIXL_EVENT("Selected host roce nic, user_device_id=%d, nic=%s, source=%s, ip=%s", input.user_id, nic.c_str(),
               source, ip.c_str());
    created.push_back(MakeHostRoceEndpoint(ip, input.net_instance_id));
  }
  HIXL_CHK_BOOL_RET_STATUS(!created.empty(), PARAM_INVALID,
                           "no host roce endpoint is generated, host roce generation failed, user_device_id=%d",
                           input.user_id);
  endpoint_list.insert(endpoint_list.end(), created.begin(), created.end());
  return SUCCESS;
}

}  // namespace

Status HostRoceEndpointGenerator::GenerateHostRoceEndpoints(const HixlOptions &options,
                                                            std::vector<EndpointConfig> &endpoint_list) {
  int32_t user_id = 0;
  const aclError device_ret = aclrtGetDevice(&user_id);
  HIXL_CHK_BOOL_RET_STATUS(device_ret == ACL_SUCCESS, PARAM_INVALID,
                           "failed to get user device id, host roce generation failed, ret=%d", device_ret);
  int32_t logic_id = 0;
  const aclError logic_ret = aclrtGetLogicDevIdByUserDevId(user_id, &logic_id);
  HIXL_CHK_BOOL_RET_STATUS(logic_ret == ACL_SUCCESS, PARAM_INVALID,
                           "Call api:aclrtGetLogicDevIdByUserDevId failed, ret=%d, user_device_id=%d", logic_ret,
                           user_id);
  std::vector<std::string> nic_names;
  HIXL_CHK_STATUS_RET(CollectHostRoceNics(options, user_id, logic_id, nic_names),
                      "CollectHostRoceNics failed, user_device_id=%d, logic_id=%d", user_id, logic_id);
  std::string net_instance_id;
  // GetDeviceInfo failure is FAILED inside the helper. The Host RoCE entry still returns PARAM_INVALID.
  HIXL_CHK_BOOL_RET_STATUS(ResolveHostRoceNetInstanceId(logic_id, net_instance_id) == SUCCESS, PARAM_INVALID,
                           "host roce net_instance_id is unavailable, host roce generation failed, logic_id=%d",
                           logic_id);
  const HostRoceBuildInput build_input{user_id, options.NicTopoPath().has_value(), nic_names, net_instance_id};
  HIXL_CHK_STATUS_RET(BuildAndAppendHostRoceEdges(build_input, endpoint_list),
                      "BuildAndAppendHostRoceEdges failed, user_device_id=%d", user_id);
  return SUCCESS;
}

}  // namespace hixl
