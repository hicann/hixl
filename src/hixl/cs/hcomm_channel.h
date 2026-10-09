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

#include "cs/channel.h"

namespace hixl {

// Hcomm transport channel: every operation forwards to HcommProxy on this channel's own handle.
class HcommChannel : public Channel {
 public:
  HcommChannel() = default;
  ~HcommChannel() override = default;

 protected:
  HcclResult ChannelCreate(EndpointHandle endpoint_handle, CommEngine engine, HcommChannelDesc &ch_desc,
                           ChannelHandle &out_handle) override;
  HcclResult ChannelDestroy() override;
  HcclResult ChannelGetStatus(int32_t &status) const override;
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_CS_HCOMM_CHANNEL_H_
