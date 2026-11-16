/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_CS_HCOMM_CHANNEL_H_
#define CANN_HIXL_SRC_HIXL_CS_HCOMM_CHANNEL_H_

#include <string>

#include "cs/channel.h"

namespace hixl {

// Generic HCOMM-backed channel: the RoCE / HCCS / UB group path. Owns the shared HCOMM lifecycle
// (channel descriptor building, create, wait-until-connected, destroy) plus the memory registration used
// by every HCOMM endpoint. Host/device and protocol specifics live in the subclasses.
class HcommChannel : public Channel {
 public:
  explicit HcommChannel(Endpoint &local_endpoint);
  ~HcommChannel() override = default;

  Status Initialize(const ChannelContext &ctx) override;
  Status Connect(const ChannelDesc &channel_desc, uint32_t timeout_ms) override;
  Status Disconnect() override;
  Status Destroy() override;

  Status RegMem(const char *mem_tag, const CommMem *mem, MemHandle *mem_handle) override;
  Status UnregMem(MemHandle mem_handle) override;
  Status SetRemoteMemInfo(const CommMem &trans_flag) override;

 protected:
  Endpoint &local_endpoint_;
  // Peer-registered builtin completion flag, resolved by the client from the remote endpoint location.
  CommMem remote_trans_flag_{};

 private:
  Status BuildChannelDesc(const ChannelDesc &channel_desc, HcommChannelDesc &ch_desc, std::string &channel_name) const;
  Status WaitChannelConnected(uint32_t timeout_ms) const;
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_CS_HCOMM_CHANNEL_H_
