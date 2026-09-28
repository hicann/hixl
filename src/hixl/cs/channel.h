/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_CS_CHANNEL_H_
#define CANN_HIXL_SRC_HIXL_CS_CHANNEL_H_

#include <memory>
#include "hccl/hccl_types.h"
#include "hcomm/hcomm_res_defs.h"
#include "cs/hixl_cs.h"
#include "common/hixl_inner_types.h"
#include "hixl/hixl_types.h"
#include "common/transfer_config.h"

namespace hixl {

class Endpoint;

enum class ChannelType : int32_t {
  kClient = HCOMM_SOCKET_ROLE_CLIENT,
  kServer = HCOMM_SOCKET_ROLE_SERVER,
};

struct ChannelDesc {
  EndpointDesc remote_endpoint;
  uint8_t tc;
  uint8_t sl;
  uint32_t retry_cnt{0U};
  uint32_t retry_interval{0U};
  ChannelType channel_type{ChannelType::kClient};
  uint64_t channel_index{0UL};
  uint8_t qos{kQosUnset};
  uint32_t max_transfer_count_per_batch{kDefaultMaxTransferCountPerBatch};
  bool configurable_transfer_supported{false};
};

// Protocol-agnostic integration layer for a transport channel. The shared state machine (create, wait
// until connected, destroy) lives here; HcommChannel and UbMemChannel fill in the protocol specifics.
// One Channel is exactly one transport channel, so the per-instance hooks below operate on the channel
// this object owns and never take its handle: the concrete implementations read channel_handle_ or
// GetHandle(). The key hooks are static because they must work before any transport channel exists.
class Channel {
 public:
  Channel() = default;
  virtual ~Channel() = default;

  // Creates the transport channel on endpoint and waits until it reports connected. On failure the
  // channel is torn down again so the caller never sees a half-created handle.
  Status Create(Endpoint &endpoint, HcommChannelDesc &ch_desc, CommEngine engine, uint32_t timeout_ms);
  Status Destroy();

  ChannelHandle GetHandle() const {
    return channel_handle_;
  }

 protected:
  // Protocol-specific channel lifecycle. Endpoint::CreateChannel builds the concrete type and then
  // drives the shared Create above. ChannelCreate is the one hook that does take a handle, because the
  // handle does not exist yet: it is what the call produces.
  virtual HcclResult ChannelCreate(EndpointHandle endpoint_handle, CommEngine engine, HcommChannelDesc &ch_desc,
                                   ChannelHandle &out_handle) = 0;
  virtual HcclResult ChannelDestroy() = 0;
  virtual HcclResult ChannelGetStatus(int32_t &status) = 0;

 private:
  static Status WaitChannelConnected(Channel &channel, uint32_t timeout_ms);

  // This channel's transport handle, assigned once by Create and cleared on teardown.
  ChannelHandle channel_handle_{0UL};
};

using ChannelPtr = std::shared_ptr<Channel>;

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_CS_CHANNEL_H_
