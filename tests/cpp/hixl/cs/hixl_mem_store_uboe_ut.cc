/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "gtest/gtest.h"
#include "hixl_mem_store.h"
#include "hixl/hixl_types.h"

namespace hixl {
namespace {
void* IntToPtr(uint32_t addr) {
  return reinterpret_cast<void*>(static_cast<uintptr_t>(addr));
}
}  // anonymous namespace

// 测试 RecordMemory 的 is_host_mem 和 register_dev_addr 参数
TEST(HixlMemStoreUboeTest, RecordHostMemoryWithDeviceAddr) {
  HixlMemStore store;
  void *host_addr = IntToPtr(100);
  void *dev_addr = IntToPtr(200);
  constexpr size_t kSize = 1024;

  // 记录 host 内存及其设备地址
  EXPECT_EQ(store.RecordMemory(true, host_addr, kSize, true, dev_addr), SUCCESS);

  // 验证可以通过 FindMemoryRegion 查找
  MemoryRegion region;
  EXPECT_EQ(store.FindMemoryRegion(true, host_addr, region), SUCCESS);
  EXPECT_EQ(region.addr, host_addr);
  EXPECT_EQ(region.size, kSize);
  EXPECT_TRUE(region.is_host_mem);
  EXPECT_EQ(region.register_dev_addr, dev_addr);

  // 注销
  EXPECT_EQ(store.UnrecordMemory(true, host_addr), SUCCESS);
}

TEST(HixlMemStoreUboeTest, RecordDeviceMemoryWithoutDeviceAddr) {
  HixlMemStore store;
  void *dev_addr = IntToPtr(100);
  constexpr size_t kSize = 1024;

  // 记录设备内存（is_host_mem = false）
  EXPECT_EQ(store.RecordMemory(true, dev_addr, kSize, false, nullptr), SUCCESS);

  // 验证
  MemoryRegion region;
  EXPECT_EQ(store.FindMemoryRegion(true, dev_addr, region), SUCCESS);
  EXPECT_EQ(region.addr, dev_addr);
  EXPECT_EQ(region.size, kSize);
  EXPECT_FALSE(region.is_host_mem);
  EXPECT_EQ(region.register_dev_addr, nullptr);

  // 注销
  EXPECT_EQ(store.UnrecordMemory(true, dev_addr), SUCCESS);
}

TEST(HixlMemStoreUboeTest, RecordMemoryWithNullptrDeviceAddr) {
  HixlMemStore store;
  void *host_addr = IntToPtr(100);
  constexpr size_t kSize = 1024;

  // 记录 host 内存，但设备地址为 nullptr
  EXPECT_EQ(store.RecordMemory(true, host_addr, kSize, true, nullptr), SUCCESS);

  // 验证
  MemoryRegion region;
  EXPECT_EQ(store.FindMemoryRegion(true, host_addr, region), SUCCESS);
  EXPECT_EQ(region.addr, host_addr);
  EXPECT_EQ(region.size, kSize);
  EXPECT_TRUE(region.is_host_mem);
  EXPECT_EQ(region.register_dev_addr, nullptr);

  // 注销
  EXPECT_EQ(store.UnrecordMemory(true, host_addr), SUCCESS);
}

TEST(HixlMemStoreUboeTest, FindMemoryRegionSucceeds) {
  HixlMemStore store;
  void *addr = IntToPtr(100);
  constexpr size_t kSize = 2048;

  EXPECT_EQ(store.RecordMemory(true, addr, kSize, true, IntToPtr(200)), SUCCESS);

  MemoryRegion region;
  EXPECT_EQ(store.FindMemoryRegion(true, addr, region), SUCCESS);
  EXPECT_EQ(region.addr, addr);
  EXPECT_EQ(region.size, kSize);
  EXPECT_TRUE(region.is_host_mem);
}

TEST(HixlMemStoreUboeTest, FindMemoryRegionWithinRange) {
  HixlMemStore store;
  void *addr = IntToPtr(100);
  constexpr size_t kSize = 2048;

  EXPECT_EQ(store.RecordMemory(true, addr, kSize), SUCCESS);

  // 查找区域内的地址
  MemoryRegion region;
  void *within_addr = IntToPtr(150);  // 在 [100, 100+2048) 范围内
  EXPECT_EQ(store.FindMemoryRegion(true, within_addr, region), SUCCESS);
  EXPECT_EQ(region.addr, addr);
  EXPECT_EQ(region.size, kSize);
}

TEST(HixlMemStoreUboeTest, FindMemoryRegionOutOfRange) {
  HixlMemStore store;
  void *addr = IntToPtr(100);
  constexpr size_t kSize = 2048;

  EXPECT_EQ(store.RecordMemory(true, addr, kSize), SUCCESS);

  // 查找区域外的地址
  MemoryRegion region;
  void *outside_addr = IntToPtr(3000);  // 在 [100, 100+2048) 范围外
  // 根据代码实现，找不到内存区域时返回 FAILED
  EXPECT_EQ(store.FindMemoryRegion(true, outside_addr, region), FAILED);
}

TEST(HixlMemStoreUboeTest, FindMemoryRegionWithNullptrAddrReturnsInvalidParam) {
  HixlMemStore store;
  MemoryRegion region;
  EXPECT_EQ(store.FindMemoryRegion(true, nullptr, region), PARAM_INVALID);
}

TEST(HixlMemStoreUboeTest, FindMemoryRegionClientSide) {
  HixlMemStore store;
  void *addr = IntToPtr(100);
  constexpr size_t kSize = 2048;

  EXPECT_EQ(store.RecordMemory(false, addr, kSize, false, nullptr), SUCCESS);

  MemoryRegion region;
  EXPECT_EQ(store.FindMemoryRegion(false, addr, region), SUCCESS);
  EXPECT_EQ(region.addr, addr);
  EXPECT_EQ(region.size, kSize);
}

TEST(HixlMemStoreUboeTest, CheckRegionsContiguousWithDeviceAddr) {
  HixlMemStore store;
  // 记录两个连续的 host 内存区域，它们的设备地址也是连续的
  void *host_addr1 = IntToPtr(100);
  void *host_addr2 = IntToPtr(200);  // 连续：100 + 100 = 200
  void *dev_addr1 = IntToPtr(1000);
  void *dev_addr2 = IntToPtr(1100);  // 连续：1000 + 100 = 1100
  constexpr size_t kSize = 100;

  EXPECT_EQ(store.RecordMemory(true, host_addr1, kSize, true, dev_addr1), SUCCESS);
  EXPECT_EQ(store.RecordMemory(true, host_addr2, kSize, true, dev_addr2), SUCCESS);

  // 验证访问跨越这两个区域的地址
  void *test_addr = IntToPtr(150);  // 在第一个区域内
  void *client_addr = IntToPtr(1);
  EXPECT_EQ(store.RecordMemory(false, client_addr, kSize), SUCCESS);

  // 访问成功（两个区域都连续）
  EXPECT_EQ(store.ValidateMemoryAccess(test_addr, 50, client_addr), SUCCESS);
}

TEST(HixlMemStoreUboeTest, CheckRegionsNonContiguousWithDeviceAddr) {
  HixlMemStore store;
  // 记录两个 host 内存区域，地址连续但设备地址不连续
  void *host_addr1 = IntToPtr(100);
  void *host_addr2 = IntToPtr(200);  // 连续
  void *dev_addr1 = IntToPtr(1000);
  void *dev_addr2 = IntToPtr(1200);  // 不连续：1000 + 100 != 1200
  constexpr size_t kSize = 100;

  EXPECT_EQ(store.RecordMemory(true, host_addr1, kSize, true, dev_addr1), SUCCESS);
  EXPECT_EQ(store.RecordMemory(true, host_addr2, kSize, true, dev_addr2), SUCCESS);

  // 尝试访问跨越这两个区域的地址应该失败
  void *test_addr = IntToPtr(150);
  void *client_addr = IntToPtr(1);
  EXPECT_EQ(store.RecordMemory(false, client_addr, kSize * 2), SUCCESS);

  // 因为设备地址不连续，访问失败
  EXPECT_EQ(store.ValidateMemoryAccess(test_addr, 100, client_addr), PARAM_INVALID);
}

TEST(HixlMemStoreUboeTest, CheckRegionsContiguousWithoutDeviceAddr) {
  HixlMemStore store;
  // 记录两个连续的内存区域，没有设备地址
  void *addr1 = IntToPtr(100);
  void *addr2 = IntToPtr(200);
  constexpr size_t kSize = 100;

  EXPECT_EQ(store.RecordMemory(true, addr1, kSize, false, nullptr), SUCCESS);
  EXPECT_EQ(store.RecordMemory(true, addr2, kSize, false, nullptr), SUCCESS);

  // 验证访问跨越这两个区域的地址
  void *test_addr = IntToPtr(150);
  void *client_addr = IntToPtr(1);
  EXPECT_EQ(store.RecordMemory(false, client_addr, kSize * 2), SUCCESS);

  // 成功，因为没有设备地址时，只要地址连续即可
  EXPECT_EQ(store.ValidateMemoryAccess(test_addr, 100, client_addr), SUCCESS);
}

TEST(HixlMemStoreUboeTest, CheckRegionsNonContiguousAddr) {
  HixlMemStore store;
  // 记录两个不连续的内存区域
  void *addr1 = IntToPtr(100);
  void *addr2 = IntToPtr(300);  // 不连续：100 + 100 != 300
  constexpr size_t kSize = 100;

  EXPECT_EQ(store.RecordMemory(true, addr1, kSize), SUCCESS);
  EXPECT_EQ(store.RecordMemory(true, addr2, kSize), SUCCESS);

  // 尝试访问跨越这两个区域的地址应该失败
  void *test_addr = IntToPtr(150);
  void *client_addr = IntToPtr(1);
  EXPECT_EQ(store.RecordMemory(false, client_addr, kSize * 2), SUCCESS);

  // 失败，因为地址不连续
  EXPECT_EQ(store.ValidateMemoryAccess(test_addr, 100, client_addr), PARAM_INVALID);
}

TEST(HixlMemStoreUboeTest, MultipleRegionsWithMixedDeviceAddr) {
  HixlMemStore store;
  // 混合场景：有些有设备地址，有些没有
  void *addr1 = IntToPtr(100);
  void *addr2 = IntToPtr(200);  // 与 addr1 连续
  void *addr3 = IntToPtr(300);  // 与 addr2 连续
  void *dev_addr1 = IntToPtr(1000);
  void *dev_addr2 = IntToPtr(1100);  // 与 dev_addr1 连续
  // addr3 没有设备地址
  constexpr size_t kSize = 100;

  EXPECT_EQ(store.RecordMemory(true, addr1, kSize, true, dev_addr1), SUCCESS);
  EXPECT_EQ(store.RecordMemory(true, addr2, kSize, true, dev_addr2), SUCCESS);
  EXPECT_EQ(store.RecordMemory(true, addr3, kSize, false, nullptr), SUCCESS);

  // 访问跨越 addr1 和 addr2 应该成功
  void *client_addr = IntToPtr(1);
  EXPECT_EQ(store.RecordMemory(false, client_addr, kSize * 2), SUCCESS);
  EXPECT_EQ(store.ValidateMemoryAccess(IntToPtr(150), kSize, client_addr), SUCCESS);
}

TEST(HixlMemStoreUboeTest, FindMemoryRegionUnregistered) {
  HixlMemStore store;
  MemoryRegion region;
  void *unregistered_addr = IntToPtr(100);

  // 根据代码实现，regions 为空或找不到时返回 FAILED
  EXPECT_EQ(store.FindMemoryRegion(true, unregistered_addr, region), FAILED);
}

}  // namespace hixl
