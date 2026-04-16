/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <gtest/gtest.h>
#include <chrono>
#include <memory>
#include <thread>

#define private public
#include "adxl/buffer_transfer_service.h"
#undef private

#include "adxl/channel.h"
#include "depends/ascendcl/src/ascendcl_stub.h"

namespace adxl {
namespace {
constexpr char kChannelId[] = "buffer_transfer_service_test_channel";
constexpr int32_t kPeerRankId = 1;
constexpr uint64_t kWaitTimeoutMs = 500;
constexpr uint64_t kExpiredTimeoutUs = 1;

class ScopedRuntimeMockForBuffer {
 public:
  explicit ScopedRuntimeMockForBuffer(const std::shared_ptr<llm::AclRuntimeStub> &instance) {
    llm::AclRuntimeStub::SetInstance(instance);
  }

  ~ScopedRuntimeMockForBuffer() {
    llm::AclRuntimeStub::Reset();
  }

  ScopedRuntimeMockForBuffer(const ScopedRuntimeMockForBuffer &) = delete;
  ScopedRuntimeMockForBuffer &operator=(const ScopedRuntimeMockForBuffer &) = delete;
};

ChannelPtr CreateChannel() {
  ChannelInfo channel_info{};
  channel_info.channel_id = kChannelId;
  channel_info.local_rank_id = 0;
  channel_info.peer_rank_id = kPeerRankId;
  return std::make_shared<Channel>(channel_info);
}
}  // namespace

class BufferTransferServiceUTest : public ::testing::Test {
 protected:
  void SetUp() override {
    mock_runtime_ = std::make_shared<llm::AclRuntimeStub>();
    scoped_runtime_mock_ = std::make_unique<ScopedRuntimeMockForBuffer>(mock_runtime_);
    service_ = std::make_unique<BufferTransferService>(std::vector<llm::LlmMemPool *>{}, 1024);
    ASSERT_EQ(service_->Initialize(), SUCCESS);
  }

  void TearDown() override {
    if (service_ != nullptr) {
      service_->Finalize();
    }
    scoped_runtime_mock_.reset();
    mock_runtime_.reset();
  }

  template <typename Queue, typename Predicate>
  void WaitUntilQueueProcessed(std::mutex &mutex, Queue &queue, Predicate &&predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kWaitTimeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
      {
        std::lock_guard<std::mutex> lock(mutex);
        if (queue.empty() && predicate()) {
          return;
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::lock_guard<std::mutex> lock(mutex);
    EXPECT_TRUE(queue.empty());
    EXPECT_TRUE(predicate());
  }

  std::unique_ptr<BufferTransferService> service_;
  std::shared_ptr<llm::AclRuntimeStub> mock_runtime_;
  std::unique_ptr<ScopedRuntimeMockForBuffer> scoped_runtime_mock_;
};

TEST_F(BufferTransferServiceUTest, ProcessBufferReqSkipsFinalizedChannel) {
  auto channel = CreateChannel();
  ASSERT_EQ(channel->Finalize(), SUCCESS);

  BufferReq buffer_req{};
  buffer_req.transfer_type = TransferType::kWriteH2RH;
  buffer_req.timeout = 1000;

  ASSERT_EQ(service_->PushBufferReq(channel, buffer_req), SUCCESS);
  WaitUntilQueueProcessed(service_->buffer_req_mutex_, service_->buffer_req_queue_, []() { return true; });
}

TEST_F(BufferTransferServiceUTest, ProcessBufferReqSecondStepSkipsFinalizedChannel) {
  auto channel = CreateChannel();
  ASSERT_EQ(channel->Finalize(), SUCCESS);

  BufferReq buffer_req{};
  buffer_req.transfer_type = TransferType::kReadRH2H;
  buffer_req.timeout = 1000;

  ASSERT_EQ(service_->PushSecondStepReq(channel, buffer_req), SUCCESS);
  WaitUntilQueueProcessed(service_->buffer_second_step_mutex_, service_->buffer_second_step_queue_,
                          []() { return true; });
}

TEST_F(BufferTransferServiceUTest, ProcessBufferReqSecondStepReleasesExpiredRequest) {
  auto channel = CreateChannel();
  ASSERT_FALSE(channel->IsFinalized());

  BufferReq buffer_req{};
  buffer_req.transfer_type = TransferType::kReadRH2H;
  buffer_req.timeout = kExpiredTimeoutUs;
  buffer_req.recv_start_time = std::chrono::steady_clock::now() - std::chrono::milliseconds(1);

  {
    std::lock_guard<std::mutex> lock(service_->buffer_second_step_mutex_);
    service_->buffer_second_step_queue_.emplace(channel, buffer_req);
  }
  service_->buffer_second_step_cv_.notify_one();

  WaitUntilQueueProcessed(service_->buffer_second_step_mutex_, service_->buffer_second_step_queue_,
                          []() { return true; });
}

TEST_F(BufferTransferServiceUTest, ProcessCtrlMsgSkipsFinalizedChannel) {
  auto channel = CreateChannel();
  ASSERT_EQ(channel->Finalize(), SUCCESS);

  BufferReq buffer_req{};
  buffer_req.transfer_type = TransferType::kWriteH2RH;

  service_->PushCtrlMsg(channel, buffer_req);
  WaitUntilQueueProcessed(service_->buffer_ctrl_msg_mutex_, service_->buffer_ctrl_msg_queue_, []() { return true; });
}
}  // namespace adxl
