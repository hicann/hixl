/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "common/ctrl_msg.h"
#include "cs/msg_receiver.h"

namespace hixl {
namespace {
void WriteAll(int fd, const void *data, size_t len) {
  const char *pos = static_cast<const char *>(data);
  size_t remaining = len;
  while (remaining > 0U) {
    const ssize_t n = write(fd, pos, remaining);
    ASSERT_GT(n, 0);
    pos += n;
    remaining -= static_cast<size_t>(n);
  }
}

void SendCtrlMsg(int fd, CtrlMsgType msg_type, const std::string &body) {
  CtrlMsgHeader header{};
  header.magic = kMagicNumber;
  header.body_size = sizeof(CtrlMsgType) + body.size();
  WriteAll(fd, &header, sizeof(header));
  WriteAll(fd, &msg_type, sizeof(msg_type));
  if (!body.empty()) {
    WriteAll(fd, body.data(), body.size());
  }
}
}  // namespace

class MsgReceiverTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds_), 0);
    receiver_ = std::make_unique<MsgReceiver>(fds_[0]);
  }

  void TearDown() override {
    receiver_.reset();
    if (fds_[0] >= 0) {
      close(fds_[0]);
      fds_[0] = -1;
    }
    if (fds_[1] >= 0) {
      close(fds_[1]);
      fds_[1] = -1;
    }
  }

  std::vector<CtrlMsgPtr> RecvUntilMessage() {
    std::vector<CtrlMsgPtr> msgs;
    EXPECT_EQ(receiver_->IRecv(msgs), SUCCESS);
    return msgs;
  }

  int fds_[2] = {-1, -1};
  std::unique_ptr<MsgReceiver> receiver_;
};

TEST_F(MsgReceiverTest, ReceivesSingleMessage) {
  SendCtrlMsg(fds_[1], CtrlMsgType::kGetEndpointInfoReq, "endpoint-req");

  const auto msgs = RecvUntilMessage();
  ASSERT_EQ(msgs.size(), 1U);
  EXPECT_EQ(msgs[0]->msg_type, CtrlMsgType::kGetEndpointInfoReq);
  EXPECT_EQ(msgs[0]->msg, "endpoint-req");
}

TEST_F(MsgReceiverTest, ReceivesCoalescedMessagesInOneRead) {
  SendCtrlMsg(fds_[1], CtrlMsgType::kGetEndpointInfoReq, "first");
  SendCtrlMsg(fds_[1], CtrlMsgType::kGetEndpointInfoResp, "second");

  std::vector<CtrlMsgPtr> msgs;
  EXPECT_EQ(receiver_->IRecv(msgs), SUCCESS);
  ASSERT_EQ(msgs.size(), 2U);
  EXPECT_EQ(msgs[0]->msg_type, CtrlMsgType::kGetEndpointInfoReq);
  EXPECT_EQ(msgs[0]->msg, "first");
  EXPECT_EQ(msgs[1]->msg_type, CtrlMsgType::kGetEndpointInfoResp);
  EXPECT_EQ(msgs[1]->msg, "second");
}

TEST_F(MsgReceiverTest, RejectsOversizedBody) {
  CtrlMsgHeader header{};
  header.magic = kMagicNumber;
  header.body_size = 4U * 1024U * 1024U + 1U;
  WriteAll(fds_[1], &header, sizeof(header));

  std::vector<CtrlMsgPtr> msgs;
  EXPECT_EQ(receiver_->IRecv(msgs), PARAM_INVALID);
  EXPECT_TRUE(msgs.empty());
}
}  // namespace hixl
