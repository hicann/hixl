/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <cerrno>
#include <fcntl.h>
#include <gtest/gtest.h>
#include <memory>
#include <sys/socket.h>
#include <unistd.h>

#include "depends/llm_datadist/src/data_cache_engine_test_helper.h"

#define private public
#include "adxl/comm_channel.h"
#undef private

namespace adxl {
namespace {

class UnbindFailureHcclApiStub final : public llm::HcclApiStub {
 public:
  HcclResult HcclCommUnbindMem(HcclComm comm, void *mem_handle) override {
    (void)comm;
    (void)mem_handle;
    ++unbind_count;
    return HCCL_E_TIMEOUT;
  }

  HcclResult HcclCommDestroy(HcclComm comm) override {
    (void)comm;
    ++destroy_count;
    return HCCL_SUCCESS;
  }

  int unbind_count = 0;
  int destroy_count = 0;
};

class CommChannelFinalizeUnitTest : public ::testing::Test {
 protected:
  void SetUp() override {
    llm::MockMmpaForHcclApi::Install();
    ASSERT_EQ(llm::CommAdapter::GetInstance().Initialize(), ge::SUCCESS);
  }

  void TearDown() override {
    llm::CommAdapter::GetInstance().Finalize();
    llm::MockMmpaForHcclApi::Reset();
  }
};

TEST_F(CommChannelFinalizeUnitTest, FinalizeCleansLocalResourcesWhenUnbindFails) {
  int socket_fds[2] = {-1, -1};
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, socket_fds), 0);

  ChannelInfo channel_info{};
  channel_info.channel_type = ChannelType::kClient;
  channel_info.channel_id = "comm_channel_finalize_test";
  auto channel = std::make_shared<CommChannel>(channel_info);
  channel->channel_info_.comm = reinterpret_cast<HcclComm>(0x1);
  channel->channel_info_.registered_mems.emplace(reinterpret_cast<MemHandle>(0x2), nullptr);
  channel->fd_ = socket_fds[0];
  channel->with_heartbeat_.store(true, std::memory_order_release);
  channel->disconnect_flag_.store(true, std::memory_order_release);
  channel->transfer_count_.store(3, std::memory_order_release);
  channel->notify_messages_.emplace_back();

  auto hccl_stub = std::make_unique<UnbindFailureHcclApiStub>();
  auto *hccl_stub_ptr = hccl_stub.get();
  llm::HcclApiStub::SetStub(std::move(hccl_stub));

  EXPECT_EQ(channel->Finalize(), FAILED);
  EXPECT_EQ(hccl_stub_ptr->unbind_count, 1);
  EXPECT_EQ(hccl_stub_ptr->destroy_count, 1);
  EXPECT_TRUE(channel->IsFinalized());
  EXPECT_EQ(channel->GetFd(), -1);
  EXPECT_FALSE(channel->with_heartbeat_.load(std::memory_order_acquire));
  EXPECT_FALSE(channel->IsDisconnecting());
  EXPECT_EQ(channel->GetTransferCount(), 0);
  EXPECT_TRUE(channel->notify_messages_.empty());
  EXPECT_EQ(channel->channel_info_.comm, nullptr);

  errno = 0;
  EXPECT_EQ(fcntl(socket_fds[0], F_GETFD), -1);
  EXPECT_EQ(errno, EBADF);
  socket_fds[0] = -1;
  close(socket_fds[1]);
}

}  // namespace
}  // namespace adxl
