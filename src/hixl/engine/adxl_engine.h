/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HIXL_SRC_HIXL_ENGINE_ADXL_ENGINE_H_
#define HIXL_SRC_HIXL_ENGINE_ADXL_ENGINE_H_

#include "engine.h"
#include "adxl/adxl_types.h"
#include "adxl/adxl_inner_engine.h"
#include "hixl/hixl_types.h"

namespace hixl {
class AdxlEngine : public Engine {
 public:
  explicit AdxlEngine(const AscendString &local_engine) : Engine(local_engine), adxl_inner_engine_(local_engine) {};

  ~AdxlEngine() override = default;

  Status Initialize(const std::map<AscendString, AscendString> &options) override;

  void Finalize() override;

  bool IsInitialized() const override;

  Status RegisterMem(const MemDesc &mem, MemType type, MemHandle &mem_handle) override;

  Status DeregisterMem(MemHandle mem_handle) override;

  Status Connect(const AscendString &remote_engine, int32_t timeout_in_millis) override;

  Status Disconnect(const AscendString &remote_engine, int32_t timeout_in_millis) override;

  void Disconnect() override;

  Status TransferSync(const AscendString &remote_engine, TransferOp operation,
                      const std::vector<TransferOpDesc> &op_descs, int32_t timeout_in_millis) override;

  Status TransferAsync(const AscendString &remote_engine, TransferOp operation,
                       const std::vector<TransferOpDesc> &op_descs, const TransferArgs &optional_args,
                       TransferReq &req) override;

  Status GetTransferStatus(const TransferReq &req, TransferStatus &status) override;

  Status SendNotify(const AscendString &remote_engine, const NotifyDesc &notify,
                    int32_t timeout_in_millis = 1000) override;

  Status GetNotifies(std::vector<NotifyDesc> &notifies) override;

  Status RegisterCallbackProcessor(int32_t msg_type, CallbackProcessor processor) override;

 private:
  adxl::AdxlInnerEngine adxl_inner_engine_;
};
}  // namespace hixl

#endif  // HIXL_SRC_HIXL_ENGINE_ADXL_ENGINE_H_