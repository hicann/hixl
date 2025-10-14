/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "swap_impl.h"
#include "common/llm_thread_pool.h"
#include "common/llm_log.h"
#include "common/llm_checker.h"
#include "common/llm_scope_guard.h"

namespace llm {
namespace {
constexpr int32_t kSwapOut = 1;
constexpr int32_t kHbmBufferNum = 4;
}  // namespace

ge::Status SwapImpl::SwapBlocks(const std::vector<uintptr_t> &src_addrs, const std::vector<uintptr_t> &dst_addrs,
                                const uint64_t block_size,
                                const std::vector<std::pair<int64_t, int64_t>> &block_mapping,
                                const CopyInfo &copy_info) {
  std::vector<std::vector<std::pair<int64_t, int64_t>>> ordered_block_mapping;
  LLM_CHK_STATUS_RET(LLMUtils::FindContiguousBlockIndexPair(block_mapping, ordered_block_mapping));
  const auto start = std::chrono::steady_clock::now();
  LLMThreadPool swap_out_pool("ge_llm_swap", kHbmBufferNum);
  rtContext_t rt_context = nullptr;
  LLM_CHK_ACL_RET(rtCtxGetCurrent(&rt_context));
  std::vector<std::future<ge::Status>> rets;
  std::atomic<size_t> rt_copy_time{0UL};
  for (size_t i = 0U; i < src_addrs.size(); ++i) {
    auto src_addr = src_addrs[i];
    auto dst_addr = dst_addrs[i];
    std::future<ge::Status> f = swap_out_pool.commit([src_addr, dst_addr, &ordered_block_mapping, &block_size,
                                                      &rt_context, &rt_copy_time, &copy_info]() -> ge::Status {
      LLM_CHK_ACL_RET(rtCtxSetCurrent(rt_context));
      for (const auto &ordered_block : ordered_block_mapping) {
        const int64_t src_index = ordered_block.front().first;
        const int64_t dst_index = ordered_block.front().second;
        const uint64_t copy_size = block_size * ordered_block.size();
        auto src = src_addr + src_index * block_size;
        auto dst = dst_addr + dst_index * block_size;
        LLMLOGI("Begin mem copy, src index:%ld, dst index:%ld, copy size:%lu, contiguous block num:%lu", src_index,
               dst_index, copy_size, ordered_block.size());
        const auto copy_start = std::chrono::steady_clock::now();
        if (copy_info.copy_type == CopyType::kMemcpyEx) {
          LLM_CHK_ACL_RET(rtMemcpyEx(reinterpret_cast<void *>(dst), copy_size, reinterpret_cast<void *>(src), copy_size,
                                   copy_info.copy_kind));
        } else {
          LLM_CHK_ACL_RET(rtMemcpy(reinterpret_cast<void *>(dst), copy_size, reinterpret_cast<void *>(src), copy_size,
                                 copy_info.copy_kind));
        }
        const auto copy_end = std::chrono::steady_clock::now();
        const auto cost = std::chrono::duration_cast<std::chrono::microseconds>(copy_end - copy_start).count();
        rt_copy_time.fetch_add(cost, std::memory_order_relaxed);
      }
      return ge::SUCCESS;
    });
    LLM_CHK_BOOL_RET_STATUS(f.valid(), ge::FAILED, "commit blocks rtMemcpyEx task failed");
    rets.emplace_back(std::move(f));
  }
  for (size_t i = 0U; i < rets.size(); ++i) {
    LLM_CHK_BOOL_RET_STATUS(rets[i].get() == ge::SUCCESS, ge::FAILED, "the %zuth blocks mem copy failed", i);
  }
  const auto end = std::chrono::steady_clock::now();
  LLMLOGI("[LlmPerf] mem copy cost time:%zu us, copy kind:%d, swap blocks cost time:%zu us", rt_copy_time.load(),
         copy_info.copy_kind, std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
  return ge::SUCCESS;
}

ge::Status SwapImpl::SwapBlocksV2(const Cache &src, const Cache &dst, const uint64_t block_size, const uint32_t type,
                                  const std::vector<std::pair<int64_t, int64_t>> &block_mapping) const {
  LLM_CHK_STATUS_RET(CheckParam(src, dst), "check param failed");
  const auto &src_addrs = src.per_device_tensor_addrs;
  const auto &dst_addrs = dst.per_device_tensor_addrs;
  LLMLOGI("Begin swap blocks, cache num:%zu, swap block num:%zu, swap type:%u", src_addrs.front().size(),
         block_mapping.size(), type);
  LLM_CHK_ACL_RET(rtSetDevice(device_id_));
  LLM_MAKE_GUARD(reset_device, [this]() { LLM_CHK_ACL(rtDeviceReset(device_id_)); });
  rtMemcpyKind_t kind = (type == kSwapOut) ? RT_MEMCPY_DEVICE_TO_HOST : RT_MEMCPY_HOST_TO_DEVICE;
  LLM_CHK_STATUS_RET(
      SwapBlocks(src_addrs.front(), dst_addrs.front(), block_size, block_mapping, CopyInfo{CopyType::kMemcpy, kind}),
      "swap blocks failed, kind:%d", kind);
  LLMLOGI("swap blocks success, kind:%d", kind);
  return ge::SUCCESS;
}

ge::Status SwapImpl::CheckParam(const Cache &src, const Cache &dst) {
  const auto &src_addrs = src.per_device_tensor_addrs;
  const auto &dst_addrs = dst.per_device_tensor_addrs;
  LLM_CHK_BOOL_RET_STATUS(((src_addrs.size() == 1) && (src_addrs.size() == dst_addrs.size())), ge::LLM_PARAM_INVALID,
                         "currently support kv cache in one device");
  LLM_CHK_BOOL_RET_STATUS((src_addrs.front().size() == dst_addrs.front().size()), ge::LLM_PARAM_INVALID,
                         "src adrrs size:%zu not equal dst addrs size:%zu", src_addrs.front().size(),
                         dst_addrs.front().size());
  return ge::SUCCESS;
}
}  // namespace llm
