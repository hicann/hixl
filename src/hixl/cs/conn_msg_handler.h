/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_CS_CONN_MSG_HANDLER_H_
#define CANN_HIXL_SRC_HIXL_CS_CONN_MSG_HANDLER_H_

#include <cstdint>
#include <vector>

#include "cs/hixl_cs.h"
#include "hixl/hixl_types.h"
#include "common/ctrl_msg.h"

namespace hixl {

class ConnMsgHandler {
 public:
  static Status SendMatchEndpointRequest(const CtrlMsgPluginPtr &connection, const EndpointDesc &dst);
  static Status RecvMatchEndpointResponse(const CtrlMsgPluginPtr &connection, uint64_t &remote_endpoint_handle,
                                          uint32_t &remote_listen_port, uint64_t &channel_index, uint32_t timeout_ms);
  // 发送 CreateChannelReq：src、dst_ep_handle、tc、sl、channel_index（role 由收发两端各自固定）
  static Status SendCreateChannelRequest(const CtrlMsgPluginPtr &connection, const CreateChannelReq &body);
  static Status RecvCreateChannelResponse(const CtrlMsgPluginPtr &connection, uint32_t timeout_ms);
  static Status SendChannelHandshakeRequest(const CtrlMsgPluginPtr &connection, const std::vector<uint8_t> &payload);
  static Status RecvChannelHandshakeResponse(const CtrlMsgPluginPtr &connection, uint32_t timeout_ms,
                                             std::vector<uint8_t> &payload, bool &done);
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_CS_CONN_MSG_HANDLER_H_
