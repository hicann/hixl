/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_CS_CHANNEL_FACTORY_H_
#define CANN_HIXL_SRC_HIXL_CS_CHANNEL_FACTORY_H_

#include "cs/channel.h"

namespace hixl {

class Endpoint;

// Picks the concrete channel from the local endpoint (protocol + locality). UB_MEM is its own family;
// every other protocol is an HCOMM channel, split by locality and protocol capability.
class ChannelFactory {
 public:
  ChannelFactory() = delete;

  static ChannelPtr Create(Endpoint &local_endpoint);
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_CS_CHANNEL_FACTORY_H_
