/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_GRAPH_ENGINE_RUNTIME_LLM_ENGINE_INC_LLM_V2_DATADIST_H
#define CANN_GRAPH_ENGINE_RUNTIME_LLM_ENGINE_INC_LLM_V2_DATADIST_H

#include "common/llm_inner_types.h"
#include "link_mgr/llm_link_manager.h"
#include "cache_mgr/data_cache_engine.h"
#include "hccl/hccl_adapter.h"
#include "link_mgr/comm_entity_manager.h"
#include "cache_mgr/cache_manager.h"

namespace llm {
class LLMDataDistV2  {
 public:
  explicit LLMDataDistV2(uint64_t cluster_id) : cluster_id_(cluster_id) {};

  virtual ~LLMDataDistV2();

  ge::Status LLMDataDistInitialize(const std::map<ge::AscendString, ge::AscendString> &options);

  void LLMDataDistFinalize();

  ge::Status Link(std::string &cluster_name,
                  const std::map<uint64_t, uint32_t> &cluster2rank, std::string &rank_table, uint64_t &comm_id);

  ge::Status Unlink(uint64_t comm_id);

  ge::Status QueryRegisterMemStatus(uint64_t comm_id, RegisterMemoryStatus &status);

  ge::Status RegisterCache(const CacheDesc &cache_desc, Cache &cache, const std::vector<CacheKey> &cache_keys = {});

  ge::Status AllocateCache(const CacheDesc &cache_desc, Cache &cache, const std::vector<CacheKey> &cache_keys = {});

  ge::Status DeallocateCache(int64_t cache_id);

  ge::Status RemoveCacheKey(const CacheKey &cache_key);

  ge::Status RemapRegisteredMemory(const std::vector<LLMMemInfo> &mem_infos);

  ge::Status PullCache(int64_t cache_id, const CacheKey &cache_key, const PullCacheParam &pull_cache_param = {});

  ge::Status PullBlocks(int64_t cache_id, const CacheKey &cache_key, const PullCacheParam &pull_cache_param = {});

  ge::Status CopyCache(const CopyCacheParam &copy_cache_param);

  ge::Status SwapBlocks(const Cache &src, const Cache &dst, const uint64_t block_size, const uint32_t type,
                        const std::vector<std::pair<int64_t, int64_t>> &block_mapping);

  ge::Status CheckCapacity(const size_t seq_len);

  ge::Status TransferCache(const uint64_t task_id, const TransferCacheConfig &transfer_cache_config,
                           const TransferBlockConfig &transfer_block_config);

  ge::Status LinkClusters(const std::vector<ClusterInfo> &clusters, std::vector<ge::Status> &rets,
                          const int32_t timeout);

  ge::Status UnlinkClusters(const std::vector<ClusterInfo> &clusters, std::vector<ge::Status> &rets,
                            const int32_t timeout, bool force_flag = false);

  ge::Status UnregisterCache(int64_t cache_id);

  ge::Status SwitchRole(const std::string &role, const std::map<std::string, std::string> &options);

  bool IsInitialized() const;

 private:
  void DoInnerFinalize();
  virtual ge::Status DoInitialize(const std::map<ge::AscendString, ge::AscendString> &options);
  virtual void DoFinalize();

  uint64_t cluster_id_;
  std::atomic<bool> is_initialized_{false};
  std::mutex mutex_;
  std::unique_ptr<LLMLinkManager> llm_link_mgr_;
  std::unique_ptr<DataCacheEngine> data_cache_engine_;
  std::unique_ptr<CommEntityManager> comm_entity_manager_;
  std::unique_ptr<CommMemManager> comm_mem_manager_;
  std::unique_ptr<CacheManager> cache_manager_;
  void *statistic_timer_handle_{nullptr};
  std::mutex transfer_mutex_;
  std::atomic<bool> inner_initialized_{false};
};
}  // namespace llm
#endif  // CANN_GRAPH_ENGINE_RUNTIME_LLM_ENGINE_INC_LLM_V2_DATADIST_H
