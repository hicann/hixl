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
constexpr size_t kShareHandleDataSize = sizeof(aclrtMemFabricHandle{}.data);
constexpr size_t kOverflowBytes = sizeof(aclrtDrvMemHandle) + sizeof(uintptr_t);
constexpr uint64_t kSerializeTestVaAddr = 0x1234U;
constexpr uint64_t kSerializeTestLen = 0x5678U;
constexpr char kNonArrayShareHandleJsonValue[] = "invalid_share_handle";

std::vector<uint8_t> CreateShareHandleBytes(size_t size) {
  std::vector<uint8_t> share_handle_bytes(size, 0U);
  for (size_t i = 0; i < size; ++i) {
    share_handle_bytes[i] = static_cast<uint8_t>(i);
  }
  return share_handle_bytes;
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

  nlohmann::json BuildConnectInfoJson(const std::vector<uint8_t> &share_handle_bytes) const {
    return BuildConnectInfoJsonWithShareHandleJson(share_handle_bytes);
  }

  nlohmann::json BuildConnectInfoJsonWithShareHandleJson(const nlohmann::json &share_handle_json) const {
    nlohmann::json share_handle_info = {
        {"va_addr", 0},
        {"len", 0},
        {"share_handle", share_handle_json},
    };
    return {
        {"channel_id", kRemoteEngine},
        {"comm_res", kRemoteCommRes},
        {"timeout", kTimeoutMs},
        {"addrs", nlohmann::json::array()},
        {"share_handles", nlohmann::json::array({share_handle_info})},
    };
  }

  std::thread StartPeerExchangeThread(const nlohmann::json &response_json) {
    return std::thread([this, response_json]() {
      int32_t msg_type = 0;
      std::vector<char> msg;
      EXPECT_EQ(llm::MsgHandlerPlugin::RecvMsg(sockets_[1], msg_type, msg), SUCCESS);
      EXPECT_EQ(msg_type, static_cast<int32_t>(ChannelMsgType::kConnect));
      EXPECT_EQ(
          llm::MsgHandlerPlugin::SendMsg(sockets_[1], static_cast<int32_t>(ChannelMsgType::kConnect), response_json.dump()),
          SUCCESS);
    });
  }

  std::array<int32_t, 2> sockets_{{-1, -1}};
  ChannelManager channel_manager_;
  std::unique_ptr<ChannelMsgHandler> handler_;
};

TEST_F(ChannelMsgHandlerUnitTest, ExchangeConnectInfoAcceptsFixedSizeShareHandleArray) {
  const auto share_handle_bytes = CreateShareHandleBytes(kShareHandleDataSize);
  auto peer_thread = StartPeerExchangeThread(BuildConnectInfoJson(share_handle_bytes));

  ChannelConnectInfo peer_connect_info{};
  EXPECT_EQ(handler_->ExchangeConnectInfo(sockets_[0], kTimeoutMs, peer_connect_info), SUCCESS);
  ASSERT_EQ(peer_connect_info.share_handles.size(), 1U);
  EXPECT_EQ(peer_connect_info.channel_id, kRemoteEngine);
  EXPECT_EQ(peer_connect_info.comm_res, kRemoteCommRes);
  EXPECT_EQ(peer_connect_info.timeout, kTimeoutMs);
  EXPECT_EQ(peer_connect_info.share_handles[0].imported_handle, nullptr);
  EXPECT_EQ(peer_connect_info.share_handles[0].imported_va, 0U);
  EXPECT_FALSE(peer_connect_info.share_handles[0].is_retained);
  for (size_t i = 0; i < kShareHandleDataSize; ++i) {
    EXPECT_EQ(peer_connect_info.share_handles[0].share_handle.data[i], share_handle_bytes[i]);
  }

  peer_thread.join();
}

TEST_F(ChannelMsgHandlerUnitTest, ExchangeConnectInfoRejectsOversizedShareHandleArray) {
  const auto share_handle_bytes = CreateShareHandleBytes(kShareHandleDataSize + kOverflowBytes);
  auto peer_thread = StartPeerExchangeThread(BuildConnectInfoJson(share_handle_bytes));

  ChannelConnectInfo peer_connect_info{};
  EXPECT_EQ(handler_->ExchangeConnectInfo(sockets_[0], kTimeoutMs, peer_connect_info), PARAM_INVALID);

  peer_thread.join();
}

TEST_F(ChannelMsgHandlerUnitTest, ExchangeConnectInfoRejectsNonArrayShareHandle) {
  auto peer_thread = StartPeerExchangeThread(BuildConnectInfoJsonWithShareHandleJson(kNonArrayShareHandleJsonValue));

  ChannelConnectInfo peer_connect_info{};
  EXPECT_EQ(handler_->ExchangeConnectInfo(sockets_[0], kTimeoutMs, peer_connect_info), PARAM_INVALID);

  peer_thread.join();
}

TEST_F(ChannelMsgHandlerUnitTest, SerializeConnectInfoWritesFixedSizeShareHandleArray) {
  const auto share_handle_bytes = CreateShareHandleBytes(kShareHandleDataSize);
  ChannelConnectInfo connect_info{};
  connect_info.channel_id = kListenInfo;
  connect_info.comm_res = kRemoteCommRes;
  connect_info.timeout = kTimeoutMs;
  connect_info.share_handles.resize(1);
  connect_info.share_handles[0].va_addr = kSerializeTestVaAddr;
  connect_info.share_handles[0].len = kSerializeTestLen;
  for (size_t i = 0; i < kShareHandleDataSize; ++i) {
    connect_info.share_handles[0].share_handle.data[i] = share_handle_bytes[i];
  }

  std::string serialized;
  ASSERT_EQ(ChannelMsgHandler::Serialize(connect_info, serialized), SUCCESS);

  const auto json = nlohmann::json::parse(serialized);
  ASSERT_EQ(json.at("share_handles").size(), 1U);
  const auto &share_handle_json = json.at("share_handles").at(0).at("share_handle");
  ASSERT_TRUE(share_handle_json.is_array());
  ASSERT_EQ(share_handle_json.size(), kShareHandleDataSize);
  for (size_t i = 0; i < kShareHandleDataSize; ++i) {
    EXPECT_EQ(share_handle_json.at(i).get<uint8_t>(), share_handle_bytes[i]);
  }
}
}  // namespace adxl
