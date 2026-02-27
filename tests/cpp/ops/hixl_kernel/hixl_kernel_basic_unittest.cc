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
#include "gtest/gtest.h"
#include "hixl/hixl_types.h"
#include "hixl_kernel/hixl_batch_transfer.h"

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

  return param;
}
}  // namespace

using namespace hixl;

static uint64_t g_remote_flag_buf = 1;  // 真实存储remote flag的内存1
static uint64_t g_local_flag_buf = 0;   // 真实存储local flag的内存0

TEST(HixlKernelBasicTest, BatchPutSuccess) {
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

TEST(HixlKernelBasicTest, BatchGetSuccess) {
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

TEST(HixlKernelBasicTest, BatchPutFailByMemSize) {
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

TEST(HixlKernelBasicTest, BatchGetFailByMemSize) {
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
