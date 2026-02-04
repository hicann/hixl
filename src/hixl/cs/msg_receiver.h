/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_CS_MSG_RECEIVER_H_
#define CANN_HIXL_SRC_HIXL_CS_MSG_RECEIVER_H_

#include <vector>
#include "common/hixl_cs.h"
#include "hixl/hixl_types.h"
#include "common/ctrl_msg.h"

namespace hixl {
enum class RecvState : int32_t {
  WAITING_FOR_HEADER,
  WAITING_FOR_BODY
};

class MsgReceiver {
 public:
  explicit MsgReceiver(int32_t fd) : fd_(fd) {};
  Status IRecv(std::vector<CtrlMsgPtr> &msgs);

 private:
  Status RecvHeader();
  bool CheckDisconnect(ssize_t recv_size) const;

  int32_t fd_ = -1;
  RecvState recv_state_ = RecvState::WAITING_FOR_HEADER;
  std::vector<char> recv_buffer_;
  size_t received_size_ = 0U;
  size_t expected_size_ = 0U;
};
}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_CS_MSG_RECEIVER_H_
