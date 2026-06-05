/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "adxl/control_msg_handler.h"

namespace adxl {
namespace {
constexpr uint64_t kWriteTimeoutUs = 5000000U;

class ControlMsgHandlerWriteTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds_), 0);
    write_fd_ = fds_[0];
    read_fd_ = fds_[1];
  }

  void TearDown() override {
    if (write_fd_ >= 0) {
      close(write_fd_);
      write_fd_ = -1;
    }
    if (read_fd_ >= 0) {
      close(read_fd_);
      read_fd_ = -1;
    }
  }

  int fds_[2] = {-1, -1};
  int write_fd_ = -1;
  int read_fd_ = -1;
};

TEST_F(ControlMsgHandlerWriteTest, WriteCompletesFullPayload) {
  const std::string payload = "control-msg-payload";
  auto start = std::chrono::steady_clock::now();
  EXPECT_EQ(ControlMsgHandler::Write(write_fd_, payload.data(), payload.size(), kWriteTimeoutUs, start), SUCCESS);

  std::vector<char> received(payload.size());
  ASSERT_EQ(read(read_fd_, received.data(), received.size()), static_cast<ssize_t>(payload.size()));
  EXPECT_EQ(std::string(received.begin(), received.end()), payload);
}

TEST_F(ControlMsgHandlerWriteTest, WriteHandlesPartialWriteAndEagain) {
  const std::string payload = "partial-write-payload";
  std::thread reader([this, &payload]() {
    char buffer[4] = {};
    ASSERT_EQ(read(read_fd_, buffer, sizeof(buffer)), static_cast<ssize_t>(sizeof(buffer)));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    std::vector<char> remaining(payload.size() - sizeof(buffer));
    ASSERT_EQ(read(read_fd_, remaining.data(), remaining.size()), static_cast<ssize_t>(remaining.size()));
  });

  auto start = std::chrono::steady_clock::now();
  EXPECT_EQ(ControlMsgHandler::Write(write_fd_, payload.data(), payload.size(), kWriteTimeoutUs, start), SUCCESS);
  reader.join();
}
}  // namespace
}  // namespace adxl
