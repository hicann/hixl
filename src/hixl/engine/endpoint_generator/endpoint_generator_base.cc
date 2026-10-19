/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "endpoint_generator_base.h"

#include <algorithm>
#include <set>
#include <string>

#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "endpoint_generator_v1.h"
#include "endpoint_generator_v2.h"

namespace hixl {
namespace {
Status ValidateFullUbCtpEndpoints(const std::vector<std::string> &protocol_desc,
                                  const std::vector<EndpointConfig> &endpoint_list) {
  if (std::find(protocol_desc.begin(), protocol_desc.end(), kProtocolUbCtp) == protocol_desc.end()) {
    return SUCCESS;
  }
  bool has_device = false;
  bool has_host = false;
  for (const auto &endpoint : endpoint_list) {
    if (endpoint.protocol != kProtocolUbCtp) {
      continue;
    }
    has_device = has_device || endpoint.placement == kPlacementDevice;
    has_host = has_host || endpoint.placement == kPlacementHost;
  }
  HIXL_CHK_BOOL_RET_STATUS(has_device && has_host, PARAM_INVALID,
                           "protocol_desc=ub_ctp requires both Device and Host UB CTP endpoints");
  return SUCCESS;
}
}  // namespace

std::unique_ptr<EndpointGeneratorBase> EndpointGeneratorBase::Create(SocType soc_type) {
  if (soc_type == SocType::kGen1 || soc_type == SocType::kGen2) {
    return std::make_unique<EndpointGeneratorV1>();
  }
  return std::make_unique<EndpointGeneratorV2>();
}

std::string EndpointGeneratorBase::BuildProtocolDescKey(const std::string &protocol, const std::string &placement) {
  return protocol + ":" + placement;
}

bool EndpointGeneratorBase::IsSupportedProtocolDesc(const std::string &protocol, const std::string &placement) {
  static const std::set<std::string> kSupportedProtocols = {kProtocolRoce, kProtocolHccs,  kProtocolUbCtp,
                                                            kProtocolUboe, kProtocolUbRtp, kProtocolUbmem};
  static const std::set<std::string> kSupportedPlacements = {kPlacementHost, kPlacementDevice};
  if (kSupportedProtocols.find(protocol) == kSupportedProtocols.end() ||
      kSupportedPlacements.find(placement) == kSupportedPlacements.end()) {
    return false;
  }
  if (protocol == kProtocolUbmem) {
    return false;
  }
  if ((protocol == kProtocolUboe || protocol == kProtocolUbRtp) && placement != kPlacementDevice) {
    return false;
  }
  return true;
}

Status EndpointGeneratorBase::ParseProtocolDesc(const std::vector<std::string> &protocol_desc,
                                                std::set<std::string> &desc_set) {
  desc_set.clear();
  for (const auto &desc : protocol_desc) {
    if (desc == kProtocolUbCtp) {
      desc_set.insert(BuildProtocolDescKey(kProtocolUbCtp, kPlacementDevice));
      desc_set.insert(BuildProtocolDescKey(kProtocolUbCtp, kPlacementHost));
      continue;
    }
    if (desc == kProtocolUbmem) {
      desc_set.insert(kProtocolUbmem);
      continue;
    }
    const auto split_pos = desc.find(':');
    HIXL_CHK_BOOL_RET_STATUS(split_pos != std::string::npos && split_pos > 0U && split_pos + 1U < desc.size() &&
                                 desc.find(':', split_pos + 1U) == std::string::npos,
                             PARAM_INVALID, "Invalid protocol_desc:%s, expected ub_ctp, ubmem or protocol:placement",
                             desc.c_str());
    const std::string protocol = desc.substr(0, split_pos);
    const std::string placement = desc.substr(split_pos + 1U);
    HIXL_CHK_BOOL_RET_STATUS(IsSupportedProtocolDesc(protocol, placement), PARAM_INVALID,
                             "Unsupported protocol_desc:%s", desc.c_str());
    desc_set.insert(BuildProtocolDescKey(protocol, placement));
  }
  return SUCCESS;
}

Status EndpointGeneratorBase::FilterEndpointsByProtocolDescList(const std::vector<std::string> &protocol_desc,
                                                                std::vector<EndpointConfig> &endpoint_list) {
  if (protocol_desc.empty()) {
    return SUCCESS;
  }
  std::set<std::string> desc_set;
  HIXL_CHK_STATUS_RET(ParseProtocolDesc(protocol_desc, desc_set), "ParseProtocolDesc failed");
  if (desc_set.empty()) {
    return SUCCESS;
  }
  std::vector<EndpointConfig> filtered;
  filtered.reserve(endpoint_list.size());
  for (auto &ep : endpoint_list) {
    const bool keep = (ep.protocol == kProtocolUbmem)
                          ? desc_set.count(kProtocolUbmem) > 0
                          : desc_set.count(BuildProtocolDescKey(ep.protocol, ep.placement)) > 0;
    if (keep) {
      filtered.emplace_back(std::move(ep));
    }
  }
  endpoint_list = std::move(filtered);
  HIXL_CHK_BOOL_RET_STATUS(!endpoint_list.empty(), PARAM_INVALID,
                           "endpoint_list is empty after filtering by protocol_desc");
  HIXL_CHK_STATUS_RET(ValidateFullUbCtpEndpoints(protocol_desc, endpoint_list), "ValidateFullUbCtpEndpoints failed");
  return SUCCESS;
}

}  // namespace hixl
