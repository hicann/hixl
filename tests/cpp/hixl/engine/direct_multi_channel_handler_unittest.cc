/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <algorithm>
#include <cstdlib>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include <gtest/gtest.h>
#include "ascendcl_stub.h"
#include "engine/endpoint_test_utils.h"
#include "engine/hixl_engine.h"
#include "engine/direct_multi_channel_handler.h"
#include "hixl/hixl_types.h"
#include "hixl/hixl.h"
#include "slog_stub.h"
#include "depends/dsmi/src/dsmi_stub.h"
#include "depends/mmpa/src/mmpa_stub.h"
#include "depends/sys_api/src/sys_api_wrap.h"
#include "engine/test_mmpa_utils.h"

namespace hixl {

namespace {
constexpr int32_t kTimeOut = 500;
constexpr size_t kElemCount = 100;
constexpr uintptr_t kMockServerDeviceAddr = 0x10000000;
constexpr uintptr_t kMockClientDeviceAddr = 0x20000000;
constexpr const char kUbgEid[] = "0000000000ff0a80000000000a140200";

using MockEngineAclRuntimeStub = endpoint_test::MockAclRuntimeStub;

std::string BuildUbgLocalCommRes() {
  std::string res = R"(
  {
      "net_instance_id": "ubg_multi_channel_test",
      "endpoint_list": [
          {
              "protocol": "ub_rtp",
              "comm_id": ")";
  res += kUbgEid;
  res += R"(",
              "placement": "device"
          }
      ],
      "version": "1.3"
  }
  )";
  return res;
}

std::string BuildMultiChannelConfig(uint32_t worker_num, uint32_t split_batch_size = 128U) {
  return R"({"comm_resource_config.multi_channel.num_workers": )" + std::to_string(worker_num) +
         R"(, "comm_resource_config.multi_channel.split_batch_size": )" + std::to_string(split_batch_size) + "}";
}
}  // namespace

class DirectMultiChannelHandlerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    acl_stub_ = endpoint_test::CreateAclRuntimeStub("Ascend910_9391", 0, 0, 9, 8);
    llm::AclRuntimeStub::SetInstance(acl_stub_);
    hixl_test::InstallSysApiHooks(std::make_shared<hixl::test::KernelJsonMmpaStub>());
    DsmiStubSetInterconType(4U);
    options_ubg_[hixl::OPTION_LOCAL_COMM_RES] = AscendString(BuildUbgLocalCommRes().c_str());
    options_ubg_.erase(hixl::OPTION_GLOBAL_RESOURCE_CONFIG);
  }

  void TearDown() override {
    llm::ResetStubMallocHostCounter();
    llm::AclRuntimeStub::Reset();
    hixl_test::ResetSysApiHooks();
  }

  struct EnginePair {
    Hixl server;
    Hixl client;
    std::string server_engine;
  };

  void InitEnginePairWithMultiChannel(EnginePair &pair, uint32_t server_port, uint32_t worker_num,
                                      uint32_t split_batch_size = 128U) {
    pair.server_engine = "127.0.0.1:" + std::to_string(server_port);
    const std::string client_engine = "127.0.0.1:" + std::to_string(server_port + 1);
    options_ubg_[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] =
        AscendString(BuildMultiChannelConfig(worker_num, split_batch_size).c_str());
    ASSERT_EQ(pair.server.Initialize(pair.server_engine.c_str(), options_ubg_), SUCCESS);
    ASSERT_EQ(pair.client.Initialize(client_engine.c_str(), options_ubg_), SUCCESS);
  }

  static MemHandle RegisterHostMem(Hixl &engine, std::vector<int32_t> &buffer) {
    hixl::MemDesc mem{};
    mem.addr = reinterpret_cast<uintptr_t>(buffer.data());
    mem.len = buffer.size() * sizeof(int32_t);
    MemHandle handle = nullptr;
    EXPECT_EQ(engine.RegisterMem(mem, MEM_HOST, handle), SUCCESS);
    return handle;
  }

  static MemHandle RegisterDeviceMem(Hixl &engine, uintptr_t addr, size_t len) {
    hixl::MemDesc mem{};
    mem.addr = addr;
    mem.len = len;
    MemHandle handle = nullptr;
    EXPECT_EQ(engine.RegisterMem(mem, MEM_DEVICE, handle), SUCCESS);
    return handle;
  }

  static TransferOpDesc BuildSingleDesc(uintptr_t local_addr, uintptr_t remote_addr, uint64_t len) {
    TransferOpDesc d{};
    d.local_addr = local_addr;
    d.remote_addr = remote_addr;
    d.len = len;
    return d;
  }

  static std::vector<TransferOpDesc> BuildMultiDescs(uintptr_t local_base, uintptr_t remote_base, uint64_t per_desc_len,
                                                     uint32_t count) {
    std::vector<TransferOpDesc> descs;
    for (uint32_t i = 0U; i < count; ++i) {
      descs.push_back(BuildSingleDesc(local_base + i * per_desc_len, remote_base + i * per_desc_len, per_desc_len));
    }
    return descs;
  }

  void ConnectTransferH2HMultiDescAndCleanup(EnginePair &pair, uint32_t desc_count) {
    const uint64_t per_desc_len = kElemCount * sizeof(int32_t);
    std::vector<int32_t> server_data(desc_count * kElemCount, 42);
    std::vector<int32_t> client_data(desc_count * kElemCount, 0);
    MemHandle server_handle = RegisterHostMem(pair.server, server_data);
    MemHandle client_handle = RegisterHostMem(pair.client, client_data);
    EXPECT_EQ(pair.client.Connect(pair.server_engine.c_str(), kTimeOut), SUCCESS);
    auto op_descs = BuildMultiDescs(reinterpret_cast<uintptr_t>(client_data.data()),
                                    reinterpret_cast<uintptr_t>(server_data.data()), per_desc_len, desc_count);
    EXPECT_EQ(pair.client.TransferSync(pair.server_engine.c_str(), TransferOp::READ, op_descs, kTimeOut), SUCCESS);
    EXPECT_EQ(pair.client.TransferSync(pair.server_engine.c_str(), TransferOp::WRITE, op_descs, kTimeOut), SUCCESS);
    EXPECT_EQ(pair.client.Disconnect(pair.server_engine.c_str()), SUCCESS);
    EXPECT_EQ(pair.client.DeregisterMem(client_handle), SUCCESS);
    EXPECT_EQ(pair.server.DeregisterMem(server_handle), SUCCESS);
    pair.client.Finalize();
    pair.server.Finalize();
  }

  void ConnectTransferD2DAndCleanup(EnginePair &pair, uint32_t desc_count) {
    const uint64_t per_desc_len = 4U * 1024U;
    const uint64_t total_len = desc_count * per_desc_len;
    MemHandle server_handle = RegisterDeviceMem(pair.server, kMockServerDeviceAddr, total_len);
    MemHandle client_handle = RegisterDeviceMem(pair.client, kMockClientDeviceAddr, total_len);
    EXPECT_EQ(pair.client.Connect(pair.server_engine.c_str(), kTimeOut), SUCCESS);
    auto op_descs = BuildMultiDescs(kMockClientDeviceAddr, kMockServerDeviceAddr, per_desc_len, desc_count);
    EXPECT_EQ(pair.client.TransferSync(pair.server_engine.c_str(), TransferOp::READ, op_descs, kTimeOut), SUCCESS);
    EXPECT_EQ(pair.client.TransferSync(pair.server_engine.c_str(), TransferOp::WRITE, op_descs, kTimeOut), SUCCESS);
    EXPECT_EQ(pair.client.Disconnect(pair.server_engine.c_str()), SUCCESS);
    EXPECT_EQ(pair.client.DeregisterMem(client_handle), SUCCESS);
    EXPECT_EQ(pair.server.DeregisterMem(server_handle), SUCCESS);
    pair.client.Finalize();
    pair.server.Finalize();
  }

  std::map<AscendString, AscendString> options_ubg_;

 private:
  std::shared_ptr<MockEngineAclRuntimeStub> acl_stub_;
};

TEST_F(DirectMultiChannelHandlerTest, ConnectWithMultiWorker) {
  EnginePair pair;
  ASSERT_NO_FATAL_FAILURE(InitEnginePairWithMultiChannel(pair, 18100, 2));
  EXPECT_EQ(pair.client.Connect(pair.server_engine.c_str(), kTimeOut), SUCCESS);
  EXPECT_EQ(pair.client.Disconnect(pair.server_engine.c_str()), SUCCESS);
  pair.client.Finalize();
  pair.server.Finalize();
}

TEST_F(DirectMultiChannelHandlerTest, TransferSyncH2HMultiDescSplit) {
  EnginePair pair;
  ASSERT_NO_FATAL_FAILURE(InitEnginePairWithMultiChannel(pair, 18500, 4, 10));
  ConnectTransferH2HMultiDescAndCleanup(pair, 40U);
}

TEST_F(DirectMultiChannelHandlerTest, TransferSyncPartialWorkerActivation) {
  EnginePair pair;
  ASSERT_NO_FATAL_FAILURE(InitEnginePairWithMultiChannel(pair, 18700, 4, 10));
  ConnectTransferD2DAndCleanup(pair, 25U);
}

TEST_F(DirectMultiChannelHandlerTest, TransferSyncFullWorkerActivation) {
  EnginePair pair;
  ASSERT_NO_FATAL_FAILURE(InitEnginePairWithMultiChannel(pair, 18900, 4, 10));
  ConnectTransferD2DAndCleanup(pair, 40U);
}

TEST_F(DirectMultiChannelHandlerTest, TransferAsyncUsesMultiWorker) {
  EnginePair pair;
  ASSERT_NO_FATAL_FAILURE(InitEnginePairWithMultiChannel(pair, 19200, 4, 10));
  const uint64_t per_desc_len = 4U * 1024U;
  const uint64_t total_len = 40U * per_desc_len;
  MemHandle server_handle = RegisterDeviceMem(pair.server, kMockServerDeviceAddr, total_len);
  MemHandle client_handle = RegisterDeviceMem(pair.client, kMockClientDeviceAddr, total_len);
  EXPECT_EQ(pair.client.Connect(pair.server_engine.c_str(), kTimeOut), SUCCESS);
  auto op_descs = BuildMultiDescs(kMockClientDeviceAddr, kMockServerDeviceAddr, per_desc_len, 40U);
  TransferReq req = 0;
  TransferArgs optional_args{};
  EXPECT_EQ(pair.client.TransferAsync(pair.server_engine.c_str(), TransferOp::WRITE, op_descs, optional_args, req),
            SUCCESS);
  TransferStatus status = TransferStatus::WAITING;
  for (int32_t attempt = 0; attempt < 20 && status == TransferStatus::WAITING; ++attempt) {
    EXPECT_EQ(pair.client.GetTransferStatus(req, status), SUCCESS);
  }
  EXPECT_EQ(status, TransferStatus::COMPLETED);
  EXPECT_EQ(pair.client.Disconnect(pair.server_engine.c_str()), SUCCESS);
  EXPECT_EQ(pair.client.DeregisterMem(client_handle), SUCCESS);
  EXPECT_EQ(pair.server.DeregisterMem(server_handle), SUCCESS);
  pair.client.Finalize();
  pair.server.Finalize();
}

TEST_F(DirectMultiChannelHandlerTest, MaxWorkerValueSixteen) {
  EnginePair pair;
  ASSERT_NO_FATAL_FAILURE(InitEnginePairWithMultiChannel(pair, 18600, 16));
  EXPECT_EQ(pair.client.Connect(pair.server_engine.c_str(), kTimeOut), SUCCESS);
  EXPECT_EQ(pair.client.Disconnect(pair.server_engine.c_str()), SUCCESS);
  pair.client.Finalize();
  pair.server.Finalize();
}

TEST_F(DirectMultiChannelHandlerTest, WorkerNumExceedsUpperBound) {
  EnginePair pair;
  pair.server_engine = "127.0.0.1:19100";
  const std::string client_engine = "127.0.0.1:19101";
  options_ubg_[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = AscendString(BuildMultiChannelConfig(17U).c_str());
  EXPECT_NE(pair.server.Initialize(pair.server_engine.c_str(), options_ubg_), SUCCESS);
  EXPECT_NE(pair.client.Initialize(client_engine.c_str(), options_ubg_), SUCCESS);
  pair.client.Finalize();
  pair.server.Finalize();
}

TEST_F(DirectMultiChannelHandlerTest, AsyncPartialSubmitFailureReturnsError) {
  EnginePair pair;
  ASSERT_NO_FATAL_FAILURE(InitEnginePairWithMultiChannel(pair, 19500, 4, 10));
  const uint64_t per_desc_len = 4U * 1024U;
  const uint64_t total_len = 40U * per_desc_len;
  MemHandle server_handle = RegisterDeviceMem(pair.server, kMockServerDeviceAddr, total_len);
  MemHandle client_handle = RegisterDeviceMem(pair.client, kMockClientDeviceAddr, total_len);
  EXPECT_EQ(pair.client.Connect(pair.server_engine.c_str(), kTimeOut), SUCCESS);
  auto op_descs = BuildMultiDescs(kMockClientDeviceAddr, kMockServerDeviceAddr, per_desc_len, 40U);

  // 第 2 次 aclrtMallocHost 失败 → 恰有 1 个 worker 提交失败
  llm::ResetStubMallocHostCounter();
  llm::SetStubMallocHostFailOn(1);

  TransferReq req = 0;
  TransferArgs optional_args{};
  // 部分提交失败：同步返回失败，在途 DMA 由上层断开时清理
  EXPECT_NE(pair.client.TransferAsync(pair.server_engine.c_str(), TransferOp::WRITE, op_descs, optional_args, req),
            SUCCESS);

  llm::ResetStubMallocHostCounter();
  EXPECT_EQ(pair.client.Disconnect(pair.server_engine.c_str()), SUCCESS);
  EXPECT_EQ(pair.client.DeregisterMem(client_handle), SUCCESS);
  EXPECT_EQ(pair.server.DeregisterMem(server_handle), SUCCESS);
  pair.client.Finalize();
  pair.server.Finalize();
}

TEST_F(DirectMultiChannelHandlerTest, AsyncAllSubmitFailureReturnsError) {
  EnginePair pair;
  ASSERT_NO_FATAL_FAILURE(InitEnginePairWithMultiChannel(pair, 19700, 4, 10));
  const uint64_t per_desc_len = 4U * 1024U;
  const uint64_t total_len = 40U * per_desc_len;
  MemHandle server_handle = RegisterDeviceMem(pair.server, kMockServerDeviceAddr, total_len);
  MemHandle client_handle = RegisterDeviceMem(pair.client, kMockClientDeviceAddr, total_len);
  EXPECT_EQ(pair.client.Connect(pair.server_engine.c_str(), kTimeOut), SUCCESS);
  auto op_descs = BuildMultiDescs(kMockClientDeviceAddr, kMockServerDeviceAddr, per_desc_len, 40U);

  llm::ResetStubMallocHostCounter();
  llm::SetStubMallocHostFailOn(-2);

  TransferReq req = 0;
  TransferArgs optional_args{};
  const Status ret =
      pair.client.TransferAsync(pair.server_engine.c_str(), TransferOp::WRITE, op_descs, optional_args, req);
  EXPECT_NE(ret, SUCCESS);

  llm::ResetStubMallocHostCounter();
  EXPECT_EQ(pair.client.Disconnect(pair.server_engine.c_str()), SUCCESS);
  EXPECT_EQ(pair.client.DeregisterMem(client_handle), SUCCESS);
  EXPECT_EQ(pair.server.DeregisterMem(server_handle), SUCCESS);
  pair.client.Finalize();
  pair.server.Finalize();
}

TEST(DirectMultiChannelHandlerPureTest, ResolveActualWorkerNumReturnsOneWhenDescsFewerThanSplit) {
  EXPECT_EQ(DirectMultiChannelHandler::ResolveActualWorkerNum(4, 5, 10), 1U);
}

TEST(DirectMultiChannelHandlerPureTest, ResolveActualWorkerNumReturnsPartialChannels) {
  EXPECT_EQ(DirectMultiChannelHandler::ResolveActualWorkerNum(4, 25, 10), 2U);
}

TEST(DirectMultiChannelHandlerPureTest, ResolveActualWorkerNumReturnsFullChannels) {
  EXPECT_EQ(DirectMultiChannelHandler::ResolveActualWorkerNum(4, 40, 10), 4U);
}

TEST(DirectMultiChannelHandlerPureTest, ResolveActualWorkerNumReturnsOneWhenConfiguredIsOne) {
  EXPECT_EQ(DirectMultiChannelHandler::ResolveActualWorkerNum(1, 100, 10), 1U);
}

TEST(DirectMultiChannelHandlerPureTest, SplitDescsDistributesEvenly) {
  std::vector<TransferOpDesc> descs;
  for (uint32_t i = 0U; i < 10U; ++i) {
    descs.push_back({i * 100, i * 100 + 50, 100});
  }
  std::vector<std::vector<HixlOneSideOpDesc>> lanes;
  DirectMultiChannelHandler::SplitDescs(descs, 3, lanes);
  ASSERT_EQ(lanes.size(), 3U);
  uint32_t total = 0U;
  for (const auto &lane : lanes) {
    total += static_cast<uint32_t>(lane.size());
  }
  EXPECT_EQ(total, 10U);
  EXPECT_LE(std::abs(static_cast<int>(lanes[0].size()) - static_cast<int>(lanes[2].size())), 1);
}

TEST(DirectMultiChannelHandlerPureTest, SplitDescsSingleLane) {
  std::vector<TransferOpDesc> descs = {{0, 0, 100}};
  std::vector<std::vector<HixlOneSideOpDesc>> lanes;
  DirectMultiChannelHandler::SplitDescs(descs, 1, lanes);
  ASSERT_EQ(lanes.size(), 1U);
  EXPECT_EQ(lanes[0].size(), 1U);
}

}  // namespace hixl
