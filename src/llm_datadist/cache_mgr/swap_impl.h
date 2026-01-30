/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_SWAP_IMPL_H_
#define CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_SWAP_IMPL_H_

#include "common/llm_utils.h"
#include "acl/acl.h"

namespace llm {
enum class CopyType : int32_t { kMemcpy = 0, kMemcpyEx = 1 };
struct CopyInfo {
  CopyType copy_type;
  aclrtMemcpyKind copy_kind;
};
class SwapImpl {
 public:
  explicit SwapImpl(int32_t device_id) : device_id_(device_id) {}
  ~SwapImpl() = default;
  ge::Status SwapBlocksV2(const Cache &src, const Cache &dst, const uint64_t block_size, const uint32_t type,
                          const std::vector<std::pair<int64_t, int64_t>> &block_mapping) const;

 private:
  static ge::Status CheckParam(const Cache &src, const Cache &dst);
  static ge::Status SwapBlocks(const std::vector<uintptr_t> &src_addrs, const std::vector<uintptr_t> &dst_addrs,
                               const uint64_t block_size, const std::vector<std::pair<int64_t, int64_t>> &block_mapping,
                               const CopyInfo &copy_info);
  int32_t device_id_{0};
};
}  // namespace llm

#endif  // CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_SWAP_IMPL_H_
