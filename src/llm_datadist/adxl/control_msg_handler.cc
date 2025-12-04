/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "control_msg_handler.h"
namespace adxl {
namespace {
Status kNoNeedRetry = 1U;
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
    auto rc = write(fd, pos, nbytes);
    if (rc < 0 && (errno == EAGAIN || errno == EINTR)) {
      continue;
    } else if (rc < 0) {
      Status ret = FAILED;
      if (errno == EPIPE || errno == EBADF) {
        ret = kNoNeedRetry;
      }
      LLMLOGE(FAILED, "Socket write failed, error msg:%s, errno:%d", strerror(errno), errno);
      return ret;
    } else if (rc == 0) {
      LLMLOGW("Socket write incompleted: expected %zu bytes, actual %zu bytes", len, len - nbytes);
      return FAILED;
    }
    pos += rc;
    nbytes -= rc;
    uint64_t time_cost =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
    ADXL_CHK_BOOL_RET_STATUS(time_cost < timeout, TIMEOUT, "Transfer timeout.");
  }
  return SUCCESS;
}
}  // namespace adxl
