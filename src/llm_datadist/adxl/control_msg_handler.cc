/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "control_msg_handler.h"

#include <poll.h>

namespace adxl {
namespace {
constexpr int64_t kMicrosPerMillis = 1000;

Status PollForWriteReady(int32_t fd, int64_t remaining_us) {
  if (remaining_us <= 0) {
    return TIMEOUT;
  }
  struct pollfd pfd = {};
  pfd.fd = fd;
  pfd.events = POLLOUT;
  const int poll_timeout_ms = static_cast<int>(remaining_us / kMicrosPerMillis);
  const int poll_ret = poll(&pfd, 1, poll_timeout_ms);
  if (poll_ret == 0) {
    return TIMEOUT;
  }
  if (poll_ret < 0) {
    if (errno == EINTR) {
      return SUCCESS;
    }
    LLMLOGE(FAILED, "Socket poll failed, error msg:%s, errno:%d", strerror(errno), errno);
    return FAILED;
  }
  return SUCCESS;
}
}  // namespace
namespace {
Status kNoNeedRetry = 1U;
}  // namespace

std::string TransferTypeToString(TransferType type) {
  switch (type) {
    case TransferType::kWriteH2RH:
      return "WriteH2RH";
    case TransferType::kReadRH2H:
      return "ReadRH2H";
    case TransferType::kWriteH2RD:
      return "WriteH2RD";
    case TransferType::kReadRH2D:
      return "ReadRH2D";
    case TransferType::kWriteD2RH:
      return "WriteD2RH";
    case TransferType::kReadRD2H:
      return "ReadRD2H";
    case TransferType::kWriteD2RD:
      return "WriteD2RD";
    case TransferType::kReadRD2D:
      return "ReadRD2D";
    case TransferType::kEnd:
      return "End";
    default:
      return "Unknown";
  }
}

Status ControlMsgHandler::SendMsgByProtocol(int32_t fd, ControlMsgType msg_type, const std::string &msg_str,
                                            uint64_t timeout) {
  auto start = std::chrono::steady_clock::now();
  auto body_size = msg_str.size() + sizeof(msg_type);
  ProtocolHeader protocol_header{kMagicNumber, body_size};
  ADXL_CHK_STATUS_RET(Write(fd, &protocol_header, sizeof(protocol_header), timeout, start), "Failed to write msg");
  ADXL_CHK_STATUS_RET(Write(fd, &msg_type, sizeof(ControlMsgType), timeout, start), "Failed to write msg");
  ADXL_CHK_STATUS_RET(Write(fd, msg_str.c_str(), msg_str.size(), timeout, start), "Failed to write msg");
  return SUCCESS;
}

Status ControlMsgHandler::Write(int32_t fd, const void *buf, size_t len, uint64_t timeout,
                                std::chrono::steady_clock::time_point &start) {
  const char *pos = static_cast<const char *>(buf);
  size_t nbytes = len;
  while (nbytes > 0U) {
    const int64_t time_cost =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
    ADXL_CHK_BOOL_RET_STATUS(time_cost < static_cast<int64_t>(timeout), TIMEOUT, "Transfer timeout.");

    auto rc = write(fd, pos, nbytes);
    if (rc < 0 && errno == EINTR) {
      continue;
    }
    if (rc < 0 && errno == EAGAIN) {
      const int64_t remaining_us = static_cast<int64_t>(timeout) - time_cost;
      ADXL_CHK_STATUS_RET(PollForWriteReady(fd, remaining_us), "Socket poll failed");
      continue;
    }
    if (rc < 0) {
      Status ret = FAILED;
      if (errno == EPIPE || errno == EBADF) {
        ret = kNoNeedRetry;
      }
      LLMLOGE(FAILED, "Socket write failed, error msg:%s, errno:%d", strerror(errno), errno);
      return ret;
    }
    if (rc == 0) {
      LLMLOGW("Socket write incompleted: expected %zu bytes, actual %zu bytes", len, len - nbytes);
      return FAILED;
    }
    pos += rc;
    nbytes -= rc;
  }
  return SUCCESS;
}
}  // namespace adxl
