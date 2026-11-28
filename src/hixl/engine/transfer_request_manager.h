/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_ENGINE_TRANSFER_REQUEST_MANAGER_H_
#define CANN_HIXL_SRC_HIXL_ENGINE_TRANSFER_REQUEST_MANAGER_H_

#include <atomic>
#include <cstdint>
#include <map>
#include "common/hixl_inner_types.h"

namespace hixl {
struct TransferRequest {
  TransferReq backend_req = nullptr;
  TransferInfo info{};
};

// All accesses, including the lifetime of returned pointers, require HixlClient::mutex_.
class TransferRequestManager {
 public:
  Status Add(const TransferRequest &request, TransferReq &req);
  TransferRequest *Get(TransferReq req);
  void Remove(TransferReq req);
  void Clear();

 private:
  static Status AllocateId(TransferReq &req);
  // Shared by all clients; never reset on removal, Finalize or client reconstruction.
  static std::atomic<uintptr_t> next_id_;
  std::map<TransferReq, TransferRequest> requests_;
};
}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_ENGINE_TRANSFER_REQUEST_MANAGER_H_
