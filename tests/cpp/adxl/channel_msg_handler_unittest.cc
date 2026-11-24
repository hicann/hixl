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
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <array>
#include <chrono>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "nlohmann/json.hpp"

#include "depends/ctrl_msg_test_helper.h"

#define private public
#include "adxl/channel_msg_handler.h"
#undef private

#include "common/msg_handler_plugin.h"
#include "common/scope_guard.h"
#include "adxl/statistic_manager.h"

namespace adxl {
namespace {
constexpr char kListenInfo[] = "127.0.0.1:28100";
constexpr char kRemoteEngine[] = "127.0.0.1:28101";
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

std::string BuildDisconnectJson() {
  return std::string(R"({"channel_id":")") + kRemoteEngine + R"("})";
}
}  // namespace

class ChannelMsgHandlerUnitTest : public hixl::test::PlainTlsTest {
 protected:
  void SetUp() override {
    ASSERT_NO_FATAL_FAILURE(hixl::test::PlainTlsTest::SetUp());
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
      EXPECT_EQ(llm::MsgHandlerPlugin::RecvMsg(hixl::test::Connection(sockets_[1]), msg_type, msg, kTimeoutMs),
                SUCCESS);
      EXPECT_EQ(msg_type, static_cast<int32_t>(ChannelMsgType::kConnect));
      EXPECT_EQ(llm::MsgHandlerPlugin::SendMsg(hixl::test::Connection(sockets_[1]),
                                               static_cast<int32_t>(ChannelMsgType::kConnect), response_json.dump()),
                SUCCESS);
    });
  }

  void BindUnavailablePeer(int &fd, std::string &remote_engine) const {
    fd = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(fd, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ASSERT_EQ(bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)), 0);
    socklen_t size = sizeof(address);
    ASSERT_EQ(getsockname(fd, reinterpret_cast<sockaddr *>(&address), &size), 0);
    remote_engine = "127.0.0.1:" + std::to_string(ntohs(address.sin_port));
  }

  std::array<int32_t, 2> sockets_{{-1, -1}};
  ChannelManager channel_manager_;
  std::unique_ptr<ChannelMsgHandler> handler_;
};

TEST_F(ChannelMsgHandlerUnitTest, DisconnectCallbackUsesReceivingChannelWithoutLookup) {
  ASSERT_EQ(handler_->SetupChannelManagerCallbacks(), SUCCESS);

  ChannelInfo channel_info{};
  channel_info.channel_type = ChannelType::kClient;
  channel_info.channel_id = kRemoteEngine;
  auto channel = std::make_shared<CommChannel>(channel_info);

  RequestDisconnectMsg req_msg{};
  req_msg.channel_id = kRemoteEngine;
  req_msg.req_id = 1U;
  req_msg.timeout = kTimeoutMs;
  std::string msg_str;
  ASSERT_EQ(ControlMsgHandler::Serialize(req_msg, msg_str), SUCCESS);

  EXPECT_EQ(channel_manager_.HandleRequestDisconnectMessage(channel, msg_str), SUCCESS);
  EXPECT_TRUE(channel->IsDisconnecting());
  EXPECT_EQ(handler_->evict_queue_.size(), 1U);

  channel_info.channel_type = ChannelType::kServer;
  auto server_channel = std::make_shared<CommChannel>(channel_info);
  EXPECT_EQ(channel_manager_.HandleRequestDisconnectMessage(server_channel, msg_str), SUCCESS);
  EXPECT_FALSE(server_channel->IsDisconnecting());
  EXPECT_EQ(handler_->evict_queue_.size(), 1U);
}

TEST_F(ChannelMsgHandlerUnitTest, EvictionLoopProcessesPendingBatch) {
  ChannelInfo channel_info{};
  channel_info.channel_type = ChannelType::kClient;
  channel_info.channel_id = kRemoteEngine;
  auto channel = std::make_shared<CommChannel>(channel_info);
  channel->IncrementTransferCount();
  channel->SetDisconnecting(true);
  channel_manager_.channels_[{ChannelType::kClient, kRemoteEngine}] = channel;

  handler_->stop_eviction_.store(false);
  handler_->evict_queue_.push(EvictItem{kRemoteEngine, ChannelType::kClient, kTimeoutMs});
  std::thread eviction_thread([this]() { handler_->EvictionLoop(); });

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
  while (channel->IsDisconnecting() && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  EXPECT_FALSE(channel->IsDisconnecting());

  handler_->stop_eviction_.store(true);
  handler_->evict_cv_.notify_all();
  eviction_thread.join();
  channel_manager_.channels_.clear();
}

TEST_F(ChannelMsgHandlerUnitTest, ConnectToPeerFailureClearsPreviousConnection) {
  int fd = -1;
  HIXL_MAKE_GUARD(close_fd, ([&fd]() { (void)close(fd); }));
  std::string remote_engine;
  ASSERT_NO_FATAL_FAILURE(BindUnavailablePeer(fd, remote_engine));
  auto connection = hixl::test::Connection(sockets_[0]);
  ASSERT_NE(connection, nullptr);
  EXPECT_EQ(handler_->ConnectToPeer(remote_engine, kTimeoutMs, connection), FAILED);
  EXPECT_EQ(connection, nullptr);
}

TEST_F(ChannelMsgHandlerUnitTest, ConnectToPeerPublishesInitializedConnection) {
  int fd = -1;
  HIXL_MAKE_GUARD(close_fd, ([&fd]() { (void)close(fd); }));
  std::string remote_engine;
  ASSERT_NO_FATAL_FAILURE(BindUnavailablePeer(fd, remote_engine));
  ASSERT_EQ(listen(fd, 1), 0);
  HIXL_MAKE_GUARD(remove_statistics, ([&remote_engine]() {
                    StatisticManager::GetInstance().RemoveStatisticChannel(remote_engine, true);
                  }));
  hixl::CtrlMsgPluginPtr connection;
  ASSERT_EQ(handler_->ConnectToPeer(remote_engine, kTimeoutMs, connection), SUCCESS);
  ASSERT_NE(connection, nullptr);
  EXPECT_TRUE(connection->IsUsable());
}

TEST_F(ChannelMsgHandlerUnitTest, PrepareDisconnectAllowsUnavailablePeerForLocalCleanup) {
  int fd = -1;
  HIXL_MAKE_GUARD(close_fd, ([&fd]() { (void)close(fd); }));
  std::string remote_engine;
  ASSERT_NO_FATAL_FAILURE(BindUnavailablePeer(fd, remote_engine));
  ChannelInfo info{};
  info.channel_type = ChannelType::kClient;
  info.channel_id = remote_engine;
  channel_manager_.channels_[{ChannelType::kClient, remote_engine}] = std::make_shared<CommChannel>(info);
  HIXL_MAKE_GUARD(clear_channels, ([this]() { channel_manager_.channels_.clear(); }));
  auto connection = hixl::test::Connection(sockets_[0]);
  EXPECT_EQ(handler_->PrepareDisconnect(remote_engine, kTimeoutMs, connection), SUCCESS);
  EXPECT_EQ(connection, nullptr);
  Status send_status = FAILED;
  handler_->SendDisconnectRequest(connection, send_status);
  EXPECT_EQ(handler_->ValidateDisconnectResponse(connection, send_status, kTimeoutMs), SUCCESS);
}

TEST_F(ChannelMsgHandlerUnitTest, PrepareDisconnectPublishesConnectionWhenPeerIsAvailable) {
  int fd = -1;
  HIXL_MAKE_GUARD(close_fd, ([&fd]() { (void)close(fd); }));
  std::string remote_engine;
  ASSERT_NO_FATAL_FAILURE(BindUnavailablePeer(fd, remote_engine));
  ASSERT_EQ(listen(fd, 1), 0);
  ChannelInfo info{};
  info.channel_type = ChannelType::kClient;
  info.channel_id = remote_engine;
  channel_manager_.channels_[{ChannelType::kClient, remote_engine}] = std::make_shared<CommChannel>(info);
  HIXL_MAKE_GUARD(clear_channels, ([this]() { channel_manager_.channels_.clear(); }));
  hixl::CtrlMsgPluginPtr connection;
  ASSERT_EQ(handler_->PrepareDisconnect(remote_engine, kTimeoutMs, connection), SUCCESS);
  ASSERT_NE(connection, nullptr);
  EXPECT_TRUE(connection->IsUsable());
}

TEST_F(ChannelMsgHandlerUnitTest, DisconnectCleansLocalResourcesAfterConnectionTimeout) {
  int fd = -1;
  HIXL_MAKE_GUARD(close_fd, ([&fd]() { (void)close(fd); }));
  std::string remote_engine;
  ASSERT_NO_FATAL_FAILURE(BindUnavailablePeer(fd, remote_engine));
  hixl::CtrlMsgPluginPtr connection;
  ASSERT_EQ(handler_->ConnectToPeer(remote_engine, 0, connection), TIMEOUT);
  ASSERT_EQ(connection, nullptr);

  ChannelInfo info{};
  info.channel_type = ChannelType::kClient;
  info.channel_id = remote_engine;
  auto channel = std::make_shared<CommChannel>(info);
  channel_manager_.channels_[{ChannelType::kClient, remote_engine}] = channel;
  SegmentTable segments;
  segments.AddRange(remote_engine, kRemoteAddrStart, kRemoteAddrEnd, MEM_HOST);
  handler_->segment_table_ = &segments;

  EXPECT_EQ(handler_->Disconnect(remote_engine, 0), SUCCESS);
  EXPECT_EQ(channel_manager_.GetChannel(ChannelType::kClient, remote_engine), nullptr);
  EXPECT_EQ(segments.FindSegment(remote_engine, kRemoteAddrStart, kRemoteAddrEnd), nullptr);
  EXPECT_TRUE(channel->IsFinalized());
}

TEST_F(ChannelMsgHandlerUnitTest, DisconnectPreservesLocalResourcesForInvalidAddress) {
  const std::string remote_engine = "invalid-engine";
  ChannelInfo info{};
  info.channel_type = ChannelType::kClient;
  info.channel_id = remote_engine;
  auto channel = std::make_shared<CommChannel>(info);
  channel_manager_.channels_[{ChannelType::kClient, remote_engine}] = channel;

  EXPECT_EQ(handler_->Disconnect(remote_engine, kTimeoutMs), PARAM_INVALID);
  EXPECT_EQ(channel_manager_.GetChannel(ChannelType::kClient, remote_engine), channel);
  EXPECT_FALSE(channel->IsFinalized());
  EXPECT_FALSE(channel->IsDisconnecting());
}

TEST(ChannelMsgHandlerLifecycleTest, InitializationFailureKeepsDisconnectCleanup) {
  ASSERT_FALSE(hixl::TlsManager::GetInstance().IsInitialized());
  ChannelManager channel_manager;
  ChannelMsgHandler handler(kListenInfo, &channel_manager);
  hixl::CtrlMsgPluginPtr connection;
  EXPECT_EQ(handler.ConnectToPeer(kRemoteEngine, kTimeoutMs, connection), FAILED);
  EXPECT_EQ(connection, nullptr);
  ChannelInfo info{};
  info.channel_type = ChannelType::kClient;
  info.channel_id = kRemoteEngine;
  channel_manager.channels_[{ChannelType::kClient, kRemoteEngine}] = std::make_shared<CommChannel>(info);
  HIXL_MAKE_GUARD(clear_channels, ([&channel_manager]() { channel_manager.channels_.clear(); }));
  EXPECT_EQ(handler.PrepareDisconnect(kRemoteEngine, kTimeoutMs, connection), SUCCESS);
  EXPECT_EQ(connection, nullptr);
  EXPECT_FALSE(hixl::TlsManager::GetInstance().IsInitialized());
}

TEST_F(ChannelMsgHandlerUnitTest, ExchangeConnectInfoAcceptsAdxlConnectInfo) {
  auto peer_thread = StartPeerExchangeThread(BuildConnectInfoJson());

  ChannelConnectInfo peer_connect_info{};
  EXPECT_EQ(handler_->ExchangeConnectInfo(hixl::test::Connection(sockets_[0]), kTimeoutMs, peer_connect_info), SUCCESS);
  EXPECT_EQ(peer_connect_info.channel_id, kRemoteEngine);
  EXPECT_EQ(peer_connect_info.comm_res, kRemoteCommRes);
  EXPECT_EQ(peer_connect_info.timeout, kTimeoutMs);
  ASSERT_EQ(peer_connect_info.addrs.size(), 1U);
  EXPECT_EQ(peer_connect_info.addrs[0].mem_type, MEM_HOST);
  EXPECT_EQ(peer_connect_info.addrs[0].start_addr, kRemoteAddrStart);
  EXPECT_EQ(peer_connect_info.addrs[0].end_addr, kRemoteAddrEnd);

  peer_thread.join();
}

TEST_F(ChannelMsgHandlerUnitTest, ExchangeConnectInfoUsesCallerTimeout) {
  ChannelConnectInfo peer_connect_info{};
  const auto start = std::chrono::steady_clock::now();
  EXPECT_EQ(handler_->ExchangeConnectInfo(hixl::test::Connection(sockets_[0]), 50, peer_connect_info), FAILED);
  const auto elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
  EXPECT_GE(elapsed, 40);
  EXPECT_LT(elapsed, 2000);
}

TEST_F(ChannelMsgHandlerUnitTest, DisconnectResponseUsesCallerTimeout) {
  const auto connection = hixl::test::Connection(sockets_[0]);
  const auto start = std::chrono::steady_clock::now();
  EXPECT_EQ(handler_->ValidateDisconnectResponse(connection, SUCCESS, 50), FAILED);
  const auto elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
  EXPECT_GE(elapsed, 40);
  EXPECT_LT(elapsed, 2000);
  EXPECT_EQ(handler_->ValidateDisconnectResponse(connection, FAILED, 50), SUCCESS);
}

TEST_F(ChannelMsgHandlerUnitTest, ServerReceivesDelayedRequest) {
  bool called = false;
  ASSERT_EQ(handler_->RegisterCallbackProcessor(
                static_cast<int32_t>(ChannelMsgType::kEnd),
                [&called](const hixl::CtrlMsgPluginPtr &, const char *, uint64_t, bool &keep_connection) {
                  called = true;
                  keep_connection = false;
                  return SUCCESS;
                }),
            SUCCESS);
  std::thread sender([&]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(llm::MsgHandlerPlugin::SendMsg(hixl::test::Connection(sockets_[1]),
                                             static_cast<int32_t>(ChannelMsgType::kEnd), "{}"),
              ge::SUCCESS);
  });
  bool keep_connection = true;
  const auto result = handler_->ConnectedProcess(hixl::test::Connection(sockets_[0]), keep_connection);
  sender.join();
  EXPECT_EQ(result, SUCCESS);
  EXPECT_TRUE(called);
  EXPECT_FALSE(keep_connection);
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
  ASSERT_EQ(json.size(), 5U);
  EXPECT_EQ(json.at("channel_id").get<std::string>(), kListenInfo);
  EXPECT_EQ(json.at("comm_res").get<std::string>(), kRemoteCommRes);
  EXPECT_EQ(json.at("timeout").get<int32_t>(), kTimeoutMs);
  EXPECT_EQ(json.at("share_handles").size(), 0U);
  ASSERT_EQ(json.at("addrs").size(), 1U);
  EXPECT_EQ(json.at("addrs").at(0).at("mem_type").get<int32_t>(), static_cast<int32_t>(MEM_HOST));
  EXPECT_EQ(json.at("addrs").at(0).at("start_addr").get<uintptr_t>(), kRemoteAddrStart);
  EXPECT_EQ(json.at("addrs").at(0).at("end_addr").get<uintptr_t>(), kRemoteAddrEnd);
}

// Deserialization must stop at msg_len even when the buffer is not null-terminated, so a malformed
// peer frame can never make the JSON parser scan past the received payload.
TEST_F(ChannelMsgHandlerUnitTest, DeserializeStopsAtMsgLenWithoutNullTerminator) {
  const std::string disconnect_json = BuildDisconnectJson();
  std::vector<char> buffer(disconnect_json.begin(), disconnect_json.end());
  const std::string garbage = "GARBAGE_NOT_JSON";
  buffer.insert(buffer.end(), garbage.begin(), garbage.end());

  ChannelDisconnectInfo info{};
  EXPECT_EQ(ChannelMsgHandler::Deserialize(buffer.data(), disconnect_json.size(), info), SUCCESS);
  EXPECT_EQ(info.channel_id, kRemoteEngine);
}

// MsgHandlerPlugin::RecvMsg appends a null terminator inside the received buffer; passing a msg_len
// that covers this byte must still parse successfully.
TEST_F(ChannelMsgHandlerUnitTest, DeserializeAcceptsTrailingNullWithinMsgLen) {
  const std::string disconnect_json = BuildDisconnectJson();
  std::vector<char> buffer(disconnect_json.begin(), disconnect_json.end());
  buffer.push_back('\0');

  ChannelDisconnectInfo info{};
  EXPECT_EQ(ChannelMsgHandler::Deserialize(buffer.data(), buffer.size(), info), SUCCESS);
  EXPECT_EQ(info.channel_id, kRemoteEngine);
}

TEST_F(ChannelMsgHandlerUnitTest, DeserializeRejectsTruncatedMsgLen) {
  const std::string disconnect_json = BuildDisconnectJson();
  ChannelDisconnectInfo info{};
  EXPECT_EQ(ChannelMsgHandler::Deserialize(disconnect_json.c_str(), disconnect_json.size() - 1, info), PARAM_INVALID);
}

TEST_F(ChannelMsgHandlerUnitTest, DeserializeRejectsNullptrAndEmptyMsgLen) {
  ChannelDisconnectInfo info{};
  EXPECT_EQ(ChannelMsgHandler::Deserialize(nullptr, 0, info), PARAM_INVALID);

  const std::string disconnect_json = BuildDisconnectJson();
  EXPECT_EQ(ChannelMsgHandler::Deserialize(disconnect_json.c_str(), 0, info), PARAM_INVALID);
}

TEST_F(ChannelMsgHandlerUnitTest, ProcessDisconnectRequestRejectsInvalidMsg) {
  bool keep_connection = true;
  EXPECT_EQ(handler_->ProcessDisconnectRequest(hixl::test::Connection(sockets_[0]), nullptr, 0, keep_connection),
            PARAM_INVALID);
  EXPECT_FALSE(keep_connection);

  const std::string disconnect_json = BuildDisconnectJson();
  EXPECT_EQ(handler_->ProcessDisconnectRequest(hixl::test::Connection(sockets_[0]), disconnect_json.c_str(), 0,
                                               keep_connection),
            PARAM_INVALID);
}

TEST_F(ChannelMsgHandlerUnitTest, ProcessConnectRequestRejectsInvalidMsg) {
  bool keep_connection = true;
  EXPECT_EQ(handler_->ProcessConnectRequest(hixl::test::Connection(sockets_[0]), nullptr, 0, keep_connection),
            PARAM_INVALID);
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

  // Set the connection on the channel (accessing private member)
  channel->connection_ = hixl::test::Connection(fd);

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

// Test that ProcessServerEviction treats an error_code=0 response as acceptance instead of a refusal
TEST_F(ChannelMsgHandlerUnitTest, ProcessServerEviction_WhenClientAccepts_LogsInfo) {
  ChannelInfo channel_info{};
  channel_info.channel_type = ChannelType::kServer;
  channel_info.channel_id = kRemoteEngine;
  auto channel = std::make_shared<CommChannel>(channel_info);

  // Create a socket pair for communication
  int socket_pair[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, socket_pair), 0);

  // Set the fd on the channel (accessing private member)
  channel->connection_ = hixl::test::Connection(socket_pair[0]);

  // Pre-set the next_req_id to a known value
  uint64_t req_id = 200;
  handler_->next_req_id_.store(req_id, std::memory_order_relaxed);

  // Wait until ProcessServerEviction registers the pending request, then inject an acceptance response
  // (error_code=0). Injecting before the registration would be a lost wakeup for this req_id.
  std::thread response_thread([this, req_id]() {
    std::unique_lock<std::mutex> lock(handler_->pending_req_mutex_);
    while (handler_->pending_disconnect_requests_.find(req_id) == handler_->pending_disconnect_requests_.end()) {
      lock.unlock();
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      lock.lock();
    }
    auto it = handler_->pending_disconnect_requests_.find(req_id);
    if (it != handler_->pending_disconnect_requests_.end()) {
      it->second->received = true;
      it->second->resp.req_id = req_id;
      it->second->resp.channel_id = kRemoteEngine;
      it->second->resp.can_disconnect = true;
      it->second->resp.disconnected = false;
      it->second->resp.error_code = 0U;  // 0 means acceptance, must NOT hit the refused-warning path
      it->second->resp.error_message = "";
      it->second->cv.notify_one();
    }
  });

  // Set disconnecting flag on channel
  channel->SetDisconnecting(true);

  // Call ProcessServerEviction which should hit the acceptance INFO path
  Status result = handler_->ProcessServerEviction(kRemoteEngine, channel);

  // Wait for the response thread to finish
  response_thread.join();

  // The function returns SUCCESS and releases the channel from the disconnecting state
  EXPECT_EQ(result, SUCCESS);
  EXPECT_FALSE(channel->IsDisconnecting());
  {
    std::lock_guard<std::mutex> lock(handler_->pending_req_mutex_);
    EXPECT_TRUE(handler_->pending_disconnect_requests_.empty());
  }

  // Cleanup
  close(socket_pair[0]);
  close(socket_pair[1]);
}

// Test that ProcessServerEviction times out and still releases the channel when the client never responds
TEST_F(ChannelMsgHandlerUnitTest, ProcessServerEviction_WhenClientSilent_TimesOutAndReleasesChannel) {
  ChannelInfo channel_info{};
  channel_info.channel_type = ChannelType::kServer;
  channel_info.channel_id = kRemoteEngine;
  auto channel = std::make_shared<CommChannel>(channel_info);

  int socket_pair[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, socket_pair), 0);
  channel->connection_ = hixl::test::Connection(socket_pair[0]);

  channel->SetDisconnecting(true);

  // No response is injected: the wait must expire within kWaitRespTime and release the channel, not hang
  Status result = handler_->ProcessServerEviction(kRemoteEngine, channel);

  EXPECT_EQ(result, SUCCESS);
  EXPECT_FALSE(channel->IsDisconnecting());
  {
    std::lock_guard<std::mutex> lock(handler_->pending_req_mutex_);
    EXPECT_TRUE(handler_->pending_disconnect_requests_.empty());
  }

  close(socket_pair[0]);
  close(socket_pair[1]);
}

TEST_F(ChannelMsgHandlerUnitTest, RegisterMemRejectsAddrLenOverflow) {
  MemDesc mem{};
  mem.addr = std::numeric_limits<uintptr_t>::max() - 0xFFU;
  mem.len = 0x1000U;  // addr + len wraps around uintptr_t
  MemHandle handle = nullptr;
  // The overflow guard runs before any HCCL registration, so this is rejected up front.
  EXPECT_EQ(handler_->RegisterMem(mem, MEM_DEVICE, handle), PARAM_INVALID);
}

TEST_F(ChannelMsgHandlerUnitTest, CreateChannelRejectsInvalidRemoteMemRange) {
  ChannelInfo channel_info{};
  channel_info.channel_id = kRemoteEngine;
  channel_info.channel_type = ChannelType::kClient;
  channel_info.local_rank_id = 0;
  channel_info.peer_rank_id = 1;

  ChannelConnectInfo peer_info{};
  peer_info.channel_id = kRemoteEngine;
  AddrInfo invalid_addr{};
  invalid_addr.start_addr = kRemoteAddrEnd;
  invalid_addr.end_addr = kRemoteAddrStart;
  invalid_addr.mem_type = MEM_HOST;
  peer_info.addrs.push_back(invalid_addr);

  EXPECT_EQ(handler_->CreateChannel(channel_info, true, peer_info), PARAM_INVALID);
}
TEST_F(ChannelMsgHandlerUnitTest, PeerFingerprintMatchesRequiresNonEmptyEquality) {
  EXPECT_TRUE(ChannelMsgHandler::PeerFingerprintMatches("abc", "abc"));
  EXPECT_FALSE(ChannelMsgHandler::PeerFingerprintMatches("abc", "def"));
  EXPECT_FALSE(ChannelMsgHandler::PeerFingerprintMatches("", "abc"));
  EXPECT_FALSE(ChannelMsgHandler::PeerFingerprintMatches("abc", ""));
  EXPECT_FALSE(ChannelMsgHandler::PeerFingerprintMatches("", ""));
}

TEST_F(ChannelMsgHandlerUnitTest, PeerCertFingerprintSkippedWithoutTlsVerification) {
  auto connection = hixl::test::Connection(sockets_[0]);
  ASSERT_NE(connection, nullptr);
  std::string fingerprint = "stale";
  EXPECT_EQ(handler_->GetPeerCertFingerprint(connection, fingerprint), SUCCESS);
  EXPECT_TRUE(fingerprint.empty());
}
}  // namespace adxl
