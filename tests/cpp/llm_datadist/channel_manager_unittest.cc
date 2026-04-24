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

class ChannelManagerUnitTest : public ::testing::Test {
 protected:
  ChannelPtr CreateChannelWithHeader(uint64_t body_size, uint32_t magic = kMagicNumber) {
    ChannelInfo channel_info{};
    channel_info.channel_type = ChannelType::kServer;
    channel_info.channel_id = kChannelId;
    auto channel = std::make_shared<Channel>(channel_info);
    ProtocolHeader header{magic, body_size};
    channel->recv_buffer_.resize(sizeof(header));
    memcpy_s(channel->recv_buffer_.data(), sizeof(header), &header, sizeof(header));
    channel->bytes_received_ = sizeof(header);
    channel->recv_state_ = RecvState::WAITING_FOR_HEADER;
    channel->expected_body_size_ = 0U;
    return channel;
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
}  // namespace
}  // namespace adxl
