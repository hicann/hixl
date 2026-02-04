/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#ifndef CANN_HIXL_SRC_HIXL_CS_HIXL_CTRL_MSG_H_
#define CANN_HIXL_SRC_HIXL_CS_HIXL_CTRL_MSG_H_

#include "hixl_cs.h"
#include "hixl/hixl_types.h"

namespace hixl {
const uint32_t kMagicNumber = 0xA4B3C2D1;
struct CtrlMsgHeader {
  uint32_t magic;
  uint64_t body_size;
};

enum class CtrlMsgType : int32_t {
  kCreateChannelReq = 1,
  kCreateChannelResp = 2,
  kGetRemoteMemReq = 3,
  kGetRemoteMemResp = 4,
  kDestroyChannelReq = 5,
  kGetEndPointInfoReq = 6,
  kGetEndPointInfoResp = 7,
  kEnd
};

struct CtrlMsg {
  CtrlMsgType msg_type;
  std::string msg;
};

struct CreateChannelReq {
  EndpointDesc src;
  EndpointDesc dst;
};

struct CreateChannelResp {
  Status result;
  uint64_t dst_ep_handle = 0UL;
};

struct GetRemoteMemReq {
  uint64_t dst_ep_handle = 0UL;
};

struct HixlMemDesc {
  HcommMem mem;
  std::string tag;
  void *export_desc = nullptr;
  uint32_t export_len = 0U;
};

struct GetRemoteMemResp {
  Status result;
  std::vector<HixlMemDesc> mem_descs;
};

struct DestroyChannelReq {
  uint64_t endpoint_handle;
  uint64_t channel_handle;
};

using CtrlMsgPtr = std::shared_ptr<CtrlMsg>;
using MsgProcessor = std::function<Status(int32_t fd, const char *msg, uint64_t msg_len)>;
}  // namespace hixl

namespace hixl {
HixlStatus HixlCSServerRegProc(HixlServerHandle server_handle, hixl::CtrlMsgType msg_type, hixl::MsgProcessor proc);
}
#endif  // CANN_HIXL_SRC_HIXL_CS_HIXL_CTRL_MSG_H_
