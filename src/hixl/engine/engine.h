/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HIXL_SRC_HIXL_ENGINE_ENGINE_H_
#define HIXL_SRC_HIXL_ENGINE_ENGINE_H_

#include "hixl/hixl_types.h"

namespace hixl {
using CallbackProcessor = std::function<Status(int32_t fd, const char *msg, uint64_t msg_len, bool &keep_fd)>;

class Engine {
 public:
  explicit Engine(const AscendString &local_engine) : local_engine_(local_engine.GetString()) {};

  virtual ~Engine() = default;

  virtual Status Initialize(const std::map<AscendString, AscendString> &options) = 0;

  virtual void Finalize() = 0;

  virtual bool IsInitialized() const = 0;

  virtual Status RegisterMem(const MemDesc &mem, MemType type, MemHandle &mem_handle) = 0;

  virtual Status DeregisterMem(MemHandle mem_handle) = 0;

  virtual Status Connect(const AscendString &remote_engine, int32_t timeout_in_millis) = 0;

  virtual Status Disconnect(const AscendString &remote_engine, int32_t timeout_in_millis) = 0;

  virtual Status TransferSync(const AscendString &remote_engine, TransferOp operation,
                              const std::vector<TransferOpDesc> &op_descs, int32_t timeout_in_millis) = 0;

  virtual Status TransferAsync(const AscendString &remote_engine, TransferOp operation,
                               const std::vector<TransferOpDesc> &op_descs, const TransferArgs &optional_args,
                               TransferReq &req) = 0;

  virtual Status GetTransferStatus(const TransferReq &req, TransferStatus &status) = 0;

  virtual Status SendNotify(const AscendString &remote_engine, const NotifyDesc &notify,
                            int32_t timeout_in_millis = 1000) = 0;

  virtual Status GetNotifies(std::vector<NotifyDesc> &notifies) = 0;

  virtual Status RegisterCallbackProcessor(int32_t msg_type, CallbackProcessor processor) = 0;

 private:
  std::string local_engine_;
};
}  // namespace hixl

#endif  // HIXL_SRC_HIXL_ENGINE_ENGINE_H_