/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "comm_mem_manager.h"
#include "common/def_types.h"
#include "hccl/hccl_adapter.h"

namespace llm {
GlobalMemManager &GlobalMemManager::GetInstance() {
  static GlobalMemManager instance;
  return instance;
}

ge::Status GlobalMemManager::Initialize(TransferEngine *transfer_engine) {
  LLM_CHECK_NOTNULL(transfer_engine);
  transfer_engine_ = transfer_engine;
  return ge::SUCCESS;
}

ge::Status GlobalMemManager::RegisterMem(void *addr, uint64_t size, HcclMemType type, void *&handle) {
  LLM_CHK_STATUS_RET(transfer_engine_->RegisterMem(addr, size, type, handle),
                     "Failed to register mem");
  LLMLOGI("Register global mem success, addr:%p, size:%lu, type:%s, handle:%p",
         addr, size, HcclUtils::HcclMemTypeToString(type).c_str(), handle);

  std::lock_guard<std::mutex> lock(mutex_);
  handles_.emplace(handle);
  return ge::SUCCESS;
}

void GlobalMemManager::Finalize() {
  std::lock_guard<std::mutex> lock(mutex_);
  handles_.clear();
}

ge::Status GlobalMemManager::UnregisterMem(void *handle) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = handles_.find(handle);
  if (it == handles_.end()) {
    LLMLOGW("Failed to find register cache mem, handle = %p", handle);
    return ge::SUCCESS;
  }
  LLM_CHK_STATUS_RET(transfer_engine_->UnregisterMem(handle), "Failed to unregister mem");
  handles_.erase(handle);
  LLMLOGI("Unregister global mem handle success, handle:%p", handle);
  return ge::SUCCESS;
 }

void CommMemManager::Finalize() {
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto &it : cache_id_to_mems_) {
    for (auto handle : it.second.mem_handles) {
      (void) transfer_engine_->UnregisterMem(handle);
    }
  }
  registered_cache_mem_.clear();
  cache_id_to_mems_.clear();
  return;
}

ge::Status CommMemManager::Initialize(TransferEngine *transfer_engine) {
  LLM_CHECK_NOTNULL(transfer_engine);
  transfer_engine_ = transfer_engine;
  return ge::SUCCESS;
}

ge::Status CommMemManager::RegisterCacheMem(int64_t cache_id,
                                            const CacheDesc &cache_desc,
                                            const std::vector<uintptr_t> &addrs,
                                            int64_t tensor_size) {
  if (!cache_desc.remote_accessible) {
    LLMLOGI("No need to register mem, remote_accessible is false");
    return ge::SUCCESS;
  }

  RegisterMems mems{};
  for (const auto &addr : addrs) {
    LLM_CHK_BOOL_RET_STATUS(addr != 0U, ge::LLM_PARAM_INVALID, "The addr of cache can not be zero.");
    void *mem_ptr = ValueToPtr(addr);
    HcclMemType mem_type = (cache_desc.placement == static_cast<uint32_t>(CachePlacement::DEVICE))
                            ? HcclMemType::HCCL_MEM_TYPE_DEVICE : HcclMemType::HCCL_MEM_TYPE_HOST;
    auto key = std::make_pair(mem_ptr, tensor_size);
    const auto &it = registered_cache_mem_.find(key);
    if (it != registered_cache_mem_.cend()) {
      continue;
    }
    void *mem_handle = nullptr;
    LLM_CHK_STATUS_RET(transfer_engine_->RegisterMem(mem_ptr, tensor_size, mem_type, mem_handle),
                       "Failed to register mem");
    mems.mem_handles.emplace_back(mem_handle);
    mems.mem_addrs.emplace_back(key);
    registered_cache_mem_.emplace(key);
    LLMLOGI("Register global mem[%p] success, size:%ld, placement:%u, handle:%p, cache_id:%ld",
           mem_ptr, tensor_size, cache_desc.placement, mem_handle, cache_id);
  }
  std::lock_guard<std::mutex> lock(mutex_);
  cache_id_to_mems_[cache_id] = std::move(mems);
  return ge::SUCCESS;
}

ge::Status CommMemManager::UnregisterCacheMem(int64_t cache_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = cache_id_to_mems_.find(cache_id);
  if (it == cache_id_to_mems_.end()) {
    LLMLOGW("Failed to find register cache mem, cache_id = %ld", cache_id);
    return ge::SUCCESS;
  }

  for (auto handle : it->second.mem_handles) {
    LLM_CHK_STATUS_RET(transfer_engine_->UnregisterMem(handle), "Failed to unregister mem");
    LLMLOGI("Unregister global mem handle success, handle:%p, cache_id:%ld",
           handle, cache_id);
  }

  for (const auto &key : it->second.mem_addrs) {
    registered_cache_mem_.erase(key);
  }
  cache_id_to_mems_.erase(it);
  LLMLOGI("Unregister cache:[%ld] addrs end", cache_id);
  return ge::SUCCESS;
}
}  // namespace llm
