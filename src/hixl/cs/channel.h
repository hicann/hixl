/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_CHANNEL_H_
#define CANN_HIXL_SRC_CHANNEL_H_

#include <memory>
#include "common/hixl_cs.h"
#include "hixl/hixl_types.h"
#include "common/hixl_checker.h"
#include "common/hixl_log.h"

namespace hixl {
class Channel {
 public:
  Channel() = default;
  ~Channel() = default;

  Status Create(EndpointHandle ep_handle, HcommChannelDesc &ch_desc, CommEngine engine);
  ChannelHandle GetChannelHandle() const;
  static Status GetStatus(ChannelHandle channel_handle, int32_t *status_out);

  Status Destroy() const;

  ChannelHandle GetHandle() const {
    return channel_handle_;
  }

 private:
  ChannelHandle channel_handle_{0UL};
};

using ChannelPtr = std::shared_ptr<Channel>;

}  // namespace hixl

#endif  // CANN_HIXL_SRC_CHANNEL_H_
