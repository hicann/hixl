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
#include <cstring>
#include <memory>

#define private public
#include "adxl/channel_manager.h"
#undef private

namespace adxl {
namespace {
constexpr char kChannelId[] = "test_channel";
constexpr uint64_t kMaxControlMsgBodySizeInBytes = 4ULL * 1024ULL * 1024ULL;
constexpr size_t kMaxNotifyStorageSize = 4096;

class ChannelManagerUnitTest : public ::testing::Test {
 protected:
  ChannelPtr CreateChannelWithHeader(uint64_t body_size, uint32_t magic = kMagicNumber) {
    ChannelInfo channel_info{};
    channel_info.channel_type = ChannelType::kServer;
    channel_info.channel_id = kChannelId;
    auto channel = std::make_shared<CommChannel>(channel_info);
    ProtocolHeader header{magic, body_size};
    channel->recv_buffer_.resize(sizeof(header));
    memcpy_s(channel->recv_buffer_.data(), sizeof(header), &header, sizeof(header));
    channel->bytes_received_ = sizeof(header);
    channel->recv_state_ = RecvState::WAITING_FOR_HEADER;
    channel->expected_body_size_ = 0U;
    return channel;
  }

  std::string CreateNotifyMsgStr(uint64_t req_id, const std::string& name = "test_name", const std::string& msg = "test_msg") {
    NotifyMsg notify_msg{req_id, name, msg};
    std::string serialized_str;
    ControlMsgHandler::Serialize(notify_msg, serialized_str);
    return serialized_str;
  }

  ChannelManager manager_;
};

TEST_F(ChannelManagerUnitTest, ProcessReceivedDataAcceptsBodySizeAtLimit) {
  auto channel = CreateChannelWithHeader(kMaxControlMsgBodySizeInBytes);

  EXPECT_EQ(manager_.ProcessReceivedData(channel), SUCCESS);
  EXPECT_EQ(channel->recv_state_, RecvState::WAITING_FOR_BODY);
  EXPECT_EQ(channel->expected_body_size_, kMaxControlMsgBodySizeInBytes);
  EXPECT_EQ(channel->bytes_received_, 0U);
}

TEST_F(ChannelManagerUnitTest, ProcessReceivedDataRejectsOversizedBodySize) {
  auto channel = CreateChannelWithHeader(kMaxControlMsgBodySizeInBytes + 1U);

  EXPECT_EQ(manager_.ProcessReceivedData(channel), FAILED);
  EXPECT_EQ(channel->recv_state_, RecvState::WAITING_FOR_HEADER);
  EXPECT_EQ(channel->expected_body_size_, 0U);
  EXPECT_EQ(channel->bytes_received_, sizeof(ProtocolHeader));
}

TEST_F(ChannelManagerUnitTest, ProcessReceivedDataRejectsInvalidMagicNumber) {
  auto channel = CreateChannelWithHeader(kMaxControlMsgBodySizeInBytes, kMagicNumber + 1U);

  EXPECT_EQ(manager_.ProcessReceivedData(channel), FAILED);
  EXPECT_EQ(channel->recv_state_, RecvState::WAITING_FOR_HEADER);
  EXPECT_EQ(channel->expected_body_size_, 0U);
  EXPECT_EQ(channel->bytes_received_, sizeof(ProtocolHeader));
}

TEST_F(ChannelManagerUnitTest, HandleNotifyMessage_WhenStorageLimitExceeded_ReturnsFailed) {
  ChannelInfo channel_info{};
  channel_info.channel_type = ChannelType::kServer;
  channel_info.channel_id = kChannelId;
  auto channel = std::make_shared<CommChannel>(channel_info);

  // Fill notify messages to reach limit
  for (size_t i = 0; i < kMaxNotifyStorageSize; ++i) {
    EXPECT_EQ(manager_.HandleNotifyMessage(channel, CreateNotifyMsgStr(i)), SUCCESS);
  }
  EXPECT_EQ(channel->notify_messages_.size(), kMaxNotifyStorageSize);

  // Attempt to add one more notify message
  EXPECT_EQ(manager_.HandleNotifyMessage(channel, CreateNotifyMsgStr(kMaxNotifyStorageSize)), FAILED);
  EXPECT_EQ(channel->notify_messages_.size(), kMaxNotifyStorageSize);
}

// Helper function to create RequestDisconnectMsg string
std::string CreateRequestDisconnectMsgStr(uint64_t req_id, const std::string& channel_id, uint64_t timeout = 1000) {
  RequestDisconnectMsg msg;
  msg.req_id = req_id;
  msg.channel_id = channel_id;
  msg.timeout = timeout;
  std::string serialized_str;
  ControlMsgHandler::Serialize(msg, serialized_str);
  return serialized_str;
}

TEST_F(ChannelManagerUnitTest, HandleRequestDisconnectMessage_WhenDisconnectCallbackFails_LogsWarning) {
  // Set up a mock disconnect callback that returns failure
  bool callback_invoked = false;
  manager_.SetDisconnectCallback([&callback_invoked](const std::string& channel_id, int32_t timeout_ms) -> Status {
    callback_invoked = true;
    (void)channel_id;
    (void)timeout_ms;
    return FAILED;  // Return failure to trigger the LLMLOGW line
  });

  // Create a channel with zero transfer count so can_disconnect will be true
  ChannelInfo channel_info{};
  channel_info.channel_type = ChannelType::kServer;
  channel_info.channel_id = kChannelId;
  auto channel = std::make_shared<CommChannel>(channel_info);

  // Verify transfer count is 0
  EXPECT_EQ(channel->GetTransferCount(), 0);

  // Create and handle the disconnect request message
  std::string msg_str = CreateRequestDisconnectMsgStr(1U, kChannelId, 1000);
  EXPECT_EQ(manager_.HandleRequestDisconnectMessage(channel, msg_str), SUCCESS);
  EXPECT_TRUE(callback_invoked);
}
}  // namespace
}  // namespace adxl
