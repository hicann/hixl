/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_CACHE_MANAGER_H_
#define CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_CACHE_MANAGER_H_

#include <map>
#include <mutex>
#include <unordered_set>
#include "llm_datadist/llm_error_codes.h"
#include "runtime/rt.h"
#include "common/common.h"
#include "common/llm_mem_pool.h"
#include "utils/cache_access_table.h"

namespace llm {
using DataCacheKey = std::pair<uint64_t, uint64_t>;  // req id/prefix id, model id
class CacheManager {
 public:
  CacheManager() = default;
  ~CacheManager() = default;
  void Finalize();
  bool GetCacheKey(const std::pair<int64_t, uint64_t> &cache_id_and_batch_index, DataCacheKey &cache_key) const;
  bool GetCacheEntry(const int64_t cache_id, CacheEntry &cache_entry) const;
  bool GetCacheEntry(const DataCacheKey &cache_key, bool is_prefix, CacheEntry &cache_entry) const;
  ge::Status RegisterCacheEntry(int64_t cache_id, const std::vector<CacheKey> &cache_keys,
                                const llm::CacheDesc &cache_desc, std::vector<uintptr_t> &addrs, int64_t tensor_size);
  ge::Status UnregisterCacheEntry(int64_t cache_id);
  ge::Status Allocate(int64_t cache_id,
                      const CacheDesc &cache_desc,
                      const std::vector<CacheKey> &cache_keys,
                      Cache &cache);
  ge::Status Deallocate(int64_t cache_id);
  ge::Status RemoveCacheKey(const CacheKey &cache_key);
  ge::Status RemoveCacheKey(const DataCacheKey &data_cache_key, bool is_prefix,
                            const std::unordered_set<uint64_t> &tensor_indices={});
  ge::Status CopyCache(const CopyCacheParam &copy_cache_param);
  void SetNpuMemPool(LlmMemPool *llm_mem_pool);
  void SetHostMemPool(LlmMemPool *llm_mem_pool);
  ge::Status CopyCacheForContinuous(const CacheEntry &src_cache_entry,
                                    const CacheEntry &dst_cache_entry,
                                    const CopyCacheParam &copy_cache_param,
                                    size_t per_device_addr_num,
                                    size_t device_index = 0U);
  ge::Status CopyCacheForBlocks(const CacheEntry &src_cache_entry,
                                const CacheEntry &dst_cache_entry,
                                const CopyCacheParam &copy_cache_param,
                                size_t per_device_addr_num,
                                size_t device_index = 0U);
  ge::Status Initialize(bool access_remote_cache);
  std::pair<void *, size_t> GetCacheTableBufferAndSize() const;
  ge::Status InitCopyStreams(size_t device_num);
  void DestroyCopyStream(size_t device_index);
  LlmMemPool *GetNpuMemPool() const;

private:
  const CacheEntry *DoGetCacheEntry(int64_t cache_id) const;
  static CacheEntry CreateCacheEntry(const CacheDesc &cache_desc,
                                     std::vector<uintptr_t> &addrs,
                                     int64_t tensor_size);
  static void NoDelete(void *) {}
  void AddCacheIndices(CacheEntry &cache_entry, int64_t cache_id, const std::vector<CacheKey> &cache_keys);
  ge::Status CheckCacheKeys(const CacheDesc &cache_desc, const std::vector<CacheKey> &cache_keys);
  static DataCacheKey CreateDataCacheKey(const CacheKey &cache_key, bool &is_prefix);
  static ge::Status CheckCopyParams(const CacheEntry &src_cache_entry,
                                    const CacheEntry &dst_cache_entry,
                                    const CopyCacheParam &copy_cache_param,
                                    uint64_t &copy_size);
  static rtMemcpyKind_t ResolveCopyKind(CachePlacement src_placement, CachePlacement dst_placement);
  ge::Status EnsureCopyStream(size_t device_index);
  ge::Status UpdateCacheTable();

  mutable std::mutex mu_;
  std::mutex copy_mu_;
  std::map<int64_t, CacheEntry> cache_id_to_entry_;
  std::map<int64_t, std::unordered_set<uint64_t>> cache_id_to_tensor_indices_;
  std::map<DataCacheKey, int64_t> cache_key_to_id_;
  std::map<DataCacheKey, int64_t> prefix_key_to_id_;
  std::map<std::pair<int64_t, uint32_t>, DataCacheKey> cache_id_and_batch_id_to_cache_key_;
  LlmMemPool *npu_mem_pool_ = nullptr;
  std::vector<rtStream_t> copy_streams_;
  LlmMemPool *host_mem_pool_ = nullptr;
  CacheAccessTableUpdater cache_access_table_updater_;
  bool enable_remote_cache_accessible_ = false;
  rtContext_t rt_context_ = nullptr;
};
}  // namespace llm

#endif  // CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_CACHE_MANAGER_H_
