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
#include <memory>

#include <gtest/gtest.h>

#include "adxl/adxl_engine.h"
#include "adxl_test_helpers.h"
#include "depends/llm_datadist/src/data_cache_engine_test_helper.h"

namespace adxl {
namespace {

constexpr char kPeerEngine[] = "127.0.0.1:28101";

struct ConnectedInt32Pair {
  std::unique_ptr<AdxlEngine> engine1;
  std::unique_ptr<AdxlEngine> engine2;
  int32_t src = 1;
  int32_t dst = 2;
  MemHandle handle1 = nullptr;
  MemHandle handle2 = nullptr;
};

ConnectedInt32Pair MakeConnectedInt32Pair() {
  ConnectedInt32Pair pair;
  pair.engine1 = std::make_unique<AdxlEngine>();
  pair.engine2 = std::make_unique<AdxlEngine>();
  llm::AutoCommResRuntimeMock::SetDevice(0);
  std::map<AscendString, AscendString> options1;
  options1[OPTION_RDMA_TRAFFIC_CLASS] = "4";
  options1[OPTION_RDMA_SERVICE_LEVEL] = "1";
  options1[OPTION_BUFFER_POOL] = "0:0";
  EXPECT_EQ(pair.engine1->Initialize("127.0.0.1", options1), SUCCESS);
  llm::AutoCommResRuntimeMock::SetDevice(1);
  std::map<AscendString, AscendString> options2;
  EXPECT_EQ(pair.engine2->Initialize(kPeerEngine, options2), SUCCESS);

  MemDesc src_desc{reinterpret_cast<uintptr_t>(&pair.src), sizeof(int32_t)};
  MemDesc dst_desc{reinterpret_cast<uintptr_t>(&pair.dst), sizeof(int32_t)};
  EXPECT_EQ(pair.engine1->RegisterMem(src_desc, MEM_DEVICE, pair.handle1), SUCCESS);
  EXPECT_EQ(pair.engine2->RegisterMem(dst_desc, MEM_DEVICE, pair.handle2), SUCCESS);
  EXPECT_EQ(pair.engine1->Connect(kPeerEngine), SUCCESS);
  return pair;
}

TransferOpDesc MakeWriteDesc(const ConnectedInt32Pair &pair) {
  return TransferOpDesc{reinterpret_cast<uintptr_t>(const_cast<int32_t *>(&pair.src)),
                        reinterpret_cast<uintptr_t>(const_cast<int32_t *>(&pair.dst)), sizeof(int32_t)};
}

void FinalizePair(ConnectedInt32Pair &pair) {
  EXPECT_EQ(pair.engine1->Disconnect(kPeerEngine), SUCCESS);
  EXPECT_EQ(pair.engine1->DeregisterMem(pair.handle1), SUCCESS);
  EXPECT_EQ(pair.engine2->DeregisterMem(pair.handle2), SUCCESS);
  pair.engine1->Finalize();
  pair.engine2->Finalize();
}

class AdxlAsyncHostFlagUTest : public test_helpers::AdxlHcclRuntimeTestBase {};

}  // namespace

TEST_F(AdxlAsyncHostFlagUTest, GetTransferStatusWaitsWhenHostFlagNotReady) {
  auto pair = MakeConnectedInt32Pair();
  TransferOpDesc desc = MakeWriteDesc(pair);
  TransferReq req = nullptr;
  TransferStatus status = TransferStatus::FAILED;

  llm::AsyncHostFlagNeverSetMock wait_mock;
  llm::AclRuntimeStub::Install(&wait_mock);
  EXPECT_EQ(pair.engine1->TransferAsync(kPeerEngine, WRITE, {desc}, {}, req), SUCCESS);
  EXPECT_EQ(pair.engine1->GetTransferStatus(req, status), SUCCESS);
  EXPECT_EQ(status, TransferStatus::WAITING);
  llm::AclRuntimeStub::UnInstall(&wait_mock);

  FinalizePair(pair);
}

}  // namespace adxl
