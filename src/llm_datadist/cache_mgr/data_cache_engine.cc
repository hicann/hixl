/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "data_cache_engine.h"
#include <set>
#include "llm_datadist/llm_error_codes.h"
#include "statistic_manager.h"
#include "common/common.h"
#include "common/llm_utils.h"
#include "common/mem_utils.h"
#include "swap_impl.h"
#include "cache_manager.h"
#include "data_transfer/d2h_data_transfer_job.h"
#include "data_transfer/layer_wise_transfer_job.h"
#include "data_transfer/data_transfer_client.h"
#include "base/err_msg.h"
#include "common/llm_checker.h"
#include "common/llm_scope_guard.h"
#include "llm_datadist/llm_engine_types.h"

namespace llm {
namespace {
constexpr size_t kMaxDimNum = 32U;
constexpr size_t kAlignment = 4096U;

ge::Status ParseMemoryPoolConfig(const std::string &mem_pool_config, size_t &pool_size, size_t &page_shift) {
  const std::string &json_str = mem_pool_config;
  nlohmann::json json_obj;
  try {
    json_obj = nlohmann::json::parse(json_str);
    LLM_CHK_BOOL_RET_STATUS(json_obj.at("memory_size").is_number_unsigned(), ge::LLM_PARAM_INVALID,
                           "memory_size is not an unsigned integer: config = %s", json_str.c_str());
    pool_size = json_obj.at("memory_size").get<size_t>();
    if (json_obj.contains("page_shift")) {
      LLM_CHK_BOOL_RET_STATUS(json_obj.at("page_shift").is_number_unsigned(), ge::LLM_PARAM_INVALID,
                             "page_shift is not an unsigned integer: config = %s", json_str.c_str());
      page_shift = json_obj.at("page_shift").get<size_t>();
    }
  } catch (nlohmann::json::exception &e) {
    REPORT_INNER_ERR_MSG("E19999", "Failed to parse memory pool config: %s", json_str.c_str());
    LLMLOGE(ge::LLM_PARAM_INVALID, "Failed to parse memory pool config: \"%s\", exception = %s", json_str.c_str(),
           e.what());
    return ge::LLM_PARAM_INVALID;
  }
  return ge::SUCCESS;
}

ge::Status CheckTensorIndicesContinuous(const std::vector<uint64_t> &tensor_indices) {
  if (tensor_indices.empty()) {
    return ge::SUCCESS;
  }
  std::set<uint64_t> unique_elements(tensor_indices.begin(), tensor_indices.end());
  LLM_CHK_BOOL_RET_STATUS(unique_elements.size() == tensor_indices.size(), ge::LLM_PARAM_INVALID,
                         "tensor_indices is not continuous");
  const uint64_t min_element = *std::min_element(unique_elements.begin(), unique_elements.end());
  const uint64_t max_element = *std::max_element(unique_elements.begin(), unique_elements.end());
  LLM_CHK_BOOL_RET_STATUS((max_element - min_element + 1) == unique_elements.size(), ge::LLM_PARAM_INVALID,
                         "tensor_indices is not continuous");
  return ge::SUCCESS;
}
}  // namespace
ge::Status DataCacheEngine::Register(const llm::CacheDesc &cache_desc, const std::vector<CacheKey> &cache_keys,
                                     llm::Cache &cache) {
  LLM_CHK_BOOL_RET_STATUS(!cache_desc.shape.empty() && (cache_desc.shape.size() < kMaxDimNum), ge::LLM_PARAM_INVALID,
                         "Invalid shape: %s, dim_num (%zu) must be in range [1, 33)",
                         ToString(cache_desc.shape).c_str(), cache_desc.shape.size());
  LLM_CHECK_GE(cache.per_device_tensor_addrs.size(), 1);
  LLM_CHK_BOOL_RET_STATUS(cache_desc.num_tensors == static_cast<uint32_t>(cache.per_device_tensor_addrs[0].size()),
                         ge::LLM_PARAM_INVALID, "cache addrs size[%zu] is not equal to num_tensors[%u] of cache_desc",
                         cache.per_device_tensor_addrs[0].size(), cache_desc.num_tensors);
  std::lock_guard<std::mutex> lock(mu_);
  const auto cache_id = cache_id_gen_.fetch_add(1, std::memory_order::memory_order_relaxed);
  LLM_CHECK_GE(cache_id, 1);
  int64_t tensor_size;
  LLM_CHK_STATUS_RET(LLMUtils::CalcTensorMemSize(cache_desc.shape,
                                                cache_desc.data_type, tensor_size),
                    "Failed to calc tensor size, shape = %s, dtype = %d",
                    ToString(cache_desc.shape).c_str(),
                    static_cast<int32_t>(cache_desc.data_type));
  LLMLOGI("[Register] start, placement:%u", static_cast<uint32_t>(cache_desc.placement));
  LLM_CHK_STATUS_RET(comm_mem_manager_->RegisterCacheMem(cache_id, cache_desc,
                                                        cache.per_device_tensor_addrs[0U], tensor_size),
                    "Register cache addr failed, cache_id = %ld.", cache_id);
  LLM_CHK_STATUS_RET(cache_manager_->RegisterCacheEntry(cache_id, cache_keys, cache_desc,
                                                       cache.per_device_tensor_addrs[0U], tensor_size),
                    "Register cache entry failed.");
  cache.cache_id = cache_id;
  LLMLOGI("[cache_id:%ld][Register] success, num_tensors = %u, shape = %s", cache_id, cache_desc.num_tensors,
         ToString(cache_desc.shape).c_str());
  return ge::SUCCESS;
}

ge::Status DataCacheEngine::Unregister(int64_t cache_id) {
  const auto start = std::chrono::steady_clock::now();
  LLM_CHK_STATUS_RET(comm_mem_manager_->UnregisterCacheMem(cache_id),
                    "Unregister cache addr failed, cache_id = %ld.", cache_id);
  LLM_CHK_STATUS_RET(cache_manager_->UnregisterCacheEntry(cache_id),
                    "Unregister cache entry failed, cache_id = %ld.", cache_id);
  LLMLOGI("[cache_id:%ld][Unregister] success, cost = %ld ms",
         cache_id,
         std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
  return ge::SUCCESS;
}

ge::Status DataCacheEngine::PullCache(int64_t cache_id, const CacheKey &cache_key,
                                      const PullCacheParam &pull_cache_param) {
  const auto start = std::chrono::steady_clock::now();
  // cache_id is local, find local addr by cache_id
  CacheEntry cache_entry;
  LLM_CHK_BOOL_RET_STATUS(cache_manager_->GetCacheEntry(cache_id, cache_entry), ge::LLM_KV_CACHE_NOT_EXIST,
                         "cache id:%ld not found", cache_id);
  LLM_CHK_STATUS_RET(CheckParam(cache_entry, pull_cache_param), "[cache_id:%ld] check param failed", cache_id);
  LLMLOGI("pull cache with tensor num per layer:%lu.", pull_cache_param.tensor_num_per_layer);
  const auto entity = comm_entity_manager_->GetEntityByRemoteClusterId(cache_key.prompt_cluster_id);
  LLM_CHK_BOOL_RET_STATUS(entity != nullptr, ge::LLM_NOT_YET_LINK,
                         "current cluster is not linked with remote cluster:%lu", cache_key.prompt_cluster_id);
  std::lock_guard<std::mutex> pull_lock(entity->GetPullMutex());
  // in case of entity is erased here, can not delete.
  LLM_CHK_BOOL_RET_STATUS((entity->GetCurState() != FsmState::FSM_DESTROYED_STATE), ge::LLM_NOT_YET_LINK,
                         "current cluster is not linked with remote cluster:%lu", cache_key.prompt_cluster_id);
  LLMLOGI("Get lock cost:%ld us.",
         std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count());
  TemporaryRtContext with_context(rt_context_);
  LLM_CHK_BOOL_RET_STATUS(entity->CheckEntityInfo(), ge::LLM_NOT_YET_LINK,
                         "pull cache must wait until the query_register_mem_status return ok");
  LLM_DISMISSABLE_GUARD(abort_stream, [this]() -> void {
    LLM_CHK_ACL(rtStreamAbort(req_stream_));
  });
  if (access_remote_cache_) {
    DataTransferClient client(*entity, req_stream_);
    LLM_CHK_STATUS_RET(client.PullCacheByGet(cache_entry, cache_key, pull_cache_param, sync_cache_timeout_));
    LLMLOGI("[PullCache] success, cache_id = %ld, num_tensors = %zu, stride = %lu, "
           "pull_size = %ld, local_block_cnt = %zu, remote_block_cnt = %zu",
           cache_id, cache_entry.cache_addrs.size(), cache_entry.stride,
           pull_cache_param.size, pull_cache_param.decoder_blocks.size(), pull_cache_param.prompt_blocks.size());
    LLM_DISMISS_GUARD(abort_stream);
    return ge::SUCCESS;
  }
  entity->ClearResponseFlags();
  if (cache_entry.placement == CachePlacement::HOST) {
    LLM_CHK_BOOL_RET_STATUS(npu_pool_memory_ != nullptr, ge::LLM_PARAM_INVALID, "Device memory pool is not enabled.");
    D2HDataTransferClient client(*entity, req_stream_);
    LLM_CHK_STATUS_RET(client.PullCache(cache_entry, cache_key, pull_cache_param, sync_cache_timeout_),
                      "Failed to pull kv from remote cluster:%lu", cache_key.prompt_cluster_id);
    LLM_DISMISS_GUARD(abort_stream);
    return ge::SUCCESS;
  }

  DataTransferClient client(*entity, req_stream_);
  LLM_CHK_STATUS_RET(client.PullCache(cache_entry, cache_key, pull_cache_param, sync_cache_timeout_),
                    "Failed to pull kv from remote cluster:%lu", cache_key.prompt_cluster_id);
  LLM_DISMISS_GUARD(abort_stream);
  return ge::SUCCESS;
}

ge::Status DataCacheEngine::SwapBlocks(const Cache &src, const Cache &dst, const uint64_t block_size,
                                       const uint32_t type,
                                       const std::vector<std::pair<int64_t, int64_t>> &block_mapping) const {
  rtContext_t user_ctx = nullptr;
  (void)rtCtxGetCurrent(&user_ctx);
  LLM_MAKE_GUARD(set_user_ctx, [&user_ctx]() { (void)rtCtxSetCurrent(user_ctx); });
  SwapImpl swap_impl(device_id_);
  LLM_CHK_STATUS_RET(swap_impl.SwapBlocksV2(src, dst, block_size, type, block_mapping));
  return ge::SUCCESS;
}

ge::Status DataCacheEngine::Initialize(const std::map<ge::AscendString, ge::AscendString> &options) {
  int32_t device_id;
  LLM_CHK_STATUS_RET(LLMUtils::ParseDeviceId(options, device_id), "Failed to get device id");
  LLM_ASSERT_RT_OK(rtSetDevice(device_id));
  device_id_ = device_id;
  LLM_ASSERT_RT_OK(rtCtxGetCurrent(&rt_context_));
  LLM_CHK_STATUS_RET(LLMUtils::ParseFlag(kLlmOptionEnableRemoteCacheAccessible, options, access_remote_cache_),
                    "Failed to parse option %s", kLlmOptionEnableRemoteCacheAccessible);
  LLM_CHK_STATUS_RET(cache_manager_->Initialize(access_remote_cache_));
  DecoderWaitTimeInfo wait_time_info{};
  LLM_CHK_STATUS_RET(LLMUtils::ParserWaitTimeInfo(options, wait_time_info), "parser wait time info failed");
  sync_cache_timeout_ = wait_time_info.sync_kv_wait_time;
  LLM_CHK_STATUS_RET(InitializeMemoryPool(options), "Failed to initialize memory pool");
  // create stream
  LLM_ASSERT_RT_OK(
      rtStreamCreateWithFlags(&req_stream_, RT_STREAM_PRIORITY_DEFAULT, RT_STREAM_FAST_LAUNCH | RT_STREAM_FAST_SYNC));
  LLM_CHECK_NOTNULL(comm_entity_manager_);
  LLM_CHECK_NOTNULL(comm_mem_manager_);
  LLM_CHECK_NOTNULL(cache_manager_);
  return ge::SUCCESS;
}

void DataCacheEngine::Finalize() const{
  {
    TemporaryRtContext with_context(rt_context_);
    cache_manager_->Finalize();
    if (npu_pool_memory_ != nullptr) {
      LLM_CHK_ACL(rtFree(npu_pool_memory_));
    }
    if (host_pool_memory_ != nullptr) {
      LLM_CHK_ACL(rtFreeHost(host_pool_memory_));
    }
    if (req_stream_ != nullptr) {
      LLM_CHK_ACL(rtStreamDestroy(req_stream_));
    }
    if (transfer_stream_ != nullptr) {
      LLM_CHK_ACL(rtStreamDestroy(transfer_stream_));
    }
  }
  if (device_id_ != -1) {
    LLM_CHK_ACL(rtDeviceReset(device_id_));
  }
}

void DataCacheEngine::SetCommEntityManager(CommEntityManager *comm_entity_manager) {
  comm_entity_manager_ = comm_entity_manager;
}

void DataCacheEngine::SetCommMemManager(CommMemManager *comm_mem_manager) {
  comm_mem_manager_ = comm_mem_manager;
}

void DataCacheEngine::SetCacheManager(CacheManager *cache_manager) {
  cache_manager_ = cache_manager;
}

ge::Status DataCacheEngine::InitializeDeviceMemoryPool(const std::map<ge::AscendString, ge::AscendString> &options) {
  const auto it = options.find(LLM_OPTION_MEM_POOL_CONFIG);
  if (it == options.cend()) {
    LLMLOGI("memory pool is not enabled");
    return ge::SUCCESS;
  }
  const std::string &json_str = it->second.GetString();
  size_t page_shift = 16U;  // 64KB by default
  LLM_CHK_STATUS_RET(ParseMemoryPoolConfig(json_str, npu_pool_size_, page_shift), "parse %s failed",
                    LLM_OPTION_MEM_POOL_CONFIG);
  ScalableConfig config{};
  config.page_idem_num = page_shift;
  config.page_mem_size_total_threshold = npu_pool_size_;
  npu_mem_pool_ = MakeUnique<LlmMemPool>(config);
  LLM_CHECK_NOTNULL(npu_mem_pool_, "Failed to create memory pool");
  LLM_CHK_BOOL_RET_STATUS(
      rtMalloc(&npu_pool_memory_, npu_pool_size_, RT_MEMORY_HBM, LLM_MODULE_NAME_U16) == RT_ERROR_NONE,
      ge::LLM_OUT_OF_MEMORY, "Failed to allocate memory for memory_pool, config = %s", json_str.c_str());
  LLM_CHK_STATUS_RET(npu_mem_pool_->Initialize(npu_pool_memory_, npu_pool_size_),
                    "Failed to initialize memory pool, config = %s", json_str.c_str());
  LLM_CHK_STATUS(
      comm_mem_manager_->RegisterCommMemAddr(npu_pool_memory_, npu_pool_size_, HcclMemType::HCCL_MEM_TYPE_DEVICE));
  cache_manager_->SetNpuMemPool(npu_mem_pool_.get());
  LLMLOGI("npu memory_size = %lu B, page_shift = %zu, page_size = %lu B", npu_pool_size_, page_shift,
         (1UL << page_shift));
  return ge::SUCCESS;
}

ge::Status DataCacheEngine::InitializeHostMemoryPool(const std::map<ge::AscendString, ge::AscendString> &options) {
  const auto it = options.find(LLM_OPTION_HOST_MEM_POOL_CONFIG);
  if (it == options.cend()) {
    LLMLOGI("host memory pool is not enabled");
    return ge::SUCCESS;
  }
  const std::string &json_str = it->second.GetString();
  size_t page_shift = 16U;  // 64KB by default
  size_t host_pool_size = 0UL;
  LLM_CHK_STATUS_RET(ParseMemoryPoolConfig(json_str, host_pool_size, page_shift), "parse %s failed",
                    LLM_OPTION_HOST_MEM_POOL_CONFIG);
  ScalableConfig config{};
  config.page_idem_num = page_shift;
  config.page_mem_size_total_threshold = host_pool_size;
  host_mem_pool_ = MakeUnique<LlmMemPool>(config);
  LLM_CHECK_NOTNULL(host_mem_pool_);
  LLM_CHK_ACL_RET(rtMallocHost(&host_pool_memory_, host_pool_size, LLM_MODULE_NAME_U16));
  LLM_CHK_STATUS_RET(host_mem_pool_->Initialize(host_pool_memory_, host_pool_size),
                    "Failed to initialize host memory pool, config = %s", json_str.c_str());
  LLM_CHK_STATUS_RET(
      comm_mem_manager_->RegisterCommMemAddr(host_pool_memory_, host_pool_size, HCCL_MEM_TYPE_HOST));
  cache_manager_->SetHostMemPool(host_mem_pool_.get());
  LLMLOGI("host memory_size = %lu B, page_shift = %zu, page_size = %lu B", host_pool_size, page_shift,
         (1UL << page_shift));
  return ge::SUCCESS;
}

ge::Status DataCacheEngine::InitializeMemoryPool(const std::map<ge::AscendString, ge::AscendString> &options) {
  LLM_CHK_STATUS_RET(InitializeDeviceMemoryPool(options), "initialize device memory pool failed");
  LLM_CHK_STATUS_RET(InitializeHostMemoryPool(options), "initialize host memory pool failed");
  return ge::SUCCESS;
}

ge::Status DataCacheEngine::Allocate(const CacheDesc &cache_desc, const std::vector<CacheKey> &cache_keys, Cache &cache) {
  LLM_CHK_BOOL_RET_STATUS(((npu_pool_memory_ != nullptr) || (host_pool_memory_ != nullptr)), ge::LLM_FEATURE_NOT_ENABLED,
                         "memory pool is not enabled");
  LLM_CHK_BOOL_RET_STATUS(
      ((npu_pool_memory_ != nullptr) && (cache_desc.placement == static_cast<uint32_t>(CachePlacement::DEVICE))) ||
      ((host_pool_memory_ != nullptr) && (cache_desc.placement == static_cast<uint32_t>(CachePlacement::HOST))),
      ge::LLM_PARAM_INVALID, "placement must be set that matches memory pool config");
  LLM_CHK_BOOL_RET_STATUS(!cache_desc.shape.empty() && (cache_desc.shape.size() < kMaxDimNum), ge::LLM_PARAM_INVALID,
                         "Invalid shape: %s, dim_num (%zu) must be in range [1, 33)",
                         ToString(cache_desc.shape).c_str(), cache_desc.shape.size());
  const auto cache_id = cache_id_gen_.fetch_add(1, std::memory_order::memory_order_relaxed);
  LLM_CHECK_GE(cache_id, 1);
  LLM_CHK_STATUS_RET(cache_manager_->Allocate(cache_id, cache_desc, cache_keys, cache));
  LLMLOGI("[cache_id:%ld][Allocate] success, num_tensors = %u, shape = %s", cache_id, cache_desc.num_tensors,
         ToString(cache_desc.shape).c_str());
  return ge::SUCCESS;
}

ge::Status DataCacheEngine::Deallocate(int64_t cache_id) const{
  LLM_CHK_BOOL_RET_STATUS((npu_pool_memory_ != nullptr) || (host_pool_memory_ != nullptr), ge::LLM_FEATURE_NOT_ENABLED,
                         "memory pool is not enabled");
  return cache_manager_->Deallocate(cache_id);
}

ge::Status DataCacheEngine::RemoveCacheKey(const CacheKey &cache_key) const {
  LLM_CHK_BOOL_RET_STATUS((npu_pool_memory_ != nullptr) || (host_pool_memory_ != nullptr), ge::LLM_FEATURE_NOT_ENABLED,
                         "memory pool is not enabled");
  return cache_manager_->RemoveCacheKey(cache_key);
}

ge::Status DataCacheEngine::CopyCache(const CopyCacheParam &copy_cache_param) const {
  LLM_CHK_ACL_RET(rtCtxSetCurrent(rt_context_));
  return cache_manager_->CopyCache(copy_cache_param);
}

ge::Status DataCacheEngine::CheckCapacity(size_t size) {
  LLM_CHK_BOOL_RET_STATUS(npu_mem_pool_ != nullptr, ge::LLM_FEATURE_NOT_ENABLED, "memory pool is not enabled");
  auto ret = (npu_mem_pool_->AllocShared(size) != nullptr) ? ge::SUCCESS : ge::LLM_OUT_OF_MEMORY;
  LLMLOGI("check size = %zu, check result = %u", size, ret);
  return ret;
}

ge::Status DataCacheEngine::CheckParam(const CacheEntry &cache_entry, const PullCacheParam &pull_cache_param) {
  LLM_CHK_BOOL_RET_STATUS(pull_cache_param.batch_index < cache_entry.batch_size, ge::LLM_PARAM_INVALID,
                         "dst_batch_index:%u out of range [0, %u)", pull_cache_param.batch_index,
                         cache_entry.batch_size);
  if (pull_cache_param.decoder_blocks.empty()) {
    LLM_CHK_BOOL_RET_STATUS(cache_entry.num_blocks == 0 || cache_entry.cache_mem_type == CacheMemType::MIX,
                           ge::LLM_PARAM_INVALID,
                           "check failed, request expect local cache is non-blocks");
    LLM_CHK_BOOL_RET_STATUS((pull_cache_param.size < 0) ||
                               (static_cast<uint64_t>(pull_cache_param.size) <= cache_entry.stride),
                           ge::LLM_PARAM_INVALID,
                           "pull_size(%ld) > cache stride(%lu)",
                           pull_cache_param.size, cache_entry.stride);
  } else {
    LLM_CHK_BOOL_RET_STATUS(cache_entry.num_blocks > 0,
                           ge::LLM_PARAM_INVALID,
                           "check failed, request expect local cache is blocks");
  }
  if ((cache_entry.placement == CachePlacement::HOST) && (cache_entry.cache_mem_type == CacheMemType::BLOCKS)) {
    for (const auto block_index : pull_cache_param.decoder_blocks) {
      LLM_CHK_BOOL_RET_STATUS(block_index < cache_entry.num_blocks,
                             ge::LLM_PARAM_INVALID,
                             "local block index out of bound, index = %lu, num_blocks = %lu", block_index,
                             cache_entry.num_blocks);
    }
    LLM_CHK_BOOL_RET_STATUS(pull_cache_param.prompt_blocks.empty() ||
                               (pull_cache_param.decoder_blocks.size() == pull_cache_param.prompt_blocks.size()),
                           ge::LLM_PARAM_INVALID,
                           "check failed, src_block_index.size() = %zu, dst_block_index.size() = %zu",
                           pull_cache_param.prompt_blocks.size(),
                           pull_cache_param.decoder_blocks.size());
  }
  LLM_CHK_STATUS_RET(CheckTensorIndices(cache_entry, pull_cache_param), "tensor_indices is invalid");
  LLM_CHK_BOOL_RET_STATUS(pull_cache_param.tensor_num_per_layer >= 1,
                          ge::LLM_PARAM_INVALID,
                          "check failed, tensor_num_per_layer expect [1, %lu]", cache_entry.cache_addrs.size());
  return ge::SUCCESS;
}

ge::Status DataCacheEngine::CheckTensorIndices(const CacheEntry &cache_entry, const PullCacheParam &pull_cache_param) {
  // 此处cache_entry是pull的dst
  const size_t remainder = cache_entry.cache_addrs.size() % pull_cache_param.tensor_num_per_layer;
  if ((!pull_cache_param.src_tensor_indices.empty()) || (!pull_cache_param.dst_tensor_indices.empty())) {
    LLM_CHK_BOOL_RET_STATUS(remainder == 0U, ge::LLM_PARAM_INVALID,
                           "When using layer wise transfer, the tensor_num [%zu] of caches must be a multiple of tensor_num_per_layer[%lu].",
                           cache_entry.cache_addrs.size(), pull_cache_param.tensor_num_per_layer);
  }
  LLM_CHK_STATUS_RET(CheckTensorIndicesContinuous(pull_cache_param.src_tensor_indices),
                    "src_tensor_indices is not continuous");
  LLM_CHK_STATUS_RET(CheckTensorIndicesContinuous(pull_cache_param.dst_tensor_indices),
                    "dst_tensor_indices is not continuous");

  if (!pull_cache_param.dst_tensor_indices.empty()) {
    LLM_CHK_BOOL_RET_STATUS(pull_cache_param.dst_tensor_indices.size() <= cache_entry.cache_addrs.size(),
                           ge::LLM_PARAM_INVALID, "dst_tensor_indices size[%zu] is out of range[0, %zu]",
                           pull_cache_param.dst_tensor_indices.size(), cache_entry.cache_addrs.size());
    LLM_CHK_BOOL_RET_STATUS((pull_cache_param.dst_tensor_indices.front() < cache_entry.cache_addrs.size()) &&
                               (pull_cache_param.dst_tensor_indices.back() < cache_entry.cache_addrs.size()),
                           ge::LLM_PARAM_INVALID,
                           "dst_tensor_indices start index[%lu] or end index[%lu] is out of range[0, %zu)",
                           pull_cache_param.dst_tensor_indices.front(), pull_cache_param.dst_tensor_indices.back(),
                           cache_entry.cache_addrs.size());
  }
  if ((!pull_cache_param.src_tensor_indices.empty()) && (!pull_cache_param.dst_tensor_indices.empty())) {
    LLM_CHK_BOOL_RET_STATUS(pull_cache_param.src_tensor_indices.size() == pull_cache_param.dst_tensor_indices.size(),
                           ge::LLM_PARAM_INVALID,
                           "src_tensor_indices size[%zu] is not match dst_tensor_indices size[%zu]",
                           pull_cache_param.src_tensor_indices.size(), pull_cache_param.dst_tensor_indices.size());
  } else if (!pull_cache_param.src_tensor_indices.empty()) {
    LLM_CHK_BOOL_RET_STATUS(pull_cache_param.src_tensor_indices.size() == cache_entry.cache_addrs.size(),
                           ge::LLM_PARAM_INVALID, "src_tensor_indices size[%zu] is not match dst_num_tensors size[%zu]",
                           pull_cache_param.src_tensor_indices.size(), cache_entry.cache_addrs.size());
  } else {
    // default
  }
  return ge::SUCCESS;
}

ge::Status DataCacheEngine::TransferCache(const uint64_t task_id, const TransferCacheConfig &transfer_cache_config,
                                          const TransferBlockConfig &transfer_block_config) {
  // cache_id is local, find local addr by cache_id
  CacheEntry cache_entry;
  LLM_CHK_BOOL_RET_STATUS(cache_manager_->GetCacheEntry(transfer_cache_config.src_cache_id, cache_entry),
                         ge::LLM_KV_CACHE_NOT_EXIST, "cache id:%ld not found", transfer_cache_config.src_cache_id);

  const auto entity = comm_entity_manager_->GetEntityByRemoteClusterId(transfer_cache_config.cluster_id);
  LLM_CHK_BOOL_RET_STATUS(entity != nullptr, ge::LLM_NOT_YET_LINK,
                         "current cluster is not linked with remote cluster:%lu", transfer_cache_config.cluster_id);
  std::lock_guard<std::mutex> pull_lock(entity->GetPullMutex());
  // in case of entity is erased here, can not delete.
  LLM_CHK_BOOL_RET_STATUS((entity->GetCurState() != FsmState::FSM_DESTROYED_STATE), ge::LLM_NOT_YET_LINK,
                         "current cluster is not linked with remote cluster:%lu", transfer_cache_config.cluster_id);
  LLM_CHK_BOOL_RET_STATUS(entity->CheckEntityInfo(), ge::LLM_NOT_YET_LINK,
                         "transfer cache must wait until the query_register_mem_status return ok");
  LLM_CHK_BOOL_RET_STATUS(transfer_cache_config.tensor_num_per_layer >= 1,
                         ge::LLM_PARAM_INVALID,
                         "check failed, tensor_num_per_layer expect [1, %lu]", cache_entry.cache_addrs.size());
  LLMLOGI("Transfer cache with tensor num per layer:%lu.", transfer_cache_config.tensor_num_per_layer);
  TemporaryRtContext with_context(rt_context_);
  rtError_t ret = RT_ERROR_NONE;
  std::call_once(create_stream_once_flag_, [&ret, this]() {
    ret = rtStreamCreateWithFlags(&transfer_stream_, RT_STREAM_PRIORITY_DEFAULT,
                                  RT_STREAM_FAST_LAUNCH | RT_STREAM_FAST_SYNC);
  });
  LLM_ASSERT_RT_OK(ret, "create transfer stream failed");
  LLM_ASSERT_NOTNULL(transfer_stream_, "transfer stream is nullptr");
  LLM_DISMISSABLE_GUARD(abort_stream, [this]() -> void {
    LLM_CHK_ACL(rtStreamAbort(transfer_stream_));
  });
  LayerWiseTransferJob layer_wise_transfer_job(*entity, transfer_stream_);
  LLM_CHK_STATUS_RET(layer_wise_transfer_job.TransferCache(cache_entry, transfer_cache_config, transfer_block_config,
                                                          sync_cache_timeout_, access_remote_cache_),
                    "task:%lu of cluster:%lu transfer cache of layer[%lu] failed", task_id,
                    transfer_cache_config.cluster_id, transfer_cache_config.layer_index);
  LLM_DISMISS_GUARD(abort_stream);
  return ge::SUCCESS;
}
}  // namespace llm
