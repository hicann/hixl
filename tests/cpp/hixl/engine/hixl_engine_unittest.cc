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
#include <cstdint>
#include <cstdlib>
#include <algorithm>
#include <condition_variable>
#include <cstring>
#include <fstream>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <gtest/gtest.h>
#include "depends/host_roce_test_helper.h"
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "depends/ctrl_msg_test_helper.h"

#include "ascendcl_stub.h"
#include "engine/endpoint_test_utils.h"
#define private public
#include "engine/hixl_engine.h"
#include "engine/direct_client_handler.h"
#include "engine/ub_client_handler.h"
#include "cs/hixl_cs_server.h"
#undef private
#include "engine/client_handler.h"
#include "hixl/hixl_types.h"
#include "engine/engine_factory.h"
#include "common/ctrl_msg_plugin.h"
#include "engine/hixl_options.h"
#include "cs/hixl_cs_client.h"
#include "hixl/hixl.h"
#include "slog_stub.h"
#include "test_mmpa_utils.h"
#include "depends/sys_api/src/sys_api_wrap.h"
#include "depends/mmpa/src/mmpa_stub.h"
#include "hixl_test_helpers.h"

#include "common/tls/tls_manager.h"
#include "common/tls/tls_test_helper.h"
#include "common/scope_guard.h"
#include "nlohmann/json.hpp"

namespace hixl {

namespace {
constexpr const int32_t kTimeOut = 1000;
constexpr const int32_t kMaxRetryCount = 10;
constexpr const int32_t kInterval = 10;
constexpr const uint32_t kCaptureLogTimeoutMs = 1000U;
constexpr const char kHccnToolPath[] = "/usr/local/Ascend/driver/tools/hccn_tool";

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

std::string BuildUbCtpEndpoint(const std::string &comm_id, const std::string &placement, const std::string &dst_eid) {
  std::ostringstream oss;
  oss << "      {\n";
  oss << "        \"protocol\": \"ub_ctp\",\n";
  oss << "        \"comm_id\": \"" << comm_id << "\",\n";
  oss << "        \"placement\": \"" << placement << "\",\n";
  oss << "        \"dst_eid\": \"" << dst_eid << "\"\n";
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
 private:
  class EngineSysApiStub : public test::TestMmpaStub {
   public:
    INT32 Access(const CHAR *path_name) override {
      if (path_name != nullptr && std::strcmp(path_name, kHccnToolPath) == 0) {
        return EN_ERROR;
      }
      return test::TestMmpaStub::Access(path_name);
    }
  };

 protected:
  std::map<AscendString, AscendString> options1;
  std::map<AscendString, AscendString> options2;
  std::map<AscendString, AscendString>
      options1_ub_pair;  // 与 options1 互补的 UB endpoint，用于同实例 UB 匹配（1条链路）
  std::map<AscendString, AscendString> options_4ub;       // 4条 UB 链路（D2D/D2H/H2D/H2H）
  std::map<AscendString, AscendString> options_4ub_pair;  // 与 options_4ub 互补的 UB endpoint
  std::shared_ptr<test::FakeHostRoceVerbs> native_verbs_;
  void SetUp() override {
    native_verbs_ = std::make_shared<test::FakeHostRoceVerbs>();
    SetHostRoceVerbsForTest(native_verbs_);
    ResetHcommVersionStub();
    SetSocStub("Ascend910B1", 0, 0, 9, 8);
    // TransferPool initialization loads device kernels, so MmpaStub must be ready before Create.
    mmpa_stub_ = std::make_shared<EngineSysApiStub>();
    mmpa_stub_->real_path_ok_ = true;
    mmpa_stub_->access_ok_ = true;
    hixl_test::InstallSysApiHooks(mmpa_stub_);
    CreateHccnTool();
    const char *old_intra_roce_enable = std::getenv("HCCL_INTRA_ROCE_ENABLE");
    old_intra_roce_enable_ = (old_intra_roce_enable == nullptr) ? "" : old_intra_roce_enable;
    unsetenv("HCCL_INTRA_ROCE_ENABLE");

    const std::string kRoceHost = BuildHostRoceEndpoint("127.0.0.1");

    // options1: 1条 UB 链路 + RoCE
    options1[hixl::OPTION_LOCAL_COMM_RES] =
        AscendString(BuildLocalCommRes("superpod1_1", "1.3",
                                       {kRoceHost, BuildUbCtpEndpoint("000000000000000000000000c0a80463", "device",
                                                                      "000000000000000000000000c0a80563")})
                         .c_str());

    // options2: 不同 net_instance_id，UB + RoCE 顺序不同
    options2[hixl::OPTION_LOCAL_COMM_RES] = AscendString(
        BuildLocalCommRes(
            "superpod2_2", "1.3",
            {BuildUbCtpEndpoint("000000000000000000000000c0a80463", "device", "000000000000000000000000c0a80563"),
             kRoceHost})
            .c_str());

    // 与 options1 同 net_instance_id，UB endpoint 的 comm_id/dst_eid 互换，用于 UB 成对匹配（1条链路）
    options1_ub_pair[hixl::OPTION_LOCAL_COMM_RES] =
        AscendString(BuildLocalCommRes("superpod1_1", "1.3",
                                       {kRoceHost, BuildUbCtpEndpoint("000000000000000000000000c0a80563", "device",
                                                                      "000000000000000000000000c0a80463")})
                         .c_str());

    // 4 条 UB 链路（D2D/D2H/H2D/H2H）：2 device + 2 host placement
    options_4ub[hixl::OPTION_LOCAL_COMM_RES] = AscendString(
        BuildLocalCommRes(
            "superpod1_1", "1.3",
            {kRoceHost,
             BuildUbCtpEndpoint("000000000000000000000000c0a81001", "device", "000000000000000000000000c0a81002"),
             BuildUbCtpEndpoint("000000000000000000000000c0a81003", "device", "000000000000000000000000c0a81004"),
             BuildUbCtpEndpoint("000000000000000000000000c0a81005", "host", "000000000000000000000000c0a81006"),
             BuildUbCtpEndpoint("000000000000000000000000c0a81007", "host", "000000000000000000000000c0a81008")})
            .c_str());

    // 与 options_4ub 互补：comm_id/dst_eid 互换，placement 对应 D2D/D2H/H2D/H2H
    options_4ub_pair[hixl::OPTION_LOCAL_COMM_RES] = AscendString(
        BuildLocalCommRes(
            "superpod1_1", "1.3",
            {kRoceHost,
             BuildUbCtpEndpoint("000000000000000000000000c0a81002", "device", "000000000000000000000000c0a81001"),
             BuildUbCtpEndpoint("000000000000000000000000c0a81004", "host", "000000000000000000000000c0a81003"),
             BuildUbCtpEndpoint("000000000000000000000000c0a81006", "device", "000000000000000000000000c0a81005"),
             BuildUbCtpEndpoint("000000000000000000000000c0a81008", "host", "000000000000000000000000c0a81007")})
            .c_str());
  }

  void TearDown() override {
    SetHostRoceVerbsForTest(nullptr);
    llm::AclRuntimeStub::Reset();
    hixl_test::ResetSysApiHooks();
    for (const auto &path : temp_files_) {
      (void)remove(path.c_str());
    }
    if (had_path_) {
      setenv("PATH", old_path_.c_str(), 1);
    } else {
      unsetenv("PATH");
    }
    if (!hccn_tool_dir_.empty()) {
      (void)rmdir(hccn_tool_dir_.c_str());
    }
    if (old_intra_roce_enable_.empty()) {
      unsetenv("HCCL_INTRA_ROCE_ENABLE");
    } else {
      setenv("HCCL_INTRA_ROCE_ENABLE", old_intra_roce_enable_.c_str(), 1);
    }
    ResetHcommVersionStub();
  }

  void Register(HixlEngine &engine, int32_t *ptr, MemHandle &handle) {
    MemDesc mem{};
    mem.addr = reinterpret_cast<uintptr_t>(ptr);
    mem.len = sizeof(int32_t);
    EXPECT_EQ(engine.RegisterMem(mem, MEM_DEVICE, handle), SUCCESS);
  }

  void VerifyNativeRoceConfig(uint32_t tc, uint32_t sl) {
    ASSERT_FALSE(native_verbs_->created_configs.empty());
    for (const auto &config : native_verbs_->created_configs) {
      EXPECT_EQ(config.tc, tc);
      EXPECT_EQ(config.sl, sl);
    }
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
    CreateAndInitEngine(engine1, opts1);
    CreateAndInitEngine(engine2, opts2);
    EXPECT_EQ(engine1.Connect("127.0.0.1:26300", kTimeOut), SUCCESS);
  }

  // same_instance: true → 同 net_instance_id → UB handler; false → 不同 → Direct handler
  void InitAutoConnectEngines(HixlEngine &engine1, HixlEngine &engine2, int32_t &src, int32_t &dst, MemHandle &handle1,
                              MemHandle &handle2, bool same_instance = false) {
    std::map<AscendString, AscendString> auto_connect_options = options1;
    auto_connect_options[hixl::OPTION_AUTO_CONNECT] = "1";
    CreateAndInitEngine(engine1, auto_connect_options);
    CreateAndInitEngine(engine2, same_instance ? options1_ub_pair : options2);

    Register(engine1, &src, handle1);
    Register(engine2, &dst, handle2);
  }

  // 不开启 auto_connect，连接生命周期由用例显式管理（Connect/Disconnect）
  void InitEnginesWithRegisteredMem(HixlEngine &engine1, HixlEngine &engine2, int32_t &src, int32_t &dst,
                                    MemHandle &handle1, MemHandle &handle2, bool same_instance = false) {
    CreateAndInitEngine(engine1, options1);
    CreateAndInitEngine(engine2, same_instance ? options1_ub_pair : options2);

    Register(engine1, &src, handle1);
    Register(engine2, &dst, handle2);
  }

  // 触发首次 READ 传输以完成 auto-connect 内部建链，并验证传输结果
  TransferOpDesc TriggerAutoConnectReadTransfer(HixlEngine &engine, int32_t &src, const int32_t &dst) {
    TransferOpDesc desc{reinterpret_cast<uintptr_t>(&src), reinterpret_cast<uintptr_t>(&dst), sizeof(int32_t)};
    EXPECT_EQ(engine.TransferSync("127.0.0.1:26300", READ, {desc}, kTimeOut), SUCCESS);
    EXPECT_EQ(src, 2);
    return desc;
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
    CreateAndInitEngine(engine, BuildOptions(local_comm_res));
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

  void CreateHccnTool() {
    char dir_template[] = "/tmp/hixl_engine_hccn_tool_XXXXXX";
    char *temp_dir = mkdtemp(dir_template);
    ASSERT_NE(temp_dir, nullptr);
    hccn_tool_dir_ = temp_dir;
    const std::string tool_path = hccn_tool_dir_ + "/hccn_tool";
    std::ofstream tool(tool_path);
    ASSERT_TRUE(tool.is_open());
    tool << "#!/bin/sh\necho \"link status: UP\"\n";
    tool.close();
    ASSERT_EQ(chmod(tool_path.c_str(), 0755), 0);
    temp_files_.emplace_back(tool_path);

    const char *old_path = std::getenv("PATH");
    had_path_ = old_path != nullptr;
    old_path_ = had_path_ ? old_path : "";
    const std::string test_path = had_path_ ? hccn_tool_dir_ + ":" + old_path_ : hccn_tool_dir_;
    setenv("PATH", test_path.c_str(), 1);
  }

  std::shared_ptr<MockEngineMmpaStub> mmpa_stub_;
  std::vector<std::string> temp_files_;
  std::string old_intra_roce_enable_;
  std::string hccn_tool_dir_;
  std::string old_path_;
  bool had_path_ = false;
};

TEST_F(HixlEngineTest, EngineFactoryUsesHixlEngineWhenProtocolDescConfigured) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"comm_resource_config.protocol_desc":"roce:device"})";
  HixlOptions parsed;
  auto engine = EngineFactory::CreateEngine("127.0.0.1:26000", options, parsed);
  ASSERT_NE(engine, nullptr);
  EXPECT_NE(dynamic_cast<HixlEngine *>(engine.get()), nullptr);
}

TEST_F(HixlEngineTest, InitializeSetsServerListenPortFromGlobalResourceConfig) {
  std::map<AscendString, AscendString> options = options1;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"comm_resource_config.listen_port":26301})";
  HixlEngine engine("127.0.0.1");
  CreateAndInitEngine(engine, options);

  auto *cs_server = static_cast<hixl::HixlCSServer *>(engine.server_.server_handle_);
  ASSERT_NE(cs_server, nullptr);
  ASSERT_TRUE(cs_server->global_config_.ListenPort().has_value());
  EXPECT_EQ(*cs_server->global_config_.ListenPort(), 26301U);

  ClientConfig config{};
  engine.BuildClientConfig(AscendString("127.0.0.1:26300"), config, kTimeOut, false);
  EXPECT_FALSE(config.global_config.comm_resource_config.qos.has_value());
  engine.Finalize();
}

TEST_F(HixlEngineTest, InitializeWithoutListenPortDoesNotSetServerListenPort) {
  HixlEngine engine("127.0.0.1");
  CreateAndInitEngine(engine, options1);

  auto *cs_server = static_cast<hixl::HixlCSServer *>(engine.server_.server_handle_);
  ASSERT_NE(cs_server, nullptr);
  EXPECT_FALSE(cs_server->global_config_.ListenPort().has_value());
  engine.Finalize();
}

TEST_F(HixlEngineTest, CsSerializationOmitsTlsCredentials) {
  GlobalResourceConfig config{};
  config.tls_config.configured = true;
  config.tls_config.cert_file = "/cert.pem";
  config.tls_config.ca_file = "/ca.pem";
  config.tls_config.ciphers = "HIGH";
  config.tls_config.key_pem = std::string("-----BEGIN PRIVATE") + " KEY-----\nMIIB\n-----END PRIVATE" + " KEY-----\n";
  const std::vector<std::string> serialized{SerializeGlobalResourceConfigForServer(config),
                                            SerializeGlobalResourceConfigForClient(config, {}, {})};
  for (const auto &text : serialized) {
    EXPECT_EQ(text.find("tls_config."), std::string::npos);
    GlobalConfig parsed;
    ASSERT_EQ(GlobalConfig::Parse(text.c_str(), parsed), SUCCESS);
    EXPECT_FALSE(parsed.TlsCfg().Enabled());
  }
}

TEST_F(HixlEngineTest, SharedTlsConfigurationReachesServerAndClient) {
  const auto dir = test::MakeTempDir();
  HIXL_MAKE_GUARD(cleanup, ([&]() {
                    test::RemovePath(dir + "/cert.pem");
                    test::RemovePath(dir + "/key.pem");
                    test::RemovePath(dir);
                  }));
  ASSERT_FALSE(dir.empty());
  ASSERT_TRUE(test::GenerateSelfSignedCert(dir));
  const auto tls = test::MakeTlsConfig(dir);
  const nlohmann::json config{{"tls_config.cert_file", tls.cert_file},
                              {"tls_config.ca_file", tls.ca_file},
                              {"tls_config.key_pem", tls.key_pem}};
  auto opts = options1;
  opts[OPTION_GLOBAL_RESOURCE_CONFIG] = AscendString(config.dump().c_str());
  HixlEngine engine_a("127.0.0.1");
  HixlEngine engine_b("127.0.0.1");
  HIXL_MAKE_GUARD(finalize, ([&]() {
                    engine_b.Finalize();
                    engine_a.Finalize();
                  }));
  const std::vector<HixlEngine *> engines{&engine_a, &engine_b};
  for (auto *engine : engines) {
    CreateAndInitEngine(*engine, opts);
    auto *server = static_cast<HixlCSServer *>(engine->server_.server_handle_);
    ASSERT_NE(server, nullptr);
    EXPECT_TRUE(CtrlMsgPluginTestAccess::IsTlsEnabled(server->listener_connection_));
    EXPECT_EQ(TlsManager::GetInstance().GetConfig().ca_file, tls.ca_file);
    EXPECT_TRUE(TlsManager::GetInstance().GetConfig().key_pem.empty());
    ClientConfig client{};
    engine->BuildClientConfig(AscendString("127.0.0.1:26300"), client, kTimeOut, false);
    EXPECT_FALSE(client.global_config.tls_config.Enabled());
  }
  const auto context = TlsManager::GetInstance().GetContext();
  engine_a.Finalize();
  engine_a.Finalize();
  EXPECT_EQ(TlsManager::GetInstance().GetContext(), context);
  auto *server_b = static_cast<HixlCSServer *>(engine_b.server_.server_handle_);
  EXPECT_EQ(TlsManager::GetInstance().GetConfig().ca_file, tls.ca_file);
  EXPECT_TRUE(CtrlMsgPluginTestAccess::IsTlsEnabled(server_b->listener_connection_));
  EXPECT_EQ(server_b->listener_connection_->Close(), SUCCESS);
  EXPECT_EQ(TlsManager::GetInstance().GetContext(), context);
  engine_b.Finalize();
  EXPECT_FALSE(TlsManager::GetInstance().IsInitialized());
}

TEST_F(HixlEngineTest, ConflictingTlsEngineInitializationPreservesActiveEngine) {
  const auto dir = test::MakeTempDir();
  HIXL_MAKE_GUARD(cleanup, ([&]() {
                    test::RemovePath(dir + "/cert.pem");
                    test::RemovePath(dir + "/key.pem");
                    test::RemovePath(dir);
                  }));
  ASSERT_FALSE(dir.empty());
  ASSERT_TRUE(test::GenerateSelfSignedCert(dir));
  const auto tls = test::MakeTlsConfig(dir);
  nlohmann::json config{{"tls_config.cert_file", tls.cert_file},
                        {"tls_config.ca_file", tls.ca_file},
                        {"tls_config.key_pem", tls.key_pem}};
  auto opts = options1;
  opts[OPTION_GLOBAL_RESOURCE_CONFIG] = AscendString(config.dump().c_str());
  HixlEngine active("127.0.0.1");
  HixlEngine conflict("127.0.0.1");
  HIXL_MAKE_GUARD(finalize, ([&]() {
                    conflict.Finalize();
                    active.Finalize();
                  }));
  CreateAndInitEngine(active, opts);
  ASSERT_TRUE(active.IsInitialized());
  config["tls_config.key_pem"] = tls.key_pem + "\n";
  opts[OPTION_GLOBAL_RESOURCE_CONFIG] = AscendString(config.dump().c_str());
  HixlOptions parsed;
  ASSERT_EQ(HixlOptions::Parse(opts, parsed), SUCCESS);
  EXPECT_NE(conflict.Initialize(parsed), SUCCESS);
  EXPECT_FALSE(conflict.IsInitialized());
  const auto context = TlsManager::GetInstance().GetContext();
  conflict.Finalize();
  EXPECT_EQ(TlsManager::GetInstance().GetContext(), context);
  ASSERT_EQ(HixlOptions::Parse(options1, parsed), SUCCESS);
  EXPECT_NE(conflict.Initialize(parsed), SUCCESS);
  auto *server = static_cast<HixlCSServer *>(active.server_.server_handle_);
  ASSERT_NE(server, nullptr);
  EXPECT_TRUE(CtrlMsgPluginTestAccess::IsTlsEnabled(server->listener_connection_));
  EXPECT_EQ(TlsManager::GetInstance().GetConfig().cert_file, tls.cert_file);
  active.Finalize();
  EXPECT_EQ(conflict.Initialize(parsed), SUCCESS);
}

TEST_F(HixlEngineTest, FailedInitializationAndRepeatedFinalizeDoNotRetainTlsOwner) {
  HixlEngine failed("127.0.0.1:65536");
  HIXL_MAKE_GUARD(finalize, ([&]() { failed.Finalize(); }));
  HixlOptions parsed;
  ASSERT_EQ(HixlOptions::Parse(options1, parsed), SUCCESS);
  EXPECT_FALSE(TlsManager::GetInstance().IsInitialized());
  EXPECT_NE(failed.Initialize(parsed), SUCCESS);
  EXPECT_FALSE(TlsManager::GetInstance().IsInitialized());
  failed.Finalize();
  EXPECT_FALSE(TlsManager::GetInstance().IsInitialized());
}

TEST_F(HixlEngineTest, InitializeRestoresCallerCurrentContext) {
  auto *const user_context = reinterpret_cast<aclrtContext>(static_cast<uintptr_t>(0xACEU));
  acl_stub_->current_context_ = user_context;

  HixlEngine engine("127.0.0.1");
  CreateAndInitEngine(engine, options1);

  aclrtContext current_context = nullptr;
  EXPECT_EQ(aclrtGetCurrentContext(&current_context), ACL_SUCCESS);
  EXPECT_EQ(current_context, user_context);
  engine.Finalize();
}

TEST_F(HixlEngineTest, InitializeSetsClientQosFromGlobalResourceConfig) {
  std::map<AscendString, AscendString> options = options1;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"comm_resource_config.qos": 7})";
  HixlEngine engine("127.0.0.1");
  CreateAndInitEngine(engine, options);

  ClientConfig config{};
  engine.BuildClientConfig(AscendString("127.0.0.1:26300"), config, kTimeOut, false);
  ASSERT_TRUE(config.global_config.comm_resource_config.qos.has_value());
  EXPECT_EQ(static_cast<uint32_t>(config.global_config.comm_resource_config.qos.value()), 7U);
  engine.Finalize();
}

TEST_F(HixlEngineTest, InitializeWithoutQosDoesNotSetClientQos) {
  HixlEngine engine("127.0.0.1");
  CreateAndInitEngine(engine, options1);

  ClientConfig config{};
  engine.BuildClientConfig(AscendString("127.0.0.1:26300"), config, kTimeOut, false);
  EXPECT_FALSE(config.global_config.comm_resource_config.qos.has_value());
  engine.Finalize();
}

TEST_F(HixlEngineTest, InitializeSetsMaxActiveChannelsFromGlobalResourceConfig) {
  std::map<AscendString, AscendString> options = options1;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"comm_resource_config.max_active_channels":8192})";
  HixlEngine engine("127.0.0.1");
  CreateAndInitEngine(engine, options);

  auto *cs_server = static_cast<hixl::HixlCSServer *>(engine.server_.server_handle_);
  ASSERT_NE(cs_server, nullptr);
  ASSERT_TRUE(cs_server->global_config_.MaxActiveChannels().has_value());
  EXPECT_EQ(*cs_server->global_config_.MaxActiveChannels(), 8192U);

  ClientConfig config{};
  engine.BuildClientConfig(AscendString("127.0.0.1:26300"), config, kTimeOut, false);
  ASSERT_TRUE(config.global_config.comm_resource_config.max_active_channels.has_value());
  EXPECT_EQ(config.global_config.comm_resource_config.max_active_channels.value(), 8192U);
  engine.Finalize();
}

TEST_F(HixlEngineTest, InitializeWithoutMaxActiveChannelsDoesNotSetConfig) {
  HixlEngine engine("127.0.0.1");
  CreateAndInitEngine(engine, options1);

  auto *cs_server = static_cast<hixl::HixlCSServer *>(engine.server_.server_handle_);
  ASSERT_NE(cs_server, nullptr);
  EXPECT_FALSE(cs_server->global_config_.MaxActiveChannels().has_value());

  ClientConfig config{};
  engine.BuildClientConfig(AscendString("127.0.0.1:26300"), config, kTimeOut, false);
  EXPECT_FALSE(config.global_config.comm_resource_config.max_active_channels.has_value());
  engine.Finalize();
}

TEST_F(HixlEngineTest, InitializeSetsChannelPoolMaxChannelFromGlobalResourceConfig) {
  std::map<AscendString, AscendString> options = options1;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"channel_pool.max_channel":100})";
  HixlEngine engine("127.0.0.1");
  CreateAndInitEngine(engine, options);

  ASSERT_TRUE(engine.client_manager_.max_channel_.has_value());
  EXPECT_EQ(engine.client_manager_.max_channel_.value(), 100U);
  engine.Finalize();
}

TEST_F(HixlEngineTest, InitializeWithoutChannelPoolDoesNotLimitChannelCount) {
  HixlEngine engine("127.0.0.1");
  CreateAndInitEngine(engine, options1);

  EXPECT_FALSE(engine.client_manager_.max_channel_.has_value());
  engine.Finalize();
}

TEST_F(HixlEngineTest, ChannelPoolEvictsIdleChannelAndRebuildsItOnTransfer) {
  std::map<AscendString, AscendString> client_options = options1;
  client_options[hixl::OPTION_AUTO_CONNECT] = "1";
  client_options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"channel_pool.max_channel":1})";
  HixlEngine client_engine("127.0.0.1:26200");
  CreateAndInitEngine(client_engine, client_options);
  HixlEngine server1("127.0.0.1:26300");
  CreateAndInitEngine(server1, options2);
  HixlEngine server2("127.0.0.1:26304");
  CreateAndInitEngine(server2, options2);

  int32_t local_num = 1;
  MemHandle local_handle = nullptr;
  Register(client_engine, &local_num, local_handle);
  int32_t remote_num = 2;
  MemHandle remote_handle = nullptr;
  Register(server1, &remote_num, remote_handle);

  const AscendString server1_engine("127.0.0.1:26300");
  EXPECT_EQ(client_engine.Connect(server1_engine, kTimeOut), SUCCESS);
  EXPECT_EQ(client_engine.Connect(AscendString("127.0.0.1:26304"), kTimeOut), SUCCESS);

  // max_channel=1，建立第二条链路前会淘汰第一条空闲链路
  ASSERT_EQ(client_engine.client_manager_.GetClient(server1_engine.GetString()), nullptr);
  EXPECT_NE(client_engine.client_manager_.GetClient("127.0.0.1:26304"), nullptr);

  // 被淘汰的链路在下次传输时重新建立
  TransferOpDesc desc{reinterpret_cast<uintptr_t>(&local_num), reinterpret_cast<uintptr_t>(&remote_num),
                      sizeof(int32_t)};
  EXPECT_EQ(client_engine.TransferSync(server1_engine, READ, {desc}, kTimeOut), SUCCESS);
  EXPECT_EQ(local_num, 2);
  EXPECT_NE(client_engine.client_manager_.GetClient(server1_engine.GetString()), nullptr);

  client_engine.Disconnect();
  client_engine.Finalize();
  server1.Finalize();
  server2.Finalize();
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

TEST_F(HixlEngineTest, EngineFactoryUsesHixlEngineWhenBufferPoolDisabled) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_BUFFER_POOL] = "0:0";
  HixlOptions parsed;
  auto engine = EngineFactory::CreateEngine("127.0.0.1:26000", options, parsed);
  ASSERT_NE(engine, nullptr);
  EXPECT_NE(dynamic_cast<HixlEngine *>(engine.get()), nullptr);
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
  CreateAndInitEngine(engine, options1);
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
  CreateAndInitEngine(engine, options1);
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
  EXPECT_NE(client->connection_, nullptr);
  NotifyDesc notify;
  std::string notify_name = "test_notify";
  notify.name = AscendString(notify_name.c_str());
  std::string notify_msg = "message";
  notify.notify_msg = AscendString(notify_msg.c_str());
  EXPECT_EQ(engine1.SendNotify("127.0.0.1:26300", notify, kTimeOut), SUCCESS);
  EXPECT_NE(client->connection_, nullptr);
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
  CreateAndInitEngine(engine1, options1);

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

  TransferOpDesc desc = TriggerAutoConnectReadTransfer(engine1, src, dst);
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
  HixlEngine engine("127.0.0.1");
  CreateAndInitEngine(engine, auto_connect_options);
  EXPECT_TRUE(engine.auto_connect_);
  engine.Finalize();
}

TEST_F(HixlEngineTest, TestConcurrentConnectReturnsSingleSuccess) {
  SetSocStub("Ascend910B1", 0, 12, 99, 88);
  HixlEngine engine1("127.0.0.1");
  CreateAndInitEngine(engine1, options1);
  HixlEngine engine2("127.0.0.1:26300");
  CreateAndInitEngine(engine2, options2);
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
  EXPECT_TRUE(native_verbs_->created_configs.empty());
  CleanupEngines(engine1, engine2);

  unsetenv("HCCL_RDMA_TC");
  unsetenv("HCCL_RDMA_SL");

  VerifyLogCapture(log_capture, {tc_log_pattern, sl_log_pattern, channel_desc_log_pattern});
}

TEST_F(HixlEngineTest, TestHcommPathPropagatesConfiguredRoceRetryAndKeepsUnsetDefault) {
  const std::string configured_log = "[channel] ROCE attributes set, tc=132, sl=4, retryCnt=4, retryInterval=15";
  auto log_capture = SetupLogCapture({configured_log});

  options1[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"roce_config.retry_cnt":4,"roce_config.timeout":15})";
  HixlEngine engine1("127.0.0.1");
  HixlEngine engine2("127.0.0.1:26300");
  InitializeAndConnectEngines(engine1, options1, engine2, options2);
  EXPECT_TRUE(native_verbs_->created_configs.empty());
  CleanupEngines(engine1, engine2);

  VerifyLogCapture(log_capture, {configured_log});
}

TEST_F(HixlEngineTest, TestHostRoceAcceptsGlobalMaxTransferCount) {
  options1[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"transfer_config.max_transfer_count_per_batch":32766})";
  HixlEngine engine1("127.0.0.1");
  HixlEngine engine2("127.0.0.1:26300");

  InitializeAndConnectEngines(engine1, options1, engine2, options2);
  CleanupEngines(engine1, engine2);
}

TEST_F(HixlEngineTest, TestLegacyHcommRejectsExplicitMaxTransferCountWhenCreatingClient) {
  SetHcommVersionNum(90199999);
  options1[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"transfer_config.max_transfer_count_per_batch":1024})";
  HixlEngine engine1("127.0.0.1");
  HixlEngine engine2("127.0.0.1:26300");

  CreateAndInitEngine(engine1, options1);
  CreateAndInitEngine(engine2, options2);
  EXPECT_EQ(engine1.Connect("127.0.0.1:26300", kTimeOut), PARAM_INVALID);
  EXPECT_TRUE(engine1.client_manager_.IsEmpty());
  CleanupEngines(engine1, engine2);
}

TEST_F(HixlEngineTest, TestNativeHostRoceExplicitMaxTransferCountDoesNotRequireHcomm) {
  SetHcommVersionNum(90199999);
  options1[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] =
      R"({"roce_config.hixl_backend":true,"transfer_config.max_transfer_count_per_batch":1024})";
  options2[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"roce_config.hixl_backend":true})";
  HixlEngine engine1("127.0.0.1");
  HixlEngine engine2("127.0.0.1:26300");

  CreateAndInitEngine(engine1, options1);
  CreateAndInitEngine(engine2, options2);
  EXPECT_EQ(engine1.Connect("127.0.0.1:26300", kTimeOut), SUCCESS);
  EXPECT_FALSE(engine1.client_manager_.IsEmpty());
  EXPECT_FALSE(native_verbs_->created_configs.empty());
  CleanupEngines(engine1, engine2);
}

TEST_F(HixlEngineTest, TestUrmaAcceptsGlobalMaxTransferCount) {
  options1[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"transfer_config.max_transfer_count_per_batch":32766})";
  HixlEngine engine1("127.0.0.1");
  HixlEngine engine2("127.0.0.1:26300");

  InitializeAndConnectEngines(engine1, options1, engine2, options1_ub_pair);
  CleanupEngines(engine1, engine2);
}

TEST_F(HixlEngineTest, TestHccsAcceptsFixedQueueTransferCountLimit) {
  const std::string local_comm_res = BuildLocalCommRes("superpod1_1", "1.3", {BuildDeviceHccsEndpoint("5")});
  auto hccs_options = BuildOptions(local_comm_res);
  hccs_options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"transfer_config.max_transfer_count_per_batch":1920})";
  HixlEngine engine1("127.0.0.1");
  HixlEngine engine2("127.0.0.1:26300");

  InitializeAndConnectEngines(engine1, hccs_options, engine2, BuildOptions(local_comm_res));
  CleanupEngines(engine1, engine2);
}

TEST_F(HixlEngineTest, TestHccsRejectsTransferCountAboveFixedQueueLimit) {
  const std::string local_comm_res = BuildLocalCommRes("superpod1_1", "1.3", {BuildDeviceHccsEndpoint("5")});
  auto hccs_options = BuildOptions(local_comm_res);
  hccs_options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"transfer_config.max_transfer_count_per_batch":1921})";
  HixlEngine engine1("127.0.0.1");
  HixlEngine engine2("127.0.0.1:26300");

  CreateAndInitEngine(engine1, hccs_options);
  CreateAndInitEngine(engine2, BuildOptions(local_comm_res));
  EXPECT_EQ(engine1.Connect("127.0.0.1:26300", kTimeOut), PARAM_INVALID);
  EXPECT_TRUE(engine1.client_manager_.IsEmpty());
  CleanupEngines(engine1, engine2);
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
  EXPECT_TRUE(native_verbs_->created_configs.empty());
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
  EXPECT_TRUE(native_verbs_->created_configs.empty());
  CleanupEngines(engine1, engine2);

  VerifyLogCapture(log_capture, {channel_desc_log_pattern});
}

TEST_F(HixlEngineTest, TestNativeHostRoceParseTcAndSlWithValidValue) {
  options1[hixl::OPTION_RDMA_TRAFFIC_CLASS] = "128";
  options1[adxl::OPTION_RDMA_SERVICE_LEVEL] = "5";
  options1[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"roce_config.hixl_backend":true})";
  options2[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"roce_config.hixl_backend":true})";

  HixlEngine engine1("127.0.0.1");
  HixlEngine engine2("127.0.0.1:26300");
  InitializeAndConnectEngines(engine1, options1, engine2, options2);
  VerifyNativeRoceConfig(128, 5);
  CleanupEngines(engine1, engine2);
}

TEST_F(HixlEngineTest, TestNativeHostRoceParseTcAndSlWithDefault) {
  options1[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"roce_config.hixl_backend":true})";
  options2[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"roce_config.hixl_backend":true})";

  HixlEngine engine1("127.0.0.1");
  HixlEngine engine2("127.0.0.1:26300");
  InitializeAndConnectEngines(engine1, options1, engine2, options2);
  // With no explicit TC/SL, the engine supplies QoS 0; the backend maps it to SL 0 and DSCP 33.
  VerifyNativeRoceConfig(132, kQosDefault);
  CleanupEngines(engine1, engine2);
}

TEST_F(HixlEngineTest, TestNativeHostRoceMultiChannelPostsConcurrently) {
  options1[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({
    "roce_config.hixl_backend":true,
    "comm_resource_config.multi_channel.num_workers":2,
    "comm_resource_config.multi_channel.split_batch_size":1})";
  options2[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"roce_config.hixl_backend":true})";
  HixlEngine engine1("127.0.0.1");
  HixlEngine engine2("127.0.0.1:26300");
  uint32_t local[]{5U, 7U};
  uint32_t remote[2]{};
  MemHandle local_handle = nullptr;
  MemHandle remote_handle = nullptr;
  CreateAndInitEngine(engine1, options1);
  CreateAndInitEngine(engine2, options2);
  ASSERT_EQ(engine1.RegisterMem({reinterpret_cast<uintptr_t>(local), sizeof(local)}, MEM_HOST, local_handle), SUCCESS);
  ASSERT_EQ(engine2.RegisterMem({reinterpret_cast<uintptr_t>(remote), sizeof(remote)}, MEM_HOST, remote_handle),
            SUCCESS);
  ASSERT_EQ(engine1.Connect("127.0.0.1:26300", kTimeOut), SUCCESS);
  ASSERT_EQ(native_verbs_->created.load(), 4U);
  const std::vector<TransferOpDesc> ops{
      {reinterpret_cast<uintptr_t>(local), reinterpret_cast<uintptr_t>(remote), sizeof(local[0])},
      {reinterpret_cast<uintptr_t>(local + 1), reinterpret_cast<uintptr_t>(remote + 1), sizeof(local[1])}};
  auto run_with_parallel_posts = [&](auto transfer) {
    std::mutex mutex;
    std::condition_variable cv;
    size_t arrived = 0U;
    bool both_posting = true;
    native_verbs_->before_write_post = [&] {
      std::unique_lock lock(mutex);
      ++arrived;
      cv.notify_all();
      if (!cv.wait_for(lock, std::chrono::seconds(2), [&] { return arrived >= 2U; })) {
        both_posting = false;
      }
    };
    const auto result = transfer();
    native_verbs_->before_write_post = nullptr;
    EXPECT_EQ(arrived, 2U);
    EXPECT_TRUE(both_posting);
    return result;
  };
  ASSERT_EQ(run_with_parallel_posts([&] { return engine1.TransferSync("127.0.0.1:26300", WRITE, ops, kTimeOut); }),
            SUCCESS);
  TransferReq req = nullptr;
  ASSERT_EQ(run_with_parallel_posts([&] { return engine1.TransferAsync("127.0.0.1:26300", WRITE, ops, {}, req); }),
            SUCCESS);
  TransferStatus status = TransferStatus::WAITING;
  for (size_t i = 0U; i < 10U && status == TransferStatus::WAITING; ++i) {
    ASSERT_EQ(engine1.GetTransferStatus(req, status), SUCCESS);
  }
  EXPECT_EQ(status, TransferStatus::COMPLETED);
  EXPECT_EQ(remote[0], local[0]);
  EXPECT_EQ(remote[1], local[1]);
  const auto posts = native_verbs_->PostsSnapshot();
  EXPECT_EQ(std::count_if(posts.begin(), posts.end(), [](const auto &post) { return !post.read; }), 4);
  CleanupEngines(engine1, engine2);
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
  Status Connect(uint32_t) override {
    return SUCCESS;
  }
  Status RegisterMem(const MemHandleInfo &mem_info) override {
    registered_mems.push_back(mem_info);
    return register_ret;
  }
  Status DeregisterMem(MemHandle mem_handle) override {
    deregistered_handles.push_back(mem_handle);
    return deregister_ret;
  }
  Status TransferAsync(const std::vector<TransferOpDesc> &, TransferOp, TransferReq &) override {
    return SUCCESS;
  }
  Status TransferSync(const std::vector<TransferOpDesc> &, TransferOp, uint32_t) override {
    return SUCCESS;
  }
  Status GetTransferStatus(const TransferReq &req, TransferStatus &status) override {
    auto it = status_by_req.find(req);
    if (it == status_by_req.end()) {
      status = default_status;
      return default_ret;
    }
    status = it->second;
    return SUCCESS;
  }
  Status Finalize() override {
    ++finalize_calls;
    return finalize_ret;
  }
  void Dump(const char *, DumpLogLevel = DumpLogLevel::EVENT) const override {}

  std::map<TransferReq, TransferStatus> status_by_req;
  TransferStatus default_status = TransferStatus::WAITING;
  Status default_ret = SUCCESS;
  Status deregister_ret = SUCCESS;
  Status finalize_ret = SUCCESS;
  uint32_t finalize_calls = 0U;
  Status register_ret = SUCCESS;
  std::vector<MemHandle> deregistered_handles;
  std::vector<MemHandleInfo> registered_mems;
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

static ClientPtr CreateMockClient(std::unique_ptr<MockClientHandler> handler) {
  ClientConfig config{};
  config.remote_engine = "127.0.0.1:26300";
  auto client = std::make_shared<HixlClient>("127.0.0.1", 26300, config);
  client->client_handler_ = std::move(handler);
  client->is_connected_ = true;
  return client;
}

static bool ClientHasLocalMem(const ClientPtr &client, MemHandle mem_handle) {
  if (client == nullptr || client->client_handler_ == nullptr) {
    return false;
  }
  auto *direct = dynamic_cast<DirectClientHandler *>(client->client_handler_.get());
  if (direct != nullptr) {
    return direct->handle_to_mem_handle_.count(mem_handle) != 0U;
  }
  auto *ub = dynamic_cast<UbClientHandler *>(client->client_handler_.get());
  if (ub != nullptr) {
    return ub->handle_to_mem_record_.count(mem_handle) != 0U;
  }
  return false;
}

static void RegisterMockTransferReq(HixlEngine &engine, const ClientPtr &client, TransferReq req,
                                    const void *user_data) {
  client->req_map_[req] = TransferInfo{nullptr, READ, AscendString()};
  engine.client_manager_.RegisterTransferReq(req, client, user_data);
}

struct DisconnectedBatchStatusContext {
  ClientPtr client;
  TransferReq req1;
  TransferReq req2;
  const void *user_data1;
  const void *user_data2;
};

static void SetupDisconnectedEngineWithFailedTransferReqs(
    HixlEngine &engine, DisconnectedBatchStatusContext &ctx,
    const void *user_data1 = reinterpret_cast<const void *>(0x3000),
    const void *user_data2 = reinterpret_cast<const void *>(0x4000)) {
  engine.is_initialized_ = true;
  engine.auto_connect_ = true;
  ASSERT_EQ(engine.client_manager_.Initialize(true), SUCCESS);
  auto handler = std::make_unique<MockClientHandler>();
  handler->default_ret = FAILED;
  ctx.client = CreateMockClient(std::move(handler));
  ctx.req1 = reinterpret_cast<TransferReq>(0x1000);
  ctx.req2 = reinterpret_cast<TransferReq>(0x2000);
  ctx.user_data1 = user_data1;
  ctx.user_data2 = user_data2;
  RegisterMockTransferReq(engine, ctx.client, ctx.req1, ctx.user_data1);
  RegisterMockTransferReq(engine, ctx.client, ctx.req2, ctx.user_data2);
}

static bool AttachClosedCtrlSocket(const ClientPtr &client) {
  int32_t fds[2] = {-1, -1};
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
    return false;
  }
  client->connection_ = test::Connection(fds[0]);
  close(fds[1]);
  return true;
}

class ClientManagerTest : public hixl::test::PlainTlsTest {};

TEST_F(ClientManagerTest, HeartbeatInitializeStartsAndStopsThread) {
  ClientManager manager;
  EXPECT_EQ(manager.Initialize(true), SUCCESS);
  EXPECT_TRUE(manager.heartbeat_sender_.joinable());
  EXPECT_EQ(manager.Finalize(), SUCCESS);
}

TEST_F(ClientManagerTest, HeartbeatInitializeWithoutAutoConnectDoesNotStartThread) {
  ClientManager manager;
  EXPECT_EQ(manager.Initialize(false), SUCCESS);
  EXPECT_FALSE(manager.heartbeat_sender_.joinable());
  EXPECT_EQ(manager.Finalize(), SUCCESS);
}

TEST_F(ClientManagerTest, GetOrCreateClientReturnsExistingClient) {
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

TEST_F(ClientManagerTest, FinalizePropagatesFailureAfterFinalizingAllClients) {
  ClientManager manager;
  ASSERT_EQ(manager.Initialize(false), SUCCESS);

  auto failed_handler = std::make_unique<MockClientHandler>();
  failed_handler->finalize_ret = FAILED;
  auto *failed_handler_ptr = failed_handler.get();
  auto failed_client = CreateMockClient(std::move(failed_handler));

  auto successful_handler = std::make_unique<MockClientHandler>();
  auto *successful_handler_ptr = successful_handler.get();
  auto successful_client = CreateMockClient(std::move(successful_handler));

  manager.clients_["127.0.0.1:26300"] = failed_client;
  manager.clients_["127.0.0.1:26301"] = successful_client;

  EXPECT_EQ(manager.Finalize(), FAILED);
  EXPECT_EQ(failed_handler_ptr->finalize_calls, 1U);
  EXPECT_EQ(successful_handler_ptr->finalize_calls, 1U);
  EXPECT_TRUE(manager.IsEmpty());
}

TEST(HixlEngineLifecycleTest, CheckInitializedRejectsConnectAndRegisterMem) {
  HixlEngine engine("127.0.0.1:26200");
  engine.is_initialized_ = false;

  MemHandle handle = nullptr;
  MemDesc mem{};
  mem.addr = 0x1000U;
  mem.len = 4096U;
  EXPECT_EQ(engine.RegisterMem(mem, MEM_HOST, handle), FAILED);
  EXPECT_EQ(engine.Connect(AscendString("127.0.0.1:26300"), kTimeOut), FAILED);
}

TEST(HixlEngineLifecycleTest, AutoConnectRejectsWhenNotInitialized) {
  HixlEngine engine("127.0.0.1:26200");
  engine.is_initialized_ = false;
  engine.auto_connect_ = true;
  ClientPtr client_ptr = nullptr;
  bool transfer_use = false;
  EXPECT_EQ(engine.AutoConnect(AscendString("127.0.0.1:26300"), kTimeOut, client_ptr, transfer_use), FAILED);
  EXPECT_EQ(client_ptr, nullptr);
}

TEST_F(ClientManagerTest, HeartbeatDestroysUnhealthyClient) {
  ClientManager manager;
  EXPECT_EQ(manager.Initialize(false), SUCCESS);
  auto client = CreateMockClient();
  ASSERT_TRUE(AttachClosedCtrlSocket(client));
  manager.clients_["127.0.0.1:26300"] = client;
  manager.SendHeartbeat();
  EXPECT_EQ(manager.GetClient("127.0.0.1:26300"), nullptr);
  EXPECT_EQ(manager.Finalize(), SUCCESS);
}

TEST_F(ClientManagerTest, HeartbeatDestroysClientWithInvalidSocket) {
  ClientManager manager;
  EXPECT_EQ(manager.Initialize(false), SUCCESS);
  auto client = CreateMockClient();
  manager.clients_["127.0.0.1:26300"] = client;
  manager.SendHeartbeat();
  EXPECT_EQ(manager.GetClient("127.0.0.1:26300"), nullptr);
  EXPECT_EQ(manager.Finalize(), SUCCESS);
}

TEST_F(ClientManagerTest, ChannelPoolEvictsLeastRecentlyUsedClientWhenFull) {
  ClientManager manager;
  ASSERT_EQ(manager.Initialize(false, 2U), SUCCESS);
  manager.clients_["127.0.0.1:26301"] = CreateMockClient();
  manager.clients_["127.0.0.1:26302"] = CreateMockClient();
  // 先访问 26301 再访问 26302，26301 成为最久未使用的链路
  ASSERT_NE(manager.GetClient("127.0.0.1:26301"), nullptr);
  ASSERT_NE(manager.GetClient("127.0.0.1:26302"), nullptr);

  {
    std::unique_lock<std::mutex> client_lock;
    bool reserved = false;
    ClientPtr client = nullptr;
    EXPECT_EQ(manager.ReserveChannelSlot("127.0.0.1:26303", client_lock, client, reserved), SUCCESS);
    EXPECT_TRUE(reserved);
    EXPECT_EQ(manager.creating_count_, 1U);
    EXPECT_EQ(manager.GetClient("127.0.0.1:26301"), nullptr);
    EXPECT_NE(manager.GetClient("127.0.0.1:26302"), nullptr);
    manager.ReleaseChannelSlot();
  }
  EXPECT_EQ(manager.creating_count_, 0U);
  EXPECT_EQ(manager.Finalize(), SUCCESS);
}

TEST_F(ClientManagerTest, ChannelPoolSkipsTransferringClientWhenEvicting) {
  ClientManager manager;
  ASSERT_EQ(manager.Initialize(false, 2U), SUCCESS);
  auto transferring_client = CreateMockClient();
  transferring_client->req_map_[reinterpret_cast<TransferReq>(0x1000)] = TransferInfo{nullptr, READ, AscendString()};
  manager.clients_["127.0.0.1:26301"] = transferring_client;
  manager.clients_["127.0.0.1:26302"] = CreateMockClient();
  // 26301 最久未使用，但它正在传输，因此只能淘汰 26302
  ASSERT_NE(manager.GetClient("127.0.0.1:26301"), nullptr);
  ASSERT_NE(manager.GetClient("127.0.0.1:26302"), nullptr);

  {
    std::unique_lock<std::mutex> client_lock;
    bool reserved = false;
    ClientPtr client = nullptr;
    EXPECT_EQ(manager.ReserveChannelSlot("127.0.0.1:26303", client_lock, client, reserved), SUCCESS);
    EXPECT_TRUE(reserved);
    EXPECT_NE(manager.GetClient("127.0.0.1:26301"), nullptr);
    EXPECT_EQ(manager.GetClient("127.0.0.1:26302"), nullptr);
    manager.ReleaseChannelSlot();
  }
  EXPECT_EQ(manager.Finalize(), SUCCESS);
}

TEST_F(ClientManagerTest, ChannelPoolSkipsClientClaimedForTransfer) {
  ClientManager manager;
  ASSERT_EQ(manager.Initialize(false, 1U), SUCCESS);
  auto client = CreateMockClient();
  manager.clients_["127.0.0.1:26301"] = client;
  ASSERT_NE(manager.GetClient("127.0.0.1:26301"), nullptr);
  EXPECT_FALSE(client->IsBusy());

  bool transfer_use = false;
  ASSERT_NE(manager.GetClientForUse("127.0.0.1:26301", transfer_use), nullptr);
  EXPECT_TRUE(transfer_use);
  EXPECT_TRUE(client->IsBusy());

  std::unique_lock<std::mutex> client_lock;
  bool reserved = false;
  ClientPtr created = nullptr;
  EXPECT_EQ(manager.ReserveChannelSlot("127.0.0.1:26302", client_lock, created, reserved, &transfer_use),
            RESOURCE_EXHAUSTED);
  EXPECT_FALSE(reserved);
  EXPECT_NE(manager.GetClient("127.0.0.1:26301"), nullptr);

  client->ReleaseUse();
  EXPECT_FALSE(client->IsBusy());
  EXPECT_EQ(manager.ReserveChannelSlot("127.0.0.1:26302", client_lock, created, reserved), SUCCESS);
  EXPECT_TRUE(reserved);
  EXPECT_EQ(manager.GetClient("127.0.0.1:26301"), nullptr);
  manager.ReleaseChannelSlot();
  EXPECT_EQ(manager.Finalize(), SUCCESS);
}

TEST_F(ClientManagerTest, ChannelPoolDisabledDoesNotClaimTransferUse) {
  ClientManager manager;
  ASSERT_EQ(manager.Initialize(false), SUCCESS);
  auto client = CreateMockClient();
  manager.clients_["127.0.0.1:26301"] = client;
  bool transfer_use = false;
  manager.ClaimTransferUseLocked(client, transfer_use);
  EXPECT_FALSE(transfer_use);
  EXPECT_FALSE(client->IsBusy());
  EXPECT_EQ(manager.Finalize(), SUCCESS);
}

TEST_F(ClientManagerTest, ChannelPoolRejectsNewClientWhenAllClientsAreTransferring) {
  ClientManager manager;
  ASSERT_EQ(manager.Initialize(false, 1U), SUCCESS);
  auto transferring_client = CreateMockClient();
  transferring_client->req_map_[reinterpret_cast<TransferReq>(0x1000)] = TransferInfo{nullptr, READ, AscendString()};
  manager.clients_["127.0.0.1:26301"] = transferring_client;

  std::unique_lock<std::mutex> client_lock;
  bool reserved = false;
  ClientPtr client = nullptr;
  EXPECT_EQ(manager.ReserveChannelSlot("127.0.0.1:26302", client_lock, client, reserved), RESOURCE_EXHAUSTED);
  EXPECT_FALSE(reserved);
  EXPECT_EQ(manager.creating_count_, 0U);
  EXPECT_NE(manager.GetClient("127.0.0.1:26301"), nullptr);
  EXPECT_EQ(manager.Finalize(), SUCCESS);
}

TEST_F(ClientManagerTest, ChannelPoolKeepsAllClientsForAlreadyConnectedEngine) {
  ClientManager manager;
  ASSERT_EQ(manager.Initialize(false, 1U), SUCCESS);
  auto client = CreateMockClient();
  manager.clients_["127.0.0.1:26301"] = client;

  {
    std::unique_lock<std::mutex> client_lock;
    bool reserved = false;
    ClientPtr returned = nullptr;
    EXPECT_EQ(manager.ReserveChannelSlot("127.0.0.1:26301", client_lock, returned, reserved), ALREADY_CONNECTED);
    EXPECT_FALSE(reserved);
    EXPECT_EQ(returned, client);
    EXPECT_EQ(manager.creating_count_, 0U);
  }
  EXPECT_EQ(manager.Finalize(), SUCCESS);
}

TEST_F(ClientManagerTest, ChannelPoolDisabledNeverEvictsClient) {
  ClientManager manager;
  ASSERT_EQ(manager.Initialize(false), SUCCESS);
  manager.clients_["127.0.0.1:26301"] = CreateMockClient();
  manager.clients_["127.0.0.1:26302"] = CreateMockClient();

  {
    std::unique_lock<std::mutex> client_lock;
    bool reserved = false;
    ClientPtr client = nullptr;
    EXPECT_EQ(manager.ReserveChannelSlot("127.0.0.1:26303", client_lock, client, reserved), SUCCESS);
    EXPECT_FALSE(reserved);
    EXPECT_EQ(manager.creating_count_, 0U);
    EXPECT_NE(manager.GetClient("127.0.0.1:26301"), nullptr);
    EXPECT_NE(manager.GetClient("127.0.0.1:26302"), nullptr);
  }
  EXPECT_EQ(manager.Finalize(), SUCCESS);
}

TEST_F(ClientManagerTest, ChannelPoolReserveAfterEvictFinalizeFailureStillRespectsMax) {
  ClientManager manager;
  ASSERT_EQ(manager.Initialize(false, 1U), SUCCESS);
  auto failed_handler = std::make_unique<MockClientHandler>();
  failed_handler->finalize_ret = FAILED;
  manager.clients_["127.0.0.1:26301"] = CreateMockClient(std::move(failed_handler));

  std::unique_lock<std::mutex> first_lock;
  bool first_reserved = false;
  ClientPtr first_client = nullptr;
  EXPECT_EQ(manager.ReserveChannelSlot("127.0.0.1:26302", first_lock, first_client, first_reserved), SUCCESS);
  EXPECT_TRUE(first_reserved);
  EXPECT_EQ(manager.GetClient("127.0.0.1:26301"), nullptr);
  EXPECT_EQ(manager.creating_count_, 1U);

  std::unique_lock<std::mutex> second_lock;
  bool second_reserved = false;
  ClientPtr second_client = nullptr;
  EXPECT_EQ(manager.ReserveChannelSlot("127.0.0.1:26303", second_lock, second_client, second_reserved),
            RESOURCE_EXHAUSTED);
  EXPECT_FALSE(second_reserved);
  EXPECT_EQ(manager.creating_count_, 1U);

  manager.ReleaseChannelSlot();
  first_lock.unlock();
  EXPECT_EQ(manager.creating_count_, 0U);
  EXPECT_EQ(manager.Finalize(), SUCCESS);
}

TEST_F(ClientManagerTest, ChannelPoolConcurrentReserveDoesNotExceedMaxChannel) {
  constexpr uint32_t kMaxChannel = 2U;
  constexpr int32_t kThreadCount = 8;
  ClientManager manager;
  ASSERT_EQ(manager.Initialize(false, kMaxChannel), SUCCESS);
  manager.clients_["127.0.0.1:26301"] = CreateMockClient();
  manager.clients_["127.0.0.1:26302"] = CreateMockClient();

  struct ReserveResult {
    Status status = FAILED;
    bool reserved = false;
    std::unique_lock<std::mutex> client_lock;
  };
  std::vector<ReserveResult> results(static_cast<size_t>(kThreadCount));
  std::vector<std::thread> threads;
  threads.reserve(static_cast<size_t>(kThreadCount));
  for (int32_t i = 0; i < kThreadCount; ++i) {
    threads.emplace_back([&manager, &results, i]() {
      ClientPtr client = nullptr;
      const std::string remote_engine = "127.0.0.1:" + std::to_string(26400 + i);
      results[static_cast<size_t>(i)].status = manager.ReserveChannelSlot(
          remote_engine, results[static_cast<size_t>(i)].client_lock, client, results[static_cast<size_t>(i)].reserved);
    });
  }
  for (auto &thread : threads) {
    thread.join();
  }

  int32_t reserved_count = 0;
  int32_t exhausted_count = 0;
  for (const auto &result : results) {
    if (result.reserved) {
      ++reserved_count;
      EXPECT_EQ(result.status, SUCCESS);
    } else {
      ++exhausted_count;
      EXPECT_EQ(result.status, RESOURCE_EXHAUSTED);
    }
  }
  {
    std::lock_guard<std::mutex> lock(manager.mutex_);
    EXPECT_LE(manager.clients_.size() + manager.creating_count_, static_cast<size_t>(kMaxChannel));
    EXPECT_EQ(manager.creating_count_, static_cast<size_t>(reserved_count));
  }
  EXPECT_EQ(reserved_count, static_cast<int32_t>(kMaxChannel));
  EXPECT_EQ(exhausted_count, kThreadCount - static_cast<int32_t>(kMaxChannel));

  for (auto &result : results) {
    if (result.reserved) {
      manager.ReleaseChannelSlot();
    }
    if (result.client_lock.owns_lock()) {
      result.client_lock.unlock();
    }
  }
  EXPECT_EQ(manager.creating_count_, 0U);
  EXPECT_EQ(manager.Finalize(), SUCCESS);
}

TEST(HixlClientBusyTest, UseCountKeepsClientBusyUntilReleased) {
  auto client = CreateMockClient();
  EXPECT_FALSE(client->IsBusy());
  client->AcquireUse();
  EXPECT_TRUE(client->IsBusy());
  client->AcquireUse();
  client->ReleaseUse();
  EXPECT_TRUE(client->IsBusy());
  client->ReleaseUse();
  EXPECT_FALSE(client->IsBusy());
}

TEST(HixlClientBusyTest, IsBusyWhenAnotherThreadHoldsTheClient) {
  auto client = CreateMockClient();
  EXPECT_FALSE(client->IsBusy());

  std::promise<void> locked;
  std::promise<void> release;
  auto release_future = release.get_future();
  std::thread holder([&client, &locked, &release_future]() {
    std::lock_guard<std::mutex> lock(client->mutex_);
    locked.set_value();
    release_future.wait();
  });
  locked.get_future().wait();
  EXPECT_TRUE(client->IsBusy());

  release.set_value();
  holder.join();
  EXPECT_FALSE(client->IsBusy());
}

TEST_F(ClientManagerTest, TransferReqIndexKeepsSubmitOrderAndFindsClient) {
  ClientManager manager;
  EXPECT_EQ(manager.Initialize(false), SUCCESS);
  auto client = CreateMockClient();

  TransferReq req1 = reinterpret_cast<TransferReq>(0x1000);
  TransferReq req2 = reinterpret_cast<TransferReq>(0x2000);
  const void *user_data1 = reinterpret_cast<const void *>(0x3000);
  const void *user_data2 = reinterpret_cast<const void *>(0x4000);
  manager.RegisterTransferReq(req1, client, user_data1);
  manager.RegisterTransferReq(req2, client, user_data2);

  EXPECT_EQ(manager.GetClientByReq(req1), client);
  auto reqs = manager.GetOrderedReqs(0);
  ASSERT_EQ(reqs.size(), 2U);
  EXPECT_EQ(reqs[0].req, req1);
  EXPECT_EQ(reqs[0].client, client);
  EXPECT_EQ(reqs[0].user_data, user_data1);
  EXPECT_EQ(reqs[1].req, req2);
  EXPECT_EQ(reqs[1].client, client);
  EXPECT_EQ(reqs[1].user_data, user_data2);

  manager.EraseTransferReq(req1);
  EXPECT_EQ(manager.GetClientByReq(req1), nullptr);
  reqs = manager.GetOrderedReqs(0);
  ASSERT_EQ(reqs.size(), 1U);
  EXPECT_EQ(reqs[0].req, req2);
  EXPECT_EQ(reqs[0].client, client);
  EXPECT_EQ(reqs[0].user_data, user_data2);
  EXPECT_EQ(manager.Finalize(), SUCCESS);
}

TEST_F(ClientManagerTest, RegisterDuplicateTransferReqRemovesOldOrderNode) {
  ClientManager manager;
  EXPECT_EQ(manager.Initialize(false), SUCCESS);
  auto client = CreateMockClient();

  TransferReq req1 = reinterpret_cast<TransferReq>(0x1000);
  TransferReq req2 = reinterpret_cast<TransferReq>(0x2000);
  const void *user_data1 = reinterpret_cast<const void *>(0x3000);
  const void *user_data2 = reinterpret_cast<const void *>(0x4000);
  const void *updated_user_data1 = reinterpret_cast<const void *>(0x5000);
  manager.RegisterTransferReq(req1, client, user_data1);
  manager.RegisterTransferReq(req2, client, user_data2);
  manager.RegisterTransferReq(req1, client, updated_user_data1);

  auto reqs = manager.GetOrderedReqs(0);
  ASSERT_EQ(reqs.size(), 2U);
  EXPECT_EQ(reqs[0].req, req2);
  EXPECT_EQ(reqs[0].user_data, user_data2);
  EXPECT_EQ(reqs[1].req, req1);
  EXPECT_EQ(reqs[1].user_data, updated_user_data1);
  EXPECT_EQ(manager.Finalize(), SUCCESS);
}

TEST(HixlEngineBatchStatusTest, BusyClientReturnsWaitingAndBatchQueriesOtherClients) {
  HixlEngine engine("127.0.0.1:26200");
  ASSERT_EQ(engine.client_manager_.Initialize(false), SUCCESS);
  auto busy_handler = std::make_unique<MockClientHandler>();
  auto other_handler = std::make_unique<MockClientHandler>();
  busy_handler->default_status = TransferStatus::COMPLETED;
  other_handler->default_status = TransferStatus::COMPLETED;
  auto busy_client = CreateMockClient(std::move(busy_handler));
  auto other_client = CreateMockClient(std::move(other_handler));
  const auto busy_req = reinterpret_cast<TransferReq>(0x1000);
  const auto other_req = reinterpret_cast<TransferReq>(0x2000);
  RegisterMockTransferReq(engine, busy_client, busy_req, nullptr);
  RegisterMockTransferReq(engine, other_client, other_req, nullptr);

  TransferStatus status = TransferStatus::FAILED;
  std::vector<TransferResult> results;
  std::unique_lock<std::mutex> lock(busy_client->mutex_);
  auto query = std::async(std::launch::async, [&] {
    const auto single_ret = engine.GetTransferStatus(busy_req, status);
    GetTransferStatusArgs args{};
    args.skip_waiting = true;
    return std::make_pair(single_ret, engine.GetTransferStatus(args, results));
  });
  const auto returned = query.wait_for(std::chrono::seconds(1));
  lock.unlock();
  EXPECT_EQ(returned, std::future_status::ready);
  const auto [single_ret, batch_ret] = query.get();
  EXPECT_EQ(single_ret, SUCCESS);
  EXPECT_EQ(batch_ret, SUCCESS);
  EXPECT_EQ(status, TransferStatus::WAITING);
  ASSERT_EQ(results.size(), 1U);
  EXPECT_EQ(results[0].req, other_req);
  EXPECT_EQ(results[0].status, TransferStatus::COMPLETED);
  EXPECT_EQ(engine.GetTransferStatus(busy_req, status), SUCCESS);
  EXPECT_EQ(status, TransferStatus::COMPLETED);
  EXPECT_EQ(engine.client_manager_.Finalize(), SUCCESS);
}

TEST_F(ClientManagerTest, DestroyClientClearsTransferReqIndex) {
  ClientManager manager;
  EXPECT_EQ(manager.Initialize(false), SUCCESS);
  auto client = CreateMockClient();
  manager.clients_["127.0.0.1:26300"] = client;

  TransferReq req = reinterpret_cast<TransferReq>(0x1000);
  manager.RegisterTransferReq(req, client, nullptr);
  EXPECT_EQ(manager.DestroyClient("127.0.0.1:26300"), SUCCESS);
  EXPECT_EQ(manager.GetClientByReq(req), nullptr);
  EXPECT_TRUE(manager.GetOrderedReqs(0).empty());
  EXPECT_EQ(manager.Finalize(), SUCCESS);
}

TEST(HixlEngineBatchStatusTest, ReturnsResultsInSubmitOrderWithUserData) {
  HixlEngine engine("127.0.0.1:26200");
  ASSERT_EQ(engine.client_manager_.Initialize(false), SUCCESS);
  auto handler = std::make_unique<MockClientHandler>();
  TransferReq req1 = reinterpret_cast<TransferReq>(0x1000);
  TransferReq req2 = reinterpret_cast<TransferReq>(0x2000);
  handler->status_by_req[req1] = TransferStatus::WAITING;
  handler->status_by_req[req2] = TransferStatus::COMPLETED;
  auto client = CreateMockClient(std::move(handler));

  const void *user_data1 = reinterpret_cast<const void *>(0x3000);
  const void *user_data2 = reinterpret_cast<const void *>(0x4000);
  RegisterMockTransferReq(engine, client, req1, user_data1);
  RegisterMockTransferReq(engine, client, req2, user_data2);

  GetTransferStatusArgs args{};
  std::vector<TransferResult> results;
  EXPECT_EQ(engine.GetTransferStatus(args, results), SUCCESS);

  ASSERT_EQ(results.size(), 2U);
  EXPECT_EQ(results[0].req, req1);
  EXPECT_EQ(results[0].user_data, user_data1);
  EXPECT_EQ(results[0].status, TransferStatus::WAITING);
  EXPECT_EQ(results[1].req, req2);
  EXPECT_EQ(results[1].user_data, user_data2);
  EXPECT_EQ(results[1].status, TransferStatus::COMPLETED);
  EXPECT_EQ(engine.client_manager_.GetClientByReq(req1), client);
  EXPECT_EQ(engine.client_manager_.GetClientByReq(req2), nullptr);
  EXPECT_EQ(engine.client_manager_.Finalize(), SUCCESS);
}

TEST(HixlEngineBatchStatusTest, SkipWaitingAndMaxQueryCountFilterResults) {
  HixlEngine engine("127.0.0.1:26200");
  ASSERT_EQ(engine.client_manager_.Initialize(false), SUCCESS);
  auto handler = std::make_unique<MockClientHandler>();
  TransferReq waiting_req = reinterpret_cast<TransferReq>(0x1000);
  TransferReq completed_req1 = reinterpret_cast<TransferReq>(0x2000);
  TransferReq completed_req2 = reinterpret_cast<TransferReq>(0x3000);
  handler->status_by_req[waiting_req] = TransferStatus::WAITING;
  handler->status_by_req[completed_req1] = TransferStatus::COMPLETED;
  handler->status_by_req[completed_req2] = TransferStatus::COMPLETED;
  auto client = CreateMockClient(std::move(handler));

  const void *waiting_user_data = reinterpret_cast<const void *>(0x4000);
  const void *completed_user_data1 = reinterpret_cast<const void *>(0x5000);
  const void *completed_user_data2 = reinterpret_cast<const void *>(0x6000);
  RegisterMockTransferReq(engine, client, waiting_req, waiting_user_data);
  RegisterMockTransferReq(engine, client, completed_req1, completed_user_data1);
  RegisterMockTransferReq(engine, client, completed_req2, completed_user_data2);

  GetTransferStatusArgs args{};
  args.skip_waiting = true;
  args.max_query_count = 1;
  std::vector<TransferResult> results;
  EXPECT_EQ(engine.GetTransferStatus(args, results), SUCCESS);

  ASSERT_EQ(results.size(), 1U);
  EXPECT_EQ(results[0].req, completed_req1);
  EXPECT_EQ(results[0].user_data, completed_user_data1);
  EXPECT_EQ(results[0].status, TransferStatus::COMPLETED);
  EXPECT_EQ(engine.client_manager_.GetClientByReq(waiting_req), client);
  EXPECT_EQ(engine.client_manager_.GetClientByReq(completed_req1), nullptr);
  EXPECT_EQ(engine.client_manager_.GetClientByReq(completed_req2), client);
  EXPECT_EQ(engine.client_manager_.Finalize(), SUCCESS);
}

TEST(HixlEngineBatchStatusTest, FailedRetAutoDisconnectDedupForSameEngine) {
  HixlEngine engine("127.0.0.1:26200");
  DisconnectedBatchStatusContext ctx{};
  SetupDisconnectedEngineWithFailedTransferReqs(engine, ctx, nullptr, nullptr);

  GetTransferStatusArgs args{};
  std::vector<TransferResult> results;
  EXPECT_EQ(engine.GetTransferStatus(args, results), SUCCESS);
  EXPECT_EQ(engine.client_manager_.GetClientByReq(ctx.req1), nullptr);
  EXPECT_EQ(engine.client_manager_.GetClientByReq(ctx.req2), nullptr);
  EXPECT_EQ(engine.client_manager_.Finalize(), SUCCESS);
}

TEST(HixlEngineBatchStatusTest, DisconnectedEngineStopsAtMaxQueryCount) {
  HixlEngine engine("127.0.0.1:26200");
  DisconnectedBatchStatusContext ctx{};
  SetupDisconnectedEngineWithFailedTransferReqs(engine, ctx);

  GetTransferStatusArgs args{};
  args.max_query_count = 2;
  std::vector<TransferResult> results;
  EXPECT_EQ(engine.GetTransferStatus(args, results), SUCCESS);

  ASSERT_EQ(results.size(), 2U);
  EXPECT_EQ(results[0].req, ctx.req1);
  EXPECT_EQ(results[0].status, TransferStatus::FAILED);
  EXPECT_EQ(results[1].req, ctx.req2);
  EXPECT_EQ(results[1].status, TransferStatus::FAILED);
  EXPECT_EQ(engine.client_manager_.GetClientByReq(ctx.req1), nullptr);
  EXPECT_EQ(engine.client_manager_.GetClientByReq(ctx.req2), nullptr);
  EXPECT_EQ(engine.client_manager_.Finalize(), SUCCESS);
}

TEST(HixlEngineBatchStatusTest, SkipWaitingStillReturnsFailedAfterEngineDisconnect) {
  HixlEngine engine("127.0.0.1:26200");
  DisconnectedBatchStatusContext ctx{};
  SetupDisconnectedEngineWithFailedTransferReqs(engine, ctx);

  GetTransferStatusArgs args{};
  args.skip_waiting = true;
  std::vector<TransferResult> results;
  EXPECT_EQ(engine.GetTransferStatus(args, results), SUCCESS);

  ASSERT_EQ(results.size(), 2U);
  EXPECT_EQ(results[0].req, ctx.req1);
  EXPECT_EQ(results[0].user_data, ctx.user_data1);
  EXPECT_EQ(results[0].status, TransferStatus::FAILED);
  EXPECT_EQ(results[1].req, ctx.req2);
  EXPECT_EQ(results[1].user_data, ctx.user_data2);
  EXPECT_EQ(results[1].status, TransferStatus::FAILED);
  EXPECT_EQ(engine.client_manager_.Finalize(), SUCCESS);
}

// auto_connect=1，显式 Connect 后再 transfer — 验证用户主动建链后传输正常
TEST_F(HixlEngineTest, TestAutoConnectExplicitConnectBeforeTransfer) {
  HixlEngine engine1("127.0.0.1");
  HixlEngine engine2("127.0.0.1:26300");
  int32_t src = 1;
  int32_t dst = 2;
  MemHandle handle1 = nullptr;
  MemHandle handle2 = nullptr;
  InitAutoConnectEngines(engine1, engine2, src, dst, handle1, handle2);

  // 用户显式 Connect
  EXPECT_EQ(engine1.Connect("127.0.0.1:26300", kTimeOut), SUCCESS);

  // 传输正常
  TransferOpDesc desc{reinterpret_cast<uintptr_t>(&src), reinterpret_cast<uintptr_t>(&dst), sizeof(int32_t)};
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26300", READ, {desc}, kTimeOut), SUCCESS);
  EXPECT_EQ(src, 2);

  EXPECT_EQ(engine1.Disconnect("127.0.0.1:26300", kTimeOut), SUCCESS);
  EXPECT_EQ(engine1.DeregisterMem(handle1), SUCCESS);
  EXPECT_EQ(engine2.DeregisterMem(handle2), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
}

// auto_connect=1 + Direct handler：先 transfer 再显式 Connect
// Direct handler 在内部 AutoConnect 时已全量建链，显式 Connect 返回 ALREADY_CONNECTED
TEST_F(HixlEngineTest, TestAutoConnectTransferThenExplicitConnect) {
  HixlEngine engine1("127.0.0.1");
  HixlEngine engine2("127.0.0.1:26300");
  int32_t src = 1;
  int32_t dst = 2;
  MemHandle handle1 = nullptr;
  MemHandle handle2 = nullptr;
  InitAutoConnectEngines(engine1, engine2, src, dst, handle1, handle2);

  // 首次 transfer：内部 AutoConnect 建链
  TransferOpDesc desc = TriggerAutoConnectReadTransfer(engine1, src, dst);

  // 显式 Connect：client 已存在且已全量建链，返回 ALREADY_CONNECTED
  EXPECT_EQ(engine1.Connect("127.0.0.1:26300", kTimeOut), ALREADY_CONNECTED);

  // 传输仍正常（链路已连接）
  src = 1;
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26300", WRITE, {desc}, kTimeOut), SUCCESS);
  EXPECT_EQ(dst, 1);

  EXPECT_EQ(engine1.Disconnect("127.0.0.1:26300", kTimeOut), SUCCESS);
  EXPECT_EQ(engine1.DeregisterMem(handle1), SUCCESS);
  EXPECT_EQ(engine2.DeregisterMem(handle2), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
}

// auto_connect=1 显式 Connect 后再次 Connect — 验证返回 ALREADY_CONNECTED
TEST_F(HixlEngineTest, TestAutoConnectDoubleConnectReturnsAlreadyConnected) {
  HixlEngine engine1("127.0.0.1");
  HixlEngine engine2("127.0.0.1:26300");
  int32_t src = 1;
  int32_t dst = 2;
  MemHandle handle1 = nullptr;
  MemHandle handle2 = nullptr;
  InitAutoConnectEngines(engine1, engine2, src, dst, handle1, handle2);

  EXPECT_EQ(engine1.Connect("127.0.0.1:26300", kTimeOut), SUCCESS);
  // 显式 Connect 强制 is_lazy=false → SupportsReconnect=false → ALREADY_CONNECTED
  EXPECT_EQ(engine1.Connect("127.0.0.1:26300", kTimeOut), ALREADY_CONNECTED);

  // 传输仍然正常
  TransferOpDesc desc{reinterpret_cast<uintptr_t>(&src), reinterpret_cast<uintptr_t>(&dst), sizeof(int32_t)};
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26300", READ, {desc}, kTimeOut), SUCCESS);
  EXPECT_EQ(src, 2);

  EXPECT_EQ(engine1.Disconnect("127.0.0.1:26300", kTimeOut), SUCCESS);
  EXPECT_EQ(engine1.DeregisterMem(handle1), SUCCESS);
  EXPECT_EQ(engine2.DeregisterMem(handle2), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
}

// auto_connect=1 + 同实例（UB handler）：显式 Connect 后再 transfer
TEST_F(HixlEngineTest, TestAutoConnectUbExplicitConnectBeforeTransfer) {
  HixlEngine engine1("127.0.0.1");
  HixlEngine engine2("127.0.0.1:26300");
  int32_t src = 1, dst = 2;
  MemHandle handle1 = nullptr, handle2 = nullptr;
  InitAutoConnectEngines(engine1, engine2, src, dst, handle1, handle2, true);

  EXPECT_EQ(engine1.Connect("127.0.0.1:26300", kTimeOut), SUCCESS);

  TransferOpDesc desc{reinterpret_cast<uintptr_t>(&src), reinterpret_cast<uintptr_t>(&dst), sizeof(int32_t)};
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26300", READ, {desc}, kTimeOut), SUCCESS);

  EXPECT_EQ(engine1.Disconnect("127.0.0.1:26300", kTimeOut), SUCCESS);
  EXPECT_EQ(engine1.DeregisterMem(handle1), SUCCESS);
  EXPECT_EQ(engine2.DeregisterMem(handle2), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
}

// auto_connect=1 + 同实例（UB handler 1条链路）：先 transfer 再显式 Connect 再 transfer
// 仅 D2D 一条链路，transfer 已建完，显式 Connect 无链路可补 → ALREADY_CONNECTED
TEST_F(HixlEngineTest, TestAutoConnectUbTransferThenExplicitConnect) {
  HixlEngine engine1("127.0.0.1");
  HixlEngine engine2("127.0.0.1:26300");
  int32_t src = 1, dst = 2;
  MemHandle handle1 = nullptr, handle2 = nullptr;
  InitAutoConnectEngines(engine1, engine2, src, dst, handle1, handle2, true);

  TransferOpDesc desc{reinterpret_cast<uintptr_t>(&src), reinterpret_cast<uintptr_t>(&dst), sizeof(int32_t)};
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26300", READ, {desc}, kTimeOut), SUCCESS);

  // 仅 1 条 D2D 链路，transfer 已建完，显式 Connect 返回 ALREADY_CONNECTED
  EXPECT_EQ(engine1.Connect("127.0.0.1:26300", kTimeOut), ALREADY_CONNECTED);

  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26300", WRITE, {desc}, kTimeOut), SUCCESS);

  EXPECT_EQ(engine1.Disconnect("127.0.0.1:26300", kTimeOut), SUCCESS);
  EXPECT_EQ(engine1.DeregisterMem(handle1), SUCCESS);
  EXPECT_EQ(engine2.DeregisterMem(handle2), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
}

// auto_connect=1 + 同实例（UB handler 4条链路）：先 transfer 再显式 Connect 再 transfer
// client 已存在时，显式 Connect 直接返回 ALREADY_CONNECTED
TEST_F(HixlEngineTest, TestAutoConnectUb4LinksTransferThenExplicitConnect) {
  HixlEngine engine1("127.0.0.1");
  HixlEngine engine2("127.0.0.1:26300");
  int32_t src = 1, dst = 2;
  MemHandle handle1 = nullptr, handle2 = nullptr;
  // 使用 4 条 UB 链路配置
  std::map<AscendString, AscendString> auto_connect_opts = options_4ub;
  auto_connect_opts[hixl::OPTION_AUTO_CONNECT] = "1";
  CreateAndInitEngine(engine1, auto_connect_opts);
  CreateAndInitEngine(engine2, options_4ub_pair);
  Register(engine1, &src, handle1);
  Register(engine2, &dst, handle2);

  TransferOpDesc desc{reinterpret_cast<uintptr_t>(&src), reinterpret_cast<uintptr_t>(&dst), sizeof(int32_t)};
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26300", READ, {desc}, kTimeOut), SUCCESS);

  EXPECT_EQ(engine1.Connect("127.0.0.1:26300", kTimeOut), ALREADY_CONNECTED);

  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26300", WRITE, {desc}, kTimeOut), SUCCESS);

  EXPECT_EQ(engine1.Disconnect("127.0.0.1:26300", kTimeOut), SUCCESS);
  EXPECT_EQ(engine1.DeregisterMem(handle1), SUCCESS);
  EXPECT_EQ(engine2.DeregisterMem(handle2), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
}

// auto_connect=1 + 同实例（UB handler）：显式 Connect 两次
// 二次 Connect 返回 ALREADY_CONNECTED
TEST_F(HixlEngineTest, TestAutoConnectUbDoubleConnect) {
  HixlEngine engine1("127.0.0.1");
  HixlEngine engine2("127.0.0.1:26300");
  int32_t src = 1, dst = 2;
  MemHandle handle1 = nullptr, handle2 = nullptr;
  InitAutoConnectEngines(engine1, engine2, src, dst, handle1, handle2, true);

  EXPECT_EQ(engine1.Connect("127.0.0.1:26300", kTimeOut), SUCCESS);
  EXPECT_EQ(engine1.Connect("127.0.0.1:26300", kTimeOut), ALREADY_CONNECTED);

  TransferOpDesc desc{reinterpret_cast<uintptr_t>(&src), reinterpret_cast<uintptr_t>(&dst), sizeof(int32_t)};
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26300", READ, {desc}, kTimeOut), SUCCESS);

  EXPECT_EQ(engine1.Disconnect("127.0.0.1:26300", kTimeOut), SUCCESS);
  EXPECT_EQ(engine1.DeregisterMem(handle1), SUCCESS);
  EXPECT_EQ(engine2.DeregisterMem(handle2), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(HixlEngineTest, DeregisterMemWhileConnectedReturnsSuccess) {
  HixlEngine engine1("127.0.0.1");
  HixlEngine engine2("127.0.0.1:26300");
  int32_t src = 1;
  int32_t dst = 2;
  MemHandle handle1 = nullptr;
  MemHandle handle2 = nullptr;
  InitEnginesWithRegisteredMem(engine1, engine2, src, dst, handle1, handle2);

  EXPECT_EQ(engine1.Connect("127.0.0.1:26300", kTimeOut), SUCCESS);

  EXPECT_EQ(engine1.DeregisterMem(handle1), SUCCESS);

  TransferOpDesc desc{reinterpret_cast<uintptr_t>(&src), reinterpret_cast<uintptr_t>(&dst), sizeof(int32_t)};
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26300", READ, {desc}, kTimeOut), PARAM_INVALID);

  EXPECT_EQ(engine1.Disconnect("127.0.0.1:26300", kTimeOut), SUCCESS);
  EXPECT_EQ(engine2.DeregisterMem(handle2), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(ClientManagerTest, DeregisterMemInvokesEveryClient) {
  ClientManager manager;
  EXPECT_EQ(manager.Initialize(false), SUCCESS);
  auto handler_a = std::make_unique<MockClientHandler>();
  auto handler_b = std::make_unique<MockClientHandler>();
  MockClientHandler *raw_a = handler_a.get();
  MockClientHandler *raw_b = handler_b.get();
  manager.clients_["engine-a"] = CreateMockClient(std::move(handler_a));
  manager.clients_["engine-b"] = CreateMockClient(std::move(handler_b));

  MemHandle mem_handle = reinterpret_cast<MemHandle>(0x2000);
  EXPECT_EQ(manager.DeregisterMem(mem_handle), SUCCESS);
  ASSERT_EQ(raw_a->deregistered_handles.size(), 1U);
  ASSERT_EQ(raw_b->deregistered_handles.size(), 1U);
  EXPECT_EQ(raw_a->deregistered_handles[0], mem_handle);
  EXPECT_EQ(raw_b->deregistered_handles[0], mem_handle);
  EXPECT_EQ(manager.Finalize(), SUCCESS);
}

TEST_F(ClientManagerTest, DeregisterMemStopsOnFailure) {
  ClientManager manager;
  EXPECT_EQ(manager.Initialize(false), SUCCESS);
  auto handler_a = std::make_unique<MockClientHandler>();
  auto handler_b = std::make_unique<MockClientHandler>();
  MockClientHandler *raw_a = handler_a.get();
  MockClientHandler *raw_b = handler_b.get();
  raw_a->deregister_ret = FAILED;
  manager.clients_["engine-a"] = CreateMockClient(std::move(handler_a));
  manager.clients_["engine-b"] = CreateMockClient(std::move(handler_b));

  EXPECT_EQ(manager.DeregisterMem(reinterpret_cast<MemHandle>(0x3000)), FAILED);
  EXPECT_EQ(raw_a->deregistered_handles.size(), 1U);
  EXPECT_EQ(raw_b->deregistered_handles.size(), 0U);
  EXPECT_EQ(manager.Finalize(), SUCCESS);
}

TEST(HixlClientDeregisterTest, DeregisterMemWithoutHandlerSucceeds) {
  ClientConfig config{};
  HixlClient client("127.0.0.1", 26300, config);
  EXPECT_EQ(client.DeregisterMem(reinterpret_cast<MemHandle>(0x1000)), SUCCESS);
}

TEST(HixlClientDeregisterTest, DeregisterMemPropagatesHandlerError) {
  auto handler = std::make_unique<MockClientHandler>();
  handler->deregister_ret = FAILED;
  ClientPtr client = CreateMockClient(std::move(handler));
  EXPECT_EQ(client->DeregisterMem(reinterpret_cast<MemHandle>(0x1000)), FAILED);
}

TEST_F(HixlEngineTest, DeregisterMemWhileUbConnectedReturnsSuccess) {
  HixlEngine engine1("127.0.0.1");
  HixlEngine engine2("127.0.0.1:26300");
  int32_t src = 1;
  int32_t dst = 2;
  MemHandle handle1 = nullptr;
  MemHandle handle2 = nullptr;
  InitEnginesWithRegisteredMem(engine1, engine2, src, dst, handle1, handle2, true);
  EXPECT_EQ(engine1.Connect("127.0.0.1:26300", kTimeOut), SUCCESS);

  EXPECT_EQ(engine1.DeregisterMem(handle1), SUCCESS);
  EXPECT_FALSE(engine1.client_manager_.IsEmpty());
  EXPECT_EQ(engine1.mem_map_.count(handle1), 0U);

  TransferOpDesc desc{reinterpret_cast<uintptr_t>(&src), reinterpret_cast<uintptr_t>(&dst), sizeof(int32_t)};
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26300", READ, {desc}, kTimeOut), PARAM_INVALID);
  EXPECT_EQ(engine1.Disconnect("127.0.0.1:26300", kTimeOut), SUCCESS);
  EXPECT_EQ(engine2.DeregisterMem(handle2), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(HixlEngineTest, DeregisterOneMemKeepsOtherMemTransferable) {
  HixlEngine engine1("127.0.0.1");
  HixlEngine engine2("127.0.0.1:26300");
  int32_t src_keep = 1;
  int32_t dst_keep = 2;
  int32_t src_drop = 3;
  int32_t dst_drop = 4;
  MemHandle handle_keep1 = nullptr;
  MemHandle handle_keep2 = nullptr;
  MemHandle handle_drop1 = nullptr;
  MemHandle handle_drop2 = nullptr;
  InitEnginesWithRegisteredMem(engine1, engine2, src_keep, dst_keep, handle_keep1, handle_keep2);
  Register(engine1, &src_drop, handle_drop1);
  Register(engine2, &dst_drop, handle_drop2);
  EXPECT_EQ(engine1.Connect("127.0.0.1:26300", kTimeOut), SUCCESS);

  EXPECT_EQ(engine1.DeregisterMem(handle_drop1), SUCCESS);
  EXPECT_EQ(engine1.mem_map_.count(handle_keep1), 1U);
  EXPECT_FALSE(engine1.client_manager_.IsEmpty());

  TransferOpDesc keep_desc{reinterpret_cast<uintptr_t>(&src_keep), reinterpret_cast<uintptr_t>(&dst_keep),
                           sizeof(int32_t)};
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26300", READ, {keep_desc}, kTimeOut), SUCCESS);
  EXPECT_EQ(src_keep, 2);
  TransferOpDesc drop_desc{reinterpret_cast<uintptr_t>(&src_drop), reinterpret_cast<uintptr_t>(&dst_drop),
                           sizeof(int32_t)};
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26300", READ, {drop_desc}, kTimeOut), PARAM_INVALID);

  EXPECT_EQ(engine1.Disconnect("127.0.0.1:26300", kTimeOut), SUCCESS);
  EXPECT_EQ(engine1.DeregisterMem(handle_keep1), SUCCESS);
  EXPECT_EQ(engine2.DeregisterMem(handle_keep2), SUCCESS);
  EXPECT_EQ(engine2.DeregisterMem(handle_drop2), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(HixlEngineTest, DeregisterMemTwiceWhileConnectedIsIdempotent) {
  HixlEngine engine1("127.0.0.1");
  HixlEngine engine2("127.0.0.1:26300");
  int32_t src = 1;
  int32_t dst = 2;
  MemHandle handle1 = nullptr;
  MemHandle handle2 = nullptr;
  InitAutoConnectEngines(engine1, engine2, src, dst, handle1, handle2);
  EXPECT_EQ(engine1.Connect("127.0.0.1:26300", kTimeOut), SUCCESS);

  EXPECT_EQ(engine1.DeregisterMem(handle1), SUCCESS);
  EXPECT_EQ(engine1.DeregisterMem(handle1), SUCCESS);
  EXPECT_FALSE(engine1.client_manager_.IsEmpty());

  EXPECT_EQ(engine1.Disconnect("127.0.0.1:26300", kTimeOut), SUCCESS);
  EXPECT_EQ(engine2.DeregisterMem(handle2), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(HixlEngineTest, DeregisterMemConcurrentWithConnectRemovesMemFromNewClient) {
  HixlEngine engine1("127.0.0.1");
  HixlEngine engine2("127.0.0.1:26300");
  int32_t src = 1;
  int32_t dst = 2;
  MemHandle handle1 = nullptr;
  MemHandle handle2 = nullptr;
  InitEnginesWithRegisteredMem(engine1, engine2, src, dst, handle1, handle2);

  Status connect_ret = FAILED;
  std::thread connect_thread(
      [&engine1, &connect_ret]() { connect_ret = engine1.Connect("127.0.0.1:26300", kTimeOut); });
  EXPECT_EQ(engine1.DeregisterMem(handle1), SUCCESS);
  connect_thread.join();
  EXPECT_EQ(engine1.mem_map_.count(handle1), 0U);

  ClientPtr client = engine1.client_manager_.GetClient("127.0.0.1:26300");
  if (connect_ret == SUCCESS || connect_ret == ALREADY_CONNECTED) {
    ASSERT_NE(client, nullptr);
    EXPECT_FALSE(ClientHasLocalMem(client, handle1));
    EXPECT_EQ(engine1.Disconnect("127.0.0.1:26300", kTimeOut), SUCCESS);
  }

  EXPECT_EQ(engine2.DeregisterMem(handle2), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(HixlEngineTest, EngineFactoryUsesHixlEngineWhenUbmemProtocolDesc) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"comm_resource_config.protocol_desc":["ubmem"]})";
  HixlOptions parsed;
  auto engine = EngineFactory::CreateEngine("127.0.0.1:26000", options, parsed);
  ASSERT_NE(engine, nullptr);
  EXPECT_NE(dynamic_cast<HixlEngine *>(engine.get()), nullptr);
  EXPECT_TRUE(parsed.EnableUbMem().value_or(false));
}

TEST_F(HixlEngineTest, EngineFactoryUsesHixlEngineWhenUbMemEnabled) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_ENABLE_USE_FABRIC_MEM] = "1";
  HixlOptions parsed;
  auto engine = EngineFactory::CreateEngine("127.0.0.1:26000", options, parsed);
  ASSERT_NE(engine, nullptr);
  EXPECT_NE(dynamic_cast<HixlEngine *>(engine.get()), nullptr);
}

TEST_F(HixlEngineTest, InitializeUbMemSucceedsAndRegisterMem) {
  SetSocStub("Ascend910_9391", 1, 23, 45, 67);
  SetHccnConfContent("address_23=10.10.10.23\n");
  std::map<AscendString, AscendString> options = BuildOptions(BuildVersionOnlyLocalCommRes("1.3"));
  options[hixl::OPTION_ENABLE_USE_FABRIC_MEM] = "1";
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"fabric_memory":{"enable_aicpu_unfold":false}})";
  HixlEngine engine("127.0.0.1");
  HixlOptions parsed;
  ASSERT_EQ(HixlOptions::Parse(options, parsed), SUCCESS);
  ASSERT_EQ(engine.Initialize(parsed), SUCCESS);
  EXPECT_TRUE(engine.enable_ubmem_);
  ASSERT_EQ(engine.endpoint_list_.size(), 1U);
  EXPECT_EQ(engine.endpoint_list_[0].protocol, kProtocolUbmem);
  EXPECT_EQ(engine.endpoint_list_[0].placement, kPlacementDevice);
  EXPECT_FALSE(engine.auto_connect_);
  EXPECT_FALSE(engine.client_manager_.heartbeat_sender_.joinable());
  std::vector<uint8_t> buf(64, 0);
  MemDesc mem{};
  mem.addr = reinterpret_cast<uintptr_t>(buf.data());
  mem.len = buf.size();
  MemHandle handle = nullptr;
  EXPECT_EQ(engine.RegisterMem(mem, MEM_HOST, handle), SUCCESS);
  EXPECT_NE(handle, nullptr);
  ASSERT_EQ(engine.mem_map_.count(handle), 1U);
  EXPECT_EQ(engine.server_.handle_to_addr_.count(handle), 1U);
  std::vector<MemInfo> mem_info = engine.server_.GetRegisteredMemInfo();
  EXPECT_FALSE(mem_info.empty());
  MemHandle duplicate_handle = nullptr;
  EXPECT_EQ(engine.RegisterMem(mem, MEM_HOST, duplicate_handle), SUCCESS);
  EXPECT_EQ(duplicate_handle, handle);
  EXPECT_EQ(engine.DeregisterMem(handle), SUCCESS);
  engine.Finalize();
}

TEST_F(HixlEngineTest, InitializeUbMemRegisterMemLocalOnlySkipsServerReg) {
  SetSocStub("Ascend910_9391", 1, 23, 45, 67);
  SetHccnConfContent("address_23=10.10.10.23\n");
  std::map<AscendString, AscendString> options = BuildOptions(BuildVersionOnlyLocalCommRes("1.3"));
  options[hixl::OPTION_ENABLE_USE_FABRIC_MEM] = "1";
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"fabric_memory":{"enable_aicpu_unfold":false}})";
  HixlEngine engine("127.0.0.1");
  HixlOptions parsed;
  ASSERT_EQ(HixlOptions::Parse(options, parsed), SUCCESS);
  ASSERT_EQ(engine.Initialize(parsed), SUCCESS);
  ASSERT_EQ(engine.endpoint_list_.size(), 1U);
  EXPECT_EQ(engine.endpoint_list_[0].protocol, kProtocolUbmem);

  std::vector<uint8_t> buf(64, 0);
  MemDesc mem{};
  mem.addr = reinterpret_cast<uintptr_t>(buf.data());
  mem.len = buf.size();
  mem.local_only = true;
  MemHandle handle = nullptr;
  EXPECT_EQ(engine.RegisterMem(mem, MEM_HOST, handle), SUCCESS);
  EXPECT_NE(handle, nullptr);
  ASSERT_EQ(engine.mem_map_.count(handle), 1U);
  ASSERT_EQ(engine.server_.handle_to_addr_.count(handle), 1U);
  EXPECT_TRUE(engine.server_.handle_to_addr_.at(handle).local_only);
  EXPECT_TRUE(engine.server_.GetRegisteredMemInfo().empty());

  MemHandle duplicate_handle = nullptr;
  EXPECT_EQ(engine.RegisterMem(mem, MEM_HOST, duplicate_handle), SUCCESS);
  EXPECT_EQ(duplicate_handle, handle);
  EXPECT_EQ(engine.DeregisterMem(handle), SUCCESS);
  EXPECT_EQ(engine.server_.handle_to_addr_.count(handle), 0U);
  engine.Finalize();
}

TEST_F(HixlEngineTest, InitializeUbMemRegisterMemRejectsLocalOnlyMismatch) {
  SetSocStub("Ascend910_9391", 1, 23, 45, 67);
  SetHccnConfContent("address_23=10.10.10.23\n");
  std::map<AscendString, AscendString> options = BuildOptions(BuildVersionOnlyLocalCommRes("1.3"));
  options[hixl::OPTION_ENABLE_USE_FABRIC_MEM] = "1";
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"fabric_memory":{"enable_aicpu_unfold":false}})";
  HixlEngine engine("127.0.0.1");
  HixlOptions parsed;
  ASSERT_EQ(HixlOptions::Parse(options, parsed), SUCCESS);
  ASSERT_EQ(engine.Initialize(parsed), SUCCESS);

  std::vector<uint8_t> local_only_buf(64, 0);
  MemDesc local_only{};
  local_only.addr = reinterpret_cast<uintptr_t>(local_only_buf.data());
  local_only.len = local_only_buf.size();
  local_only.local_only = true;
  MemHandle local_only_handle = nullptr;
  ASSERT_EQ(engine.RegisterMem(local_only, MEM_HOST, local_only_handle), SUCCESS);

  // 已按 local-only 注册的内存，不允许再以 local_only=false 注册同一区间。
  MemDesc conflicting = local_only;
  conflicting.local_only = false;
  MemHandle conflicting_handle = nullptr;
  EXPECT_EQ(engine.RegisterMem(conflicting, MEM_HOST, conflicting_handle), PARAM_INVALID);
  EXPECT_EQ(conflicting_handle, nullptr);
  EXPECT_EQ(engine.server_.handle_to_addr_.count(local_only_handle), 1U);
  EXPECT_EQ(engine.DeregisterMem(local_only_handle), SUCCESS);
  engine.Finalize();
}

TEST_F(HixlEngineTest, InitializeUbMemViaUbmemProtocolDesc) {
  SetSocStub("Ascend910_9391", 1, 23, 45, 67);
  SetHccnConfContent("address_23=10.10.10.23\n");
  std::map<AscendString, AscendString> options = BuildOptions(BuildVersionOnlyLocalCommRes("1.3"));
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"comm_resource_config.protocol_desc":["ubmem"]})";
  HixlEngine engine("127.0.0.1");
  HixlOptions parsed;
  ASSERT_EQ(HixlOptions::Parse(options, parsed), SUCCESS);
  ASSERT_EQ(engine.Initialize(parsed), SUCCESS);
  EXPECT_TRUE(engine.enable_ubmem_);
  ASSERT_EQ(engine.endpoint_list_.size(), 1U);
  EXPECT_EQ(engine.endpoint_list_[0].protocol, kProtocolUbmem);
  EXPECT_EQ(engine.endpoint_list_[0].placement, kPlacementDevice);
  engine.Finalize();
}

TEST_F(HixlEngineTest, InitializeUbMemIgnoresOtherProtocolDescWhenSwitchEnabled) {
  SetSocStub("Ascend910_9391", 1, 23, 45, 67);
  SetHccnConfContent("address_23=10.10.10.23\n");
  std::map<AscendString, AscendString> options = BuildOptions(BuildVersionOnlyLocalCommRes("1.3"));
  options[hixl::OPTION_ENABLE_USE_FABRIC_MEM] = "1";
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"fabric_memory":{"enable_aicpu_unfold":false},)"
                                                 R"("comm_resource_config.protocol_desc":["roce:device"]})";
  HixlEngine engine("127.0.0.1");
  HixlOptions parsed;
  ASSERT_EQ(HixlOptions::Parse(options, parsed), SUCCESS);
  ASSERT_EQ(engine.Initialize(parsed), SUCCESS);
  ASSERT_EQ(engine.endpoint_list_.size(), 1U);
  EXPECT_EQ(engine.endpoint_list_[0].protocol, kProtocolUbmem);
  EXPECT_EQ(engine.endpoint_list_[0].placement, kPlacementDevice);
  engine.Finalize();
}

TEST_F(HixlEngineTest, UbMemAutoConnectStartsHeartbeat) {
  SetSocStub("Ascend910_9391", 1, 23, 45, 67);
  SetHccnConfContent("address_23=10.10.10.23\n");
  std::map<AscendString, AscendString> options = BuildOptions(BuildVersionOnlyLocalCommRes("1.3"));
  options[hixl::OPTION_ENABLE_USE_FABRIC_MEM] = "1";
  options[hixl::OPTION_AUTO_CONNECT] = "1";
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"fabric_memory":{"enable_aicpu_unfold":false}})";
  HixlEngine engine("127.0.0.1");
  HixlOptions parsed;
  ASSERT_EQ(HixlOptions::Parse(options, parsed), SUCCESS);
  ASSERT_EQ(engine.Initialize(parsed), SUCCESS);
  EXPECT_TRUE(engine.auto_connect_);
  EXPECT_TRUE(engine.client_manager_.heartbeat_sender_.joinable());
  engine.Finalize();
}

TEST_F(HixlEngineTest, UbMemCrossSuperPodKeepsUbmemAndRoceDevice) {
  SetSocStub("Ascend910_9391", 1, 23, 45, 67);
  SetHccnConfContent("address_23=10.10.10.23\n");
  std::map<AscendString, AscendString> options = BuildOptions(BuildVersionOnlyLocalCommRes("1.3"));
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"fabric_memory":{"enable_aicpu_unfold":false},)"
                                                 R"("comm_resource_config.protocol_desc":["ubmem","roce:device"]})";
  HixlEngine engine("127.0.0.1");
  HixlOptions parsed;
  ASSERT_EQ(HixlOptions::Parse(options, parsed), SUCCESS);
  ASSERT_EQ(engine.Initialize(parsed), SUCCESS);
  EXPECT_TRUE(engine.enable_ubmem_);
  bool has_ubmem = false;
  bool has_roce_device = false;
  for (const auto &ep : engine.endpoint_list_) {
    has_ubmem = has_ubmem || (ep.protocol == kProtocolUbmem && ep.placement == kPlacementDevice);
    has_roce_device = has_roce_device || (ep.protocol == kProtocolRoce && ep.placement == kPlacementDevice);
  }
  EXPECT_TRUE(has_ubmem);
  EXPECT_TRUE(has_roce_device);
  std::vector<uint8_t> buf(64, 0);
  MemDesc mem{};
  mem.addr = reinterpret_cast<uintptr_t>(buf.data());
  mem.len = buf.size();
  MemHandle handle = nullptr;
  EXPECT_EQ(engine.RegisterMem(mem, MEM_HOST, handle), SUCCESS);
  EXPECT_NE(handle, nullptr);
  ASSERT_EQ(engine.mem_map_.count(handle), 1U);
  EXPECT_EQ(engine.mem_map_.at(handle).type, MEM_HOST);
  EXPECT_EQ(engine.server_.handle_to_addr_.count(handle), 1U);
  EXPECT_EQ(engine.DeregisterMem(handle), SUCCESS);
  engine.Finalize();
}

TEST_F(HixlEngineTest, UbMemCrossSuperPodKeepsUbmemWhenRoceDeviceMissing) {
  SetSocStub("Ascend910_9391", 1, 23, 45, 67);
  SetHccnConfContent("address_23=10.10.10.23\n");
  std::map<AscendString, AscendString> options =
      BuildOptions(BuildLocalCommRes("superpod1_1", "1.3", {BuildHostRoceEndpoint("127.0.0.1")}));
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"fabric_memory":{"enable_aicpu_unfold":false},)"
                                                 R"("comm_resource_config.protocol_desc":["ubmem","roce:device"]})";
  HixlEngine engine("127.0.0.1");
  HixlOptions parsed;
  ASSERT_EQ(HixlOptions::Parse(options, parsed), SUCCESS);
  ASSERT_EQ(engine.Initialize(parsed), SUCCESS);
  ASSERT_EQ(engine.endpoint_list_.size(), 1U);
  EXPECT_EQ(engine.endpoint_list_[0].protocol, kProtocolUbmem);
  EXPECT_EQ(engine.endpoint_list_[0].placement, kPlacementDevice);
  engine.Finalize();
}

}  // namespace hixl
