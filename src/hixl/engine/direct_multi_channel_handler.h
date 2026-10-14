/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_ENGINE_DIRECT_MULTI_CHANNEL_HANDLER_H_
#define CANN_HIXL_SRC_HIXL_ENGINE_DIRECT_MULTI_CHANNEL_HANDLER_H_

#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
#include "common/hixl_inner_types.h"
#include "engine/client_handler.h"
#include "engine/client_handler_factory.h"
#include "engine/multi_channel_dispatcher.h"

namespace hixl {

class DirectMultiChannelHandler final : public IClientHandler {
 public:
  static Status Create(const HandlerCreateArgs &args, std::unique_ptr<DirectMultiChannelHandler> &out);
  ~DirectMultiChannelHandler() override = default;

  Status Connect(uint32_t timeout_ms) override;
  Status RegisterMem(const MemHandleInfo &mem_info) override;
  Status DeregisterMem(MemHandle mem_handle) override;
  Status TransferAsync(const std::vector<TransferOpDesc> &op_descs, TransferOp operation, TransferReq &req) override;
  Status TransferSync(const std::vector<TransferOpDesc> &op_descs, TransferOp operation, uint32_t timeout_ms) override;
  Status GetTransferStatus(const TransferReq &req, TransferStatus &status) override;
  Status Finalize() override;
  void Dump(const char *reason, DumpLogLevel level = DumpLogLevel::EVENT) const override;

  static uint32_t ResolveActualWorkerNum(uint32_t configured_n, uint32_t desc_count,
                                         uint32_t split_batch_size = kDefaultSplitBatchSize) {
    return MultiChannelDispatcher::ResolveActualWorkerNum(configured_n, desc_count, split_batch_size);
  }
  static void SplitDescs(const std::vector<TransferOpDesc> &descs, uint32_t worker_num,
                         std::vector<std::vector<HixlOneSideOpDesc>> &worker_descs) {
    MultiChannelDispatcher::SplitDescs(descs, worker_num, worker_descs);
  }

 public:
  explicit DirectMultiChannelHandler(const std::string &local_engine = "", const std::string &remote_engine = "",
                                     const HandlerCreateArgs::EndpointPair &pair = HandlerCreateArgs::EndpointPair{});

 private:
  MultiChannelDispatcher dispatcher_;
  Status UnregOneMemHandle(HixlClientHandle handle, MemHandle mh, MemHandle mem_handle) const;
  void EraseMemHandle(HixlClientHandle handle, MemHandle mh);
  std::vector<std::pair<HixlClientHandle, MemHandle>> mem_handles_;
  std::map<MemHandle, std::vector<std::pair<HixlClientHandle, MemHandle>>> handle_to_mem_handles_;
  std::string local_engine_;
  std::string remote_engine_;
  HandlerCreateArgs::EndpointPair pair_{};
  bool is_connected_{false};
  mutable std::mutex mutex_;
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_ENGINE_DIRECT_MULTI_CHANNEL_HANDLER_H_
