/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_INC_EXTERNAL_HIXL_HIXL_TYPES_H_
#define CANN_HIXL_INC_EXTERNAL_HIXL_HIXL_TYPES_H_

#include <cstdint>
#include "external/ge_common/ge_api_error_codes.h"

#ifdef FUNC_VISIBILITY
#define ASCEND_FUNC_VISIBILITY __attribute__((visibility("default")))
#else
#define ASCEND_FUNC_VISIBILITY
#endif

namespace hixl {
using Status = uint32_t;
using AscendString = ge::AscendString;
using TransferReq = void *;

// options
constexpr const char OPTION_RDMA_TRAFFIC_CLASS[] = "RdmaTrafficClass";
constexpr const char OPTION_RDMA_SERVICE_LEVEL[] = "RdmaServiceLevel";
constexpr const char OPTION_BUFFER_POOL[] = "BufferPool";
constexpr const char OPTION_GLOBAL_RESOURCE_CONFIG[] = "GlobalResourceConfig";
 
// status codes
constexpr Status SUCCESS = 0U;
constexpr Status PARAM_INVALID = 103900U;
constexpr Status TIMEOUT = 103901U;
constexpr Status NOT_CONNECTED = 103902U;
constexpr Status ALREADY_CONNECTED = 103903U;
constexpr Status NOTIFY_FAILED = 103904U;
constexpr Status UNSUPPORTED = 103905U;
constexpr Status FAILED = 503900U;
constexpr Status RESOURCE_EXHAUSTED = 203900U;

using MemHandle = void *;

enum MemType {
  MEM_DEVICE,
  MEM_HOST
};

enum TransferOp {
  READ,
  WRITE
};

struct MemDesc {
  uintptr_t addr;
  size_t len;
  uint8_t reserved[128] = {};
};

struct TransferOpDesc {
  uintptr_t local_addr;
  uintptr_t remote_addr;
  size_t len;
};
enum class TransferStatus {
  WAITING,
  COMPLETED,
  TIMEOUT,
  FAILED
};

struct TransferArgs{
  uint8_t reserved[128] = {};
};

struct NotifyDesc {
  AscendString name;
  AscendString notify_msg;
};
}  // namespace hixl

#endif  // CANN_HIXL_INC_EXTERNAL_HIXL_HIXL_TYPES_H_
