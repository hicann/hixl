/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <gtest/gtest.h>
#include <memory>
#include "host_register_proxy.h"
#include "hixl/hixl_types.h"

namespace hixl {

namespace {

constexpr uint64_t kTestMemSize = 1024U;

// 使用静态变量生成唯一的设备 ID
int32_t GetUniqueDevPhyId() {
  static int32_t dev_id = 1000;  // 从1000开始，避免与0冲突
  return dev_id++;
}

}  // namespace

class HostRegisterProxyTest : public ::testing::Test {
 protected:
  void TearDown() override {}
};

// 测试成功注册 host 内存
TEST_F(HostRegisterProxyTest, RegisterSuccess) {
  const int32_t dev_phy_id = GetUniqueDevPhyId();
  int32_t test_data = 42;
  void *host_addr = &test_data;
  void *device_addr = nullptr;

  // 默认 stub 会返回 SUCCESS
  Status ret = HostRegisterProxy::RegisterByDev(dev_phy_id, host_addr, kTestMemSize, device_addr);

  EXPECT_EQ(ret, SUCCESS);
  EXPECT_NE(device_addr, nullptr);  // 默认 stub 返回 ptr 本身作为 dev_addr
}

// 测试成功注销
TEST_F(HostRegisterProxyTest, UnregisterSuccess) {
  const int32_t dev_phy_id = GetUniqueDevPhyId();
  int32_t test_data = 42;
  void *host_addr = &test_data;
  void *device_addr = nullptr;

  // 先注册
  ASSERT_EQ(HostRegisterProxy::RegisterByDev(dev_phy_id, host_addr, kTestMemSize, device_addr), SUCCESS);

  // 再注销
  Status ret = HostRegisterProxy::UnregisterByDev(dev_phy_id, host_addr);
  EXPECT_EQ(ret, SUCCESS);
}

// 测试注销未注册的地址（应该返回 SUCCESS）
TEST_F(HostRegisterProxyTest, UnregisterUnregisteredAddress) {
  const int32_t dev_phy_id = GetUniqueDevPhyId();
  int32_t test_data = 42;
  void *host_addr = &test_data;

  // 尝试注销未注册的地址
  // 根据代码实现，未注册的地址会直接返回 SUCCESS（不会调用 aclrtHostUnregister）
  Status ret = HostRegisterProxy::UnregisterByDev(dev_phy_id, host_addr);
  EXPECT_EQ(ret, SUCCESS);
}

// 测试获取已注册的设备地址成功
TEST_F(HostRegisterProxyTest, GetRegisteredDeviceAddrSuccess) {
  const int32_t dev_phy_id = GetUniqueDevPhyId();
  int32_t test_data = 42;
  void *host_addr = &test_data;
  void *device_addr = nullptr;

  // 先注册
  ASSERT_EQ(HostRegisterProxy::RegisterByDev(dev_phy_id, host_addr, kTestMemSize, device_addr), SUCCESS);
  ASSERT_NE(device_addr, nullptr);

  // 获取设备地址
  void *retrieved_dev_addr = nullptr;
  Status ret = HostRegisterProxy::GetRegisteredDeviceAddrByDev(dev_phy_id, host_addr, retrieved_dev_addr);

  EXPECT_EQ(ret, SUCCESS);
  EXPECT_EQ(retrieved_dev_addr, device_addr);
}

// 测试获取未注册地址的设备地址失败
TEST_F(HostRegisterProxyTest, GetRegisteredDeviceAddrFailOnUnregistered) {
  const int32_t dev_phy_id = GetUniqueDevPhyId();
  int32_t test_data = 42;
  void *host_addr = &test_data;
  void *device_addr = nullptr;

  // 直接获取未注册地址的设备地址
  Status ret = HostRegisterProxy::GetRegisteredDeviceAddrByDev(dev_phy_id, host_addr, device_addr);

  EXPECT_NE(ret, SUCCESS);
}

// 测试空指针参数
TEST_F(HostRegisterProxyTest, RegisterFailOnNullptr) {
  const int32_t dev_phy_id = GetUniqueDevPhyId();
  void *device_addr = nullptr;

  Status ret = HostRegisterProxy::RegisterByDev(dev_phy_id, nullptr, kTestMemSize, device_addr);

  EXPECT_EQ(ret, PARAM_INVALID);
}

// 测试多个地址注册到同一设备
TEST_F(HostRegisterProxyTest, RegisterMultipleAddresses) {
  const int32_t dev_phy_id = GetUniqueDevPhyId();
  int32_t test_data1 = 42;
  int32_t test_data2 = 43;
  void *host_addr1 = &test_data1;
  void *host_addr2 = &test_data2;
  void *device_addr1 = nullptr;
  void *device_addr2 = nullptr;

  // 注册第一个地址
  Status ret1 = HostRegisterProxy::RegisterByDev(dev_phy_id, host_addr1, kTestMemSize, device_addr1);
  EXPECT_EQ(ret1, SUCCESS);
  EXPECT_NE(device_addr1, nullptr);

  // 注册第二个地址
  Status ret2 = HostRegisterProxy::RegisterByDev(dev_phy_id, host_addr2, kTestMemSize, device_addr2);
  EXPECT_EQ(ret2, SUCCESS);
  EXPECT_NE(device_addr2, nullptr);
}

// 测试重复注册同一地址（应该返回缓存的设备地址）
TEST_F(HostRegisterProxyTest, RegisterSameAddressTwice) {
  const int32_t dev_phy_id = GetUniqueDevPhyId();
  int32_t test_data = 42;
  void *host_addr = &test_data;
  void *device_addr1 = nullptr;
  void *device_addr2 = nullptr;

  // 第一次注册
  Status ret1 = HostRegisterProxy::RegisterByDev(dev_phy_id, host_addr, kTestMemSize, device_addr1);
  EXPECT_EQ(ret1, SUCCESS);
  ASSERT_NE(device_addr1, nullptr);

  // 第二次注册同一地址（应返回缓存的地址）
  Status ret2 = HostRegisterProxy::RegisterByDev(dev_phy_id, host_addr, kTestMemSize, device_addr2);
  EXPECT_EQ(ret2, SUCCESS);
  EXPECT_EQ(device_addr2, device_addr1);  // 应该返回相同的设备地址
}

// 测试不同设备 ID 的注册
TEST_F(HostRegisterProxyTest, RegisterOnDifferentDevices) {
  const int32_t dev_phy_id1 = GetUniqueDevPhyId();
  const int32_t dev_phy_id2 = GetUniqueDevPhyId();
  int32_t test_data = 42;
  void *host_addr = &test_data;
  void *device_addr1 = nullptr;
  void *device_addr2 = nullptr;

  // 在设备1上注册
  Status ret1 = HostRegisterProxy::RegisterByDev(dev_phy_id1, host_addr, kTestMemSize, device_addr1);
  EXPECT_EQ(ret1, SUCCESS);
  EXPECT_NE(device_addr1, nullptr);

  // 在设备2上注册同一地址
  Status ret2 = HostRegisterProxy::RegisterByDev(dev_phy_id2, host_addr, kTestMemSize, device_addr2);
  EXPECT_EQ(ret2, SUCCESS);
  EXPECT_NE(device_addr2, nullptr);
}

// ========== 引用计数相关测试 ==========

// 测试多次注册后单次注销，内存仍有效
TEST_F(HostRegisterProxyTest, RefCntPartialUnregister) {
  const int32_t dev_phy_id = GetUniqueDevPhyId();
  int32_t test_data = 42;
  void *host_addr = &test_data;
  void *device_addr = nullptr;

  // 注册 3 次
  ASSERT_EQ(HostRegisterProxy::RegisterByDev(dev_phy_id, host_addr, kTestMemSize, device_addr), SUCCESS);
  ASSERT_EQ(HostRegisterProxy::RegisterByDev(dev_phy_id, host_addr, kTestMemSize, device_addr), SUCCESS);
  ASSERT_EQ(HostRegisterProxy::RegisterByDev(dev_phy_id, host_addr, kTestMemSize, device_addr), SUCCESS);
  ASSERT_NE(device_addr, nullptr);

  // 注销 1 次，ref_cnt 应为 2
  EXPECT_EQ(HostRegisterProxy::UnregisterByDev(dev_phy_id, host_addr), SUCCESS);

  // 内存应仍有效，可以获取 device_addr
  void *retrieved_dev_addr = nullptr;
  EXPECT_EQ(HostRegisterProxy::GetRegisteredDeviceAddrByDev(dev_phy_id, host_addr, retrieved_dev_addr), SUCCESS);
  EXPECT_EQ(retrieved_dev_addr, device_addr);

  // 清理剩余引用
  EXPECT_EQ(HostRegisterProxy::UnregisterByDev(dev_phy_id, host_addr), SUCCESS);
  EXPECT_EQ(HostRegisterProxy::UnregisterByDev(dev_phy_id, host_addr), SUCCESS);
}

// 测试多次注册后多次注销，只有全部注销后内存才释放
TEST_F(HostRegisterProxyTest, RefCntFullUnregister) {
  const int32_t dev_phy_id = GetUniqueDevPhyId();
  int32_t test_data = 42;
  void *host_addr = &test_data;
  void *device_addr = nullptr;
  constexpr int kRegisterCount = 3;

  // 注册 N 次
  for (int i = 0; i < kRegisterCount; ++i) {
    ASSERT_EQ(HostRegisterProxy::RegisterByDev(dev_phy_id, host_addr, kTestMemSize, device_addr), SUCCESS);
  }
  ASSERT_NE(device_addr, nullptr);

  // 注销 N-1 次，内存仍有效
  for (int i = 0; i < kRegisterCount - 1; ++i) {
    EXPECT_EQ(HostRegisterProxy::UnregisterByDev(dev_phy_id, host_addr), SUCCESS);
  }
  void *retrieved_dev_addr = nullptr;
  EXPECT_EQ(HostRegisterProxy::GetRegisteredDeviceAddrByDev(dev_phy_id, host_addr, retrieved_dev_addr), SUCCESS);

  // 第 N 次注销，内存释放
  EXPECT_EQ(HostRegisterProxy::UnregisterByDev(dev_phy_id, host_addr), SUCCESS);

  // 无法再获取 device_addr
  EXPECT_NE(HostRegisterProxy::GetRegisteredDeviceAddrByDev(dev_phy_id, host_addr, retrieved_dev_addr), SUCCESS);
}

// 测试重复注册但 size 不同，返回错误
TEST_F(HostRegisterProxyTest, RefCntRegisterWithDifferentSize) {
  const int32_t dev_phy_id = GetUniqueDevPhyId();
  int32_t test_data = 42;
  void *host_addr = &test_data;
  void *device_addr1 = nullptr;
  void *device_addr2 = nullptr;

  // 首次注册
  ASSERT_EQ(HostRegisterProxy::RegisterByDev(dev_phy_id, host_addr, kTestMemSize, device_addr1), SUCCESS);
  ASSERT_NE(device_addr1, nullptr);

  // 用不同 size 再次注册，应返回错误
  constexpr uint64_t kDifferentSize = 2048U;
  Status ret = HostRegisterProxy::RegisterByDev(dev_phy_id, host_addr, kDifferentSize, device_addr2);
  EXPECT_EQ(ret, PARAM_INVALID);

  // 原注册仍有效
  void *retrieved_dev_addr = nullptr;
  EXPECT_EQ(HostRegisterProxy::GetRegisteredDeviceAddrByDev(dev_phy_id, host_addr, retrieved_dev_addr), SUCCESS);
  EXPECT_EQ(retrieved_dev_addr, device_addr1);

  // 清理
  EXPECT_EQ(HostRegisterProxy::UnregisterByDev(dev_phy_id, host_addr), SUCCESS);
}

// 测试重复注销（超过注册次数）
TEST_F(HostRegisterProxyTest, RefCntOverUnregister) {
  const int32_t dev_phy_id = GetUniqueDevPhyId();
  int32_t test_data = 42;
  void *host_addr = &test_data;
  void *device_addr = nullptr;

  // 注册 1 次
  ASSERT_EQ(HostRegisterProxy::RegisterByDev(dev_phy_id, host_addr, kTestMemSize, device_addr), SUCCESS);

  // 注销 1 次，内存释放
  EXPECT_EQ(HostRegisterProxy::UnregisterByDev(dev_phy_id, host_addr), SUCCESS);

  // 再次注销未注册的地址，应返回 SUCCESS
  EXPECT_EQ(HostRegisterProxy::UnregisterByDev(dev_phy_id, host_addr), SUCCESS);
}

}  // namespace hixl
