/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_GRAPH_ENGINE_RUNTIME_LLM_ENGINE_DATA_TRANSFER_LAYER_WISE_TRANSFER_JOB_H_
#define CANN_GRAPH_ENGINE_RUNTIME_LLM_ENGINE_DATA_TRANSFER_LAYER_WISE_TRANSFER_JOB_H_

#include "ge_common/ge_api_types.h"
#include "link_mgr/comm_entity.h"

namespace llm {
class LayerWiseTransferJob {
 public:
  LayerWiseTransferJob(CommEntity &comm_entity, rtStream_t stream);
  ~LayerWiseTransferJob() = default;
  ge::Status TransferCache(const CacheEntry &cache_entry,
                           const TransferCacheConfig &transfer_cache_config,
                           const TransferBlockConfig &transfer_block_config,
                           int32_t timeout_in_ms,
                           bool access_remote_cache);

 private:
  ge::Status Prepare(const CacheEntry &cache_entry,
                     const TransferCacheConfig &transfer_cache_config,
                     const TransferBlockConfig &transfer_block_config);
  ge::Status GenerateCacheToCacheTask(const CacheEntry &cache_entry,
                                      const std::vector<std::shared_ptr<void>> &src_layer_addrs,
                                      const TransferCacheConfig &transfer_cache_config);
  ge::Status GenerateCacheToBlocksTask(const CacheEntry &cache_entry,
                                       const std::vector<std::shared_ptr<void>> &src_layer_addrs,
                                       const TransferCacheConfig &transfer_cache_config,
                                       const TransferBlockConfig &transfer_block_config);
  ge::Status GenerateBlocksToBlocksTask(const CacheEntry &cache_entry,
                                        const std::vector<std::shared_ptr<void>> &src_layer_addrs,
                                        const TransferCacheConfig &transfer_cache_config,
                                        const TransferBlockConfig &transfer_block_config);
  ge::Status SynchronizeTransferCache(const int32_t timeout_in_ms);
  ge::Status SynchronizeTransferCacheWithRecord(const int32_t timeout_in_ms);
  ge::Status FillRemoteLayerAddrs(int32_t timeout_in_ms,
                                  TransferCacheConfig &transfer_config,
                                  const TransferBlockConfig &transfer_block_config);

  ge::Status ValidateRemoteCache(const CacheEntry &remote_cache_entry, const TransferCacheConfig &transfer_cache_config,
                                 const TransferBlockConfig &transfer_block_config) const;

  rtStream_t stream_;
  CommEntity *comm_entity_;
  std::list<HcclOneSideOpDesc> layer_transfer_tasks_;
  rtEvent_t event_{nullptr};
};
}  // namespace llm
#endif  // CANN_GRAPH_ENGINE_RUNTIME_LLM_ENGINE_DATA_TRANSFER_LAYER_WISE_TRANSFER_JOB_H_
