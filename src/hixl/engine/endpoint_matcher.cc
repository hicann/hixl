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
#include <cstddef>
#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/hixl_utils.h"
#include "engine/client_handler_factory.h"

namespace hixl {
namespace {
constexpr uint32_t kMaxUbCsClientNum = 4U;

enum class MatchRuleType {
  GROUP,
  SINGLE,
};

struct MatchRule {
  MatchRuleType rule_type;
  HandlerCreateArgs::HandlerType handler_type;
  const char *protocol;
  const char *placement;
  CommType type;
  const char *reason;
};

constexpr MatchRule kCrossInstanceRules[] = {
    {MatchRuleType::SINGLE, HandlerCreateArgs::HandlerType::DIRECT, kProtocolUboe, kPlacementDevice,
     CommType::COMM_TYPE_UBOE, "cross-instance prefers device uboe"},
    {MatchRuleType::SINGLE, HandlerCreateArgs::HandlerType::DIRECT, kProtocolUbRtp, kPlacementDevice,
     CommType::COMM_TYPE_UBG, "cross-instance falls back to device ub_rtp"},
    {MatchRuleType::SINGLE, HandlerCreateArgs::HandlerType::DIRECT, kProtocolRoce, kPlacementDevice,
     CommType::COMM_TYPE_ROCE, "cross-instance falls back to device roce"},
    {MatchRuleType::SINGLE, HandlerCreateArgs::HandlerType::DIRECT, kProtocolRoce, kPlacementHost,
     CommType::COMM_TYPE_ROCE, "cross-instance falls back to host roce"},
};

constexpr MatchRule kSameInstanceRules[] = {
    {MatchRuleType::SINGLE, HandlerCreateArgs::HandlerType::DIRECT, kProtocolUbmem, kPlacementDevice,
     CommType::COMM_TYPE_UBMEM, "same-instance prefers device ubmem"},
    {MatchRuleType::GROUP, HandlerCreateArgs::HandlerType::UB, nullptr, nullptr, CommType::COMM_TYPE_UB_D2D,
     "same-instance prefers ub group"},
    {MatchRuleType::SINGLE, HandlerCreateArgs::HandlerType::DIRECT, kProtocolHccs, kPlacementDevice,
     CommType::COMM_TYPE_HCCS, "same-instance falls back to device hccs"},
    {MatchRuleType::SINGLE, HandlerCreateArgs::HandlerType::DIRECT, kProtocolUboe, kPlacementDevice,
     CommType::COMM_TYPE_UBOE, "same-instance falls back to device uboe"},
    {MatchRuleType::SINGLE, HandlerCreateArgs::HandlerType::DIRECT, kProtocolUbRtp, kPlacementDevice,
     CommType::COMM_TYPE_UBG, "same-instance falls back to device ub_rtp"},
    {MatchRuleType::SINGLE, HandlerCreateArgs::HandlerType::DIRECT, kProtocolRoce, kPlacementDevice,
     CommType::COMM_TYPE_ROCE, "same-instance falls back to device roce"},
    {MatchRuleType::SINGLE, HandlerCreateArgs::HandlerType::DIRECT, kProtocolRoce, kPlacementHost,
     CommType::COMM_TYPE_ROCE, "same-instance falls back to host roce"},
};

const char *EndpointLinkType(const EndpointConfig &ep) {
  if (!ep.dst_eid.empty()) {
    return "PEER2PEER";
  }
  return !ep.plane.empty() ? "PEER2NET" : "OTHER";
}

const char *LinkTypeName(LinkType priority) {
  return priority == LinkType::PEER2NET ? "PEER2NET" : "PEER2PEER";
}

int32_t LinkRank(const EndpointConfig &ep, LinkType priority) {
  const std::string type = EndpointLinkType(ep);
  if (type == LinkTypeName(priority)) {
    return 0;
  }
  return priority == LinkType::PEER2NET && type == "OTHER" ? 2 : 1;
}

const char *PreferredLinkStatus(size_t local_count, size_t remote_count, const std::string &preferred,
                                const char *selected) {
  if (preferred == selected) {
    return "matched";
  }
  if (local_count == 0U) {
    return "no_local_candidate";
  }
  return remote_count == 0U ? "no_remote_candidate" : "no_matching_pair";
}

void LogD2DSelection(const std::vector<EndpointConfig> &local, const std::map<MatchKey, EndpointConfig> &peers,
                     LinkType priority, const char *selected) {
  std::map<std::string, size_t> local_counts{{"PEER2NET", 0U}, {"PEER2PEER", 0U}};
  auto remote_counts = local_counts;
  for (const auto &ep : local) {
    ++local_counts[EndpointLinkType(ep)];
  }
  for (const auto &entry : peers) {
    if (entry.second.placement == kPlacementDevice) {
      ++remote_counts[EndpointLinkType(entry.second)];
    }
  }
  const std::string preferred = LinkTypeName(priority);
  HIXL_EVENT(
      "EndpointMatcher D2D priority:%s, local PEER2NET candidates:%zu, local PEER2PEER candidates:%zu, "
      "remote PEER2NET candidates:%zu, remote PEER2PEER candidates:%zu, selected:%s, preferred_status:%s",
      preferred.c_str(), local_counts["PEER2NET"], local_counts["PEER2PEER"], remote_counts["PEER2NET"],
      remote_counts["PEER2PEER"], selected,
      PreferredLinkStatus(local_counts[preferred], remote_counts[preferred], preferred, selected));
}

void LogEndpointMatchFailure(const std::vector<EndpointConfig> &local, const std::vector<EndpointConfig> &remote,
                             bool cross_instance) {
  HIXL_LOGE(PARAM_INVALID, "EndpointMatcher failed, cross_instance:%d, local_count:%zu, remote_count:%zu",
            static_cast<int32_t>(cross_instance), local.size(), remote.size());
  for (size_t i = 0; i < local.size(); ++i) {
    HIXL_LOGE(PARAM_INVALID, "local endpoint[%zu]:{%s}", i, local[i].ToString().c_str());
  }
  for (size_t i = 0; i < remote.size(); ++i) {
    HIXL_LOGE(PARAM_INVALID, "remote endpoint[%zu]:{%s}", i, remote[i].ToString().c_str());
  }
}

constexpr MatchRule kSameInstanceUbCtpDeviceOnlyRule = {
    MatchRuleType::SINGLE,      HandlerCreateArgs::HandlerType::DIRECT, kProtocolUbCtp, kPlacementDevice,
    CommType::COMM_TYPE_UB_D2D, "same-instance device-only ub ctp"};
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
  return protocol == kProtocolUbCtp;
}

static bool IsUbCtpEndpoint(const EndpointConfig &endpoint) {
  return endpoint.protocol == kProtocolUbCtp;
}

static bool AreUbCtpEndpointsAllDevice(const std::vector<EndpointConfig> &endpoints) {
  return std::all_of(endpoints.begin(), endpoints.end(), [](const EndpointConfig &endpoint) {
    return !IsUbCtpEndpoint(endpoint) || endpoint.placement == kPlacementDevice;
  });
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

void EndpointMatcher::BuildMatchMap(const std::vector<EndpointConfig> &endpoints,
                                    std::map<MatchKey, EndpointConfig> &out) {
  for (const auto &ep : endpoints) {
    if (IsUbProtocol(ep.protocol)) {
      out[{ep.dst_eid, ep.plane, ep.placement}] = ep;
    }
  }
}

bool EndpointMatcher::IsCrossInstance(const std::vector<EndpointConfig> &local,
                                      const std::vector<EndpointConfig> &remote) {
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

Status EndpointMatcher::TryMatchSingle(const std::vector<EndpointConfig> &local,
                                       const std::vector<EndpointConfig> &remote, const std::string &protocol,
                                       const std::string &placement, CommType type,
                                       std::vector<HandlerCreateArgs::EndpointPair> &pairs) {
  auto is_matched = [&protocol, &placement](const EndpointConfig &endpoint) {
    return endpoint.protocol == protocol && endpoint.placement == placement;
  };
  auto local_it = std::find_if(local.begin(), local.end(), is_matched);
  auto remote_it = std::find_if(remote.begin(), remote.end(), is_matched);
  if (local_it != local.end() && remote_it != remote.end()) {
    pairs.push_back({*local_it, *remote_it, type});
    return SUCCESS;
  }
  return FAILED;
}

Status EndpointMatcher::TryMatchUb(const EndpointConfig &local, const std::map<MatchKey, EndpointConfig> &peers,
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

Status EndpointMatcher::TryMatchH2rHLoopback(const EndpointConfig &local, const std::vector<EndpointConfig> &remote,
                                             std::map<CommType, bool> &expected, uint32_t &count,
                                             std::vector<HandlerCreateArgs::EndpointPair> &pairs) {
  if (!IsUbProtocol(local.protocol) || local.placement != kPlacementHost || local.server_id.empty() ||
      expected[CommType::COMM_TYPE_UB_H2H]) {
    return SUCCESS;
  }
  auto is_loopback_peer = [&local](const EndpointConfig &peer) {
    return IsUbProtocol(peer.protocol) && peer.protocol == local.protocol && peer.placement == kPlacementHost &&
           peer.plane == local.plane && peer.server_id == local.server_id;
  };
  auto remote_it = std::find_if(remote.begin(), remote.end(), is_loopback_peer);
  if (remote_it == remote.end()) {
    return SUCCESS;
  }

  EndpointConfig loopback_local = local;
  loopback_local.comm_id = remote_it->comm_id;
  pairs.push_back({loopback_local, *remote_it, CommType::COMM_TYPE_UB_H2H});
  expected[CommType::COMM_TYPE_UB_H2H] = true;
  count++;
  return SUCCESS;
}

void EndpointMatcher::MatchD2D(const std::vector<EndpointConfig> &local,
                               const std::map<MatchKey, EndpointConfig> &peers, LinkType priority,
                               std::vector<HandlerCreateArgs::EndpointPair> &pairs) {
  std::vector<EndpointConfig> candidates;
  for (const auto &ep : local) {
    if (IsUbCtpEndpoint(ep) && ep.placement == kPlacementDevice) {
      candidates.push_back(ep);
    }
  }
  std::stable_sort(candidates.begin(), candidates.end(), [priority](const EndpointConfig &a, const EndpointConfig &b) {
    const auto a_rank = LinkRank(a, priority);
    const auto b_rank = LinkRank(b, priority);
    return a_rank != b_rank ? a_rank < b_rank : a.plane < b.plane;
  });
  const char *selected = "NONE";
  for (const auto &ep : candidates) {
    const auto it = FindMatchingKey(peers, {ep.comm_id, ep.plane, kPlacementDevice});
    if (it != peers.end()) {
      pairs.push_back({ep, it->second, CommType::COMM_TYPE_UB_D2D});
      selected = EndpointLinkType(ep);
      break;
    }
  }
  LogD2DSelection(candidates, peers, priority, selected);
}

Status EndpointMatcher::TryMatchGroup(const std::vector<EndpointConfig> &local,
                                      const std::vector<EndpointConfig> &remote,
                                      std::vector<HandlerCreateArgs::EndpointPair> &pairs, LinkType priority) {
  std::map<CommType, bool> expected = {{CommType::COMM_TYPE_UB_D2D, false},
                                       {CommType::COMM_TYPE_UB_H2D, false},
                                       {CommType::COMM_TYPE_UB_D2H, false},
                                       {CommType::COMM_TYPE_UB_H2H, false}};
  std::map<MatchKey, EndpointConfig> peers;
  BuildMatchMap(remote, peers);
  MatchD2D(local, peers, priority, pairs);
  uint32_t count = static_cast<uint32_t>(pairs.size());
  // D2D has already been attempted in link priority order; preserve the old order for all other directions.
  expected[CommType::COMM_TYPE_UB_D2D] = true;
  auto sorted_local = local;
  // Prefer non-empty dst_eid, then ascending plane; preserve input order when both priorities are equal.
  std::stable_sort(sorted_local.begin(), sorted_local.end(), [](const EndpointConfig &a, const EndpointConfig &b) {
    if (a.dst_eid.empty() != b.dst_eid.empty()) {
      return !a.dst_eid.empty();
    }
    return a.plane < b.plane;
  });
  for (const auto &ep : sorted_local) {
    HIXL_CHK_STATUS_RET(TryMatchH2rHLoopback(ep, remote, expected, count, pairs));
    if (count == kMaxUbCsClientNum) {
      return SUCCESS;
    }
    HIXL_CHK_STATUS_RET(TryMatchUb(ep, peers, expected, count, pairs));
    if (count == kMaxUbCsClientNum) {
      return SUCCESS;
    }
  }
  return count > 0 ? SUCCESS : FAILED;
}

Status EndpointMatcher::TryMatchUbCtpD2DOnlyByGroup(const std::vector<EndpointConfig> &local,
                                                    const std::vector<EndpointConfig> &remote,
                                                    std::vector<HandlerCreateArgs::EndpointPair> &pairs,
                                                    LinkType priority) {
  std::vector<HandlerCreateArgs::EndpointPair> group_pairs;
  if (TryMatchGroup(local, remote, group_pairs, priority) != SUCCESS) {
    return FAILED;
  }
  if (group_pairs.size() != 1U || group_pairs[0].type != CommType::COMM_TYPE_UB_D2D ||
      !IsUbCtpEndpoint(group_pairs[0].local) || !IsUbCtpEndpoint(group_pairs[0].remote)) {
    return FAILED;
  }
  pairs = std::move(group_pairs);
  return SUCCESS;
}

Status EndpointMatcher::TryMatchByPriority(const std::vector<EndpointConfig> &local,
                                           const std::vector<EndpointConfig> &remote,
                                           std::vector<HandlerCreateArgs::EndpointPair> &pairs,
                                           HandlerCreateArgs::HandlerType &handler_type, LinkType priority) {
  auto try_match_rule = [&local, &remote, &pairs, &handler_type, priority](const MatchRule &rule) -> Status {
    Status status = FAILED;
    pairs.clear();
    switch (rule.rule_type) {
      case MatchRuleType::GROUP:
        status = TryMatchGroup(local, remote, pairs, priority);
        break;
      case MatchRuleType::SINGLE:
        if (rule.protocol == kProtocolUbCtp && rule.type == CommType::COMM_TYPE_UB_D2D) {
          status = TryMatchUbCtpD2DOnlyByGroup(local, remote, pairs, priority);
        } else {
          status = TryMatchSingle(local, remote, rule.protocol, rule.placement, rule.type, pairs);
        }
        break;
      default:
        break;
    }
    if (status == SUCCESS) {
      handler_type = rule.handler_type;
      const char *selected_protocol = (rule.protocol == nullptr) ? "ub_group" : rule.protocol;
      const char *selected_placement = (rule.placement == nullptr) ? "mixed" : rule.placement;
      HIXL_EVENT("EndpointMatcher selected link, handler:%s, reason:%s, protocol:%s, placement:%s, comm_type:%s",
                 HandlerTypeToString(rule.handler_type), rule.reason, selected_protocol, selected_placement,
                 CommTypeToString(rule.type));
      LogMatchedEndpoints(pairs, handler_type);
      return SUCCESS;
    }
    return FAILED;
  };

  const bool cross_instance = IsCrossInstance(local, remote);
  if (!cross_instance && AreUbCtpEndpointsAllDevice(local) && AreUbCtpEndpointsAllDevice(remote)) {
    if (try_match_rule(kSameInstanceUbCtpDeviceOnlyRule) == SUCCESS) {
      return SUCCESS;
    }
  }

  const MatchRule *rules = cross_instance ? kCrossInstanceRules : kSameInstanceRules;
  const size_t rule_count = cross_instance ? sizeof(kCrossInstanceRules) / sizeof(kCrossInstanceRules[0])
                                           : sizeof(kSameInstanceRules) / sizeof(kSameInstanceRules[0]);
  for (size_t i = 0; i < rule_count; ++i) {
    if (try_match_rule(rules[i]) == SUCCESS) {
      return SUCCESS;
    }
  }
  LogEndpointMatchFailure(local, remote, cross_instance);
  HIXL_LOGE(PARAM_INVALID, "Failed to find matched endpoints");
  return PARAM_INVALID;
}

void EndpointMatcher::LogMatchedEndpoints(const std::vector<HandlerCreateArgs::EndpointPair> &pairs,
                                          HandlerCreateArgs::HandlerType handler_type) {
  HIXL_EVENT("EndpointMatcher selected handler:%s, matched pairs:%zu", HandlerTypeToString(handler_type), pairs.size());
  for (size_t i = 0; i < pairs.size(); ++i) {
    const auto &pair = pairs[i];
    HIXL_EVENT("EndpointMatcher pair[%zu], comm_type:%s, local_endpoint:{%s}, remote_endpoint:{%s}", i,
               CommTypeToString(pair.type), pair.local.ToString().c_str(), pair.remote.ToString().c_str());
  }
}

Status EndpointMatcher::MatchEndpoints(const std::vector<EndpointConfig> &local,
                                       const std::vector<EndpointConfig> &remote,
                                       std::vector<HandlerCreateArgs::EndpointPair> &matched_pairs,
                                       HandlerCreateArgs::HandlerType &handler_type,
                                       const GlobalResourceConfig &global_config) {
  const auto &priorities = global_config.comm_resource_config.link_priority;
  const auto priority = priorities.has_value() && !priorities->empty() ? priorities->front() : LinkType::PEER2NET;
  HIXL_CHK_BOOL_RET_STATUS(!local.empty() && !remote.empty(), PARAM_INVALID,
                           "EndpointMatcher requires non-empty local and remote endpoints");
  const bool cross_instance = IsCrossInstance(local, remote);
  HIXL_EVENT("EndpointMatcher select start, cross_instance:%d, local_net_instance:%s, remote_net_instance:%s",
             static_cast<int32_t>(cross_instance), local[0].net_instance_id.c_str(), remote[0].net_instance_id.c_str());
  return TryMatchByPriority(local, remote, matched_pairs, handler_type, priority);
}

}  // namespace hixl
