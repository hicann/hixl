/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "fabric_mem_stars_sdma.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <ctime>
#include <limits>

#include "ascend_hal_error.h"
#include "common/hixl_log.h"
#include "fabric_mem_rtsq_query.h"
#include "hal_pkg/trs_pkg.h"
#include "hixl/hixl_types.h"
#include "securec.h"

namespace hixl {
namespace {
constexpr uint32_t kSuccess = 0U;
constexpr uint32_t kFailed = 1U;
// Match HComm's HCCL_PER_LAUNCH_SQE_CNT: RTSQ entries per tail update, not host desc count.
// Must match FabricMemRtsqBatch::entries capacity in fabric_mem_stars_sdma.h.
constexpr uint32_t kMaxRtsqEntriesPerPublish = 128U;
constexpr uint64_t kRtsqPollDefaultTimeoutSec = 60U;
constexpr uint64_t kNsPerSecond = 1000000000ULL;
constexpr uint64_t kNsPerMillisecond = 1000000ULL;
constexpr uint64_t kMaxSdmaTransferBytes = 0xFFFFFFFFULL;
constexpr uint32_t kLogicCqReportCount = 32U;
constexpr size_t kLogicCqeBytes = 32U;
constexpr uint8_t kStarsExistErrorMask = 0x3FU;
constexpr uint32_t kLogicCqAnyTaskId = 0xFFFFU;
constexpr uint32_t kLogicCqRecvMatchVersion = 1U;

extern "C" {
drvError_t __attribute__((weak)) halSqCqQuery(uint32_t dev_id, struct halSqCqQueryInfo *info);
drvError_t __attribute__((weak)) halSqCqConfig(uint32_t dev_id, struct halSqCqConfigInfo *info);
drvError_t __attribute__((weak)) drvGetLocalDevIDByHostDevID(uint32_t host_dev_id, uint32_t *local_dev_id);
drvError_t __attribute__((weak)) halCqReportRecv(uint32_t dev_id, struct halReportRecvInfo *info);
drvError_t __attribute__((weak)) halResourceIdRestore(FabricMemDrvResIdKey *info);
}

bool QueryRtsqValues(uint32_t device_id, uint32_t sq_id, drvSqCqPropType_t property, uint32_t &value_low,
                     uint32_t &value_high) {
  return fabric_mem_rtsq::QueryRtsqValues(device_id, sq_id, property, value_low, value_high, halSqCqQuery);
}

bool QueryRtsqValue(uint32_t device_id, uint32_t sq_id, drvSqCqPropType_t property, uint32_t &value) {
  return fabric_mem_rtsq::QueryRtsqValue(device_id, sq_id, property, value, halSqCqQuery);
}
}  // namespace

uint32_t FabricMemStarsSdma::Submit(const FabricMemAicpuKernelParam &param, const FabricMemAicpuTransferDesc *descs) {
  if (!ValidateSubmitArgs(param, descs)) {
    return kFailed;
  }
  FabricMemRtsqState state;
  if (!InitializeRtsq(param, state)) {
    HIXL_LOGE(FAILED, "[FabricMem][AICPU] initialize RTSQ failed. host_dev=%u rtsq=%u stream=%u task=%u logic_cq=%u",
              param.device_id, param.rtsq_id, param.rtsq_stream_id, param.rtsq_task_id, param.rtsq_logic_cq_id);
    return kFailed;
  }
  uint64_t deadline = 0U;
  if (!BuildSubmitDeadline(param.timeout_ms, deadline)) {
    return kFailed;
  }
  FabricMemRtsqBatch batch;
  const bool sdma_ok = AppendAndPublishSdmaTasks(descs, param.desc_count, state, batch, deadline);
  // Always emit NotifyRecord when requested (even after SDMA/CQ failure) so the host
  // control-stream WaitAndResetNotify is not stuck until its default timeout.
  bool notify_ok = true;
  if (param.emit_notify_record != 0U) {
    notify_ok = EmitNotifyRecord(param.notify_id, state, batch, deadline);
  }
  return (sdma_ok && notify_ok) ? kSuccess : kFailed;
}

bool FabricMemStarsSdma::ValidateSubmitArgs(const FabricMemAicpuKernelParam &param,
                                            const FabricMemAicpuTransferDesc *descs) {
  if (descs == nullptr || param.desc_count == 0U || param.emit_notify_record > 1U ||
      (param.emit_notify_record != 0U && param.notify_id >= kFabricMemA3NotifyIdLimit) ||
      !AreValidDescriptors(descs, param.desc_count)) {
    HIXL_LOGE(PARAM_INVALID,
              "[FabricMem][AICPU] invalid submit args. descs=%p desc_count=%u emit_notify=%u notify_id=%u", descs,
              param.desc_count, param.emit_notify_record, param.notify_id);
    return false;
  }
  return true;
}

bool FabricMemStarsSdma::BuildSubmitDeadline(uint64_t timeout_ms, uint64_t &deadline) {
  const uint64_t effective_timeout_ms = timeout_ms > 0U ? timeout_ms : kRtsqPollDefaultTimeoutSec * 1000U;
  const uint64_t now_ns = MonotonicNs();
  if (now_ns == 0U) {
    HIXL_LOGE(FAILED, "[FabricMem][AICPU] cannot start RTSQ submit deadline. timeout_ms=%llu",
              static_cast<uint64_t>(effective_timeout_ms));
    return false;
  }
  deadline = now_ns + effective_timeout_ms * kNsPerMillisecond;
  return true;
}

bool FabricMemStarsSdma::AppendAndPublishSdmaTasks(const FabricMemAicpuTransferDesc *descs, uint32_t desc_count,
                                                   FabricMemRtsqState &state, FabricMemRtsqBatch &batch,
                                                   uint64_t deadline) {
  for (uint32_t desc_idx = 0U; desc_idx < desc_count; ++desc_idx) {
    if (!AppendDescriptorTasks(descs[desc_idx], state, batch, deadline)) {
      HIXL_LOGE(FAILED, "[FabricMem][AICPU] append SDMA tasks failed at desc=%u/%u", desc_idx, desc_count);
      return false;
    }
  }
  // Publish SDMA first. NotifyRecord is fenced by host WaitAndResetNotify; AICPU only
  // polls logic CQ for abnormal CQEs once before emitting NotifyRecord.
  if (!PublishRtsqBatch(state, batch, deadline)) {
    HIXL_LOGE(FAILED, "[FabricMem][AICPU] publish SDMA RTSQ batch failed. sq=%u stream=%u", state.sq_id,
              state.stream_id);
    return false;
  }
  return true;
}

bool FabricMemStarsSdma::EmitNotifyRecord(uint32_t notify_id, FabricMemRtsqState &state, FabricMemRtsqBatch &batch,
                                          uint64_t deadline) {
  const bool cq_ok = PollLogicCqUntilEmpty(state, deadline);
  if (!cq_ok) {
    HIXL_LOGE(FAILED,
              "[FabricMem][AICPU] logic CQ poll failed before NotifyRecord; still emitting notify. "
              "sq=%u stream=%u logic_cq=%u notify_id=%u",
              state.sq_id, state.stream_id, state.logic_cq_id, notify_id);
  }
  if (!AppendNotifyTask(notify_id, state, batch, deadline)) {
    HIXL_LOGE(FAILED, "[FabricMem][AICPU] append NotifyRecord failed. notify_id=%u", notify_id);
    return false;
  }
  if (!PublishRtsqBatch(state, batch, deadline)) {
    HIXL_LOGE(FAILED, "[FabricMem][AICPU] publish NotifyRecord failed. sq=%u stream=%u notify_id=%u", state.sq_id,
              state.stream_id, notify_id);
    return false;
  }
  return cq_ok;
}

bool FabricMemStarsSdma::IsValidDescriptor(const FabricMemAicpuTransferDesc &desc) {
  if (desc.src_addr == 0U || desc.dst_addr == 0U || desc.length == 0U) {
    return false;
  }
  return desc.length <= std::numeric_limits<uint64_t>::max() - desc.src_addr &&
         desc.length <= std::numeric_limits<uint64_t>::max() - desc.dst_addr;
}

bool FabricMemStarsSdma::AreValidDescriptors(const FabricMemAicpuTransferDesc *descs, uint32_t desc_count) {
  for (uint32_t desc_idx = 0U; desc_idx < desc_count; ++desc_idx) {
    if (!IsValidDescriptor(descs[desc_idx])) {
      HIXL_LOGE(PARAM_INVALID, "[FabricMem][AICPU] invalid descriptor. idx=%u/%u src=0x%llx dst=0x%llx length=%llu",
                desc_idx, desc_count, static_cast<uint64_t>(descs[desc_idx].src_addr),
                static_cast<uint64_t>(descs[desc_idx].dst_addr), static_cast<uint64_t>(descs[desc_idx].length));
      return false;
    }
  }
  return true;
}

bool FabricMemStarsSdma::RestoreRtsqStream(const FabricMemRtsqState &state) {
  if (halResourceIdRestore == nullptr) {
    return true;
  }
  FabricMemDrvResIdKey resource{};
  resource.ruDevId = state.device_id;
  resource.resType = static_cast<uint32_t>(DRV_STREAM_ID);
  resource.resId = state.stream_id;
  const drvError_t ret = halResourceIdRestore(&resource);
  if (ret != DRV_ERROR_NONE) {
    HIXL_LOGE(FAILED, "[FabricMem][AICPU] halResourceIdRestore failed. ret=%d device=%u stream=%u",
              static_cast<int32_t>(ret), state.device_id, state.stream_id);
    return false;
  }
  return true;
}

bool FabricMemStarsSdma::ResolveLocalDeviceId(uint32_t host_device_id, uint32_t &local_device_id) {
  if (drvGetLocalDevIDByHostDevID == nullptr) {
    HIXL_LOGE(FAILED, "[FabricMem][AICPU] drvGetLocalDevIDByHostDevID unavailable. host_dev=%u", host_device_id);
    return false;
  }
  const drvError_t ret = drvGetLocalDevIDByHostDevID(host_device_id, &local_device_id);
  if (ret != DRV_ERROR_NONE) {
    HIXL_LOGE(FAILED, "[FabricMem][AICPU] drvGetLocalDevIDByHostDevID failed. ret=%d host_dev=%u",
              static_cast<int32_t>(ret), host_device_id);
    return false;
  }
  return true;
}

uint64_t FabricMemStarsSdma::MonotonicNs() {
  struct timespec ts {};
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
    HIXL_LOGE(FAILED, "[FabricMem][AICPU] clock_gettime(CLOCK_MONOTONIC) failed. errno=%d", errno);
    return 0U;
  }
  return static_cast<uint64_t>(ts.tv_sec) * kNsPerSecond + static_cast<uint64_t>(ts.tv_nsec);
}

bool FabricMemStarsSdma::DeadlineExceeded(uint64_t deadline_ns) {
  return MonotonicNs() >= deadline_ns;
}

bool FabricMemStarsSdma::IsExceptionLogicCqe(const FabricMemLogicCqeView &report) {
  return (report.error_type & kStarsExistErrorMask) != 0U;
}

void FabricMemStarsSdma::LogLogicCqe(const FabricMemRtsqState &state, const FabricMemLogicCqeView &report, uint32_t idx,
                                     uint32_t report_count, uint32_t total_reports, bool is_exception) {
  const char *tag = is_exception ? "abnormal" : "drained";
  if (is_exception) {
    HIXL_LOGE(FAILED,
              "[FabricMem][CQ] %s logic CQE. device=%u stream=%u sq=%u logic_cq=%u idx=%u/%u "
              "total=%u cqe{stream=%u task=%u err_code=%u err_type=%u sqe_type=%u sq=%u head=%u}",
              tag, state.device_id, state.stream_id, state.sq_id, state.logic_cq_id, idx, report_count, total_reports,
              static_cast<uint32_t>(report.stream_id), static_cast<uint32_t>(report.task_id), report.error_code,
              static_cast<uint32_t>(report.error_type), static_cast<uint32_t>(report.sqe_type),
              static_cast<uint32_t>(report.sq_id), static_cast<uint32_t>(report.sq_head));
    return;
  }
  HIXL_LOGD(
      "[FabricMem][CQ] %s logic CQE. device=%u stream=%u sq=%u logic_cq=%u idx=%u/%u total=%u "
      "cqe{stream=%u task=%u err_code=%u err_type=%u sqe_type=%u sq=%u head=%u}",
      tag, state.device_id, state.stream_id, state.sq_id, state.logic_cq_id, idx, report_count, total_reports,
      static_cast<uint32_t>(report.stream_id), static_cast<uint32_t>(report.task_id), report.error_code,
      static_cast<uint32_t>(report.error_type), static_cast<uint32_t>(report.sqe_type),
      static_cast<uint32_t>(report.sq_id), static_cast<uint32_t>(report.sq_head));
}

bool FabricMemStarsSdma::InspectLogicCqReports(const FabricMemRtsqState &state, const uint8_t *reports,
                                               uint32_t report_count, uint32_t total_reports) {
  for (uint32_t idx = 0U; idx < report_count; ++idx) {
    const auto &report =
        *reinterpret_cast<const FabricMemLogicCqeView *>(reports + static_cast<size_t>(idx) * kLogicCqeBytes);
    const bool is_exception = IsExceptionLogicCqe(report);
    LogLogicCqe(state, report, idx, report_count, total_reports, is_exception);
    if (is_exception) {
      return false;
    }
  }
  return true;
}

FabricMemLogicCqRecvResult FabricMemStarsSdma::RecvLogicCqBatch(const FabricMemRtsqState &state, uint8_t *reports,
                                                                uint32_t &report_count) {
  halReportRecvInfo recv_info{};
  recv_info.type = DRV_LOGIC_TYPE;
  recv_info.tsId = 0U;
  recv_info.report_cqe_num = 0U;
  recv_info.stream_id = state.stream_id;
  recv_info.cqId = state.logic_cq_id;
  recv_info.timeout = 0U;
  recv_info.task_id = kLogicCqAnyTaskId;
  recv_info.cqe_addr = reports;
  recv_info.cqe_num = kLogicCqReportCount;
  // Prefer match-copy (version 1): truncates to cqe_num. Legacy non-match does not.
  recv_info.res[0] = kLogicCqRecvMatchVersion;
  const drvError_t ret = halCqReportRecv(state.device_id, &recv_info);
  if (ret == DRV_ERROR_WAIT_TIMEOUT || (ret == DRV_ERROR_NONE && recv_info.report_cqe_num == 0U)) {
    report_count = 0U;
    return FabricMemLogicCqRecvResult::kEmpty;
  }
  if (ret != DRV_ERROR_NONE) {
    HIXL_LOGE(FAILED, "[FabricMem][CQ] halCqReportRecv failed, ret=%d device=%u stream=%u sq=%u logic_cq=%u",
              static_cast<int32_t>(ret), state.device_id, state.stream_id, state.sq_id, state.logic_cq_id);
    return FabricMemLogicCqRecvResult::kError;
  }
  if (recv_info.report_cqe_num > kLogicCqReportCount) {
    HIXL_LOGE(FAILED,
              "[FabricMem][CQ] halCqReportRecv returned more CQEs than buffer capacity. device=%u stream=%u "
              "sq=%u logic_cq=%u report=%u capacity=%u",
              state.device_id, state.stream_id, state.sq_id, state.logic_cq_id, recv_info.report_cqe_num,
              kLogicCqReportCount);
    return FabricMemLogicCqRecvResult::kError;
  }
  report_count = recv_info.report_cqe_num;
  return FabricMemLogicCqRecvResult::kReports;
}

bool FabricMemStarsSdma::PollLogicCqUntilEmpty(const FabricMemRtsqState &state, uint64_t deadline) {
  if (halCqReportRecv == nullptr) {
    HIXL_LOGW(
        "[FabricMem][CQ] halCqReportRecv unavailable, skip logic CQ poll. "
        "device=%u stream=%u sq=%u logic_cq=%u",
        state.device_id, state.stream_id, state.sq_id, state.logic_cq_id);
    return true;
  }

  alignas(uint64_t) uint8_t reports[kLogicCqReportCount * kLogicCqeBytes]{};
  uint32_t total_reports = 0U;
  for (;;) {
    if (deadline != 0U && DeadlineExceeded(deadline)) {
      HIXL_LOGE(FAILED,
                "[FabricMem][CQ] logic CQ poll hit submit deadline before empty. device=%u stream=%u sq=%u "
                "logic_cq=%u total=%u",
                state.device_id, state.stream_id, state.sq_id, state.logic_cq_id, total_reports);
      return false;
    }
    uint32_t report_count = 0U;
    const FabricMemLogicCqRecvResult recv_result = RecvLogicCqBatch(state, reports, report_count);
    if (recv_result == FabricMemLogicCqRecvResult::kEmpty) {
      return true;
    }
    if (recv_result == FabricMemLogicCqRecvResult::kError) {
      return false;
    }
    total_reports += report_count;
    if (!InspectLogicCqReports(state, reports, report_count, total_reports)) {
      return false;
    }
  }
}

bool FabricMemStarsSdma::LoadRtsqQueueState(FabricMemRtsqState &state) {
  uint32_t base_low = 0U;
  uint32_t base_high = 0U;
  if (!QueryRtsqValues(state.device_id, state.sq_id, DRV_SQCQ_PROP_SQ_BASE, base_low, base_high)) {
    HIXL_LOGE(FAILED, "[FabricMem][AICPU] query SQ_BASE failed. device=%u sq=%u", state.device_id, state.sq_id);
    return false;
  }
  if (!QueryRtsqValue(state.device_id, state.sq_id, DRV_SQCQ_PROP_SQ_DEPTH, state.depth) ||
      !QueryRtsqValue(state.device_id, state.sq_id, DRV_SQCQ_PROP_SQ_HEAD, state.head) ||
      !QueryRtsqValue(state.device_id, state.sq_id, DRV_SQCQ_PROP_SQ_TAIL, state.tail)) {
    HIXL_LOGE(FAILED, "[FabricMem][AICPU] query SQ depth/head/tail failed. device=%u sq=%u", state.device_id,
              state.sq_id);
    return false;
  }
  // Do not query DRV_SQCQ_PROP_CQ_DEPTH (prop=12): driver rejects it for NORMAL RTSQ.
  state.base_addr = (static_cast<uint64_t>(base_high) << 32U) | base_low;
  if (state.base_addr == 0U || state.depth <= 1U || state.head >= state.depth || state.tail >= state.depth) {
    HIXL_LOGE(FAILED, "[FabricMem][AICPU] invalid RTSQ state. device=%u sq=%u base=0x%llx depth=%u head=%u tail=%u",
              state.device_id, state.sq_id, static_cast<uint64_t>(state.base_addr), state.depth, state.head,
              state.tail);
    return false;
  }
  if (state.depth <= kMaxRtsqEntriesPerPublish) {
    HIXL_LOGE(FAILED,
              "[FabricMem][AICPU] RTSQ depth too small for max publish batch. device=%u sq=%u depth=%u "
              "min_exclusive=%u",
              state.device_id, state.sq_id, state.depth, kMaxRtsqEntriesPerPublish);
    return false;
  }
  HIXL_LOGD("[FabricMem][AICPU] RTSQ ready. device=%u sq=%u stream=%u logic_cq=%u sq_depth=%u head=%u tail=%u",
            state.device_id, state.sq_id, state.stream_id, state.logic_cq_id, state.depth, state.head, state.tail);
  return true;
}

bool FabricMemStarsSdma::InitializeRtsq(const FabricMemAicpuKernelParam &param, FabricMemRtsqState &state) {
  // sq_id must fit the u16 sq_id field of the A3 logic CQE ABI; stream_id must fit the
  // u16 rt_stream_id field of the A3 SQE header ABI. The driver query/config interfaces
  // themselves are u32, so out-of-range values are rejected here rather than truncated.
  if (param.rtsq_id > std::numeric_limits<uint16_t>::max()) {
    HIXL_LOGE(PARAM_INVALID, "[FabricMem][AICPU] rtsq_id out of A3 CQE u16 ABI range. sq=%u", param.rtsq_id);
    return false;
  }
  if (param.rtsq_stream_id > std::numeric_limits<uint16_t>::max()) {
    HIXL_LOGE(PARAM_INVALID, "[FabricMem][AICPU] rtsq_stream_id out of A3 SQE u16 ABI range. stream=%u",
              param.rtsq_stream_id);
    return false;
  }
  if (halSqCqQuery == nullptr || halSqCqConfig == nullptr) {
    HIXL_LOGE(FAILED, "[FabricMem][AICPU] halSqCqQuery/halSqCqConfig unavailable. query_null=%d config_null=%d",
              halSqCqQuery == nullptr ? 1 : 0, halSqCqConfig == nullptr ? 1 : 0);
    return false;
  }
  // param.device_id is the host/physical id from the host dispatcher; convert on-device.
  if (!ResolveLocalDeviceId(param.device_id, state.device_id)) {
    return false;
  }
  state.sq_id = param.rtsq_id;
  state.stream_id = param.rtsq_stream_id;
  state.logic_cq_id = param.rtsq_logic_cq_id;
  state.next_task_id = param.rtsq_task_id;
  return RestoreRtsqStream(state) && LoadRtsqQueueState(state);
}

bool FabricMemStarsSdma::HasRtsqCapacity(const FabricMemRtsqState &state, uint32_t count) {
  if (count == 0U || count >= state.depth) {
    return false;
  }
  const uint32_t used = (state.tail + state.depth - state.head) % state.depth;
  return used + count < state.depth;
}

bool FabricMemStarsSdma::EnsureRtsqCapacity(FabricMemRtsqState &state, uint32_t count, uint64_t deadline) {
  if (count == 0U || count >= state.depth) {
    HIXL_LOGE(PARAM_INVALID,
              "[FabricMem][AICPU] invalid RTSQ capacity request. device=%u sq=%u depth=%u need=%u head=%u tail=%u",
              state.device_id, state.sq_id, state.depth, count, state.head, state.tail);
    return false;
  }
  if (HasRtsqCapacity(state, count)) {
    return true;
  }
  if (!PollLogicCqUntilEmpty(state, deadline)) {
    HIXL_LOGE(FAILED,
              "[FabricMem][AICPU] logic CQ poll failed while refreshing RTSQ capacity. device=%u sq=%u stream=%u "
              "depth=%u need=%u head=%u tail=%u",
              state.device_id, state.sq_id, state.stream_id, state.depth, count, state.head, state.tail);
    return false;
  }
  if (!QueryRtsqValue(state.device_id, state.sq_id, DRV_SQCQ_PROP_SQ_HEAD, state.head)) {
    HIXL_LOGE(FAILED,
              "[FabricMem][AICPU] query SQ_HEAD failed while refreshing RTSQ capacity. device=%u sq=%u stream=%u "
              "depth=%u need=%u head=%u tail=%u",
              state.device_id, state.sq_id, state.stream_id, state.depth, count, state.head, state.tail);
    return false;
  }
  if (HasRtsqCapacity(state, count)) {
    return true;
  }
  const uint32_t used = (state.tail + state.depth - state.head) % state.depth;
  // Only two things can put us here, and they need very different follow-up, so name which one it is
  // instead of leaving the reader to compare the numbers.
  const char *const reason = state.depth < kFabricMemMinRtsqDepth
                                 ? "this RTSQ is too shallow to ever hold the host submit budget"
                                 : "SDMA has stopped retiring published entries";
  HIXL_LOGE(FAILED,
            "[FabricMem][AICPU] no RTSQ capacity after refreshing head: %s. The host queues up to %u entries "
            "between NotifyRecord waits, needing depth >= %u. device=%u sq=%u stream=%u depth=%u need=%u head=%u "
            "tail=%u used=%u",
            reason, kFabricMemMaxInFlightRtsqTasks, kFabricMemMinRtsqDepth, state.device_id, state.sq_id,
            state.stream_id, state.depth, count, state.head, state.tail, used);
  return false;
}

bool FabricMemStarsSdma::BuildSdmaSqe(uint64_t source, uint64_t destination, uint64_t size, uint32_t task_id,
                                      const FabricMemRtsqState &state, FabricMemA3SdmaSqe &sqe) {
  if (memset_s(&sqe, sizeof(sqe), 0, sizeof(sqe)) != EOK) {
    HIXL_LOGE(FAILED, "[FabricMem][AICPU] memset SDMA SQE failed.");
    return false;
  }
  sqe.header.type = kFabricMemA3SdmaSqeType;
  sqe.header.wr_cqe = 0U;
  sqe.header.rt_stream_id = static_cast<uint16_t>(state.stream_id);
  sqe.header.task_id = static_cast<uint16_t>(task_id);
  sqe.kernel_credit = kFabricMemA3SdmaKernelCredit;
  sqe.sssv = 1U;
  sqe.dssv = 1U;
  sqe.sns = 1U;
  sqe.dns = 1U;
  sqe.qos = kFabricMemA3SdmaQosDefault;
  sqe.length = static_cast<uint32_t>(size);
  sqe.src_addr_low = static_cast<uint32_t>(source);
  sqe.src_addr_high = static_cast<uint32_t>(source >> 32U);
  sqe.dst_addr_low = static_cast<uint32_t>(destination);
  sqe.dst_addr_high = static_cast<uint32_t>(destination >> 32U);
  sqe.link_type = kFabricMemA3SdmaLinkOnChip;
  return true;
}

bool FabricMemStarsSdma::BuildNotifySqe(uint32_t notify_id, uint32_t task_id, const FabricMemRtsqState &state,
                                        FabricMemA3NotifySqe &sqe) {
  if (memset_s(&sqe, sizeof(sqe), 0, sizeof(sqe)) != EOK) {
    HIXL_LOGE(FAILED, "[FabricMem][AICPU] memset NotifyRecord SQE failed.");
    return false;
  }
  sqe.header.type = kFabricMemA3NotifyRecordSqeType;
  sqe.header.wr_cqe = 0U;
  sqe.header.rt_stream_id = static_cast<uint16_t>(state.stream_id);
  sqe.header.task_id = static_cast<uint16_t>(task_id);
  sqe.notify_id = notify_id;
  sqe.kernel_credit = kFabricMemA3NotifyKernelCredit;
  return true;
}

bool FabricMemStarsSdma::CopySqeBatchToRing(FabricMemRtsqState &state, const FabricMemRtsqBatch &batch) {
  auto *sq_base = reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(state.base_addr));
  const uint32_t first_count = std::min(batch.count, state.depth - state.tail);
  const size_t first_bytes = static_cast<size_t>(first_count) * kFabricMemA3RtsqEntryBytes;
  const size_t first_dest_max = static_cast<size_t>(state.depth - state.tail) * kFabricMemA3RtsqEntryBytes;
  if (memcpy_s(sq_base + state.tail * kFabricMemA3RtsqEntryBytes, first_dest_max, batch.entries, first_bytes) != EOK) {
    HIXL_LOGE(FAILED,
              "[FabricMem][AICPU] memcpy SQE ring (first segment) failed. device=%u sq=%u tail=%u first_count=%u "
              "bytes=%zu",
              state.device_id, state.sq_id, state.tail, first_count, first_bytes);
    return false;
  }
  if (first_count < batch.count) {
    const size_t wrap_bytes = static_cast<size_t>(batch.count - first_count) * kFabricMemA3RtsqEntryBytes;
    const size_t wrap_dest_max = static_cast<size_t>(state.depth) * kFabricMemA3RtsqEntryBytes;
    if (memcpy_s(sq_base, wrap_dest_max, batch.entries + first_count, wrap_bytes) != EOK) {
      HIXL_LOGE(FAILED,
                "[FabricMem][AICPU] memcpy SQE ring (wrap segment) failed. device=%u sq=%u wrap_count=%u bytes=%zu",
                state.device_id, state.sq_id, batch.count - first_count, wrap_bytes);
      return false;
    }
  }
  return true;
}

bool FabricMemStarsSdma::CommitRtsqTail(FabricMemRtsqState &state, uint32_t new_tail, uint32_t batch_count) {
  std::atomic_thread_fence(std::memory_order_release);
  halSqCqConfigInfo config{};
  config.type = DRV_NORMAL_TYPE;
  config.tsId = 0U;
  config.sqId = state.sq_id;
  config.cqId = 0U;
  config.prop = DRV_SQCQ_PROP_SQ_TAIL;
  config.value[0U] = new_tail;
  const drvError_t ret = halSqCqConfig(state.device_id, &config);
  if (ret != DRV_ERROR_NONE) {
    HIXL_LOGE(FAILED,
              "[FabricMem][AICPU] halSqCqConfig SQ_TAIL failed. ret=%d device=%u sq=%u stream=%u old_tail=%u "
              "new_tail=%u batch_count=%u",
              static_cast<int32_t>(ret), state.device_id, state.sq_id, state.stream_id, state.tail, new_tail,
              batch_count);
    return false;
  }
  state.tail = new_tail;
  return true;
}

bool FabricMemStarsSdma::PublishRtsqBatch(FabricMemRtsqState &state, FabricMemRtsqBatch &batch, uint64_t deadline) {
  if (batch.count == 0U) {
    return true;
  }
  if (!EnsureRtsqCapacity(state, batch.count, deadline)) {
    HIXL_LOGE(FAILED, "[FabricMem][AICPU] publish has no RTSQ capacity. device=%u sq=%u stream=%u batch_count=%u",
              state.device_id, state.sq_id, state.stream_id, batch.count);
    return false;
  }
  const uint32_t new_tail = (state.tail + batch.count) % state.depth;
  if (!CopySqeBatchToRing(state, batch) || !CommitRtsqTail(state, new_tail, batch.count)) {
    return false;
  }
  batch.count = 0U;
  return true;
}

bool FabricMemStarsSdma::AppendDescriptorTasks(const FabricMemAicpuTransferDesc &desc, FabricMemRtsqState &state,
                                               FabricMemRtsqBatch &batch, uint64_t deadline) {
  uint64_t source = desc.src_addr;
  uint64_t destination = desc.dst_addr;
  uint64_t remaining = desc.length;
  while (remaining > 0U) {
    if (batch.count == kMaxRtsqEntriesPerPublish && !PublishRtsqBatch(state, batch, deadline)) {
      HIXL_LOGE(FAILED,
                "[FabricMem][AICPU] flush full SDMA batch failed. device=%u sq=%u remaining=%llu src=0x%llx "
                "dst=0x%llx",
                state.device_id, state.sq_id, static_cast<uint64_t>(remaining), static_cast<uint64_t>(source),
                static_cast<uint64_t>(destination));
      return false;
    }
    const uint64_t block_size = std::min(remaining, kMaxSdmaTransferBytes);
    if (!BuildSdmaSqe(source, destination, block_size, state.next_task_id++, state, batch.entries[batch.count].sdma)) {
      return false;
    }
    ++batch.count;
    source += block_size;
    destination += block_size;
    remaining -= block_size;
  }
  return true;
}

bool FabricMemStarsSdma::AppendNotifyTask(uint32_t notify_id, FabricMemRtsqState &state, FabricMemRtsqBatch &batch,
                                          uint64_t deadline) {
  if (batch.count == kMaxRtsqEntriesPerPublish && !PublishRtsqBatch(state, batch, deadline)) {
    HIXL_LOGE(FAILED,
              "[FabricMem][AICPU] flush full batch before NotifyRecord failed. device=%u sq=%u stream=%u "
              "notify_id=%u batch_count=%u",
              state.device_id, state.sq_id, state.stream_id, notify_id, batch.count);
    return false;
  }
  if (!BuildNotifySqe(notify_id, state.next_task_id++, state, batch.entries[batch.count].notify)) {
    return false;
  }
  ++batch.count;
  return true;
}

}  // namespace hixl
