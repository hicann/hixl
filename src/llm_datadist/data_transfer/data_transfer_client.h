/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_DATA_TRANSFER_DATA_TRANSFER_CLIENT_H_
#define CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_DATA_TRANSFER_DATA_TRANSFER_CLIENT_H_

#include <vector>
#include "llm_datadist/llm_error_codes.h"
#include "ge_common/ge_api_types.h"
#include "common/llm_inner_types.h"
#include "common/llm_mem_pool.h"
#include "link_mgr/comm_entity_manager.h"

namespace llm {
using TimePoint = std::chrono::time_point<std::chrono::steady_clock>;
class DataTransferClient {
 public:
  DataTransferClient(CommEntity &comm_entity, rtStream_t stream) : comm_entity_(&comm_entity), req_stream_(stream) {}
  ~DataTransferClient() = default;
  ge::Status PullCache(const CacheEntry &cache_entry, const CacheKey &cache_key, const PullCacheParam &pull_cache_param,
                       int32_t timeout_in_ms);
  ge::Status PullCacheByGet(const CacheEntry &cache_entry, const CacheKey &cache_key,
                            const PullCacheParam &pull_cache_param, int32_t timeout_in_ms);

 private:
  ge::Status PullCacheFromRemote(const TimePoint &start_time) const;
  ge::Status SendCacheInfoToRemote() const;
  ge::Status SynchronizeStreamTask(const TimePoint &start_time) const;
  ge::Status GetResponseInfo() const;
  ge::Status ConstructTransferInfo(const PullCacheParam &pull_cache_param, const CacheEntry &cache_entry,
                                   const CacheKey &cache_key, int32_t timeout, TransferCacheReq &request) const;

  CommEntity *comm_entity_;
  rtStream_t req_stream_{};
  int32_t timeout_in_ms_{0};
};
}  // namespace llm

#endif  // CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_DATA_TRANSFER_CLIENT_H_
