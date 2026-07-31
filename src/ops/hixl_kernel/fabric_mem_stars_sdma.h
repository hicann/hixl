/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_OPS_HIXL_KERNEL_FABRIC_MEM_STARS_SDMA_H_
#define CANN_HIXL_SRC_OPS_HIXL_KERNEL_FABRIC_MEM_STARS_SDMA_H_

#include <cstdint>

#include "fabric_mem/fabric_mem_aicpu_types.h"
#include "fabric_mem_a3_rtsq.h"

namespace hixl {

// Layout-compatible with driver drvResIdKey. Do not include ascend_hal_base.h here:
// it defines MEM_HOST as a macro and breaks hixl::MemType in hixl_types.h.
struct FabricMemDrvResIdKey {
  uint32_t ruDevId = 0U;
  uint32_t tsId = 0U;
  uint32_t resType = 0U;
  uint32_t resId = 0U;
  uint32_t flag = 0U;
  uint32_t rsv[3]{};
};

struct FabricMemRtsqState {
  uint32_t device_id = 0U;
  uint32_t sq_id = 0U;
  uint32_t stream_id = 0U;
  uint32_t logic_cq_id = 0U;
  uint32_t next_task_id = 0U;
  uint64_t base_addr = 0U;
  uint32_t depth = 0U;
  uint32_t head = 0U;
  uint32_t tail = 0U;
};

// Leading fields of driver trs_logic_cqe / runtime rtLogicCqReport_t for logging.
struct __attribute__((packed)) FabricMemLogicCqeView {
  uint16_t stream_id = 0U;
  uint16_t task_id = 0U;
  uint32_t error_code = 0U;
  uint8_t error_type = 0U;
  uint8_t sqe_type = 0U;
  uint16_t sq_id = 0U;
  uint16_t sq_head = 0U;
};

union FabricMemRtsqEntry {
  FabricMemA3SdmaSqe sdma;
  FabricMemA3NotifySqe notify;
  uint8_t bytes[kFabricMemA3RtsqEntryBytes];
};

// Capacity must match kMaxRtsqEntriesPerPublish in fabric_mem_stars_sdma.cc.
struct FabricMemRtsqBatch {
  FabricMemRtsqEntry entries[128U]{};
  uint32_t count = 0U;
};

enum class FabricMemLogicCqRecvResult { kEmpty, kReports, kError };

// HIXL-owned A3 RTSQ submission wrapper. It mirrors HComm's A3 SDMA SQE
// construction while consuming only FabricMem VMM addresses and a HIXL-owned
// worker RTSQ passed by the dispatcher.
class FabricMemStarsSdma {
 public:
  static uint32_t Submit(const FabricMemAicpuKernelParam &param, const FabricMemAicpuTransferDesc *descs);

 private:
  static bool ValidateSubmitArgs(const FabricMemAicpuKernelParam &param, const FabricMemAicpuTransferDesc *descs);
  static bool BuildSubmitDeadline(uint64_t timeout_ms, uint64_t &deadline);
  static bool AppendAndPublishSdmaTasks(const FabricMemAicpuTransferDesc *descs, uint32_t desc_count,
                                        FabricMemRtsqState &state, FabricMemRtsqBatch &batch, uint64_t deadline);
  static bool EmitNotifyRecord(uint32_t notify_id, FabricMemRtsqState &state, FabricMemRtsqBatch &batch,
                               uint64_t deadline);

  static bool IsValidDescriptor(const FabricMemAicpuTransferDesc &desc);
  static bool AreValidDescriptors(const FabricMemAicpuTransferDesc *descs, uint32_t desc_count);
  static bool RestoreRtsqStream(const FabricMemRtsqState &state);
  static bool ResolveLocalDeviceId(uint32_t host_device_id, uint32_t &local_device_id);
  static uint64_t MonotonicNs();
  static bool DeadlineExceeded(uint64_t deadline_ns);
  static bool IsExceptionLogicCqe(const FabricMemLogicCqeView &report);
  static void LogLogicCqe(const FabricMemRtsqState &state, const FabricMemLogicCqeView &report, uint32_t idx,
                          uint32_t report_count, uint32_t total_reports, bool is_exception);
  static bool InspectLogicCqReports(const FabricMemRtsqState &state, const uint8_t *reports, uint32_t report_count,
                                    uint32_t total_reports);
  static FabricMemLogicCqRecvResult RecvLogicCqBatch(const FabricMemRtsqState &state, uint8_t *reports,
                                                     uint32_t &report_count);
  static bool PollLogicCqUntilEmpty(const FabricMemRtsqState &state, uint64_t deadline);
  static bool LoadRtsqQueueState(FabricMemRtsqState &state);
  static bool InitializeRtsq(const FabricMemAicpuKernelParam &param, FabricMemRtsqState &state);
  static bool HasRtsqCapacity(const FabricMemRtsqState &state, uint32_t count);
  static bool EnsureRtsqCapacity(FabricMemRtsqState &state, uint32_t count, uint64_t deadline);
  static bool BuildSdmaSqe(uint64_t source, uint64_t destination, uint64_t size, uint32_t task_id,
                           const FabricMemRtsqState &state, FabricMemA3SdmaSqe &sqe);
  static bool BuildNotifySqe(uint32_t notify_id, uint32_t task_id, const FabricMemRtsqState &state,
                             FabricMemA3NotifySqe &sqe);
  static bool CopySqeBatchToRing(FabricMemRtsqState &state, const FabricMemRtsqBatch &batch);
  static bool CommitRtsqTail(FabricMemRtsqState &state, uint32_t new_tail, uint32_t batch_count);
  static bool PublishRtsqBatch(FabricMemRtsqState &state, FabricMemRtsqBatch &batch, uint64_t deadline);
  static bool AppendDescriptorTasks(const FabricMemAicpuTransferDesc &desc, FabricMemRtsqState &state,
                                    FabricMemRtsqBatch &batch, uint64_t deadline);
  static bool AppendNotifyTask(uint32_t notify_id, FabricMemRtsqState &state, FabricMemRtsqBatch &batch,
                               uint64_t deadline);
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_OPS_HIXL_KERNEL_FABRIC_MEM_STARS_SDMA_H_
