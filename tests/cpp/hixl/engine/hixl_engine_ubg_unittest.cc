/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <cstdlib>
#include <string>
#include <vector>
#include <gtest/gtest.h>
#include "ascendcl_stub.h"
#include "engine/endpoint_test_utils.h"
#include "engine/hixl_engine.h"
#include "hixl/hixl_types.h"
#include "hixl/hixl.h"
#include "slog_stub.h"
#include "depends/dsmi/src/dsmi_stub.h"
#include "depends/mmpa/src/mmpa_stub.h"
#include "engine/test_mmpa_utils.h"

namespace hixl {

namespace {
constexpr int32_t kTimeOut = 500;
constexpr size_t kElemCount = 100;
constexpr uintptr_t kMockServerDeviceAddr = 0x10000000;
constexpr uintptr_t kMockClientDeviceAddr = 0x20000000;
// 32 位无冒号十六进制 ScaleOut EID，byte7 高两 bit 为 10 表示 UBG
constexpr const char kUbgEid[] = "0000000000ff0a80000000000a140200";

using MockEngineAclRuntimeStub = endpoint_test::MockAclRuntimeStub;

std::string BuildUbgLocalCommRes() {
  std::string res = R"(
  {
      "net_instance_id": "ubg_test_1",
      "endpoint_list": [
          {
              "protocol": "ubg",
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
}  // namespace

// UBG 端到端联调用例：初始化 -> 内存注册 -> 连接 -> Transfer（READ + WRITE）
// 覆盖 4 种内存方向、8 个子方向：
//   H2H: READ=RH2H, WRITE=H2RH    H2D: READ=RH2D, WRITE=D2RH
//   D2H: READ=RD2H, WRITE=H2RD    D2D: READ=RD2D, WRITE=D2RD
// 两端使用显式 UBG endpoint_list（同 net_instance_id，超节点内表可匹配 UBG），不依赖 DSMI/DCMI 自动生成。
class HixlEngineUbgTest : public ::testing::Test {
 protected:
  void SetUp() override {
    acl_stub_ = endpoint_test::CreateAclRuntimeStub("Ascend910_9391", 0, 0, 9, 8);
    llm::AclRuntimeStub::SetInstance(acl_stub_);
    // EnsureDeviceKernelLoadedLocked 在初始化阶段调用，需要提前设置 MmpaStub；UBG 不依赖 hccn_tool/bond IP
    llm::MmpaStub::GetInstance().SetImpl(std::make_shared<hixl::test::KernelJsonMmpaStub>());
    DsmiStubSetInterconType(4U);
    options_ubg_[hixl::OPTION_LOCAL_COMM_RES] = AscendString(BuildUbgLocalCommRes().c_str());
  }

  void TearDown() override {
    llm::AclRuntimeStub::Reset();
    llm::MmpaStub::GetInstance().Reset();
  }

  struct EnginePair {
    Hixl server;
    Hixl client;
    std::string server_engine;
  };

  void InitEnginePair(EnginePair &pair, uint32_t server_port) {
    pair.server_engine = "127.0.0.1:" + std::to_string(server_port);
    const std::string client_engine = "127.0.0.1:" + std::to_string(server_port + 1);
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

  static MemHandle RegisterDeviceMem(Hixl &engine, uintptr_t addr) {
    hixl::MemDesc mem{};
    mem.addr = addr;
    mem.len = kElemCount * sizeof(int32_t);
    MemHandle handle = nullptr;
    EXPECT_EQ(engine.RegisterMem(mem, MEM_DEVICE, handle), SUCCESS);
    return handle;
  }

  // 建链后同一对内存上先 READ 后 WRITE，覆盖该内存方向的两个子方向，最后断链清理
  void ConnectTransferBothAndCleanup(EnginePair &pair, uintptr_t local_addr, uintptr_t remote_addr,
                                     MemHandle server_handle, MemHandle client_handle) {
    EXPECT_EQ(pair.client.Connect(pair.server_engine.c_str(), kTimeOut), SUCCESS);

    std::vector<TransferOpDesc> op_descs;
    TransferOpDesc desc{};
    desc.local_addr = local_addr;
    desc.remote_addr = remote_addr;
    desc.len = kElemCount * sizeof(int32_t);
    op_descs.push_back(desc);

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

// H2H：READ=RH2H（远端 Host -> 本端 Host），WRITE=H2RH（本端 Host -> 远端 Host）
TEST_F(HixlEngineUbgTest, EndToEndUbgBatchTransferHostToHost) {
  EnginePair pair;
  InitEnginePair(pair, 17000);
  std::vector<int32_t> server_data(kElemCount, 42);
  std::vector<int32_t> client_data(kElemCount, 0);
  MemHandle server_handle = RegisterHostMem(pair.server, server_data);
  MemHandle client_handle = RegisterHostMem(pair.client, client_data);
  ConnectTransferBothAndCleanup(pair, reinterpret_cast<uintptr_t>(client_data.data()),
                                reinterpret_cast<uintptr_t>(server_data.data()), server_handle, client_handle);
}

// H2D：本端 Device + 远端 Host，READ=RH2D，WRITE=D2RH
TEST_F(HixlEngineUbgTest, EndToEndUbgBatchTransferHostToDevice) {
  EnginePair pair;
  InitEnginePair(pair, 17100);
  std::vector<int32_t> server_data(kElemCount, 42);
  MemHandle server_handle = RegisterHostMem(pair.server, server_data);
  MemHandle client_handle = RegisterDeviceMem(pair.client, kMockClientDeviceAddr);
  ConnectTransferBothAndCleanup(pair, kMockClientDeviceAddr, reinterpret_cast<uintptr_t>(server_data.data()),
                                server_handle, client_handle);
}

// D2H：本端 Host + 远端 Device，READ=RD2H，WRITE=H2RD
TEST_F(HixlEngineUbgTest, EndToEndUbgBatchTransferDeviceToHost) {
  EnginePair pair;
  InitEnginePair(pair, 17200);
  std::vector<int32_t> client_data(kElemCount, 0);
  MemHandle server_handle = RegisterDeviceMem(pair.server, kMockServerDeviceAddr);
  MemHandle client_handle = RegisterHostMem(pair.client, client_data);
  ConnectTransferBothAndCleanup(pair, reinterpret_cast<uintptr_t>(client_data.data()), kMockServerDeviceAddr,
                                server_handle, client_handle);
}

// D2D：双方 Device，READ=RD2D，WRITE=D2RD
TEST_F(HixlEngineUbgTest, EndToEndUbgBatchTransferDeviceToDevice) {
  EnginePair pair;
  InitEnginePair(pair, 17300);
  MemHandle server_handle = RegisterDeviceMem(pair.server, kMockServerDeviceAddr);
  MemHandle client_handle = RegisterDeviceMem(pair.client, kMockClientDeviceAddr);
  ConnectTransferBothAndCleanup(pair, kMockClientDeviceAddr, kMockServerDeviceAddr, server_handle, client_handle);
}

}  // namespace hixl
