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
#include <utility>
#include "gtest/gtest.h"
#include "cs/hixl_cs.h"
#include "hixl/hixl_types.h"
#include "hixl_kernel/hixl_batch_transfer.h"
#include "proxy/hcomm/hcomm_res_defs.h"
#include "hccl/hccl_types.h"

// Mock 控制变量
static int32_t g_mock_batch_transfer_ret = 0;
static uint32_t g_mock_batch_transfer_call_count = 0;

// Mock 函数覆盖弱引用
extern "C" int32_t HcommBatchTransferOnThread(ThreadHandle thread, ChannelHandle channel,
    const HcommBatchTransferDesc *transfer_descs, uint32_t transfer_desc_num) {
  (void)thread;
  (void)channel;
  (void)transfer_descs;
  (void)transfer_desc_num;
  g_mock_batch_transfer_call_count++;
  return g_mock_batch_transfer_ret;
}

namespace {
template <size_t kN>
struct TestArgs {
  HixlOneSideOpParam param;
  std::array<HixlOneSideOpDesc, kN> ops;
};

template <size_t kN>
TestArgs<kN> CreateTestArgs(
    std::array<std::array<uint8_t, 8>, kN> &src_buffers,
    std::array<std::array<uint8_t, 8>, kN> &dst_buffers,
    std::array<uint64_t, kN> &lens_storage, uint64_t remote_flag_addr,
    uint64_t local_flag_addr, ThreadHandle thread = 0ULL,
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

class HixlKernelBasicTest : public ::testing::Test {
 protected:
  void SetUp() override {
    g_mock_batch_transfer_ret = HCCL_E_NOT_SUPPORT;
    g_mock_batch_transfer_call_count = 0;
  }

  void TearDown() override {
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

  auto args = CreateTestArgs<3>(local_addr, remote_addr, lens_storage, remote_flag_addr, local_flag_addr);
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

  auto args = CreateTestArgs<3>(remote_addr, local_addr, lens_storage, remote_flag_addr, local_flag_addr);
  uint32_t ret = HixlBatchGet(&args.param);
  EXPECT_EQ(ret, SUCCESS);
}

TEST_F(HixlKernelBasicTest, BatchPutFailByMemSize) {
  std::array<std::array<uint8_t, 8>, 1> local_src{};
  std::array<std::array<uint8_t, 8>, 1> remote_addr{};
  std::array<uint64_t, 1> lens_storage{0};

  uint64_t remote_flag_addr = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&g_remote_flag_buf));
  uint64_t local_flag_addr = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&g_local_flag_buf));

  auto args = CreateTestArgs<1>(local_src, remote_addr, lens_storage, remote_flag_addr, local_flag_addr);
  uint32_t ret = HixlBatchPut(&args.param);
  EXPECT_EQ(ret, FAILED);
}

TEST_F(HixlKernelBasicTest, BatchGetFailByMemSize) {
  std::array<std::array<uint8_t, 8>, 1> remote_addr{};
  std::array<std::array<uint8_t, 8>, 1> local_addr{};
  std::array<uint64_t, 1> lens_storage{0};

  uint64_t remote_flag_addr = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&g_remote_flag_buf));
  uint64_t local_flag_addr = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&g_local_flag_buf));

  auto args = CreateTestArgs<1>(remote_addr, local_addr, lens_storage, remote_flag_addr, local_flag_addr);
  uint32_t ret = HixlBatchGet(&args.param);
  EXPECT_EQ(ret, FAILED);
}

int32_t HcommAclrtNotifyRecordOnThread(ThreadHandle thread, uint64_t dstNotifyId)
{
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

  auto args = CreateTestArgs<3>(remote_addr_hccs, local_addr_hccs, lens_storage,
    remote_flag_addr_hccs, local_flag_addr_hccs);
  args.param.use_notify_record = 1;
  uint32_t ret = HixlBatchGet(&args.param);
  EXPECT_NE(ret, SUCCESS);
}

class HixlBatchTransferTest : public ::testing::Test {
 protected:
  void SetUp() override {
    g_mock_batch_transfer_ret = 0;
    g_mock_batch_transfer_call_count = 0;
  }

  void TearDown() override {
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

  auto args = CreateTestArgs<3>(local_addr, remote_addr, lens_storage, remote_flag_addr, local_flag_addr);

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

  auto args = CreateTestArgs<3>(local_addr, remote_addr, lens_storage, remote_flag_addr, local_flag_addr);

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

  auto args = CreateTestArgs<3>(local_addr, remote_addr, lens_storage, remote_flag_addr, local_flag_addr);

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

  auto args = CreateTestArgs<3>(remote_addr, local_addr, lens_storage, remote_flag_addr, local_flag_addr);

  g_mock_batch_transfer_ret = HCCL_SUCCESS;

  uint32_t ret = HixlBatchGet(&args.param);
  EXPECT_EQ(ret, SUCCESS);
  EXPECT_EQ(g_mock_batch_transfer_call_count, 1u);
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

  auto args = CreateTestArgs<3>(remote_addr, local_addr, lens_storage, remote_flag_addr, local_flag_addr);

  g_mock_batch_transfer_ret = HCCL_E_NOT_SUPPORT;

  uint32_t ret = HixlBatchGet(&args.param);
  EXPECT_EQ(ret, SUCCESS);
  EXPECT_EQ(g_mock_batch_transfer_call_count, 1u);
}
