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
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "acl/acl.h"
#include "fabric_mem/fabric_mem_aicpu_types.h"
#include "hixl/hixl_types.h"

namespace hixl {
constexpr uint64_t kMillisToMicros = 1000UL;

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
  MemType mem_type = MEM_DEVICE;
};

struct AsyncSlot {
  aclrtContext ctx = nullptr;
  // Control streams. With AICPU unfold there is exactly one control stream;
  // AICPU writes SDMA SQEs to the paired worker RTSQ.
  std::vector<aclrtStream> streams;
  std::vector<aclrtStream> sdma_streams;
  // One device-only notify per control/worker pair bridges completion from
  // the worker RTSQ back to its control stream (size 1 for AICPU unfold).
  std::vector<aclrtNotify> notifies;
  std::vector<uint32_t> notify_ids;
  std::vector<void *> host_flags;
  // Synthetic key for AICPU TransferContextManager (no Hcomm thread). Set from notifies[0]
  // when the bound slot is registered; used by Sync ADD/DELETE and kernel Submit locking.
  uint64_t transfer_ctx_key = 0U;
  // When true, host_flags are owned by this transfer (AICPU async multiplexing) and must be freed
  // by the transfer path; pool entry host_flags stay untouched.
  bool owns_host_flags = false;
  bool available = true;
  bool has_aicpu_unfold = false;
};

struct AsyncRecord {
  AsyncSlot slot;
  FabricMemAicpuRequestResource aicpu_resource;
  std::chrono::steady_clock::time_point transfer_start;
  std::chrono::steady_clock::time_point real_copy_start;
  uint64_t transfer_bytes = 0UL;
  uint64_t op_desc_count = 0UL;
  std::string channel_id;
  std::string statistic_channel_id;
  std::shared_ptr<FabricMemTransferStatisticInfo> stat_info;
  TransferOp op_type = READ;
  uint64_t prof_start_time{0U};
};

// One in-flight request bound to a slot that is being aborted. Its descriptor / status buffers may
// still be read by a running AICPU kernel, so the abort flow frees them only after the kernel exits.
struct FabricMemAicpuPendingRequest {
  AsyncSlot *slot = nullptr;
  FabricMemAicpuRequestResource *resource = nullptr;
};

struct AsyncTransferPollInfo {
  TransferOp op_type = READ;
  uint64_t prof_start_time{0U};
  std::string channel_id;
};

struct FabricMemTransferContext {
  std::string channel_id;
  std::string statistic_channel_id;
  std::unordered_map<uintptr_t, VaInfo> remote_va_to_old_va;
  std::shared_ptr<FabricMemTransferStatisticInfo> stat_info;
};
}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_FABRIC_MEM_FABRIC_MEM_TYPES_H_
