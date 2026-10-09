/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "channel.h"

#include <chrono>
#include <thread>

#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/hixl_utils.h"
#include "endpoint.h"

namespace hixl {
namespace {
constexpr int32_t kChannelConnectedStatus = 0;
constexpr int32_t kChannelConnectingStatus = 1;
constexpr int32_t kChannelUnknownStatus = -1;
constexpr auto kChannelStatusPollInterval = std::chrono::microseconds(1);
}  // namespace

Status Channel::Create(Endpoint &endpoint, HcommChannelDesc &ch_desc, CommEngine engine, uint32_t timeout_ms) {
  const EndpointHandle ep_handle = endpoint.GetHandle();
  HIXL_CHK_BOOL_RET_STATUS(ep_handle != nullptr, PARAM_INVALID, "Channel::Create called with null endpoint handle");
  HIXL_LOGI("ChannelCreate start, protocol=%s, devPhyId=%u, ep_handle=%p",
            ProtocolToString(ch_desc.remoteEndpoint.protocol).c_str(), ch_desc.remoteEndpoint.loc.device.devPhyId,
            ep_handle);
  HIXL_CHK_HCCL_RET(ChannelCreate(ep_handle, engine, ch_desc, channel_handle_));
  const Status ret = WaitChannelConnected(*this, timeout_ms);
  if (ret != SUCCESS) {
    const HcclResult destroy_ret = ChannelDestroy();
    if (destroy_ret != HCCL_SUCCESS) {
      HIXL_LOGW("Destroy channel after create failure failed, handle=%lu, ret=0x%X", channel_handle_,
                static_cast<uint32_t>(destroy_ret));
    }
    channel_handle_ = 0UL;
    return ret;
  }
  HIXL_LOGI("Channel::Create success, handle=%lu", channel_handle_);
  return SUCCESS;
}

Status Channel::Destroy() {
  if (channel_handle_ == 0UL) {
    HIXL_LOGI("Channel::Destroy skip, handle=%lu", channel_handle_);
    return SUCCESS;
  }
  HIXL_CHK_HCCL_RET(ChannelDestroy());
  const ChannelHandle destroyed_handle = channel_handle_;
  channel_handle_ = 0UL;
  HIXL_LOGI("Channel::Destroy success, handle=%lu", destroyed_handle);
  return SUCCESS;
}

Status Channel::WaitChannelConnected(const Channel &channel, uint32_t timeout_ms) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (true) {
    int32_t status = kChannelUnknownStatus;
    HIXL_CHK_HCCL_RET(channel.ChannelGetStatus(status));
    if (status == kChannelConnectedStatus) {
      return SUCCESS;
    }
    HIXL_CHK_BOOL_RET_STATUS(status == kChannelConnectingStatus, FAILED,
                             "Wait channel connected failed, handle:%lu, status:%d", channel.GetHandle(), status);
    HIXL_CHK_BOOL_RET_STATUS(std::chrono::steady_clock::now() < deadline, TIMEOUT,
                             "Wait channel connected timed out, handle:%lu, status:%d, timeout:%u ms",
                             channel.GetHandle(), status, timeout_ms);
    std::this_thread::sleep_for(kChannelStatusPollInterval);
  }
}

}  // namespace hixl
