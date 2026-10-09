/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm_channel.h"

#include "proxy/hcomm_proxy.h"

namespace hixl {
namespace {
// HcommProxy works on lists of channels; one Channel object always carries exactly one.
constexpr uint32_t kSingleChannel = 1U;
}  // namespace

HcclResult HcommChannel::ChannelCreate(EndpointHandle endpoint_handle, CommEngine engine, HcommChannelDesc &ch_desc,
                                       ChannelHandle &out_handle) {
  return HcommProxy::ChannelCreate(endpoint_handle, engine, &ch_desc, kSingleChannel, &out_handle);
}

HcclResult HcommChannel::ChannelDestroy() {
  const ChannelHandle handle = GetHandle();
  return HcommProxy::ChannelDestroy(&handle, kSingleChannel);
}

HcclResult HcommChannel::ChannelGetStatus(int32_t &status) const {
  const ChannelHandle handle = GetHandle();
  return HcommProxy::ChannelGetStatus(&handle, kSingleChannel, &status);
}

}  // namespace hixl
