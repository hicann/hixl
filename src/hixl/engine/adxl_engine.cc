/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "adxl_engine.h"

namespace hixl {
Status AdxlEngine::Initialize(const std::map<AscendString, AscendString> &options) {
  return adxl_inner_engine_.Initialize(options);
}

void AdxlEngine::Finalize() {
  adxl_inner_engine_.Finalize();
}

bool AdxlEngine::IsInitialized() const {
  return adxl_inner_engine_.IsInitialized();
}

Status AdxlEngine::RegisterMem(const MemDesc &mem, MemType type, MemHandle &mem_handle) {
  adxl::MemDesc adxl_mem{mem.addr, mem.len};
  adxl::MemType adxl_type = static_cast<adxl::MemType>(type);
  return adxl_inner_engine_.RegisterMem(adxl_mem, adxl_type, mem_handle);
}

Status AdxlEngine::DeregisterMem(MemHandle mem_handle) {
  return adxl_inner_engine_.DeregisterMem(mem_handle);
}

Status AdxlEngine::Connect(const AscendString &remote_engine, int32_t timeout_in_millis) {
  return adxl_inner_engine_.Connect(remote_engine, timeout_in_millis);
}

Status AdxlEngine::Disconnect(const AscendString &remote_engine, int32_t timeout_in_millis) {
  return adxl_inner_engine_.Disconnect(remote_engine, timeout_in_millis);
}

Status AdxlEngine::TransferSync(const AscendString &remote_engine, TransferOp operation,
                                const std::vector<TransferOpDesc> &op_descs, int32_t timeout_in_millis) {
  adxl::TransferOp adxl_operation = static_cast<adxl::TransferOp>(operation);
  std::vector<adxl::TransferOpDesc> adxl_op_descs;
  for (const auto &op_desc : op_descs) {
    adxl::TransferOpDesc temp{op_desc.local_addr, op_desc.remote_addr, op_desc.len};
    adxl_op_descs.push_back(temp);
  }
  return adxl_inner_engine_.TransferSync(remote_engine, adxl_operation, adxl_op_descs, timeout_in_millis);
}

Status AdxlEngine::TransferAsync(const AscendString &remote_engine, TransferOp operation,
                                 const std::vector<TransferOpDesc> &op_descs, const TransferArgs &optional_args,
                                 TransferReq &req) {
  (void)optional_args;
  adxl::TransferOp adxl_operation = static_cast<adxl::TransferOp>(operation);
  std::vector<adxl::TransferOpDesc> adxl_op_descs;
  for (const auto &op_desc : op_descs) {
    adxl::TransferOpDesc temp{op_desc.local_addr, op_desc.remote_addr, op_desc.len};
    adxl_op_descs.push_back(temp);
  }
  adxl::TransferArgs adxl_optional_args;
  return adxl_inner_engine_.TransferAsync(remote_engine, adxl_operation, adxl_op_descs, adxl_optional_args, req);
}

Status AdxlEngine::GetTransferStatus(const TransferReq &req, TransferStatus &status) {
  adxl::TransferStatus adxl_status;
  auto ret = adxl_inner_engine_.GetTransferStatus(req, adxl_status);
  status = static_cast<hixl::TransferStatus>(adxl_status);
  return ret;
}

Status AdxlEngine::SendNotify(const AscendString &remote_engine, const NotifyDesc &notify, int32_t timeout_in_millis) {
  adxl::NotifyDesc adxl_notify{notify.name, notify.notify_msg};
  return adxl_inner_engine_.SendNotify(remote_engine, adxl_notify, timeout_in_millis);
}

Status AdxlEngine::GetNotifies(std::vector<NotifyDesc> &notifies) {
  std::vector<adxl::NotifyDesc> adxl_notifies;
  auto ret = adxl_inner_engine_.GetNotifies(adxl_notifies);
  for (const auto &adxl_notify : adxl_notifies) {
    hixl::NotifyDesc notify{adxl_notify.name, adxl_notify.notify_msg};
    notifies.push_back(notify);
  }
  return ret;
}
}  // namespace hixl