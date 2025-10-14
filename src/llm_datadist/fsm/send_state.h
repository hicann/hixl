/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_SEND_STATE_H_
#define CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_SEND_STATE_H_

#include <vector>
#include <list>
#include "llm_datadist/llm_error_codes.h"
#include "ge/ge_api_types.h"
#include "common/llm_inner_types.h"
#include "common/common.h"
#include "fsm/base_state.h"
#include "link_mgr/comm_entity.h"

namespace llm {
struct CacheDataSlice {
  void *data_addr;
  size_t size;
};

class SendState : public BaseState {
 public:
  SendState() noexcept = default;
  ~SendState() override = default;
  ge::Status Preprocess(CommEntity &entity) override;
  ge::Status Process(CommEntity &entity) override;
  ge::Status Postprocess(CommEntity &entity) override;

  SendState(const SendState &) = delete;
  SendState(const SendState &&) = delete;
  SendState &operator=(const SendState &) = delete;
  SendState &operator=(const SendState &&) = delete;
 private:
  static ge::Status Prepare(CommEntity &entity);
  static ge::Status CheckParam(const CacheEntry &cache_entry, const TransferCacheReq &request);
  static ge::Status QueryCacheEntryAndOffset(CommEntity &entity, CacheEntry &cache_entry, uint64_t &offset);
  static ge::Status QueryBlocksCache(const CacheManager &cache_manager,
                                     const TransferCacheReq &request,
                                     CacheEntry &cache_entry);
  static ge::Status QueryCacheByCacheId(const CacheManager &cache_manager,
                                        const TransferCacheReq &request,
                                        CacheEntry &cache_entry);
  static bool GetCacheKey(const CacheManager &cache_manager,
                          const TransferCacheReq &request,
                          std::pair<uint64_t, uint64_t> &cache_key,
                          bool &is_prefix);
  static int32_t ResolveTransferType(const TransferCacheReq &request, const CacheEntry &cache_entry);
  static std::unordered_set<uint64_t> GetLayerRangeTensorIndices(const TransferCacheReq &request);
};
}  // namespace llm

#endif  // CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_SEND_STATE_H_
