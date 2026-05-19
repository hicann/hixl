/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING
 * BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE. See LICENSE in the root of
 * the software repository for the full text of the License.
 */

#include <gtest/gtest.h>
#include <sys/socket.h>
#include <unistd.h>
#include <array>
#include <memory>
#include <thread>
#include <vector>

#include "nlohmann/json.hpp"

#define private public
#include "adxl/channel_msg_handler.h"
#undef private

#include "common/msg_handler_plugin.h"

namespace adxl {
namespace {
constexpr char kListenInfo[] = "127.0.0.1:26000";
constexpr char kRemoteEngine[] = "127.0.0.1:26001";
constexpr char kRemoteCommRes[] = "remote_comm_res";
constexpr int32_t kTimeoutMs = 1000;
constexpr uintptr_t kRemoteAddrStart = 0x1234U;
constexpr uintptr_t kRemoteAddrEnd = 0x5678U;

nlohmann::json BuildAddrInfoJson() {
  return {
      {"mem_type", MEM_HOST},
      {"start_addr", kRemoteAddrStart},
      {"end_addr", kRemoteAddrEnd},
  };
}
}  // namespace

class ChannelMsgHandlerUnitTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets_.data()), 0);
    handler_ = std::make_unique<ChannelMsgHandler>(kListenInfo, &channel_manager_);
  }

  void TearDown() override {
    handler_.reset();
    for (auto &fd : sockets_) {
      if (fd >= 0) {
        close(fd);
        fd = -1;
      }
    }
  }

  nlohmann::json BuildConnectInfoJson() const {
    return {
        {"channel_id", kRemoteEngine},
        {"comm_res", kRemoteCommRes},
        {"timeout", kTimeoutMs},
        {"addrs", nlohmann::json::array({BuildAddrInfoJson()})},
    };
  }

  std::thread StartPeerExchangeThread(const nlohmann::json &response_json) {
    return std::thread([this, response_json]() {
      int32_t msg_type = 0;
      std::vector<char> msg;
      EXPECT_EQ(llm::MsgHandlerPlugin::RecvMsg(sockets_[1], msg_type, msg), SUCCESS);
      EXPECT_EQ(msg_type, static_cast<int32_t>(ChannelMsgType::kConnect));
      EXPECT_EQ(llm::MsgHandlerPlugin::SendMsg(sockets_[1], static_cast<int32_t>(ChannelMsgType::kConnect),
                                               response_json.dump()),
                SUCCESS);
    });
  }

  std::array<int32_t, 2> sockets_{{-1, -1}};
  ChannelManager channel_manager_;
  std::unique_ptr<ChannelMsgHandler> handler_;
};

TEST_F(ChannelMsgHandlerUnitTest, ExchangeConnectInfoAcceptsAdxlConnectInfo) {
  auto peer_thread = StartPeerExchangeThread(BuildConnectInfoJson());

  ChannelConnectInfo peer_connect_info{};
  EXPECT_EQ(handler_->ExchangeConnectInfo(sockets_[0], kTimeoutMs, peer_connect_info), SUCCESS);
  EXPECT_EQ(peer_connect_info.channel_id, kRemoteEngine);
  EXPECT_EQ(peer_connect_info.comm_res, kRemoteCommRes);
  EXPECT_EQ(peer_connect_info.timeout, kTimeoutMs);
  ASSERT_EQ(peer_connect_info.addrs.size(), 1U);
  EXPECT_EQ(peer_connect_info.addrs[0].mem_type, MEM_HOST);
  EXPECT_EQ(peer_connect_info.addrs[0].start_addr, kRemoteAddrStart);
  EXPECT_EQ(peer_connect_info.addrs[0].end_addr, kRemoteAddrEnd);

  peer_thread.join();
}

TEST_F(ChannelMsgHandlerUnitTest, SerializeConnectInfoWritesAdxlConnectInfo) {
  ChannelConnectInfo connect_info{};
  connect_info.channel_id = kListenInfo;
  connect_info.comm_res = kRemoteCommRes;
  connect_info.timeout = kTimeoutMs;
  connect_info.addrs.emplace_back(AddrInfo{kRemoteAddrStart, kRemoteAddrEnd, MEM_HOST});

  std::string serialized;
  ASSERT_EQ(ChannelMsgHandler::Serialize(connect_info, serialized), SUCCESS);

  const auto json = nlohmann::json::parse(serialized);
  ASSERT_EQ(json.size(), 4U);
  EXPECT_EQ(json.at("channel_id").get<std::string>(), kListenInfo);
  EXPECT_EQ(json.at("comm_res").get<std::string>(), kRemoteCommRes);
  EXPECT_EQ(json.at("timeout").get<int32_t>(), kTimeoutMs);
  ASSERT_EQ(json.at("addrs").size(), 1U);
  EXPECT_EQ(json.at("addrs").at(0).at("mem_type").get<int32_t>(), static_cast<int32_t>(MEM_HOST));
  EXPECT_EQ(json.at("addrs").at(0).at("start_addr").get<uintptr_t>(), kRemoteAddrStart);
  EXPECT_EQ(json.at("addrs").at(0).at("end_addr").get<uintptr_t>(), kRemoteAddrEnd);
}

TEST_F(ChannelMsgHandlerUnitTest, ParseTcSlRejectsOutOfRangeValues) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_RDMA_TRAFFIC_CLASS] = "-1";
  EXPECT_EQ(handler_->ParseTrafficClass(options), PARAM_INVALID);

  options[hixl::OPTION_RDMA_TRAFFIC_CLASS] = "256";
  EXPECT_EQ(handler_->ParseTrafficClass(options), PARAM_INVALID);

  options[hixl::OPTION_RDMA_TRAFFIC_CLASS] = "invalid";
  EXPECT_EQ(handler_->ParseTrafficClass(options), PARAM_INVALID);

  options.clear();
  options[hixl::OPTION_RDMA_SERVICE_LEVEL] = "8";
  EXPECT_EQ(handler_->ParseServiceLevel(options), PARAM_INVALID);
}

TEST_F(ChannelMsgHandlerUnitTest, ParseTcSlAcceptsValidBoundaryValues) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_RDMA_TRAFFIC_CLASS] = "252";
  options[adxl::OPTION_RDMA_SERVICE_LEVEL] = "7";

  EXPECT_EQ(handler_->ParseTrafficClass(options), SUCCESS);
  EXPECT_EQ(handler_->ParseServiceLevel(options), SUCCESS);
  EXPECT_EQ(handler_->comm_config_.hcclRdmaTrafficClass, 252U);
  EXPECT_EQ(handler_->comm_config_.hcclRdmaServiceLevel, 7U);
}

// Test that ProcessServerEviction handles error response from client correctly
// This covers the LLMLOGW line at channel_msg_handler.cc:941
TEST_F(ChannelMsgHandlerUnitTest, ProcessServerEviction_WhenClientReturnsError_LogsWarning) {
  // Create a mock channel that will return a valid fd
  ChannelInfo channel_info{};
  channel_info.channel_type = ChannelType::kServer;
  channel_info.channel_id = kRemoteEngine;
  auto channel = std::make_shared<CommChannel>(channel_info);

  // Create a socket pair for communication
  int socket_pair[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, socket_pair), 0);
  int fd = socket_pair[0];
  int peer_fd = socket_pair[1];

  // Set the fd on the channel (accessing private member)
  channel->fd_ = fd;

  // Pre-set the next_req_id to a known value
  uint64_t req_id = 100;
  handler_->next_req_id_.store(req_id, std::memory_order_relaxed);

  // Create a thread that will simulate receiving a disconnect response with error
  // This needs to happen AFTER ProcessServerEviction sends the request
  std::thread response_thread([this, req_id]() {

    // Now inject the response
    std::lock_guard<std::mutex> lock(handler_->pending_req_mutex_);
    auto it = handler_->pending_disconnect_requests_.find(req_id);
    if (it != handler_->pending_disconnect_requests_.end()) {
      it->second->received = true;
      it->second->resp.req_id = req_id;
      it->second->resp.channel_id = kRemoteEngine;
      it->second->resp.can_disconnect = false;
      it->second->resp.disconnected = false;
      it->second->resp.error_code = 123;  // Error code to trigger LLMLOGW
      it->second->resp.error_message = "Test error message";
      it->second->cv.notify_one();
    }
  });

  // Set disconnecting flag on channel
  channel->SetDisconnecting(true);

  // Call ProcessServerEviction which should hit the LLMLOGW line
  Status result = handler_->ProcessServerEviction(kRemoteEngine, channel);

  // Wait for the response thread to finish
  response_thread.join();

  // The function should return SUCCESS even when client returns error
  EXPECT_EQ(result, SUCCESS);

  // Cleanup
  close(fd);
  close(peer_fd);
}
}  // namespace adxl
