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
#include <cinttypes>
#include <cstddef>
#include <vector>
#include <securec.h>
#include "common/ctrl_msg_plugin.h"
#include "common/hixl_checker.h"
#include "common/hixl_log.h"

namespace {
hixl::Status SendHeaderTypeBody(const hixl::CtrlMsgPluginPtr &connection, const hixl::CtrlMsgHeader &header,
                                hixl::CtrlMsgType msg_type, const void *body, uint64_t body_size) {
  HIXL_LOGI("Start sending header, type and body. connection: %s, body_size: %lu", connection->Desc().c_str(),
            body_size);
  HIXL_CHK_STATUS_RET(connection->Send(&header, static_cast<uint64_t>(sizeof(header))));
  HIXL_CHK_STATUS_RET(connection->Send(&msg_type, static_cast<uint64_t>(sizeof(msg_type))));
  HIXL_CHK_STATUS_RET(connection->Send(body, body_size));
  HIXL_LOGI("Successfully sent header, type and body.");
  return hixl::SUCCESS;
}

hixl::Status RecvAndCheckHeader(const hixl::CtrlMsgPluginPtr &connection, uint64_t expect_body_size,
                                hixl::CtrlMsgHeader &header, uint32_t timeout_ms) {
  HIXL_LOGI("Start receiving and checking header. connection: %s, expect_body_size: %lu", connection->Desc().c_str(),
            expect_body_size);
  HIXL_CHK_STATUS_RET(connection->Recv(&header, static_cast<uint32_t>(sizeof(header)), timeout_ms),
                      "Failed to recv header. connection=%s, len=%lu, timeout=%u ms", connection->Desc().c_str(),
                      sizeof(header), timeout_ms);
  HIXL_LOGD("Header received. magic: 0x%X, body_size: %lu", header.magic, header.body_size);
  HIXL_CHK_BOOL_RET_STATUS(header.magic == hixl::kMagicNumber, hixl::PARAM_INVALID,
                           "Invalid magic for CreateChannelResp, expect:0x%X, actual:0x%X", hixl::kMagicNumber,
                           header.magic);
  HIXL_CHK_BOOL_RET_STATUS(header.body_size == expect_body_size, hixl::PARAM_INVALID,
                           "Invalid body_size in CreateChannelResp, expect:%" PRIu64 ", actual:%" PRIu64,
                           expect_body_size, header.body_size);
  return hixl::SUCCESS;
}

hixl::Status RecvBody(const hixl::CtrlMsgPluginPtr &connection, uint64_t body_size, std::vector<uint8_t> &body,
                      uint32_t timeout_ms) {
  HIXL_LOGI("Start receiving body. connection: %s, body_size: %lu", connection->Desc().c_str(), body_size);
  body.resize(static_cast<size_t>(body_size));
  HIXL_CHK_STATUS_RET(connection->Recv(body.data(), static_cast<uint32_t>(body_size), timeout_ms));
  HIXL_LOGI("Successfully received body.");
  return hixl::SUCCESS;
}

hixl::Status ParseMsgType(const std::vector<uint8_t> &body, size_t &offset, hixl::CtrlMsgType &msg_type,
                          hixl::CtrlMsgType expected) {
  if (offset + sizeof(hixl::CtrlMsgType) > body.size()) {
    HIXL_LOGE(hixl::PARAM_INVALID, "ctrl resp body too short for msg_type, offset=%zu, need=%zu, body=%zu", offset,
              offset + sizeof(hixl::CtrlMsgType), body.size());
    return hixl::PARAM_INVALID;
  }

  const void *src = static_cast<const void *>(body.data() + offset);
  const errno_t rc = memcpy_s(&msg_type, sizeof(msg_type), src, sizeof(msg_type));
  HIXL_CHK_BOOL_RET_STATUS(rc == EOK, hixl::FAILED, "memcpy_s msg_type failed, rc=%d", static_cast<int32_t>(rc));

  offset += sizeof(hixl::CtrlMsgType);
  HIXL_CHK_BOOL_RET_STATUS(msg_type == expected, hixl::PARAM_INVALID, "Unexpected msg_type=%d in ctrl resp, expect=%d",
                           static_cast<int32_t>(msg_type), static_cast<int32_t>(expected));
  return hixl::SUCCESS;
}

hixl::Status ParseCreateChannelResp(const std::vector<uint8_t> &body, size_t offset, hixl::CreateChannelResp &resp) {
  if (offset + sizeof(hixl::CreateChannelResp) > body.size()) {
    HIXL_LOGE(hixl::PARAM_INVALID, "CreateChannelResp body too short, offset=%zu, need=%zu, body=%zu", offset,
              offset + sizeof(hixl::CreateChannelResp), body.size());
    return hixl::PARAM_INVALID;
  }

  const void *src = static_cast<const void *>(body.data() + offset);
  const errno_t rc = memcpy_s(&resp, sizeof(resp), src, sizeof(resp));
  HIXL_CHK_BOOL_RET_STATUS(rc == EOK, hixl::FAILED, "memcpy_s createChannelResp failed, rc=%d",
                           static_cast<int32_t>(rc));
  HIXL_LOGD("Parsed CreateChannelResp. result: %d", static_cast<int32_t>(resp.result));
  return hixl::SUCCESS;
}

hixl::Status ParseMatchEndpointResp(const std::vector<uint8_t> &body, size_t offset, hixl::MatchEndpointResp &resp) {
  if (offset + sizeof(hixl::MatchEndpointResp) > body.size()) {
    HIXL_LOGE(hixl::PARAM_INVALID, "MatchEndpointResp body too short, offset=%zu, need=%zu, body=%zu", offset,
              offset + sizeof(hixl::MatchEndpointResp), body.size());
    return hixl::PARAM_INVALID;
  }
  const void *src = static_cast<const void *>(body.data() + offset);
  const errno_t rc = memcpy_s(&resp, sizeof(resp), src, sizeof(resp));
  HIXL_CHK_BOOL_RET_STATUS(rc == EOK, hixl::FAILED, "memcpy_s MatchEndpointResp failed, rc=%d",
                           static_cast<int32_t>(rc));
  HIXL_LOGD("Parsed MatchEndpointResp. result: %d, dst_ep_handle: %lu, channel_index: %" PRIu64,
            static_cast<int32_t>(resp.result), resp.dst_ep_handle, resp.channel_index);
  return hixl::SUCCESS;
}

hixl::Status RecvMatchEndpointHandleResponse(const hixl::CtrlMsgPluginPtr &connection, uint64_t &remote_endpoint_handle,
                                             uint32_t &remote_listen_port, uint64_t &channel_index,
                                             uint32_t timeout_ms) {
  const uint64_t expect_body_size = static_cast<uint64_t>(sizeof(hixl::CtrlMsgType) + sizeof(hixl::MatchEndpointResp));

  hixl::CtrlMsgHeader header{};
  HIXL_CHK_STATUS_RET(RecvAndCheckHeader(connection, expect_body_size, header, timeout_ms));

  std::vector<uint8_t> body;
  HIXL_CHK_STATUS_RET(RecvBody(connection, header.body_size, body, timeout_ms));

  size_t offset = 0U;
  hixl::CtrlMsgType msg_type{};
  HIXL_CHK_STATUS_RET(ParseMsgType(body, offset, msg_type, hixl::CtrlMsgType::kMatchEndpointResp));

  hixl::MatchEndpointResp resp{};
  HIXL_CHK_STATUS_RET(ParseMatchEndpointResp(body, offset, resp));

  HIXL_CHK_BOOL_RET_STATUS(resp.result == hixl::SUCCESS, hixl::FAILED,
                           "MatchEndpointResp result not SUCCESS, result=%u", static_cast<uint32_t>(resp.result));
  remote_endpoint_handle = resp.dst_ep_handle;
  remote_listen_port = resp.port;
  channel_index = resp.channel_index;
  HIXL_LOGI(
      "MatchEndpointResp check passed. remote_endpoint_handle set to %lu, remote_listen_port=%u, "
      "channel_index=%" PRIu64,
      remote_endpoint_handle, remote_listen_port, channel_index);
  return hixl::SUCCESS;
}

hixl::Status RecvCreateChannelOnlyResponse(const hixl::CtrlMsgPluginPtr &connection, uint32_t timeout_ms) {
  const uint64_t expect_body_size = static_cast<uint64_t>(sizeof(hixl::CtrlMsgType) + sizeof(hixl::CreateChannelResp));

  hixl::CtrlMsgHeader header{};
  HIXL_CHK_STATUS_RET(RecvAndCheckHeader(connection, expect_body_size, header, timeout_ms));

  std::vector<uint8_t> body;
  HIXL_CHK_STATUS_RET(RecvBody(connection, header.body_size, body, timeout_ms));

  size_t offset = 0U;
  hixl::CtrlMsgType msg_type{};
  HIXL_CHK_STATUS_RET(ParseMsgType(body, offset, msg_type, hixl::CtrlMsgType::kCreateChannelResp));

  hixl::CreateChannelResp resp{};
  HIXL_CHK_STATUS_RET(ParseCreateChannelResp(body, offset, resp));

  HIXL_CHK_BOOL_RET_STATUS(resp.result == hixl::SUCCESS, hixl::FAILED,
                           "CreateChannelResp result not SUCCESS, result=%u", static_cast<uint32_t>(resp.result));
  return hixl::SUCCESS;
}
}  // namespace
namespace hixl {
Status ConnMsgHandler::SendMatchEndpointRequest(const CtrlMsgPluginPtr &connection, const EndpointDesc &dst) {
  HIXL_EVENT("SendMatchEndpointRequest start. connection: %s", connection->Desc().c_str());
  CtrlMsgHeader header{};
  header.magic = kMagicNumber;
  header.body_size = static_cast<uint64_t>(sizeof(CtrlMsgType) + sizeof(MatchEndpointReq));
  const CtrlMsgType msg_type = CtrlMsgType::kMatchEndpointReq;
  MatchEndpointReq body{};
  body.dst = dst;
  const Status ret = SendHeaderTypeBody(connection, header, msg_type, &body, static_cast<uint64_t>(sizeof(body)));
  if (ret == SUCCESS) {
    HIXL_EVENT("SendMatchEndpointRequest success.");
  } else {
    HIXL_LOGE(ret, "SendMatchEndpointRequest failed.");
  }
  return ret;
}

Status ConnMsgHandler::RecvMatchEndpointResponse(const CtrlMsgPluginPtr &connection, uint64_t &remote_endpoint_handle,
                                                 uint32_t &remote_listen_port, uint64_t &channel_index,
                                                 uint32_t timeout_ms) {
  HIXL_EVENT("RecvMatchEndpointResponse start. connection: %s", connection->Desc().c_str());
  const Status ret = RecvMatchEndpointHandleResponse(connection, remote_endpoint_handle, remote_listen_port,
                                                     channel_index, timeout_ms);
  if (ret == SUCCESS) {
    HIXL_EVENT("RecvMatchEndpointResponse success. Remote handle: %lu, channel_index=%" PRIu64, remote_endpoint_handle,
               channel_index);
  } else {
    HIXL_LOGE(ret, "RecvMatchEndpointResponse failed.");
  }
  return ret;
}

Status ConnMsgHandler::SendCreateChannelRequest(const CtrlMsgPluginPtr &connection, const CreateChannelReq &body) {
  HIXL_EVENT("SendCreateChannelRequest start. connection: %s", connection->Desc().c_str());
  CtrlMsgHeader header{};
  header.magic = kMagicNumber;
  header.body_size = static_cast<uint64_t>(sizeof(CtrlMsgType) + sizeof(CreateChannelReq));
  const CtrlMsgType msg_type = CtrlMsgType::kCreateChannelReq;
  const Status ret = SendHeaderTypeBody(connection, header, msg_type, &body, static_cast<uint64_t>(sizeof(body)));
  if (ret == SUCCESS) {
    HIXL_EVENT("SendCreateChannelRequest success.");
  } else {
    HIXL_LOGE(ret, "SendCreateChannelRequest failed.");
  }
  return ret;
}

Status ConnMsgHandler::RecvCreateChannelResponse(const CtrlMsgPluginPtr &connection, uint32_t timeout_ms) {
  HIXL_EVENT("RecvCreateChannelResponse start. connection: %s", connection->Desc().c_str());
  const Status ret = RecvCreateChannelOnlyResponse(connection, timeout_ms);
  if (ret == SUCCESS) {
    HIXL_EVENT("RecvCreateChannelResponse success.");
  } else {
    HIXL_LOGE(ret, "RecvCreateChannelResponse failed during check.");
  }
  return ret;
}

Status ConnMsgHandler::SendChannelHandshakeRequest(const CtrlMsgPluginPtr &connection,
                                                   const std::vector<uint8_t> &payload) {
  HIXL_CHECK_NOTNULL(connection);
  HIXL_CHK_BOOL_RET_STATUS(payload.size() <= kMaxChannelHandshakePayloadBytes, PARAM_INVALID,
                           "ChannelHandshakeReq payload too large, size=%zu", payload.size());
  ChannelHandshakeHeader handshake_hdr{};
  handshake_hdr.result = SUCCESS;
  handshake_hdr.done = 0U;
  handshake_hdr.payload_len = static_cast<uint32_t>(payload.size());
  CtrlMsgHeader header{};
  header.magic = kMagicNumber;
  header.body_size = static_cast<uint64_t>(sizeof(CtrlMsgType) + sizeof(ChannelHandshakeHeader) + payload.size());
  const CtrlMsgType msg_type = CtrlMsgType::kChannelHandshakeReq;
  const size_t total_size = sizeof(header) + sizeof(msg_type) + sizeof(handshake_hdr) + payload.size();
  std::vector<uint8_t> frame(total_size);
  size_t offset = 0U;
  HIXL_CHK_BOOL_RET_STATUS(memcpy_s(frame.data() + offset, total_size - offset, &header, sizeof(header)) == EOK, FAILED,
                           "memcpy_s CtrlMsgHeader failed");
  offset += sizeof(header);
  HIXL_CHK_BOOL_RET_STATUS(memcpy_s(frame.data() + offset, total_size - offset, &msg_type, sizeof(msg_type)) == EOK,
                           FAILED, "memcpy_s CtrlMsgType failed");
  offset += sizeof(msg_type);
  HIXL_CHK_BOOL_RET_STATUS(
      memcpy_s(frame.data() + offset, total_size - offset, &handshake_hdr, sizeof(handshake_hdr)) == EOK, FAILED,
      "memcpy_s ChannelHandshakeHeader failed");
  offset += sizeof(handshake_hdr);
  if (!payload.empty()) {
    HIXL_CHK_BOOL_RET_STATUS(
        memcpy_s(frame.data() + offset, total_size - offset, payload.data(), payload.size()) == EOK, FAILED,
        "memcpy_s ChannelHandshakeReq payload failed");
  }
  HIXL_CHK_STATUS_RET(connection->Send(frame.data(), static_cast<uint64_t>(frame.size())));
  return SUCCESS;
}

Status ConnMsgHandler::RecvChannelHandshakeResponse(const CtrlMsgPluginPtr &connection, uint32_t timeout_ms,
                                                    std::vector<uint8_t> &payload, bool &done) {
  HIXL_CHECK_NOTNULL(connection);
  payload.clear();
  done = false;
  CtrlMsgHeader header{};
  HIXL_CHK_STATUS_RET(connection->Recv(&header, static_cast<uint32_t>(sizeof(header)), timeout_ms),
                      "Failed to recv ChannelHandshakeResp header. connection=%s, timeout=%u ms",
                      connection->Desc().c_str(), timeout_ms);
  HIXL_CHK_BOOL_RET_STATUS(header.magic == kMagicNumber, PARAM_INVALID,
                           "Invalid magic for ChannelHandshakeResp, expect:0x%X, actual:0x%X", kMagicNumber,
                           header.magic);
  const uint64_t min_body = static_cast<uint64_t>(sizeof(CtrlMsgType) + sizeof(ChannelHandshakeHeader));
  const uint64_t max_body = min_body + static_cast<uint64_t>(kMaxChannelHandshakePayloadBytes);
  HIXL_CHK_BOOL_RET_STATUS(header.body_size >= min_body && header.body_size <= max_body, PARAM_INVALID,
                           "Invalid body_size in ChannelHandshakeResp, actual:%" PRIu64, header.body_size);
  std::vector<uint8_t> body;
  HIXL_CHK_STATUS_RET(RecvBody(connection, header.body_size, body, timeout_ms));
  size_t offset = 0U;
  CtrlMsgType msg_type{};
  HIXL_CHK_STATUS_RET(ParseMsgType(body, offset, msg_type, CtrlMsgType::kChannelHandshakeResp));
  ChannelHandshakeHeader resp{};
  const errno_t rc = memcpy_s(&resp, sizeof(resp), body.data() + offset, sizeof(resp));
  HIXL_CHK_BOOL_RET_STATUS(rc == EOK, FAILED, "memcpy_s ChannelHandshakeHeader failed, rc=%d",
                           static_cast<int32_t>(rc));
  offset += sizeof(resp);
  HIXL_CHK_BOOL_RET_STATUS(resp.payload_len == body.size() - offset, PARAM_INVALID,
                           "ChannelHandshakeResp payload_len mismatch, header=%u, body=%zu", resp.payload_len,
                           body.size() - offset);
  if (resp.result != SUCCESS) {
    return resp.result;
  }
  done = (resp.done != 0U);
  if (resp.payload_len > 0U) {
    payload.assign(body.begin() + static_cast<std::ptrdiff_t>(offset), body.end());
  }
  return SUCCESS;
}

}  // namespace hixl
