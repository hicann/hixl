/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <map>
#include <vector>

#include <gtest/gtest.h>

#include "adxl/adxl_engine.h"
#include "adxl_test_helpers.h"
#include "depends/llm_datadist/src/data_cache_engine_test_helper.h"

namespace adxl {
namespace {
constexpr char kPeerEngine[] = "127.0.0.1:28101";

class SyncTransferFailMock : public llm::AutoCommResRuntimeMock {
 public:
  aclError aclrtSynchronizeStreamWithTimeout(aclrtStream stream, int32_t timeout) override {
    (void)stream;
    (void)timeout;
    return ACL_ERROR_RT_STREAM_SYNC_TIMEOUT;
  }
};

struct Int32MemPair {
  int32_t src = 1;
  int32_t dst = 2;
  MemHandle handle1 = nullptr;
  MemHandle handle2 = nullptr;
};

void RegisterInt32Mem(AdxlEngine &engine, int32_t *ptr, MemHandle &handle) {
  adxl::MemDesc mem_desc{};
  mem_desc.addr = reinterpret_cast<uintptr_t>(ptr);
  mem_desc.len = sizeof(int32_t);
  EXPECT_EQ(engine.RegisterMem(mem_desc, MEM_DEVICE, handle), SUCCESS);
}

TransferOpDesc MakeInt32TransferDesc(int32_t &src, int32_t &dst) {
  return TransferOpDesc{reinterpret_cast<uintptr_t>(&src), reinterpret_cast<uintptr_t>(&dst), sizeof(int32_t)};
}

void SetupPlainEngines(AdxlEngine &engine1, AdxlEngine &engine2) {
  llm::AutoCommResRuntimeMock::SetDevice(0);
  std::map<AscendString, AscendString> options1;
  options1[OPTION_RDMA_TRAFFIC_CLASS] = "4";
  options1[OPTION_RDMA_SERVICE_LEVEL] = "1";
  options1[OPTION_BUFFER_POOL] = "0:0";
  EXPECT_EQ(engine1.Initialize("127.0.0.1", options1), SUCCESS);
  llm::AutoCommResRuntimeMock::SetDevice(1);
  std::map<AscendString, AscendString> options2;
  EXPECT_EQ(engine2.Initialize(kPeerEngine, options2), SUCCESS);
}

Int32MemPair SetupPlainConnectedEngines(AdxlEngine &engine1, AdxlEngine &engine2) {
  SetupPlainEngines(engine1, engine2);
  Int32MemPair mem;
  RegisterInt32Mem(engine1, &mem.src, mem.handle1);
  RegisterInt32Mem(engine2, &mem.dst, mem.handle2);
  EXPECT_EQ(engine1.Connect(kPeerEngine), SUCCESS);
  return mem;
}

Int32MemPair SetupAutoConnectEngines(AdxlEngine &engine1, AdxlEngine &engine2) {
  llm::AutoCommResRuntimeMock::SetDevice(0);
  std::map<AscendString, AscendString> options1;
  options1[OPTION_AUTO_CONNECT] = "1";
  EXPECT_EQ(engine1.Initialize("127.0.0.1:28100", options1), SUCCESS);
  llm::AutoCommResRuntimeMock::SetDevice(1);
  std::map<AscendString, AscendString> options2;
  EXPECT_EQ(engine2.Initialize(kPeerEngine, options2), SUCCESS);
  Int32MemPair mem;
  RegisterInt32Mem(engine1, &mem.src, mem.handle1);
  RegisterInt32Mem(engine2, &mem.dst, mem.handle2);
  return mem;
}

void CleanupEngines(AdxlEngine &engine1, AdxlEngine &engine2, MemHandle handle1, MemHandle handle2) {
  EXPECT_EQ(engine1.Disconnect(kPeerEngine), SUCCESS);
  EXPECT_EQ(engine1.DeregisterMem(handle1), SUCCESS);
  EXPECT_EQ(engine2.DeregisterMem(handle2), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
}
}  // namespace

class AdxlCircuitBreakerUTest : public test_helpers::AdxlHcclRuntimeTestBase {};

TEST_F(AdxlCircuitBreakerUTest, TestLinkUnavailableAfterSyncTransferFailure) {
  AdxlEngine engine1;
  AdxlEngine engine2;
  auto mem = SetupPlainConnectedEngines(engine1, engine2);
  TransferOpDesc desc = MakeInt32TransferDesc(mem.src, mem.dst);

  SyncTransferFailMock fail_mock;
  llm::AclRuntimeStub::Install(&fail_mock);
  EXPECT_EQ(engine1.TransferSync(kPeerEngine, READ, {desc}), TIMEOUT);
  llm::AclRuntimeStub::UnInstall(&fail_mock);

  TransferReq req = nullptr;
  EXPECT_EQ(engine1.TransferSync(kPeerEngine, READ, {desc}), FAILED);
  EXPECT_EQ(engine1.TransferAsync(kPeerEngine, READ, {desc}, {}, req), FAILED);

  EXPECT_EQ(engine1.Disconnect(kPeerEngine), SUCCESS);
  EXPECT_EQ(engine1.Connect(kPeerEngine), SUCCESS);
  mem.src = 1;
  EXPECT_EQ(engine1.TransferSync(kPeerEngine, READ, {desc}), SUCCESS);
  EXPECT_EQ(mem.src, 2);

  CleanupEngines(engine1, engine2, mem.handle1, mem.handle2);
}

TEST_F(AdxlCircuitBreakerUTest, TestAutoConnectNotBrickedByTransferFailure) {
  AdxlEngine engine1;
  AdxlEngine engine2;
  auto mem = SetupAutoConnectEngines(engine1, engine2);
  TransferOpDesc desc = MakeInt32TransferDesc(mem.src, mem.dst);

  SyncTransferFailMock fail_mock;
  llm::AclRuntimeStub::Install(&fail_mock);
  EXPECT_EQ(engine1.TransferSync(kPeerEngine, READ, {desc}), TIMEOUT);
  llm::AclRuntimeStub::UnInstall(&fail_mock);

  mem.src = 1;
  EXPECT_EQ(engine1.TransferSync(kPeerEngine, READ, {desc}), SUCCESS);
  EXPECT_EQ(mem.src, 2);

  engine1.Disconnect(kPeerEngine);
  EXPECT_EQ(engine1.DeregisterMem(mem.handle1), SUCCESS);
  EXPECT_EQ(engine2.DeregisterMem(mem.handle2), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
}

}  // namespace adxl
