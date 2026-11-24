/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_CS_HOST_ROCE_HANDSHAKE_H_
#define CANN_HIXL_SRC_HIXL_CS_HOST_ROCE_HANDSHAKE_H_

#include <cstddef>
#include <cstdint>
#include <vector>
#include "cs/host_roce/host_roce_verbs.h"

namespace hixl {
enum class HostRoceHandshakeStep : uint32_t {
  kPrepare = 1U,
  kNegotiate = 2U,
  kReady = 3U,
};

struct HostRoceHandshake {
  HostRoceHandshakeStep step{HostRoceHandshakeStep::kPrepare};
  uint64_t index{0};
  HostRocePeer peer;
};

std::vector<uint8_t> EncodeHostRoceHandshake(const HostRoceHandshake &value);
Status DecodeHostRoceHandshake(const void *data, size_t size, HostRoceHandshake &value);
}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_CS_HOST_ROCE_HANDSHAKE_H_
