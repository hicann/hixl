/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <memory>
#include <thread>
#include <gtest/gtest.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "ascendcl_stub.h"
#include "engine/endpoint_test_utils.h"
#define private public
#include "engine/hixl_engine.h"
#undef private
#include "hixl/hixl_types.h"
#include "engine/engine_factory.h"
#include "common/ctrl_msg_plugin.h"
#include "engine/hixl_options.h"
#include "cs/hixl_cs_client.h"
#include "hixl/hixl.h"
#include "slog_stub.h"
#include "test_mmpa_utils.h"
#include "depends/mmpa/src/mmpa_stub.h"
#include "hixl_test_helpers.h"

namespace hixl {

namespace {
constexpr const int32_t kTimeOut = 1000;
constexpr const int32_t kMaxRetryCount = 10;
constexpr const int32_t kInterval = 10;
constexpr const uint32_t kCaptureLogTimeoutMs = 1000U;

using MockEngineMmpaStub = test::TestMmpaStub;

std::string BuildDeviceRoceEndpoint(const std::string &comm_id) {
  std::ostringstream oss;
  oss << "      {\n";
  oss << "        \"protocol\": \"roce\",\n";
  oss << "        \"comm_id\": \"" << comm_id << "\",\n";
  oss << "        \"placement\": \"device\"\n";
  oss << "      }";
  return oss.str();
}

std::string BuildDeviceHccsEndpoint(const std::string &comm_id) {
  std::ostringstream oss;
  oss << "      {\n";
  oss << "        \"protocol\": \"hccs\",\n";
  oss << "        \"comm_id\": \"" << comm_id << "\",\n";
  oss << "        \"placement\": \"device\"\n";
  oss << "      }";
  return oss.str();
}

std::string BuildHostRoceEndpoint(const std::string &comm_id) {
  std::ostringstream oss;
  oss << "      {\n";
  oss << "        \"protocol\": \"roce\",\n";
  oss << "        \"comm_id\": \"" << comm_id << "\",\n";
  oss << "        \"placement\": \"host\"\n";
  oss << "      }";
  return oss.str();
}

std::string BuildLocalCommRes(const std::string &net_instance_id, const std::string &version,
                              const std::vector<std::string> &endpoint_items) {
  std::ostringstream oss;
  oss << "{\n";
  oss << "  \"net_instance_id\": \"" << net_instance_id << "\",\n";
  oss << "  \"endpoint_list\": [\n";
  for (size_t i = 0; i < endpoint_items.size(); ++i) {
    oss << endpoint_items[i];
    if (i + 1 != endpoint_items.size()) {
      oss << ",";
    }
    oss << "\n";
  }
  oss << "  ],\n";
  oss << "  \"version\": \"" << version << "\"\n";
  oss << "}";
  return oss.str();
}

std::string BuildVersionOnlyLocalCommRes(const std::string &version) {
  std::ostringstream oss;
  oss << "{\n";
  oss << "  \"version\": \"" << version << "\"\n";
  oss << "}";
  return oss.str();
}

}  // namespace

using MockEngineAclRuntimeStub = endpoint_test::MockAclRuntimeStub;

class HixlEngineTest : public ::testing::Test {
 protected:
  std::map<AscendString, AscendString> options1;
  std::map<AscendString, AscendString> options2;
  void SetUp() override {
    SetSocStub("Ascend910B1", 0, 0, 9, 8);
    mmpa_stub_ = std::make_shared<MockEngineMmpaStub>();
    // EnsureDeviceKernelLoadedLocked 现在在初始化阶段调用，需要提前设置
    mmpa_stub_->real_path_ok_ = true;
    mmpa_stub_->access_ok_ = true;
    llm::MmpaStub::GetInstance().SetImpl(mmpa_stub_);
    const char *old_intra_roce_enable = std::getenv("HCCL_INTRA_ROCE_ENABLE");
    old_intra_roce_enable_ = (old_intra_roce_enable == nullptr) ? "" : old_intra_roce_enable;
    unsetenv("HCCL_INTRA_ROCE_ENABLE");
    options1[hixl::OPTION_LOCAL_COMM_RES] = R"(
    {
        "net_instance_id": "superpod1_1",
        "endpoint_list": [
            {
                "protocol": "roce",
                "comm_id": "127.0.0.1",
                "placement": "host"
            },
            {
                "protocol": "ub_ctp",
                "comm_id": "000000000000000000000000c0a80463",
                "placement": "device",
                "dst_eid": "000000000000000000000000c0a80563"
            }
        ],
        "version": "1.3"
    }
    )";

    options2[hixl::OPTION_LOCAL_COMM_RES] = R"(
    {
        "net_instance_id": "superpod2_2",
        "endpoint_list": [
            {
                "protocol": "ub_ctp",
                "comm_id": "000000000000000000000000c0a80463",
                "placement": "device",
                "dst_eid": "000000000000000000000000c0a80563"
            },
            {
                "protocol": "roce",
                "comm_id": "127.0.0.1",
                "placement": "host"
            }
        ],
        "version": "1.3"
    }
    )";
  }

  void TearDown() override {
    llm::AclRuntimeStub::Reset();
    llm::MmpaStub::GetInstance().Reset();
    for (const auto &path : temp_files_) {
      (void)remove(path.c_str());
    }
    if (old_intra_roce_enable_.empty()) {
      unsetenv("HCCL_INTRA_ROCE_ENABLE");
    } else {
      setenv("HCCL_INTRA_ROCE_ENABLE", old_intra_roce_enable_.c_str(), 1);
    }
  }

  void Register(HixlEngine &engine, int32_t *ptr, MemHandle &handle) {
    MemDesc mem{};
    mem.addr = reinterpret_cast<uintptr_t>(ptr);
    mem.len = sizeof(int32_t);
    EXPECT_EQ(engine.RegisterMem(mem, MEM_DEVICE, handle), SUCCESS);
  }

  std::shared_ptr<llm::LogCaptureStub> SetupLogCapture(const std::vector<std::string> &patterns) {
    auto log_capture = std::make_shared<llm::LogCaptureStub>();
    for (const auto &pattern : patterns) {
      log_capture->AddCapturePattern(pattern);
    }
    log_capture->SetLevelInfo();
    llm::SlogStub::SetInstance(log_capture);
    return log_capture;
  }

  void VerifyLogCapture(const std::shared_ptr<llm::LogCaptureStub> &log_capture,
                        const std::vector<std::string> &patterns) {
    EXPECT_TRUE(log_capture->WaitForAllPatternsCaptured(kCaptureLogTimeoutMs));
    for (const auto &pattern : patterns) {
      EXPECT_TRUE(log_capture->IsPatternCaptured(pattern)) << "Log pattern capture failed: " << pattern;
    }
    llm::SlogStub::SetInstance(nullptr);
  }

  void InitializeAndConnectEngines(HixlEngine &engine1, const std::map<AscendString, AscendString> &opts1,
                                   HixlEngine &engine2, const std::map<AscendString, AscendString> &opts2) {
    {
      HixlOptions parsed;
      ASSERT_EQ(HixlOptions::Parse(opts1, parsed), SUCCESS);
      EXPECT_EQ(engine1.Initialize(parsed), SUCCESS);
    }
    {
      HixlOptions parsed;
      ASSERT_EQ(HixlOptions::Parse(opts2, parsed), SUCCESS);
      EXPECT_EQ(engine2.Initialize(parsed), SUCCESS);
    }
    EXPECT_EQ(engine1.Connect("127.0.0.1:26300", kTimeOut), SUCCESS);
  }

  void InitAutoConnectEngines(HixlEngine &engine1, HixlEngine &engine2, int32_t &src, int32_t &dst,
                              MemHandle &handle1, MemHandle &handle2) {
    std::map<AscendString, AscendString> auto_connect_options = options1;
    auto_connect_options[hixl::OPTION_AUTO_CONNECT] = "1";
    HixlOptions parsed1;
    ASSERT_EQ(HixlOptions::Parse(auto_connect_options, parsed1), SUCCESS);
    EXPECT_EQ(engine1.Initialize(parsed1), SUCCESS);

    HixlOptions parsed2;
    ASSERT_EQ(HixlOptions::Parse(options2, parsed2), SUCCESS);
    EXPECT_EQ(engine2.Initialize(parsed2), SUCCESS);

    Register(engine1, &src, handle1);
    Register(engine2, &dst, handle2);
  }

  void CreateAndInitEngine(HixlEngine &engine, const std::map<AscendString, AscendString> &opts) {
    HixlOptions parsed;
    EXPECT_EQ(HixlOptions::Parse(opts, parsed), SUCCESS);
    EXPECT_EQ(engine.Initialize(parsed), SUCCESS);
  }

  TransferStatus WaitForTransferStatus(HixlEngine &engine, TransferReq req) {
    TransferStatus status = TransferStatus::WAITING;
    for (int32_t i = 0; i < kMaxRetryCount && status == TransferStatus::WAITING; i++) {
      engine.GetTransferStatus(req, status);
      std::this_thread::sleep_for(std::chrono::milliseconds(kInterval));
    }
    return status;
  }

  void TransferSyncTest(const std::string &addr1, const std::string &addr2, const std::string &connect_addr) {
    HixlEngine engine1(AscendString(addr1.c_str()));
    CreateAndInitEngine(engine1, options1);
    HixlEngine engine2(AscendString(addr2.c_str()));
    CreateAndInitEngine(engine2, options2);

    int32_t num1 = 1;
    MemHandle handle1 = nullptr;
    Register(engine1, &num1, handle1);

    int32_t num2 = 2;
    MemHandle handle2 = nullptr;
    Register(engine2, &num2, handle2);

    AscendString remote(connect_addr.c_str());
    EXPECT_EQ(engine1.Connect(remote, kTimeOut), SUCCESS);
    TransferOpDesc desc{reinterpret_cast<uintptr_t>(&num1), reinterpret_cast<uintptr_t>(&num2), sizeof(int32_t)};
    EXPECT_EQ(engine1.TransferSync(remote, READ, {desc}, kTimeOut), SUCCESS);
    EXPECT_EQ(num1, 2);
    num1 = 1;
    EXPECT_EQ(engine1.TransferSync(remote, WRITE, {desc}, kTimeOut), SUCCESS);
    EXPECT_EQ(num2, 1);

    EXPECT_EQ(engine1.Disconnect(remote, kTimeOut), SUCCESS);
    EXPECT_EQ(engine1.DeregisterMem(handle1), SUCCESS);
    EXPECT_EQ(engine2.DeregisterMem(handle2), SUCCESS);
    engine1.Finalize();
    engine2.Finalize();
  }

  void CleanupEngines(HixlEngine &engine1, HixlEngine &engine2) {
    engine1.Disconnect();
    engine1.Finalize();
    engine2.Disconnect();
    engine2.Finalize();
  }

  std::vector<EndpointConfig> InitEngineAndGetEndpoints(HixlEngine &engine, const std::string &local_comm_res) {
    auto options = BuildOptions(local_comm_res);
    HixlOptions parsed;
    EXPECT_EQ(HixlOptions::Parse(options, parsed), SUCCESS);
    EXPECT_EQ(engine.Initialize(parsed), SUCCESS);
    return engine.endpoint_list_;
  }

  std::shared_ptr<MockEngineAclRuntimeStub> acl_stub_;

  void SetSocStub(const std::string &soc_name, int32_t device_id, int32_t phy_device_id, int64_t super_device_id,
                  int64_t super_pod_id) {
    acl_stub_ = endpoint_test::CreateAclRuntimeStub(soc_name, device_id, phy_device_id, super_device_id, super_pod_id);
    llm::AclRuntimeStub::SetInstance(acl_stub_);
  }

  std::map<AscendString, AscendString> BuildOptions(const std::string &local_comm_res) {
    std::map<AscendString, AscendString> options;
    options[adxl::OPTION_LOCAL_COMM_RES] = AscendString(local_comm_res.c_str());
    return options;
  }

  void SetHccnConfContent(const std::string &content) {
    const std::string file_path = test::CreateTempFileWithContent("/tmp/hixl_engine_ut_XXXXXX", content);
    ASSERT_FALSE(file_path.empty());
    temp_files_.emplace_back(file_path);
    mmpa_stub_->real_path_ok_ = true;
    mmpa_stub_->access_ok_ = true;
    mmpa_stub_->fake_real_path_ = file_path;
  }

 private:
  std::shared_ptr<MockEngineMmpaStub> mmpa_stub_;
  std::vector<std::string> temp_files_;
  std::string old_intra_roce_enable_;
};

TEST_F(HixlEngineTest, EngineFactoryUsesHixlEngineWhenProtocolDescConfigured) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"comm_resource_config.protocol_desc":"roce:device"})";
  HixlOptions parsed;
  auto engine = EngineFactory::CreateEngine("127.0.0.1:26000", options, parsed);
  ASSERT_NE(engine, nullptr);
  EXPECT_NE(dynamic_cast<HixlEngine *>(engine.get()), nullptr);
}

TEST_F(HixlEngineTest, InitializeSetsClientListenPortFromGlobalResourceConfig) {
  std::map<AscendString, AscendString> options = options1;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"comm_resource_config.listen_port":26301})";
  HixlOptions parsed;
  ASSERT_EQ(HixlOptions::Parse(options, parsed), SUCCESS);

  HixlEngine engine("127.0.0.1");
  EXPECT_EQ(engine.Initialize(parsed), SUCCESS);

  ClientConfig config{};
  std::vector<MemInfo> mem_info_list;
  engine.BuildClientConfig(AscendString("127.0.0.1:26300"), config, mem_info_list, kTimeOut);
  ASSERT_TRUE(config.local_listen_port.has_value());
  EXPECT_EQ(*config.local_listen_port, 26301U);
  engine.Finalize();
}

TEST_F(HixlEngineTest, InitializeWithoutListenPortDoesNotSetClientListenPort) {
  HixlOptions parsed;
  ASSERT_EQ(HixlOptions::Parse(options1, parsed), SUCCESS);

  HixlEngine engine("127.0.0.1");
  EXPECT_EQ(engine.Initialize(parsed), SUCCESS);

  ClientConfig config{};
  std::vector<MemInfo> mem_info_list;
  engine.BuildClientConfig(AscendString("127.0.0.1:26300"), config, mem_info_list, kTimeOut);
  EXPECT_FALSE(config.local_listen_port.has_value());
  engine.Finalize();
}

TEST_F(HixlEngineTest, TestHixl) {
  SetSocStub("Ascend910B1", 0, 12, 99, 88);
  Hixl engine1;
  EXPECT_EQ(engine1.Initialize("127.0.0.1", options1), SUCCESS);

  Hixl engine2;
  EXPECT_EQ(engine2.Initialize("127.0.0.1:26300", options2), SUCCESS);

  int32_t src = 1;
  MemHandle handle1 = nullptr;
  test_helpers::RegisterInt32DeviceMem(engine1, src, handle1);

  int32_t dst = 2;
  MemHandle handle2 = nullptr;
  test_helpers::RegisterInt32DeviceMem(engine2, dst, handle2);

  EXPECT_EQ(engine1.Connect("127.0.0.1:26300", kTimeOut), SUCCESS);

  TransferOpDesc desc{reinterpret_cast<uintptr_t>(&src), reinterpret_cast<uintptr_t>(&dst), sizeof(int32_t)};
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26300", READ, {desc}), SUCCESS);
  EXPECT_EQ(src, 2);
  src = 1;
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26300", WRITE, {desc}), SUCCESS);
  EXPECT_EQ(dst, 1);

  NotifyDesc notify;
  std::string notify_name = "test_notify";
  notify.name = AscendString(notify_name.c_str());
  std::string notify_msg = "message";
  notify.notify_msg = AscendString(notify_msg.c_str());
  EXPECT_EQ(engine1.SendNotify("127.0.0.1:26300", notify, kTimeOut), SUCCESS);

  std::vector<NotifyDesc> notifies;
  EXPECT_EQ(engine2.GetNotifies(notifies), SUCCESS);
  ASSERT_EQ(notifies.size(), 1U);
  EXPECT_EQ(std::string(notifies[0].name.GetString()), notify_name);
  EXPECT_EQ(std::string(notifies[0].notify_msg.GetString()), notify_msg);

  EXPECT_EQ(engine1.Disconnect("127.0.0.1:26300"), SUCCESS);
  EXPECT_EQ(engine1.DeregisterMem(handle1), SUCCESS);
  EXPECT_EQ(engine2.DeregisterMem(handle2), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(HixlEngineTest, TestHixlEngineIPv4) {
  SetSocStub("Ascend910B1", 0, 12, 99, 88);
  TransferSyncTest("127.0.0.1", "127.0.0.1:26300", "127.0.0.1:26300");
}

TEST_F(HixlEngineTest, TestHixlEngineIPv6) {
  if (!test_helpers::CheckIpv6Supported()) {
    GTEST_SKIP() << "IPv6 not supported on this system";
  }
  SetSocStub("Ascend910B1", 0, 12, 99, 88);
  TransferSyncTest("[::1]", "[::1]:26302", "[::1]:26302");
}

TEST_F(HixlEngineTest, TestTransferAsync) {
  SetSocStub("Ascend910B1", 0, 12, 99, 88);
  HixlEngine engine1("127.0.0.1");
  CreateAndInitEngine(engine1, options1);
  HixlEngine engine2("127.0.0.1:26300");
  CreateAndInitEngine(engine2, options2);

  int32_t src = 1;
  MemHandle handle1 = nullptr;
  Register(engine1, &src, handle1);

  int32_t dst = 2;
  MemHandle handle2 = nullptr;
  Register(engine2, &dst, handle2);

  EXPECT_EQ(engine1.Connect("127.0.0.1:26300", kTimeOut), SUCCESS);
  TransferOpDesc desc{reinterpret_cast<uintptr_t>(&src), reinterpret_cast<uintptr_t>(&dst), sizeof(int32_t)};
  TransferReq req = nullptr;
  TransferStatus status;

  ASSERT_EQ(engine1.TransferAsync("127.0.0.1:26300", READ, {desc}, {}, req), SUCCESS);
  EXPECT_EQ(WaitForTransferStatus(engine1, req), TransferStatus::COMPLETED);
  EXPECT_EQ(src, 2);
  EXPECT_EQ(engine1.GetTransferStatus(req, status), PARAM_INVALID);
  EXPECT_EQ(status, TransferStatus::FAILED);

  src = 1;
  ASSERT_EQ(engine1.TransferAsync("127.0.0.1:26300", WRITE, {desc}, {}, req), SUCCESS);
  EXPECT_EQ(WaitForTransferStatus(engine1, req), TransferStatus::COMPLETED);
  EXPECT_EQ(dst, 1);
  EXPECT_EQ(engine1.GetTransferStatus(req, status), PARAM_INVALID);
  EXPECT_EQ(status, TransferStatus::FAILED);

  EXPECT_EQ(engine1.Disconnect("127.0.0.1:26300", kTimeOut), SUCCESS);
  EXPECT_EQ(engine1.DeregisterMem(handle1), SUCCESS);
  EXPECT_EQ(engine2.DeregisterMem(handle2), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(HixlEngineTest, TestInitFailed) {
  SetSocStub("Ascend910B1", 0, 12, 99, 88);
  // invalid ip
  std::string local_engine = "ad.0.0.1:26302";
  HixlEngine engine(AscendString(local_engine.c_str()));
  {
    HixlOptions parsed;
    HixlOptions::Parse(options1, parsed);
    EXPECT_EQ(engine.Initialize(parsed), PARAM_INVALID);
  }
  engine.Finalize();
}

TEST_F(HixlEngineTest, TestNotListenFailed) {
  SetSocStub("Ascend910B1", 0, 12, 99, 88);
  std::string local_engine = "127.0.0.1:26300";
  HixlEngine engine(AscendString(local_engine.c_str()));
  {
    HixlOptions parsed;
    ASSERT_EQ(HixlOptions::Parse(options1, parsed), SUCCESS);
    EXPECT_EQ(engine.Initialize(parsed), SUCCESS);
  }
  // not listen
  EXPECT_EQ(engine.Connect("127.0.0.1:26301", kTimeOut), FAILED);
  engine.Finalize();
}

TEST_F(HixlEngineTest, TestAlreadyConnectedFailed) {
  SetSocStub("Ascend910B1", 0, 12, 99, 88);
  HixlEngine engine1("127.0.0.1");
  CreateAndInitEngine(engine1, options1);
  HixlEngine engine2("127.0.0.1:26300");
  CreateAndInitEngine(engine2, options2);
  EXPECT_EQ(engine1.Connect("127.0.0.1:26300", kTimeOut), SUCCESS);
  EXPECT_EQ(engine1.Connect("127.0.0.1:26300", kTimeOut), ALREADY_CONNECTED);
  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(HixlEngineTest, TestDeregisterUnregisteredMem) {
  SetSocStub("Ascend910B1", 0, 12, 99, 88);
  std::string local_engine = "127.0.0.1";
  HixlEngine engine(AscendString(local_engine.c_str()));
  {
    HixlOptions parsed;
    ASSERT_EQ(HixlOptions::Parse(options1, parsed), SUCCESS);
    EXPECT_EQ(engine.Initialize(parsed), SUCCESS);
  }
  MemHandle handle = (MemHandle)0x100;
  // deregister unregister mem
  EXPECT_EQ(engine.DeregisterMem(handle), SUCCESS);
  engine.Finalize();
}

TEST_F(HixlEngineTest, TestGetTransferStatusWithInterrupt) {
  SetSocStub("Ascend910B1", 0, 12, 99, 88);
  HixlEngine engine1("127.0.0.1");
  CreateAndInitEngine(engine1, options1);
  HixlEngine engine2("127.0.0.1:26300");
  CreateAndInitEngine(engine2, options2);

  int32_t src = 1;
  int32_t dst = 2;
  MemHandle handle1 = nullptr;
  MemHandle handle2 = nullptr;
  Register(engine1, &src, handle1);
  Register(engine2, &dst, handle2);

  EXPECT_EQ(engine1.Connect("127.0.0.1:26300", kTimeOut), SUCCESS);

  TransferOpDesc desc{reinterpret_cast<uintptr_t>(&src), reinterpret_cast<uintptr_t>(&dst), sizeof(int32_t)};
  TransferReq req = nullptr;
  EXPECT_EQ(engine1.TransferAsync("127.0.0.1:26300", WRITE, {desc}, {}, req), SUCCESS);
  engine1.Disconnect("127.0.0.1:26300", kTimeOut);
  TransferStatus status = TransferStatus::WAITING;
  EXPECT_EQ(engine1.GetTransferStatus(req, status), PARAM_INVALID);
  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(HixlEngineTest, TestSendAndGetNotifies) {
  SetSocStub("Ascend910B1", 0, 12, 99, 88);
  HixlEngine engine1("127.0.0.1");
  CreateAndInitEngine(engine1, options1);
  HixlEngine engine2("127.0.0.1:26300");
  CreateAndInitEngine(engine2, options2);

  EXPECT_EQ(engine1.Connect("127.0.0.1:26300", kTimeOut), SUCCESS);
  auto client = engine1.client_manager_.GetClient("127.0.0.1:26300");
  ASSERT_NE(client, nullptr);
  EXPECT_GE(client->ctrl_socket_, 0);
  NotifyDesc notify;
  std::string notify_name = "test_notify";
  notify.name = AscendString(notify_name.c_str());
  std::string notify_msg = "message";
  notify.notify_msg = AscendString(notify_msg.c_str());
  EXPECT_EQ(engine1.SendNotify("127.0.0.1:26300", notify, kTimeOut), SUCCESS);
  EXPECT_GE(client->ctrl_socket_, 0);
  std::vector<NotifyDesc> notifies;
  EXPECT_EQ(engine2.GetNotifies(notifies), SUCCESS);
  ASSERT_EQ(notifies.size(), 1U);
  EXPECT_EQ(std::string(notifies[0].name.GetString()), notify_name);
  EXPECT_EQ(std::string(notifies[0].notify_msg.GetString()), notify_msg);
  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(HixlEngineTest, TestSendNotifyWithoutConnection) {
  HixlEngine engine1("127.0.0.1");
  HixlOptions parsed;
  ASSERT_EQ(HixlOptions::Parse(options1, parsed), SUCCESS);
  EXPECT_EQ(engine1.Initialize(parsed), SUCCESS);

  NotifyDesc notify;
  notify.name = AscendString("test_notify");
  notify.notify_msg = AscendString("message");
  EXPECT_EQ(engine1.SendNotify("127.0.0.1:26300", notify, kTimeOut), NOT_CONNECTED);

  engine1.Finalize();
}

TEST_F(HixlEngineTest, TestMultipleNotifyMessages) {
  HixlEngine engine1("127.0.0.1");
  HixlEngine engine2("127.0.0.1:26300");
  InitializeAndConnectEngines(engine1, options1, engine2, options2);

  constexpr size_t kNotifyCount = 5U;
  for (size_t i = 0; i < kNotifyCount; ++i) {
    NotifyDesc notify;
    const std::string notify_name = "test_notify" + std::to_string(i);
    const std::string notify_msg = "message " + std::to_string(i);
    notify.name = AscendString(notify_name.c_str());
    notify.notify_msg = AscendString(notify_msg.c_str());
    EXPECT_EQ(engine1.SendNotify("127.0.0.1:26300", notify, kTimeOut), SUCCESS);
  }

  std::vector<NotifyDesc> notifies;
  EXPECT_EQ(engine2.GetNotifies(notifies), SUCCESS);
  ASSERT_EQ(notifies.size(), kNotifyCount);
  for (size_t i = 0; i < kNotifyCount; ++i) {
    EXPECT_EQ(std::string(notifies[i].name.GetString()), "test_notify" + std::to_string(i));
    EXPECT_EQ(std::string(notifies[i].notify_msg.GetString()), "message " + std::to_string(i));
  }

  notifies.clear();
  EXPECT_EQ(engine2.GetNotifies(notifies), SUCCESS);
  EXPECT_TRUE(notifies.empty());
  CleanupEngines(engine1, engine2);
}

TEST_F(HixlEngineTest, TestNotifyWithTooLongMessage) {
  HixlEngine engine1("127.0.0.1");
  HixlEngine engine2("127.0.0.1:26300");
  InitializeAndConnectEngines(engine1, options1, engine2, options2);

  NotifyDesc notify;
  notify.name = AscendString("test_notify");
  std::string long_msg(1025U, 'a');
  notify.notify_msg = AscendString(long_msg.c_str());

  EXPECT_EQ(engine1.SendNotify("127.0.0.1:26300", notify, kTimeOut), PARAM_INVALID);
  CleanupEngines(engine1, engine2);
}

TEST_F(HixlEngineTest, TestNotifyWithTooLongName) {
  HixlEngine engine1("127.0.0.1");
  HixlEngine engine2("127.0.0.1:26300");
  InitializeAndConnectEngines(engine1, options1, engine2, options2);

  NotifyDesc notify;
  std::string long_name(1025U, 'n');
  notify.name = AscendString(long_name.c_str());
  notify.notify_msg = AscendString("message");

  EXPECT_EQ(engine1.SendNotify("127.0.0.1:26300", notify, kTimeOut), PARAM_INVALID);
  CleanupEngines(engine1, engine2);
}

TEST_F(HixlEngineTest, TestNotifyWithMaxLengthFields) {
  HixlEngine engine1("127.0.0.1");
  HixlEngine engine2("127.0.0.1:26300");
  InitializeAndConnectEngines(engine1, options1, engine2, options2);

  NotifyDesc notify;
  std::string max_name(1024U, 'n');
  std::string max_msg(1024U, 'm');
  notify.name = AscendString(max_name.c_str());
  notify.notify_msg = AscendString(max_msg.c_str());

  EXPECT_EQ(engine1.SendNotify("127.0.0.1:26300", notify, kTimeOut), SUCCESS);
  std::vector<NotifyDesc> notifies;
  EXPECT_EQ(engine2.GetNotifies(notifies), SUCCESS);
  ASSERT_EQ(notifies.size(), 1U);
  EXPECT_EQ(std::string(notifies[0].name.GetString()), max_name);
  EXPECT_EQ(std::string(notifies[0].notify_msg.GetString()), max_msg);
  CleanupEngines(engine1, engine2);
}

TEST_F(HixlEngineTest, TestAutoConnectSync) {
  HixlEngine engine1("127.0.0.1");
  HixlEngine engine2("127.0.0.1:26300");
  int32_t src = 1;
  int32_t dst = 2;
  MemHandle handle1 = nullptr;
  MemHandle handle2 = nullptr;
  InitAutoConnectEngines(engine1, engine2, src, dst, handle1, handle2);

  TransferOpDesc desc{reinterpret_cast<uintptr_t>(&src), reinterpret_cast<uintptr_t>(&dst), sizeof(int32_t)};
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26300", READ, {desc}, kTimeOut), SUCCESS);
  EXPECT_EQ(src, 2);
  src = 3;
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26300", WRITE, {desc}, kTimeOut), SUCCESS);
  EXPECT_EQ(dst, 3);

  EXPECT_EQ(engine1.Disconnect("127.0.0.1:26300", kTimeOut), SUCCESS);
  EXPECT_EQ(engine1.DeregisterMem(handle1), SUCCESS);
  EXPECT_EQ(engine2.DeregisterMem(handle2), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(HixlEngineTest, TestInitializeUsesParsedAutoConnectOption) {
  std::map<AscendString, AscendString> auto_connect_options = options1;
  auto_connect_options[hixl::OPTION_AUTO_CONNECT] = "1";
  HixlOptions parsed;
  ASSERT_EQ(HixlOptions::Parse(auto_connect_options, parsed), SUCCESS);

  HixlEngine engine("127.0.0.1");
  EXPECT_EQ(engine.Initialize(parsed), SUCCESS);
  EXPECT_TRUE(engine.auto_connect_);
  engine.Finalize();
}

TEST_F(HixlEngineTest, TestConcurrentConnectReturnsSingleSuccess) {
  SetSocStub("Ascend910B1", 0, 12, 99, 88);
  HixlEngine engine1("127.0.0.1");
  HixlOptions parsed1;
  ASSERT_EQ(HixlOptions::Parse(options1, parsed1), SUCCESS);
  EXPECT_EQ(engine1.Initialize(parsed1), SUCCESS);
  HixlEngine engine2("127.0.0.1:26300");
  HixlOptions parsed2;
  ASSERT_EQ(HixlOptions::Parse(options2, parsed2), SUCCESS);
  EXPECT_EQ(engine2.Initialize(parsed2), SUCCESS);
  Status first_ret = FAILED;
  Status second_ret = FAILED;
  std::thread first_thread([&engine1, &first_ret]() { first_ret = engine1.Connect("127.0.0.1:26300", kTimeOut); });
  std::thread second_thread([&engine1, &second_ret]() { second_ret = engine1.Connect("127.0.0.1:26300", kTimeOut); });
  first_thread.join();
  second_thread.join();

  int32_t success_count = 0;
  int32_t already_connected_count = 0;
  for (Status ret : {first_ret, second_ret}) {
    if (ret == SUCCESS) {
      ++success_count;
    } else if (ret == ALREADY_CONNECTED) {
      ++already_connected_count;
    }
  }
  EXPECT_EQ(success_count, 1);
  EXPECT_EQ(already_connected_count, 1);
  EXPECT_NE(engine1.client_manager_.GetClient("127.0.0.1:26300"), nullptr);

  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(HixlEngineTest, TestParseAutoConnectConfigInvalid) {
  for (const char *invalid_auto_connect : {"2", "invalid"}) {
    std::map<AscendString, AscendString> invalid_options = options1;
    invalid_options[hixl::OPTION_AUTO_CONNECT] = AscendString(invalid_auto_connect);
    HixlOptions parsed;
    EXPECT_EQ(HixlOptions::Parse(invalid_options, parsed), PARAM_INVALID);
  }
}

TEST_F(HixlEngineTest, TestAutoConnectAutoDisconnect) {
  HixlEngine engine1("127.0.0.1");
  HixlEngine engine2("127.0.0.1:26300");
  int32_t src = 1;
  int32_t dst = 2;
  MemHandle handle1 = nullptr;
  MemHandle handle2 = nullptr;
  InitAutoConnectEngines(engine1, engine2, src, dst, handle1, handle2);

  int32_t unregistered = 3;
  TransferOpDesc invalid_desc{reinterpret_cast<uintptr_t>(&unregistered), reinterpret_cast<uintptr_t>(&dst),
                              sizeof(int32_t)};
  EXPECT_NE(engine1.TransferSync("127.0.0.1:26300", READ, {invalid_desc}, kTimeOut), SUCCESS);
  // 如果断链成功，显式 Disconnect 应返回失败（因为 client 已经不存在了）
  EXPECT_NE(engine1.Disconnect("127.0.0.1:26300", kTimeOut), SUCCESS);
  EXPECT_EQ(engine1.DeregisterMem(handle1), SUCCESS);
  EXPECT_EQ(engine2.DeregisterMem(handle2), SUCCESS);

  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(HixlEngineTest, TestParseTcAndSlWithValidValue) {
  std::string tc_log_pattern = "Set rdma traffic class to 128";
  std::string sl_log_pattern = "Set rdma service level to 5";
  std::string channel_desc_log_pattern = "[channel] ROCE attributes set, tc=128, sl=5";
  auto log_capture = SetupLogCapture({tc_log_pattern, sl_log_pattern, channel_desc_log_pattern});

  mmSetEnv("HCCL_RDMA_TC", "120", 1);
  mmSetEnv("HCCL_RDMA_SL", "3", 1);
  options1[hixl::OPTION_RDMA_TRAFFIC_CLASS] = "128";
  options1[adxl::OPTION_RDMA_SERVICE_LEVEL] = "5";

  HixlEngine engine1("127.0.0.1");
  HixlEngine engine2("127.0.0.1:26300");
  InitializeAndConnectEngines(engine1, options1, engine2, options2);
  CleanupEngines(engine1, engine2);

  unsetenv("HCCL_RDMA_TC");
  unsetenv("HCCL_RDMA_SL");

  VerifyLogCapture(log_capture, {tc_log_pattern, sl_log_pattern, channel_desc_log_pattern});
}

TEST_F(HixlEngineTest, TestParseTcSlInvalidValue) {
  options1[hixl::OPTION_RDMA_TRAFFIC_CLASS] = "-1";
  {
    HixlOptions parsed;
    EXPECT_EQ(HixlOptions::Parse(options1, parsed), PARAM_INVALID);
  }
  options1[hixl::OPTION_RDMA_TRAFFIC_CLASS] = "256";
  {
    HixlOptions parsed;
    EXPECT_EQ(HixlOptions::Parse(options1, parsed), PARAM_INVALID);
  }
  options1[hixl::OPTION_RDMA_TRAFFIC_CLASS] = "invalid";
  {
    HixlOptions parsed;
    EXPECT_EQ(HixlOptions::Parse(options1, parsed), PARAM_INVALID);
  }
  options2[hixl::OPTION_RDMA_SERVICE_LEVEL] = "8";
  {
    HixlOptions parsed;
    EXPECT_EQ(HixlOptions::Parse(options2, parsed), PARAM_INVALID);
  }
  options2[hixl::OPTION_RDMA_SERVICE_LEVEL] = "-1";
  {
    HixlOptions parsed;
    EXPECT_EQ(HixlOptions::Parse(options2, parsed), PARAM_INVALID);
  }
}

TEST_F(HixlEngineTest, TestParseTcAndSlWithEnv) {
  std::string tc_log_pattern = "Set rdma traffic class to 128";
  std::string sl_log_pattern = "Set rdma service level to 5";
  std::string channel_desc_log_pattern = "[channel] ROCE attributes set, tc=128, sl=5";
  auto log_capture = SetupLogCapture({tc_log_pattern, sl_log_pattern, channel_desc_log_pattern});

  mmSetEnv("HCCL_RDMA_TC", "128", 1);
  mmSetEnv("HCCL_RDMA_SL", "5", 1);

  HixlEngine engine1("127.0.0.1");
  HixlEngine engine2("127.0.0.1:26300");
  InitializeAndConnectEngines(engine1, options1, engine2, options2);
  CleanupEngines(engine1, engine2);

  unsetenv("HCCL_RDMA_TC");
  unsetenv("HCCL_RDMA_SL");

  VerifyLogCapture(log_capture, {tc_log_pattern, sl_log_pattern, channel_desc_log_pattern});
}

TEST_F(HixlEngineTest, TestParseTcAndSlWithDefault) {
  std::string channel_desc_log_pattern = "[channel] ROCE attributes set, tc=132, sl=4";
  auto log_capture = SetupLogCapture({channel_desc_log_pattern});

  HixlEngine engine1("127.0.0.1");
  HixlEngine engine2("127.0.0.1:26300");
  InitializeAndConnectEngines(engine1, options1, engine2, options2);
  CleanupEngines(engine1, engine2);

  VerifyLogCapture(log_capture, {channel_desc_log_pattern});
}

TEST_F(HixlEngineTest, TestTcAndSlWithUb) {
  std::string channel_desc_log_pattern = "[channel] ROCE attributes set";
  auto log_capture = SetupLogCapture({channel_desc_log_pattern});

  std::map<AscendString, AscendString> options3;
  options3[hixl::OPTION_LOCAL_COMM_RES] = R"(
    {
        "net_instance_id": "superpod1_1",
        "endpoint_list": [
            {
                "protocol": "ub_ctp",
                "comm_id": "000000000000000000000000c0a80563",
                "placement": "device",
                "dst_eid": "000000000000000000000000c0a80463"
            }
        ],
        "version": "1.3"
    }
    )";
  HixlEngine engine1("127.0.0.1");
  HixlEngine engine2("127.0.0.1:26300");
  InitializeAndConnectEngines(engine1, options1, engine2, options3);
  CleanupEngines(engine1, engine2);

  EXPECT_FALSE(log_capture->WaitForAllPatternsCaptured(kCaptureLogTimeoutMs));
  llm::SlogStub::SetInstance(nullptr);
}

TEST_F(HixlEngineTest, TestInitializeFillDeviceInfoForV2FullConfigured) {
  SetSocStub("Ascend910B1", 0, 12, 99, 88);

  const std::string local_comm_res =
      BuildLocalCommRes("sp_v2", "1.3", {BuildDeviceRoceEndpoint("127.0.0.1"), BuildDeviceHccsEndpoint("5")});

  HixlEngine engine("127.0.0.1");
  auto endpoints = InitEngineAndGetEndpoints(engine, local_comm_res);
  ASSERT_EQ(endpoints.size(), 2U);

  for (const auto &ep : endpoints) {
    EXPECT_EQ(ep.placement, kPlacementDevice);
    EXPECT_EQ(ep.device_info.phy_device_id, 12);
    EXPECT_EQ(ep.device_info.super_device_id, -1);
    EXPECT_EQ(ep.device_info.super_pod_id, -1);
  }

  engine.Finalize();
}

TEST_F(HixlEngineTest, TestInitializeFillDeviceInfoForV3FullConfigured) {
  SetSocStub("Ascend910_9391", 1, 23, 45, 67);

  const std::string local_comm_res =
      BuildLocalCommRes("sp_v3", "1.3", {BuildDeviceRoceEndpoint("127.0.0.1"), BuildDeviceHccsEndpoint("7")});

  HixlEngine engine("127.0.0.1");
  auto endpoints = InitEngineAndGetEndpoints(engine, local_comm_res);
  ASSERT_EQ(endpoints.size(), 2U);

  for (const auto &ep : endpoints) {
    EXPECT_EQ(ep.placement, kPlacementDevice);
    EXPECT_EQ(ep.device_info.phy_device_id, 23);
    EXPECT_EQ(ep.device_info.super_device_id, 45);
    EXPECT_EQ(ep.device_info.super_pod_id, 67);
  }

  engine.Finalize();
}

TEST_F(HixlEngineTest, TestInitializeParsesConfiguredEndpointListRegardlessOfVersion) {
  SetSocStub("Ascend910_9391", 1, 23, 45, 67);

  const std::string local_comm_res = BuildLocalCommRes("sp_old", "1.2", {BuildDeviceRoceEndpoint("127.0.0.1")});

  HixlEngine engine("127.0.0.1");
  auto endpoints = InitEngineAndGetEndpoints(engine, local_comm_res);
  ASSERT_EQ(endpoints.size(), 1U);
  EXPECT_EQ(endpoints[0].protocol, kProtocolRoce);
  EXPECT_EQ(endpoints[0].placement, kPlacementDevice);
  EXPECT_EQ(endpoints[0].net_instance_id, "sp_old");
  EXPECT_EQ(endpoints[0].device_info.phy_device_id, 23);
  EXPECT_EQ(endpoints[0].device_info.super_device_id, 45);
  EXPECT_EQ(endpoints[0].device_info.super_pod_id, 67);

  engine.Finalize();
}

TEST_F(HixlEngineTest, TestInitializeFillDeviceInfoOnlyForDevicePlacement) {
  SetSocStub("Ascend910_9391", 1, 23, 45, 67);

  const std::string local_comm_res =
      BuildLocalCommRes("sp_mix", "1.3", {BuildHostRoceEndpoint("127.0.0.1"), BuildDeviceHccsEndpoint("7")});

  HixlEngine engine("127.0.0.1");
  auto endpoints = InitEngineAndGetEndpoints(engine, local_comm_res);
  ASSERT_EQ(endpoints.size(), 2U);

  const auto &host_ep = endpoints[0];
  EXPECT_EQ(host_ep.placement, kPlacementHost);
  EXPECT_EQ(host_ep.device_info.phy_device_id, -1);
  EXPECT_EQ(host_ep.device_info.super_device_id, -1);
  EXPECT_EQ(host_ep.device_info.super_pod_id, -1);

  const auto &device_ep = endpoints[1];
  EXPECT_EQ(device_ep.placement, kPlacementDevice);
  EXPECT_EQ(device_ep.device_info.phy_device_id, 23);
  EXPECT_EQ(device_ep.device_info.super_device_id, 45);
  EXPECT_EQ(device_ep.device_info.super_pod_id, 67);

  engine.Finalize();
}

TEST_F(HixlEngineTest, TestInitializeAutoGenerateForV2FillsDeviceInfo) {
  SetSocStub("Ascend910B1", 0, 12, 99, 88);
  SetHccnConfContent("address_12=10.10.10.12\n");

  HixlEngine engine("127.0.0.1");
  auto endpoints = InitEngineAndGetEndpoints(engine, BuildVersionOnlyLocalCommRes("1.3"));
  ASSERT_EQ(endpoints.size(), 2U);

  const auto &roce_ep = endpoints[0];
  EXPECT_EQ(roce_ep.protocol, kProtocolRoce);
  EXPECT_EQ(roce_ep.comm_id, "10.10.10.12");
  EXPECT_EQ(roce_ep.net_instance_id, "127.0.0.1");
  EXPECT_EQ(roce_ep.device_info.phy_device_id, 12);
  EXPECT_EQ(roce_ep.device_info.super_device_id, -1);
  EXPECT_EQ(roce_ep.device_info.super_pod_id, -1);

  const auto &hccs_ep = endpoints[1];
  EXPECT_EQ(hccs_ep.protocol, kProtocolHccs);
  EXPECT_EQ(hccs_ep.comm_id, "12");
  EXPECT_EQ(hccs_ep.net_instance_id, "127.0.0.1");
  EXPECT_EQ(hccs_ep.device_info.phy_device_id, 12);
  EXPECT_EQ(hccs_ep.device_info.super_device_id, -1);
  EXPECT_EQ(hccs_ep.device_info.super_pod_id, -1);

  engine.Finalize();
}

TEST_F(HixlEngineTest, TestInitializeAutoGenerateForV3FillsDeviceInfo) {
  SetSocStub("Ascend910_9391", 1, 23, 45, 67);
  SetHccnConfContent("address_23=10.10.10.23\n");

  HixlEngine engine("127.0.0.1");
  auto endpoints = InitEngineAndGetEndpoints(engine, BuildVersionOnlyLocalCommRes("1.3"));
  ASSERT_EQ(endpoints.size(), 2U);

  const auto &roce_ep = endpoints[0];
  EXPECT_EQ(roce_ep.protocol, kProtocolRoce);
  EXPECT_EQ(roce_ep.comm_id, "10.10.10.23");
  EXPECT_EQ(roce_ep.net_instance_id, "67");
  EXPECT_EQ(roce_ep.device_info.phy_device_id, 23);
  EXPECT_EQ(roce_ep.device_info.super_device_id, 45);
  EXPECT_EQ(roce_ep.device_info.super_pod_id, 67);

  const auto &hccs_ep = endpoints[1];
  EXPECT_EQ(hccs_ep.protocol, kProtocolHccs);
  EXPECT_EQ(hccs_ep.comm_id, "23");
  EXPECT_EQ(hccs_ep.net_instance_id, "67");
  EXPECT_EQ(hccs_ep.device_info.phy_device_id, 23);
  EXPECT_EQ(hccs_ep.device_info.super_device_id, 45);
  EXPECT_EQ(hccs_ep.device_info.super_pod_id, 67);

  engine.Finalize();
}

class MockClientHandler : public IClientHandler {
 public:
  Status Connect(uint32_t) override { return SUCCESS; }
  Status RegisterMem(const MemInfo &) override { return SUCCESS; }
  Status TransferAsync(const std::vector<TransferOpDesc> &, TransferOp, TransferReq &) override { return SUCCESS; }
  Status TransferSync(const std::vector<TransferOpDesc> &, TransferOp, uint32_t) override { return SUCCESS; }
  Status GetTransferStatus(const TransferReq &, TransferStatus &) override { return SUCCESS; }
  Status Finalize() override { return SUCCESS; }
};

static ClientPtr CreateMockClient() {
  ClientConfig config{};
  config.remote_engine = "127.0.0.1:26300";
  auto client = std::make_shared<HixlClient>("127.0.0.1", 26300, config);
  auto handler = std::make_unique<MockClientHandler>();
  client->client_handler_ = std::move(handler);
  client->is_connected_ = true;
  return client;
}

static bool AttachClosedCtrlSocket(const ClientPtr &client) {
  int32_t fds[2] = {-1, -1};
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
    return false;
  }
  client->ctrl_socket_ = fds[0];
  close(fds[1]);
  return true;
}

TEST(ClientManagerTest, HeartbeatInitializeStartsAndStopsThread) {
  ClientManager manager;
  EXPECT_EQ(manager.Initialize(true), SUCCESS);
  EXPECT_TRUE(manager.heartbeat_sender_.joinable());
  EXPECT_EQ(manager.Finalize(), SUCCESS);
}

TEST(ClientManagerTest, HeartbeatInitializeWithoutAutoConnectDoesNotStartThread) {
  ClientManager manager;
  EXPECT_EQ(manager.Initialize(false), SUCCESS);
  EXPECT_FALSE(manager.heartbeat_sender_.joinable());
  EXPECT_EQ(manager.Finalize(), SUCCESS);
}

TEST(ClientManagerTest, GetOrCreateClientReturnsExistingClient) {
  ClientManager manager;
  EXPECT_EQ(manager.Initialize(false), SUCCESS);

  auto client = CreateMockClient();
  manager.clients_["127.0.0.1:26300"] = client;

  ClientConfig config{};
  config.remote_engine = "127.0.0.1:26300";
  ClientPtr returned_client = nullptr;
  EXPECT_EQ(manager.GetOrCreateClient(config, {}, kTimeOut, returned_client), ALREADY_CONNECTED);
  EXPECT_EQ(returned_client, client);
  EXPECT_EQ(manager.Finalize(), SUCCESS);
}

TEST(ClientManagerTest, HeartbeatDestroysUnhealthyClient) {
  ClientManager manager;
  EXPECT_EQ(manager.Initialize(false), SUCCESS);
  CtrlMsgPlugin::Initialize();
  auto client = CreateMockClient();
  ASSERT_TRUE(AttachClosedCtrlSocket(client));
  manager.clients_["127.0.0.1:26300"] = client;
  manager.SendHeartbeat();
  EXPECT_EQ(manager.GetClient("127.0.0.1:26300"), nullptr);
  EXPECT_EQ(manager.Finalize(), SUCCESS);
}

TEST(ClientManagerTest, HeartbeatDestroysClientWithInvalidSocket) {
  ClientManager manager;
  EXPECT_EQ(manager.Initialize(false), SUCCESS);
  auto client = CreateMockClient();
  manager.clients_["127.0.0.1:26300"] = client;
  manager.SendHeartbeat();
  EXPECT_EQ(manager.GetClient("127.0.0.1:26300"), nullptr);
  EXPECT_EQ(manager.Finalize(), SUCCESS);
}
}  // namespace hixl
