/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_ENGINE_CLIENT_HANDLER_H_
#define CANN_HIXL_SRC_HIXL_ENGINE_CLIENT_HANDLER_H_

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "cs/hixl_cs.h"
#include "common/hixl_inner_types.h"
#include "common/segment.h"

namespace hixl {
enum class CommType : uint32_t {
  COMM_TYPE_UB_D2D = 0U,
  COMM_TYPE_UB_H2D = 1U,
  COMM_TYPE_UB_D2H = 2U,
  COMM_TYPE_UB_H2H = 3U,
  COMM_TYPE_ROCE = 4U,
  COMM_TYPE_HCCS = 5U,
  COMM_TYPE_UBOE = 6U
};

inline const char *CommTypeToString(CommType type) {
  switch (type) {
    case CommType::COMM_TYPE_UB_D2D:
      return "UB_D2D";
    case CommType::COMM_TYPE_UB_H2D:
      return "UB_H2D";
    case CommType::COMM_TYPE_UB_D2H:
      return "UB_D2H";
    case CommType::COMM_TYPE_UB_H2H:
      return "UB_H2H";
    case CommType::COMM_TYPE_ROCE:
      return "ROCE";
    case CommType::COMM_TYPE_HCCS:
      return "HCCS";
    case CommType::COMM_TYPE_UBOE:
      return "UBOE";
    default:
      return "UNKNOWN";
  }
}

class IClientHandler {
 public:
  virtual ~IClientHandler() = default;

  virtual Status Connect(uint32_t timeout_ms) = 0;
  virtual Status RegisterMem(const MemInfo &mem_info) = 0;
  virtual Status TransferAsync(const std::vector<TransferOpDesc> &op_descs, TransferOp operation,
                               TransferReq &req) = 0;
  virtual Status TransferSync(const std::vector<TransferOpDesc> &op_descs, TransferOp operation,
                              uint32_t timeout_ms) = 0;
  virtual Status GetTransferStatus(const TransferReq &req, TransferStatus &status) = 0;
  virtual Status Finalize() = 0;
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_ENGINE_CLIENT_HANDLER_H_
