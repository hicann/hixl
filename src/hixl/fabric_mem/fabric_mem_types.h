/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_FABRIC_MEM_FABRIC_MEM_TYPES_H_
#define CANN_HIXL_SRC_HIXL_FABRIC_MEM_FABRIC_MEM_TYPES_H_

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "acl/acl.h"

namespace hixl {
struct FabricMemTransferStatisticInfo;
struct VaInfo {
  uintptr_t va_addr = 0;
  size_t len = 0;
};

struct ShareHandleInfo {
  uintptr_t va_addr = 0;
  size_t len = 0;
  aclrtMemFabricHandle share_handle{};
  aclrtDrvMemHandle imported_handle = nullptr;
  uintptr_t imported_va = 0;
  bool is_retained = false;
};

using AsyncResource = std::pair<aclrtStream, aclrtEvent>;

struct AsyncRecord {
  std::vector<AsyncResource> async_resources;
  std::chrono::steady_clock::time_point transfer_start;
  std::chrono::steady_clock::time_point real_copy_start;
  uint64_t transfer_bytes = 0UL;
  uint64_t op_desc_count = 0UL;
};

struct FabricMemTransferContext {
  std::string channel_id;
  std::string statistic_channel_id;
  std::unordered_map<uintptr_t, VaInfo> remote_va_to_old_va;
  FabricMemTransferStatisticInfo *stat_info = nullptr;
};
}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_FABRIC_MEM_FABRIC_MEM_TYPES_H_
