/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hixl_cs_server.h"
#include "common/hixl_checker.h"
#include "common/ctrl_msg.h"

namespace hixl {
void MsgHandler::SubmitMsg(int32_t fd, const CtrlMsgPtr &msg) {
  {
    std::lock_guard<std::mutex> lock(req_mutex_);
    req_queue_.emplace(fd, msg);
  }
  req_cv_.notify_one();
}

Status MsgHandler::Initialize() {
  constexpr uint32_t kThreadPoolSize = 4U;
  thread_pool_ = MakeUnique<ThreadPool>("cs_server", kThreadPoolSize);
  HIXL_CHECK_NOTNULL(thread_pool_);
  running_ = true;

  HIXL_CHK_ACL_RET(aclrtGetCurrentContext(&ctx_));
  HIXL_LOGI("aclrtGetCurrentContext ctx=%p", ctx_);
  listener_ = std::thread([this]() {
    HandleMsg();
  });
  return SUCCESS;
}

void MsgHandler::Finalize() {
  running_ = false;
  req_cv_.notify_one();
  if (listener_.joinable()) {
    listener_.join();
  }
  thread_pool_->Destroy();
}

Status MsgHandler::HandleMsg(int32_t fd, CtrlMsgPtr msg, MsgProcessor proc) {
  HIXL_EVENT("[HixlServer] handle msg begin, msg type:%d, msg size:%zu",
             static_cast<int32_t>(msg->msg_type), msg->msg.size());
  HIXL_CHK_STATUS_RET(proc(fd, msg->msg.c_str(), msg->msg.size()), "Failed to handle msg");
  HIXL_EVENT("[HixlServer] handle msg success, msg type:%d, msg size:%zu",
             static_cast<int32_t>(msg->msg_type), msg->msg.size());
  return SUCCESS;
}

Status MsgHandler::RegisterMsgProcessor(CtrlMsgType msg_type, MsgProcessor msg_processor) {
  const auto it = processors_.find(msg_type);
  HIXL_CHK_BOOL_RET_STATUS(it == processors_.cend(), PARAM_INVALID, "msg_type:%d, has beeen registed.",
                           static_cast<int32_t>(msg_type));
  processors_[msg_type] = msg_processor;
  return SUCCESS;
}

void MsgHandler::HandleMsg() {
  while (running_.load()) {
    std::pair<int32_t, CtrlMsgPtr> req;
    {
      std::unique_lock<std::mutex> lock(req_mutex_);
      req_cv_.wait(lock, [this] { return !req_queue_.empty() || !running_.load(); });
      if (!running_.load()) {
        break;
      }
      req = std::move(req_queue_.front());
      req_queue_.pop();
    }
    const auto it = processors_.find(req.second->msg_type);
    if (it == processors_.cend()) {
      HIXL_EVENT("[HixlServer] msg type:%d, not registed", static_cast<int32_t>(req.second->msg_type));
      continue;
    }
    auto proc = it->second;
    (void)thread_pool_->commit([this, req, proc]() -> void {
      if (ctx_ != nullptr) {
        aclrtSetCurrentContext(ctx_);
      }
      (void)HandleMsg(req.first, req.second, proc);
    });
  }
}
}  // namespace hixl
