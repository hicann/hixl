/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_TRANSFER_REQUEST_VALIDATION_H_
#define CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_TRANSFER_REQUEST_VALIDATION_H_

#include <cstddef>
#include <cstdint>

#include "ge_common/ge_api_error_codes.h"
#include "llm_datadist/llm_error_codes.h"

namespace llm {
namespace transfer_request_validation {

// D2H 服务端按 dst_buffer_size 把一次传输切成多批。取 0 时切分循环无法推进 (cur == 0)，
// 会在 FSM 线程里死循环；取值远小于块大小时批次数按 data_size/dst_buffer_size 放大。
// 内置客户端固定为 32MB，这里给一个宽松但足以兜住非法值的下限。
constexpr uint64_t kMinDstBufferSize = 4U * 1024U;

// 对端给的 src 张量区间必须整体落在本端 cache 的张量数组内。
// num_tensors 表示本次要用的张量个数，调用方需按实际语义展开（整段 cache 时即 cache_addrs.size()）。
inline ge::Status CheckSrcTensorRange(uint64_t start_index, uint64_t num_tensors, size_t cache_addrs_size) {
  if (start_index > cache_addrs_size) {
    return ge::LLM_PARAM_INVALID;
  }
  return num_tensors <= (cache_addrs_size - start_index) ? ge::SUCCESS : ge::LLM_PARAM_INVALID;
}

// H2D 用张量下标直接索引 dst_addr 段，而该段只有 dst_addr_count 项。
// 张量数一旦超过它，下标就会读到后面的 buffer_info 段（union 语义错位）。
inline ge::Status CheckTensorCountWithinDstAddr(uint64_t num_tensors, uint32_t dst_addr_count) {
  return num_tensors <= dst_addr_count ? ge::SUCCESS : ge::LLM_PARAM_INVALID;
}

// 对端给的 block 下标换算出的读写区间 [block_index*block_size, +buffer_len) 必须落在本端实际分配区内。
// region_size 是该张量真实可用的字节数；block_size 传 0 表示偏移恒为 0。
// 先比较 buffer_len 再相减以避免下溢，用除法代替乘法以避免 block_index*block_size 溢出。
// 注意：下标与长度各自合法并不够，二者的乘积仍可能越过 region_size，所以这里必须一起校验。
inline ge::Status CheckBlockSpanWithinRegion(uint64_t block_index, uint64_t block_size, uint64_t buffer_len,
                                             uint64_t region_size) {
  if (buffer_len > region_size) {
    return ge::LLM_PARAM_INVALID;
  }
  if (block_size == 0U) {
    return ge::SUCCESS;
  }
  return block_index <= ((region_size - buffer_len) / block_size) ? ge::SUCCESS : ge::LLM_PARAM_INVALID;
}

// D2D 的单边读起点是 cache_addrs[i] + offset + block_index * block_size，其中 offset 来自本端 cache 的
// batch_index * stride（block cache 时为 0）。offset 直接叠加在地址上，所以可读区间要从 offset 起算，
// 只用张量分配大小做界会漏掉 offset 这一项：batch 偏移与 block 偏移叠加后（各自都合法）仍能越过张量。
inline ge::Status CheckBlockSpanWithinD2dRegion(uint64_t block_index, uint64_t block_size, uint64_t len,
                                                uint64_t tensor_size, uint64_t offset) {
  if (offset > tensor_size) {
    return ge::LLM_PARAM_INVALID;
  }
  return CheckBlockSpanWithinRegion(block_index, block_size, len, tensor_size - offset);
}

// D2H 在响应区之后为每个 dst 地址放一个 int32 recv flag，必须放得下。
inline ge::Status CheckRecvFlagArea(uint32_t dst_addr_count, uint64_t response_size, uint64_t max_payload_size,
                                    uint64_t flag_size) {
  const uint64_t flags_size = static_cast<uint64_t>(flag_size) * static_cast<uint64_t>(dst_addr_count);
  return (response_size + flags_size) <= max_payload_size ? ge::SUCCESS : ge::LLM_PARAM_INVALID;
}

// D2H 服务端把 dst_buffer_size 收窄成 uint32_t 使用；超出范围会被截断，0 或极小值会放大批次数。
inline ge::Status CheckD2hDstBufferSize(uint64_t dst_buffer_size) {
  if ((dst_buffer_size < kMinDstBufferSize) || (dst_buffer_size > UINT32_MAX)) {
    return ge::LLM_PARAM_INVALID;
  }
  return ge::SUCCESS;
}

// D2D 用 block_size 做整除/取模来推算传输块数；取 0 会触发除零（SIGFPE，整个进程退出）。
// 对端可以用 request.block_size = 0 选中"回落到本端 cache stride"的分支，因此两边都要非 0。
inline ge::Status CheckD2dBlockSize(uint64_t request_block_size, uint64_t cache_stride) {
  const uint64_t effective_block_size = (request_block_size != 0U) ? request_block_size : cache_stride;
  return effective_block_size != 0U ? ge::SUCCESS : ge::LLM_PARAM_INVALID;
}

// 对端在响应里回报的 sync flag 地址必须逐个给出（非 0）。
// 客户端在发请求前会把响应区清零，对端只回短响应时未被覆盖的槽位保持 0，据此判定地址缺失，
// 避免把残留字节当成远端地址去做单边写。
inline ge::Status CheckSyncFlagAddresses(const uint64_t *addresses, uint32_t count) {
  if (addresses == nullptr) {
    return ge::LLM_PARAM_INVALID;
  }
  for (uint32_t i = 0U; i < count; ++i) {
    if (addresses[i] == 0U) {
      return ge::LLM_PARAM_INVALID;
    }
  }
  return ge::SUCCESS;
}

// 响应要能装下 required 个 sync flag 地址。transfer_count 为 0 表示对端未填该字段
// （老版本对端的行为），此时跳过比较以保持跨版本互通；地址本身是否可用由 CheckSyncFlagAddresses 负责。
inline ge::Status CheckResponseTransferCount(uint32_t transfer_count, uint32_t required) {
  if (transfer_count == 0U) {
    return ge::SUCCESS;
  }
  return transfer_count >= required ? ge::SUCCESS : ge::LLM_PARAM_INVALID;
}

}  // namespace transfer_request_validation
}  // namespace llm

#endif  // CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_TRANSFER_REQUEST_VALIDATION_H_
