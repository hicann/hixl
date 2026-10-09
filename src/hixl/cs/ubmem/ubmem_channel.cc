/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "cs/ubmem/ubmem_channel.h"

#include <atomic>

#include "common/hixl_log.h"

namespace hixl {
namespace {
constexpr int32_t kChannelConnectedStatus = 0;
std::atomic<uint64_t> g_next_ubmem_channel_id{1UL};
}  // namespace

HcclResult UbMemChannel::ChannelCreate(EndpointHandle endpoint_handle, CommEngine engine, HcommChannelDesc &ch_desc,
                                       ChannelHandle &out_handle) {
  (void)endpoint_handle;
  (void)engine;
  (void)ch_desc;
  out_handle = g_next_ubmem_channel_id.fetch_add(1U, std::memory_order_relaxed);
  HIXL_LOGI("[UbMemChannel] ChannelCreate success, channel=%lu", out_handle);
  return HCCL_SUCCESS;
}

HcclResult UbMemChannel::ChannelDestroy() {
  return HCCL_SUCCESS;
}

HcclResult UbMemChannel::ChannelGetStatus(int32_t &status) const {
  status = kChannelConnectedStatus;
  return HCCL_SUCCESS;
}

}  // namespace hixl
