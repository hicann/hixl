/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_DATA_CACHE_ENGINE_H_
#define CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_DATA_CACHE_ENGINE_H_

#include <vector>
#include <mutex>
#include "llm_datadist/llm_error_codes.h"
#include "ge_common/ge_api_types.h"
#include "common/llm_inner_types.h"
#include "link_mgr/comm_entity_manager.h"
#include "cache_mgr/comm_mem_manager.h"
#include "cache_manager.h"
#include "common/llm_mem_pool.h"

namespace llm {
using TimePoint = std::chrono::time_point<std::chrono::steady_clock>;
class DataCacheEngine {
 public:
  DataCacheEngine() = default;
  ~DataCacheEngine() = default;
  ge::Status Initialize(const std::map<ge::AscendString, ge::AscendString> &options);
  void Finalize();
  ge::Status Register(const CacheDesc &cache_desc, const std::vector<CacheKey> &cache_keys, Cache &cache);
  ge::Status Allocate(const CacheDesc &cache_desc, const std::vector<CacheKey> &cache_keys, Cache &cache);
  ge::Status Deallocate(int64_t cache_id) const;
  ge::Status RemoveCacheKey(const CacheKey &cache_key) const;
  ge::Status PullCache(int64_t cache_id, const CacheKey &cache_key, const PullCacheParam &pull_cache_param);
  ge::Status CopyCache(const CopyCacheParam &copy_cache_param) const;
  ge::Status SwapBlocks(const Cache &src, const Cache &dst, const uint64_t block_size, const uint32_t type,
                        const std::vector<std::pair<int64_t, int64_t>> &block_mapping) const;
  ge::Status CheckCapacity(size_t size);
  ge::Status TransferCache(const uint64_t task_id, const TransferCacheConfig &transfer_cache_config,
                           const TransferBlockConfig &transfer_block_config);
  void SetCommEntityManager(CommEntityManager *comm_entity_manager);
  void SetCommMemManager(CommMemManager *comm_mem_manager);
  void SetCacheManager(CacheManager *cache_manager);
  ge::Status Unregister(int64_t cache_id);

 private:
  static ge::Status CheckParam(const CacheEntry &cache_entry, const PullCacheParam &pull_cache_param);
  static ge::Status CheckTensorIndices(const CacheEntry &cache_entry, const PullCacheParam &pull_cache_param);
  ge::Status InitializeMemoryPool(const std::map<ge::AscendString, ge::AscendString> &options);
  ge::Status InitializeDeviceMemoryPool(const std::map<ge::AscendString, ge::AscendString> &options);
  ge::Status InitializeHostMemoryPool(const std::map<ge::AscendString, ge::AscendString> &options);

  std::mutex mu_;
  std::atomic_int64_t cache_id_gen_{1};
  int32_t device_id_{-1};
  CommEntityManager *comm_entity_manager_{};
  CommMemManager *comm_mem_manager_{};
  CacheManager *cache_manager_{};
  aclrtStream req_stream_{nullptr};
  aclrtContext aclrt_context_{nullptr};
  int32_t sync_cache_timeout_{0};
  void *npu_pool_memory_{};
  size_t npu_pool_size_{};
  bool access_remote_cache_ = false;
  std::unique_ptr<LlmMemPool> npu_mem_pool_{};
  aclrtStream transfer_stream_{nullptr};
  std::once_flag create_stream_once_flag_;
  void *host_pool_memory_{nullptr};
  std::unique_ptr<LlmMemPool> host_mem_pool_{};
  void *npu_pool_handle_{nullptr};
  void *host_pool_handle_{nullptr};
  void *cache_table_handle_{nullptr};
};
}  // namespace llm

#endif  // CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_DATA_CACHE_ENGINE_H_
