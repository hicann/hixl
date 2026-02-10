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
#include "engine/hixl_server.h"
#include "hixl/hixl_types.h"
#include "common/hixl_inner_types.h"

namespace hixl {
static constexpr uint32_t kMemAddr1 = 0x1000;
static constexpr uint32_t kMemAddr2 = 0x1080;  // Overlaps
static constexpr uint32_t kMemAddr3 = 0x1100;  // No overlap
static constexpr uint32_t kMemLen = 0x100;
static constexpr uint32_t kPort = 16000;

class HixlServerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    EndpointConfig ep0;
    ep0.protocol = "roce";
    ep0.comm_id = "192.168.1.2";
    ep0.placement = "device";
    EndpointConfig ep1;
    ep1.protocol = "roce";
    ep1.comm_id = "192.168.1.1";
    ep1.placement = "host";

    default_eps.emplace_back(ep0);
    default_eps.emplace_back(ep1);

    mem_.addr = kMemAddr1;
    mem_.len = kMemLen;
  }

  void TearDown() override {}

 private:
  HixlServer server_;
  std::vector<EndpointConfig> default_eps;
  MemDesc mem_{};
  std::string ip_ = "127.0.0.1";
  int32_t port_ = kPort;
};

TEST_F(HixlServerTest, RegisterMemWithoutInit) {
  MemHandle handle = nullptr;
  Status ret = server_.RegisterMem(mem_, MemType::MEM_DEVICE, handle);
  EXPECT_EQ(ret, PARAM_INVALID);
}

TEST_F(HixlServerTest, FinalizeWithoutInit) {
  Status ret = server_.Finalize();
  EXPECT_EQ(ret, SUCCESS);
}

TEST_F(HixlServerTest, InitializePortZero) {
  EXPECT_EQ(server_.Initialize(ip_, 0, default_eps), SUCCESS);
  EXPECT_EQ(server_.Finalize(), SUCCESS);
}

TEST_F(HixlServerTest, RegisterMemSameTwice) {
  EXPECT_EQ(server_.Initialize(ip_, port_, default_eps), SUCCESS);
  MemHandle handle1 = nullptr;
  MemHandle handle2 = nullptr;
  EXPECT_EQ(server_.RegisterMem(mem_, MemType::MEM_DEVICE, handle1), SUCCESS);
  EXPECT_EQ(server_.RegisterMem(mem_, MemType::MEM_DEVICE, handle2), SUCCESS);
  EXPECT_EQ(server_.Finalize(), SUCCESS);
}

TEST_F(HixlServerTest, RegisterMemOverlap) {
  EXPECT_EQ(server_.Initialize(ip_, port_, default_eps), SUCCESS);
  MemDesc mem1{};
  mem1.addr = kMemAddr1;
  mem1.len = kMemLen;
  MemHandle handle1 = nullptr;
  EXPECT_EQ(server_.RegisterMem(mem1, MemType::MEM_DEVICE, handle1), SUCCESS);
  MemDesc mem2{};
  mem2.addr = kMemAddr2;
  mem2.len = kMemLen;
  MemHandle handle2 = nullptr;
  EXPECT_EQ(server_.RegisterMem(mem2, MemType::MEM_DEVICE, handle2), PARAM_INVALID);
  EXPECT_EQ(server_.Finalize(), SUCCESS);
}

TEST_F(HixlServerTest, DeregisterMemDoubleFree) {
  EXPECT_EQ(server_.Initialize(ip_, port_, default_eps), SUCCESS);
  MemHandle handle = nullptr;
  EXPECT_EQ(server_.RegisterMem(mem_, MemType::MEM_DEVICE, handle), SUCCESS);

  MemHandle handle_copy = handle;
  EXPECT_EQ(server_.DeregisterMem(handle), SUCCESS);
  EXPECT_EQ(handle, nullptr);

  // Using the copy
  EXPECT_EQ(server_.DeregisterMem(handle_copy), SUCCESS);

  EXPECT_EQ(server_.Finalize(), SUCCESS);
}

TEST_F(HixlServerTest, RegisterMemBoundary) {
  EXPECT_EQ(server_.Initialize(ip_, port_, default_eps), SUCCESS);

  MemDesc mem1{};
  mem1.addr = kMemAddr1;
  mem1.len = kMemLen;
  MemHandle handle1 = nullptr;
  EXPECT_EQ(server_.RegisterMem(mem1, MemType::MEM_DEVICE, handle1), SUCCESS);

  MemDesc mem2{};
  mem2.addr = kMemAddr3;  // Starts exactly at end of mem1
  mem2.len = kMemLen;
  MemHandle handle2 = nullptr;
  EXPECT_EQ(server_.RegisterMem(mem2, MemType::MEM_DEVICE, handle2), SUCCESS);

  EXPECT_EQ(server_.Finalize(), SUCCESS);
}

TEST_F(HixlServerTest, RegisterMemOverlapDifferentType) {
  EXPECT_EQ(server_.Initialize(ip_, port_, default_eps), SUCCESS);

  MemDesc mem1{};
  mem1.addr = kMemAddr1;
  mem1.len = kMemLen;
  MemHandle handle1 = nullptr;
  EXPECT_EQ(server_.RegisterMem(mem1, MemType::MEM_HOST, handle1), SUCCESS);

  MemDesc mem2{};
  mem2.addr = kMemAddr2;  // Overlaps
  mem2.len = kMemLen;
  MemHandle handle2 = nullptr;
  EXPECT_EQ(server_.RegisterMem(mem2, MemType::MEM_DEVICE, handle2), SUCCESS);

  EXPECT_EQ(server_.Finalize(), SUCCESS);
}

TEST_F(HixlServerTest, DeregisterNonExistent) {
  EXPECT_EQ(server_.Initialize(ip_, port_, default_eps), SUCCESS);

  MemHandle handle = nullptr;
  EXPECT_EQ(server_.RegisterMem(mem_, MemType::MEM_DEVICE, handle), SUCCESS);

  MemHandle invalid_handle = reinterpret_cast<MemHandle>(0xdeadbeef);
  EXPECT_NE(server_.DeregisterMem(invalid_handle), FAILED);
  EXPECT_EQ(server_.Finalize(), SUCCESS);
}

TEST_F(HixlServerTest, RegisterAfterFinalize) {
  EXPECT_EQ(server_.Initialize(ip_, port_, default_eps), SUCCESS);
  EXPECT_EQ(server_.Finalize(), SUCCESS);

  MemHandle handle = nullptr;
  EXPECT_NE(server_.RegisterMem(mem_, MemType::MEM_DEVICE, handle), FAILED);
}

TEST_F(HixlServerTest, NormalInitRegisterDeregisterFinalize) {
  EXPECT_EQ(server_.Initialize(ip_, port_, default_eps), SUCCESS);
  MemHandle handle = nullptr;
  EXPECT_EQ(server_.RegisterMem(mem_, MemType::MEM_DEVICE, handle), SUCCESS);
  EXPECT_NE(handle, nullptr);
  EXPECT_EQ(server_.DeregisterMem(handle), SUCCESS);
  EXPECT_EQ(server_.Finalize(), SUCCESS);
}
}  // namespace hixl