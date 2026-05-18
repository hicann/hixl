/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "engine/endpoint_matcher.h"
#include <algorithm>
#include <cstdlib>
#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "engine/client_handler_factory.h"

namespace hixl {
namespace {
constexpr uint32_t kMaxUbCsClientNum = 4U;
}  // namespace

CommType EndpointMatcher::ParseCommType(const std::string &local, const std::string &remote) {
  if (local == kPlacementDevice && remote == kPlacementDevice) {
    return CommType::COMM_TYPE_UB_D2D;
  }
  if (local == kPlacementDevice && remote == kPlacementHost) {
    return CommType::COMM_TYPE_UB_D2H;
  }
  if (local == kPlacementHost && remote == kPlacementHost) {
    return CommType::COMM_TYPE_UB_H2H;
  }
  return CommType::COMM_TYPE_UB_H2D;
}

bool EndpointMatcher::IsUbProtocol(const std::string &protocol) {
  return protocol == kProtocolUbCtp || protocol == kProtocolUbTp;
}

bool EndpointMatcher::IsDirectProtocol(const std::string &protocol) {
  return protocol == kProtocolRoce || protocol == kProtocolHccs || protocol == kProtocolUboe;
}

const char *EndpointMatcher::HandlerTypeToString(HandlerCreateArgs::HandlerType type) {
  switch (type) {
    case HandlerCreateArgs::HandlerType::DIRECT:
      return "DIRECT";
    case HandlerCreateArgs::HandlerType::UB:
      return "UB";
    default:
      return "UNKNOWN";
  }
}

const EndpointConfig *EndpointMatcher::FindByProtocol(const std::vector<EndpointConfig> &endpoints,
                                                      const std::string &protocol) {
  auto it = std::find_if(endpoints.begin(), endpoints.end(),
                         [&protocol](const auto &e) { return e.protocol == protocol; });
  return it != endpoints.end() ? &(*it) : nullptr;
}

void EndpointMatcher::BuildMatchMap(const std::vector<EndpointConfig> &endpoints,
                                    std::map<MatchKey, EndpointConfig> &out) {
  for (const auto &ep : endpoints) {
    if (IsUbProtocol(ep.protocol)) {
      out[{ep.dst_eid, ep.plane, ep.placement}] = ep;
    }
  }
}

bool EndpointMatcher::MustUseRoce(const std::vector<EndpointConfig> &local,
                                  const std::vector<EndpointConfig> &remote) {
  const char *env = std::getenv("HCCL_INTRA_ROCE_ENABLE");
  if (env != nullptr && std::string(env) == "1") {
    return true;
  }
  return local[0].net_instance_id != remote[0].net_instance_id;
}

std::map<MatchKey, EndpointConfig>::const_iterator EndpointMatcher::FindMatchingKey(
    const std::map<MatchKey, EndpointConfig> &map, const MatchKey &query) {
  for (auto it = map.begin(); it != map.end(); ++it) {
    if (it->first.Matches(query)) {
      return it;
    }
  }
  return map.end();
}

Status EndpointMatcher::TryMatchUboe(const std::vector<EndpointConfig> &local,
                                     const std::vector<EndpointConfig> &remote,
                                     std::vector<HandlerCreateArgs::EndpointPair> &pairs) {
  auto *li = FindByProtocol(local, kProtocolUboe);
  auto *ri = FindByProtocol(remote, kProtocolUboe);
  if (li != nullptr && ri != nullptr) {
    pairs.push_back({*li, *ri, CommType::COMM_TYPE_UBOE});
    return SUCCESS;
  }
  return FAILED;
}

Status EndpointMatcher::TryMatchRoce(const std::vector<EndpointConfig> &local,
                                     const std::vector<EndpointConfig> &remote,
                                     std::vector<HandlerCreateArgs::EndpointPair> &pairs) {
  auto *li = FindByProtocol(local, kProtocolRoce);
  auto *ri = FindByProtocol(remote, kProtocolRoce);
  if (li != nullptr && ri != nullptr) {
    pairs.push_back({*li, *ri, CommType::COMM_TYPE_ROCE});
    return SUCCESS;
  }
  HIXL_LOGE(PARAM_INVALID, "Failed to find matched ROCE endpoints");
  return PARAM_INVALID;
}

Status EndpointMatcher::TryMatchHccs(const std::vector<EndpointConfig> &local,
                                     const std::vector<EndpointConfig> &remote,
                                     std::vector<HandlerCreateArgs::EndpointPair> &pairs) {
  auto *li = FindByProtocol(local, kProtocolHccs);
  auto *ri = FindByProtocol(remote, kProtocolHccs);
  if (li != nullptr && ri != nullptr) {
    pairs.push_back({*li, *ri, CommType::COMM_TYPE_HCCS});
    return SUCCESS;
  }
  HIXL_LOGE(PARAM_INVALID, "Failed to find matched HCCS endpoints");
  return PARAM_INVALID;
}

Status EndpointMatcher::TryMatchUb(const EndpointConfig &local,
                                   const std::map<MatchKey, EndpointConfig> &peers,
                                   std::map<CommType, bool> &expected, uint32_t &count,
                                   std::vector<HandlerCreateArgs::EndpointPair> &pairs) {
  if (!IsUbProtocol(local.protocol)) {
    return SUCCESS;
  }
  for (const auto &placement : {kPlacementDevice, kPlacementHost}) {
    MatchKey key{local.comm_id, local.plane, placement};
    auto it = FindMatchingKey(peers, key);
    if (it != peers.end()) {
      CommType type = ParseCommType(local.placement, it->second.placement);
      if (!expected[type]) {
        pairs.push_back({local, it->second, type});
        expected[type] = true;
        count++;
      }
    }
  }
  return SUCCESS;
}

Status EndpointMatcher::MatchEndpoints(const std::vector<EndpointConfig> &local,
                                       const std::vector<EndpointConfig> &remote,
                                       std::vector<HandlerCreateArgs::EndpointPair> &matched_pairs,
                                       HandlerCreateArgs::HandlerType &handler_type) {
  if (TryMatchUboe(local, remote, matched_pairs) == SUCCESS) {
    handler_type = HandlerCreateArgs::HandlerType::DIRECT;
    return SUCCESS;
  }
  if (MustUseRoce(local, remote)) {
    HIXL_CHK_STATUS_RET(TryMatchRoce(local, remote, matched_pairs));
    handler_type = HandlerCreateArgs::HandlerType::DIRECT;
    return SUCCESS;
  }
  std::map<CommType, bool> expected = {{CommType::COMM_TYPE_UB_D2D, false}, {CommType::COMM_TYPE_UB_H2D, false},
                                       {CommType::COMM_TYPE_UB_D2H, false}, {CommType::COMM_TYPE_UB_H2H, false}};
  uint32_t count = 0;
  std::map<MatchKey, EndpointConfig> peers;
  BuildMatchMap(remote, peers);
  // 对local排序，dst_eid不为空的元素排在dst_eid为空的元素前面
  auto sorted_local = local;
  std::sort(sorted_local.begin(), sorted_local.end(),
            [](const EndpointConfig &a, const EndpointConfig &b) { return !a.dst_eid.empty() && b.dst_eid.empty(); });
  for (const auto &ep : sorted_local) {
    HIXL_CHK_STATUS_RET(TryMatchUb(ep, peers, expected, count, matched_pairs));
    if (count == kMaxUbCsClientNum) {
      handler_type = HandlerCreateArgs::HandlerType::UB;
      return SUCCESS;
    }
  }
  if (count > 0) {
    handler_type = HandlerCreateArgs::HandlerType::UB;
    return SUCCESS;
  }
  if (TryMatchHccs(local, remote, matched_pairs) == SUCCESS) {
    handler_type = HandlerCreateArgs::HandlerType::DIRECT;
    return SUCCESS;
  }
  HIXL_LOGE(PARAM_INVALID, "Failed to find matched endpoints");
  return PARAM_INVALID;
}

}  // namespace hixl
