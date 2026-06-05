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
#include <thread>
#include <gtest/gtest.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;
#include <fstream>
#include <sys/stat.h>
#include "ascendcl_stub.h"
#include "engine/endpoint_test_utils.h"
#include "engine/hixl_engine.h"
#include "hixl/hixl_types.h"
#include "cs/hixl_cs_client.h"
#include "hixl/hixl.h"
#include "slog_stub.h"
#include "depends/mmpa/src/mmpa_stub.h"
#include "engine/test_mmpa_utils.h"

namespace hixl {

namespace {
constexpr int32_t kTimeOut = 500;
constexpr const char kHccnConfPath[] = "/etc/hccn.conf";
constexpr const char kHccnToolPath[] = "/usr/local/Ascend/driver/tools/hccn_tool";
constexpr const char kEmptyPathDir[] = "/tmp/hixl_engine_uboe_empty_path";
using MockEngineAclRuntimeStub = endpoint_test::MockAclRuntimeStub;

class UboeMmpaStub : public hixl::test::KernelJsonMmpaStub {
 public:
  INT32 Access(const CHAR *path_name) override {
    std::string path_str(path_name);
    if (path_str == kHccnToolPath) {
      return EN_ERROR;
    }
    return KernelJsonMmpaStub::Access(path_name);
  }
};

class NoHccnConfMmpaStub : public UboeMmpaStub {
 public:
  int32_t RealPath(const CHAR *path, CHAR *realPath, INT32 realPathLen) override {
    std::string path_str(path);
    if (path_str == kHccnConfPath) {
      return EN_ERROR;
    }
    return UboeMmpaStub::RealPath(path, realPath, realPathLen);
  }
};
}  // namespace

class HixlEngineUboeTest : public ::testing::Test {
 protected:
  std::map<AscendString, AscendString> options_uboe;
  std::map<AscendString, AscendString> options_mixed;
  std::map<AscendString, AscendString> options_default;

  void SetUp() override {
    acl_stub_ = endpoint_test::CreateAclRuntimeStub("Ascend910_9391", 0, 0, 9, 8);
    llm::AclRuntimeStub::SetInstance(acl_stub_);
    // EnsureDeviceKernelLoadedLocked 现在在初始化阶段调用，需要提前设置 MmpaStub
    llm::MmpaStub::GetInstance().SetImpl(std::make_shared<UboeMmpaStub>());
    temp_dir_ = fs::path("/tmp/hixl_engine_uboe_unittest");
    fs::remove_all(temp_dir_);
    fs::create_directories(temp_dir_);
    old_path_ = getenv("PATH") == nullptr ? "" : getenv("PATH");

    // UBOE 协议配置
    options_uboe[hixl::OPTION_LOCAL_COMM_RES] = R"(
    {
        "net_instance_id": "uboe_test_1",
        "endpoint_list": [
            {
                "protocol": "uboe",
                "comm_id": "127.0.0.1",
                "placement": "device"
            }
        ],
        "version": "1.3"
    }
    )";

    // 混合协议配置（UBOE + ROCE）
    options_mixed[hixl::OPTION_LOCAL_COMM_RES] = R"(
    {
        "net_instance_id": "mixed_test_1",
        "endpoint_list": [
            {
                "protocol": "roce",
                "comm_id": "127.0.0.1",
                "placement": "host"
            },
            {
                "protocol": "uboe",
                "comm_id": "127.0.0.1",
                "placement": "device"
            }
        ],
        "version": "1.3"
    }
    )";

    // 默认配置（空 endpoint_list，需要生成默认配置）
    options_default[hixl::OPTION_LOCAL_COMM_RES] = R"(
    {
        "net_instance_id": "default_test_1",
        "endpoint_list": [],
        "version": "1.3"
    }
    )";
  }

  void TearDown() override {
    llm::AclRuntimeStub::Reset();
    llm::MmpaStub::GetInstance().Reset();
    if (old_path_.empty()) {
      unsetenv("PATH");
    } else {
      setenv("PATH", old_path_.c_str(), 1);
    }
    fs::remove_all(temp_dir_);
  }

  void CreateHccnTool(const std::string &output) const {
    const auto tool_path = temp_dir_ / "hccn_tool";
    std::ofstream file(tool_path);
    ASSERT_TRUE(file.is_open());
    file << "#!/bin/sh\n";
    file << "echo \"" << output << "\"\n";
    file.close();
    ASSERT_EQ(chmod(tool_path.c_str(), 0755), 0);
    const auto new_path = temp_dir_.string() + ":" + old_path_;
    setenv("PATH", new_path.c_str(), 1);
  }

  void SetUboeOptions(std::map<AscendString, AscendString> &options) {
    // 配置 comm_resource_config.protocol_desc 为 ["uboe:device"]
    options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"(
      {
        "comm_resource_config.protocol_desc": ["uboe:device"]
      }
    )";
  }

  void SetA5Env() {
    acl_stub_->soc_name_ = "Ascend950A";
    acl_stub_->device_id_ = 0;
    acl_stub_->phy_device_id_ = 3;
  }

 private:
  std::shared_ptr<MockEngineAclRuntimeStub> acl_stub_;
  fs::path temp_dir_;
  std::string old_path_;
};

// 测试 UBOE 协议 Endpoint 的初始化
TEST_F(HixlEngineUboeTest, InitializeWithUboeProtocol) {
  Hixl engine;
  EXPECT_EQ(engine.Initialize("127.0.0.1", options_uboe), SUCCESS);
  engine.Finalize();
}

// 测试混合协议（UBOE + ROCE）的初始化
TEST_F(HixlEngineUboeTest, InitializeWithMixedProtocols) {
  Hixl engine;
  EXPECT_EQ(engine.Initialize("127.0.0.1", options_mixed), SUCCESS);
  engine.Finalize();
}

// 测试空 endpoint_list 的处理
TEST_F(HixlEngineUboeTest, InitializeWithEmptyEndpointList) {
  fs::create_directories(kEmptyPathDir);
  test::ScopedPathGuard path_guard(kEmptyPathDir);
  llm::MmpaStub::GetInstance().SetImpl(std::make_shared<NoHccnConfMmpaStub>());

  Hixl engine;
  Status ret = engine.Initialize("127.0.0.1", options_default);
  EXPECT_NE(ret, SUCCESS);
  if (ret == SUCCESS) {
    engine.Finalize();
  }
}

// 测试uboe init 的配置
TEST_F(HixlEngineUboeTest, InitializeUboeTest) {
  Hixl engine;
  EXPECT_EQ(engine.Initialize("127.0.0.1", options_uboe), SUCCESS);
  engine.Finalize();
}

// 测试 UBOE 协议下的 Host 内存注册
TEST_F(HixlEngineUboeTest, RegisterHostMemoryWithUboe) {
  Hixl engine;
  ASSERT_EQ(engine.Initialize("127.0.0.1", options_uboe), SUCCESS);

  // 分配 host 内存
  int32_t src = 42;
  hixl::MemDesc src_mem{};
  src_mem.addr = reinterpret_cast<uintptr_t>(&src);
  src_mem.len = sizeof(int32_t);

  // 注册 host 内存（在 UBOE 场景下应该自动注册设备地址）
  MemHandle handle = nullptr;
  Status ret = engine.RegisterMem(src_mem, MEM_HOST, handle);
  // 注意：这里可能需要根据实际实现调整
  // 在 UBOE 场景下，host 内存注册可能需要特殊处理
  EXPECT_EQ(ret, SUCCESS);

  if (ret == SUCCESS) {
    EXPECT_EQ(engine.DeregisterMem(handle), SUCCESS);
  }

  engine.Finalize();
}

// 测试 UBOE 协议下的 Device 内存注册
TEST_F(HixlEngineUboeTest, RegisterDeviceMemoryWithUboe) {
  Hixl engine;
  ASSERT_EQ(engine.Initialize("127.0.0.1", options_uboe), SUCCESS);

  // 模拟 device 内存地址
  hixl::MemDesc device_mem{};
  device_mem.addr = 0x100000;  // 模拟设备地址
  device_mem.len = sizeof(int32_t);

  MemHandle handle = nullptr;
  Status ret = engine.RegisterMem(device_mem, MEM_DEVICE, handle);
  // Device 内存注册应该直接成功
  EXPECT_EQ(ret, SUCCESS);

  if (ret == SUCCESS) {
    EXPECT_EQ(engine.DeregisterMem(handle), SUCCESS);
  }

  engine.Finalize();
}

// 测试两个 UBOE endpoint 的连接
TEST_F(HixlEngineUboeTest, ConnectTwoUboeEngines) {
  std::map<AscendString, AscendString> options1;
  options1[hixl::OPTION_LOCAL_COMM_RES] = R"(
  {
      "net_instance_id": "uboe_engine1",
      "endpoint_list": [
          {
              "protocol": "uboe",
              "comm_id": "192.168.1.100",
              "placement": "device"
          }
      ],
      "version": "1.3"
  }
  )";

  std::map<AscendString, AscendString> options2;
  options2[hixl::OPTION_LOCAL_COMM_RES] = R"(
  {
      "net_instance_id": "uboe_engine2",
      "endpoint_list": [
          {
              "protocol": "uboe",
              "comm_id": "192.168.111.111",
              "placement": "device"
          }
      ],
      "version": "1.3"
  }
  )";

  Hixl engine1;
  Hixl engine2;
  ASSERT_EQ(engine1.Initialize("127.0.0.1", options1), SUCCESS);
  ASSERT_EQ(engine2.Initialize("127.0.0.1:26320", options2), SUCCESS);

  // 连接
  EXPECT_EQ(engine1.Connect("127.0.0.1:26320", kTimeOut), SUCCESS);

  // 断开连接并清理
  engine1.Disconnect("127.0.0.1:26320");
  engine1.Finalize();
  engine2.Finalize();
}

// 测试优先使用 UBOE 协议连接
TEST_F(HixlEngineUboeTest, PreferUboeProtocolForConnection) {
  // 本地配置同时有 ROCE 和 UBOE
  std::map<AscendString, AscendString> local_options;
  local_options[hixl::OPTION_LOCAL_COMM_RES] = R"(
  {
      "net_instance_id": "local_mixed",
      "endpoint_list": [
          {
              "protocol": "roce",
              "comm_id": "127.0.0.1",
              "placement": "host"
          },
          {
              "protocol": "uboe",
              "comm_id": "192.168.100.100",
              "placement": "device"
          }
      ],
      "version": "1.3"
  }
  )";

  // 远端配置同时有 ROCE 和 UBOE
  std::map<AscendString, AscendString> remote_options;
  remote_options[hixl::OPTION_LOCAL_COMM_RES] = R"(
  {
      "net_instance_id": "remote_mixed",
      "endpoint_list": [
          {
              "protocol": "roce",
              "comm_id": "127.0.0.1",
              "placement": "host"
          },
          {
              "protocol": "uboe",
              "comm_id": "192.168.111.112",
              "placement": "device"
          }
      ],
      "version": "1.3"
  }
  )";

  Hixl engine1;
  Hixl engine2;
  ASSERT_EQ(engine1.Initialize("127.0.0.1", local_options), SUCCESS);
  ASSERT_EQ(engine2.Initialize("127.0.0.1:26320", remote_options), SUCCESS);

  // 连接，应该优先使用 UBOE
  EXPECT_EQ(engine1.Connect("127.0.0.1:26320", kTimeOut), SUCCESS);

  engine1.Disconnect("127.0.0.1:26320");
  engine1.Finalize();
  engine2.Finalize();
}

// 测试 UBOE 协议的内存注册和注销配对
TEST_F(HixlEngineUboeTest, RegisterAndDeregisterMemoryPaired) {
  Hixl engine;
  ASSERT_EQ(engine.Initialize("127.0.0.1", options_uboe), SUCCESS);

  // 分配 host 内存
  std::vector<int32_t> host_data(100, 42);
  hixl::MemDesc mem{};
  mem.addr = reinterpret_cast<uintptr_t>(host_data.data());
  mem.len = host_data.size() * sizeof(int32_t);

  MemHandle handle = nullptr;
  EXPECT_EQ(engine.RegisterMem(mem, MEM_HOST, handle), SUCCESS);
  EXPECT_NE(handle, nullptr);

  // 注销内存
  EXPECT_EQ(engine.DeregisterMem(handle), SUCCESS);

  engine.Finalize();
}

// 测试重复注册同一内存
TEST_F(HixlEngineUboeTest, RegisterSameMemoryTwice) {
  Hixl engine;
  ASSERT_EQ(engine.Initialize("127.0.0.1", options_uboe), SUCCESS);

  int32_t data = 42;
  hixl::MemDesc mem{};
  mem.addr = reinterpret_cast<uintptr_t>(&data);
  mem.len = sizeof(int32_t);

  MemHandle handle1 = nullptr;
  MemHandle handle2 = nullptr;

  EXPECT_EQ(engine.RegisterMem(mem, MEM_HOST, handle1), SUCCESS);
  // 第二次注册相同地址可能返回错误或返回不同的 handle
  // 根据实际实现调整
  Status ret = engine.RegisterMem(mem, MEM_HOST, handle2);
  // 暂时假设允许重复注册
  if (ret == SUCCESS) {
    EXPECT_EQ(engine.DeregisterMem(handle2), SUCCESS);
  }
  EXPECT_EQ(engine.DeregisterMem(handle1), SUCCESS);

  engine.Finalize();
}

// 测试通过 OPTION_GLOBAL_RESOURCE_CONFIG 触发 endpoint_generator 生成默认 UBOE endpoint
TEST_F(HixlEngineUboeTest, InitializeWithGlobalResourceConfig) {
  SetA5Env();
  // Create mock hccn_tool that returns bond IP
  CreateHccnTool("ipaddr:192.168.100.200\nnetmask:255.255.255.0");

  std::map<AscendString, AscendString> options;
  SetUboeOptions(options);

  Hixl engine;
  // GetBondIpAddress 会通过 mock hccn_tool 返回固定 IP
  EXPECT_EQ(engine.Initialize("127.0.0.1", options), SUCCESS);
  engine.Finalize();
}

TEST_F(HixlEngineUboeTest, InitializeWithAutoGeneratedEndpointsAndUboe) {
  SetA5Env();
  CreateHccnTool("ipaddr:192.168.100.200\nnetmask:255.255.255.0");

  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_LOCAL_COMM_RES] = R"({"version":"1.3"})";
  SetUboeOptions(options);

  Hixl engine;
  EXPECT_EQ(engine.Initialize("127.0.0.1", options), SUCCESS);
  engine.Finalize();
}

TEST_F(HixlEngineUboeTest, InitializeFailsDirectlyWhenEndpointGenerationFails) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_LOCAL_COMM_RES] = "{";
  SetUboeOptions(options);

  Hixl engine;
  EXPECT_NE(engine.Initialize("127.0.0.1", options), SUCCESS);
}

// 测试端到端 UBOE 传输：初始化 -> 内存注册 -> 连接 -> Transfer
// 使用 127.0.0.1 进行本地测试，底层接口已打桩
// UBOE placement 永远是 device，H2H 指的是内存类型
TEST_F(HixlEngineUboeTest, EndToEndUboeBatchTransferHostToHost) {
  SetA5Env();
  // Create mock hccn_tool that returns bond IP
  CreateHccnTool("ipaddr:127.0.0.1");

  // 初始化 engine1 (server) - UBOE placement 永远是 device
  std::map<AscendString, AscendString> options;
  SetUboeOptions(options);

  Hixl engine1;
  Hixl engine2;
  // Server 使用带端口的 local_engine
  ASSERT_EQ(engine1.Initialize("127.0.0.1:26320", options), SUCCESS);
  // Client 使用不同的端口
  ASSERT_EQ(engine2.Initialize("127.0.0.1:26321", options), SUCCESS);

  // 注册 host 内存 - Server side (H2H: 双方都是 host 内存)
  std::vector<int32_t> server_data(100, 42);
  hixl::MemDesc server_mem{};
  server_mem.addr = reinterpret_cast<uintptr_t>(server_data.data());
  server_mem.len = server_data.size() * sizeof(int32_t);

  MemHandle server_handle = nullptr;
  EXPECT_EQ(engine1.RegisterMem(server_mem, MEM_HOST, server_handle), SUCCESS);

  // 注册 host 内存 - Client side (H2H: 双方都是 host 内存)
  std::vector<int32_t> client_data(100, 0);
  hixl::MemDesc client_mem{};
  client_mem.addr = reinterpret_cast<uintptr_t>(client_data.data());
  client_mem.len = client_data.size() * sizeof(int32_t);

  MemHandle client_handle = nullptr;
  EXPECT_EQ(engine2.RegisterMem(client_mem, MEM_HOST, client_handle), SUCCESS);

  // 连接 - Client 连接 Server
  EXPECT_EQ(engine2.Connect("127.0.0.1:26320", kTimeOut), SUCCESS);

  // 执行传输 (client 从 server 获取数据) - Host to Host
  std::vector<TransferOpDesc> op_descs;
  TransferOpDesc desc;
  desc.local_addr = client_mem.addr;   // client's local address
  desc.remote_addr = server_mem.addr;  // server's remote address
  desc.len = server_mem.len;
  op_descs.push_back(desc);

  // 执行传输
  (void)engine2.TransferSync("127.0.0.1:26320", TransferOp::READ, op_descs, kTimeOut);

  // 清理
  (void)engine2.Disconnect("127.0.0.1:26320");
  EXPECT_EQ(engine2.DeregisterMem(client_handle), SUCCESS);
  EXPECT_EQ(engine1.DeregisterMem(server_handle), SUCCESS);
  engine2.Finalize();
  engine1.Finalize();
}

// 测试 Host 到 Device 传输 (client 是 device 内存，server 是 host 内存)
TEST_F(HixlEngineUboeTest, EndToEndUboeBatchTransferHostToDevice) {
  SetA5Env();
  CreateHccnTool("ipaddr:127.0.0.1");

  std::map<AscendString, AscendString> options;
  SetUboeOptions(options);

  Hixl engine1;
  Hixl engine2;
  // Server 使用带端口的 local_engine
  ASSERT_EQ(engine1.Initialize("127.0.0.1:26330", options), SUCCESS);
  // Client 使用不同的端口
  ASSERT_EQ(engine2.Initialize("127.0.0.1:26331", options), SUCCESS);

  // Server: 注册 host 内存
  std::vector<int32_t> server_data(100, 42);
  hixl::MemDesc server_mem{};
  server_mem.addr = reinterpret_cast<uintptr_t>(server_data.data());
  server_mem.len = server_data.size() * sizeof(int32_t);

  MemHandle server_handle = nullptr;
  EXPECT_EQ(engine1.RegisterMem(server_mem, MEM_HOST, server_handle), SUCCESS);

  // Client: 注册 device 内存 (H2D: client 是 device 内存)
  hixl::MemDesc client_mem{};
  client_mem.addr = 0x10000000;  // 模拟设备地址
  client_mem.len = 100 * sizeof(int32_t);

  MemHandle client_handle = nullptr;
  EXPECT_EQ(engine2.RegisterMem(client_mem, MEM_DEVICE, client_handle), SUCCESS);

  EXPECT_EQ(engine2.Connect("127.0.0.1:26330", kTimeOut), SUCCESS);

  // Host to Device 传输
  std::vector<TransferOpDesc> op_descs;
  TransferOpDesc desc;
  desc.local_addr = client_mem.addr;
  desc.remote_addr = server_mem.addr;
  desc.len = server_mem.len;
  op_descs.push_back(desc);

  // 执行传输
  EXPECT_EQ(engine2.TransferSync("127.0.0.1:26330", TransferOp::READ, op_descs, kTimeOut), SUCCESS);

  EXPECT_EQ(engine2.Disconnect("127.0.0.1:26330"), SUCCESS);
  EXPECT_EQ(engine2.DeregisterMem(client_handle), SUCCESS);
  EXPECT_EQ(engine1.DeregisterMem(server_handle), SUCCESS);
  engine2.Finalize();
  engine1.Finalize();
}

// 测试 Device 到 Host 传输 (client 是 host 内存，server 是 device 内存)
TEST_F(HixlEngineUboeTest, EndToEndUboeBatchTransferDeviceToHost) {
  SetA5Env();
  CreateHccnTool("ipaddr:127.0.0.1");

  std::map<AscendString, AscendString> options;
  SetUboeOptions(options);

  Hixl engine1;
  Hixl engine2;
  // Server 使用带端口的 local_engine
  ASSERT_EQ(engine1.Initialize("127.0.0.1:26340", options), SUCCESS);
  // Client 使用不同的端口
  ASSERT_EQ(engine2.Initialize("127.0.0.1:26341", options), SUCCESS);

  // Server: 注册 device 内存 (D2H: server 是 device 内存)
  hixl::MemDesc server_mem{};
  server_mem.addr = 0x20000000;  // 模拟设备地址
  server_mem.len = 100 * sizeof(int32_t);

  MemHandle server_handle = nullptr;
  EXPECT_EQ(engine1.RegisterMem(server_mem, MEM_DEVICE, server_handle), SUCCESS);

  // Client: 注册 host 内存 (D2H: client 是 host 内存)
  std::vector<int32_t> client_data(100, 0);
  hixl::MemDesc client_mem{};
  client_mem.addr = reinterpret_cast<uintptr_t>(client_data.data());
  client_mem.len = client_data.size() * sizeof(int32_t);

  MemHandle client_handle = nullptr;
  EXPECT_EQ(engine2.RegisterMem(client_mem, MEM_HOST, client_handle), SUCCESS);

  EXPECT_EQ(engine2.Connect("127.0.0.1:26340", kTimeOut), SUCCESS);

  // Device to Host 传输
  std::vector<TransferOpDesc> op_descs;
  TransferOpDesc desc;
  desc.local_addr = client_mem.addr;
  desc.remote_addr = server_mem.addr;
  desc.len = server_mem.len;
  op_descs.push_back(desc);

  // 执行传输
  EXPECT_EQ(engine2.TransferSync("127.0.0.1:26340", TransferOp::READ, op_descs, kTimeOut), SUCCESS);

  EXPECT_EQ(engine2.Disconnect("127.0.0.1:26340"), SUCCESS);
  EXPECT_EQ(engine2.DeregisterMem(client_handle), SUCCESS);
  EXPECT_EQ(engine1.DeregisterMem(server_handle), SUCCESS);
  engine2.Finalize();
  engine1.Finalize();
}

// 测试 Device 到 Device 传输 (双方都是 device 内存)
TEST_F(HixlEngineUboeTest, EndToEndUboeBatchTransferDeviceToDevice) {
  SetA5Env();
  // Create mock hccn_tool that returns bond IP
  CreateHccnTool("ipaddr:192.168.100.100\nnetmask:255.255.255.0");

  std::map<AscendString, AscendString> options;
  SetUboeOptions(options);

  Hixl engine1;
  Hixl engine2;
  // Server 使用带端口的 local_engine
  ASSERT_EQ(engine1.Initialize("127.0.0.1:26350", options), SUCCESS);
  // Client 使用不同的端口
  ASSERT_EQ(engine2.Initialize("127.0.0.1:26351", options), SUCCESS);

  // Server: 注册 device 内存 (D2D: 双方都是 device 内存)
  hixl::MemDesc server_mem{};
  server_mem.addr = 0x30000000;  // 模拟设备地址
  server_mem.len = 100 * sizeof(int32_t);

  MemHandle server_handle = nullptr;
  EXPECT_EQ(engine1.RegisterMem(server_mem, MEM_DEVICE, server_handle), SUCCESS);

  // Client: 注册 device 内存 (D2D: 双方都是 device 内存)
  hixl::MemDesc client_mem{};
  client_mem.addr = 0x40000000;  // 模拟设备地址
  client_mem.len = 100 * sizeof(int32_t);

  MemHandle client_handle = nullptr;
  EXPECT_EQ(engine2.RegisterMem(client_mem, MEM_DEVICE, client_handle), SUCCESS);

  EXPECT_EQ(engine2.Connect("127.0.0.1:26350", kTimeOut), SUCCESS);

  // Device to Device 传输
  std::vector<TransferOpDesc> op_descs;
  TransferOpDesc desc;
  desc.local_addr = client_mem.addr;
  desc.remote_addr = server_mem.addr;
  desc.len = server_mem.len;
  op_descs.push_back(desc);

  // 执行传输
  EXPECT_EQ(engine2.TransferSync("127.0.0.1:26350", TransferOp::READ, op_descs, kTimeOut), SUCCESS);

  EXPECT_EQ(engine2.Disconnect("127.0.0.1:26350"), SUCCESS);
  EXPECT_EQ(engine2.DeregisterMem(client_handle), SUCCESS);
  EXPECT_EQ(engine1.DeregisterMem(server_handle), SUCCESS);
  engine2.Finalize();
  engine1.Finalize();
}
}  // namespace hixl
