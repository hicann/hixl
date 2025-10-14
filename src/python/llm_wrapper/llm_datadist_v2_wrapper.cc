/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "llm_datadist_v2_wrapper.h"
#include "llm_datadist/llm_datadist.h"
#include "common/mem_utils.h"
#include "common/llm_checker.h"

namespace llm {
std::unique_ptr<LLMDataDistV2> LLMDataDistV2Wrapper::llm_data_dist;

namespace {}  // namespace

ge::Status LLMDataDistV2Wrapper::Init(uint64_t cluster_id, const std::map<std::string, std::string> &options) {
  LLM_CHK_BOOL_RET_STATUS(llm_data_dist == nullptr, ge::FAILED, "Repeat Init");
  std::unique_ptr<LLMDataDistV2> instance = llm::MakeUnique<LLMDataDistV2>(cluster_id);
  LLM_CHECK_NOTNULL(instance);

  std::map<ge::AscendString, ge::AscendString> engine_options;
  for (const auto &option : options) {
    (void)engine_options.emplace(option.first.c_str(), option.second.c_str());
  }
  LLM_CHK_STATUS_RET(instance->LLMDataDistInitialize(engine_options));
  llm_data_dist = std::move(instance);
  return ge::SUCCESS;
}

void LLMDataDistV2Wrapper::Finalize() {
  if (llm_data_dist != nullptr) {
    llm_data_dist->LLMDataDistFinalize();
    llm_data_dist.reset();
  }
}

std::pair<ge::Status, uint64_t> LLMDataDistV2Wrapper::Link(std::string &cluster_name,
                                                           const std::map<uint64_t, uint32_t> &cluster2rank,
                                                           std::string &rank_table) {
  ge::Status ret = ge::FAILED;
  uint64_t comm_id = 0UL;
  if (llm_data_dist != nullptr) {
    ret = llm_data_dist->Link(cluster_name, cluster2rank, rank_table, comm_id);
  }
  return {ret, comm_id};
}

ge::Status LLMDataDistV2Wrapper::Unlink(uint64_t comm_id) {
  return llm_data_dist->Unlink(comm_id);
}

std::pair<ge::Status, std::vector<ge::Status>> LLMDataDistV2Wrapper::LinkClusters(
    const std::vector<ClusterInfoTuple> &clusters, int32_t timeout) {
  ge::Status ret = ge::FAILED;
  std::vector<ge::Status> rets;
  if (llm_data_dist != nullptr) {
    auto cluster_infos = LLMDataDistV2Wrapper::UnpackClusterInfos(clusters);
    ret = llm_data_dist->LinkClusters(cluster_infos, rets, timeout);
  }
  return {ret, rets};
}

std::pair<ge::Status, std::vector<ge::Status>> LLMDataDistV2Wrapper::UnlinkClusters(
    const std::vector<ClusterInfoTuple> &clusters, int32_t timeout, bool force_flag) {
  ge::Status ret = ge::FAILED;
  std::vector<ge::Status> rets;
  if (llm_data_dist != nullptr) {
    auto cluster_infos = LLMDataDistV2Wrapper::UnpackClusterInfos(clusters);
    ret = llm_data_dist->UnlinkClusters(cluster_infos, rets, timeout, force_flag);
  }
  return {ret, rets};
}

std::pair<ge::Status, uint32_t> LLMDataDistV2Wrapper::QueryRegisterMemStatus(uint64_t comm_id) {
  RegisterMemoryStatus status;
  auto ret = llm_data_dist->QueryRegisterMemStatus(comm_id, status);
  return {ret, static_cast<uint32_t>(status)};
}

std::pair<ge::Status, CacheTuple> LLMDataDistV2Wrapper::RegisterCache(const CacheDescTuple &cache_desc,
                                                                      const std::vector<uintptr_t> &tensor_addrs,
                                                                      const std::vector<CacheKeyTuple> &cache_keys,
                                                                      bool remote_accessible) {
  ge::Status ret = ge::FAILED;
  CacheTuple result;
  if (llm_data_dist != nullptr) {
    Cache cache;
    cache.per_device_tensor_addrs = {tensor_addrs};
    auto real_cache_desc = LLMDataDistV2Wrapper::UnpackCacheDesc(cache_desc);
    real_cache_desc.remote_accessible = remote_accessible;
    ret = llm_data_dist->RegisterCache(real_cache_desc, cache,
                                       LLMDataDistV2Wrapper::UnpackCacheKeys(cache_keys));
    result = std::make_tuple(cache.cache_id, std::move(cache.per_device_tensor_addrs));
  }
  return {ret, result};
}

ge::Status LLMDataDistV2Wrapper::UnregisterCache(int64_t cache_id) {
  ge::Status ret = ge::FAILED;
  if (llm_data_dist != nullptr) {
    ret = llm_data_dist->UnregisterCache(cache_id);
  }
  return ret;
}

std::pair<ge::Status, CacheTuple> LLMDataDistV2Wrapper::AllocateCache(const CacheDescTuple &cache_desc,
                                                                      const std::vector<CacheKeyTuple> &cache_keys) {
  ge::Status ret = ge::FAILED;
  CacheTuple result;
  if (llm_data_dist != nullptr) {
    Cache cache;
    ret = llm_data_dist->AllocateCache(LLMDataDistV2Wrapper::UnpackCacheDesc(cache_desc), cache,
                                       LLMDataDistV2Wrapper::UnpackCacheKeys(cache_keys));
    result = std::make_tuple(cache.cache_id, std::move(cache.per_device_tensor_addrs));
  }
  return {ret, result};
}

ge::Status LLMDataDistV2Wrapper::DeallocateCache(int64_t cache_id) {
  LLM_CHECK_NOTNULL(llm_data_dist);
  return llm_data_dist->DeallocateCache(cache_id);
}

ge::Status LLMDataDistV2Wrapper::PullCache(int64_t cache_id, const CacheKeyTuple &cache_key,
                                           const PullCacheParamTuple &pull_cache_param) {
  LLM_CHECK_NOTNULL(llm_data_dist);
  return llm_data_dist->PullCache(cache_id, LLMDataDistV2Wrapper::UnpackCacheKey(cache_key),
                                  LLMDataDistV2Wrapper::UnpackPullCacheParam(pull_cache_param));
}

ge::Status LLMDataDistV2Wrapper::CopyCache(CopyCacheParamTuple copy_cache_param) {
  LLM_CHECK_NOTNULL(llm_data_dist);
  return llm_data_dist->CopyCache(LLMDataDistV2Wrapper::UnpackCopyCacheParam(std::move(copy_cache_param)));
}

ge::Status LLMDataDistV2Wrapper::RemoveCacheKey(const CacheKeyTuple &cache_key_tuple) {
  LLM_CHECK_NOTNULL(llm_data_dist);
  return llm_data_dist->RemoveCacheKey(LLMDataDistV2Wrapper::UnpackCacheKey(cache_key_tuple));
}

ge::Status LLMDataDistV2Wrapper::RemapRegisteredMemory(const std::vector<MemInfoTuple> &mem_infos) {
  LLM_CHECK_NOTNULL(llm_data_dist);
  return llm_data_dist->RemapRegisteredMemory(LLMDataDistV2Wrapper::UnpackMemInfos(mem_infos));
}

ge::Status LLMDataDistV2Wrapper::SwapBlocks(const CacheTuple &src, const CacheTuple &dst, const uint64_t block_size,
                                            const uint32_t type,
                                            const std::vector<std::pair<int64_t, int64_t>> &block_mapping) {
  LLM_CHECK_NOTNULL(llm_data_dist);
  return llm_data_dist->SwapBlocks(LLMDataDistV2Wrapper::UnpackCacheTuple(src),
                                   LLMDataDistV2Wrapper::UnpackCacheTuple(dst),
                                   block_size, type, block_mapping);
}

ge::Status LLMDataDistV2Wrapper::CheckCapacity(const size_t seq_len) {
  LLM_CHECK_NOTNULL(llm_data_dist);
  return llm_data_dist->CheckCapacity(seq_len);
}

ge::Status LLMDataDistV2Wrapper::TransferCache(const uint64_t task_id,
                                               const TransferCacheConfigTuple &transfer_cache_param,
                                               const TransferBlockConfigTuple &transfer_block_param) {
  LLM_CHECK_NOTNULL(llm_data_dist);
  return llm_data_dist->TransferCache(task_id, LLMDataDistV2Wrapper::UnpackTransferCacheConfig(transfer_cache_param),
                                      LLMDataDistV2Wrapper::UnpackTransferBlockConfig(transfer_block_param));
}

ge::Status LLMDataDistV2Wrapper::SwitchRole(const std::string &role, std::map<std::string, std::string> &options) {
  LLM_CHECK_NOTNULL(llm_data_dist);
  return llm_data_dist->SwitchRole(role, options);
}

CopyCacheParam LLMDataDistV2Wrapper::UnpackCopyCacheParam(CopyCacheParamTuple cache_param_tuple) {
  constexpr size_t kIndexDstCacheId = 0;
  constexpr size_t kIndexSrcCacheId = 1;
  constexpr size_t kIndexDstCBatchIndex = 2;
  constexpr size_t kIndexSrcCBatchIndex = 3;
  constexpr size_t kIndexOffset = 4;
  constexpr size_t kIndexSize = 5;
  constexpr size_t kIndexReqId = 6;
  constexpr size_t kIndexBlockInfos = 7;
  CopyCacheParam copy_cache_param;
  copy_cache_param.dst_cache_id = std::get<kIndexDstCacheId>(cache_param_tuple);
  copy_cache_param.src_cache_id = std::get<kIndexSrcCacheId>(cache_param_tuple);
  copy_cache_param.dst_batch_index = std::get<kIndexDstCBatchIndex>(cache_param_tuple);
  copy_cache_param.src_batch_index = std::get<kIndexSrcCBatchIndex>(cache_param_tuple);
  copy_cache_param.offset = std::get<kIndexOffset>(cache_param_tuple);
  copy_cache_param.size = std::get<kIndexSize>(cache_param_tuple);
  copy_cache_param.req_id = std::get<kIndexReqId>(cache_param_tuple);
  copy_cache_param.copy_block_infos = std::move(std::get<kIndexBlockInfos>(cache_param_tuple));
  return copy_cache_param;
}

CacheDesc LLMDataDistV2Wrapper::UnpackCacheDesc(const CacheDescTuple &cache_desc_tuple) {
  constexpr size_t kIndexNumTensors = 0;
  constexpr size_t kIndexDataType = 1;
  constexpr size_t kIndexDimIndex = 2;
  constexpr size_t kIndexShape = 3;
  constexpr size_t kPlacement = 4;
  constexpr size_t kIsBlocks = 5;
  CacheDesc cache_desc{};
  cache_desc.num_tensors = std::get<kIndexNumTensors>(cache_desc_tuple);
  cache_desc.data_type = static_cast<ge::DataType>(std::get<kIndexDataType>(cache_desc_tuple));
  cache_desc.seq_len_dim_index = std::get<kIndexDimIndex>(cache_desc_tuple);
  cache_desc.shape = std::get<kIndexShape>(cache_desc_tuple);
  cache_desc.placement = std::get<kPlacement>(cache_desc_tuple);
  auto is_blocks = std::get<kIsBlocks>(cache_desc_tuple);
  cache_desc.cache_mem_type = is_blocks == 0 ? CacheMemType::CACHE : CacheMemType::BLOCKS;
  return cache_desc;
}

CacheKey LLMDataDistV2Wrapper::UnpackCacheKey(const CacheKeyTuple &cache_key_tuple) {
  constexpr size_t kIndexPromptClusterId = 0;
  constexpr size_t kIndexPromptCacheId = 1;
  constexpr size_t kIndexPromptBatchIndex = 2;
  constexpr size_t kIndexReqId = 3;
  constexpr size_t kIndexPrefixId = 4;
  constexpr size_t kIndexModelId = 5;
  constexpr size_t kIndexIsAllocateBlocks = 6;
  CacheKey cache_key{};
  cache_key.prompt_cluster_id = std::get<kIndexPromptClusterId>(cache_key_tuple);
  cache_key.prompt_cache_id = std::get<kIndexPromptCacheId>(cache_key_tuple);
  cache_key.prompt_batch_index = std::get<kIndexPromptBatchIndex>(cache_key_tuple);
  cache_key.req_id = std::get<kIndexReqId>(cache_key_tuple);
  cache_key.prefix_id = std::get<kIndexPrefixId>(cache_key_tuple);
  cache_key.model_id = std::get<kIndexModelId>(cache_key_tuple);
  cache_key.is_allocate_blocks = std::get<kIndexIsAllocateBlocks>(cache_key_tuple);
  return cache_key;
}

Cache LLMDataDistV2Wrapper::UnpackCacheTuple(const CacheTuple &cache_tuple) {
  constexpr size_t kIndexCacheId = 0;
  constexpr size_t kIndexAddrs = 1;
  Cache cache{};
  cache.cache_id = std::get<kIndexCacheId>(cache_tuple);
  cache.per_device_tensor_addrs = std::move(std::get<kIndexAddrs>(cache_tuple));
  return cache;
}

std::vector<CacheKey> LLMDataDistV2Wrapper::UnpackCacheKeys(const std::vector<CacheKeyTuple> &cache_key_tuples) {
  std::vector<CacheKey> cache_keys;
  cache_keys.reserve(cache_key_tuples.size());
  for (const auto &cache_key_tuple : cache_key_tuples) {
    cache_keys.emplace_back(UnpackCacheKey(cache_key_tuple));
  }
  return cache_keys;
}

LLMMemInfo LLMDataDistV2Wrapper::UnpackMemInfo(const MemInfoTuple &mem_info_tuple) {
  constexpr size_t kIndexMemType = 0;
  constexpr size_t kIndexAddr = 1;
  constexpr size_t kIndexSize = 2;
  LLMMemInfo mem_info{};
  mem_info.mem_type = static_cast<LLMMemType>(std::get<kIndexMemType>(mem_info_tuple));
  mem_info.addr = std::get<kIndexAddr>(mem_info_tuple);
  mem_info.size = std::get<kIndexSize>(mem_info_tuple);
  return mem_info;
}

std::vector<LLMMemInfo> LLMDataDistV2Wrapper::UnpackMemInfos(const std::vector<MemInfoTuple> &mem_info_tuples) {
  std::vector<LLMMemInfo> mem_infos;
  mem_infos.reserve(mem_info_tuples.size());
  for (const auto &mem_info_tuple : mem_info_tuples) {
    mem_infos.emplace_back(UnpackMemInfo(mem_info_tuple));
  }
  return mem_infos;
}

PullCacheParam LLMDataDistV2Wrapper::UnpackPullCacheParam(const PullCacheParamTuple &pull_cache_param_tuple) {
  constexpr size_t kIndexSize = 0;
  constexpr size_t kIndexBatchIndex = 1;
  constexpr size_t kIndexPromptBlocks = 2;
  constexpr size_t kIndexDecoderBlocks = 3;
  constexpr size_t kIndexSrcTensorIndices = 4;
  constexpr size_t kIndexDstTensorIndices = 5;
  constexpr size_t kIndexSrcCacheOffset = 6;
  constexpr size_t kIndexDstCacheOffset = 7;
  constexpr size_t kIndexTensorNumPerLayerIndex = 8;
  PullCacheParam pull_cache_param{};
  pull_cache_param.size = std::get<kIndexSize>(pull_cache_param_tuple);
  pull_cache_param.batch_index = std::get<kIndexBatchIndex>(pull_cache_param_tuple);
  pull_cache_param.prompt_blocks = std::get<kIndexPromptBlocks>(pull_cache_param_tuple);
  pull_cache_param.decoder_blocks = std::get<kIndexDecoderBlocks>(pull_cache_param_tuple);
  pull_cache_param.src_tensor_indices = std::get<kIndexSrcTensorIndices>(pull_cache_param_tuple);
  pull_cache_param.dst_tensor_indices = std::get<kIndexDstTensorIndices>(pull_cache_param_tuple);
  pull_cache_param.src_cache_offset = std::get<kIndexSrcCacheOffset>(pull_cache_param_tuple);
  pull_cache_param.dst_cache_offset = std::get<kIndexDstCacheOffset>(pull_cache_param_tuple);
  pull_cache_param.tensor_num_per_layer = std::get<kIndexTensorNumPerLayerIndex>(pull_cache_param_tuple);
  return pull_cache_param;
}

TransferCacheConfig LLMDataDistV2Wrapper::UnpackTransferCacheConfig(
    const TransferCacheConfigTuple &transfer_cache_config_tuple) {
  constexpr size_t kIndexCacheId = 0;
  constexpr size_t kIndexBatchIndex = 1;
  constexpr size_t kIndexLayerIndex = 2;
  constexpr size_t kIndexDstAddrs = 3;
  constexpr size_t kIndexClusterId = 4;
  constexpr size_t kIndexModelId = 5;
  constexpr size_t kIndexDstBatchIndex = 6;
  constexpr size_t kIndexType = 7;
  constexpr size_t kIndexDstLayerIndex = 8;
  constexpr size_t kIndexTensorNumPerLayerIndex = 9;
  TransferCacheConfig transfer_cache_config{};
  transfer_cache_config.src_cache_id = std::get<kIndexCacheId>(transfer_cache_config_tuple);
  transfer_cache_config.batch_index = std::get<kIndexBatchIndex>(transfer_cache_config_tuple);
  transfer_cache_config.layer_index = std::get<kIndexLayerIndex>(transfer_cache_config_tuple);
  transfer_cache_config.dst_addrs = std::get<kIndexDstAddrs>(transfer_cache_config_tuple);
  transfer_cache_config.cluster_id = std::get<kIndexClusterId>(transfer_cache_config_tuple);
  transfer_cache_config.model_id_or_cache_id = std::get<kIndexModelId>(transfer_cache_config_tuple);
  transfer_cache_config.dst_batch_index = std::get<kIndexDstBatchIndex>(transfer_cache_config_tuple);
  transfer_cache_config.type = std::get<kIndexType>(transfer_cache_config_tuple);
  transfer_cache_config.dst_layer_index = std::get<kIndexDstLayerIndex>(transfer_cache_config_tuple);
  transfer_cache_config.tensor_num_per_layer = std::get<kIndexTensorNumPerLayerIndex>(transfer_cache_config_tuple);
  return transfer_cache_config;
}

TransferBlockConfig LLMDataDistV2Wrapper::UnpackTransferBlockConfig(
    const TransferBlockConfigTuple &transfer_block_config_tuple) {
  constexpr size_t kIndexBlockMemSize = 0;
  constexpr size_t kIndexSrcBlocks = 1;
  constexpr size_t kIndexDstBlocks = 2;
  TransferBlockConfig transfer_block_config{};
  transfer_block_config.block_mem_size = std::get<kIndexBlockMemSize>(transfer_block_config_tuple);
  transfer_block_config.src_blocks = std::get<kIndexSrcBlocks>(transfer_block_config_tuple);
  transfer_block_config.dst_blocks = std::get<kIndexDstBlocks>(transfer_block_config_tuple);
  return transfer_block_config;
}

std::vector<llm::ClusterInfo> LLMDataDistV2Wrapper::UnpackClusterInfos(const std::vector<ClusterInfoTuple> &clusters) {
  constexpr size_t kIndexRemoteClusterId = 0;
  constexpr size_t kIndexRemoteRoleType = 1;
  constexpr size_t kIndexLocalIpInfos = 2;
  constexpr size_t kIndexRemoteIpInfos = 3;
  std::vector<llm::ClusterInfo> cluster_infos;
  for (const auto &cluster : clusters) {
    llm::ClusterInfo cluster_info;
    cluster_info.remote_cluster_id = std::get<kIndexRemoteClusterId>(cluster);
    cluster_info.remote_role_type = std::get<kIndexRemoteRoleType>(cluster);
    for (const auto &ip_and_port : std::get<kIndexLocalIpInfos>(cluster)) {
      IpInfo ip_info;
      ip_info.ip = ip_and_port.first;
      ip_info.port = ip_and_port.second;
      cluster_info.local_ip_infos.emplace_back(ip_info);
    }
    for (const auto &ip_and_port : std::get<kIndexRemoteIpInfos>(cluster)) {
      IpInfo ip_info;
      ip_info.ip = ip_and_port.first;
      ip_info.port = ip_and_port.second;
      cluster_info.remote_ip_infos.emplace_back(ip_info);
    }
    cluster_infos.emplace_back(std::move(cluster_info));
  }
  return cluster_infos;
}
}  // namespace llm
