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
#include <memory>
#include <thread>
#include <gtest/gtest.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "ascendcl_stub.h"
#include "engine/endpoint_test_utils.h"
#define private public
#include "engine/hixl_engine.h"
#undef private
#include "hixl/hixl_types.h"
#include "cs/hixl_cs_client.h"
#include "hixl/hixl.h"
#include "slog_stub.h"
#include "test_mmpa_utils.h"
#include "depends/mmpa/src/mmpa_stub.h"

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

std::string BuildLocalCommRes(const std::string &net_instance_id,
                              const std::string &version,
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
    EXPECT_EQ(engine1.Initialize(opts1), SUCCESS);
    EXPECT_EQ(engine2.Initialize(opts2), SUCCESS);
    EXPECT_EQ(engine1.Connect("127.0.0.1:16000", kTimeOut), SUCCESS);
  }

  void CleanupEngines(HixlEngine &engine1, HixlEngine &engine2) {
    engine1.Disconnect();
    engine1.Finalize();
    engine2.Disconnect();
    engine2.Finalize();
  }

  std::shared_ptr<MockEngineAclRuntimeStub> acl_stub_;

  void SetSocStub(const std::string &soc_name, int32_t device_id, int32_t phy_device_id,
                  int64_t super_device_id, int64_t super_pod_id) {
    acl_stub_ =
        endpoint_test::CreateAclRuntimeStub(soc_name, device_id, phy_device_id, super_device_id, super_pod_id);
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
  bool CheckIpv6Supported() {
    int fd = socket(AF_INET6, SOCK_STREAM, 0);
    if (fd < 0) {
      return false;
    }
    struct sockaddr_in6 addr{};
    addr.sin6_family = AF_INET6;
    (void)inet_pton(AF_INET6, "::1", &addr.sin6_addr);
    addr.sin6_port = htons(0U);
    bool ok = (connect(fd, (sockaddr *)&addr, sizeof(addr)) != -1 || errno != EADDRNOTAVAIL);
    close(fd);
    return ok;
  }

  std::shared_ptr<MockEngineMmpaStub> mmpa_stub_;
  std::vector<std::string> temp_files_;
  std::string old_intra_roce_enable_;
};

TEST_F(HixlEngineTest, TestHixl) {
  SetSocStub("Ascend910B1", 0, 12, 99, 88);
  Hixl engine1;
  EXPECT_EQ(engine1.Initialize("127.0.0.1", options1), SUCCESS);

  Hixl engine2;
  EXPECT_EQ(engine2.Initialize("127.0.0.1:16000", options2), SUCCESS);

  int32_t src = 1;
  hixl::MemDesc src_mem{};
  src_mem.addr = reinterpret_cast<uintptr_t>(&src);
  src_mem.len = sizeof(int32_t);
  MemHandle handle1 = nullptr;
  EXPECT_EQ(engine1.RegisterMem(src_mem, MEM_DEVICE, handle1), SUCCESS);

  int32_t dst = 2;
  hixl::MemDesc dst_mem{};
  dst_mem.addr = reinterpret_cast<uintptr_t>(&dst);
  dst_mem.len = sizeof(int32_t);
  MemHandle handle2 = nullptr;
  EXPECT_EQ(engine2.RegisterMem(dst_mem, MEM_DEVICE, handle2), SUCCESS);
  
  EXPECT_EQ(engine1.Connect("127.0.0.1:16000", kTimeOut), SUCCESS);

  TransferOpDesc desc{reinterpret_cast<uintptr_t>(&src), reinterpret_cast<uintptr_t>(&dst), sizeof(int32_t)};
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:16000", READ, {desc}), SUCCESS);
  EXPECT_EQ(src, 2);
  src = 1;
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:16000", WRITE, {desc}), SUCCESS);
  EXPECT_EQ(dst, 1);

  NotifyDesc notify;
  std::string notify_name = "test_notify";
  notify.name = AscendString(notify_name.c_str());
  std::string notify_msg = "message";
  notify.notify_msg = AscendString(notify_msg.c_str());
  EXPECT_EQ(engine1.SendNotify("127.0.0.1:16000", notify, kTimeOut), UNSUPPORTED);

  std::vector<NotifyDesc> notifies;
  EXPECT_EQ(engine2.GetNotifies(notifies), UNSUPPORTED);

  EXPECT_EQ(engine1.Disconnect("127.0.0.1:16000"), SUCCESS);
  EXPECT_EQ(engine1.DeregisterMem(handle1), SUCCESS);
  EXPECT_EQ(engine2.DeregisterMem(handle2), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(HixlEngineTest, TestHixlEngine) {
  SetSocStub("Ascend910B1", 0, 12, 99, 88);
  // IPV4
  HixlEngine engine1("127.0.0.1");
  EXPECT_EQ(engine1.Initialize(options1), SUCCESS);

  // IPV4 with port
  HixlEngine engine2("127.0.0.1:16000");
  EXPECT_EQ(engine2.Initialize(options2), SUCCESS);

  int32_t num1 = 1;
  MemHandle handle1 = nullptr;
  Register(engine1, &num1, handle1);

  int32_t num2 = 2;
  MemHandle handle2 = nullptr;
  Register(engine2, &num2, handle2);

  EXPECT_EQ(engine1.Connect("127.0.0.1:16000", kTimeOut), SUCCESS);
  TransferOpDesc desc1{reinterpret_cast<uintptr_t>(&num1), reinterpret_cast<uintptr_t>(&num2), sizeof(int32_t)};
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:16000", READ, {desc1}, kTimeOut), SUCCESS);
  EXPECT_EQ(num1, 2);
  num1 = 1;
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:16000", WRITE, {desc1}, kTimeOut), SUCCESS);
  EXPECT_EQ(num2, 1);

  EXPECT_EQ(engine1.Disconnect("127.0.0.1:16000", kTimeOut), SUCCESS);

  EXPECT_EQ(engine1.DeregisterMem(handle1), SUCCESS);
  EXPECT_EQ(engine2.DeregisterMem(handle2), SUCCESS);

  engine1.Finalize();
  engine2.Finalize();

  if (CheckIpv6Supported()) {
    // IPV6
    HixlEngine engine3("[::1]");
    EXPECT_EQ(engine3.Initialize(options1), SUCCESS);

    // IPV6 with port
    HixlEngine engine4("[::1]:26000");
    EXPECT_EQ(engine4.Initialize(options2), SUCCESS);

    int32_t num3 = 3;
    MemHandle handle3 = nullptr;
    Register(engine3, &num3, handle3);

    int32_t num4 = 4;
    MemHandle handle4 = nullptr;
    Register(engine4, &num4, handle4);

    EXPECT_EQ(engine3.Connect("[::1]:26000", kTimeOut), SUCCESS);
    TransferOpDesc desc2{reinterpret_cast<uintptr_t>(&num3), reinterpret_cast<uintptr_t>(&num4), sizeof(int32_t)};
    EXPECT_EQ(engine3.TransferSync("[::1]:26000", READ, {desc2}, kTimeOut), SUCCESS);
    EXPECT_EQ(num3, 4);
    num3 = 3;
    EXPECT_EQ(engine3.TransferSync("[::1]:26000", WRITE, {desc2}, kTimeOut), SUCCESS);
    EXPECT_EQ(num4, 3);

    EXPECT_EQ(engine3.Disconnect("[::1]:26000", kTimeOut), SUCCESS);

    EXPECT_EQ(engine3.DeregisterMem(handle3), SUCCESS);
    EXPECT_EQ(engine4.DeregisterMem(handle4), SUCCESS);

    engine3.Finalize();
    engine4.Finalize();
  }
}

TEST_F(HixlEngineTest, TestTransferAsync) {
  SetSocStub("Ascend910B1", 0, 12, 99, 88);
  std::string local_engine1 = "127.0.0.1";
  HixlEngine engine1(AscendString(local_engine1.c_str()));
  EXPECT_EQ(engine1.Initialize(options1), SUCCESS);

  std::string local_engine2 = "127.0.0.1:16000";
  HixlEngine engine2(AscendString(local_engine2.c_str()));
  EXPECT_EQ(engine2.Initialize(options2), SUCCESS);

  int32_t src = 1;
  MemHandle handle1 = nullptr;
  Register(engine1, &src, handle1);

  int32_t dst = 2;
  MemHandle handle2 = nullptr;
  Register(engine2, &dst, handle2);

  EXPECT_EQ(engine1.Connect("127.0.0.1:16000", kTimeOut), SUCCESS);
  TransferOpDesc desc{reinterpret_cast<uintptr_t>(&src), reinterpret_cast<uintptr_t>(&dst), sizeof(int32_t)};
  TransferReq req = nullptr;

  ASSERT_EQ(engine1.TransferAsync("127.0.0.1:16000", READ, {desc}, {}, req), SUCCESS);

  TransferStatus status = TransferStatus::WAITING;
  for (int32_t i = 0; i < kMaxRetryCount && status == TransferStatus::WAITING; i++) {
    engine1.GetTransferStatus(req, status);
    std::this_thread::sleep_for(std::chrono::milliseconds(kInterval));
  }
  EXPECT_EQ(status, TransferStatus::COMPLETED);
  EXPECT_EQ(src, 2);
  EXPECT_EQ(engine1.GetTransferStatus(req, status), PARAM_INVALID);
  EXPECT_EQ(status, TransferStatus::FAILED);

  src = 1;
  ASSERT_EQ(engine1.TransferAsync("127.0.0.1:16000", WRITE, {desc}, {}, req), SUCCESS);
  status = TransferStatus::WAITING;
  for (int32_t i = 0; i < kMaxRetryCount && status == TransferStatus::WAITING; i++) {
    engine1.GetTransferStatus(req, status);
    std::this_thread::sleep_for(std::chrono::milliseconds(kInterval));
  }
  EXPECT_EQ(status, TransferStatus::COMPLETED);
  EXPECT_EQ(dst, 1);
  EXPECT_EQ(engine1.GetTransferStatus(req, status), PARAM_INVALID);
  EXPECT_EQ(status, TransferStatus::FAILED);

  EXPECT_EQ(engine1.Disconnect("127.0.0.1:16000", kTimeOut), SUCCESS);
  EXPECT_EQ(engine1.DeregisterMem(handle1), SUCCESS);
  EXPECT_EQ(engine2.DeregisterMem(handle2), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(HixlEngineTest, TestInitFailed) {
  SetSocStub("Ascend910B1", 0, 12, 99, 88);
  // invalid ip
  std::string local_engine = "ad.0.0.1:26000";
  HixlEngine engine(AscendString(local_engine.c_str()));
  EXPECT_EQ(engine.Initialize(options1), PARAM_INVALID);
  engine.Finalize();
}

TEST_F(HixlEngineTest, TestNotListenFailed) {
  SetSocStub("Ascend910B1", 0, 12, 99, 88);
  std::string local_engine = "127.0.0.1:16000";
  HixlEngine engine(AscendString(local_engine.c_str()));
  EXPECT_EQ(engine.Initialize(options1), SUCCESS);
  // not listen
  EXPECT_EQ(engine.Connect("127.0.0.1:16001", kTimeOut), FAILED);
  engine.Finalize();
}

TEST_F(HixlEngineTest, TestAlreadyConnectedFailed) {
  SetSocStub("Ascend910B1", 0, 12, 99, 88);
  std::string local_engine1 = "127.0.0.1";
  HixlEngine engine1(AscendString(local_engine1.c_str()));
  EXPECT_EQ(engine1.Initialize(options1), SUCCESS);

  std::string local_engine2 = "127.0.0.1:16000";
  HixlEngine engine2(AscendString(local_engine2.c_str()));
  EXPECT_EQ(engine2.Initialize(options2), SUCCESS);
  EXPECT_EQ(engine1.Connect("127.0.0.1:16000", kTimeOut), SUCCESS);
  EXPECT_EQ(engine1.Connect("127.0.0.1:16000", kTimeOut), ALREADY_CONNECTED);
  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(HixlEngineTest, TestDeregisterUnregisteredMem) {
  SetSocStub("Ascend910B1", 0, 12, 99, 88);
  std::string local_engine = "127.0.0.1";
  HixlEngine engine(AscendString(local_engine.c_str()));
  EXPECT_EQ(engine.Initialize(options1), SUCCESS);
  MemHandle handle = (MemHandle)0x100;
  // deregister unregister mem
  EXPECT_EQ(engine.DeregisterMem(handle), SUCCESS);
  engine.Finalize();
}

TEST_F(HixlEngineTest, TestGetTransferStatusWithInterrupt) {
  SetSocStub("Ascend910B1", 0, 12, 99, 88);
  std::string local_engine1 = "127.0.0.1";
  HixlEngine engine1(AscendString(local_engine1.c_str()));

  std::string local_engine2 = "127.0.0.1:16000";
  HixlEngine engine2(AscendString(local_engine2.c_str()));

  EXPECT_EQ(engine1.Initialize(options1), SUCCESS);
  EXPECT_EQ(engine2.Initialize(options2), SUCCESS);

  int32_t src = 1;
  int32_t dst = 2;
  MemHandle handle1 = nullptr;
  MemHandle handle2 = nullptr;
  Register(engine1, &src, handle1);
  Register(engine2, &dst, handle2);

  EXPECT_EQ(engine1.Connect("127.0.0.1:16000", kTimeOut), SUCCESS);

  TransferOpDesc desc{reinterpret_cast<uintptr_t>(&src), reinterpret_cast<uintptr_t>(&dst), sizeof(int32_t)};
  TransferReq req = nullptr;
  EXPECT_EQ(engine1.TransferAsync("127.0.0.1:16000", WRITE, {desc}, {}, req), SUCCESS);
  engine1.Disconnect("127.0.0.1:16000", kTimeOut);
  TransferStatus status = TransferStatus::WAITING;
  EXPECT_EQ(engine1.GetTransferStatus(req, status), PARAM_INVALID);
  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(HixlEngineTest, TestSendAndGetNotifies) {
  SetSocStub("Ascend910B1", 0, 12, 99, 88);
  std::string local_engine1 = "127.0.0.1";
  HixlEngine engine1(AscendString(local_engine1.c_str()));
  EXPECT_EQ(engine1.Initialize(options1), SUCCESS);

  std::string local_engine2 = "127.0.0.1:16000";
  HixlEngine engine2(AscendString(local_engine2.c_str()));
  EXPECT_EQ(engine2.Initialize(options2), SUCCESS);

  EXPECT_EQ(engine1.Connect("127.0.0.1:16000", kTimeOut), SUCCESS);
  NotifyDesc notify;
  std::string notify_name = "test_notify";
  notify.name = AscendString(notify_name.c_str());
  std::string notify_msg = "message";
  notify.notify_msg = AscendString(notify_msg.c_str());
  EXPECT_EQ(engine1.SendNotify("127.0.0.1:16000", notify, kTimeOut), UNSUPPORTED);
  std::vector<NotifyDesc> notifies;
  EXPECT_EQ(engine2.GetNotifies(notifies), UNSUPPORTED);
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
  HixlEngine engine2("127.0.0.1:16000");
  InitializeAndConnectEngines(engine1, options1, engine2, options2);
  CleanupEngines(engine1, engine2);

  unsetenv("HCCL_RDMA_TC");
  unsetenv("HCCL_RDMA_SL");

  VerifyLogCapture(log_capture, {tc_log_pattern, sl_log_pattern, channel_desc_log_pattern});
}

TEST_F(HixlEngineTest, TestParseTcSlInvalidValue) {
  options1[hixl::OPTION_RDMA_TRAFFIC_CLASS] = "129";
  HixlEngine engine("127.0.0.1");
  EXPECT_EQ(engine.Initialize(options1), PARAM_INVALID);
  engine.Finalize();
  options1[hixl::OPTION_RDMA_TRAFFIC_CLASS] = "256";
  EXPECT_EQ(engine.Initialize(options1), PARAM_INVALID);
  engine.Finalize();
  options1[hixl::OPTION_RDMA_TRAFFIC_CLASS] = "invalid";
  EXPECT_EQ(engine.Initialize(options1), PARAM_INVALID);
  engine.Finalize();
  options2[hixl::OPTION_RDMA_SERVICE_LEVEL] = "8";
  EXPECT_EQ(engine.Initialize(options2), PARAM_INVALID);
  engine.Finalize();
}

TEST_F(HixlEngineTest, TestParseTcAndSlWithEnv) {
  std::string tc_log_pattern = "Set rdma traffic class to 128";
  std::string sl_log_pattern = "Set rdma service level to 5";
  std::string channel_desc_log_pattern = "[channel] ROCE attributes set, tc=128, sl=5";
  auto log_capture = SetupLogCapture({tc_log_pattern, sl_log_pattern, channel_desc_log_pattern});

  mmSetEnv("HCCL_RDMA_TC", "128", 1);
  mmSetEnv("HCCL_RDMA_SL", "5", 1);

  HixlEngine engine1("127.0.0.1");
  HixlEngine engine2("127.0.0.1:16000");
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
  HixlEngine engine2("127.0.0.1:16000");
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
  HixlEngine engine2("127.0.0.1:16000");
  InitializeAndConnectEngines(engine1, options1, engine2, options3);
  CleanupEngines(engine1, engine2);

  EXPECT_FALSE(log_capture->WaitForAllPatternsCaptured(kCaptureLogTimeoutMs));
  llm::SlogStub::SetInstance(nullptr);
}

TEST_F(HixlEngineTest, TestInitializeFillDeviceInfoForV2FullConfigured) {
  SetSocStub("Ascend910B1", 0, 12, 99, 88);

  const std::string local_comm_res = BuildLocalCommRes(
      "sp_v2",
      "1.3",
      {
          BuildDeviceRoceEndpoint("127.0.0.1"),
          BuildDeviceHccsEndpoint("5")
      });

  HixlEngine engine("127.0.0.1");
  auto options = BuildOptions(local_comm_res);
  EXPECT_EQ(engine.Initialize(options), SUCCESS);
  ASSERT_EQ(engine.endpoint_list_.size(), 2U);

  for (const auto &ep : engine.endpoint_list_) {
    EXPECT_EQ(ep.placement, kPlacementDevice);
    EXPECT_EQ(ep.device_info.phy_device_id, 12);
    EXPECT_EQ(ep.device_info.super_device_id, -1);
    EXPECT_EQ(ep.device_info.super_pod_id, -1);
  }

  engine.Finalize();
}

TEST_F(HixlEngineTest, TestInitializeFillDeviceInfoForV3FullConfigured) {
  SetSocStub("Ascend910_9391", 1, 23, 45, 67);

  const std::string local_comm_res = BuildLocalCommRes(
      "sp_v3",
      "1.3",
      {
          BuildDeviceRoceEndpoint("127.0.0.1"),
          BuildDeviceHccsEndpoint("7")
      });

  HixlEngine engine("127.0.0.1");
  auto options = BuildOptions(local_comm_res);
  EXPECT_EQ(engine.Initialize(options), SUCCESS);
  ASSERT_EQ(engine.endpoint_list_.size(), 2U);

  for (const auto &ep : engine.endpoint_list_) {
    EXPECT_EQ(ep.placement, kPlacementDevice);
    EXPECT_EQ(ep.device_info.phy_device_id, 23);
    EXPECT_EQ(ep.device_info.super_device_id, 45);
    EXPECT_EQ(ep.device_info.super_pod_id, 67);
  }

  engine.Finalize();
}

TEST_F(HixlEngineTest, TestInitializeParsesConfiguredEndpointListRegardlessOfVersion) {
  SetSocStub("Ascend910_9391", 1, 23, 45, 67);

  const std::string local_comm_res = BuildLocalCommRes(
      "sp_old",
      "1.2",
      {
          BuildDeviceRoceEndpoint("127.0.0.1")
      });

  HixlEngine engine("127.0.0.1");
  auto options = BuildOptions(local_comm_res);
  EXPECT_EQ(engine.Initialize(options), SUCCESS);
  ASSERT_EQ(engine.endpoint_list_.size(), 1U);
  EXPECT_EQ(engine.endpoint_list_[0].protocol, kProtocolRoce);
  EXPECT_EQ(engine.endpoint_list_[0].placement, kPlacementDevice);
  EXPECT_EQ(engine.endpoint_list_[0].net_instance_id, "sp_old");
  EXPECT_EQ(engine.endpoint_list_[0].device_info.phy_device_id, 23);
  EXPECT_EQ(engine.endpoint_list_[0].device_info.super_device_id, 45);
  EXPECT_EQ(engine.endpoint_list_[0].device_info.super_pod_id, 67);

  engine.Finalize();
}

TEST_F(HixlEngineTest, TestInitializeFillDeviceInfoOnlyForDevicePlacement) {
  SetSocStub("Ascend910_9391", 1, 23, 45, 67);

  const std::string local_comm_res = BuildLocalCommRes(
      "sp_mix",
      "1.3",
      {
          BuildHostRoceEndpoint("127.0.0.1"),
          BuildDeviceHccsEndpoint("7")
      });

  HixlEngine engine("127.0.0.1");
  auto options = BuildOptions(local_comm_res);
  EXPECT_EQ(engine.Initialize(options), SUCCESS);
  ASSERT_EQ(engine.endpoint_list_.size(), 2U);

  const auto &host_ep = engine.endpoint_list_[0];
  EXPECT_EQ(host_ep.placement, kPlacementHost);
  EXPECT_EQ(host_ep.device_info.phy_device_id, -1);
  EXPECT_EQ(host_ep.device_info.super_device_id, -1);
  EXPECT_EQ(host_ep.device_info.super_pod_id, -1);

  const auto &device_ep = engine.endpoint_list_[1];
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
  auto options = BuildOptions(BuildVersionOnlyLocalCommRes("1.3"));
  EXPECT_EQ(engine.Initialize(options), SUCCESS);
  ASSERT_EQ(engine.endpoint_list_.size(), 2U);

  const auto &roce_ep = engine.endpoint_list_[0];
  EXPECT_EQ(roce_ep.protocol, kProtocolRoce);
  EXPECT_EQ(roce_ep.comm_id, "10.10.10.12");
  EXPECT_EQ(roce_ep.net_instance_id, "127.0.0.1");
  EXPECT_EQ(roce_ep.device_info.phy_device_id, 12);
  EXPECT_EQ(roce_ep.device_info.super_device_id, -1);
  EXPECT_EQ(roce_ep.device_info.super_pod_id, -1);

  const auto &hccs_ep = engine.endpoint_list_[1];
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
  auto options = BuildOptions(BuildVersionOnlyLocalCommRes("1.3"));
  EXPECT_EQ(engine.Initialize(options), SUCCESS);
  ASSERT_EQ(engine.endpoint_list_.size(), 2U);

  const auto &roce_ep = engine.endpoint_list_[0];
  EXPECT_EQ(roce_ep.protocol, kProtocolRoce);
  EXPECT_EQ(roce_ep.comm_id, "10.10.10.23");
  EXPECT_EQ(roce_ep.net_instance_id, "67");
  EXPECT_EQ(roce_ep.device_info.phy_device_id, 23);
  EXPECT_EQ(roce_ep.device_info.super_device_id, 45);
  EXPECT_EQ(roce_ep.device_info.super_pod_id, 67);

  const auto &hccs_ep = engine.endpoint_list_[1];
  EXPECT_EQ(hccs_ep.protocol, kProtocolHccs);
  EXPECT_EQ(hccs_ep.comm_id, "23");
  EXPECT_EQ(hccs_ep.net_instance_id, "67");
  EXPECT_EQ(hccs_ep.device_info.phy_device_id, 23);
  EXPECT_EQ(hccs_ep.device_info.super_device_id, 45);
  EXPECT_EQ(hccs_ep.device_info.super_pod_id, 67);

  engine.Finalize();
}
}  // namespace hixl
