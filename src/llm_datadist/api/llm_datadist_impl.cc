/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "llm_datadist/llm_datadist.h"
#include "llm_datadist/llm_engine_types.h"
#include "common/llm_inner_types.h"
#include "common/common.h"
#include "cache_mgr/cache_manager.h"
#include "llm_datadist_v2.h"
#include "common/llm_utils.h"
#include "common/mem_utils.h"
#include "common/def_types.h"
#include "common/llm_common.h"
#include "nlohmann/json.hpp"
#include "runtime/rt.h"
#include "common/llm_checker.h"
#include "common/llm_scope_guard.h"

namespace llm_datadist {
namespace {
constexpr std::pair<int32_t, int32_t> kDefaultLayerRange{-1, -1};

std::string RoleToString(LlmRole role) {
  static const std::map<LlmRole, std::string> kRoleToStr{
      {LlmRole::kPrompt, llm::kPrompt},
      {LlmRole::kDecoder, llm::kDecoder},
      {LlmRole::kMix, llm::kMix},
  };
  const auto it = kRoleToStr.find(role);
  return it != kRoleToStr.cend() ? it->second : "";
}

Status ConvertIpInfos(const std::vector<IpInfo> &ip_infos, std::vector<llm::IpInfo> &ret) {
  ret.reserve(ip_infos.size());
  for (const auto &ip_info : ip_infos) {
    llm::IpInfo llm_ip_info;
    LLM_CHK_BOOL_RET_STATUS(ip_info.ip.GetLength() > 0, LLM_PARAM_INVALID, "ip not set");
    LLM_CHK_STATUS_RET_NOLOG(llm::LLMUtils::IpToInt(ip_info.ip.GetString(), llm_ip_info.ip));
    llm_ip_info.port = ip_info.port;
    ret.emplace_back(llm_ip_info);
  }
  return LLM_SUCCESS;
}

Status ConvertClusterInfos(const std::vector<ClusterInfo> &cluster_infos, std::vector<llm::ClusterInfo> &ret) {
  for (size_t i = 0U; i < cluster_infos.size(); ++i) {
    const auto &cluster_info = cluster_infos[i];
    llm::ClusterInfo llm_cluster_info{};
    llm_cluster_info.remote_cluster_id = cluster_info.remote_cluster_id;
    llm_cluster_info.remote_role_type = cluster_info.remote_role_type;
    LLM_CHK_STATUS_RET(ConvertIpInfos(cluster_info.remote_ip_infos, llm_cluster_info.remote_ip_infos),
                      "Failed to parse remote ip info, index = %zu", i);
    LLM_CHK_STATUS_RET(ConvertIpInfos(cluster_info.local_ip_infos, llm_cluster_info.local_ip_infos),
                      "Failed to parse local ip info, index = %zu", i);
    ret.emplace_back(std::move(llm_cluster_info));
  }
  return LLM_SUCCESS;
}

llm::CacheKey ToCacheKey(const CacheIndex &src_cache_index, bool dst_is_cache) {
  llm::CacheKey cache_key{};
  cache_key.prompt_cluster_id = src_cache_index.cluster_id;
  cache_key.prompt_cache_id = src_cache_index.cache_id;
  if (dst_is_cache) {
    cache_key.prompt_batch_index = src_cache_index.batch_index;
  }
  return cache_key;
}

Status CheckKvCacheExtParam(const KvCacheExtParam &ext_param, bool check_range_same = false) {
  LLM_CHK_BOOL_RET_STATUS((ext_param.src_layer_range.first >= -1 && ext_param.src_layer_range.second >= -1 &&
                          ext_param.src_layer_range.first <= ext_param.src_layer_range.second),
                          ge::LLM_PARAM_INVALID, "Invalid src layer:[%d,%d], layer need >= -1, and first <= second.",
                          ext_param.src_layer_range.first, ext_param.src_layer_range.second);
  LLM_CHK_BOOL_RET_STATUS((ext_param.dst_layer_range.first >= -1 && ext_param.dst_layer_range.second >= -1 &&
                          ext_param.dst_layer_range.first <= ext_param.dst_layer_range.second),
                          ge::LLM_PARAM_INVALID, "Invalid dst layer:[%d,%d], layer need >= -1, and first <= second.",
                          ext_param.dst_layer_range.first, ext_param.dst_layer_range.second);
  LLM_CHK_BOOL_RET_STATUS((ext_param.tensor_num_per_layer > 0), ge::LLM_PARAM_INVALID,
                          "Invalid tensor_num_per_layer:[%u], tensor_num_per_layer need > 0.",
                          static_cast<uint32_t>(ext_param.tensor_num_per_layer));
  if (check_range_same) {
    LLM_CHK_BOOL_RET_STATUS((ext_param.src_layer_range.second - ext_param.src_layer_range.first) ==
                           (ext_param.dst_layer_range.second - ext_param.dst_layer_range.first),
                           ge::LLM_PARAM_INVALID,
                          "src_layer_range should keep the same size as dst_layer_range.");
  }
  return LLM_SUCCESS;
}

static int32_t GetCacheMaxLayer(const Cache &cache, const KvCacheExtParam &ext_param) {
  // the range is [a, b], including a and b
  int32_t max_layer = 0;
  // when num_tensors < tensor_num_per_layer, all treat as one layer, for history default input error case 
  if (cache.cache_desc.num_tensors >= ext_param.tensor_num_per_layer) {
    if (cache.cache_desc.num_tensors % ext_param.tensor_num_per_layer == 0) {
      max_layer = static_cast<int32_t>(cache.cache_desc.num_tensors / ext_param.tensor_num_per_layer) - 1;
    } else {
      // treat last part as one layer, for history default input error case
      max_layer = static_cast<int32_t>(cache.cache_desc.num_tensors / ext_param.tensor_num_per_layer);
    }
  }
  return max_layer;
}

// not calc default layer ranges
void CalcIndicesWithValidRanges(const KvCacheExtParam &ext_param, llm::PullCacheParam &pull_cache_param) {
  if (ext_param.src_layer_range.first < 0 || ext_param.dst_layer_range.first < 0) {
    return;
  }

  uint64_t indices_beg = static_cast<uint64_t>(ext_param.src_layer_range.first * ext_param.tensor_num_per_layer);
  uint64_t range = static_cast<uint64_t>((ext_param.src_layer_range.second - ext_param.src_layer_range.first + 1) *
                                          ext_param.tensor_num_per_layer);
  for (uint64_t i = 0; i < range; i++) {
    pull_cache_param.src_tensor_indices.push_back(indices_beg + i);
  }

  indices_beg = static_cast<uint64_t>(ext_param.dst_layer_range.first * ext_param.tensor_num_per_layer);
  range = static_cast<uint64_t>((ext_param.dst_layer_range.second - ext_param.dst_layer_range.first + 1) *
                                ext_param.tensor_num_per_layer);
  for (uint64_t i = 0; i < range; i++) {
    pull_cache_param.dst_tensor_indices.push_back(indices_beg + i);
  }
}
}  // namespace

class LlmDataDist::LlmDataDistImpl {
 public:
  explicit LlmDataDistImpl(const uint64_t cluster_id, LlmRole role)
      : llm_data_dist_(cluster_id), role_(role), cluster_id_(cluster_id) {}
  ~LlmDataDistImpl() = default;

  Status Initialize(const std::map<AscendString, AscendString> &options);
  void Finalize();

  Status SetRole(LlmRole role, const std::map<AscendString, AscendString> &options);

  Status LinkLlmClusters(const std::vector<ClusterInfo> &clusters,
                         std::vector<Status> &rets,
                         int32_t timeout);

  Status UnlinkLlmClusters(const std::vector<ClusterInfo> &clusters,
                           std::vector<Status> &rets,
                           int32_t timeout,
                           bool force_flag);

  Status PullKvCache(const CacheIndex &src_cache_index,
                     const Cache &dst_cache,
                     uint32_t batch_index,
                     int64_t size,
                     const KvCacheExtParam &ext_param);

  Status PullKvBlocks(const CacheIndex &src_cache_index,
                      const Cache &dst_cache,
                      const std::vector<uint64_t> &src_blocks,
                      const std::vector<uint64_t> &dst_blocks,
                      const KvCacheExtParam &dst_param);

  Status PushKvCache(const Cache &src_cache,
                     const CacheIndex &dst_cache_index,
                     uint32_t src_batch_index = 0U,
                     int64_t size = -1,
                     const KvCacheExtParam &ext_param = {});

  Status PushKvBlocks(const Cache &src_cache,
                      const CacheIndex &dst_cache_index,
                      const std::vector<uint64_t> &src_blocks,
                      const std::vector<uint64_t> &dst_blocks,
                      const KvCacheExtParam &ext_param = {});

  Status RegisterKvCache(const CacheDesc &cache_desc,
                         const std::vector<uint64_t> &addrs,
                         const RegisterCfg &cfg,
                         int64_t &cache_id);

  Status UnregisterKvCache(int64_t cache_id);

 private:
  Status PushData(const Cache &src_cache, const KvCacheExtParam &ext_param,
                  llm::TransferCacheConfig &transfer_cache_config,
                  llm::TransferBlockConfig &transfer_block_config);

  std::mutex mutex_;
  llm::LLMDataDistV2 llm_data_dist_;
  LlmRole role_;
  uint64_t cluster_id_;
  std::vector<int32_t> device_ids_;
};

Status LlmDataDist::LlmDataDistImpl::Initialize(const std::map<AscendString, AscendString> &options) {
  std::lock_guard<std::mutex> lk(mutex_);
  LLM_CHK_BOOL_RET_SPECIAL_STATUS((llm_data_dist_.IsInitialized()),
                                  LLM_SUCCESS, "Already initialized");
  const auto &role_str = RoleToString(role_);
  LLM_CHK_BOOL_RET_STATUS((!role_str.empty()),
                         LLM_PARAM_INVALID,
                         "Invalid role: %d",
                         static_cast<int32_t>(role_));
  LLM_DISMISSABLE_GUARD(clear_guard, [this]() {
    device_ids_.clear();
  });
  LLM_CHK_STATUS_RET(llm::LLMUtils::ParseDeviceId(options, device_ids_, llm_datadist::OPTION_DEVICE_ID),
                    "Failed to get device_id from options");
  LLM_CHK_BOOL_RET_STATUS(!device_ids_.empty(), ge::LLM_PARAM_INVALID, "option %s can not be empty.",
                         llm_datadist::OPTION_DEVICE_ID);
  auto init_options = options;
  // set ge.exec.deviceId
  init_options[ge::OPTION_EXEC_DEVICE_ID] = init_options[llm_datadist::OPTION_DEVICE_ID];
  if (init_options.find(ge::OPTION_SESSION_DEVICE_ID) == init_options.end()) {
    // ensure array index before.
    init_options[ge::OPTION_SESSION_DEVICE_ID] = std::to_string(device_ids_[0U]).c_str();
  }
  init_options[llm::LLM_OPTION_ROLE] = role_str.c_str();
  ge::AscendString local_comm_res;
  const auto &rank_it = init_options.find(llm_datadist::OPTION_LOCAL_COMM_RES);
  if (rank_it != init_options.cend()) {
    local_comm_res = rank_it->second;
  }

  std::string ip;
  uint32_t port = 0U;
  const auto &iter = options.find(llm_datadist::OPTION_LISTEN_IP_INFO);
  if (iter != options.cend()) {
    LLMLOGI("role:%s listen option %s=%s",
            role_str.c_str(), llm_datadist::OPTION_LISTEN_IP_INFO, iter->second.GetString());
    LLM_CHK_STATUS_RET(llm::LLMUtils::ParseListenIpInfo(options, ip, port), "Failed to parse listen ip info");
    init_options[llm::kLlmOptionListenIp] = ip.c_str();
    init_options[llm::kLlmOptionListenPort] = std::to_string(port).c_str();
  }
  init_options[llm::kLlmOptionEnableRemoteCacheAccessible] = "1";
  LLM_CHK_STATUS_RET(llm_data_dist_.LLMDataDistInitialize(init_options), "Failed to initialize LLMDataDist.");
  LLM_DISMISS_GUARD(clear_guard);
  return ge::SUCCESS;
}

void LlmDataDist::LlmDataDistImpl::Finalize() {
  llm_data_dist_.LLMDataDistFinalize();
  device_ids_.clear();
}

Status LlmDataDist::LlmDataDistImpl::LinkLlmClusters(const std::vector<ClusterInfo> &clusters,
                                                     std::vector<Status> &rets,
                                                     int32_t timeout) {
  LLM_CHK_BOOL_RET_STATUS((llm_data_dist_.IsInitialized()),
                         ge::FAILED, "LlmDataDist is not initialized");
  LLM_CHK_BOOL_RET_STATUS(timeout > 0, LLM_PARAM_INVALID, "Check timeout (%d) > 0 failed", timeout);
  std::vector<llm::ClusterInfo> cluster_infos;
  LLM_CHK_STATUS_RET(ConvertClusterInfos(clusters, cluster_infos), "Failed to parse cluster info");
  return llm_data_dist_.LinkClusters(cluster_infos, rets, timeout);
}

Status LlmDataDist::LlmDataDistImpl::UnlinkLlmClusters(const std::vector<ClusterInfo> &clusters,
                                                       std::vector<Status> &rets,
                                                       int32_t timeout,
                                                       bool force_flag) {
  LLM_CHK_BOOL_RET_STATUS((llm_data_dist_.IsInitialized()),
                         ge::FAILED, "LlmDataDist is not initialized");
  LLM_CHK_BOOL_RET_STATUS(timeout > 0, LLM_PARAM_INVALID, "Check timeout (%d) > 0 failed", timeout);
  std::vector<llm::ClusterInfo> cluster_infos;
  LLM_CHK_STATUS_RET(ConvertClusterInfos(clusters, cluster_infos), "Failed to unlink clusters");
  return llm_data_dist_.UnlinkClusters(cluster_infos, rets, timeout, force_flag);
}

Status LlmDataDist::LlmDataDistImpl::PullKvBlocks(const CacheIndex &src_cache_index,
                                                  const Cache &dst_cache,
                                                  const std::vector<uint64_t> &src_blocks,
                                                  const std::vector<uint64_t> &dst_blocks,
                                                  const KvCacheExtParam &dst_param) {
  LLM_CHK_BOOL_RET_STATUS((llm_data_dist_.IsInitialized()),
                         ge::FAILED, "LlmDataDist is not initialized");
  llm::PullCacheParam pull_cache_param{};
  LLM_CHK_BOOL_RET_STATUS(src_cache_index.batch_index == 0U,
                         LLM_PARAM_INVALID,
                         "invalid src_cache_index.batch_index (%u), only 0 is supported in pull block",
                         src_cache_index.batch_index);

  LLM_CHK_STATUS_RET(CheckKvCacheExtParam(dst_param));

  pull_cache_param.prompt_blocks = src_blocks;
  pull_cache_param.decoder_blocks = dst_blocks;
  pull_cache_param.tensor_num_per_layer = dst_param.tensor_num_per_layer;
  CalcIndicesWithValidRanges(dst_param, pull_cache_param);
  return llm_data_dist_.PullBlocks(dst_cache.cache_id,
                                   ToCacheKey(src_cache_index, dst_blocks.empty()),
                                   pull_cache_param);
}

Status LlmDataDist::LlmDataDistImpl::PullKvCache(const CacheIndex &src_cache_index,
                                                 const Cache &dst_cache,
                                                 uint32_t batch_index,
                                                 int64_t size,
                                                 const KvCacheExtParam &ext_param) {
  LLM_CHK_BOOL_RET_STATUS((llm_data_dist_.IsInitialized()),
                         ge::FAILED, "LlmDataDist is not initialized");
  LLM_CHK_BOOL_RET_STATUS((size == -1) || (size > 0),
                         LLM_PARAM_INVALID,
                         "Invalid size (%ld), size must = -1 or > 0",
                         size);

  LLM_CHK_STATUS_RET(CheckKvCacheExtParam(ext_param));

  llm::PullCacheParam pull_cache_param{};
  pull_cache_param.batch_index = batch_index;
  pull_cache_param.size = size;
  pull_cache_param.tensor_num_per_layer = ext_param.tensor_num_per_layer;
  CalcIndicesWithValidRanges(ext_param, pull_cache_param);
  return llm_data_dist_.PullCache(dst_cache.cache_id, ToCacheKey(src_cache_index, true), pull_cache_param);
}

Status LlmDataDist::LlmDataDistImpl::SetRole(LlmRole role, const std::map<AscendString, AscendString> &options) {
  LLM_CHK_BOOL_RET_STATUS((llm_data_dist_.IsInitialized()),
                         ge::FAILED, "LlmDataDist is not initialized");
  const auto &role_str = RoleToString(role);
  LLM_CHK_BOOL_RET_STATUS((!role_str.empty()),
                         LLM_PARAM_INVALID,
                         "Invalid role: %d",
                         static_cast<int32_t>(role));
  std::map<std::string, std::string> switch_options;
  for (const auto &key_and_value : options) {
    (void) switch_options.emplace(key_and_value.first.GetString(), key_and_value.second.GetString());
  }
  std::string ip;
  uint32_t port = 0U;
  const auto &iter = options.find(llm_datadist::OPTION_LISTEN_IP_INFO);
  if (iter != options.cend()) {
    LLMLOGI("Target role:%s listen option %s=%s",
           role_str.c_str(), llm_datadist::OPTION_LISTEN_IP_INFO, iter->second.GetString());
    LLM_CHK_STATUS_RET(llm::LLMUtils::ParseListenIpInfo(options, ip, port), "Failed to parse listen ip info");
    switch_options[llm::kLlmOptionListenIp] = ip;
    switch_options[llm::kLlmOptionListenPort] = std::to_string(port);
  }
  const auto ret = llm_data_dist_.SwitchRole(role_str, switch_options);
  LLM_CHK_BOOL_RET_STATUS(ret == LLM_SUCCESS,
                         ret,
                         "[SetRole] failed, prev_role = %s, target_role = %s",
                         RoleToString(role_).c_str(), role_str.c_str());
  LLMLOGI("[SetRole] success, prev_role = %s, target_role = %s",
         RoleToString(role_).c_str(), role_str.c_str());
  role_ = role;
  return LLM_SUCCESS;
}

Status LlmDataDist::LlmDataDistImpl::PushData(const Cache &src_cache, const KvCacheExtParam &ext_param,
                  llm::TransferCacheConfig &transfer_cache_config,
                  llm::TransferBlockConfig &transfer_block_config)
{
  auto src_layer_index = ext_param.src_layer_range.first;
  auto dst_layer_index = ext_param.dst_layer_range.first;
  // push one appointed layer
  if (src_layer_index >= 0 && (ext_param.src_layer_range.first == ext_param.src_layer_range.second)) {
    transfer_cache_config.layer_index = static_cast<uint64_t>(src_layer_index);
    transfer_cache_config.dst_layer_index = static_cast<uint64_t>(dst_layer_index);
    return llm_data_dist_.TransferCache(0U, transfer_cache_config, transfer_block_config);
  } else {
    int32_t cache_max_layer;
    // push all ranges without appointed layer
    if (src_layer_index < 0) {
      src_layer_index = 0;
      dst_layer_index = 0;
      cache_max_layer = GetCacheMaxLayer(src_cache, ext_param);
    // push the appointed mult-layers
    } else {
      cache_max_layer = ext_param.src_layer_range.second;
    }

    for (;src_layer_index <= cache_max_layer; src_layer_index++, dst_layer_index++) {
      transfer_cache_config.layer_index = static_cast<uint64_t>(src_layer_index);
      transfer_cache_config.dst_layer_index = static_cast<uint64_t>(dst_layer_index);
      const auto ret = llm_data_dist_.TransferCache(0U, transfer_cache_config, transfer_block_config);
      LLM_CHK_STATUS_RET(ret, "[PushKvCache] failed, src_layer_index = %d.", src_layer_index);
    }
  }
  return LLM_SUCCESS;
}

Status LlmDataDist::LlmDataDistImpl::PushKvCache(const Cache &src_cache,
                                                 const CacheIndex &dst_cache_index,
                                                 uint32_t src_batch_index,
                                                 int64_t size,
                                                 const KvCacheExtParam &ext_param) {
  LLM_CHK_BOOL_RET_STATUS((llm_data_dist_.IsInitialized()),
                         ge::FAILED, "LlmDataDist is not initialized");
  LLM_CHK_BOOL_RET_STATUS(src_cache.cache_desc.placement == CachePlacement::kDevice,
                         LLM_PARAM_INVALID, "Only support push to device cache");
  LLM_CHK_BOOL_RET_STATUS((size == -1), LLM_PARAM_INVALID,
                         "Invalid size (%ld), size only support -1 currently.", size);

  LLM_CHK_STATUS_RET(CheckKvCacheExtParam(ext_param, true));
  LLM_CHK_BOOL_RET_STATUS(((ext_param.src_layer_range.first >= 0) ||
                        (ext_param.src_layer_range.first == -1 && src_cache.cache_desc.num_tensors > 0)), LLM_PARAM_INVALID,
                        "Invalid src layer:%d, layer need >= 0 or -1 with valid num_tensors.", ext_param.src_layer_range.first);

  constexpr uint64_t kCacheKeyByIdType = 2UL;
  llm::TransferCacheConfig transfer_cache_config{};
  transfer_cache_config.src_cache_id = src_cache.cache_id;
  transfer_cache_config.batch_index = src_batch_index;
  transfer_cache_config.cluster_id = dst_cache_index.cluster_id;
  transfer_cache_config.dst_cache_id = dst_cache_index.cache_id;
  transfer_cache_config.model_id_or_cache_id = dst_cache_index.cache_id;
  transfer_cache_config.dst_batch_index = static_cast<uint64_t>(dst_cache_index.batch_index);
  transfer_cache_config.type = kCacheKeyByIdType;
  transfer_cache_config.tensor_num_per_layer = ext_param.tensor_num_per_layer;
  llm::TransferBlockConfig transfer_block_config{};
  return PushData(src_cache, ext_param, transfer_cache_config, transfer_block_config);
}

Status LlmDataDist::LlmDataDistImpl::PushKvBlocks(const Cache &src_cache,
                                                  const CacheIndex &dst_cache_index,
                                                  const std::vector<uint64_t> &src_blocks,
                                                  const std::vector<uint64_t> &dst_blocks,
                                                  const KvCacheExtParam &ext_param) {
  LLM_CHK_BOOL_RET_STATUS((llm_data_dist_.IsInitialized()),
                         ge::FAILED, "LlmDataDist is not initialized");
  LLM_CHK_BOOL_RET_STATUS(src_cache.cache_desc.placement == CachePlacement::kDevice,
                         LLM_PARAM_INVALID, "Only support push to device cache");

  LLM_CHK_BOOL_RET_STATUS(!src_blocks.empty(),
                         LLM_PARAM_INVALID,
                         "src_blocks is empty, push from non-block cache is not supported yet");
  LLM_CHK_BOOL_RET_STATUS(src_blocks.size() == dst_blocks.size(),
                         LLM_PARAM_INVALID,
                         "number of src_blocks (%zu) mismatches that of dst_blocks (%zu)",
                         src_blocks.size(), dst_blocks.size());
  LLM_CHK_BOOL_RET_STATUS(dst_cache_index.batch_index == 0U,
                         LLM_PARAM_INVALID,
                         "invalid dst_cache_index.batch_index (%u), only 0 is supported in push block",
                         dst_cache_index.batch_index);

  LLM_CHK_STATUS_RET(CheckKvCacheExtParam(ext_param, true));
  LLM_CHK_BOOL_RET_STATUS(((ext_param.src_layer_range.first >= 0) ||
                        (ext_param.src_layer_range.first == -1 && src_cache.cache_desc.num_tensors > 0)), LLM_PARAM_INVALID,
                        "Invalid src layer:%d, layer need >= 0, or -1 with valid num_tensors.", ext_param.src_layer_range.first);

  constexpr uint64_t kCacheKeyByIdType = 2UL;
  llm::TransferCacheConfig transfer_cache_config{};
  transfer_cache_config.src_cache_id = src_cache.cache_id;
  transfer_cache_config.cluster_id = dst_cache_index.cluster_id;
  transfer_cache_config.dst_cache_id = dst_cache_index.cache_id;
  transfer_cache_config.model_id_or_cache_id = dst_cache_index.cache_id;
  transfer_cache_config.type = kCacheKeyByIdType;
  transfer_cache_config.tensor_num_per_layer = ext_param.tensor_num_per_layer;
  llm::TransferBlockConfig transfer_block_config{};
  transfer_block_config.src_blocks = src_blocks;
  transfer_block_config.dst_blocks = dst_blocks;

  return PushData(src_cache, ext_param, transfer_cache_config, transfer_block_config);
}

Status LlmDataDist::LlmDataDistImpl::RegisterKvCache(const CacheDesc &cache_desc,
                                                     const std::vector<uint64_t> &addrs,
                                                     const RegisterCfg &cfg,
                                                     int64_t &cache_id) {
  (void) cfg;
  LLM_CHK_BOOL_RET_STATUS((llm_data_dist_.IsInitialized()),
                         ge::FAILED, "LlmDataDist is not initialized");
  LLM_CHK_BOOL_RET_STATUS(!addrs.empty(), LLM_PARAM_INVALID,  "addrs cann not be empty");
  LLM_CHK_BOOL_RET_STATUS(addrs.size() == static_cast<size_t>(cache_desc.num_tensors), LLM_PARAM_INVALID,
                         "addrs length:%zu should be equal to num_tensors:%u",
                         addrs.size(), cache_desc.num_tensors);
  for (auto addr : addrs) {
    LLM_CHK_BOOL_RET_STATUS(addr != 0UL, LLM_PARAM_INVALID,  "addr cann not be nullptr");
  }

  llm::CacheDesc internal_cache_desc{};
  internal_cache_desc.num_tensors = cache_desc.num_tensors;
  internal_cache_desc.data_type = static_cast<ge::DataType>(cache_desc.data_type);
  internal_cache_desc.shape = cache_desc.shape;
  internal_cache_desc.placement = static_cast<uint32_t>(cache_desc.placement);
  internal_cache_desc.seq_len_dim_index = 0;
  internal_cache_desc.remote_accessible = true;
  internal_cache_desc.cache_mem_type = llm::CacheMemType::MIX;

  llm::Cache internal_cache{};
  std::vector<uintptr_t> per_device_tensor_addrs;
  for (auto addr : addrs) {
    per_device_tensor_addrs.emplace_back(static_cast<uintptr_t>(addr));
  }
  internal_cache.per_device_tensor_addrs = {per_device_tensor_addrs};
  LLM_CHK_STATUS_RET(llm_data_dist_.RegisterCache(internal_cache_desc, internal_cache), "Failed to register cache");
  cache_id = internal_cache.cache_id;
  return LLM_SUCCESS;
}

Status LlmDataDist::LlmDataDistImpl::UnregisterKvCache(int64_t cache_id) {
  LLM_CHK_BOOL_RET_STATUS((llm_data_dist_.IsInitialized()),
                         ge::FAILED, "LlmDataDist is not initialized");
  LLM_CHK_STATUS_RET(llm_data_dist_.UnregisterCache(cache_id),
                    "Failed to unregister cache, cache_id = %ld", cache_id);
  return LLM_SUCCESS;
}

LlmDataDist::LlmDataDist(uint64_t cluster_id, LlmRole role) {
  LLMLOGD("Enter LlmDataDist construction.");
  impl_ = llm::MakeUnique<LlmDataDistImpl>(cluster_id, role);
  LLM_LOGE_IF(impl_ == nullptr, "Create LlmDataDist failed");
}

LlmDataDist::~LlmDataDist() = default;

Status LlmDataDist::Initialize(const std::map<AscendString, AscendString> &options) {
  LLMLOGI("LlmDataDist initialize start");
  LLM_CHK_BOOL_RET_STATUS(impl_ != nullptr, LLM_FAILED, "impl is nullptr, check LlmDataDist construct");
  const auto ret = llm::TransRetToLlmCodes(impl_->Initialize(options));
  LLM_CHK_BOOL_RET_STATUS(ret == LLM_SUCCESS, ret, "Failed to initialize LlmDataDist");
  LLMLOGI("LlmDataDist initialized successfully");
  return LLM_SUCCESS;
}

void LlmDataDist::Finalize() {
  LLMLOGI("LlmDataDist finalize start");
  if (impl_ != nullptr) {
    impl_->Finalize();
  }
  LLMLOGI("LlmDataDist finalized successfully");
}

Status LlmDataDist::SetRole(LlmRole role, const std::map<AscendString, AscendString> &options) {
  LLMLOGI("[SetRole] start");
  LLM_CHK_BOOL_RET_STATUS(impl_ != nullptr, LLM_FAILED, "impl is nullptr, check LlmDataDist construct");
  return impl_->SetRole(role, options);
}

Status LlmDataDist::LinkLlmClusters(const std::vector<ClusterInfo> &clusters,
                                    std::vector<Status> &rets,
                                    int32_t timeout_in_millis) {
  LLMLOGI("[LinkLlmClusters] start");
  LLM_CHK_BOOL_RET_STATUS(impl_ != nullptr, LLM_FAILED, "impl is nullptr, check LlmDataDist construct");
  const auto ret = impl_->LinkLlmClusters(clusters, rets, timeout_in_millis);
  LLM_CHK_BOOL_RET_STATUS(ret == LLM_SUCCESS, ret,
                         "[LinkLlmClusters] failed, number of clusters = %zu.",
                         clusters.size());
  LLMLOGI("[LinkLlmClusters] success, number of clusters = %zu", clusters.size());
  return ret;
}

Status LlmDataDist::UnlinkLlmClusters(const std::vector<ClusterInfo> &clusters,
                                      std::vector<Status> &rets,
                                      int32_t timeout_in_millis,
                                      bool force_flag) {
  LLMLOGI("[UnlinkLlmClusters] start");
  LLM_CHK_BOOL_RET_STATUS(impl_ != nullptr, LLM_FAILED, "impl is nullptr, check LlmDataDist construct");
  const auto ret = impl_->UnlinkLlmClusters(clusters, rets, timeout_in_millis, force_flag);
  LLM_CHK_BOOL_RET_STATUS(ret == LLM_SUCCESS, ret,
                         "[UnlinkLlmClusters] failed, number of clusters = %zu, force_flag = %d",
                         clusters.size(), static_cast<int32_t>(force_flag));
  LLMLOGI("[UnlinkLlmClusters] success, number of clusters = %zu", clusters.size());
  return ret;
}

Status LlmDataDist::AllocateCache(const CacheDesc &cache_desc, Cache &cache) {
  (void) cache_desc;
  (void) cache;
  LLMLOGE(LLM_FEATURE_NOT_ENABLED, "The feature is not supported.");
  return LLM_FEATURE_NOT_ENABLED;
}

Status LlmDataDist::DeallocateCache(int64_t cache_id) {
  (void) cache_id;
  LLMLOGE(LLM_FEATURE_NOT_ENABLED, "The feature is not supported.");
  return LLM_FEATURE_NOT_ENABLED;
}

Status LlmDataDist::PullKvCache(const CacheIndex &src_cache_index,
                                const Cache &dst_cache,
                                uint32_t batch_index,
                                int64_t size,
                                const KvCacheExtParam &ext_param) {
  LLMLOGI("[PullKvCache] start");
  LLM_CHK_BOOL_RET_STATUS(impl_ != nullptr, LLM_FAILED, "impl is nullptr, check LlmDataDist construct");
  const auto ret = impl_->PullKvCache(src_cache_index, dst_cache, batch_index, size, ext_param);
  LLM_CHK_BOOL_RET_STATUS(ret == LLM_SUCCESS, ret,
                         "[PullKvCache] failed, src_cluster_id = %lu, src_cache_id = %ld, src_batch_index = %u, "
                         "dst_cache_id = %ld",
                         src_cache_index.cluster_id,
                         src_cache_index.cache_id,
                         src_cache_index.batch_index,
                         dst_cache.cache_id);
  LLMLOGI("[PullKvCache] success, src_cluster_id = %lu, src_cache_id = %ld, src_batch_index = %u, dst_cache_id = %ld",
         src_cache_index.cluster_id,
         src_cache_index.cache_id,
         src_cache_index.batch_index,
         dst_cache.cache_id);
  return LLM_SUCCESS;
}

Status LlmDataDist::PullKvBlocks(const CacheIndex &src_cache_index,
                                 const Cache &dst_cache,
                                 const std::vector<uint64_t> &src_blocks,
                                 const std::vector<uint64_t> &dst_blocks,
                                 const KvCacheExtParam &ext_param) {
  LLMLOGI("[PullKvBlocks] start");
  LLM_CHK_BOOL_RET_STATUS(impl_ != nullptr, LLM_FAILED, "impl is nullptr, check LlmDataDist construct");
  const auto ret = impl_->PullKvBlocks(src_cache_index, dst_cache, src_blocks, dst_blocks, ext_param);
  LLM_CHK_BOOL_RET_STATUS(ret == LLM_SUCCESS, ret,
                         "[PullKvBlocks] failed, src_cluster_id = %lu, src_cache_id = %ld, dst_cache_id = %ld, "
                         "src_blocks = %s, dst_blocks = %s",
                         src_cache_index.cluster_id, src_cache_index.cache_id, dst_cache.cache_id,
                         llm::ToString(src_blocks).c_str(), llm::ToString(dst_blocks).c_str());
  LLMLOGI("[PullKvBlocks] success, src_cluster_id = %lu, src_cache_id = %ld, dst_cache_id = %ld, "
         "src_blocks = %s, dst_blocks = %s",
         src_cache_index.cluster_id, src_cache_index.cache_id, dst_cache.cache_id,
         llm::ToString(src_blocks).c_str(), llm::ToString(dst_blocks).c_str());
  return LLM_SUCCESS;
}

Status LlmDataDist::CopyKvCache(const Cache &src_cache,
                                const Cache &dst_cache,
                                uint32_t src_batch_index,
                                uint32_t dst_batch_index,
                                uint64_t offset,
                                int64_t size) {
  (void) src_cache;
  (void) dst_cache;
  (void) src_batch_index;
  (void) dst_batch_index;
  (void) offset;
  (void) size;
  LLMLOGE(LLM_FEATURE_NOT_ENABLED, "The feature is not supported.");
  return LLM_FEATURE_NOT_ENABLED;
}

Status LlmDataDist::CopyKvBlocks(const Cache &src_cache,
                                 const Cache &dst_cache,
                                 const std::vector<uint64_t> &src_blocks,
                                 const std::vector<std::vector<uint64_t>> &dst_blocks_list) {
  (void) src_cache;
  (void) dst_cache;
  (void) src_blocks;
  (void) dst_blocks_list;
  LLMLOGE(LLM_FEATURE_NOT_ENABLED, "The feature is not supported.");
  return LLM_FEATURE_NOT_ENABLED;
}

Status LlmDataDist::PushKvCache(const Cache &src_cache,
                                const CacheIndex &dst_cache_index,
                                uint32_t src_batch_index,
                                int64_t size,
                                const KvCacheExtParam &ext_param) {
  LLMLOGI("[PushKvCache] start");
  LLM_CHK_BOOL_RET_STATUS(impl_ != nullptr, LLM_FAILED, "impl is nullptr, check LlmDataDist construct");
  const auto ret = impl_->PushKvCache(src_cache, dst_cache_index, src_batch_index, size, ext_param);
  LLM_CHK_BOOL_RET_STATUS(ret == LLM_SUCCESS, ret,
                         "[PushKvCache] failed, dst_cluster_id = %lu, dst_cache_id = %ld, dst_batch_index = %u, "
                         "src_cache_id = %ld",
                         dst_cache_index.cluster_id,
                         dst_cache_index.cache_id,
                         dst_cache_index.batch_index,
                         src_cache.cache_id);
  LLMLOGI("[PushKvCache] success, dst_cluster_id = %lu, dst_cache_id = %ld, dst_batch_index = %u, src_cache_id = %ld",
         dst_cache_index.cluster_id,
         dst_cache_index.cache_id,
         dst_cache_index.batch_index,
         src_cache.cache_id);
  return LLM_SUCCESS;
}

Status LlmDataDist::PushKvBlocks(const Cache &src_cache,
                                 const CacheIndex &dst_cache_index,
                                 const std::vector<uint64_t> &src_blocks,
                                 const std::vector<uint64_t> &dst_blocks,
                                 const KvCacheExtParam &ext_param) {
  LLMLOGI("[PushKvBlocks] start");
  LLM_CHK_BOOL_RET_STATUS(impl_ != nullptr, LLM_FAILED, "impl is nullptr, check LlmDataDist construct");
  const auto ret = impl_->PushKvBlocks(src_cache, dst_cache_index, src_blocks, dst_blocks, ext_param);
  LLM_CHK_BOOL_RET_STATUS(ret == LLM_SUCCESS, ret,
                         "[PushKvBlocks] failed, dst_cluster_id = %lu, dst_cache_id = %ld, src_cache_id = %ld, "
                         "src_blocks = %s, dst_blocks = %s",
                         dst_cache_index.cluster_id, dst_cache_index.cache_id, src_cache.cache_id,
                         llm::ToString(src_blocks).c_str(), llm::ToString(dst_blocks).c_str());
  LLMLOGI("[PushKvBlocks] success, dst_cluster_id = %lu, dst_cache_id = %ld, src_cache_id = %ld, "
      "src_blocks = %s, dst_blocks = %s",
      dst_cache_index.cluster_id, dst_cache_index.cache_id, src_cache.cache_id,
      llm::ToString(src_blocks).c_str(), llm::ToString(dst_blocks).c_str());
  return LLM_SUCCESS;
}

Status LlmDataDist::RegisterKvCache(const CacheDesc &cache_desc,
                                    const std::vector<uint64_t> &addrs,
                                    const RegisterCfg &cfg,
                                    int64_t &cache_id) {
  LLMLOGI("[RegisterKvCache] start");
  LLM_CHK_BOOL_RET_STATUS(impl_ != nullptr, LLM_FAILED, "impl is nullptr, check LlmDataDist construct");
  const auto ret = impl_->RegisterKvCache(cache_desc, addrs, cfg, cache_id);
  LLM_CHK_BOOL_RET_STATUS(ret == LLM_SUCCESS, ret,  "[RegisterKvCache] failed, addr size = %zu", addrs.size());
  LLMLOGI("[RegisterKvCache] success, cache_id = %ld", cache_id);
  return LLM_SUCCESS;
}

Status LlmDataDist::UnregisterKvCache(int64_t cache_id) {
  LLMLOGI("[UnregisterKvCache] start");
  LLM_CHK_BOOL_RET_STATUS(impl_ != nullptr, LLM_FAILED, "impl is nullptr, check LlmDataDist construct");
  const auto ret = impl_->UnregisterKvCache(cache_id);
  LLM_CHK_BOOL_RET_STATUS(ret == LLM_SUCCESS, ret,  "[UnregisterKvCache] failed");
  LLMLOGI("[UnregisterKvCache] success, cache_id = %ld", cache_id);
  return LLM_SUCCESS;
}
}  // namespace llm_datadist