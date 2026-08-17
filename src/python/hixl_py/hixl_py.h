/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_PYTHON_HIXL_PY_HIXL_PY_H_
#define CANN_HIXL_PYTHON_HIXL_PY_HIXL_PY_H_

#include <cstdint>
#include <memory>
#include <shared_mutex>
#include <utility>
#include "hixl/hixl.h"
#include "hixl/hixl_types.h"

namespace hixl_py {

class HixlPy {
 public:
  HixlPy();
  ~HixlPy();

  hixl::Status Initialize(const std::string &local_engine, const std::map<std::string, std::string> &options);
  void Finalize();

  std::pair<hixl::Status, uintptr_t> RegisterMem(const hixl::MemDesc &mem_desc, hixl::MemType mem_type);
  hixl::Status DeregisterMem(uintptr_t mem_handle);
  hixl::Status Connect(const std::string &remote_engine, int32_t timeout_ms = 1000);
  hixl::Status Disconnect(const std::string &remote_engine, int32_t timeout_ms = 1000);
  hixl::Status ConnectAsync(const std::string &remote_engine, int32_t timeout_ms = 1000);
  hixl::Status DisconnectAsync(const std::string &remote_engine, int32_t timeout_ms = 1000);
  std::pair<hixl::Status, hixl::AsyncConnectStatus> GetAsyncConnectStatus(const std::string &remote_engine);
  std::pair<hixl::Status, std::map<std::string, hixl::AsyncConnectStatus>> GetAllAsyncConnectStatus();
  hixl::Status TransferSync(const std::string &remote_engine, hixl::TransferOp operation,
                            const std::vector<hixl::TransferOpDesc> &op_descs, int32_t timeout_ms = 1000);
  std::pair<hixl::Status, uintptr_t> TransferAsync(const std::string &remote_engine, hixl::TransferOp operation,
                                                   const std::vector<hixl::TransferOpDesc> &op_descs,
                                                   hixl::TransferArgs args);
  std::pair<hixl::Status, hixl::TransferStatus> GetTransferStatus(uintptr_t req_id);
  std::pair<hixl::Status, std::vector<hixl::TransferResult>> GetAllTransferStatus(hixl::GetTransferStatusArgs args);
  hixl::Status SendNotify(const std::string &remote_engine, const hixl::NotifyDesc &notify, int32_t timeout_ms = 1000);
  std::pair<hixl::Status, std::vector<hixl::NotifyDesc>> GetNotifies();
  static std::pair<hixl::Status, int32_t> GetCapability(hixl::FeatureType feature_type);

 private:
  std::unique_ptr<hixl::Hixl> hixl_engine_;
  std::shared_mutex mutex_;
  bool initialized_ = false;
};

}  // namespace hixl_py

#endif  // CANN_HIXL_PYTHON_HIXL_PY_HIXL_PY_H_
