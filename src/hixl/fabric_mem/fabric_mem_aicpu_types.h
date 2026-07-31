/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_FABRIC_MEM_FABRIC_MEM_AICPU_TYPES_H_
#define CANN_HIXL_SRC_HIXL_FABRIC_MEM_FABRIC_MEM_AICPU_TYPES_H_

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace hixl {

// Upper bound on the RTSQ entries the host lets the AICPU kernel queue between two NotifyRecord
// waits. The host splits every transfer so that one descriptor is exactly one SDMA SQE, then inserts
// a WaitAndResetNotify once this many entries have been queued. Publishing therefore never has to
// wait for RTSQ capacity, and a full queue means the model was violated rather than back-pressure.
constexpr uint32_t kFabricMemMaxInFlightRtsqTasks = 1920U;
// Shallowest RTSQ that can still honour that budget: the in-flight tasks, the trailing NotifyRecord
// SQE, and the one entry a ring keeps free to tell full apart from empty.
constexpr uint32_t kFabricMemMinRtsqDepth = kFabricMemMaxInFlightRtsqTasks + 2U;

enum class FabricMemAicpuTransferDirection : uint32_t {
  kRead = 0U,
  kWrite = 1U,
};

// This ABI is shared by the host dispatcher and the standalone FabricMem AICPU
// binary. It deliberately carries only VMM-resolved virtual addresses, transfer length, and
// direction; no communication-channel objects or state may cross this boundary.
struct FabricMemAicpuTransferDesc {
  uint64_t src_addr = 0U;
  uint64_t dst_addr = 0U;
  uint64_t length = 0U;
};

struct FabricMemAicpuKernelParam {
  uint64_t desc_addr = 0U;
  uint32_t desc_count = 0U;
  // Host/physical device ID from aclrtGetPhyDevIdByLogicDevId. The AICPU kernel converts it to the
  // local driver id via drvGetLocalDevIDByHostDevID before calling halSqCqQuery/halSqCqConfig.
  uint32_t device_id = 0U;
  // HIXL-owned worker RTSQ metadata. The AICPU kernel emits SDMA SQEs into
  // this queue; it must not append them to its control/AICPU launch stream.
  uint32_t rtsq_id = 0U;
  uint32_t rtsq_stream_id = 0U;
  uint32_t rtsq_task_id = 0U;
  // Logical CQ id paired with the worker RTSQ. The AICPU kernel polls this queue
  // until empty before NotifyRecord and before declaring the RTSQ full; abnormal
  // CQEs (typical when wr_cqe=0) fail the kernel.
  uint32_t rtsq_logic_cq_id = 0U;
  uint32_t direction = 0U;
  // Maximum milliseconds the AICPU kernel may spend draining the logic CQ before
  // returning failure. Passed from the host so the kernel respects the same
  // deadline the caller requested instead of a hardcoded value.
  uint32_t timeout_ms = 0U;
  // Runtime notify metadata for an optional trailing A3 NotifyRecord SQE.
  // emit_notify_record is a fixed-width ABI field; bool must not cross the
  // independently packaged host/AICPU binary boundary.
  uint32_t notify_id = 0U;
  uint32_t emit_notify_record = 0U;
  // Optional device pointer to a uint32_t result written by the AICPU kernel
  // (0 success / non-zero failure) so the host can observe SDMA submit errors.
  uint64_t status_addr = 0U;
  // Synthetic TransferContextManager key (host sets from slot notify). Kernel locks
  // this context for the duration of Submit so disconnect Sync DELETE can try_lock.
  uint64_t transfer_ctx_key = 0U;
};

// Device buffers allocated on the host side must remain alive until the stream
// has completed (or has been aborted). kernel_args_buffer holds one
// FabricMemAicpuKernelParam per launch for aclrtLaunchKernelV2.
struct FabricMemAicpuRequestResource {
  void *descriptor_buffer = nullptr;
  size_t descriptor_buffer_size = 0U;
  void *status_buffer = nullptr;
  size_t status_count = 0U;
  void *kernel_args_buffer = nullptr;
  size_t kernel_args_count = 0U;
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_FABRIC_MEM_FABRIC_MEM_AICPU_TYPES_H_
