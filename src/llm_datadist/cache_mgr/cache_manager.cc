/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "cache_manager.h"
#include <numeric>
#include "runtime/rt.h"
#include "common/llm_utils.h"
#include "common/llm_thread_pool.h"
#include "common/llm_scope_guard.h"

namespace llm {
namespace {
constexpr uint64_t kMaxBlockSize = 4UL * 1024 * 1024 * 1024; // 4GB

class CopyJob {
 public:
  explicit CopyJob(rtStream_t stream, bool mbuf_involved = false, uint64_t max_block_size = kMaxBlockSize)
      : stream_(stream), mbuf_involved_(mbuf_involved), max_block_size_(max_block_size) {
  }
  ~CopyJob() = default;

  ge::Status AddCopyTask(void *dst, uint64_t dest_max, const void *src, uint64_t count, rtMemcpyKind_t kind) {
    if (!NeedCopyAsync(kind, count)) {
      if (rt_context_ == nullptr) {
        LLM_CHK_BOOL_RET_STATUS(rtCtxGetCurrent(&rt_context_) == RT_ERROR_NONE, ge::FAILED, "Failed to get rt context");
      }
      auto fut = pool_.commit([this, dst, dest_max, src, count, kind]() -> ge::Status {
        (void) rtCtxSetCurrent(rt_context_);
        const auto ret = mbuf_involved_ ? rtMemcpyEx(dst, dest_max, src, count, kind) :
                         rtMemcpy(dst, dest_max, src, count, kind);
        LLM_CHK_BOOL_RET_STATUS(ret == RT_ERROR_NONE, ge::FAILED,
                               "failed to copy cache, rt_ret = 0x%X", static_cast<uint32_t>(ret));
        return ge::SUCCESS;
      });
      LLM_CHK_BOOL_RET_STATUS(fut.valid(), ge::FAILED, "Failed to commit copy task");
      copy_futs_.emplace_back(std::move(fut));
      return ge::SUCCESS;
    }
    need_sync_ = true;
    uint64_t offset = 0;
    uint64_t remaining = count;
    while (remaining > 0) {
      uint64_t size_to_copy = remaining <= max_block_size_ ? remaining : max_block_size_;
      auto dst_start = static_cast<uint8_t *>(dst) + offset;
      auto src_start = static_cast<const uint8_t *>(src) + offset;
      LLM_CHK_ACL_RET(rtMemcpyAsyncWithoutCheckKind(dst_start,
                                                    dest_max,
                                                    src_start,
                                                    size_to_copy,
                                                    RT_MEMCPY_DEVICE_TO_DEVICE,
                                                    stream_));
      offset += max_block_size_;
      remaining -= size_to_copy;
    }
    return ge::SUCCESS;
  }

  ge::Status GetResult() {
    if (need_sync_) {
      LLM_CHK_STATUS_RET(rtStreamSynchronize(stream_));
    }
    for (size_t i = 0U; i < copy_futs_.size(); ++i) {
      LLM_CHK_STATUS_RET(copy_futs_[i].get(), "Failed to copy cache, index = %zu", i);
    }
    return ge::SUCCESS;
  }

 private:
  bool NeedCopyAsync(rtMemcpyKind_t kind, uint64_t count) const {
    constexpr uint64_t kMinBlockSize = 2UL * 1024 * 1024; // 2MB
    return (kind == RT_MEMCPY_DEVICE_TO_DEVICE) && (count >= kMinBlockSize || mbuf_involved_);
  }

  rtStream_t stream_ = nullptr;
  rtContext_t rt_context_ = nullptr;
  bool need_sync_ = false;
  bool mbuf_involved_ = false;
  uint64_t max_block_size_ = 0;
  LLMThreadPool pool_{"ge_llm_cahm", 4};
  std::vector<std::future<ge::Status>> copy_futs_;
};

void RemoveTensorIndices(const std::unordered_set<uint64_t> &tensor_indices,
                         std::unordered_set<uint64_t> &cache_tensor_indices) {
  std::unordered_set<uint64_t> remaining_indices;
  for (const auto &index : cache_tensor_indices) {
    if (tensor_indices.find(index) == tensor_indices.end()) {
      remaining_indices.insert(index);
    }
  }
  cache_tensor_indices.swap(remaining_indices);
}
}  // namespace

const CacheEntry *CacheManager::DoGetCacheEntry(int64_t cache_id) const {
  const auto iter = cache_id_to_entry_.find(cache_id);
  return (iter == cache_id_to_entry_.cend()) ? nullptr : &iter->second;
}

bool CacheManager::GetCacheKey(const std::pair<int64_t, uint64_t> &cache_id_and_batch_index,
                               DataCacheKey &cache_key) const {
  std::lock_guard<std::mutex> lk(mu_);
  const auto iter = cache_id_and_batch_id_to_cache_key_.find(cache_id_and_batch_index);
  if (iter != cache_id_and_batch_id_to_cache_key_.end()) {
    cache_key = iter->second;
    return true;
  }
  return false;
}

bool CacheManager::GetCacheEntry(const int64_t cache_id, CacheEntry &cache_entry) const {
  std::lock_guard<std::mutex> lk(mu_);
  auto entry = DoGetCacheEntry(cache_id);
  bool success = (entry != nullptr);
  if (success) {
    cache_entry = *entry;
  }
  return success;
}

bool CacheManager::GetCacheEntry(const DataCacheKey &cache_key, bool is_prefix, CacheEntry &cache_entry) const {
  std::lock_guard<std::mutex> lk(mu_);
  auto &cache_key_to_id = is_prefix ? prefix_key_to_id_ : cache_key_to_id_;
  const auto iter = cache_key_to_id.find(cache_key);
  if (iter == cache_key_to_id.cend()) {
    return false;
  }
  // cache_key exist, cache entry necessarily exist
  cache_entry = *DoGetCacheEntry(iter->second);
  return true;
}

ge::Status CacheManager::RegisterCacheEntry(int64_t cache_id, const std::vector<CacheKey> &cache_keys,
                                            const CacheDesc &cache_desc, std::vector<uintptr_t> &addrs,
                                            int64_t tensor_size) {
  CacheEntry cache_entry = CreateCacheEntry(cache_desc, addrs, tensor_size);
  {
    std::lock_guard<std::mutex> lk(mu_);
    AddCacheIndices(cache_entry, cache_id, cache_keys);
    cache_id_to_entry_[cache_id] = std::move(cache_entry);
    LLM_CHK_STATUS_RET(UpdateCacheTable(), "Failed to update cache table");
  }
  return ge::SUCCESS;
}

ge::Status CacheManager::UnregisterCacheEntry(int64_t cache_id) {
  std::lock_guard<std::mutex> lk(mu_);
  auto iter = cache_id_to_entry_.find(cache_id);
  if (iter == cache_id_to_entry_.cend()) {
    return ge::SUCCESS;
  }
  cache_id_to_entry_.erase(iter);
  LLM_CHK_STATUS_RET(UpdateCacheTable(), "Failed to update cache table");
  return ge::SUCCESS;
}

void CacheManager::SetNpuMemPool(LlmMemPool *llm_mem_pool) {
  npu_mem_pool_ = llm_mem_pool;
}

void CacheManager::SetHostMemPool(LlmMemPool *llm_mem_pool) {
  host_mem_pool_ = llm_mem_pool;
}

ge::Status CacheManager::Allocate(int64_t cache_id,
                                  const CacheDesc &cache_desc,
                                  const std::vector<CacheKey> &cache_keys,
                                  Cache &cache) {
  LlmMemPool *mem_pool =
      (cache_desc.placement == static_cast<uint32_t>(CachePlacement::HOST)) ? host_mem_pool_ : npu_mem_pool_;
  LLM_CHK_BOOL_RET_STATUS(mem_pool != nullptr, ge::LLM_FEATURE_NOT_ENABLED,
                         "memory pool is not enabled");
  LLM_CHK_STATUS_RET(CheckCacheKeys(cache_desc, cache_keys), "Check cache_keys failed");
  int64_t tensor_size;
  LLM_CHK_STATUS_RET(LLMUtils::CalcTensorMemSize(cache_desc.shape, cache_desc.data_type, tensor_size),
                    "Failed to calc tensor size");
  std::vector<std::shared_ptr<void>> cache_tensors;
  std::vector<uintptr_t> tensor_addresses;
  tensor_addresses.reserve(cache_desc.num_tensors);
  cache_tensors.reserve(cache_desc.num_tensors);
  for (uint32_t i = 0U; i < cache_desc.num_tensors; ++i) {
    auto tensor_addr = mem_pool->AllocShared(static_cast<size_t>(tensor_size));
    if (tensor_addr == nullptr) {
      mem_pool->LogPoolState();
      LLMLOGE(ge::LLM_OUT_OF_MEMORY, "Failed to allocate memory, size = %ld, index = %u", tensor_size, i);
      return ge::LLM_OUT_OF_MEMORY;
    }
    (void)cache_tensors.emplace_back(tensor_addr);
    (void)tensor_addresses.emplace_back(reinterpret_cast<uintptr_t>(tensor_addr.get()));
  }
  auto cache_entry = CreateCacheEntry(cache_desc, tensor_addresses, tensor_size);
  cache_entry.is_owned = true; // allocated by llm datadist
  cache_entry.ext_ref_count = 1;
  cache_entry.cache_addrs = cache_tensors;
  {
    std::lock_guard<std::mutex> lk(mu_);
    AddCacheIndices(cache_entry, cache_id, cache_keys);
    cache_id_to_entry_[cache_id] = std::move(cache_entry);
  }
  cache.cache_id = cache_id;
  (void)cache.per_device_tensor_addrs.emplace_back(std::move(tensor_addresses));
  LLMLOGI("[cache_id:%ld][Allocate] success, num_tensors = %u, shape = %s， placement = %u", cache_id,
         cache_desc.num_tensors, ToString(cache_desc.shape).c_str(), cache_desc.placement);
  std::lock_guard<std::mutex> lk(mu_);
  LLM_CHK_STATUS_RET(UpdateCacheTable(), "Failed to update cache table");
  return ge::SUCCESS;
}

CacheEntry CacheManager::CreateCacheEntry(const CacheDesc &cache_desc,
                                          std::vector<uintptr_t> &addrs,
                                          int64_t tensor_size) {
  CacheEntry cache_entry{};
  cache_entry.cache_addrs.reserve(addrs.size());
  for (const auto addr : addrs) {
    cache_entry.cache_addrs.emplace_back(std::shared_ptr<void>(ValueToPtr(addr), &NoDelete));
  }
  cache_entry.tensor_size = static_cast<uint64_t>(tensor_size);
  if (cache_desc.cache_mem_type == CacheMemType::BLOCKS) {
    cache_entry.batch_size = 1U;
    cache_entry.num_blocks = static_cast<uint64_t>(cache_desc.shape.front());
    cache_entry.stride = (cache_entry.tensor_size / cache_entry.num_blocks);
  } else if (cache_desc.cache_mem_type == CacheMemType::CACHE) {
    cache_entry.batch_size = cache_desc.shape.empty() ? 1U : static_cast<uint32_t>(cache_desc.shape.front());
    cache_entry.stride = (cache_entry.tensor_size / cache_entry.batch_size);
  } else {
    cache_entry.batch_size = cache_desc.shape.empty() ? 1U : static_cast<uint32_t>(cache_desc.shape.front());
    cache_entry.num_blocks = cache_entry.batch_size;
    cache_entry.stride = (cache_entry.tensor_size / cache_entry.batch_size);
  }
  cache_entry.cache_mem_type = cache_desc.cache_mem_type;
  cache_entry.seq_len_dim_index = cache_desc.seq_len_dim_index;
  cache_entry.placement = static_cast<CachePlacement>(cache_desc.placement);
  cache_entry.remote_accessible = cache_desc.remote_accessible;
  return cache_entry;
}

ge::Status CacheManager::CheckCacheKeys(const CacheDesc &cache_desc, const std::vector<CacheKey> &cache_keys) {
  LLM_CHK_BOOL_RET_STATUS(cache_keys.size() <= static_cast<size_t>(cache_desc.shape.front()), ge::LLM_PARAM_INVALID,
                         "Number of cache_keys(%zu) > batch_size (%ld)", cache_keys.size(), cache_desc.shape.front());
  std::set<DataCacheKey> data_cache_keys;
  for (const auto &cache_key : cache_keys) {
    bool is_prefix = false;
    auto data_cache_key = CreateDataCacheKey(cache_key, is_prefix);
    const auto &key_to_id = is_prefix ? prefix_key_to_id_ : cache_key_to_id_;
    const auto it = key_to_id.find(data_cache_key);
    LLM_CHK_BOOL_RET_STATUS(it == key_to_id.cend(),
                           ge::LLM_PARAM_INVALID,
                           "cache_key (%lu, %lu) already bound to cache_id(%ld), is_prefix = %d",
                           data_cache_key.first, data_cache_key.second, it->second, static_cast<int32_t>(is_prefix));
    LLM_CHK_BOOL_RET_STATUS(data_cache_keys.emplace(data_cache_key).second,
                           ge::LLM_PARAM_INVALID,
                           "multiply identical cache_keys (%lu, %lu) occurred in the request",
                           data_cache_key.first,
                           data_cache_key.second);
  }
  return ge::SUCCESS;
}

void CacheManager::AddCacheIndices(CacheEntry &cache_entry,
                                   int64_t cache_id,
                                   const std::vector<CacheKey> &cache_keys) {
  uint32_t batch_index = 0U;
  for (const auto &cache_key : cache_keys) {
    bool is_prefix = false;
    auto data_cache_key = CreateDataCacheKey(cache_key, is_prefix);
    if (cache_entry.num_blocks > 0U) {
      cache_key_to_id_[data_cache_key] = cache_id;
    } else if (data_cache_key.first != UINT64_MAX) {
      auto &key_to_id = is_prefix ? prefix_key_to_id_ : cache_key_to_id_;
      (void) key_to_id.emplace(data_cache_key, cache_id);
      cache_entry.id_to_batch_index_and_size[data_cache_key.first] = std::make_pair(batch_index, cache_entry.stride);
      cache_id_and_batch_id_to_cache_key_[std::make_pair(cache_id, batch_index)] = data_cache_key;
      std::vector<uint64_t> tmp_tensor_indices(cache_entry.cache_addrs.size());
      std::iota(tmp_tensor_indices.begin(), tmp_tensor_indices.end(), 0);
      std::unordered_set<uint64_t> tensor_indices(tmp_tensor_indices.begin(), tmp_tensor_indices.end());
      cache_id_to_tensor_indices_[cache_id] = tensor_indices;
      LLMLOGI("[cache_id:%ld][AddCacheIndex] success, cache_key(%lu, %lu) added, batch_index = %u, size = %lu",
             cache_id, data_cache_key.first, data_cache_key.second, batch_index, cache_entry.stride);
    } else {
      // do nothing
    }
    ++batch_index;
  }
}

DataCacheKey CacheManager::CreateDataCacheKey(const CacheKey &cache_key, bool &is_prefix) {
  is_prefix = cache_key.prefix_id != UINT64_MAX;
  return is_prefix ? std::make_pair(cache_key.prefix_id, cache_key.model_id) :
         std::make_pair(cache_key.req_id, cache_key.model_id);
}

ge::Status CacheManager::Deallocate(int64_t cache_id) {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = cache_id_to_entry_.find(cache_id);
  if (it == cache_id_to_entry_.cend()) {
    LLMLOGI("[cache_id:%ld][Deallocate] cache_id does not exist", cache_id);
    return ge::SUCCESS;
  }

  auto &cache_entry = it->second;
  if (!cache_entry.is_owned) {
    LLMLOGI("[cache_id:%ld][Deallocate] cannot deallocate registered cache", cache_id);
    return ge::SUCCESS;
  }
  if (cache_entry.num_blocks > 0U) {
    // remove cache keys for blocks cache
    for (auto entry_it = cache_key_to_id_.begin(); entry_it != cache_key_to_id_.end();) {
      if (entry_it->second == cache_id) {
        entry_it = cache_key_to_id_.erase(entry_it);
        continue;
      }
      entry_it++;
    }
    (void) cache_id_to_entry_.erase(it);
    LLMLOGI("[cache_id:%ld][Deallocate blocks cache] success", cache_id);
    LLM_CHK_STATUS_RET(UpdateCacheTable(), "Failed to update cache table");
    return ge::SUCCESS;
  }
  cache_entry.ext_ref_count = 0;
  if (cache_entry.id_to_batch_index_and_size.empty()) {
    (void) cache_id_to_entry_.erase(it);
    LLMLOGI("[cache_id:%ld][Deallocate] success", cache_id);
    LLM_CHK_STATUS_RET(UpdateCacheTable(), "Failed to update cache table");
  } else {
    LLMLOGI("[cache_id:%ld][Deallocate] delayed for that it is still referenced by %zu cache_key(s)",
           cache_id, cache_entry.id_to_batch_index_and_size.size());
  }
  return ge::SUCCESS;
}

ge::Status CacheManager::RemoveCacheKey(const CacheKey &cache_key) {
  bool is_prefix = false;
  auto data_cache_key = CreateDataCacheKey(cache_key, is_prefix);
  return RemoveCacheKey(data_cache_key, is_prefix);
}

ge::Status CacheManager::RemoveCacheKey(const DataCacheKey &data_cache_key, bool is_prefix,
                                        const std::unordered_set<uint64_t> &tensor_indices) {
  auto &key_to_id = is_prefix ? prefix_key_to_id_ : cache_key_to_id_;
  std::lock_guard<std::mutex> lk(mu_);
  auto it = key_to_id.find(data_cache_key);
  if (it == key_to_id.cend()) {
    LLMLOGI("[RemoveCacheKey] cache_key (%lu, %lu) does not exist, is_prefix = %d",
           data_cache_key.first, data_cache_key.second, static_cast<int32_t>(is_prefix));
    return ge::SUCCESS;
  }
  const auto cache_id = it->second;
  auto &cache_entry = cache_id_to_entry_.at(cache_id);
  if (!cache_entry.is_owned) {
    LLMLOGI("[cache_id:%ld] [RemoveCacheKey] does not operate on registered cache, "
           "cache_key = (%lu, %lu), is_prefix = %d",
           cache_id, data_cache_key.first, data_cache_key.second, static_cast<int32_t>(is_prefix));
    return ge::SUCCESS;
  }
  auto &cache_tensor_indices = cache_id_to_tensor_indices_[cache_id];
  if (tensor_indices.empty()) {
    cache_tensor_indices.clear();
  } else {
    RemoveTensorIndices(tensor_indices, cache_tensor_indices);
    LLMLOGI("[cache_id:%ld] [RemoveCacheKey] already removed num[%zu] tensors, start index is:%lu", cache_id,
           tensor_indices.size(), *tensor_indices.begin());
  }
  if (!cache_tensor_indices.empty()) {
    LLMLOGI("[cache_id:%ld] [RemoveCacheKey] There are still %zu tensors that have not been removed", cache_id,
           cache_tensor_indices.size());
    return ge::SUCCESS;
  }
  const auto batch_index_and_size_it = cache_entry.id_to_batch_index_and_size.find(data_cache_key.first);
  if (batch_index_and_size_it != cache_entry.id_to_batch_index_and_size.cend()) {
    const auto batch_index = batch_index_and_size_it->second.first;
    (void) cache_id_and_batch_id_to_cache_key_.erase(std::make_pair(cache_id, batch_index));
    (void) cache_entry.id_to_batch_index_and_size.erase(batch_index_and_size_it);
  }
  (void) key_to_id.erase(it);
  LLMLOGI("[cache_id:%ld] [RemoveCacheKey] success, cache_key = (%lu, %lu), is_prefix = %d",
         cache_id, data_cache_key.first, data_cache_key.second, static_cast<int32_t>(is_prefix));
  if ((cache_entry.ext_ref_count == 0) && cache_entry.id_to_batch_index_and_size.empty()) {
    (void)cache_id_to_entry_.erase(cache_id);
    LLMLOGI("[cache_id:%ld][Deallocate] success", cache_id);
  }
  LLM_CHK_STATUS_RET(UpdateCacheTable(), "Failed to update cache table");
  return ge::SUCCESS;
}

ge::Status CacheManager::CopyCache(const CopyCacheParam &copy_cache_param) {
  LLMLOGI("CopyCache [%ld->%ld]", copy_cache_param.src_cache_id, copy_cache_param.dst_cache_id);
  const auto src_id = copy_cache_param.src_cache_id;
  const auto dst_id = copy_cache_param.dst_cache_id;
  CacheEntry src_cache_entry;
  CacheEntry dst_cache_entry;
  LLM_CHK_BOOL_RET_STATUS(GetCacheEntry(src_id, src_cache_entry), ge::LLM_KV_CACHE_NOT_EXIST,
                         "[Copy][%ld->%ld] failed, src_cache_id not found", src_id, dst_id);
  LLM_CHK_BOOL_RET_STATUS(GetCacheEntry(dst_id, dst_cache_entry), ge::LLM_KV_CACHE_NOT_EXIST,
                         "[Copy][%ld->%ld] failed, dst_cache_id not found", src_id, dst_id);
  LLM_CHK_BOOL_RET_STATUS(src_cache_entry.cache_addrs.size() == dst_cache_entry.cache_addrs.size(),
                         ge::LLM_PARAM_INVALID,
                         "num_tensors mismatches, src_tensor_num = %zu, dst_tensor_num = %zu",
                         src_cache_entry.cache_addrs.size(), dst_cache_entry.cache_addrs.size());
  if (copy_cache_param.copy_block_infos.empty()) {
    return CopyCacheForContinuous(src_cache_entry, dst_cache_entry, copy_cache_param,
                                  src_cache_entry.cache_addrs.size());
  } else {
    return CopyCacheForBlocks(src_cache_entry, dst_cache_entry, copy_cache_param, src_cache_entry.cache_addrs.size());
  }
}

ge::Status CacheManager::CopyCacheForContinuous(const CacheEntry &src_cache_entry,
                                                const CacheEntry &dst_cache_entry,
                                                const CopyCacheParam &copy_cache_param,
                                                size_t per_device_addr_num,
                                                size_t device_index) {
  const auto src_id = copy_cache_param.src_cache_id;
  const auto dst_id = copy_cache_param.dst_cache_id;
  uint64_t copy_size;
  LLM_CHK_STATUS_RET(CheckCopyParams(src_cache_entry, dst_cache_entry, copy_cache_param, copy_size),
                    "[Copy][%ld->%ld] check param failed", src_id, dst_id);
  auto src_offset = src_cache_entry.stride * copy_cache_param.src_batch_index + copy_cache_param.offset;
  auto dst_offset = dst_cache_entry.stride * copy_cache_param.dst_batch_index + copy_cache_param.offset;
  LLMLOGI("[Copy][%ld->%ld] start", src_id, dst_id);
  EnsureCopyStream(device_index);
  const auto copy_kind = ResolveCopyKind(src_cache_entry.placement, dst_cache_entry.placement);
  CopyJob copy_job(copy_streams_[device_index], copy_cache_param.mbuf_involved);
  auto begin = device_index * per_device_addr_num;
  for (size_t i = 0U; i < per_device_addr_num; ++i) {
    auto src_addr = PtrToPtr<void, uint8_t>(src_cache_entry.cache_addrs[begin + i].get()) + src_offset;
    auto dst_max = dst_cache_entry.stride - copy_cache_param.offset;
    auto dst_addr = PtrToPtr<void, uint8_t>(dst_cache_entry.cache_addrs[begin + i].get()) + dst_offset;
    LLM_CHK_STATUS_RET(copy_job.AddCopyTask(dst_addr, dst_max, src_addr, copy_size, copy_kind),
                      "[Copy][%ld->%ld] invoke rtMemcpy failed, index = %zu",
                      src_id,
                      dst_id,
                      i);
  }
  LLM_CHK_STATUS_RET(copy_job.GetResult(), "[Copy][%ld->%ld] invoke rtStreamSynchronize failed", src_id, dst_id);
  LLMLOGI("[Copy][%ld->%ld] success, num_tensors = %zu, src_batch_index = %u, "
         "dst_batch_index = %u, offset = %lu, size = %ld",
         src_id, dst_id, per_device_addr_num, copy_cache_param.src_batch_index,
         copy_cache_param.dst_batch_index, copy_cache_param.offset, copy_cache_param.size);
  return ge::SUCCESS;
}

ge::Status CacheManager::CopyCacheForBlocks(const CacheEntry &src_cache_entry,
                                            const CacheEntry &dst_cache_entry,
                                            const CopyCacheParam &copy_cache_param,
                                            size_t per_device_addr_num,
                                            size_t device_index) {
  const auto src_id = copy_cache_param.src_cache_id;
  const auto dst_id = copy_cache_param.dst_cache_id;
  LLM_CHK_BOOL_RET_STATUS(src_cache_entry.stride == dst_cache_entry.stride, ge::FAILED,
                         "[Copy][%ld->%ld] failed, block_size mismatches, src = %lu, dst = %lu",
                         src_id, dst_id, src_cache_entry.stride, dst_cache_entry.stride);
  const auto block_size = src_cache_entry.stride;
  LLMLOGI("[Copy][%ld->%ld] start", src_id, dst_id);
  EnsureCopyStream(device_index);
  CopyJob copy_job(copy_streams_[device_index], copy_cache_param.mbuf_involved);
  const auto copy_kind = ResolveCopyKind(src_cache_entry.placement, dst_cache_entry.placement);
  auto begin = device_index * per_device_addr_num;
  for (size_t i = 0U; i < per_device_addr_num; ++i) {
    auto src_addr_base = PtrToPtr<void, uint8_t>(src_cache_entry.cache_addrs[begin + i].get());
    auto dst_addr_base = PtrToPtr<void, uint8_t>(dst_cache_entry.cache_addrs[begin + i].get());
    for (auto &block_index_pair : copy_cache_param.copy_block_infos) {
      const auto src_block_index = block_index_pair.first;
      const auto dst_block_index = block_index_pair.second;
      LLM_CHK_BOOL_RET_STATUS(src_block_index < src_cache_entry.num_blocks, ge::LLM_PARAM_INVALID,
                             "src_block_index:%lu out of range [0, %lu)", src_block_index, src_cache_entry.num_blocks);
      LLM_CHK_BOOL_RET_STATUS(dst_block_index < dst_cache_entry.num_blocks, ge::LLM_PARAM_INVALID,
                             "dst_block_index:%lu out of range [0, %lu)", dst_block_index, src_cache_entry.num_blocks);
      auto src_addr = src_addr_base + block_size * src_block_index;
      auto dst_addr = dst_addr_base + block_size * dst_block_index;
      LLM_CHK_STATUS_RET(copy_job.AddCopyTask(dst_addr, block_size, src_addr, block_size, copy_kind),
                        "[Copy][%ld->%ld] invoke rtMemcpy failed, index = %zu", src_id, dst_id, i);
    }
  }
  LLM_CHK_STATUS_RET(copy_job.GetResult(), "[Copy][%ld->%ld] invoke rtStreamSynchronize failed", src_id, dst_id);
  LLMLOGI("[Copy][%ld->%ld] success, num_tensors = %zu, num_blocks = %zu",
         src_id, dst_id, per_device_addr_num, copy_cache_param.copy_block_infos.size());
  return ge::SUCCESS;
}

ge::Status CacheManager::CheckCopyParams(const CacheEntry &src_cache_entry,
                                         const CacheEntry &dst_cache_entry,
                                         const CopyCacheParam &copy_cache_param,
                                         uint64_t &copy_size) {
  const auto src_batch_size = src_cache_entry.batch_size;
  LLM_CHK_BOOL_RET_STATUS(copy_cache_param.src_batch_index < src_batch_size,
                         ge::LLM_PARAM_INVALID,
                         "src_batch_index (%u) out of range [0, %u)",
                         copy_cache_param.src_batch_index, src_batch_size);
  const auto dst_batch_size = dst_cache_entry.batch_size;
  LLM_CHK_BOOL_RET_STATUS(copy_cache_param.dst_batch_index < dst_batch_size,
                         ge::LLM_PARAM_INVALID,
                         "dst_batch_index (%u) out of range [0, %u)",
                         copy_cache_param.dst_batch_index, dst_batch_size);
  if (copy_cache_param.offset > 0) {
    LLM_CHK_BOOL_RET_STATUS(copy_cache_param.offset < src_cache_entry.stride,
                           ge::LLM_PARAM_INVALID,
                           "offset out of range of src cache tensor, offset = %lu, tensor stride = %lu",
                           copy_cache_param.offset, src_cache_entry.stride);
    LLM_CHK_BOOL_RET_STATUS(copy_cache_param.offset < dst_cache_entry.stride,
                           ge::LLM_PARAM_INVALID,
                           "offset out of range of dst cache tensor, offset = %lu, tensor stride = %lu",
                           copy_cache_param.offset, dst_cache_entry.stride);
  }
  copy_size = copy_cache_param.size != -1 ? static_cast<uint64_t>(copy_cache_param.size)
                                          : src_cache_entry.stride - copy_cache_param.offset;
  LLM_CHK_BOOL_RET_STATUS(copy_size <= (src_cache_entry.stride - copy_cache_param.offset),
                         ge::LLM_PARAM_INVALID,
                         "src_tensor out of range, offset = %lu, copy_size = %ld, tensor stride = %lu",
                         copy_cache_param.offset, copy_cache_param.size, src_cache_entry.stride);
  LLM_CHK_BOOL_RET_STATUS(copy_size <= (dst_cache_entry.stride - copy_cache_param.offset),
                         ge::LLM_PARAM_INVALID,
                         "dst_tensor out of range, offset = %lu, copy_size = %ld, tensor stride = %lu, "
                         "src tensor stride = %lu",
                         copy_cache_param.offset, copy_cache_param.size, dst_cache_entry.stride,
                         src_cache_entry.stride);
  return ge::SUCCESS;
}

rtMemcpyKind_t CacheManager::ResolveCopyKind(CachePlacement src_placement, CachePlacement dst_placement) {
  return (src_placement == CachePlacement::HOST) ?
         ((dst_placement == CachePlacement::HOST) ? RT_MEMCPY_HOST_TO_HOST : RT_MEMCPY_HOST_TO_DEVICE) :
         ((dst_placement == CachePlacement::HOST) ? RT_MEMCPY_DEVICE_TO_HOST : RT_MEMCPY_DEVICE_TO_DEVICE);
}

void CacheManager::DestroyCopyStream(size_t device_index) {
  if (device_index < copy_streams_.size() && (copy_streams_[device_index] != nullptr)) {
    LLM_CHK_ACL(rtStreamDestroy(copy_streams_[device_index]));
    copy_streams_[device_index] = nullptr;
  }
}

void CacheManager::Finalize() {
  DestroyCopyStream(0U);
  cache_access_table_updater_.Finalize();
}

ge::Status CacheManager::EnsureCopyStream(size_t device_index) {
  std::lock_guard<std::mutex> lk(copy_mu_);
  if (copy_streams_[device_index] == nullptr) {
    LLM_CHK_ACL_RET(rtStreamCreate(&copy_streams_[device_index], RT_STREAM_PRIORITY_DEFAULT));
  }
  return ge::SUCCESS;
}

ge::Status CacheManager::InitCopyStreams(size_t device_num) {
  copy_streams_.resize(device_num);
  return ge::SUCCESS;
}

ge::Status CacheManager::Initialize(bool access_remote_cache) {
  enable_remote_cache_accessible_ = access_remote_cache;
  LLM_CHK_STATUS_RET(cache_access_table_updater_.Initialize(access_remote_cache));
  LLM_CHK_ACL_RET(rtCtxGetCurrent(&rt_context_));
  copy_streams_.resize(1U);
  return ge::SUCCESS;
}

std::pair<void *, size_t> CacheManager::GetCacheTableBufferAndSize() const {
  return cache_access_table_updater_.GetDevBufferAndSize();
}

ge::Status CacheManager::UpdateCacheTable() {
  if (!enable_remote_cache_accessible_) {
    return ge::SUCCESS;
  }
  TemporaryRtContext with_context(rt_context_);
  LLM_CHK_ACL_RET(cache_access_table_updater_.UpdateTableBuffer(cache_id_to_entry_, cache_key_to_id_));
  return ge::SUCCESS;
}

LlmMemPool *CacheManager::GetNpuMemPool() const {
  return npu_mem_pool_;
}
}  // namespace llm
