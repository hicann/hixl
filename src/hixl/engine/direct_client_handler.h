/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_ENGINE_DIRECT_CLIENT_HANDLER_H_
#define CANN_HIXL_SRC_HIXL_ENGINE_DIRECT_CLIENT_HANDLER_H_

#include <map>
#include <mutex>
#include <vector>
#include "engine/client_handler.h"
#include "engine/client_handler_factory.h"

namespace hixl {

class DirectClientHandler : public IClientHandler {
 public:
  static Status Create(const HandlerCreateArgs &args, std::unique_ptr<DirectClientHandler> &out);
  ~DirectClientHandler() override = default;

  Status Connect(uint32_t timeout_ms) override;
  Status RegisterMem(const MemHandleInfo &mem_info) override;
  Status TransferAsync(const std::vector<TransferOpDesc> &op_descs, TransferOp operation, TransferReq &req) override;
  Status TransferSync(const std::vector<TransferOpDesc> &op_descs, TransferOp operation, uint32_t timeout_ms) override;
  Status GetTransferStatus(const TransferReq &req, TransferStatus &status) override;
  Status Finalize() override;
  void Dump(const char *reason, DumpLogLevel level = DumpLogLevel::EVENT) const override;

 public:
  explicit DirectClientHandler(HixlClientHandle handle, const std::string &local_engine = "",
                               const std::string &remote_engine = "",
                               const HandlerCreateArgs::EndpointPair &pair = HandlerCreateArgs::EndpointPair{});

 private:
  HixlClientHandle handle_;
  std::string local_engine_;
  std::string remote_engine_;
  HandlerCreateArgs::EndpointPair pair_{};
  bool is_connected_{false};
  std::vector<MemHandle> mem_handles_;
  std::map<TransferReq, CompleteHandle> complete_handles_;
  mutable std::mutex mutex_;
  mutable std::mutex complete_handles_mutex_;
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_ENGINE_DIRECT_CLIENT_HANDLER_H_
