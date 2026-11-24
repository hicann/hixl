/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "channel.h"

#include <chrono>

#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/hixl_utils.h"
#include "common/scope_guard.h"
#include "conn_msg_handler.h"
#include "endpoint.h"
#include "hixl_mem_store.h"

namespace hixl {
namespace {
constexpr size_t kMaxChannelHandshakeSteps = 16U;

uint32_t RemainingMs(std::chrono::steady_clock::time_point deadline) {
  const auto remaining =
      std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
  return remaining <= 0 ? 0U : static_cast<uint32_t>(remaining);
}
}  // namespace

Status Channel::Connect(const ChannelDesc &channel_desc, uint32_t timeout_ms, const CtrlMsgPluginPtr &connection) {
  if (connection == nullptr) {
    return DoConnect(channel_desc, timeout_ms);
  }
  HIXL_CHK_STATUS_RET(EnsureInitialized("Connect"));
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  const CreateChannelReq create_body{ctx_.local_endpoint->GetEndpoint(),
                                     channel_desc.remote_endpoint_handle,
                                     channel_desc.tc,
                                     channel_desc.sl,
                                     channel_desc.retry_cnt,
                                     channel_desc.retry_interval,
                                     channel_desc.channel_index,
                                     channel_desc.qos,
                                     timeout_ms};
  HIXL_CHK_STATUS_RET(ConnMsgHandler::SendCreateChannelRequest(connection, create_body),
                      "[Channel] SendCreateChannelRequest failed. connection=%s", connection->Desc().c_str());
  HIXL_CHK_STATUS_RET(DoConnect(channel_desc, timeout_ms), "[Channel] Connect failed. Dst[id:0x%x]",
                      channel_desc.remote_endpoint.commAddr.id);
  HIXL_DISMISSABLE_GUARD(channel_rollback, ([this]() {
                           const Status ret = Disconnect();
                           if (ret != SUCCESS) {
                             HIXL_LOGW("[Channel] Rollback channel failed. ret=%u", static_cast<uint32_t>(ret));
                           }
                         }));
  HIXL_CHK_STATUS_RET(ConnMsgHandler::RecvCreateChannelResponse(connection, timeout_ms),
                      "[Channel] RecvCreateChannelResponse failed. connection=%s, timeout=%u ms",
                      connection->Desc().c_str(), timeout_ms);
  std::vector<uint8_t> in_payload;
  std::vector<uint8_t> out_payload;
  bool done = false;
  bool exchanged = false;
  bool last_server_done = false;
  for (size_t step = 0U; step < kMaxChannelHandshakeSteps; ++step) {
    out_payload.clear();
    HIXL_CHK_STATUS_RET(StepClientHandshake(channel_desc, in_payload, out_payload, done),
                        "[Channel] StepClientHandshake failed. step=%zu", step);
    HIXL_CHK_BOOL_RET_STATUS(!out_payload.empty() || done, FAILED,
                             "[Channel] Handshake step produced no payload and did not complete, step=%zu", step);
    if (!out_payload.empty()) {
      HIXL_CHK_BOOL_RET_STATUS(RemainingMs(deadline) > 0U, TIMEOUT,
                               "[Channel] Handshake deadline expired before send, step=%zu", step);
      HIXL_CHK_STATUS_RET(ConnMsgHandler::SendChannelHandshakeRequest(connection, out_payload),
                          "[Channel] SendChannelHandshakeRequest failed. connection=%s, step=%zu",
                          connection->Desc().c_str(), step);
      const uint32_t recv_timeout = RemainingMs(deadline);
      HIXL_CHK_BOOL_RET_STATUS(recv_timeout > 0U, TIMEOUT, "[Channel] Handshake deadline expired before recv, step=%zu",
                               step);
      in_payload.clear();
      bool server_done = false;
      HIXL_CHK_STATUS_RET(
          ConnMsgHandler::RecvChannelHandshakeResponse(connection, recv_timeout, in_payload, server_done),
          "[Channel] RecvChannelHandshakeResponse failed. connection=%s, step=%zu", connection->Desc().c_str(), step);
      exchanged = true;
      last_server_done = server_done;
    }
    if (done) {
      HIXL_CHK_BOOL_RET_STATUS(!exchanged || last_server_done, FAILED,
                               "[Channel] Handshake completed on client before server signaled done, step=%zu", step);
      HIXL_DISMISS_GUARD(channel_rollback);
      return SUCCESS;
    }
  }
  HIXL_LOGE(UNSUPPORTED, "[Channel] Handshake exceeded max steps=%zu", kMaxChannelHandshakeSteps);
  return UNSUPPORTED;
}

Status Channel::StepClientHandshake(const ChannelDesc &channel_desc, const std::vector<uint8_t> &in_payload,
                                    std::vector<uint8_t> &out_payload, bool &done) {
  (void)channel_desc;
  (void)in_payload;
  out_payload.clear();
  done = true;
  return SUCCESS;
}

Status Channel::StepServerHandshake(const std::vector<uint8_t> &in_payload, std::vector<uint8_t> &out_payload,
                                    bool &done) {
  (void)in_payload;
  out_payload.clear();
  done = false;
  return UNSUPPORTED;
}

Status Channel::InitContext(const ChannelContext &ctx) {
  ctx_ = ctx;
  HIXL_CHK_BOOL_RET_STATUS(ctx_.local_endpoint != nullptr, FAILED, "[Channel] Initialize with null local endpoint");
  HIXL_CHK_BOOL_RET_STATUS(ctx_.mem_store != nullptr, FAILED, "[Channel] Initialize with null mem store");
  HIXL_CHK_BOOL_RET_STATUS(ctx_.global_config != nullptr, FAILED, "[Channel] Initialize with null global config");
  initialized_ = true;
  return SUCCESS;
}

Status Channel::EnsureInitialized(const char *op) const {
  HIXL_CHK_BOOL_RET_STATUS(initialized_, FAILED,
                           "[Channel] %s rejected, channel is not initialized; call Initialize() first", op);
  return SUCCESS;
}

Status Channel::ValidateAddress(uint32_t list_num, const HixlOneSideOpDesc *desc_list) const {
  // hccs/ubmem:device链路不经hcomm注册本地内存，跳过本地内存校验
  const EndpointDesc &local_ep = ctx_.local_endpoint->GetEndpoint();
  const bool check_local_mem = !((local_ep.protocol == COMM_PROTOCOL_HCCS || IsUbMemProtocol(local_ep.protocol)) &&
                                 local_ep.loc.locType == ENDPOINT_LOC_TYPE_DEVICE);
  HIXL_CHK_STATUS_RET(ctx_.mem_store->BatchValidateMemoryAccess(list_num, desc_list, check_local_mem),
                      "Validate address failed, list_num=%u", list_num);
  return SUCCESS;
}

void Channel::LatchTransferFailure(Status ret) {
  if (transfer_failure_latched_) {
    return;
  }
  transfer_failure_latched_ = true;
  transfer_failure_status_ = ret;
  HIXL_LOGE(ret, "[Channel] Transfer failure latched, status=%u", static_cast<uint32_t>(ret));
}

Status Channel::LatchTransferFailureIfNeeded(Status ret) {
  if (ret != SUCCESS && ret != RESOURCE_EXHAUSTED) {
    LatchTransferFailure(ret);
  }
  return ret;
}

Status Channel::BatchTransferSync(bool is_get, const char *op, const HixlOneSideOpDesc *desc_list, uint32_t list_num,
                                  uint32_t timeout_ms) {
  HIXL_CHK_STATUS_RET(EnsureInitialized(op), "[Channel] EnsureInitialized failed, op=%s", op);
  HIXL_CHK_BOOL_RET_STATUS(!transfer_failure_latched_, FAILED,
                           "[Channel] %s rejected after prior transfer failure. last_status=%u, list_num=%u", op,
                           static_cast<uint32_t>(transfer_failure_status_), list_num);
  HIXL_CHK_STATUS_RET(ValidateAddress(list_num, desc_list), "[Channel] ValidateAddress failed, op=%s", op);
  return LatchTransferFailureIfNeeded(DoBatchTransferSync(is_get, desc_list, list_num, timeout_ms));
}

Status Channel::BatchTransferAsync(bool is_get, const char *op, const HixlOneSideOpDesc *desc_list, uint32_t list_num,
                                   void **complete_handle) {
  HIXL_CHK_STATUS_RET(EnsureInitialized(op), "[Channel] EnsureInitialized failed, op=%s", op);
  HIXL_CHK_BOOL_RET_STATUS(!transfer_failure_latched_, FAILED,
                           "[Channel] %s rejected after prior transfer failure. last_status=%u, list_num=%u", op,
                           static_cast<uint32_t>(transfer_failure_status_), list_num);
  HIXL_CHK_STATUS_RET(ValidateAddress(list_num, desc_list), "[Channel] ValidateAddress failed, op=%s", op);
  return LatchTransferFailureIfNeeded(DoBatchTransferAsync(is_get, desc_list, list_num, complete_handle));
}

Status Channel::BatchPutSync(const HixlOneSideOpDesc *desc_list, uint32_t list_num, uint32_t timeout_ms) {
  return BatchTransferSync(false, "BatchPutSync", desc_list, list_num, timeout_ms);
}

Status Channel::BatchGetSync(const HixlOneSideOpDesc *desc_list, uint32_t list_num, uint32_t timeout_ms) {
  return BatchTransferSync(true, "BatchGetSync", desc_list, list_num, timeout_ms);
}

Status Channel::BatchPutAsync(const HixlOneSideOpDesc *desc_list, uint32_t list_num, void **complete_handle) {
  return BatchTransferAsync(false, "BatchPutAsync", desc_list, list_num, complete_handle);
}

Status Channel::BatchGetAsync(const HixlOneSideOpDesc *desc_list, uint32_t list_num, void **complete_handle) {
  return BatchTransferAsync(true, "BatchGetAsync", desc_list, list_num, complete_handle);
}

}  // namespace hixl
