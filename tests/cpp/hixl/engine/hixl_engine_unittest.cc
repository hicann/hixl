/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <thread>
#include <gtest/gtest.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "engine/hixl_engine.h"
#include "hixl/hixl_types.h"
#include "adxl/adxl_types.h"
#include "cs/hixl_cs_client.h"
#include "hixl/hixl.h"

namespace hixl {

namespace {
constexpr const int32_t kTimeOut = 1000;
constexpr const int32_t kMaxRetryCount = 10;
constexpr const int32_t kInterval = 10;
}

class HixlEngineTest : public ::testing::Test {
 protected:
  std::map<AscendString, AscendString> options1;
  std::map<AscendString, AscendString> options2;
  void SetUp() override {
    options1[adxl::OPTION_LOCAL_COMM_RES] = R"(
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

    options2[adxl::OPTION_LOCAL_COMM_RES] = R"(
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

  void TearDown() override {}

  void Register(HixlEngine &engine, int32_t *ptr, MemHandle &handle) {
    MemDesc mem{};
    mem.addr = reinterpret_cast<uintptr_t>(ptr);
    mem.len = sizeof(int32_t);
    EXPECT_EQ(engine.RegisterMem(mem, MEM_DEVICE, handle), SUCCESS);
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
};

TEST_F(HixlEngineTest, TestHixl) {
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
  // invalid ip
  std::string local_engine = "ad.0.0.1:26000";
  HixlEngine engine(AscendString(local_engine.c_str()));
  EXPECT_EQ(engine.Initialize(options1), PARAM_INVALID);
  engine.Finalize();
}

TEST_F(HixlEngineTest, TestNotListenFailed) {
  std::string local_engine = "127.0.0.1:16000";
  HixlEngine engine(AscendString(local_engine.c_str()));
  EXPECT_EQ(engine.Initialize(options1), SUCCESS);
  // not listen
  EXPECT_EQ(engine.Connect("127.0.0.1:16001", kTimeOut), FAILED);
  engine.Finalize();
}

TEST_F(HixlEngineTest, TestAlreadyConnectedFailed) {
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
  std::string local_engine = "127.0.0.1";
  HixlEngine engine(AscendString(local_engine.c_str()));
  EXPECT_EQ(engine.Initialize(options1), SUCCESS);
  MemHandle handle = (MemHandle)0x100;
  // deregister unregister mem
  EXPECT_EQ(engine.DeregisterMem(handle), SUCCESS);
  engine.Finalize();
}

TEST_F(HixlEngineTest, TestGetTransferStatusWithInterrupt) {
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
}  // namespace hixl
