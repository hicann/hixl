/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_TRANSFER_MESSAGE_LIMITS_H_
#define CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_TRANSFER_MESSAGE_LIMITS_H_

#include <cstddef>
#include <cstdint>

#include "common/common.h"

namespace llm {
namespace transfer_message_limits {

constexpr uint64_t kMsgFlagSize = 8U;
constexpr uint64_t kDefaultRespBufferSize = 16U * 1024U;
constexpr uint64_t kDefaultReqBufferSize = 112U * 1024U;

constexpr size_t kResponseInfoHeaderSize = offsetof(ResponseInfo, sync_flag_addresses);
constexpr uint64_t kMaxResponsePayloadSize = kDefaultRespBufferSize - kMsgFlagSize;
constexpr uint64_t kMaxRequestPayloadSize = kDefaultReqBufferSize - kMsgFlagSize;

constexpr uint32_t kMaxDstAddrCount =
    static_cast<uint32_t>((kMaxResponsePayloadSize > kResponseInfoHeaderSize
                               ? (kMaxResponsePayloadSize - kResponseInfoHeaderSize) / sizeof(uint64_t)
                               : 0U));

constexpr uint64_t kMaxTransferInfoCount =
    (kMaxRequestPayloadSize > sizeof(TransferCacheReq)
         ? (kMaxRequestPayloadSize - sizeof(TransferCacheReq)) / sizeof(TransferInfo)
         : 0U);

constexpr uint32_t kBufferInfoMultiplierD2h = 1U;
constexpr uint32_t kBufferInfoMultiplierD2dH2d = 2U;

inline uint64_t CalcResponseSize(uint32_t dst_addr_count) {
  return kResponseInfoHeaderSize + static_cast<uint64_t>(dst_addr_count) * sizeof(uint64_t);
}

inline uint64_t CalcMinRequestSize(uint32_t dst_addr_count, uint32_t buffer_info_count,
                                   uint32_t buffer_info_multiplier) {
  return sizeof(TransferCacheReq) +
         sizeof(TransferInfo) * (static_cast<uint64_t>(dst_addr_count) +
                                 static_cast<uint64_t>(buffer_info_count) * buffer_info_multiplier);
}

inline uint64_t MaxD2hPromptBlocksForDstCount(uint64_t dst_addr_count) {
  if (dst_addr_count >= kMaxTransferInfoCount) {
    return 0U;
  }
  return kMaxTransferInfoCount - dst_addr_count;
}

}  // namespace transfer_message_limits
}  // namespace llm

#endif  // CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_TRANSFER_MESSAGE_LIMITS_H_
