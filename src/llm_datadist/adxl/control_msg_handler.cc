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

#include <cerrno>
#include <algorithm>
#include <limits>
#include "common/ctrl_msg_plugin.h"
#include "common/scope_guard.h"

namespace adxl {
namespace {
constexpr uint64_t kMicrosPerMillis = 1000U;
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

Status ControlMsgHandler::SendMsgByProtocol(const hixl::CtrlMsgPluginPtr &connection, ControlMsgType msg_type,
                                            const std::string &msg_str, uint64_t timeout) {
  auto start = std::chrono::steady_clock::now();
  auto body_size = msg_str.size() + sizeof(msg_type);
  ProtocolHeader protocol_header{kMagicNumber, body_size};
  ADXL_CHK_STATUS_RET(Write(connection, &protocol_header, sizeof(protocol_header), timeout, start),
                      "Failed to write msg");
  HIXL_DISMISSABLE_GUARD(incomplete_message, ([&connection]() {
                           ADXL_CHK_STATUS(connection->InvalidateTlsSession(), "Failed to invalidate TLS message");
                         }));
  ADXL_CHK_STATUS_RET(Write(connection, &msg_type, sizeof(ControlMsgType), timeout, start), "Failed to write msg");
  ADXL_CHK_STATUS_RET(Write(connection, msg_str.c_str(), msg_str.size(), timeout, start), "Failed to write msg");
  HIXL_DISMISS_GUARD(incomplete_message);
  return SUCCESS;
}

Status ControlMsgHandler::Write(const hixl::CtrlMsgPluginPtr &connection, const void *buf, size_t len, uint64_t timeout,
                                std::chrono::steady_clock::time_point &start) {
  if (len == 0U) {
    return SUCCESS;
  }
  const auto elapsed =
      std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
  ADXL_CHK_BOOL_RET_STATUS(static_cast<uint64_t>(elapsed) < timeout, TIMEOUT,
                           "Control message send timed out, elapsed:%ld us, timeout:%lu us", elapsed, timeout);
  // Round up to the Send API's millisecond resolution while the shared message budget is still positive.
  const uint64_t remaining_us = timeout - static_cast<uint64_t>(elapsed);
  const uint64_t remaining_ms = (remaining_us - 1U) / kMicrosPerMillis + 1U;
  const auto timeout_ms = static_cast<uint32_t>(std::min<uint64_t>(remaining_ms, std::numeric_limits<uint32_t>::max()));
  int32_t err_no = 0;
  const auto status = connection->Send(buf, len, timeout_ms, err_no);
  if (status == hixl::SUCCESS) {
    return SUCCESS;
  }
  LLMLOGE(FAILED,
          "Call api:CtrlMsgPlugin::Send failed, connection:%s, hixl_status:%u, size:%zu bytes, timeout:%u ms, errno:%d",
          connection->Desc().c_str(), static_cast<uint32_t>(status), len, timeout_ms, err_no);
  if (err_no == EPIPE || err_no == EBADF) {
    return kNoNeedRetry;
  }
  return status == hixl::TIMEOUT ? TIMEOUT : FAILED;
}
}  // namespace adxl
