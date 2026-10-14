/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "engine/ub_multi_channel_handler.h"
#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/hixl_utils.h"
#include "common/scope_guard.h"

namespace hixl {

UbMultiChannelHandler::UbMultiChannelHandler(const std::string &local_engine, const std::string &remote_engine,
                                             std::map<CommType, HandlerCreateArgs::EndpointPair> link_pairs)
    : UbClientHandler({}, local_engine, remote_engine, std::move(link_pairs)) {}

Status UbMultiChannelHandler::CreateHandlesForType(CommType type, const HixlClientDesc &desc,
                                                   const HixlClientConfig &config, const HandlerCreateArgs &args) {
  MultiChannelDispatcher &dispatcher = dispatchers_[type];
  HIXL_CHK_STATUS_RET(dispatcher.CreateHandles(desc, config, args.MultiWorkerNum(), args.MultiChannelSplitBatchSize()),
                      "UbMultiChannelHandler create workers failed for type:%s", CommTypeToString(type));
  HIXL_EVENT("UbMultiChannelHandler created %u workers for type:%s, split_batch_size:%u", args.MultiWorkerNum(),
             CommTypeToString(type), args.MultiChannelSplitBatchSize());
  return SUCCESS;
}

Status UbMultiChannelHandler::Create(const HandlerCreateArgs &args, std::unique_ptr<UbMultiChannelHandler> &out) {
  auto handler = MakeUnique<UbMultiChannelHandler>(args.local_engine, args.remote_engine,
                                                   std::map<CommType, HandlerCreateArgs::EndpointPair>{});
  HIXL_CHK_BOOL_RET_STATUS(handler != nullptr, FAILED,
                           "UbMultiChannelHandler create failed: MakeUnique returned nullptr");

  // 创建期单线程，可直接调用 Finalize 钩子清理已建 worker
  auto cleanup_fn = [&handler]() {
    if (handler != nullptr) {
      handler->DestroyHandlesLocked();
    }
  };
  HIXL_DISMISSABLE_GUARD(cleanup, cleanup_fn);

  HIXL_CHK_STATUS_RET(handler->InitLinkPairs(args), "UbMultiChannelHandler create links failed");
  HIXL_CHK_STATUS_RET(handler->InitRemoteState(args), "UbMultiChannelHandler init remote state failed");
  HIXL_DISMISS_GUARD(cleanup);
  out = std::move(handler);
  return SUCCESS;
}

std::vector<HixlClientHandle> UbMultiChannelHandler::GetHandlesForType(CommType type) const {
  auto it = dispatchers_.find(type);
  if (it != dispatchers_.end()) {
    return it->second.Handles();
  }
  return {};
}

std::map<CommType, std::vector<HixlClientHandle>> UbMultiChannelHandler::GetAllLinkHandles() const {
  std::map<CommType, std::vector<HixlClientHandle>> all;
  for (const auto &[type, dispatcher] : dispatchers_) {
    all[type] = dispatcher.Handles();
  }
  return all;
}

Status UbMultiChannelHandler::SubmitDescsForType(CommType type, const std::vector<TransferOpDesc> &descs, bool is_get,
                                                 uint32_t timeout_ms, bool is_async, TransferReq &req) {
  auto it = dispatchers_.find(type);
  HIXL_CHK_BOOL_RET_STATUS(it != dispatchers_.end(), FAILED,
                           "[UbMultiChannelHandler] Requested communication type:%s is unavailable, "
                           "local_engine:%s, remote_engine:%s",
                           CommTypeToString(type), local_engine_.c_str(), remote_engine_.c_str());
  const auto mode = is_async ? MultiChannelDispatcher::SubmitMode::ASYNC : MultiChannelDispatcher::SubmitMode::SYNC;
  return it->second.Submit(descs, is_get, timeout_ms, mode, is_async ? &req : nullptr);
}

Status UbMultiChannelHandler::CheckSubReqStatus(const SubReq &sub, TransferStatus &sub_status) {
  auto it = dispatchers_.find(sub.type);
  HIXL_CHK_BOOL_RET_STATUS(it != dispatchers_.end(), FAILED,
                           "[UbMultiChannelHandler] Requested communication type:%s is unavailable, "
                           "local_engine:%s, remote_engine:%s",
                           CommTypeToString(sub.type), local_engine_.c_str(), remote_engine_.c_str());
  return it->second.CheckStatus(sub.req, sub_status);
}

void UbMultiChannelHandler::ClearTransferStateLocked() {
  UbClientHandler::ClearTransferStateLocked();
  for (auto &[type, dispatcher] : dispatchers_) {
    (void)type;
    dispatcher.Reset();
  }
}

void UbMultiChannelHandler::DestroyHandlesLocked() {
  for (auto &[type, dispatcher] : dispatchers_) {
    (void)type;
    dispatcher.DestroyHandles();
  }
}

}  // namespace hixl
