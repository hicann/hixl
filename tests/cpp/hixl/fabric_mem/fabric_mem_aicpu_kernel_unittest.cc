/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <array>
#include <cstdint>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include "ascend_hal_error.h"
#include "fabric_mem/fabric_mem_aicpu_types.h"
#include "fabric_mem_a3_rtsq.h"
#include "fabric_mem_rtsq_query.h"
#include "fabric_mem_stars_sdma.h"
#include "hal_pkg/trs_pkg.h"
#include "runtime/rt_external_stream.h"
#include "depends/runtime/src/runtime_stub.h"
#include "transfer_context_manager.h"

extern "C" {
uint32_t HixlFabricMemBatchRead(hixl::FabricMemAicpuKernelParam *param);
uint32_t HixlFabricMemBatchWrite(hixl::FabricMemAicpuKernelParam *param);
}

namespace hixl {
namespace {
constexpr uint32_t kDriverDeviceId = 17U;
constexpr uint32_t kRtsqId = 23U;
constexpr uint32_t kRtsqStreamId = 29U;
constexpr uint32_t kInitialTaskId = 31U;
constexpr uint32_t kRtsqDepth = 512U;
constexpr uint32_t kNotifyId = 37U;
constexpr uint32_t kLogicCqId = 43U;
constexpr ThreadHandle kTransferCtxKey = 0xABCDEF01ULL;

struct CapturedSdmaTask {
  uint64_t source = 0U;
  uint64_t destination = 0U;
  uint32_t length = 0U;
  uint16_t stream_id = 0U;
  uint16_t task_id = 0U;
  uint8_t type = 0U;
  uint8_t kernel_credit = 0U;
  uint8_t qos = 0U;
  uint8_t link_type = 0U;
  uint16_t source_stream_id = 0U;
  uint16_t source_substream_id = 0U;
  uint16_t destination_stream_id = 0U;
  uint16_t destination_substream_id = 0U;
  bool uses_smmu = false;
  bool writes_cqe = false;
};

struct CapturedNotifyTask {
  uint32_t notify_id = 0U;
  uint16_t stream_id = 0U;
  uint16_t task_id = 0U;
  uint8_t type = 0U;
  uint8_t kernel_credit = 0U;
  bool writes_cqe = false;
};

std::array<FabricMemA3SdmaSqe, kRtsqDepth> g_rtsq{};
std::vector<CapturedSdmaTask> g_sdma_tasks;
std::vector<CapturedNotifyTask> g_notify_tasks;
uint32_t g_rtsq_head = 0U;
uint32_t g_rtsq_tail = 0U;
uint32_t g_rtsq_depth = kRtsqDepth;
uint32_t g_query_count = 0U;
uint32_t g_head_query_count = 0U;
uint32_t g_query_flag = 0U;
uint32_t g_config_count = 0U;
uint32_t g_driver_device_id = 0U;
uint32_t g_host_device_id = 0U;
uint32_t g_local_device_id = kDriverDeviceId;
uint32_t g_sq_id = 0U;
uint32_t g_restore_count = 0U;
FabricMemDrvResIdKey g_restored_resource{};
int g_query_result = 0;
int g_config_result = 0;
int g_restore_result = 0;
int g_local_dev_result = 0;
bool g_consume_on_publish = true;
bool g_advance_head_on_query = false;
// Counts SQ_HEAD queries while the ring is non-empty; init (head==tail) does not count
// against the retirement simulation.
uint32_t g_nonempty_head_query_count = 0U;
uint32_t g_cq_recv_count = 0U;
uint32_t g_cq_report_num = 0U;
uint8_t g_cq_error_type = 0U;
// When true with report_num>=2, only CQE index 1 is abnormal (catches 14 vs 32 stride bugs).
bool g_cq_only_second_abnormal = false;
// When non-zero, the N-th halCqReportRecv call returns one abnormal CQE.
uint32_t g_cq_inject_error_at_recv = 0U;
uint32_t g_last_cq_id = 0U;
uint32_t g_last_cq_stream_id = 0U;

// Match production kLogicCqeBytes: driver CQE stride is 32; view is a 14-byte prefix.
constexpr size_t kStubLogicCqeBytes = 32U;

struct __attribute__((packed)) StubLogicCqeView {
  uint16_t stream_id = 0U;
  uint16_t task_id = 0U;
  uint32_t error_code = 0U;
  uint8_t error_type = 0U;
  uint8_t sqe_type = 0U;
  uint16_t sq_id = 0U;
  uint16_t sq_head = 0U;
};
static_assert(sizeof(StubLogicCqeView) == 14U, "StubLogicCqeView prefix size mismatch.");
static_assert(kStubLogicCqeBytes >= sizeof(StubLogicCqeView), "Stub CQE stride smaller than prefix.");

void CaptureTasks(uint32_t new_tail) {
  for (uint32_t index = g_rtsq_tail; index != new_tail; index = (index + 1U) % g_rtsq_depth) {
    const auto &sqe = g_rtsq[index];
    if (sqe.header.type == kFabricMemA3NotifyRecordSqeType) {
      const auto &notify = *reinterpret_cast<const FabricMemA3NotifySqe *>(&sqe);
      g_notify_tasks.push_back({notify.notify_id, notify.header.rt_stream_id, notify.header.task_id, notify.header.type,
                                notify.kernel_credit, notify.header.wr_cqe != 0U});
      continue;
    }
    g_sdma_tasks.push_back(
        {(static_cast<uint64_t>(sqe.src_addr_high) << 32U) | sqe.src_addr_low,
         (static_cast<uint64_t>(sqe.dst_addr_high) << 32U) | sqe.dst_addr_low, sqe.length, sqe.header.rt_stream_id,
         sqe.header.task_id, sqe.header.type, sqe.kernel_credit, static_cast<uint8_t>(sqe.qos), sqe.link_type,
         sqe.src_stream_id, sqe.src_substream_id, sqe.dst_stream_id, sqe.dst_substream_id,
         sqe.sssv != 0U && sqe.dssv != 0U && sqe.sns != 0U && sqe.dns != 0U, sqe.header.wr_cqe != 0U});
  }
}

void ResetRtsqStub() {
  g_rtsq.fill(FabricMemA3SdmaSqe{});
  g_sdma_tasks.clear();
  g_notify_tasks.clear();
  g_rtsq_head = 0U;
  g_rtsq_tail = 0U;
  g_rtsq_depth = kRtsqDepth;
  g_query_count = 0U;
  g_head_query_count = 0U;
  g_query_flag = 0U;
  g_config_count = 0U;
  g_driver_device_id = 0U;
  g_host_device_id = 0U;
  g_local_device_id = kDriverDeviceId;
  g_sq_id = 0U;
  g_restore_count = 0U;
  g_restored_resource = {};
  g_query_result = 0;
  g_config_result = 0;
  g_restore_result = 0;
  g_local_dev_result = 0;
  g_consume_on_publish = true;
  g_advance_head_on_query = false;
  g_nonempty_head_query_count = 0U;
  g_cq_recv_count = 0U;
  g_cq_report_num = 0U;
  g_cq_error_type = 0U;
  g_cq_only_second_abnormal = false;
  g_cq_inject_error_at_recv = 0U;
  g_last_cq_id = 0U;
  g_last_cq_stream_id = 0U;
  SetStubRtStreamSqId(kRtsqId);
}

FabricMemAicpuKernelParam MakeParam(const void *descs, uint32_t desc_count, FabricMemAicpuTransferDirection direction) {
  FabricMemAicpuKernelParam param;
  param.desc_addr = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(descs));
  param.desc_count = desc_count;
  param.device_id = kDriverDeviceId;
  param.rtsq_id = kRtsqId;
  param.rtsq_stream_id = kRtsqStreamId;
  param.rtsq_task_id = kInitialTaskId;
  param.rtsq_logic_cq_id = kLogicCqId;
  param.direction = static_cast<uint32_t>(direction);
  param.transfer_ctx_key = static_cast<uint64_t>(kTransferCtxKey);
  return param;
}

class FabricMemAicpuKernelUTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ResetRtsqStub();
    (void)TransferContextManager::Instance().Delete(kTransferCtxKey);
    (void)TransferContextManager::Instance().Add(kTransferCtxKey);
  }

  void TearDown() override {
    (void)TransferContextManager::Instance().Delete(kTransferCtxKey);
  }
};
}  // namespace
}  // namespace hixl

extern "C" {
// Satisfy TaskExceptionHandler link deps pulled in via transfer_context_manager.
drvError_t halEschedSubmitEvent(unsigned int /*dev_id*/, struct event_summary * /*event*/) {
  return DRV_ERROR_NONE;
}

drvError_t halCqReportRecv(uint32_t /*dev_id*/, struct halReportRecvInfo *info) {
  if (info == nullptr) {
    return static_cast<drvError_t>(1);
  }
  ++hixl::g_cq_recv_count;
  hixl::g_last_cq_id = info->cqId;
  hixl::g_last_cq_stream_id = info->stream_id;

  const bool inject_error =
      hixl::g_cq_inject_error_at_recv != 0U && hixl::g_cq_recv_count == hixl::g_cq_inject_error_at_recv;
  if (inject_error) {
    info->report_cqe_num = 1U;
  } else {
    info->report_cqe_num = hixl::g_cq_report_num;
    // Only the first ordinary recv call returns injected reports; subsequent calls empty.
    hixl::g_cq_report_num = 0U;
  }
  if (info->report_cqe_num == 0U) {
    return DRV_ERROR_WAIT_TIMEOUT;
  }
  if (info->cqe_addr != nullptr && (inject_error || hixl::g_cq_error_type != 0U || hixl::g_cq_only_second_abnormal)) {
    auto *base = reinterpret_cast<uint8_t *>(info->cqe_addr);
    const uint8_t default_err =
        inject_error ? (hixl::g_cq_error_type != 0U ? hixl::g_cq_error_type : 0x1U) : hixl::g_cq_error_type;
    for (uint32_t idx = 0U; idx < info->report_cqe_num; ++idx) {
      auto *report =
          reinterpret_cast<hixl::StubLogicCqeView *>(base + static_cast<size_t>(idx) * hixl::kStubLogicCqeBytes);
      *report = {};
      uint8_t err_type = default_err;
      if (hixl::g_cq_only_second_abnormal) {
        err_type = (idx == 1U) ? (hixl::g_cq_error_type != 0U ? hixl::g_cq_error_type : 0x1U) : 0U;
      }
      report->error_type = err_type;
      report->error_code = 0xaU;
      report->sqe_type = 11U;
    }
  }
  return DRV_ERROR_NONE;
}

drvError_t halSqCqQuery(uint32_t dev_id, struct halSqCqQueryInfo *info) {
  ++hixl::g_query_count;
  hixl::g_driver_device_id = dev_id;
  if (hixl::g_query_result != 0 || info == nullptr) {
    return static_cast<drvError_t>(1);
  }
  hixl::g_sq_id = info->sqId;
  hixl::g_query_flag = info->value[hixl::fabric_mem_rtsq::kRtsqQueryInfoFlagIndex];
  const uint64_t queue_addr = reinterpret_cast<uintptr_t>(hixl::g_rtsq.data());
  switch (info->prop) {
    case DRV_SQCQ_PROP_SQ_BASE:
      info->value[0U] = static_cast<uint32_t>(queue_addr);
      info->value[1U] = static_cast<uint32_t>(queue_addr >> 32U);
      break;
    case DRV_SQCQ_PROP_SQ_DEPTH:
      info->value[0U] = hixl::g_rtsq_depth;
      break;
    case DRV_SQCQ_PROP_CQ_DEPTH:
      // Runtime pairs physical CQ with SQ; UT mirrors the common A3 depth.
      info->value[0U] = hixl::g_rtsq_depth;
      break;
    case DRV_SQCQ_PROP_SQ_HEAD:
      ++hixl::g_head_query_count;
      // Simulate HW retiring published SQEs by the time the kernel re-reads head. Init queries
      // (head==tail) must not consume the simulation.
      if (hixl::g_rtsq_head != hixl::g_rtsq_tail) {
        ++hixl::g_nonempty_head_query_count;
        if (hixl::g_advance_head_on_query) {
          hixl::g_rtsq_head = hixl::g_rtsq_tail;
          hixl::g_advance_head_on_query = false;
        }
      }
      info->value[0U] = hixl::g_rtsq_head;
      break;
    case DRV_SQCQ_PROP_SQ_TAIL:
      info->value[0U] = hixl::g_rtsq_tail;
      break;
    default:
      return static_cast<drvError_t>(1);
  }
  return DRV_ERROR_NONE;
}

drvError_t halSqCqConfig(uint32_t dev_id, struct halSqCqConfigInfo *info) {
  ++hixl::g_config_count;
  hixl::g_driver_device_id = dev_id;
  if (hixl::g_config_result != 0 || info == nullptr || info->prop != DRV_SQCQ_PROP_SQ_TAIL) {
    return static_cast<drvError_t>(1);
  }
  hixl::CaptureTasks(info->value[0U]);
  hixl::g_rtsq_tail = info->value[0U];
  if (hixl::g_consume_on_publish) {
    hixl::g_rtsq_head = hixl::g_rtsq_tail;
  }
  return DRV_ERROR_NONE;
}

drvError_t drvGetLocalDevIDByHostDevID(uint32_t host_dev_id, uint32_t *local_dev_id) {
  hixl::g_host_device_id = host_dev_id;
  if (local_dev_id == nullptr || hixl::g_local_dev_result != 0) {
    return static_cast<drvError_t>(1);
  }
  *local_dev_id = hixl::g_local_device_id;
  return DRV_ERROR_NONE;
}

drvError_t halResourceIdRestore(hixl::FabricMemDrvResIdKey *resource) {
  ++hixl::g_restore_count;
  if (resource == nullptr || hixl::g_restore_result != 0) {
    return static_cast<drvError_t>(1);
  }
  hixl::g_restored_resource = *resource;
  return DRV_ERROR_NONE;
}
}  // extern "C"

namespace hixl {
namespace {
TEST_F(FabricMemAicpuKernelUTest, BatchWritePollsLogicCqBeforeNotifyRecord) {
  FabricMemAicpuTransferDesc desc{0x1000U, 0x2000U, 16U};
  auto param = MakeParam(&desc, 1U, FabricMemAicpuTransferDirection::kWrite);
  param.emit_notify_record = 1U;
  param.notify_id = kNotifyId;
  param.timeout_ms = 1000U;
  g_cq_report_num = 2U;

  EXPECT_EQ(HixlFabricMemBatchWrite(&param), 0U);
  ASSERT_EQ(g_sdma_tasks.size(), 1U);
  ASSERT_EQ(g_notify_tasks.size(), 1U);
  EXPECT_EQ(g_notify_tasks[0U].notify_id, kNotifyId);
  EXPECT_EQ(g_notify_tasks[0U].stream_id, kRtsqStreamId);
  EXPECT_EQ(g_notify_tasks[0U].task_id, kInitialTaskId + 1U);
  EXPECT_EQ(g_notify_tasks[0U].type, kFabricMemA3NotifyRecordSqeType);
  EXPECT_EQ(g_notify_tasks[0U].kernel_credit, kFabricMemA3NotifyKernelCredit);
  EXPECT_FALSE(g_notify_tasks[0U].writes_cqe);
  // SDMA publish + NotifyRecord publish.
  EXPECT_EQ(g_config_count, 2U);
  // Pre-notify PollLogicCqUntilEmpty drains leftover reports then sees empty.
  EXPECT_GE(g_cq_recv_count, 2U);
  EXPECT_EQ(g_last_cq_id, kLogicCqId);
  EXPECT_EQ(g_last_cq_stream_id, kRtsqStreamId);
}

TEST_F(FabricMemAicpuKernelUTest, BatchWriteFailsOnAbnormalLogicCqeBeforeNotifyRecord) {
  FabricMemAicpuTransferDesc desc{0x1000U, 0x2000U, 16U};
  auto param = MakeParam(&desc, 1U, FabricMemAicpuTransferDirection::kWrite);
  param.emit_notify_record = 1U;
  param.notify_id = kNotifyId;
  param.timeout_ms = 1000U;
  // Pre-notify CQ poll is the first recv in this submit.
  g_cq_inject_error_at_recv = 1U;
  g_cq_error_type = 0x1U;

  EXPECT_EQ(HixlFabricMemBatchWrite(&param), 1U);
  EXPECT_EQ(g_cq_recv_count, 1U);
  // Failure still emits NotifyRecord so host WaitAndResetNotify can complete quickly.
  ASSERT_EQ(g_notify_tasks.size(), 1U);
  EXPECT_EQ(g_notify_tasks[0U].notify_id, kNotifyId);
  // SDMA publish + NotifyRecord publish.
  EXPECT_EQ(g_config_count, 2U);
  EXPECT_EQ(g_sdma_tasks.size(), 1U);
}

TEST_F(FabricMemAicpuKernelUTest, BatchWriteFailsWhenOnlySecondLogicCqeAbnormal) {
  FabricMemAicpuTransferDesc desc{0x1000U, 0x2000U, 16U};
  auto param = MakeParam(&desc, 1U, FabricMemAicpuTransferDirection::kWrite);
  param.emit_notify_record = 1U;
  param.notify_id = kNotifyId;
  param.timeout_ms = 1000U;
  // Two CQEs with only index 1 abnormal: 14-byte stride would miss it and wrongly succeed.
  g_cq_report_num = 2U;
  g_cq_only_second_abnormal = true;
  g_cq_error_type = 0x1U;

  EXPECT_EQ(HixlFabricMemBatchWrite(&param), 1U);
  EXPECT_GE(g_cq_recv_count, 1U);
  ASSERT_EQ(g_notify_tasks.size(), 1U);
  EXPECT_EQ(g_notify_tasks[0U].notify_id, kNotifyId);
  EXPECT_EQ(g_config_count, 2U);
}

TEST_F(FabricMemAicpuKernelUTest, BatchWriteSkipsLogicCqPollWithoutNotifyRecord) {
  FabricMemAicpuTransferDesc desc{0x1000U, 0x2000U, 16U};
  auto param = MakeParam(&desc, 1U, FabricMemAicpuTransferDirection::kWrite);
  g_cq_report_num = 2U;
  g_cq_error_type = 0x1U;

  EXPECT_EQ(HixlFabricMemBatchWrite(&param), 0U);
  EXPECT_EQ(g_cq_recv_count, 0U);
  EXPECT_EQ(g_config_count, 1U);
  EXPECT_TRUE(g_notify_tasks.empty());
}

TEST_F(FabricMemAicpuKernelUTest, BatchWriteFailsOnAbnormalLogicCqeWhileRefreshingCapacity) {
  constexpr uint32_t kSmallDepth = 129U;
  g_rtsq_depth = kSmallDepth;
  g_consume_on_publish = false;

  // Fill the ring so the next publish has to refresh capacity before it can commit.
  std::vector<FabricMemAicpuTransferDesc> fill_descs(128U, FabricMemAicpuTransferDesc{0x1000U, 0x2000U, 1U});
  auto fill_param =
      MakeParam(fill_descs.data(), static_cast<uint32_t>(fill_descs.size()), FabricMemAicpuTransferDirection::kWrite);
  fill_param.timeout_ms = 1000U;
  EXPECT_EQ(HixlFabricMemBatchWrite(&fill_param), 0U);
  EXPECT_EQ(g_rtsq_tail, 128U);
  EXPECT_EQ(g_cq_recv_count, 0U);

  // The capacity refresh poll is the first recv of the second submit.
  g_cq_inject_error_at_recv = 1U;
  g_cq_error_type = 0x1U;
  g_sdma_tasks.clear();
  FabricMemAicpuTransferDesc more{0x3000U, 0x4000U, 1U};
  auto refresh_param = MakeParam(&more, 1U, FabricMemAicpuTransferDirection::kWrite);
  refresh_param.timeout_ms = 1000U;
  refresh_param.rtsq_task_id = kInitialTaskId + 128U;
  EXPECT_EQ(HixlFabricMemBatchWrite(&refresh_param), 1U);
  EXPECT_EQ(g_cq_recv_count, 1U);
}

TEST_F(FabricMemAicpuKernelUTest, BatchWriteFailsInsteadOfWaitingWhenRtsqStaysFull) {
  constexpr uint32_t kSmallDepth = 129U;
  g_rtsq_depth = kSmallDepth;
  g_consume_on_publish = false;
  // Head never moves: the queue is genuinely full, which the host throttling should have prevented.
  g_advance_head_on_query = false;

  std::vector<FabricMemAicpuTransferDesc> fill_descs(128U, FabricMemAicpuTransferDesc{0x1000U, 0x2000U, 1U});
  auto fill_param =
      MakeParam(fill_descs.data(), static_cast<uint32_t>(fill_descs.size()), FabricMemAicpuTransferDirection::kWrite);
  fill_param.timeout_ms = 1000U;
  ASSERT_EQ(HixlFabricMemBatchWrite(&fill_param), 0U);
  ASSERT_EQ(g_rtsq_tail, 128U);
  const uint32_t config_count_after_fill = g_config_count;
  const uint32_t head_query_after_fill = g_head_query_count;

  FabricMemAicpuTransferDesc more{0x3000U, 0x4000U, 1U};
  auto full_param = MakeParam(&more, 1U, FabricMemAicpuTransferDirection::kWrite);
  // A long timeout must not turn into a long wait: refresh head once, then fail.
  full_param.timeout_ms = 60U * 1000U;
  full_param.rtsq_task_id = kInitialTaskId + 128U;
  EXPECT_EQ(HixlFabricMemBatchWrite(&full_param), 1U);
  EXPECT_EQ(g_config_count, config_count_after_fill);
  // One initial state load plus exactly one capacity refresh, i.e. no retry loop.
  EXPECT_EQ(g_head_query_count - head_query_after_fill, 2U);
}

TEST_F(FabricMemAicpuKernelUTest, BatchWriteBuildsHcommStyleA3SdmaSqes) {
  std::vector<FabricMemAicpuTransferDesc> descs = {
      {0x1000U, 0x2000U, 16U},
      {0x3000U, 0x4000U, 32U},
  };
  auto param = MakeParam(descs.data(), static_cast<uint32_t>(descs.size()), FabricMemAicpuTransferDirection::kWrite);

  EXPECT_EQ(HixlFabricMemBatchWrite(&param), 0U);
  ASSERT_EQ(g_sdma_tasks.size(), 2U);
  EXPECT_EQ(g_sdma_tasks[0U].source, 0x1000U);
  EXPECT_EQ(g_sdma_tasks[0U].destination, 0x2000U);
  EXPECT_EQ(g_sdma_tasks[0U].length, 16U);
  EXPECT_EQ(g_sdma_tasks[1U].source, 0x3000U);
  EXPECT_EQ(g_sdma_tasks[1U].destination, 0x4000U);
  EXPECT_EQ(g_sdma_tasks[1U].length, 32U);
  EXPECT_EQ(g_sdma_tasks[0U].stream_id, kRtsqStreamId);
  EXPECT_EQ(g_sdma_tasks[0U].task_id, kInitialTaskId);
  EXPECT_EQ(g_sdma_tasks[1U].task_id, kInitialTaskId + 1U);
  EXPECT_EQ(g_sdma_tasks[0U].type, kFabricMemA3SdmaSqeType);
  EXPECT_EQ(g_sdma_tasks[0U].kernel_credit, kFabricMemA3SdmaKernelCredit);
  EXPECT_EQ(g_sdma_tasks[0U].qos, kFabricMemA3SdmaQosDefault);
  EXPECT_EQ(g_sdma_tasks[0U].link_type, kFabricMemA3SdmaLinkOnChip);
  EXPECT_EQ(g_sdma_tasks[0U].source_stream_id, 0U);
  EXPECT_EQ(g_sdma_tasks[0U].source_substream_id, 0U);
  EXPECT_EQ(g_sdma_tasks[0U].destination_stream_id, 0U);
  EXPECT_EQ(g_sdma_tasks[0U].destination_substream_id, 0U);
  EXPECT_TRUE(g_sdma_tasks[0U].uses_smmu);
  EXPECT_FALSE(g_sdma_tasks[0U].writes_cqe);
  EXPECT_TRUE(g_notify_tasks.empty());
  EXPECT_EQ(g_restore_count, 1U);
  EXPECT_EQ(g_host_device_id, kDriverDeviceId);
  EXPECT_EQ(g_restored_resource.ruDevId, kDriverDeviceId);
  EXPECT_EQ(g_restored_resource.resType, DRV_STREAM_ID);
  EXPECT_EQ(g_restored_resource.resId, kRtsqStreamId);
  EXPECT_EQ(g_config_count, 1U);
  EXPECT_EQ(g_driver_device_id, kDriverDeviceId);
  EXPECT_EQ(g_sq_id, kRtsqId);
}

TEST_F(FabricMemAicpuKernelUTest, BatchWriteConvertsHostDeviceIdToLocalDriverId) {
  FabricMemAicpuTransferDesc desc{0x1000U, 0x2000U, 16U};
  auto param = MakeParam(&desc, 1U, FabricMemAicpuTransferDirection::kWrite);
  param.device_id = 7U;
  g_local_device_id = 3U;

  EXPECT_EQ(HixlFabricMemBatchWrite(&param), 0U);
  EXPECT_EQ(g_host_device_id, 7U);
  EXPECT_EQ(g_driver_device_id, 3U);
  EXPECT_EQ(g_restored_resource.ruDevId, 3U);
}

TEST_F(FabricMemAicpuKernelUTest, BatchWriteFailsWhenHostToLocalDeviceIdFails) {
  FabricMemAicpuTransferDesc desc{0x1000U, 0x2000U, 16U};
  auto param = MakeParam(&desc, 1U, FabricMemAicpuTransferDirection::kWrite);
  g_local_dev_result = 1;

  EXPECT_EQ(HixlFabricMemBatchWrite(&param), 1U);
  EXPECT_EQ(g_host_device_id, kDriverDeviceId);
  EXPECT_EQ(g_restore_count, 0U);
  EXPECT_EQ(g_query_count, 0U);
}

TEST_F(FabricMemAicpuKernelUTest, BatchWriteRejectsRtsqIdBeyondCqeAbiRange) {
  FabricMemAicpuTransferDesc desc{0x1000U, 0x2000U, 16U};
  auto param = MakeParam(&desc, 1U, FabricMemAicpuTransferDirection::kWrite);
  // A3 logic CQE carries sq_id in u16; the kernel must reject, never truncate.
  param.rtsq_id = static_cast<uint32_t>(std::numeric_limits<uint16_t>::max()) + 1U;

  EXPECT_EQ(HixlFabricMemBatchWrite(&param), 1U);
  EXPECT_TRUE(g_sdma_tasks.empty());
  EXPECT_TRUE(g_notify_tasks.empty());
  EXPECT_EQ(g_query_count, 0U);
  EXPECT_EQ(g_config_count, 0U);
}

TEST_F(FabricMemAicpuKernelUTest, BatchWriteRejectsRtsqStreamIdBeyondSqeAbiRange) {
  FabricMemAicpuTransferDesc desc{0x1000U, 0x2000U, 16U};
  auto param = MakeParam(&desc, 1U, FabricMemAicpuTransferDirection::kWrite);
  // A3 SQE header carries rt_stream_id in u16; the kernel must reject, never truncate.
  param.rtsq_stream_id = static_cast<uint32_t>(std::numeric_limits<uint16_t>::max()) + 1U;

  EXPECT_EQ(HixlFabricMemBatchWrite(&param), 1U);
  EXPECT_TRUE(g_sdma_tasks.empty());
  EXPECT_TRUE(g_notify_tasks.empty());
  EXPECT_EQ(g_query_count, 0U);
  EXPECT_EQ(g_config_count, 0U);
}

TEST_F(FabricMemAicpuKernelUTest, BatchReadSplitsTransfersLargerThanOneSdmaTask) {
  constexpr uint64_t kMaxSdmaLength = std::numeric_limits<uint32_t>::max();
  std::vector<FabricMemAicpuTransferDesc> descs = {
      {0x1000U, 0x2000U, kMaxSdmaLength + 3U},
  };
  auto param = MakeParam(descs.data(), static_cast<uint32_t>(descs.size()), FabricMemAicpuTransferDirection::kRead);
  param.emit_notify_record = 1U;
  param.notify_id = kNotifyId;

  EXPECT_EQ(HixlFabricMemBatchRead(&param), 0U);
  ASSERT_EQ(g_sdma_tasks.size(), 2U);
  EXPECT_EQ(g_sdma_tasks[0U].length, std::numeric_limits<uint32_t>::max());
  EXPECT_EQ(g_sdma_tasks[1U].source, 0x1000U + kMaxSdmaLength);
  EXPECT_EQ(g_sdma_tasks[1U].destination, 0x2000U + kMaxSdmaLength);
  EXPECT_EQ(g_sdma_tasks[1U].length, 3U);
  ASSERT_EQ(g_notify_tasks.size(), 1U);
  EXPECT_EQ(g_notify_tasks[0U].task_id, kInitialTaskId + 2U);
  EXPECT_EQ(g_notify_tasks[0U].notify_id, kNotifyId);
}

TEST_F(FabricMemAicpuKernelUTest, BatchWritePublishesBoundedRtsqBatches) {
  constexpr size_t kTaskCount = 128U;
  std::vector<FabricMemAicpuTransferDesc> descs(kTaskCount);
  for (size_t i = 0U; i < descs.size(); ++i) {
    descs[i] = {0x1000U + i * 0x1000U, 0x2000U + i * 0x1000U, 16U};
  }
  auto param = MakeParam(descs.data(), static_cast<uint32_t>(descs.size()), FabricMemAicpuTransferDirection::kWrite);

  EXPECT_EQ(HixlFabricMemBatchWrite(&param), 0U);
  ASSERT_EQ(g_sdma_tasks.size(), kTaskCount);
  EXPECT_EQ(g_config_count, 1U);
  EXPECT_EQ(g_sdma_tasks.front().task_id, kInitialTaskId);
  EXPECT_EQ(g_sdma_tasks.back().task_id, kInitialTaskId + kTaskCount - 1U);
}

TEST_F(FabricMemAicpuKernelUTest, BatchWriteRefreshesStaleHeadBeforePublishingNextBatch) {
  constexpr size_t kTaskCount = 128U;
  std::vector<FabricMemAicpuTransferDesc> descs(kTaskCount);
  for (size_t i = 0U; i < descs.size(); ++i) {
    descs[i] = {0x1000U + i * 0x1000U, 0x2000U + i * 0x1000U, 16U};
  }
  auto param = MakeParam(descs.data(), static_cast<uint32_t>(descs.size()), FabricMemAicpuTransferDirection::kWrite);
  param.emit_notify_record = 1U;
  param.notify_id = kNotifyId;
  param.timeout_ms = 1000U;
  g_rtsq_depth = 129U;
  g_consume_on_publish = false;
  g_advance_head_on_query = true;

  EXPECT_EQ(HixlFabricMemBatchWrite(&param), 0U);
  EXPECT_EQ(g_sdma_tasks.size(), kTaskCount);
  // One publish for the 128 SDMA SQEs, one for NotifyRecord. The cached head still shows the ring
  // as full at that point, so the second publish only succeeds because it re-reads head once.
  EXPECT_EQ(g_config_count, 2U);
  EXPECT_GE(g_head_query_count, 2U);
  EXPECT_GE(g_cq_recv_count, 1U);
  EXPECT_EQ(g_notify_tasks.size(), 1U);
}

TEST_F(FabricMemAicpuKernelUTest, BatchWriteRejectsMoreThan128Descriptors) {
  std::vector<FabricMemAicpuTransferDesc> descs(129U, {0x1000U, 0x2000U, 16U});
  auto param = MakeParam(descs.data(), static_cast<uint32_t>(descs.size()), FabricMemAicpuTransferDirection::kWrite);

  EXPECT_EQ(HixlFabricMemBatchWrite(&param), 1U);
  EXPECT_EQ(g_query_count, 0U);
  EXPECT_TRUE(g_sdma_tasks.empty());
}

TEST_F(FabricMemAicpuKernelUTest, BatchWriteRejectsInvalidNotifyMetadata) {
  FabricMemAicpuTransferDesc desc{0x1000U, 0x2000U, 16U};
  auto param = MakeParam(&desc, 1U, FabricMemAicpuTransferDirection::kWrite);
  param.emit_notify_record = 1U;
  param.notify_id = kFabricMemA3NotifyIdLimit;

  EXPECT_EQ(HixlFabricMemBatchWrite(&param), 1U);
  EXPECT_EQ(g_query_count, 0U);

  param.notify_id = kNotifyId;
  param.emit_notify_record = 2U;
  EXPECT_EQ(HixlFabricMemBatchWrite(&param), 1U);
  EXPECT_EQ(g_query_count, 0U);
}

TEST_F(FabricMemAicpuKernelUTest, BatchWriteWritesStatusOnSuccessAndFailure) {
  FabricMemAicpuTransferDesc desc{0x1000U, 0x2000U, 16U};
  uint32_t status = 0xFFFFFFFFU;
  auto param = MakeParam(&desc, 1U, FabricMemAicpuTransferDirection::kWrite);
  param.status_addr = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&status));

  EXPECT_EQ(HixlFabricMemBatchWrite(&param), 0U);
  EXPECT_EQ(status, 0U);

  status = 0xFFFFFFFFU;
  g_query_result = 1;
  EXPECT_EQ(HixlFabricMemBatchWrite(&param), 1U);
  EXPECT_EQ(status, 1U);
}

TEST_F(FabricMemAicpuKernelUTest, BatchWriteFailsWhenRtsqQueriesAreUnavailable) {
  FabricMemAicpuTransferDesc desc{0x1000U, 0x2000U, 16U};
  auto param = MakeParam(&desc, 1U, FabricMemAicpuTransferDirection::kWrite);
  g_query_result = 1;

  EXPECT_EQ(HixlFabricMemBatchWrite(&param), 1U);
  EXPECT_GT(g_query_count, 0U);
  EXPECT_EQ(g_config_count, 0U);
  EXPECT_TRUE(g_sdma_tasks.empty());
}

TEST_F(FabricMemAicpuKernelUTest, BatchWriteFailsWhenRtsqStreamRestoreFails) {
  FabricMemAicpuTransferDesc desc{0x1000U, 0x2000U, 16U};
  auto param = MakeParam(&desc, 1U, FabricMemAicpuTransferDirection::kWrite);
  g_restore_result = 1;

  EXPECT_EQ(HixlFabricMemBatchWrite(&param), 1U);
  EXPECT_EQ(g_restore_count, 1U);
  EXPECT_EQ(g_query_count, 0U);
  EXPECT_EQ(g_config_count, 0U);
}

TEST_F(FabricMemAicpuKernelUTest, BatchWriteFailsWhenRtsqTailUpdateFails) {
  FabricMemAicpuTransferDesc desc{0x1000U, 0x2000U, 16U};
  auto param = MakeParam(&desc, 1U, FabricMemAicpuTransferDirection::kWrite);
  g_config_result = 1;

  EXPECT_EQ(HixlFabricMemBatchWrite(&param), 1U);
  EXPECT_EQ(g_config_count, 1U);
  EXPECT_TRUE(g_sdma_tasks.empty());
}

TEST_F(FabricMemAicpuKernelUTest, BatchWriteRejectsInvalidDescriptorBeforeRtsqAccess) {
  std::vector<FabricMemAicpuTransferDesc> descs = {
      {0x1000U, 0x2000U, 16U},
      {0U, 0x4000U, 16U},
  };
  auto param = MakeParam(descs.data(), static_cast<uint32_t>(descs.size()), FabricMemAicpuTransferDirection::kWrite);

  EXPECT_EQ(HixlFabricMemBatchWrite(&param), 1U);
  EXPECT_EQ(g_query_count, 0U);
  EXPECT_EQ(g_config_count, 0U);
  EXPECT_TRUE(g_sdma_tasks.empty());
}

TEST_F(FabricMemAicpuKernelUTest, BatchEntryRejectsMismatchedDirection) {
  FabricMemAicpuTransferDesc desc{0x1000U, 0x2000U, 16U};
  auto param = MakeParam(&desc, 1U, FabricMemAicpuTransferDirection::kWrite);

  EXPECT_EQ(HixlFabricMemBatchRead(&param), 1U);
  EXPECT_EQ(g_query_count, 0U);
  EXPECT_TRUE(g_sdma_tasks.empty());
}
}  // namespace
}  // namespace hixl
