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

#include "common/hixl_cs.h"
#include "hixl/hixl_types.h"
#include "common/hixl_checker.h"
#include "common/hixl_utils.h"
#include "common/hixl_log.h"
#include "common/ctrl_msg.h"

namespace hixl {

class ConnMsgHandler {
 public:
  // 发送 CreateChannelReq，携带本端 src_endpoint 和远端 dst_endpoint
  static Status SendCreateChannelRequest(int32_t socket, const EndpointDesc &src_endpoint, const EndpointDesc &dst_endpoint);
  // 接收 CreateChannelResp，解析出对端 endpoint_handle
  static Status RecvCreateChannelResponse(int32_t socket, uint64_t &dst_endpoint_handle, uint32_t timeout_ms);
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_CS_CONN_MSG_HANDLER_H_
