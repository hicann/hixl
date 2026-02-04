/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <securec.h>
#include "hixl_cs_server.h"
#include "common/hixl_checker.h"

namespace hixl {
namespace {
const size_t kRecvChunkSizeInBytes = 4096U;  // 异步recv的默认buffer size
const size_t kMaxBodySizeInBytes = 4U * 1024U * 1024U;  // 消息体的最大长度
}

Status MsgReceiver::RecvHeader() {
  CtrlMsgHeader *header = nullptr;
  header = reinterpret_cast<CtrlMsgHeader *>(recv_buffer_.data());
  HIXL_CHK_BOOL_RET_STATUS(header->magic == kMagicNumber, PARAM_INVALID, "Invalid magic number:%u received.",
                            header->magic);
  HIXL_CHK_BOOL_RET_STATUS(header->body_size >= sizeof(CtrlMsgType), PARAM_INVALID,
                            "Invalid body size:%lu received, must >= %zu.", header->body_size, sizeof(CtrlMsgType));
  expected_size_ = header->body_size;
  HIXL_CHK_BOOL_RET_STATUS(expected_size_ <= kMaxBodySizeInBytes, PARAM_INVALID,
                           "Invalid body size:%zu received, must <= %zu.", expected_size_, kMaxBodySizeInBytes);
  recv_state_ = RecvState::WAITING_FOR_BODY;
  if (received_size_ > sizeof(CtrlMsgHeader)) {
    size_t remaining = received_size_ - sizeof(CtrlMsgHeader);
    auto ret = memmove_s(recv_buffer_.data(), remaining, recv_buffer_.data() + sizeof(CtrlMsgHeader), remaining);
    HIXL_CHK_BOOL_RET_STATUS(ret == EOK, FAILED,
                             "Call api:memmove_s failed, ret:%d, dst_addr:%p, dst_max:%zu, src_addr:%p, count:%zu",
                             static_cast<int32_t>(ret), recv_buffer_.data(), remaining,
                             recv_buffer_.data() + sizeof(CtrlMsgHeader), remaining);
    received_size_ = remaining;
  } else {
    received_size_ = 0U;
  }
  return SUCCESS;
}

bool MsgReceiver::CheckDisconnect(ssize_t recv_size) const {
  if (recv_size == 0) {
    HIXL_LOGI("Connection closed by peer, fd:%d.", fd_);
    return true;
  }
  if (recv_size < 0) {
    if (errno == EAGAIN || errno == EINTR) {
      return false;
    }
    HIXL_LOGE(FAILED, "recv error on fd:%d, errno:%s", fd_, strerror(errno));
    return true;
  }
  return false;
}

Status MsgReceiver::IRecv(std::vector<CtrlMsgPtr> &msgs) {
  if (recv_buffer_.size() < received_size_ + kRecvChunkSizeInBytes) {
    recv_buffer_.resize(received_size_ + kRecvChunkSizeInBytes);
  }
  auto buffer = recv_buffer_.data() + received_size_;
  auto buffer_size = recv_buffer_.size() - received_size_;
  ssize_t n = recv(fd_, buffer, buffer_size, 0);
  if (CheckDisconnect(n)) {
    auto msg = MakeShared<CtrlMsg>();
    HIXL_CHECK_NOTNULL(msg);
    msg->msg_type = CtrlMsgType::kDestroyChannelReq;
    msgs.emplace_back(msg);
    return SUCCESS;
  }

  received_size_ += static_cast<size_t>(n);
  while (true) {
    if (recv_state_ == RecvState::WAITING_FOR_HEADER) {
      if (received_size_ < sizeof(CtrlMsgHeader)) {
        break;
      }
      HIXL_CHK_STATUS_RET(RecvHeader(), "Failed to recv header");
    }
    if (recv_state_ == RecvState::WAITING_FOR_BODY) {
      if (received_size_ < expected_size_) {
        break;
      }
      auto ctrl_msg = MakeShared<CtrlMsg>();
      HIXL_CHECK_NOTNULL(ctrl_msg);
      ctrl_msg->msg_type = *reinterpret_cast<CtrlMsgType *>(recv_buffer_.data());
      ctrl_msg->msg = std::string(recv_buffer_.data() + sizeof(CtrlMsgType), expected_size_ - sizeof(CtrlMsgType));
      msgs.emplace_back(ctrl_msg);
      HIXL_LOGI("[HixlServer] recv ctrl msg, msg type:%d, msg body size:%zu",
                static_cast<int32_t>(ctrl_msg->msg_type), ctrl_msg->msg.size());
      if (received_size_ > expected_size_) {
        size_t remaining = received_size_ - expected_size_;
        auto ret = memmove_s(recv_buffer_.data(), remaining, recv_buffer_.data() + expected_size_, remaining);
        HIXL_CHK_BOOL_RET_STATUS(ret == EOK, FAILED,
                                 "Call api:memmove_s failed, ret:%d, dst_addr:%p, dst_max:%zu, src_addr:%p, count:%zu",
                                 static_cast<int32_t>(ret), recv_buffer_.data(), remaining,
                                 recv_buffer_.data() + expected_size_, remaining);
        received_size_ = remaining;
        recv_state_ = RecvState::WAITING_FOR_HEADER;
      } else {
        received_size_ = 0U;
        recv_state_ = RecvState::WAITING_FOR_HEADER;
        break;
      }
    }
  }
  return SUCCESS;
}
}  // namespace hixl
