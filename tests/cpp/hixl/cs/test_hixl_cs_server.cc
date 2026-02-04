/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <vector>
#include <cstdlib>
#include <thread>
#include <chrono>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "common/hixl_cs.h"
#include "hixl/hixl_types.h"
#include "common/ctrl_msg.h"
#include "common/ctrl_msg_plugin.h"
#include "dlog_pub.h"

using namespace std;
using namespace ::testing;
using ::testing::Invoke;
using ::testing::Mock;

namespace hixl {
static constexpr uint32_t kPort = 16000;
static constexpr uint32_t kEpAddrId0 = 1U;
static constexpr uint32_t kEpAddrId1 = 2U;
static constexpr uint32_t kEpAddrId2 = 3U;
static constexpr uint32_t kMemNum = 100U;
static constexpr uint32_t kBackLog = 1024U;
static constexpr uint32_t kRecvTimeoutMs = 1000U;
static constexpr uint32_t kTimeSleepMs = 10U;
static constexpr int32_t kNUm1 = 1;
static constexpr int32_t kNUm2 = 2;
static std::vector<int32_t> kHostMems(kMemNum, kNUm1);
static std::vector<int32_t> kDeviceMems(kMemNum, kNUm2);

static constexpr int32_t kCtrlMsgType = 1024;
class HixlCSTest : public ::testing::Test {
 protected:
  // 在测试类中设置一些准备工作，如果需要的话
  void SetUp() override {
    EndpointDesc ep0{};
    ep0.loc.locType = ENDPOINT_LOC_TYPE_HOST;
    ep0.protocol = COMM_PROTOCOL_UBC_CTP;
    ep0.commAddr.type = COMM_ADDR_TYPE_ID;
    ep0.commAddr.id = kEpAddrId0;
    EndpointDesc ep1{};
    ep1.loc.locType = ENDPOINT_LOC_TYPE_HOST;
    ep1.protocol = COMM_PROTOCOL_UBC_CTP;
    ep1.commAddr.type = COMM_ADDR_TYPE_ID;
    ep1.commAddr.id = kEpAddrId1;
    EndpointDesc ep_dev{};
    ep_dev.loc.locType = ENDPOINT_LOC_TYPE_DEVICE;
    ep_dev.protocol = COMM_PROTOCOL_UBC_TP;
    ep_dev.commAddr.type = COMM_ADDR_TYPE_ID;
    ep_dev.commAddr.id = kEpAddrId2;

    default_eps.emplace_back(ep0);
    default_eps.emplace_back(ep1);
    default_eps.emplace_back(ep_dev);
  }
  // 在测试类中进行清理工作，如果需要的话
  void TearDown() override {
  }

 private:
  std::vector<EndpointDesc> default_eps;

  void SendCreateChannelReq(int32_t client_fd) {
    CtrlMsgHeader header{};
    header.magic = kMagicNumber;
    header.body_size = static_cast<uint64_t>(sizeof(CtrlMsgType) + sizeof(CreateChannelReq));
    CtrlMsgType msg_type = CtrlMsgType::kCreateChannelReq;
    CreateChannelReq body{};
    body.src = default_eps[0];
    body.dst = default_eps[1];
    auto ret = CtrlMsgPlugin::Send(client_fd, &header, static_cast<uint64_t>(sizeof(header)));
    EXPECT_EQ(ret, SUCCESS);
    ret = CtrlMsgPlugin::Send(client_fd, &msg_type, static_cast<uint64_t>(sizeof(msg_type)));
    EXPECT_EQ(ret, SUCCESS);
    ret = CtrlMsgPlugin::Send(client_fd, &body, static_cast<uint64_t>(sizeof(body)));
    EXPECT_EQ(ret, SUCCESS);
  }

  void GetCreateChannelResp(int32_t client_fd, CreateChannelResp &resp_body) {
    CtrlMsgHeader recv_header{};
    const uint64_t expect_body_size = static_cast<uint64_t>(sizeof(CtrlMsgType) + sizeof(CreateChannelResp));
    recv_header.body_size = expect_body_size;
    auto ret = CtrlMsgPlugin::Recv(client_fd, &recv_header, static_cast<uint64_t>(sizeof(recv_header)), kRecvTimeoutMs);
    EXPECT_EQ(ret, SUCCESS);
    EXPECT_EQ(recv_header.magic, kMagicNumber);
    EXPECT_EQ(recv_header.body_size, expect_body_size);
    CtrlMsgType resp_type{};
    ret = CtrlMsgPlugin::Recv(client_fd, &resp_type, static_cast<uint64_t>(sizeof(resp_type)), kRecvTimeoutMs);
    EXPECT_EQ(ret, SUCCESS);
    EXPECT_EQ(resp_type, CtrlMsgType::kCreateChannelResp);
    ret = CtrlMsgPlugin::Recv(client_fd, &resp_body, static_cast<uint64_t>(sizeof(resp_body)), kRecvTimeoutMs);
    EXPECT_EQ(ret, SUCCESS);
    EXPECT_EQ(resp_body.result, SUCCESS);
  }

  void SendGetRemoteMemReq(int32_t client_fd, const CreateChannelResp &resp_body) {
    CtrlMsgHeader header{};
    header.magic = kMagicNumber;
    header.body_size = static_cast<uint64_t>(sizeof(CtrlMsgType) + sizeof(GetRemoteMemReq));
    CtrlMsgType msg_type = CtrlMsgType::kGetRemoteMemReq;
    GetRemoteMemReq body{};
    body.dst_ep_handle = resp_body.dst_ep_handle;
    auto ret = CtrlMsgPlugin::Send(client_fd, &header, static_cast<uint64_t>(sizeof(header)));
    EXPECT_EQ(ret, SUCCESS);
    ret = CtrlMsgPlugin::Send(client_fd, &msg_type, static_cast<uint64_t>(sizeof(msg_type)));
    EXPECT_EQ(ret, SUCCESS);
    ret = CtrlMsgPlugin::Send(client_fd, &body, static_cast<uint64_t>(sizeof(body)));
    EXPECT_EQ(ret, SUCCESS);
  }
};

TEST_F(HixlCSTest, TestHixlCSServer) {
  HixlServerConfig config{};
  HixlServerHandle server_handle = nullptr;
  HixlServerDesc desc{};
  desc.server_ip = "127.0.0.1";
  desc.server_port = kPort;
  desc.endpoint_list = &default_eps[0];
  desc.endpoint_list_num = default_eps.size();
  auto ret = HixlCSServerCreate(&desc, &config, &server_handle);
  EXPECT_EQ(ret, SUCCESS);
  auto proc = [](int32_t fd, const char *msg, uint64_t msg_len) -> Status {
    (void) fd;
    (void) msg;
    (void) msg_len;
    return 0;
  };
  ret = HixlCSServerRegProc(server_handle, static_cast<CtrlMsgType>(kCtrlMsgType), proc);
  EXPECT_EQ(ret, SUCCESS);
  HcommMem mem{};
  mem.size = sizeof(int32_t);
  mem.addr = &kDeviceMems[0];
  MemHandle mem_handle = nullptr;
  ret = HixlCSServerRegMem(server_handle, nullptr, &mem, &mem_handle);
  EXPECT_EQ(ret, SUCCESS);
  HcommMem mem2{};
  mem2.size = sizeof(int32_t);
  mem2.addr = &kHostMems[0];
  MemHandle mem_handle2 = nullptr;
  ret = HixlCSServerRegMem(server_handle, "a", &mem2, &mem_handle2);
  EXPECT_EQ(ret, SUCCESS);
  ret = HixlCSServerListen(server_handle, kBackLog);
  EXPECT_EQ(ret, SUCCESS);
  ret = HixlCSServerUnregMem(server_handle, mem_handle);
  EXPECT_EQ(ret, SUCCESS);
  ret = HixlCSServerUnregMem(server_handle, mem_handle2);
  EXPECT_EQ(ret, SUCCESS);
  ret = HixlCSServerDestroy(server_handle);
  EXPECT_EQ(ret, SUCCESS);
}

TEST_F(HixlCSTest, TestHixlCSClient2Server) {
  HixlServerConfig config{};
  HixlServerHandle server_handle = nullptr;
  HixlServerDesc desc{};
  desc.server_ip = "127.0.0.1";
  desc.server_port = kPort;
  desc.endpoint_list = &default_eps[0];
  desc.endpoint_list_num = default_eps.size();
  auto ret = HixlCSServerCreate(&desc, &config, &server_handle);
  EXPECT_EQ(ret, SUCCESS);
  HcommMem mem{};
  mem.size = sizeof(int32_t);
  mem.addr = &kDeviceMems[0];
  MemHandle mem_handle = nullptr;
  ret = HixlCSServerRegMem(server_handle, nullptr, &mem, &mem_handle);
  EXPECT_EQ(ret, SUCCESS);
  ret = HixlCSServerListen(server_handle, kBackLog);
  EXPECT_EQ(ret, SUCCESS);

  int32_t client_fd = -1;
  ret = CtrlMsgPlugin::Connect("127.0.0.1", kPort, client_fd, 1);
  EXPECT_EQ(ret, SUCCESS);
  SendCreateChannelReq(client_fd);
  CreateChannelResp resp_body{};
  GetCreateChannelResp(client_fd, resp_body);
  SendGetRemoteMemReq(client_fd, resp_body);
  std::this_thread::sleep_for(std::chrono::milliseconds(kTimeSleepMs));
  // 没有读取缓冲区数据，测试server recv报错场景
  (void) close(client_fd);

  ret = HixlCSServerUnregMem(server_handle, mem_handle);
  EXPECT_EQ(ret, SUCCESS);
  ret = HixlCSServerDestroy(server_handle);
  EXPECT_EQ(ret, SUCCESS);
}

}  // namespace hixl
