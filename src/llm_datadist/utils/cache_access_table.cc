/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "utils/cache_access_table.h"
#include "runtime/rt.h"
#include "common/llm_checker.h"
#include "hccl/hccl_adapter.h"
#include "cache_mgr/comm_mem_manager.h"
#include "common/llm_scope_guard.h"

namespace llm {
SharedDevBuffer CacheAccessTable::shared_dev_buffer_;
std::mutex CacheAccessTable::shared_mu_;
namespace {
constexpr int32_t kDefaultTimeout = 5000;
void NoDelete(void *) {}

struct CacheIndex {
  int64_t cache_id;
  uint64_t req_id;
  uint64_t model_id;
};

struct CacheSummary {
  int64_t cache_id;
  uint64_t num_blocks;
  uint64_t batch_size;
  uint64_t tensor_size;
  uint64_t stride; // batch stride or block size
  uint64_t placement;
  uint64_t num_tensors;
  bool remote_accessible;
  CacheMemType cache_mem_type;
  uint64_t tensor_addresses[0];
};

struct CacheTableHeader {
  uint64_t version_num;
  uint64_t num_caches;
  uint64_t num_cache_indices;
};

void FillCacheSummary(int64_t cache_id, const CacheEntry &cache_entry, CacheSummary &cache_summary) {
  cache_summary.cache_id = cache_id;
  cache_summary.num_blocks = cache_entry.num_blocks;
  cache_summary.batch_size = cache_entry.batch_size;
  cache_summary.tensor_size = cache_entry.tensor_size;
  cache_summary.stride = cache_entry.stride;
  cache_summary.placement = static_cast<uint64_t>(cache_entry.placement);
  cache_summary.num_tensors = cache_entry.cache_addrs.size();
  cache_summary.remote_accessible = cache_entry.remote_accessible;
  cache_summary.cache_mem_type = cache_entry.cache_mem_type;
  for (size_t i = 0U; i < cache_entry.cache_addrs.size(); ++i) {
    cache_summary.tensor_addresses[i] = PtrToValue(cache_entry.cache_addrs[i].get());
  }
}

CacheEntry ToCacheEntry(const CacheSummary &cache_summary) {
  CacheEntry cache_entry{};
  cache_entry.num_blocks = cache_summary.num_blocks;
  cache_entry.batch_size = static_cast<uint32_t>(cache_summary.batch_size);
  cache_entry.tensor_size = cache_summary.tensor_size;
  cache_entry.stride = cache_summary.stride;
  cache_entry.placement = static_cast<CachePlacement>(cache_summary.placement);
  cache_entry.remote_accessible = cache_summary.remote_accessible;
  cache_entry.cache_mem_type = cache_summary.cache_mem_type;
  for (size_t tensor_index = 0U; tensor_index < cache_summary.num_tensors; ++tensor_index) {
    auto tensor_addr = cache_summary.tensor_addresses[tensor_index];
    cache_entry.cache_addrs.emplace_back(std::shared_ptr<void>(ValueToPtr(tensor_addr), &NoDelete));
  }
  return cache_entry;
}
}  // namespace

void *SharedDevBuffer::GetOrCreateBuffer(size_t size) {
  std::lock_guard<std::mutex> lk(mu_);
  if (buffer_ == nullptr) {
    LLM_ASSERT_RT_OK(rtMalloc(&buffer_, size, RT_MEMORY_HBM | RT_MEMORY_POLICY_HUGE_PAGE_ONLY, LLM_MODULE_NAME_U16));
    LLM_DISMISSABLE_GUARD(fail_guard, ([this]() { LLM_CHK_ACL(rtFree(buffer_)); }));
    auto ret = GlobalMemManager::GetInstance().RegisterMem(buffer_, size, HcclMemType::HCCL_MEM_TYPE_DEVICE,
                                                           mem_register_handle_);
    if (ret != ge::SUCCESS) {
      LLMLOGE(ge::FAILED, "Failed to register global mem, ret = %u", ret);
      return nullptr;
    }
    LLM_DISMISS_GUARD(fail_guard);
  }
  ref_count_ += 1;
  return buffer_;
}

void SharedDevBuffer::Unref() {
  std::lock_guard<std::mutex> lk(mu_);
  if (ref_count_ > 0) {
    if (--ref_count_ == 0) {
      LLM_CHK_ACL(rtFree(buffer_));
      if (mem_register_handle_ != nullptr) {
        GlobalMemManager::GetInstance().UnregisterMem(mem_register_handle_);
        mem_register_handle_ = nullptr;
      }
      buffer_ = nullptr;
    }
  }
}

CacheAccessTableUpdater::~CacheAccessTableUpdater() {
  Finalize();
}

ge::Status CacheAccessTableUpdater::Initialize(bool enable) {
  LLM_CHK_BOOL_RET_SPECIAL_STATUS(dev_buffer_ != nullptr, ge::SUCCESS, "Already initialized");
  if (enable) {
    buffer_size_ = kCacheAccessTableBufferSize;
    LLM_CHK_ACL_RET(rtMalloc(&dev_buffer_, buffer_size_, RT_MEMORY_HBM | RT_MEMORY_POLICY_HUGE_PAGE_ONLY,
                           LLM_MODULE_NAME_U16));
  } else {
    buffer_size_ = sizeof(CacheTableHeader);
    LLM_CHK_ACL_RET(rtMalloc(&dev_buffer_, buffer_size_, RT_MEMORY_HBM | RT_MEMORY_POLICY_HUGE_PAGE_ONLY,
                           LLM_MODULE_NAME_U16));
    CacheTableHeader header{};
    header.version_num = UINT64_MAX;
    LLM_CHK_ACL_RET(rtMemcpy(dev_buffer_,
                           buffer_size_,
                           &header,
                           sizeof(header),
                           RT_MEMCPY_HOST_TO_DEVICE));
  }
  return ge::SUCCESS;
}

void CacheAccessTableUpdater::Finalize() {
  if (dev_buffer_ != nullptr) {
    LLM_CHK_ACL(rtFree(dev_buffer_));
    dev_buffer_ = nullptr;
  }
}

ge::Status CacheAccessTableUpdater::UpdateTableBuffer(const std::map<int64_t, CacheEntry> &cache_id_to_entry,
                                                      std::map<std::pair<uint64_t, uint64_t>,
                                                          int64_t> &cache_key_to_id) {
  uint64_t version_num = ++version_num_;  // start from 1
  LLM_CHK_BOOL_RET_STATUS(version_num != UINT64_MAX, ge::FAILED, "version_num reached UINT64_MAX");
  std::vector<uint8_t> buffer;
  LLMLOGI("Update cache access table start, version_num = %lu, num_caches = %zu, num_cache_indices = %zu",
         version_num, cache_id_to_entry.size(), cache_key_to_id.size());
  LLM_CHK_STATUS_RET(ToBuffer(version_num, cache_id_to_entry, cache_key_to_id, buffer),
                    "Failed to generate cache access table, "
                    "version_num = %lu, num_caches = %zu, num_cache_indices = %zu",
                    version_num, cache_id_to_entry.size(), cache_key_to_id.size());
  LLMLOGI("Generate cache access table success");
  LLM_CHK_ACL_RET(rtMemcpy(dev_buffer_,
                         buffer_size_,
                         buffer.data(),
                         buffer.size(),
                         RT_MEMCPY_HOST_TO_DEVICE));
  LLMLOGI("Write to device memory success");
  return ge::SUCCESS;
}

ge::Status CacheAccessTableUpdater::ToBuffer(uint64_t version_num,
                                             const std::map<int64_t, CacheEntry> &cache_id_to_entry,
                                             std::map<std::pair<uint64_t, uint64_t>, int64_t> &cache_key_to_id,
                                             std::vector<uint8_t> &buffer) {
  size_t total_size = sizeof(CacheTableHeader);
  total_size += sizeof(CacheIndex) * cache_key_to_id.size();
  std::unordered_map<int64_t, size_t> cache_id_to_summary_size;
  for (const auto &cache_id_and_entry : cache_id_to_entry) {
    auto size = sizeof(CacheSummary) + sizeof(uint64_t) * cache_id_and_entry.second.cache_addrs.size();
    (void) cache_id_to_summary_size.emplace(cache_id_and_entry.first, size);
    total_size += size;
  }
  LLM_CHK_BOOL_RET_STATUS(total_size <= kCacheAccessTableBufferSize,
                         ge::LLM_PARAM_INVALID,
                         "Serialize cache access table failed, sized needed (%zu) exceeds 1MB", total_size);
  buffer.resize(total_size);
  auto &header = *PtrToPtr<uint8_t, CacheTableHeader>(buffer.data());
  header.version_num = version_num;
  header.num_caches = cache_id_to_entry.size();
  header.num_cache_indices = cache_key_to_id.size();
  auto buffer_offset = sizeof(CacheTableHeader);  // to first cache summary
  for (const auto &cache_id_and_entry : cache_id_to_entry) {
    auto cache_summary_buffer = buffer.data() + buffer_offset;
    auto &cache_summary = *PtrToPtr<uint8_t, CacheSummary>(cache_summary_buffer);
    FillCacheSummary(cache_id_and_entry.first, cache_id_and_entry.second, cache_summary);
    buffer_offset += cache_id_to_summary_size[cache_id_and_entry.first];  // move to the next cache summary
    LLMLOGI("Serialize cache success, cache_id = %lu, num_blocks = %lu, batch_size = %u, "
           "tensor_size = %lu, stride = %lu, placement = %lu, num_tensors = %zu",
           cache_summary.cache_id, cache_summary.num_blocks, cache_summary.batch_size,
           cache_summary.tensor_size, cache_summary.stride, cache_summary.placement, cache_summary.num_tensors);
  }
  auto *cache_index = PtrToPtr<uint8_t, CacheIndex>(buffer.data() + buffer_offset);
  for (const auto &cache_key_and_cache_id : cache_key_to_id) {
    *cache_index = CacheIndex{cache_key_and_cache_id.second, cache_key_and_cache_id.first.first,
                              cache_key_and_cache_id.first.second};
    LLMLOGI("CacheIndex added, cache_id = %ld, cache_key = (%lu, %lu)",
           cache_index->cache_id,
           cache_index->req_id,
           cache_index->model_id);
    ++cache_index;
  }
  return ge::SUCCESS;
}

std::pair<void *, size_t> CacheAccessTableUpdater::GetDevBufferAndSize() const {
  return std::make_pair(dev_buffer_, buffer_size_);
}

CacheAccessTable::~CacheAccessTable() {
  if (dev_buffer_ != nullptr) {
    shared_dev_buffer_.Unref();
    dev_buffer_ = nullptr;
  }
}

ge::Status CacheAccessTable::Initialize(bool remote_cache_accessible) {
  LLM_CHK_BOOL_RET_SPECIAL_STATUS(dev_buffer_ != nullptr, ge::SUCCESS, "Already initialized");
  buffer_size_ = remote_cache_accessible ? kCacheAccessTableBufferSize : sizeof(CacheTableHeader);
  dev_buffer_ = shared_dev_buffer_.GetOrCreateBuffer(buffer_size_);
  LLM_CHECK_NOTNULL(dev_buffer_);
  return ge::SUCCESS;
}

ge::Status CacheAccessTable::LoadFromBuffer(const uint8_t *buffer, size_t buffer_size) {
  auto &header = *PtrToPtr<uint8_t, CacheTableHeader>(buffer);
  LLMLOGI("version_num = %lu, num_caches = %lu, num_cache_indices = %lu",
         header.version_num, header.num_caches, header.num_cache_indices);
  auto cache_summary_offset = sizeof(CacheTableHeader);
  for (uint64_t i = 0U; i < header.num_caches; ++i) {
    auto cache_summary_size = sizeof(CacheSummary);
    LLM_CHK_BOOL_RET_STATUS(cache_summary_offset + cache_summary_size <= buffer_size,
                           ge::LLM_PARAM_INVALID,
                           "Parse cache[%lu] failed, version_num = %lu, num_caches = %lu",
                           i, header.version_num, header.num_caches);
    auto cache_summary_buffer = buffer + cache_summary_offset;
    const CacheSummary &cache_summary = *PtrToPtr<uint8_t, CacheSummary>(cache_summary_buffer);
    cache_summary_size += cache_summary.num_tensors * sizeof(uint64_t);
    LLM_CHK_BOOL_RET_STATUS(cache_summary_offset + cache_summary_size <= buffer_size,
                           ge::LLM_PARAM_INVALID,
                           "Parse cache[%lu] failed, num_tensors = %lu, version_num = %lu, num_caches = %lu",
                           i, cache_summary.num_tensors, header.version_num, header.num_caches);
    cache_summary_offset += cache_summary_size;
    CacheEntry cache_entry = ToCacheEntry(cache_summary);
    LLMLOGI("Cache entry loaded, cache_id = %lu, num_blocks = %lu, batch_size = %u, "
           "tensor_size = %lu, stride = %lu, placement = %lu, num_tensors = %zu",
           cache_summary.cache_id, cache_entry.num_blocks, cache_entry.batch_size,
           cache_entry.tensor_size, cache_entry.stride, cache_entry.placement, cache_entry.cache_addrs.size());
    LLM_CHK_BOOL_RET_STATUS(cache_id_to_entry_.emplace(cache_summary.cache_id, std::move(cache_entry)).second,
                           ge::LLM_PARAM_INVALID,
                           "duplicate cache_id: %ld", cache_summary.cache_id);
  }
  auto *cache_indices = PtrToPtr<uint8_t, CacheIndex>(buffer + cache_summary_offset);
  for (uint64_t i = 0U; i < header.num_cache_indices; ++i) {
    const auto &cache_index = cache_indices[i];
    LLM_CHK_BOOL_RET_STATUS(cache_id_to_entry_.find(cache_index.cache_id) != cache_id_to_entry_.cend(),
                           ge::FAILED,
                           "Cache access table is inconsistent, cache_id:%ld not found",
                           cache_index.cache_id);
    auto cache_key = std::make_pair(cache_index.req_id, cache_index.model_id);
    LLM_CHK_BOOL_RET_STATUS(cache_key_to_cache_id_.emplace(cache_key, cache_index.cache_id).second,
                           ge::LLM_PARAM_INVALID,
                           "cache_key: (%lu, %lu) already bound to cache_id: %ld",
                           cache_key.first, cache_key.second, cache_key_to_cache_id_.at(cache_key));
    LLMLOGI("CacheIndex added, cache_id = %lu, cache_key = (%lu, %lu)",
           cache_index.cache_id, cache_key.first, cache_key.second);
  }
  LLMLOGI("Load cache table success");
  version_num_ = header.version_num;
  return ge::SUCCESS;
}

ge::Status CacheAccessTable::FindCacheEntry(const TransferCacheReq &request,
                                            CacheEntry &cache_entry) {
  if (version_num_ == 0) {  // first pull
    LLM_CHK_STATUS_RET(SyncFromRemote(request.timeout_in_ms),
                      "Failed to sync remote cache access table, timeout = %d ms",
                      request.timeout_in_ms);
  }
  const auto cache_id = request.cache_id;
  if (cache_id > 0) {
    const auto it = cache_id_to_entry_.find(cache_id);
    LLM_CHK_BOOL_RET_STATUS(it != cache_id_to_entry_.cend(),
                           ge::LLM_KV_CACHE_NOT_EXIST,
                           "cache_id: %ld does not exist", cache_id);
    cache_entry = it->second;
    LLMLOGI("CacheEntry found by cache_id: %ld", cache_id);
    return ge::SUCCESS;
  }

  auto cache_key = std::make_pair(request.req_id, request.model_id);
  const auto it = cache_key_to_cache_id_.find(cache_key);
  LLM_CHK_BOOL_RET_STATUS(it != cache_key_to_cache_id_.cend(),
                         ge::LLM_KV_CACHE_NOT_EXIST,
                         "cache_key: (%lu, %lu) does not exist", cache_key.first, cache_key.second);
  LLMLOGI("CacheEntry found by cache_key: (%lu, %lu), cache_id = %lu", cache_key.first, cache_key.second, it->second);
  cache_entry = cache_id_to_entry_.at(it->second);
  return ge::SUCCESS;
}

ge::Status CacheAccessTable::SyncFromRemote(int32_t timeout) {
  LLMLOGI("Sync cache access start, timeout = %d ms", timeout);
  std::vector<uint8_t> buffer(kCacheAccessTableBufferSize);
  {
    std::lock_guard<std::mutex> lk(shared_mu_);
    LLM_CHK_STATUS_RET(transfer_func_(remote_dev_buffer_, dev_buffer_, kCacheAccessTableBufferSize, timeout));
    LLMLOGI("Sync cache access table data success");
    LLM_CHK_ACL_RET(rtMemcpy(buffer.data(),
                           buffer.size(),
                           dev_buffer_,
                           kCacheAccessTableBufferSize,
                           RT_MEMCPY_DEVICE_TO_HOST));
  }
  LLM_CHK_STATUS_RET(LoadFromBuffer(buffer.data(), buffer.size()));
  return ge::SUCCESS;
}

void CacheAccessTable::SetTransferFunc(const CacheAccessTable::TransferFunc &transfer_func, void *remote_dev_buffer) {
  transfer_func_ = transfer_func;
  remote_dev_buffer_ = remote_dev_buffer;
}

std::pair<void *, size_t> CacheAccessTable::GetDevBufferAndSize() const {
  return std::make_pair(dev_buffer_, buffer_size_);
}

ge::Status CacheAccessTable::CheckRemoteFlag(bool expected_flag) const {
  std::vector<uint8_t> buffer(sizeof(CacheTableHeader));
  {
    std::lock_guard<std::mutex> lk(shared_mu_);
    LLM_CHK_STATUS_RET(transfer_func_(remote_dev_buffer_, dev_buffer_, buffer.size(), kDefaultTimeout));
    LLMLOGI("Sync cache access table data success");
    LLM_CHK_ACL_RET(rtMemcpy(buffer.data(),
                           buffer.size(),
                           dev_buffer_,
                           buffer.size(),
                           RT_MEMCPY_DEVICE_TO_HOST));
  }
  auto &header = *PtrToPtr<uint8_t, CacheTableHeader>(buffer.data());
  LLMLOGI("Get remote version success, version_num = %lu", header.version_num);
  bool remote_flag_enabled = (header.version_num != UINT64_MAX);
  LLM_CHK_BOOL_RET_STATUS(expected_flag == remote_flag_enabled,
                         ge::LLM_PARAM_INVALID,
                         "Check failed, RemoteCacheAccessible is not identical, local = %d, remote = %d",
                         static_cast<int32_t>(expected_flag), static_cast<int32_t>(remote_flag_enabled));
  LLMLOGI("Check success, RemoteCacheAccessible is identical, value = %d", static_cast<int32_t>(remote_flag_enabled));
  return ge::SUCCESS;
}
}  // namespace llm