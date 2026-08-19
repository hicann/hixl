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
#include "proxy/hcomm_proxy.h"

namespace hixl {
namespace {
constexpr uint32_t kChannelListNum = 1U;
// HcommChannelGetStatus 返回的通道建链状态码：0=建链完成，1=建链进行中，
// 2=建链失败，3=建链超时。2/3 为终态失败，检测到时应立即返回不再等待。
constexpr int32_t kChannelConnectedStatus = 0;
constexpr int32_t kChannelConnectingStatus = 1;
constexpr int32_t kChannelUnknownStatus = -1;
constexpr auto kChannelStatusPollInterval = std::chrono::microseconds(1);

Status WaitChannelConnected(ChannelHandle channel_handle, uint32_t timeout_ms) {
  const ChannelHandle ch_list[kChannelListNum] = {channel_handle};
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (true) {
    int32_t status = kChannelUnknownStatus;
    HIXL_CHK_HCCL_RET(HcommProxy::ChannelGetStatus(ch_list, kChannelListNum, &status));
    if (status == kChannelConnectedStatus) {
      return SUCCESS;
    }
    HIXL_CHK_BOOL_RET_STATUS(status == kChannelConnectingStatus, FAILED,
                             "Wait channel connected failed, handle:%lu, status:%d", channel_handle, status);
    HIXL_CHK_BOOL_RET_STATUS(std::chrono::steady_clock::now() < deadline, TIMEOUT,
                             "Wait channel connected timed out, handle:%lu, status:%d, timeout:%u ms", channel_handle,
                             status, timeout_ms);
    std::this_thread::sleep_for(kChannelStatusPollInterval);
  }
}
}  // namespace

Status Channel::Create(EndpointHandle ep_handle, HcommChannelDesc &ch_desc, CommEngine engine, uint32_t timeout_ms) {
  HIXL_CHK_BOOL_RET_STATUS(ep_handle != nullptr, PARAM_INVALID, "Channel::Create called with null endpoint handle");
  ChannelHandle ch_list[kChannelListNum] = {};
  HIXL_LOGI("HcommChannelCreate start, protocol=%s, devPhyId=%u, ep_handle=%p",
            ProtocolToString(ch_desc.remoteEndpoint.protocol).c_str(), ch_desc.remoteEndpoint.loc.device.devPhyId,
            ep_handle);
  HIXL_CHK_HCCL_RET(HcommProxy::ChannelCreate(ep_handle, engine, &ch_desc, kChannelListNum, ch_list));
  channel_handle_ = ch_list[0];
  Status ret = WaitChannelConnected(channel_handle_, timeout_ms);
  if (ret != SUCCESS) {
    HcclResult destroy_ret = HcommProxy::ChannelDestroy(ch_list, kChannelListNum);
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

ChannelHandle Channel::GetChannelHandle() const {
  return channel_handle_;
}

Status Channel::Destroy() const {
  const ChannelHandle ch_list[1] = {channel_handle_};
  HIXL_CHK_HCCL_RET(HcommProxy::ChannelDestroy(ch_list, 1U));

  HIXL_LOGI("Channel::Destroy success, handle=%lu", channel_handle_);
  return SUCCESS;
}

}  // namespace hixl
