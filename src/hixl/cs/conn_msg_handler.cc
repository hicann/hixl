/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "conn_msg_handler.h"
#include <cstring>
#include <vector>
#include <securec.h>
#include "common/ctrl_msg_plugin.h"

namespace {
hixl::Status SendHeaderTypeBody(int32_t socket, const hixl::CtrlMsgHeader &header, hixl::CtrlMsgType msg_type,
                                const void *body, uint64_t body_size) {
  HIXL_LOGI("Start sending header, type and body. socket: %d, body_size: %lu", socket, body_size);
  HIXL_CHK_STATUS_RET(hixl::CtrlMsgPlugin::Send(socket, &header, static_cast<uint64_t>(sizeof(header))));
  HIXL_CHK_STATUS_RET(hixl::CtrlMsgPlugin::Send(socket, &msg_type, static_cast<uint64_t>(sizeof(msg_type))));
  HIXL_CHK_STATUS_RET(hixl::CtrlMsgPlugin::Send(socket, body, body_size));
  HIXL_LOGI("Successfully sent header, type and body.");
  return hixl::SUCCESS;
}

hixl::Status RecvAndCheckHeader(int32_t socket, uint64_t expect_body_size, hixl::CtrlMsgHeader &header,
                                uint32_t timeout_ms) {
  HIXL_LOGI("Start receiving and checking header. socket: %d, expect_body_size: %lu", socket, expect_body_size);
  HIXL_CHK_STATUS_RET(hixl::CtrlMsgPlugin::Recv(socket, &header, static_cast<uint32_t>(sizeof(header)), timeout_ms),
                      "Failed to recv header. socket=%d, len=%lu, timeout=%u ms", socket, sizeof(header), timeout_ms);
  HIXL_LOGD("Header received. magic: 0x%X, body_size: %lu", header.magic, header.body_size);
  HIXL_CHK_BOOL_RET_STATUS(header.magic == hixl::kMagicNumber, hixl::PARAM_INVALID,
                           "Invalid magic for CreateChannelResp, expect:0x%X, actual:0x%X", hixl::kMagicNumber,
                           header.magic);
  HIXL_CHK_BOOL_RET_STATUS(header.body_size == expect_body_size, hixl::PARAM_INVALID,
                           "Invalid body_size in CreateChannelResp, expect:%" PRIu64 ", actual:%" PRIu64,
                           expect_body_size, header.body_size);
  return hixl::SUCCESS;
}

hixl::Status RecvBody(int32_t socket, uint64_t body_size, std::vector<uint8_t> &body, uint32_t timeout_ms) {
  HIXL_LOGI("Start receiving body. socket: %d, body_size: %lu", socket, body_size);
  body.resize(static_cast<size_t>(body_size));
  HIXL_CHK_STATUS_RET(hixl::CtrlMsgPlugin::Recv(socket, body.data(), static_cast<uint32_t>(body_size), timeout_ms));
  HIXL_LOGI("Successfully received body.");
  return hixl::SUCCESS;
}

hixl::Status ParseMsgType(const std::vector<uint8_t> &body, size_t &offset, hixl::CtrlMsgType &msg_type) {
  if (offset + sizeof(hixl::CtrlMsgType) > body.size()) {
    HIXL_LOGE(hixl::PARAM_INVALID, "CreateChannelResp body too short for msg_type, offset=%zu, need=%zu, body=%zu",
              offset, offset + sizeof(hixl::CtrlMsgType), body.size());
    return hixl::PARAM_INVALID;
  }

  const void *src = static_cast<const void *>(body.data() + offset);
  errno_t rc = memcpy_s(&msg_type, sizeof(msg_type), src, sizeof(msg_type));
  HIXL_CHK_BOOL_RET_STATUS(rc == EOK, hixl::FAILED, "memcpy_s msg_type failed, rc=%d", static_cast<int32_t>(rc));

  offset += sizeof(hixl::CtrlMsgType);
  HIXL_CHK_BOOL_RET_STATUS(msg_type == hixl::CtrlMsgType::kCreateChannelResp, hixl::PARAM_INVALID,
                           "Unexpected msg_type=%d in CreateChannelResp, expect=%d", static_cast<int32_t>(msg_type),
                           static_cast<int32_t>(hixl::CtrlMsgType::kCreateChannelResp));
  return hixl::SUCCESS;
}

hixl::Status ParseCreateChannelResp(const std::vector<uint8_t> &body, size_t offset, hixl::CreateChannelResp &resp) {
  if (offset + sizeof(hixl::CreateChannelResp) > body.size()) {
    HIXL_LOGE(hixl::PARAM_INVALID, "CreateChannelResp body too short, offset=%zu, need=%zu, body=%zu", offset,
              offset + sizeof(hixl::CreateChannelResp), body.size());
    return hixl::PARAM_INVALID;
  }

  const void *src = static_cast<const void *>(body.data() + offset);
  errno_t rc = memcpy_s(&resp, sizeof(resp), src, sizeof(resp));
  HIXL_CHK_BOOL_RET_STATUS(rc == EOK, hixl::FAILED, "memcpy_s createChannelResp failed, rc=%d",
                           static_cast<int32_t>(rc));
  HIXL_LOGD("Parsed CreateChannelResp. result: %d, dst_ep_handle: %lu", resp.result, resp.dst_ep_handle);
  return hixl::SUCCESS;
}

hixl::Status CheckRespResultAndSetHandle(const hixl::CreateChannelResp &resp, uint64_t &dst_endpoint_handle) {
  HIXL_CHK_BOOL_RET_STATUS(resp.result == hixl::SUCCESS, hixl::FAILED,
                           "CreateChannelResp result not SUCCESS, result=%u", static_cast<uint32_t>(resp.result));
  dst_endpoint_handle = resp.dst_ep_handle;
  HIXL_LOGI("CreateChannelResp check passed. dst_endpoint_handle set to %lu", dst_endpoint_handle);
  return hixl::SUCCESS;
}
}  // namespace
namespace hixl {
Status ConnMsgHandler::SendCreateChannelRequest(int32_t socket, const EndpointDesc &src_endpoint,
                                                const EndpointDesc &dst_endpoint) {
  HIXL_EVENT("SendCreateChannelRequest start. socket: %d", socket);
  CtrlMsgHeader header{};
  header.magic = kMagicNumber;
  header.body_size = static_cast<uint64_t>(sizeof(CtrlMsgType) + sizeof(CreateChannelReq));

  CtrlMsgType msg_type = CtrlMsgType::kCreateChannelReq;

  CreateChannelReq body{};
  body.src = src_endpoint;
  body.dst = dst_endpoint;

  Status ret = SendHeaderTypeBody(socket, header, msg_type, &body, static_cast<uint64_t>(sizeof(body)));
  if (ret == SUCCESS) {
    HIXL_EVENT("SendCreateChannelRequest success.");
  } else {
    HIXL_LOGE(ret, "SendCreateChannelRequest failed.");
  }
  return ret;
}

Status ConnMsgHandler::RecvCreateChannelResponse(int32_t socket, uint64_t &dst_endpoint_handle, uint32_t timeout_ms) {
  HIXL_EVENT("RecvCreateChannelResponse start. socket: %d", socket);
  const uint64_t expect_body_size = static_cast<uint64_t>(sizeof(CtrlMsgType) + sizeof(CreateChannelResp));

  CtrlMsgHeader header{};
  HIXL_CHK_STATUS_RET(RecvAndCheckHeader(socket, expect_body_size, header, timeout_ms));

  std::vector<uint8_t> body;
  HIXL_CHK_STATUS_RET(RecvBody(socket, header.body_size, body, timeout_ms));

  size_t offset = 0U;
  CtrlMsgType msg_type{};
  HIXL_CHK_STATUS_RET(ParseMsgType(body, offset, msg_type));

  CreateChannelResp resp{};
  HIXL_CHK_STATUS_RET(ParseCreateChannelResp(body, offset, resp));

  Status ret = CheckRespResultAndSetHandle(resp, dst_endpoint_handle);
  if (ret == SUCCESS) {
    HIXL_EVENT("RecvCreateChannelResponse success. Remote handle: %lu", dst_endpoint_handle);
  } else {
    HIXL_LOGE(ret, "RecvCreateChannelResponse failed during check.");
  }
  return ret;
}

}  // namespace hixl
