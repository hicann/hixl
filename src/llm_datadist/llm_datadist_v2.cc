/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "llm_datadist_v2.h"
#include "common/llm_utils.h"
#include "llm_datadist_timer.h"
#include "statistic_manager.h"
#include "common/llm_checker.h"
#include "common/llm_scope_guard.h"
#include "llm_datadist/llm_engine_types.h"

namespace llm {
ge::Status LLMDataDistV2::DoInitialize(const std::map<ge::AscendString, ge::AscendString> &options) {
  auto iter = options.find(LLM_OPTION_ROLE);
  LLM_ASSERT_TRUE(iter != options.end(), "[LLMDataDist] option:%s not found.", LLM_OPTION_ROLE);
  LLMLOGI("LLMDataDist role:%s.", iter->second.GetString());
  LLM_CHK_STATUS_RET(HcclAdapter::GetInstance().Initialize(), "HcclSoManager initialize failed.");
  bool remote_cache_accessible = false;
  LLM_CHK_STATUS_RET(LLMUtils::ParseFlag(kLlmOptionEnableRemoteCacheAccessible,
                                        options,
                                        remote_cache_accessible),
                    "Failed to parse option %s", kLlmOptionEnableRemoteCacheAccessible);
  int32_t device_id = 0;
  LLM_CHK_STATUS_RET(LLMUtils::ParseDeviceId(options, device_id), "Failed to get device id");
  cache_manager_ = MakeUnique<CacheManager>();
  LLM_CHECK_NOTNULL(cache_manager_);
  comm_mem_manager_ = MakeUnique<CommMemManager>();
  LLM_CHECK_NOTNULL(comm_mem_manager_);
  comm_entity_manager_ = MakeUnique<CommEntityManager>();
  LLM_CHECK_NOTNULL(comm_entity_manager_);
  comm_entity_manager_->SetCommMemManager(comm_mem_manager_.get());
  llm_link_mgr_ = MakeUnique<LLMLinkManager>(cluster_id_, device_id, comm_entity_manager_.get(),
                                             comm_mem_manager_.get(), cache_manager_.get(), remote_cache_accessible);
  LLM_CHECK_NOTNULL(llm_link_mgr_);
  llm_link_mgr_->SetCommEntityManager(comm_entity_manager_.get());
  llm_link_mgr_->SetCommMemManager(comm_mem_manager_.get());
  llm_link_mgr_->SetCacheManager(cache_manager_.get());
  data_cache_engine_ = MakeUnique<DataCacheEngine>();
  LLM_CHECK_NOTNULL(data_cache_engine_);
  data_cache_engine_->SetCommEntityManager(comm_entity_manager_.get());
  data_cache_engine_->SetCommMemManager(comm_mem_manager_.get());
  data_cache_engine_->SetCacheManager(cache_manager_.get());

  LLM_DISMISSABLE_GUARD(fail_guard, ([this]() {
    llm_link_mgr_->Finalize();
    comm_entity_manager_->Finalize();
    comm_mem_manager_->Finalize();
    data_cache_engine_->Finalize();
  }));
  LLM_CHK_STATUS_RET(data_cache_engine_->Initialize(options), "DataCacheEngine initialize failed.");
  LLM_CHK_STATUS_RET(llm_link_mgr_->Initialize(options), "CommLinkManager initialize failed.");
  LLM_CHK_STATUS_RET(comm_entity_manager_->Initialize(!remote_cache_accessible),
                    "CommEntityManager initialize failed.");

  LlmDatadistTimer::Instance().Init();
  statistic_timer_handle_ = LlmDatadistTimer::Instance().CreateTimer([this]() {
    StatisticManager::GetInstance().Dump();
    comm_entity_manager_->Dump();
  });
  constexpr uint32_t kStatisticTimerPeriod = 80U * 1000U;
  (void)LlmDatadistTimer::Instance().StartTimer(statistic_timer_handle_, kStatisticTimerPeriod, false);

  LLMLOGI("[LLMDataDist] cluster:[%lu] init success", cluster_id_);
  inner_initialized_ = true;
  LLM_DISMISS_GUARD(fail_guard);
  return ge::SUCCESS;
}

ge::Status LLMDataDistV2::LLMDataDistInitialize(const std::map<ge::AscendString, ge::AscendString> &options) {
  LLMLOGI("LLMDataDist initilize cluster id:%lu", cluster_id_);
  std::lock_guard<std::mutex> lk(mutex_);
  if (!is_initialized_.load()) {
    LLM_CHK_STATUS_RET(DoInitialize(options), "Failed to initialize llm datadist, cluster id:%lu", cluster_id_);
    is_initialized_ = true;
  }
  return ge::SUCCESS;
}

void LLMDataDistV2::DoInnerFinalize() {
  llm_link_mgr_->Finalize();
  comm_entity_manager_->Dump();
  comm_entity_manager_->Finalize();
  comm_mem_manager_->Finalize();
  data_cache_engine_->Finalize();

  StatisticManager::GetInstance().Dump();
  StatisticManager::GetInstance().Reset();
  if (statistic_timer_handle_ != nullptr) {
    (void)LlmDatadistTimer::Instance().StopTimer(statistic_timer_handle_);
    (void)LlmDatadistTimer::Instance().DeleteTimer(statistic_timer_handle_);
    statistic_timer_handle_ = nullptr;
  }
  LlmDatadistTimer::Instance().Finalize();
  inner_initialized_.store(false);
}

void LLMDataDistV2::DoFinalize() {
  DoInnerFinalize();
}

void LLMDataDistV2::LLMDataDistFinalize() {
  std::lock_guard<std::mutex> lk(mutex_);
  if (!is_initialized_.load()) {
    LLMLOGW("[LLMDataDist] cluster:%lu is not initialized", cluster_id_);
    return;
  }
  DoFinalize();
  is_initialized_.store(false);
  LLMLOGI("LLMDataDist Finalize end.");
}

LLMDataDistV2::~LLMDataDistV2() {
  if (inner_initialized_.load()) {
    DoInnerFinalize();
  }
}

ge::Status LLMDataDistV2::Link(std::string &cluster_name,
                               const std::map<uint64_t, uint32_t> &cluster2rank, std::string &rank_table,
                               uint64_t &comm_id) {
  const auto start = std::chrono::steady_clock::now();
  LLM_CHK_BOOL_RET_STATUS(is_initialized_.load(), ge::FAILED, "Llm datadist of cluster:%lu is not initialized.",
                         cluster_id_);
  LLM_CHK_STATUS_RET(llm_link_mgr_->Link(cluster_name, cluster2rank, rank_table, comm_id), "Link failed.");
  const auto end = std::chrono::steady_clock::now();
  auto &func_statistic_info = StatisticManager::GetInstance().GetFuncStatisticInfo();
  const uint64_t cost = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
  StatisticManager::UpdateCost(cost, func_statistic_info.link_func_times, func_statistic_info.link_func_min_cost,
                               func_statistic_info.link_func_max_cost, func_statistic_info.link_func_total_cost);
  return ge::SUCCESS;
}

ge::Status LLMDataDistV2::Unlink(uint64_t comm_id) {
  const auto start = std::chrono::steady_clock::now();
  LLM_CHK_BOOL_RET_STATUS(is_initialized_.load(std::memory_order::memory_order_relaxed), ge::FAILED,
                         "Llm datadist of cluster:%lu is not initialized.", cluster_id_);
  LLM_CHK_STATUS_RET(llm_link_mgr_->Unlink(comm_id), "Unlink failed.");
  const auto end = std::chrono::steady_clock::now();
  auto &func_statistic_info = StatisticManager::GetInstance().GetFuncStatisticInfo();
  const uint64_t cost = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
  StatisticManager::UpdateCost(cost, func_statistic_info.unlink_func_times, func_statistic_info.unlink_func_min_cost,
                               func_statistic_info.unlink_func_max_cost, func_statistic_info.unlink_func_total_cost);
  return ge::SUCCESS;
}

ge::Status LLMDataDistV2::QueryRegisterMemStatus(uint64_t comm_id, RegisterMemoryStatus &status) {
  LLM_CHK_BOOL_RET_STATUS(is_initialized_.load(std::memory_order::memory_order_relaxed), ge::FAILED,
                         "Llm datadist of cluster:%lu is not initialized.", cluster_id_);
  LLM_CHK_STATUS_RET(llm_link_mgr_->QueryRegisterMemStatus(comm_id, status), "QueryRegisterMemStatus failed.");
  return ge::SUCCESS;
}

ge::Status LLMDataDistV2::RegisterCache(const CacheDesc &cache_desc, Cache &cache,
                                        const std::vector<CacheKey> &cache_keys) {
  LLM_CHK_BOOL_RET_STATUS(is_initialized_.load(std::memory_order::memory_order_relaxed), ge::FAILED,
                         "Llm datadist of cluster:%lu is not initialized.", cluster_id_);
  auto &func_statistic_info = StatisticManager::GetInstance().GetFuncStatisticInfo();
  func_statistic_info.register_func_times++;
  return data_cache_engine_->Register(cache_desc, cache_keys, cache);
}

ge::Status LLMDataDistV2::AllocateCache(const CacheDesc &cache_desc, Cache &cache,
                                        const std::vector<CacheKey> &cache_keys) {
  LLM_CHK_BOOL_RET_STATUS(is_initialized_.load(std::memory_order::memory_order_relaxed), ge::FAILED,
                         "Llm datadist of cluster:%lu is not initialized.", cluster_id_);
  auto &mem_statistic_info = StatisticManager::GetInstance().GetMemoryStatisticInfo();
  mem_statistic_info.alloc_times++;
  return data_cache_engine_->Allocate(cache_desc, cache_keys, cache);
}

ge::Status LLMDataDistV2::DeallocateCache(int64_t cache_id) {
  LLM_CHK_BOOL_RET_STATUS(is_initialized_.load(std::memory_order::memory_order_relaxed), ge::FAILED,
                         "Llm datadist of cluster:%lu is not initialized.", cluster_id_);
  auto &mem_statistic_info = StatisticManager::GetInstance().GetMemoryStatisticInfo();
  mem_statistic_info.free_times++;
  return data_cache_engine_->Deallocate(cache_id);
}

ge::Status LLMDataDistV2::PullCache(int64_t cache_id, const CacheKey &cache_key,
                                    const PullCacheParam &pull_cache_param) {
  const auto start = std::chrono::steady_clock::now();
  LLM_CHK_BOOL_RET_STATUS(is_initialized_.load(std::memory_order::memory_order_relaxed), ge::FAILED,
                         "Llm datadist of cluster:%lu is not initialized.", cluster_id_);
  LLM_CHK_BOOL_RET_STATUS(pull_cache_param.tensor_num_per_layer > 0UL,
                         ge::LLM_PARAM_INVALID, "tensor_num_per_layer is invalid, must > 0");
  LLM_CHK_BOOL_RET_STATUS(cluster_id_ != cache_key.prompt_cluster_id, ge::LLM_PARAM_INVALID,
                         "data can not be pulled from own cluster:%lu", cluster_id_);
  LLM_CHK_STATUS_RET(data_cache_engine_->PullCache(cache_id, cache_key, pull_cache_param), "pull cache failed");
  const auto end = std::chrono::steady_clock::now();
  auto &func_statistic_info = StatisticManager::GetInstance().GetFuncStatisticInfo();
  const uint64_t cost = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
  StatisticManager::UpdateCost(cost, func_statistic_info.pull_func_times, func_statistic_info.pull_func_min_cost,
                               func_statistic_info.pull_func_max_cost, func_statistic_info.pull_func_total_cost);
  return ge::SUCCESS;
}

ge::Status LLMDataDistV2::PullBlocks(int64_t cache_id, const CacheKey &cache_key,
                                     const PullCacheParam &pull_cache_param) {
  LLM_CHK_BOOL_RET_STATUS((!pull_cache_param.prompt_blocks.empty()),
                         ge::LLM_PARAM_INVALID,
                         "src_blocks is empty, pull from non-block cache is not supported yet");
  LLM_CHK_BOOL_RET_STATUS(pull_cache_param.prompt_blocks.size() == pull_cache_param.decoder_blocks.size(),
                         ge::LLM_PARAM_INVALID,
                         "number of src_blocks (%zu) mismatches that of dst_blocks (%zu)",
                         pull_cache_param.prompt_blocks.size(), pull_cache_param.decoder_blocks.size());
  LLM_CHK_BOOL_RET_STATUS(cache_key.prompt_batch_index == 0U,
                         ge::LLM_PARAM_INVALID,
                         "invalid cache_key.prompt_batch_index (%lu), only 0 is supported in pull block",
                         cache_key.prompt_batch_index);
  return PullCache(cache_id, cache_key, pull_cache_param);
}

ge::Status LLMDataDistV2::CopyCache(const CopyCacheParam &copy_cache_param) {
  const auto start = std::chrono::steady_clock::now();
  LLM_CHK_BOOL_RET_STATUS(is_initialized_.load(std::memory_order::memory_order_relaxed), ge::FAILED,
                         "Llm datadist of cluster:%lu is not initialized.", cluster_id_);
  LLM_CHK_STATUS_RET(data_cache_engine_->CopyCache(copy_cache_param), "copy cache failed");
  const auto end = std::chrono::steady_clock::now();
  auto &func_statistic_info = StatisticManager::GetInstance().GetFuncStatisticInfo();
  const uint64_t cost = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
  StatisticManager::UpdateCost(cost, func_statistic_info.copy_func_times, func_statistic_info.copy_func_min_cost,
                               func_statistic_info.copy_func_max_cost, func_statistic_info.copy_func_total_cost);
  return ge::SUCCESS;
}

ge::Status LLMDataDistV2::RemoveCacheKey(const CacheKey &cache_key) {
  LLM_CHK_BOOL_RET_STATUS(is_initialized_.load(std::memory_order::memory_order_relaxed), ge::FAILED,
                         "Llm datadist of cluster:%lu is not initialized.", cluster_id_);
  return data_cache_engine_->RemoveCacheKey(cache_key);
}

ge::Status LLMDataDistV2::RemapRegisteredMemory(const std::vector<LLMMemInfo> &mem_infos) {
  LLM_CHK_BOOL_RET_STATUS(is_initialized_.load(std::memory_order::memory_order_relaxed), ge::FAILED,
                         "Llm datadist of cluster:%lu is not initialized.", cluster_id_);
  LLM_CHK_STATUS_RET(comm_entity_manager_->RemapRegisteredMemory(mem_infos), "RemapRegisteredMemory failed.");
  return ge::SUCCESS;
}

ge::Status LLMDataDistV2::SwapBlocks(const Cache &src, const Cache &dst, const uint64_t block_size, const uint32_t type,
                                     const std::vector<std::pair<int64_t, int64_t>> &block_mapping) {
  const auto start = std::chrono::steady_clock::now();
  LLM_CHK_BOOL_RET_STATUS(is_initialized_.load(std::memory_order::memory_order_relaxed), ge::FAILED,
                         "Llm datadist of cluster:%lu is not initialized.", cluster_id_);
  LLM_CHK_STATUS_RET(data_cache_engine_->SwapBlocks(src, dst, block_size, type, block_mapping), "swap blocks failed");
  const auto end = std::chrono::steady_clock::now();
  auto &func_statistic_info = StatisticManager::GetInstance().GetFuncStatisticInfo();
  const uint64_t cost = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
  StatisticManager::UpdateCost(cost, func_statistic_info.swap_func_times, func_statistic_info.swap_func_min_cost,
                               func_statistic_info.swap_func_max_cost, func_statistic_info.swap_func_total_cost);
  return ge::SUCCESS;
}

ge::Status LLMDataDistV2::CheckCapacity(const size_t seq_len) {
  LLM_CHK_BOOL_RET_STATUS(is_initialized_.load(std::memory_order::memory_order_relaxed), ge::FAILED,
                         "Llm datadist of cluster:%lu is not initialized.", cluster_id_);
  LLM_CHK_STATUS_RET(data_cache_engine_->CheckCapacity(seq_len), "check capacity failed, seq_len:%zu", seq_len);
  return ge::SUCCESS;
}

ge::Status LLMDataDistV2::TransferCache(const uint64_t task_id, const TransferCacheConfig &transfer_cache_config,
                                        const TransferBlockConfig &transfer_block_config) {
  const auto start = std::chrono::steady_clock::now();
  LLM_CHK_BOOL_RET_STATUS(is_initialized_.load(std::memory_order::memory_order_relaxed), ge::FAILED,
                         "Llm datadist of cluster:%lu is not initialized.", cluster_id_);
  LLM_CHK_BOOL_RET_STATUS(transfer_cache_config.tensor_num_per_layer > 0UL,
                         ge::LLM_PARAM_INVALID, "tensor_num_per_layer is invalid, must > 0");
  std::lock_guard<std::mutex> lk(transfer_mutex_);
  LLM_CHK_STATUS_RET(data_cache_engine_->TransferCache(task_id, transfer_cache_config, transfer_block_config),
                    "task:%lu of cluster:%lu transfer cache of layer[%lu] failed", task_id,
                    transfer_cache_config.cluster_id, transfer_cache_config.layer_index);
  const auto end = std::chrono::steady_clock::now();
  auto &func_statistic_info = StatisticManager::GetInstance().GetFuncStatisticInfo();
  const uint64_t cost = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
  StatisticManager::UpdateCost(cost, func_statistic_info.transfer_func_times,
                               func_statistic_info.transfer_func_min_cost, func_statistic_info.transfer_func_max_cost,
                               func_statistic_info.transfer_func_total_cost);
  LLMLOGI("task:%lu of cluster:%lu transfer cache of layer[%lu] success", task_id, transfer_cache_config.cluster_id,
         transfer_cache_config.layer_index);
  return ge::SUCCESS;
}

ge::Status LLMDataDistV2::UnregisterCache(int64_t cache_id) {
  LLM_CHK_BOOL_RET_STATUS(is_initialized_.load(std::memory_order::memory_order_relaxed), ge::FAILED,
                         "Llm datadist of cluster:%lu is not initialized.", cluster_id_);
  auto &func_statistic_info = StatisticManager::GetInstance().GetFuncStatisticInfo();
  func_statistic_info.deregister_func_times++;
  return data_cache_engine_->Unregister(cache_id);
}

ge::Status LLMDataDistV2::LinkClusters(const std::vector<ClusterInfo> &clusters,
                                       std::vector<ge::Status> &rets,
                                       const int32_t timeout) {
  const auto start = std::chrono::steady_clock::now();
  LLM_CHK_BOOL_RET_STATUS(is_initialized_.load(), ge::FAILED, "Llm datadist of cluster:%lu is not initialized.",
                         cluster_id_);
  LLM_CHK_STATUS_RET(llm_link_mgr_->LinkClusters(clusters, rets, timeout), "Failed to link clusters.");
  const auto end = std::chrono::steady_clock::now();
  auto &func_statistic_info = StatisticManager::GetInstance().GetFuncStatisticInfo();
  const uint64_t cost = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
  StatisticManager::UpdateCost(cost, func_statistic_info.link_func_times, func_statistic_info.link_func_min_cost,
                               func_statistic_info.link_func_max_cost, func_statistic_info.link_func_total_cost);
  return ge::SUCCESS;
}

ge::Status LLMDataDistV2::UnlinkClusters(const std::vector<ClusterInfo> &clusters,
                                         std::vector<ge::Status> &rets,
                                         const int32_t timeout,
                                         bool force_flag) {
  const auto start = std::chrono::steady_clock::now();
  LLM_CHK_BOOL_RET_STATUS(is_initialized_.load(std::memory_order::memory_order_relaxed), ge::FAILED,
                         "Llm datadist of cluster:%lu is not initialized.", cluster_id_);
  LLM_CHK_STATUS_RET(llm_link_mgr_->UnlinkClusters(clusters, rets, timeout, force_flag), "Failed to unlink clusters.");
  const auto end = std::chrono::steady_clock::now();
  auto &func_statistic_info = StatisticManager::GetInstance().GetFuncStatisticInfo();
  const uint64_t cost = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
  StatisticManager::UpdateCost(cost, func_statistic_info.unlink_func_times, func_statistic_info.unlink_func_min_cost,
                               func_statistic_info.unlink_func_max_cost, func_statistic_info.unlink_func_total_cost);
  return ge::SUCCESS;
}

ge::Status LLMDataDistV2::SwitchRole(const std::string &role, const std::map<std::string, std::string> &options) {
  LLM_CHK_BOOL_RET_STATUS(is_initialized_.load(std::memory_order::memory_order_relaxed), ge::FAILED,
                         "Llm datadist of cluster:%lu is not initialized.", cluster_id_);
  LLM_CHK_STATUS_RET(llm_link_mgr_->SwitchRole(role, options), "Failed to switch role.");
  return ge::SUCCESS;
}

bool LLMDataDistV2::IsInitialized() const {
  return is_initialized_.load(std::memory_order::memory_order_relaxed);
}
}  // namespace llm
