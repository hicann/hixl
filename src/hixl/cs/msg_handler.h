/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_CS_MSG_HANDLER_H_
#define CANN_HIXL_SRC_HIXL_CS_MSG_HANDLER_H_

#include <queue>
#include <mutex>
#include <condition_variable>
#include "common/hixl_cs.h"
#include "hixl/hixl_types.h"
#include "common/thread_pool.h"
#include "common/ctrl_msg.h"

namespace hixl {
class MsgHandler {
 public:
  Status Initialize();
  void Finalize();
  void SubmitMsg(int32_t fd, const CtrlMsgPtr &msg);
  Status RegisterMsgProcessor(CtrlMsgType msg_type, MsgProcessor msg_processor);

 private:
  void HandleMsg();
  static Status HandleMsg(int32_t fd, CtrlMsgPtr msg, MsgProcessor proc);

  std::mutex req_mutex_;
  std::queue<std::pair<int32_t, CtrlMsgPtr>> req_queue_;
  std::condition_variable req_cv_;
  std::map<CtrlMsgType, MsgProcessor> processors_;

  std::unique_ptr<ThreadPool> thread_pool_ = nullptr;
  std::atomic<bool> running_{false};
  std::thread listener_;
  aclrtContext ctx_ = nullptr;
};
}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_CS_MSG_HANDLER_H_
