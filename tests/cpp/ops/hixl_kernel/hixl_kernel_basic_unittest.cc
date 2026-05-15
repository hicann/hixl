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
#include <algorithm>  // 补充：确保std::fill能正常使用
#include <cstdint>
#include "gtest/gtest.h"
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
HixlOneSideOpParam CreateTestParamFixed(std::array<std::array<uint8_t, 8>, kN> &src_buffers,
                                        std::array<std::array<uint8_t, 8>, kN> &dst_buffers,
                                        std::array<uint64_t, kN> &lens_storage, uint64_t remote_flag_addr,
                                        uint64_t local_flag_addr, ThreadHandle thread = 0ULL,
                                        ChannelHandle channel = 0ULL) {
  // 注意：移除static！static会导致多测试用例共享数组，引发数据污染
  static std::array<void *, kN> src_ptrs;
  static std::array<void *, kN> dst_ptrs;
  static std::array<uint64_t, kN> lens;

  for (size_t i = 0; i < kN; ++i) {
    src_ptrs[i] = src_buffers[i].data();
    dst_ptrs[i] = dst_buffers[i].data();
    lens[i] = lens_storage[i];
  }

  HixlOneSideOpParam param{};
  param.thread = thread;
  param.channel = channel;
  param.list_num = static_cast<uint32_t>(kN);
  param.dst_buf_addr_list = dst_ptrs.data();
  param.src_buf_addr_list = src_ptrs.data();
  param.len_list = lens.data();
  param.remote_flag_addr = remote_flag_addr;
  param.local_flag_addr = local_flag_addr;
  param.flag_size = sizeof(uint64_t);
  param.protocol = COMM_PROTOCOL_RESERVED; // skip hccs
  return param;
}
}  // namespace

using namespace hixl;

static uint64_t g_remote_flag_buf = 1;  // 真实存储remote flag的内存1
static uint64_t g_local_flag_buf = 0;   // 真实存储local flag的内存0

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
  // 模拟本地源数据和远端目标缓冲区
  std::array<std::array<uint8_t, 8>, 3> local_addr{};
  std::array<std::array<uint8_t, 8>, 3> remote_addr{};
  std::array<uint64_t, 3> lens_storage{8, 8, 8};

  for (auto &arr : local_addr) {
    std::fill(arr.begin(), arr.end(), 0xAA);
  }
  for (auto &arr : remote_addr) {
    std::fill(arr.begin(), arr.end(), 0xBB);
  }

  // ========== 核心修改1：使用真实的合法内存地址 ==========
  uint64_t remote_flag_addr = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&g_remote_flag_buf));
  uint64_t local_flag_addr = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&g_local_flag_buf));

  // ========== 核心修改2：直接传递地址值，不再取& ==========
  auto param = CreateTestParamFixed<3>(local_addr, remote_addr, lens_storage,
                                       remote_flag_addr,  // 直接传值，无需二次转换
                                       local_flag_addr);  // 直接传值，无需二次转换
  uint32_t ret = HixlBatchPut(&param);
  EXPECT_EQ(ret, SUCCESS);
}

TEST_F(HixlKernelBasicTest, BatchGetSuccess) {
  // 模拟远端源数据和本地目标缓冲区
  std::array<std::array<uint8_t, 8>, 3> remote_addr{};
  std::array<std::array<uint8_t, 8>, 3> local_addr{};
  std::array<uint64_t, 3> lens_storage{8, 8, 8};

  for (auto &arr : remote_addr) {
    std::fill(arr.begin(), arr.end(), 0xAA);
  }
  for (auto &arr : local_addr) {
    std::fill(arr.begin(), arr.end(), 0xBB);
  }

  // ========== 核心修改1：使用真实的合法内存地址 ==========
  uint64_t remote_flag_addr = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&g_remote_flag_buf));
  uint64_t local_flag_addr = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&g_local_flag_buf));

  // ========== 核心修改2：直接传递地址值 ==========
  auto param = CreateTestParamFixed<3>(remote_addr, local_addr, lens_storage, remote_flag_addr, local_flag_addr);
  uint32_t ret = HixlBatchGet(&param);
  EXPECT_EQ(ret, SUCCESS);
}

TEST_F(HixlKernelBasicTest, BatchPutFailByMemSize) {
  std::array<std::array<uint8_t, 8>, 1> local_src{};
  std::array<std::array<uint8_t, 8>, 1> remote_addr{};
  std::array<uint64_t, 1> lens_storage{0};  // 内存大小为0，触发失败

  // ========== 核心修改1：使用真实的合法内存地址 ==========
  uint64_t remote_flag_addr = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&g_remote_flag_buf));
  uint64_t local_flag_addr = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&g_local_flag_buf));

  // ========== 核心修改2：直接传递地址值 ==========
  auto param = CreateTestParamFixed<1>(local_src, remote_addr, lens_storage, remote_flag_addr, local_flag_addr);

  uint32_t ret = HixlBatchPut(&param);
  EXPECT_EQ(ret, FAILED);
}

TEST_F(HixlKernelBasicTest, BatchGetFailByMemSize) {
  std::array<std::array<uint8_t, 8>, 1> remote_addr{};
  std::array<std::array<uint8_t, 8>, 1> local_addr{};
  std::array<uint64_t, 1> lens_storage{0};  // 内存大小为0，触发失败

  // ========== 核心修改1：使用真实的合法内存地址 ==========
  uint64_t remote_flag_addr = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&g_remote_flag_buf));
  uint64_t local_flag_addr = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&g_local_flag_buf));

  // ========== 核心修改2：直接传递地址值 ==========
  auto param = CreateTestParamFixed<1>(remote_addr, local_addr, lens_storage, remote_flag_addr, local_flag_addr);

  uint32_t ret = HixlBatchGet(&param);
  EXPECT_EQ(ret, FAILED);
}

int32_t HcommAclrtNotifyRecordOnThread(ThreadHandle thread, uint64_t dstNotifyId)
{
  return 0;
}

TEST_F(HixlKernelBasicTest, BatchGetHccsError) {
  // 模拟远端源数据和本地目标缓冲区
  std::array<std::array<uint8_t, 8>, 3> remote_addr_hccs{};
  std::array<std::array<uint8_t, 8>, 3> local_addr_hccs{};
  std::array<uint64_t, 3> lens_storage{8, 8, 8};

  for (auto &arr : remote_addr_hccs) {
    std::fill(arr.begin(), arr.end(), 0xAA);
  }
  for (auto &arr : local_addr_hccs) {
    std::fill(arr.begin(), arr.end(), 0xBB);
  }

  // ========== 核心修改1：使用真实的合法内存地址 ==========
  uint64_t remote_flag_addr_hccs = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&g_remote_flag_buf));
  uint64_t local_flag_addr_hccs = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&g_local_flag_buf));

  // ========== 核心修改2：直接传递地址值 ==========
  auto param = CreateTestParamFixed<3>(remote_addr_hccs, local_addr_hccs, lens_storage,
    remote_flag_addr_hccs, local_flag_addr_hccs);
  param.protocol = COMM_PROTOCOL_HCCS;
  uint32_t ret = HixlBatchGet(&param);
  EXPECT_NE(ret, SUCCESS);
}

// ========== 批量传输接口测试用例 ==========

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

// 测试批量接口成功的情况
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

  auto param = CreateTestParamFixed<3>(local_addr, remote_addr, lens_storage, remote_flag_addr, local_flag_addr);

  // 设置 mock 返回成功
  g_mock_batch_transfer_ret = HCCL_SUCCESS;

  uint32_t ret = HixlBatchPut(&param);
  EXPECT_EQ(ret, SUCCESS);
  // 验证批量接口被调用
  EXPECT_EQ(g_mock_batch_transfer_call_count, 1u);
}

// 测试批量接口不支持时 fallback 到单次调用
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

  auto param = CreateTestParamFixed<3>(local_addr, remote_addr, lens_storage, remote_flag_addr, local_flag_addr);

  // 设置 mock 返回不支持，触发 fallback
  g_mock_batch_transfer_ret = HCCL_E_NOT_SUPPORT;

  uint32_t ret = HixlBatchPut(&param);
  EXPECT_EQ(ret, SUCCESS);
  // 验证批量接口被调用一次（尝试），然后 fallback
  EXPECT_EQ(g_mock_batch_transfer_call_count, 1u);
}

// 测试批量接口返回其他错误时失败
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

  auto param = CreateTestParamFixed<3>(local_addr, remote_addr, lens_storage, remote_flag_addr, local_flag_addr);

  // 设置 mock 返回其他错误（如参数错误）
  g_mock_batch_transfer_ret = HCCL_E_PARA;

  uint32_t ret = HixlBatchPut(&param);
  EXPECT_EQ(ret, FAILED);
  // 验证批量接口被调用一次
  EXPECT_EQ(g_mock_batch_transfer_call_count, 1u);
}

// 测试 BatchGet 批量接口成功
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

  auto param = CreateTestParamFixed<3>(remote_addr, local_addr, lens_storage, remote_flag_addr, local_flag_addr);

  g_mock_batch_transfer_ret = HCCL_SUCCESS;

  uint32_t ret = HixlBatchGet(&param);
  EXPECT_EQ(ret, SUCCESS);
  EXPECT_EQ(g_mock_batch_transfer_call_count, 1u);
}

// 测试 BatchGet 批量接口不支持时 fallback
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

  auto param = CreateTestParamFixed<3>(remote_addr, local_addr, lens_storage, remote_flag_addr, local_flag_addr);

  g_mock_batch_transfer_ret = HCCL_E_NOT_SUPPORT;

  uint32_t ret = HixlBatchGet(&param);
  EXPECT_EQ(ret, SUCCESS);
  EXPECT_EQ(g_mock_batch_transfer_call_count, 1u);
}
