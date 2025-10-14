/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_GRAPH_ENGINE_PYTHON_LLM_WRAPPER_LLM_DATADIST_V2_WRAPPER_H
#define CANN_GRAPH_ENGINE_PYTHON_LLM_WRAPPER_LLM_DATADIST_V2_WRAPPER_H

#include "llm_datadist_v2.h"

namespace llm {
using ClusterInfoTuple = std::tuple<uint64_t,
                                    int32_t,
                                    std::vector<std::pair<uint32_t, uint16_t>>,
                                    std::vector<std::pair<uint32_t, uint16_t>>>;

using CacheTuple = std::tuple<int64_t, std::vector<std::vector<uintptr_t>>>;

using CacheDescTuple = std::tuple<uint32_t, int32_t, int32_t, std::vector<int64_t>, uint32_t, uint32_t>;

using PullCacheParamTuple = std::tuple<int64_t, uint32_t, std::vector<uint64_t>, std::vector<uint64_t>,
                                       std::vector<uint64_t>, std::vector<uint64_t>, int64_t, int64_t, uint64_t>;

using CacheKeyTuple = std::tuple<uint64_t, int64_t, uint64_t, uint64_t, uint64_t, uint64_t, bool>;

using MemInfoTuple = std::tuple<uint32_t, uint64_t, uint64_t>;

using CopyCacheParamTuple = std::tuple<int64_t,
                                       int64_t,
                                       uint32_t,
                                       uint32_t,
                                       uint64_t,
                                       int64_t,
                                       uint64_t,
std::vector<std::pair<uint64_t, uint64_t>>>;

using TransferBlockConfigTuple = std::tuple<uint64_t, std::vector<uint64_t>, std::vector<uint64_t>>;

using TransferCacheConfigTuple =
  std::tuple<uint64_t, uint64_t, uint64_t, std::vector<uintptr_t>, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
             uint64_t>;

class LLMDataDistV2Wrapper {
 public:
  static std::vector<llm::ClusterInfo> UnpackClusterInfos(const std::vector<ClusterInfoTuple> &clusters);
  static CacheDesc UnpackCacheDesc(const CacheDescTuple &cache_desc_tuple);
  static CacheKey UnpackCacheKey(const CacheKeyTuple &cache_key_tuple);
  static std::vector<CacheKey> UnpackCacheKeys(const std::vector<CacheKeyTuple> &cache_key_tuples);
  static PullCacheParam UnpackPullCacheParam(const PullCacheParamTuple &pull_cache_param_tuple);
  static CopyCacheParam UnpackCopyCacheParam(CopyCacheParamTuple cache_param_tuple);
  static LLMMemInfo UnpackMemInfo(const MemInfoTuple &mem_info_tuple);
  static std::vector<LLMMemInfo> UnpackMemInfos(const std::vector<MemInfoTuple> &mem_info_tuples);
  static Cache UnpackCacheTuple(const CacheTuple &cache_tuple);
  static TransferCacheConfig UnpackTransferCacheConfig(const TransferCacheConfigTuple &transfer_cache_config_tuple);
  static TransferBlockConfig UnpackTransferBlockConfig(const TransferBlockConfigTuple &transfer_block_config_tuple);

  static ge::Status Init(uint64_t cluster_id, const std::map<std::string, std::string> &options);

  static void Finalize();

  static std::pair<ge::Status, uint64_t> Link(std::string &cluster_name,
                                              const std::map<uint64_t, uint32_t> &cluster2rank,
                                              std::string &rank_table);

  static ge::Status Unlink(uint64_t comm_id);

  static std::pair<ge::Status, std::vector<ge::Status>> LinkClusters(
      const std::vector<ClusterInfoTuple> &clusters,
      int32_t timeout);

  static std::pair<ge::Status, std::vector<ge::Status>> UnlinkClusters(
      const std::vector<ClusterInfoTuple> &clusters,
      int32_t timeout, bool force_flag);

  static std::pair<ge::Status, uint32_t> QueryRegisterMemStatus(uint64_t comm_id);

  static std::pair<ge::Status, CacheTuple> RegisterCache(const CacheDescTuple &cache_desc,
                                                         const std::vector<uintptr_t> &tensor_addrs,
                                                         const std::vector<CacheKeyTuple> &cache_keys,
                                                         bool remote_accessible);

  static ge::Status UnregisterCache(int64_t cache_id);

  static std::pair<ge::Status, CacheTuple> AllocateCache(const CacheDescTuple &cache_desc,
                                                         const std::vector<CacheKeyTuple> &cache_keys);

  static ge::Status DeallocateCache(int64_t cache_id);

  static ge::Status PullCache(int64_t cache_id, const CacheKeyTuple &cache_key,
                              const PullCacheParamTuple &pull_cache_param);

  static ge::Status CopyCache(CopyCacheParamTuple copy_cache_param);

  static ge::Status RemoveCacheKey(const CacheKeyTuple &cache_key_tuple);

  static ge::Status RemapRegisteredMemory(const std::vector<MemInfoTuple> &mem_infos);

  static ge::Status SwapBlocks(const CacheTuple &src, const CacheTuple &dst, const uint64_t block_size,
                               const uint32_t type, const std::vector<std::pair<int64_t, int64_t>> &block_mapping);

  static ge::Status CheckCapacity(const size_t seq_len);

  static ge::Status TransferCache(const uint64_t task_id, const TransferCacheConfigTuple &transfer_cache_param,
                                  const TransferBlockConfigTuple &transfer_block_param =
                                      std::make_tuple(0, std::vector<uint64_t>{}, std::vector<uint64_t>{}));

  static ge::Status SwitchRole(const std::string &role, std::map<std::string, std::string> &options);

 private:
  static std::unique_ptr<LLMDataDistV2> llm_data_dist;
};
}  // namespace llm

#endif  // D_CANN_GRAPH_ENGINE_PYTHON_LLM_WRAPPER_LLM_DATADIST_V2_WRAPPER_H
