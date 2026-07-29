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
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <utility>
#include "gtest/gtest.h"
#include "cs/hixl_cs.h"
#include "hixl/hixl_types.h"
#include "hixl_kernel/hixl_batch_transfer.h"
#include "hixl_kernel/hixl_sync_transfer_context.h"
#include "hixl_kernel/transfer_context_manager.h"
#include "hixl_kernel/task_exception_handler.h"
#include "proxy/hcomm/hcomm_res_defs.h"
#include "proxy/hcomm/hcomm_exception_notify.h"
#include "hccl/hccl_types.h"
#include "hccl_stub.h"
#include "aicpu_stub.h"
#include "driver/ascend_hal.h"

// Mock 控制变量
static int32_t g_mock_batch_transfer_ret = 0;
static uint32_t g_mock_batch_transfer_call_count = 0;
static int32_t g_hal_esched_submit_ret = 0;
static uint32_t g_hal_esched_submit_call_count = 0;

// Mock 函数覆盖弱引用
extern "C" int32_t HcommBatchTransferOnThread(ThreadHandle thread, ChannelHandle channel,
                                              const HcommBatchTransferDesc *transfer_descs,
                                              uint32_t transfer_desc_num) {
  (void)thread;
  (void)channel;
  (void)transfer_descs;
  (void)transfer_desc_num;
  g_mock_batch_transfer_call_count++;
  return g_mock_batch_transfer_ret;
}

// 覆盖 halEschedSubmitEvent 弱符号
extern "C" drvError_t halEschedSubmitEvent(unsigned int devId, struct event_summary *event) {
  (void)devId;
  (void)event;
  g_hal_esched_submit_call_count++;
  return static_cast<drvError_t>(g_hal_esched_submit_ret);
}

namespace {
template <size_t kN>
struct TestArgs {
  HixlOneSideOpParam param;
  std::array<HixlOneSideOpDesc, kN> ops;
};

template <size_t kN>
TestArgs<kN> CreateTestArgs(std::array<std::array<uint8_t, 8>, kN> &src_buffers,
                            std::array<std::array<uint8_t, 8>, kN> &dst_buffers, std::array<uint64_t, kN> &lens_storage,
                            uint64_t remote_flag_addr, uint64_t local_flag_addr, ThreadHandle thread = 0ULL,
                            ChannelHandle channel = 0ULL) {
  TestArgs<kN> args{};
  args.param.thread = thread;
  args.param.channel = channel;
  args.param.list_num = static_cast<uint32_t>(kN);
  args.param.remote_flag_addr = remote_flag_addr;
  args.param.local_flag_addr = local_flag_addr;
  args.param.flag_size = sizeof(uint64_t);
  args.param.use_notify_record = 0;

  for (size_t i = 0; i < kN; ++i) {
    args.ops[i].remote_buf = dst_buffers[i].data();
    args.ops[i].local_buf = src_buffers[i].data();
    args.ops[i].len = lens_storage[i];
  }
  args.param.op_desc_list_addr = reinterpret_cast<uint64_t>(args.ops.data());

  return args;
}
}  // namespace

using namespace hixl;

static uint64_t g_remote_flag_buf = 1;
static uint64_t g_local_flag_buf = 0;
constexpr ThreadHandle kKernelTestThread = 910001ULL;

static void ResetHalEschedStub() {
  g_hal_esched_submit_ret = 0;
  g_hal_esched_submit_call_count = 0;
}

uint32_t SyncContext(ThreadHandle thread, uint32_t op, uint32_t *state) {
  HixlTransferContextSyncEntry entry{};
  entry.thread = thread;
  entry.op = op;
  uint32_t result_state = TRANSFER_THREAD_STATE_DELETED;
  HixlTransferContextSyncParam param{};
  param.entry_list_addr = reinterpret_cast<uint64_t>(&entry);
  param.state_list_addr = reinterpret_cast<uint64_t>(&result_state);
  param.entry_num = 1U;
  uint32_t ret = HixlSyncTransferContext(&param);
  if (state != nullptr) {
    *state = result_state;
  }
  return ret;
}

class HixlKernelBasicTest : public ::testing::Test {
 protected:
  void SetUp() override {
    g_mock_batch_transfer_ret = HCCL_E_NOT_SUPPORT;
    g_mock_batch_transfer_call_count = 0;
    uint32_t state = TRANSFER_THREAD_STATE_DELETED;
    ASSERT_EQ(SyncContext(kKernelTestThread, TRANSFER_CONTEXT_OP_ADD, &state), SUCCESS);
    ASSERT_EQ(state, TRANSFER_THREAD_STATE_INITIALIZED);
  }

  void TearDown() override {
    uint32_t state = TRANSFER_THREAD_STATE_DELETED;
    (void)SyncContext(kKernelTestThread, TRANSFER_CONTEXT_OP_DELETE, &state);
    g_mock_batch_transfer_ret = HCCL_E_NOT_SUPPORT;
    g_mock_batch_transfer_call_count = 0;
  }
};

TEST_F(HixlKernelBasicTest, BatchPutSuccess) {
  std::array<std::array<uint8_t, 8>, 3> local_addr{};
  std::array<std::array<uint8_t, 8>, 3> remote_addr{};
  std::array<uint64_t, 3> lens_storage{8, 8, 8};

  for (auto &arr : local_addr) {
    std::fill(arr.begin(), arr.end(), 0xAA);
  }
  for (auto &arr : remote_addr) {
    std::fill(arr.begin(), arr.end(), 0xBB);
  }

  uint64_t remote_flag_addr = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&g_remote_flag_buf));
  uint64_t local_flag_addr = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&g_local_flag_buf));

  auto args =
      CreateTestArgs<3>(local_addr, remote_addr, lens_storage, remote_flag_addr, local_flag_addr, kKernelTestThread);
  uint32_t ret = HixlBatchPut(&args.param);
  EXPECT_EQ(ret, SUCCESS);
}

TEST_F(HixlKernelBasicTest, BatchGetSuccess) {
  std::array<std::array<uint8_t, 8>, 3> remote_addr{};
  std::array<std::array<uint8_t, 8>, 3> local_addr{};
  std::array<uint64_t, 3> lens_storage{8, 8, 8};

  for (auto &arr : remote_addr) {
    std::fill(arr.begin(), arr.end(), 0xAA);
  }
  for (auto &arr : local_addr) {
    std::fill(arr.begin(), arr.end(), 0xBB);
  }

  uint64_t remote_flag_addr = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&g_remote_flag_buf));
  uint64_t local_flag_addr = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&g_local_flag_buf));

  auto args =
      CreateTestArgs<3>(remote_addr, local_addr, lens_storage, remote_flag_addr, local_flag_addr, kKernelTestThread);
  uint32_t ret = HixlBatchGet(&args.param);
  EXPECT_EQ(ret, SUCCESS);
}

TEST_F(HixlKernelBasicTest, BatchPutFailByMemSize) {
  std::array<std::array<uint8_t, 8>, 1> local_src{};
  std::array<std::array<uint8_t, 8>, 1> remote_addr{};
  std::array<uint64_t, 1> lens_storage{0};

  uint64_t remote_flag_addr = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&g_remote_flag_buf));
  uint64_t local_flag_addr = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&g_local_flag_buf));

  auto args =
      CreateTestArgs<1>(local_src, remote_addr, lens_storage, remote_flag_addr, local_flag_addr, kKernelTestThread);
  uint32_t ret = HixlBatchPut(&args.param);
  EXPECT_EQ(ret, FAILED);
}

TEST_F(HixlKernelBasicTest, BatchGetFailByMemSize) {
  std::array<std::array<uint8_t, 8>, 1> remote_addr{};
  std::array<std::array<uint8_t, 8>, 1> local_addr{};
  std::array<uint64_t, 1> lens_storage{0};

  uint64_t remote_flag_addr = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&g_remote_flag_buf));
  uint64_t local_flag_addr = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&g_local_flag_buf));

  auto args =
      CreateTestArgs<1>(remote_addr, local_addr, lens_storage, remote_flag_addr, local_flag_addr, kKernelTestThread);
  uint32_t ret = HixlBatchGet(&args.param);
  EXPECT_EQ(ret, FAILED);
}

int32_t HcommAclrtNotifyRecordOnThread(ThreadHandle thread, uint64_t dstNotifyId) {
  return 0;
}

TEST_F(HixlKernelBasicTest, BatchGetHccsError) {
  std::array<std::array<uint8_t, 8>, 3> remote_addr_hccs{};
  std::array<std::array<uint8_t, 8>, 3> local_addr_hccs{};
  std::array<uint64_t, 3> lens_storage{8, 8, 8};

  for (auto &arr : remote_addr_hccs) {
    std::fill(arr.begin(), arr.end(), 0xAA);
  }
  for (auto &arr : local_addr_hccs) {
    std::fill(arr.begin(), arr.end(), 0xBB);
  }

  uint64_t remote_flag_addr_hccs = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&g_remote_flag_buf));
  uint64_t local_flag_addr_hccs = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&g_local_flag_buf));

  auto args = CreateTestArgs<3>(remote_addr_hccs, local_addr_hccs, lens_storage, remote_flag_addr_hccs,
                                local_flag_addr_hccs, kKernelTestThread);
  args.param.use_notify_record = 1;
  uint32_t ret = HixlBatchGet(&args.param);
  EXPECT_NE(ret, SUCCESS);
}

class HixlBatchTransferTest : public ::testing::Test {
 protected:
  void SetUp() override {
    g_mock_batch_transfer_ret = 0;
    g_mock_batch_transfer_call_count = 0;
    uint32_t state = TRANSFER_THREAD_STATE_DELETED;
    ASSERT_EQ(SyncContext(kKernelTestThread, TRANSFER_CONTEXT_OP_ADD, &state), SUCCESS);
    ASSERT_EQ(state, TRANSFER_THREAD_STATE_INITIALIZED);
  }

  void TearDown() override {
    uint32_t state = TRANSFER_THREAD_STATE_DELETED;
    (void)SyncContext(kKernelTestThread, TRANSFER_CONTEXT_OP_DELETE, &state);
    g_mock_batch_transfer_ret = 0;
    g_mock_batch_transfer_call_count = 0;
  }
};

TEST_F(HixlBatchTransferTest, BatchTransferSuccess) {
  std::array<std::array<uint8_t, 8>, 3> local_addr{};
  std::array<std::array<uint8_t, 8>, 3> remote_addr{};
  std::array<uint64_t, 3> lens_storage{8, 8, 8};

  for (auto &arr : local_addr) {
    std::fill(arr.begin(), arr.end(), 0x11);
  }
  for (auto &arr : remote_addr) {
    std::fill(arr.begin(), arr.end(), 0xBB);
  }

  uint64_t remote_flag_addr = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&g_remote_flag_buf));
  uint64_t local_flag_addr = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&g_local_flag_buf));

  auto args =
      CreateTestArgs<3>(local_addr, remote_addr, lens_storage, remote_flag_addr, local_flag_addr, kKernelTestThread);

  g_mock_batch_transfer_ret = HCCL_SUCCESS;

  uint32_t ret = HixlBatchPut(&args.param);
  EXPECT_EQ(ret, SUCCESS);
  EXPECT_EQ(g_mock_batch_transfer_call_count, 1u);
}

TEST_F(HixlBatchTransferTest, BatchTransferFallbackToSingle) {
  std::array<std::array<uint8_t, 8>, 3> local_addr{};
  std::array<std::array<uint8_t, 8>, 3> remote_addr{};
  std::array<uint64_t, 3> lens_storage{8, 8, 8};

  for (auto &arr : local_addr) {
    std::fill(arr.begin(), arr.end(), 0x22);
  }
  for (auto &arr : remote_addr) {
    std::fill(arr.begin(), arr.end(), 0xBB);
  }

  uint64_t remote_flag_addr = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&g_remote_flag_buf));
  uint64_t local_flag_addr = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&g_local_flag_buf));

  auto args =
      CreateTestArgs<3>(local_addr, remote_addr, lens_storage, remote_flag_addr, local_flag_addr, kKernelTestThread);

  g_mock_batch_transfer_ret = HCCL_E_NOT_SUPPORT;

  uint32_t ret = HixlBatchPut(&args.param);
  EXPECT_EQ(ret, SUCCESS);
  EXPECT_EQ(g_mock_batch_transfer_call_count, 1u);
}

TEST_F(HixlBatchTransferTest, BatchTransferOtherError) {
  std::array<std::array<uint8_t, 8>, 3> local_addr{};
  std::array<std::array<uint8_t, 8>, 3> remote_addr{};
  std::array<uint64_t, 3> lens_storage{8, 8, 8};

  for (auto &arr : local_addr) {
    std::fill(arr.begin(), arr.end(), 0x33);
  }
  for (auto &arr : remote_addr) {
    std::fill(arr.begin(), arr.end(), 0xBB);
  }

  uint64_t remote_flag_addr = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&g_remote_flag_buf));
  uint64_t local_flag_addr = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&g_local_flag_buf));

  auto args =
      CreateTestArgs<3>(local_addr, remote_addr, lens_storage, remote_flag_addr, local_flag_addr, kKernelTestThread);

  g_mock_batch_transfer_ret = HCCL_E_PARA;

  uint32_t ret = HixlBatchPut(&args.param);
  EXPECT_EQ(ret, FAILED);
  EXPECT_EQ(g_mock_batch_transfer_call_count, 1u);
}

TEST_F(HixlBatchTransferTest, BatchGetSuccess) {
  std::array<std::array<uint8_t, 8>, 3> remote_addr{};
  std::array<std::array<uint8_t, 8>, 3> local_addr{};
  std::array<uint64_t, 3> lens_storage{8, 8, 8};

  for (auto &arr : remote_addr) {
    std::fill(arr.begin(), arr.end(), 0x55);
  }
  for (auto &arr : local_addr) {
    std::fill(arr.begin(), arr.end(), 0xBB);
  }

  uint64_t remote_flag_addr = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&g_remote_flag_buf));
  uint64_t local_flag_addr = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&g_local_flag_buf));

  auto args =
      CreateTestArgs<3>(remote_addr, local_addr, lens_storage, remote_flag_addr, local_flag_addr, kKernelTestThread);

  g_mock_batch_transfer_ret = HCCL_SUCCESS;

  uint32_t ret = HixlBatchGet(&args.param);
  EXPECT_EQ(ret, SUCCESS);
  EXPECT_EQ(g_mock_batch_transfer_call_count, 1u);
}

TEST_F(HixlBatchTransferTest, BatchPutNullParam) {
  uint32_t ret = HixlBatchPut(nullptr);
  EXPECT_NE(ret, SUCCESS);
}

TEST_F(HixlBatchTransferTest, BatchGetNullParam) {
  uint32_t ret = HixlBatchGet(nullptr);
  EXPECT_NE(ret, SUCCESS);
}

TEST_F(HixlBatchTransferTest, BatchPutListNumZero) {
  std::array<std::array<uint8_t, 8>, 1> local_addr{};
  std::array<std::array<uint8_t, 8>, 1> remote_addr{};
  std::array<uint64_t, 1> lens_storage{8};

  auto args = CreateTestArgs<1>(local_addr, remote_addr, lens_storage, 0, 0, kKernelTestThread);
  args.param.list_num = 0;

  uint32_t ret = HixlBatchPut(&args.param);
  EXPECT_NE(ret, SUCCESS);
  EXPECT_EQ(g_mock_batch_transfer_call_count, 0u);
}

TEST_F(HixlBatchTransferTest, BatchGetListNumZero) {
  std::array<std::array<uint8_t, 8>, 1> local_addr{};
  std::array<std::array<uint8_t, 8>, 1> remote_addr{};
  std::array<uint64_t, 1> lens_storage{8};

  auto args = CreateTestArgs<1>(local_addr, remote_addr, lens_storage, 0, 0, kKernelTestThread);
  args.param.list_num = 0;

  uint32_t ret = HixlBatchGet(&args.param);
  EXPECT_NE(ret, SUCCESS);
  EXPECT_EQ(g_mock_batch_transfer_call_count, 0u);
}

TEST_F(HixlBatchTransferTest, BatchPutListNumExceedMax) {
  std::array<std::array<uint8_t, 8>, 1> local_addr{};
  std::array<std::array<uint8_t, 8>, 1> remote_addr{};
  std::array<uint64_t, 1> lens_storage{8};

  auto args = CreateTestArgs<1>(local_addr, remote_addr, lens_storage, 0, 0, kKernelTestThread);
  args.param.list_num = 8193;

  uint32_t ret = HixlBatchPut(&args.param);
  EXPECT_NE(ret, SUCCESS);
  EXPECT_EQ(g_mock_batch_transfer_call_count, 0u);
}

TEST_F(HixlBatchTransferTest, BatchGetListNumExceedMax) {
  std::array<std::array<uint8_t, 8>, 1> local_addr{};
  std::array<std::array<uint8_t, 8>, 1> remote_addr{};
  std::array<uint64_t, 1> lens_storage{8};

  auto args = CreateTestArgs<1>(local_addr, remote_addr, lens_storage, 0, 0, kKernelTestThread);
  args.param.list_num = 8193;

  uint32_t ret = HixlBatchGet(&args.param);
  EXPECT_NE(ret, SUCCESS);
  EXPECT_EQ(g_mock_batch_transfer_call_count, 0u);
}

TEST_F(HixlBatchTransferTest, BatchPutOpDescListAddrNull) {
  std::array<std::array<uint8_t, 8>, 1> local_addr{};
  std::array<std::array<uint8_t, 8>, 1> remote_addr{};
  std::array<uint64_t, 1> lens_storage{8};

  auto args = CreateTestArgs<1>(local_addr, remote_addr, lens_storage, 0, 0);
  args.param.op_desc_list_addr = 0;

  uint32_t ret = HixlBatchPut(&args.param);
  EXPECT_NE(ret, SUCCESS);
  EXPECT_EQ(g_mock_batch_transfer_call_count, 0u);
}

TEST_F(HixlBatchTransferTest, BatchGetOpDescListAddrNull) {
  std::array<std::array<uint8_t, 8>, 1> local_addr{};
  std::array<std::array<uint8_t, 8>, 1> remote_addr{};
  std::array<uint64_t, 1> lens_storage{8};

  auto args = CreateTestArgs<1>(local_addr, remote_addr, lens_storage, 0, 0);
  args.param.op_desc_list_addr = 0;

  uint32_t ret = HixlBatchGet(&args.param);
  EXPECT_NE(ret, SUCCESS);
  EXPECT_EQ(g_mock_batch_transfer_call_count, 0u);
}

TEST_F(HixlBatchTransferTest, BatchGetFallbackToSingle) {
  std::array<std::array<uint8_t, 8>, 3> remote_addr{};
  std::array<std::array<uint8_t, 8>, 3> local_addr{};
  std::array<uint64_t, 3> lens_storage{8, 8, 8};

  for (auto &arr : remote_addr) {
    std::fill(arr.begin(), arr.end(), 0x66);
  }
  for (auto &arr : local_addr) {
    std::fill(arr.begin(), arr.end(), 0xBB);
  }

  uint64_t remote_flag_addr = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&g_remote_flag_buf));
  uint64_t local_flag_addr = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&g_local_flag_buf));

  auto args =
      CreateTestArgs<3>(remote_addr, local_addr, lens_storage, remote_flag_addr, local_flag_addr, kKernelTestThread);

  g_mock_batch_transfer_ret = HCCL_E_NOT_SUPPORT;

  uint32_t ret = HixlBatchGet(&args.param);
  EXPECT_EQ(ret, SUCCESS);
  EXPECT_EQ(g_mock_batch_transfer_call_count, 1u);
}

TEST_F(HixlBatchTransferTest, BatchPutFailsWhenContextMissing) {
  uint32_t state = TRANSFER_THREAD_STATE_DELETED;
  ASSERT_EQ(SyncContext(kKernelTestThread, TRANSFER_CONTEXT_OP_DELETE, &state), SUCCESS);
  ASSERT_EQ(state, TRANSFER_THREAD_STATE_DELETED);

  std::array<std::array<uint8_t, 8>, 1> local_addr{};
  std::array<std::array<uint8_t, 8>, 1> remote_addr{};
  std::array<uint64_t, 1> lens_storage{8};
  auto args = CreateTestArgs<1>(local_addr, remote_addr, lens_storage, 0, 0, kKernelTestThread);

  uint32_t ret = HixlBatchPut(&args.param);
  EXPECT_EQ(ret, FAILED);
  EXPECT_EQ(g_mock_batch_transfer_call_count, 0u);

  ASSERT_EQ(SyncContext(kKernelTestThread, TRANSFER_CONTEXT_OP_ADD, &state), SUCCESS);
}

TEST_F(HixlBatchTransferTest, DeleteMissingContextReturnsDeleted) {
  constexpr ThreadHandle kMissingThread = 910099ULL;
  uint32_t state = TRANSFER_THREAD_STATE_INITIALIZED;
  ASSERT_EQ(SyncContext(kMissingThread, TRANSFER_CONTEXT_OP_DELETE, &state), SUCCESS);
  EXPECT_EQ(state, TRANSFER_THREAD_STATE_DELETED);
}

TEST_F(HixlBatchTransferTest, DeleteBusyContextReturnsDeletingThenDeleted) {
  constexpr ThreadHandle kBusyThread = 910100ULL;
  uint32_t state = TRANSFER_THREAD_STATE_DELETED;
  ASSERT_EQ(SyncContext(kBusyThread, TRANSFER_CONTEXT_OP_ADD, &state), SUCCESS);
  ASSERT_EQ(state, TRANSFER_THREAD_STATE_INITIALIZED);

  auto ctx = TransferContextManager::Instance().Get(kBusyThread);
  ASSERT_NE(ctx, nullptr);

  ctx->lock();
  ASSERT_EQ(SyncContext(kBusyThread, TRANSFER_CONTEXT_OP_DELETE, &state), SUCCESS);
  EXPECT_EQ(state, TRANSFER_THREAD_STATE_DELETING);

  ctx->unlock();
  ASSERT_EQ(SyncContext(kBusyThread, TRANSFER_CONTEXT_OP_DELETE, &state), SUCCESS);
  EXPECT_EQ(state, TRANSFER_THREAD_STATE_DELETED);
}

class HixlSyncTransferContextTest : public ::testing::Test {
 protected:
  void TearDown() override {
    std::vector<ThreadHandle> threads{910200ULL, 910201ULL, 910300ULL};
    for (ThreadHandle thread : threads) {
      uint32_t state = TRANSFER_THREAD_STATE_DELETED;
      (void)SyncContext(thread, TRANSFER_CONTEXT_OP_DELETE, &state);
    }
  }
};

TEST_F(HixlSyncTransferContextTest, NullParamReturnsFailed) {
  EXPECT_EQ(HixlSyncTransferContext(nullptr), FAILED);
}

TEST_F(HixlSyncTransferContextTest, ZeroEntryNumReturnsFailed) {
  HixlTransferContextSyncParam param{};
  param.entry_num = 0U;
  EXPECT_EQ(HixlSyncTransferContext(&param), FAILED);
}

TEST_F(HixlSyncTransferContextTest, ZeroEntryListAddrReturnsFailed) {
  HixlTransferContextSyncParam param{};
  param.entry_num = 1U;
  param.entry_list_addr = 0U;
  EXPECT_EQ(HixlSyncTransferContext(&param), FAILED);
}

TEST_F(HixlSyncTransferContextTest, ZeroStateListAddrReturnsFailed) {
  HixlTransferContextSyncEntry entry{};
  entry.thread = kKernelTestThread;
  entry.op = TRANSFER_CONTEXT_OP_ADD;
  HixlTransferContextSyncParam param{};
  param.entry_num = 1U;
  param.entry_list_addr = reinterpret_cast<uint64_t>(&entry);
  param.state_list_addr = 0U;
  EXPECT_EQ(HixlSyncTransferContext(&param), FAILED);
}

TEST_F(HixlSyncTransferContextTest, InvalidOpReturnsFailed) {
  HixlTransferContextSyncEntry entry{};
  entry.thread = kKernelTestThread;
  entry.op = 99U;
  uint32_t state = 0U;
  HixlTransferContextSyncParam param{};
  param.entry_num = 1U;
  param.entry_list_addr = reinterpret_cast<uint64_t>(&entry);
  param.state_list_addr = reinterpret_cast<uint64_t>(&state);
  EXPECT_EQ(HixlSyncTransferContext(&param), FAILED);
}

TEST_F(HixlSyncTransferContextTest, MultipleEntriesBatch) {
  constexpr uint32_t kEntryCount = 3U;
  HixlTransferContextSyncEntry entries[kEntryCount]{};
  uint32_t states[kEntryCount]{};

  entries[0].thread = 910200ULL;
  entries[0].op = TRANSFER_CONTEXT_OP_ADD;
  entries[0].notify_id = 2U;
  entries[0].err_flag_dev_va = 0x1000ULL;

  entries[1].thread = 910201ULL;
  entries[1].op = TRANSFER_CONTEXT_OP_ADD;

  entries[2].thread = 910200ULL;
  entries[2].op = TRANSFER_CONTEXT_OP_DELETE;

  HixlTransferContextSyncParam param{};
  param.entry_list_addr = reinterpret_cast<uint64_t>(entries);
  param.state_list_addr = reinterpret_cast<uint64_t>(states);
  param.entry_num = kEntryCount;

  EXPECT_EQ(HixlSyncTransferContext(&param), SUCCESS);
  EXPECT_EQ(states[0], TRANSFER_THREAD_STATE_INITIALIZED);
  EXPECT_EQ(states[1], TRANSFER_THREAD_STATE_INITIALIZED);
  EXPECT_EQ(states[2], TRANSFER_THREAD_STATE_DELETED);
}

TEST_F(HixlSyncTransferContextTest, ExtendedFieldsStoredAfterAdd) {
  constexpr ThreadHandle kThread = 910300ULL;
  constexpr uint32_t kNotifyId = 7U;
  constexpr uint64_t kErrFlagDevVa = 0xDEADBEEFULL;

  HixlTransferContextSyncEntry entry{};
  entry.thread = kThread;
  entry.op = TRANSFER_CONTEXT_OP_ADD;
  entry.notify_id = kNotifyId;
  entry.err_flag_dev_va = kErrFlagDevVa;

  uint32_t state = 0U;
  HixlTransferContextSyncParam param{};
  param.entry_list_addr = reinterpret_cast<uint64_t>(&entry);
  param.state_list_addr = reinterpret_cast<uint64_t>(&state);
  param.entry_num = 1U;

  EXPECT_EQ(HixlSyncTransferContext(&param), SUCCESS);
  EXPECT_EQ(state, TRANSFER_THREAD_STATE_INITIALIZED);

  auto ctx = TransferContextManager::Instance().Get(kThread);
  ASSERT_NE(ctx, nullptr);
  EXPECT_EQ(ctx->notify_id, kNotifyId);
  EXPECT_EQ(ctx->err_flag_dev_va, kErrFlagDevVa);
}

// ==================== TransferContext::WriteErrorFlag tests ====================

class WriteErrorFlagTest : public ::testing::Test {
 protected:
  void SetUp() override {
    aicpu_test::ResetAicpuGetContextResult();
    ResetExceptionCallbackStub();
    TaskExceptionHandler::Instance().DisableExceptionCallback();
    ResetExceptionCallbackStub();
    uint32_t state = TRANSFER_THREAD_STATE_DELETED;
    (void)SyncContext(kKernelTestThread, TRANSFER_CONTEXT_OP_DELETE, &state);
    (void)SyncContext(kKernelTestThread, TRANSFER_CONTEXT_OP_ADD, &state);
  }

  void TearDown() override {
    uint32_t state = TRANSFER_THREAD_STATE_DELETED;
    (void)SyncContext(kKernelTestThread, TRANSFER_CONTEXT_OP_DELETE, &state);
    TaskExceptionHandler::Instance().DisableExceptionCallback();
    aicpu_test::ResetAicpuGetContextResult();
    ResetExceptionCallbackStub();
  }
};

TEST_F(WriteErrorFlagTest, WritesOneWhenErrFlagDevVaNonZero) {
  uint8_t err_flag = 0U;
  uint32_t state = TRANSFER_THREAD_STATE_DELETED;
  ASSERT_EQ(SyncContext(kKernelTestThread, TRANSFER_CONTEXT_OP_DELETE, &state), SUCCESS);

  HixlTransferContextSyncEntry entry{};
  entry.thread = kKernelTestThread;
  entry.op = TRANSFER_CONTEXT_OP_ADD;
  entry.err_flag_dev_va = reinterpret_cast<uint64_t>(&err_flag);
  HixlTransferContextSyncParam param{};
  param.entry_list_addr = reinterpret_cast<uint64_t>(&entry);
  param.state_list_addr = reinterpret_cast<uint64_t>(&state);
  param.entry_num = 1U;
  ASSERT_EQ(HixlSyncTransferContext(&param), SUCCESS);

  auto ctx = TransferContextManager::Instance().Get(kKernelTestThread);
  ASSERT_NE(ctx, nullptr);
  ctx->WriteErrorFlag();
  EXPECT_EQ(err_flag, 1U);
}

TEST_F(WriteErrorFlagTest, NoopWhenErrFlagDevVaZero) {
  auto ctx = TransferContextManager::Instance().Get(kKernelTestThread);
  ASSERT_NE(ctx, nullptr);
  EXPECT_EQ(ctx->err_flag_dev_va, 0U);
  ctx->WriteErrorFlag();
  SUCCEED();
}

// ==================== TaskExceptionHandler tests ====================

class TaskExceptionHandlerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    aicpu_test::ResetAicpuGetContextResult();
    ResetExceptionCallbackStub();
    TaskExceptionHandler::Instance().DisableExceptionCallback();
    ResetExceptionCallbackStub();
  }

  void TearDown() override {
    uint32_t state = TRANSFER_THREAD_STATE_DELETED;
    (void)SyncContext(kKernelTestThread, TRANSFER_CONTEXT_OP_DELETE, &state);
    aicpu_test::ResetAicpuGetContextResult();
    ResetExceptionCallbackStub();
  }
};

TEST_F(TaskExceptionHandlerTest, EnableRegistersCallback) {
  TaskExceptionHandler::Instance().EnableExceptionCallback();
  EXPECT_NE(GetRegisteredExceptionCallback(), nullptr);
}

TEST_F(TaskExceptionHandlerTest, EnableTwiceRegistersOnce) {
  TaskExceptionHandler::Instance().EnableExceptionCallback();
  ExceptionCallback first_cb = GetRegisteredExceptionCallback();
  TaskExceptionHandler::Instance().EnableExceptionCallback();
  EXPECT_EQ(GetRegisteredExceptionCallback(), first_cb);
}

TEST_F(TaskExceptionHandlerTest, DisableUnregistersCallback) {
  TaskExceptionHandler::Instance().EnableExceptionCallback();
  EXPECT_NE(GetRegisteredExceptionCallback(), nullptr);
  TaskExceptionHandler::Instance().DisableExceptionCallback();
  EXPECT_EQ(GetRegisteredExceptionCallback(), nullptr);
}

TEST_F(TaskExceptionHandlerTest, DisableTwiceUnregistersOnce) {
  TaskExceptionHandler::Instance().EnableExceptionCallback();
  TaskExceptionHandler::Instance().DisableExceptionCallback();
  EXPECT_EQ(GetRegisteredExceptionCallback(), nullptr);
  TaskExceptionHandler::Instance().DisableExceptionCallback();
  EXPECT_EQ(GetRegisteredExceptionCallback(), nullptr);
}

TEST_F(TaskExceptionHandlerTest, EnableAfterFailureCanRegisterAgain) {
  SetRegisterExceptionResult(HCCL_E_INTERNAL);
  TaskExceptionHandler::Instance().EnableExceptionCallback();
  EXPECT_EQ(GetRegisteredExceptionCallback(), nullptr);

  SetRegisterExceptionResult(0);
  TaskExceptionHandler::Instance().EnableExceptionCallback();
  EXPECT_NE(GetRegisteredExceptionCallback(), nullptr);
}

// ==================== HixlBatchTransfer err_flag tests ====================

class BatchTransferErrFlagTest : public ::testing::Test {
 protected:
  uint8_t err_flag_ = 0U;

  void SetUp() override {
    g_mock_batch_transfer_ret = 0;
    g_mock_batch_transfer_call_count = 0;
    err_flag_ = 0U;
    aicpu_test::ResetAicpuGetContextResult();

    HixlTransferContextSyncEntry entry{};
    entry.thread = kKernelTestThread;
    entry.op = TRANSFER_CONTEXT_OP_ADD;
    entry.err_flag_dev_va = reinterpret_cast<uint64_t>(&err_flag_);
    uint32_t state = 0U;
    HixlTransferContextSyncParam param{};
    param.entry_list_addr = reinterpret_cast<uint64_t>(&entry);
    param.state_list_addr = reinterpret_cast<uint64_t>(&state);
    param.entry_num = 1U;
    ASSERT_EQ(HixlSyncTransferContext(&param), SUCCESS);
  }

  void TearDown() override {
    uint32_t state = TRANSFER_THREAD_STATE_DELETED;
    (void)SyncContext(kKernelTestThread, TRANSFER_CONTEXT_OP_DELETE, &state);
    TaskExceptionHandler::Instance().DisableExceptionCallback();
    g_mock_batch_transfer_ret = HCCL_E_NOT_SUPPORT;
    g_mock_batch_transfer_call_count = 0;
  }
};

TEST_F(BatchTransferErrFlagTest, ErrFlagStaysZeroOnSuccess) {
  std::array<std::array<uint8_t, 8>, 1> local_addr{};
  std::array<std::array<uint8_t, 8>, 1> remote_addr{};
  std::array<uint64_t, 1> lens{8};

  auto args = CreateTestArgs<1>(local_addr, remote_addr, lens, reinterpret_cast<uint64_t>(&g_remote_flag_buf),
                                reinterpret_cast<uint64_t>(&g_local_flag_buf), kKernelTestThread);
  g_mock_batch_transfer_ret = HCCL_SUCCESS;

  EXPECT_EQ(HixlBatchPut(&args.param), SUCCESS);
  EXPECT_EQ(err_flag_, 0U);
}

TEST_F(BatchTransferErrFlagTest, ErrFlagWrittenOnTransferFailure) {
  std::array<std::array<uint8_t, 8>, 1> local_addr{};
  std::array<std::array<uint8_t, 8>, 1> remote_addr{};
  std::array<uint64_t, 1> lens{8};

  auto args = CreateTestArgs<1>(local_addr, remote_addr, lens, reinterpret_cast<uint64_t>(&g_remote_flag_buf),
                                reinterpret_cast<uint64_t>(&g_local_flag_buf), kKernelTestThread);
  g_mock_batch_transfer_ret = HCCL_E_PARA;

  EXPECT_EQ(HixlBatchPut(&args.param), FAILED);
  EXPECT_EQ(err_flag_, 1U);
}

TEST_F(BatchTransferErrFlagTest, ErrFlagWrittenOnFenceFailure) {
  std::array<std::array<uint8_t, 8>, 1> local_addr{};
  std::array<std::array<uint8_t, 8>, 1> remote_addr{};
  std::array<uint64_t, 1> lens{8};

  auto args = CreateTestArgs<1>(local_addr, remote_addr, lens, reinterpret_cast<uint64_t>(&g_remote_flag_buf),
                                reinterpret_cast<uint64_t>(&g_local_flag_buf), kKernelTestThread);
  g_mock_batch_transfer_ret = HCCL_SUCCESS;
  SetNextFenceFailure(HCCL_E_INTERNAL);

  EXPECT_EQ(HixlBatchPut(&args.param), FAILED);
  EXPECT_EQ(err_flag_, 1U);
}

// ==================== HixlTaskExceptionCallback tests ====================

class ExceptionCallbackTest : public ::testing::Test {
 protected:
  uint8_t err_flag_ = 0U;

  void SetUp() override {
    aicpu_test::ResetAicpuGetContextResult();
    aicpu_test::SetAicpuGetContextResult(7, 0, 12345, 0, 0);
    ResetExceptionCallbackStub();
    ResetHalEschedStub();
    TaskExceptionHandler::Instance().DisableExceptionCallback();
    ResetExceptionCallbackStub();
    err_flag_ = 0U;
  }

  void TearDown() override {
    uint32_t state = TRANSFER_THREAD_STATE_DELETED;
    (void)SyncContext(kExceptionThread, TRANSFER_CONTEXT_OP_DELETE, &state);
    TaskExceptionHandler::Instance().DisableExceptionCallback();
    aicpu_test::ResetAicpuGetContextResult();
    ResetExceptionCallbackStub();
    ResetHalEschedStub();
  }

  static constexpr ThreadHandle kExceptionThread = 910400ULL;

  void AddContextWithErrFlag() {
    HixlTransferContextSyncEntry entry{};
    entry.thread = kExceptionThread;
    entry.op = TRANSFER_CONTEXT_OP_ADD;
    entry.err_flag_dev_va = reinterpret_cast<uint64_t>(&err_flag_);
    entry.notify_id = 3U;
    uint32_t state = 0U;
    HixlTransferContextSyncParam param{};
    param.entry_list_addr = reinterpret_cast<uint64_t>(&entry);
    param.state_list_addr = reinterpret_cast<uint64_t>(&state);
    param.entry_num = 1U;
    ASSERT_EQ(HixlSyncTransferContext(&param), SUCCESS);
  }

  void TriggerCallback(uint64_t thread, uint32_t ret_code) {
    ExceptionCallback cb = GetRegisteredExceptionCallback();
    ASSERT_NE(cb, nullptr);
    HcommExceptionInfo info{};
    info.thread = thread;
    info.retCode = ret_code;
    cb(&info, GetRegisteredExceptionUserData());
  }

  void TriggerCallbackStars(uint64_t thread, uint32_t ret_code, uint32_t stars_errcode, uint8_t seq_type,
                            uint8_t status_merged) {
    ExceptionCallback cb = GetRegisteredExceptionCallback();
    ASSERT_NE(cb, nullptr);
    HcommExceptionInfo info{};
    info.thread = thread;
    info.retCode = ret_code;
    info.expandInfo.type = HCOMM_EXCEPTION_STARS;
    info.expandInfo.detail.starsInfo.starsErrcode = stars_errcode;
    info.expandInfo.detail.starsInfo.seqType = seq_type;
    info.expandInfo.detail.starsInfo.statusMerged = status_merged;
    cb(&info, GetRegisteredExceptionUserData());
  }
};

TEST_F(ExceptionCallbackTest, CallbackWritesErrFlagForHixlThread) {
  AddContextWithErrFlag();
  TaskExceptionHandler::Instance().EnableExceptionCallback();
  TriggerCallback(kExceptionThread, HCCL_E_ROCE_TRANSFER);
  EXPECT_EQ(err_flag_, 1U);
}

TEST_F(ExceptionCallbackTest, CallbackSkipsNonHixlThread) {
  AddContextWithErrFlag();
  TaskExceptionHandler::Instance().EnableExceptionCallback();
  TriggerCallback(999999ULL, HCCL_E_ROCE_TRANSFER);
  EXPECT_EQ(err_flag_, 0U);
}

TEST_F(ExceptionCallbackTest, CallbackHandlesNullExceptionInfo) {
  AddContextWithErrFlag();
  TaskExceptionHandler::Instance().EnableExceptionCallback();
  ExceptionCallback cb = GetRegisteredExceptionCallback();
  ASSERT_NE(cb, nullptr);
  cb(nullptr, GetRegisteredExceptionUserData());
  EXPECT_EQ(err_flag_, 0U);
}

TEST_F(ExceptionCallbackTest, CallbackSkipsDeletingThread) {
  AddContextWithErrFlag();
  TaskExceptionHandler::Instance().EnableExceptionCallback();

  auto ctx = TransferContextManager::Instance().Get(kExceptionThread);
  ASSERT_NE(ctx, nullptr);
  ctx->lock();
  ctx->SetState(TRANSFER_THREAD_STATE_DELETING);
  ctx->unlock();

  TriggerCallback(kExceptionThread, HCCL_E_ROCE_TRANSFER);
  EXPECT_EQ(err_flag_, 0U);
}

TEST_F(ExceptionCallbackTest, CallbackNotifiesTsfwOnSuccess) {
  AddContextWithErrFlag();
  TaskExceptionHandler::Instance().EnableExceptionCallback();
  ResetHalEschedStub();

  TriggerCallback(kExceptionThread, HCCL_E_ROCE_TRANSFER);
  EXPECT_EQ(err_flag_, 1U);
  EXPECT_EQ(g_hal_esched_submit_call_count, 1U);
}

TEST_F(ExceptionCallbackTest, CallbackTsfwFailureStillWritesErrFlag) {
  AddContextWithErrFlag();
  TaskExceptionHandler::Instance().EnableExceptionCallback();
  g_hal_esched_submit_ret = 1;

  TriggerCallback(kExceptionThread, HCCL_E_ROCE_TRANSFER);
  EXPECT_EQ(err_flag_, 1U);
  EXPECT_EQ(g_hal_esched_submit_call_count, 1U);
}

TEST_F(ExceptionCallbackTest, CallbackTsfwFailureWithAicpuGetContextFailure) {
  AddContextWithErrFlag();
  TaskExceptionHandler::Instance().EnableExceptionCallback();
  aicpu_test::SetAicpuGetContextResult(0, 0, 0, 0, 1);
  ResetHalEschedStub();

  TriggerCallback(kExceptionThread, HCCL_E_ROCE_TRANSFER);
  EXPECT_EQ(err_flag_, 1U);
  EXPECT_EQ(g_hal_esched_submit_call_count, 0U);
}

TEST_F(ExceptionCallbackTest, CallbackWithStarsExceptionWritesErrFlagAndNotifiesTsfw) {
  AddContextWithErrFlag();
  TaskExceptionHandler::Instance().EnableExceptionCallback();
  ResetHalEschedStub();

  TriggerCallbackStars(kExceptionThread, HCCL_E_ROCE_TRANSFER, 0x1234, 1, 2);
  EXPECT_EQ(err_flag_, 1U);
  EXPECT_EQ(g_hal_esched_submit_call_count, 1U);
}

TEST_F(ExceptionCallbackTest, CallbackWithStarsExceptionNonHixlThreadSkips) {
  AddContextWithErrFlag();
  TaskExceptionHandler::Instance().EnableExceptionCallback();
  ResetHalEschedStub();

  TriggerCallbackStars(999999ULL, HCCL_E_ROCE_TRANSFER, 0x5678, 3, 4);
  EXPECT_EQ(err_flag_, 0U);
  EXPECT_EQ(g_hal_esched_submit_call_count, 0U);
}
