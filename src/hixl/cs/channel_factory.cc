/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "channel_factory.h"

#include "common/hixl_log.h"
#include "common/hixl_utils.h"
#include "cs/host_roce/host_roce_channel.h"
#include "cs/ubmem/ubmem_channel.h"
#include "endpoint.h"
#include "hcomm_device_channel.h"
#include "hcomm_host_channel.h"

namespace hixl {

ChannelPtr ChannelFactory::Create(Endpoint &local_endpoint) {
  const EndpointDesc &endpoint = local_endpoint.GetEndpoint();
  if (local_endpoint.Capabilities().backend == BackendKind::kHixl) {
    if (IsUbMemProtocol(endpoint.protocol)) {
      return MakeShared<UbMemChannel>(local_endpoint);
    }
    if (IsHostRoce(endpoint)) {
      return MakeShared<HostRoceChannel>(local_endpoint);
    }
  }
  // Remaining protocols use the default HCOMM backend, split by locality.
  if (endpoint.loc.locType == ENDPOINT_LOC_TYPE_HOST && !IsUbMemProtocol(endpoint.protocol)) {
    return MakeShared<HcommHostChannel>(local_endpoint);
  }
  switch (endpoint.protocol) {
    case COMM_PROTOCOL_HCCS:
      return MakeShared<HccsDeviceChannel>(local_endpoint);
    case COMM_PROTOCOL_ROCE:
      return MakeShared<RoceDeviceChannel>(local_endpoint);
    case COMM_PROTOCOL_UBC_CTP:
    case COMM_PROTOCOL_UBC_TP:
    case COMM_PROTOCOL_UBOE:
    case COMM_PROTOCOL_UBG:
      return MakeShared<UbDeviceChannel>(local_endpoint);
    case COMM_PROTOCOL_UB_MEM:
    case COMM_PROTOCOL_RESERVED:
    case COMM_PROTOCOL_PCIE:
    case COMM_PROTOCOL_SIO:
    case COMM_PROTOCOL_HCCS_ONLY:
      break;
    default:
      break;
  }
  HIXL_LOGE(PARAM_INVALID, "[ChannelFactory] Unsupported device protocol=%d", static_cast<int32_t>(endpoint.protocol));
  return nullptr;
}

}  // namespace hixl
