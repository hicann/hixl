/**
* Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

/**
 * @file dcmi_proxy_ut.cc
 * @brief DCMI Proxy 模块单元测试
 *
 * 测试覆盖：
 * - 5个代理函数成功路径（DcmiGetLogicIdFromPhyId / DcmiGetUrmaDeviceCnt / DcmiGetEidList /
 *   DcmiGetMainboardId / DcmiGetDeviceInfo）
 * - LoadDcmi() 已加载缓存路径
 *
 * 注意：dcmi_proxy.cc 使用真实的 dlopen/dlsym/dlclose（来自 <dlfcn.h>），而非 MmpaStub。
 * 因此失败路径（dlopen 失败、dlsym 失败、init 失败）无法通过 MmpaStub mock 来测试，
 * 需要通过 LD_PRELOAD mock dlopen 来测试。LD_PRELOAD 测试在本地环境可运行，
 * 但在某些 CI 环境中可能受限。
 */

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include "gtest/gtest.h"
#include "proxy/hcomm/dcmi_proxy.h"

// DCMI 桩函数控制接口（定义在 tests/depends/dcmi/src/dcmi_stub.cc）
extern "C" {
void DcmiStubSetInitRet(int ret);
void DcmiStubSetMainboardId(unsigned int id, int ret);
void DcmiStubSetLogicId(unsigned int id, int ret);
void DcmiStubSetUrmaDeviceCnt(unsigned int cnt, int ret);
void DcmiStubSetSuperPodId(unsigned int id, int ret);
void DcmiStubSetEidCount(int count);
}

namespace hixl {

namespace {

// DCMI 桩初始化状态重置
void ResetDcmiStub() {
  DcmiStubSetInitRet(0);
  DcmiStubSetMainboardId(0x3, 0);  // Pod1
  DcmiStubSetLogicId(0, 0);
  DcmiStubSetUrmaDeviceCnt(1, 0);
  DcmiStubSetSuperPodId(0, 0);
  DcmiStubSetEidCount(2);
}

}  // anonymous namespace

// ============================================================================
// 成功路径测试
// ============================================================================

class DcmiProxySuccessTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ResetDcmiStub();
  }

  void TearDown() override {
    ResetDcmiStub();
  }
};

// DcmiGetLogicIdFromPhyId 成功路径
TEST_F(DcmiProxySuccessTest, GetLogicIdFromPhyIdSuccess) {
  unsigned int phy_id = 5;
  unsigned int logic_id = 0;
  int32_t ret = DcmiGetLogicIdFromPhyId(phy_id, &logic_id);
  EXPECT_EQ(ret, 0);
  // stub 返回 phy_id 作为 logic_id
  EXPECT_EQ(logic_id, phy_id);
}

// DcmiGetUrmaDeviceCnt 成功路径
TEST_F(DcmiProxySuccessTest, GetUrmaDeviceCntSuccess) {
  unsigned int logic_id = 0;
  unsigned int dev_cnt = 0;
  int32_t ret = DcmiGetUrmaDeviceCnt(logic_id, &dev_cnt);
  EXPECT_EQ(ret, 0);
  EXPECT_EQ(dev_cnt, 1U);
}

// DcmiGetEidList 成功路径
TEST_F(DcmiProxySuccessTest, GetEidListSuccess) {
  unsigned int logic_id = 0;
  int urma_dev_index = 0;
  dcmi_urma_eid_info_t eid_list[2];
  int eid_cnt = 2;
  int32_t ret = DcmiGetEidList(logic_id, urma_dev_index, eid_list, &eid_cnt);
  EXPECT_EQ(ret, 0);
  EXPECT_GT(eid_cnt, 0);
}

// DcmiGetMainboardId 成功路径
TEST_F(DcmiProxySuccessTest, GetMainboardIdSuccess) {
  unsigned int logic_id = 0;
  unsigned int mainboard_id = 0;
  int32_t ret = DcmiGetMainboardId(logic_id, &mainboard_id);
  EXPECT_EQ(ret, 0);
  EXPECT_EQ(mainboard_id, 0x3U);  // Pod1
}

// DcmiGetDeviceInfo 成功路径
TEST_F(DcmiProxySuccessTest, GetDeviceInfoSuccess) {
  unsigned int logic_id = 0;
  int main_cmd = 0;
  unsigned int sub_cmd = 0;
  unsigned char buf[128];
  unsigned int size = sizeof(buf);
  int32_t ret = DcmiGetDeviceInfo(logic_id, main_cmd, sub_cmd, buf, &size);
  EXPECT_EQ(ret, 0);
  EXPECT_EQ(size, sizeof(buf));
}

// ============================================================================
// LoadDcmi() 已加载缓存路径测试
// ============================================================================

class DcmiProxyCachedLoadTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ResetDcmiStub();
  }

  void TearDown() override {
    ResetDcmiStub();
  }
};

// 第一次 LoadDcmi 成功后，第二次调用应直接返回缓存状态
TEST_F(DcmiProxyCachedLoadTest, LoadDcmiCachedPath) {
  // 第一次调用（应成功，因为 dcmi_stub.cc 提供所有符号）
  unsigned int logic_id1 = 0;
  unsigned int mainboard_id1 = 0;
  int32_t ret1 = DcmiGetMainboardId(logic_id1, &mainboard_id1);
  EXPECT_EQ(ret1, 0);

  // 第二次调用（应直接返回缓存的 g_dcmi_init_status，不重新加载）
  unsigned int logic_id2 = 0;
  unsigned int mainboard_id2 = 0;
  int32_t ret2 = DcmiGetMainboardId(logic_id2, &mainboard_id2);
  EXPECT_EQ(ret2, 0);
  EXPECT_EQ(mainboard_id2, mainboard_id1);
}

// ============================================================================
// 边界条件测试
// ============================================================================

class DcmiProxyBoundaryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ResetDcmiStub();
  }

  void TearDown() override {
    ResetDcmiStub();
  }
};

// DcmiGetEidList 空 eid_count
TEST_F(DcmiProxyBoundaryTest, GetEidListZeroCount) {
  DcmiStubSetEidCount(0);  // 不返回任何 EID

  unsigned int logic_id = 0;
  int urma_dev_index = 0;
  dcmi_urma_eid_info_t eid_list[2];
  int eid_cnt = 2;
  int32_t ret = DcmiGetEidList(logic_id, urma_dev_index, eid_list, &eid_cnt);
  EXPECT_EQ(ret, 0);
  EXPECT_EQ(eid_cnt, 0);  // stub 返回 0 个 EID
}

// DcmiGetMainboardId 不同产品形态
TEST_F(DcmiProxyBoundaryTest, GetMainboardIdPod2) {
  DcmiStubSetMainboardId(0x5, 0);  // Pod2

  unsigned int logic_id = 0;
  unsigned int mainboard_id = 0;
  int32_t ret = DcmiGetMainboardId(logic_id, &mainboard_id);
  EXPECT_EQ(ret, 0);
  EXPECT_EQ(mainboard_id, 0x5U);
}

TEST_F(DcmiProxyBoundaryTest, GetMainboardIdServer) {
  DcmiStubSetMainboardId(0x21, 0);  // Server

  unsigned int logic_id = 0;
  unsigned int mainboard_id = 0;
  int32_t ret = DcmiGetMainboardId(logic_id, &mainboard_id);
  EXPECT_EQ(ret, 0);
  EXPECT_EQ(mainboard_id, 0x21U);
}

}  // namespace hixl