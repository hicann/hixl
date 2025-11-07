/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_LLM_INNER_TYPES_H
#define CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_LLM_INNER_TYPES_H

#include <vector>
#include "ge_common/ge_api_types.h"
#include "llm_log.h"

namespace llm {
using char_t = ge::char_t;
using float32_t = ge::float32_t;
using float64_t = ge::float64_t;

constexpr const char kMix[] = "Mix";
constexpr const char kLlmOptionListenIp[] = "llm.ListenIp";
constexpr const char kLlmOptionListenPort[] = "llm.ListenPort";
constexpr const char kLlmOptionEnableRemoteCacheAccessible[] = "llm.EnableRemoteCacheAccessible";
constexpr uint64_t kDefaultTensorNumPerLayer = 2U;

struct IpInfo {
  uint32_t ip = 0U;
  uint16_t port = 0U;
};

struct ClusterInfo {
  uint64_t remote_cluster_id = 0U;
  int32_t remote_role_type = 0;
  std::vector<IpInfo> local_ip_infos;
  std::vector<IpInfo> remote_ip_infos;
};

#pragma pack(push, 1)
enum class RegisterMemoryStatus : uint32_t { OK = 0U, PREPARING = 1U, FAILED = 2U };

enum class LLMMemType : uint32_t { MEM_TYPE_DEVICE = 0U, MEM_TYPE_HOST = 1U };

struct LLMMemInfo {
  LLMMemType mem_type = LLMMemType::MEM_TYPE_DEVICE;
  uint64_t addr = 0UL;
  uint64_t size = 0UL;
};

struct CacheKey {
  uint64_t prompt_cluster_id;
  int64_t prompt_cache_id = -1;
  uint64_t prompt_batch_index = 0UL;
  uint64_t req_id;
  uint64_t prefix_id = UINT64_MAX;
  uint64_t model_id;
  bool is_allocate_blocks = false;
};

enum class CachePlacement : uint32_t { HOST = 0U, DEVICE = 1U };

enum class CacheMemType : uint32_t { CACHE = 0U, BLOCKS = 1U, MIX = 2U };

struct CacheDesc {
  uint32_t num_tensors;
  ge::DataType data_type;
  int32_t seq_len_dim_index = -1;
  std::vector<int64_t> shape;
  uint32_t placement = 1U;
  CacheMemType cache_mem_type = CacheMemType::CACHE;
  bool remote_accessible = true;
};

struct Cache {
  int64_t cache_id = -1;
  std::vector<std::vector<uintptr_t>> per_device_tensor_addrs;
};

struct PullCacheParam {
  int64_t size = -1;
  uint32_t batch_index = 0U;
  std::vector<uint64_t> prompt_blocks;
  std::vector<uint64_t> decoder_blocks;
  std::vector<uint64_t> src_tensor_indices;
  std::vector<uint64_t> dst_tensor_indices;
  int64_t src_cache_offset = -1;
  int64_t dst_cache_offset = -1;
  uint64_t tensor_num_per_layer = kDefaultTensorNumPerLayer;
};

struct TransferCacheConfig {
  int64_t src_cache_id = 0U;
  uint64_t batch_index = 0U;
  uint64_t layer_index = 0U;
  std::vector<uintptr_t> dst_addrs;
  uint64_t cluster_id = 0U;
  uint64_t model_id_or_cache_id = 0U;
  int64_t dst_cache_id = 0U;
  uint64_t dst_batch_index = 0U;
  uint64_t type = 0U;
  uint64_t dst_layer_index = 0U;
  uint64_t tensor_num_per_layer = kDefaultTensorNumPerLayer;
};

struct TransferBlockConfig {
  uint64_t block_mem_size = 0U;
  std::vector<uint64_t> src_blocks;
  std::vector<uint64_t> dst_blocks;
};

struct CopyCacheParam {
  int64_t src_cache_id = -1;
  int64_t dst_cache_id = -1;
  uint32_t src_batch_index = 0U;
  uint32_t dst_batch_index = 0U;
  uint64_t offset = 0UL;
  int64_t size = -1L;
  uint64_t req_id = UINT64_MAX;  // just for logging
  bool mbuf_involved = false;
  std::vector<std::pair<uint64_t, uint64_t>> copy_block_infos;
};
#pragma pack(pop)

inline bool operator==(const IpInfo &lhs, const IpInfo &rhs) {
  return (lhs.ip == rhs.ip) && (lhs.port == rhs.port);
}
}  // namespace llm

#endif  // CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_LLM_INNER_TYPES_H
