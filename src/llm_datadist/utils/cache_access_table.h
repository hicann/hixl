/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_UTILS_CACHE_ACCESS_TABLE_H_
#define CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_UTILS_CACHE_ACCESS_TABLE_H_

#include "common/common.h"
#include "common/def_types.h"

namespace llm {
constexpr uint64_t kCacheAccessTableBufferSize = 1024U * 1024U;

class SharedDevBuffer {
 public:
  void *GetOrCreateBuffer(size_t size);
  void Unref();

 private:
  std::mutex mu_;
  void *buffer_ = nullptr;
  void *mem_register_handle_ = nullptr;
  int32_t ref_count_ = 0;
};

class CacheAccessTableUpdater {
 public:
  using TransferFunc = std::function<ge::Status(void *remote, void *local, size_t size, int32_t timeout)>;
  CacheAccessTableUpdater() = default;
  ~CacheAccessTableUpdater();

  ge::Status Initialize(bool enable);
  void Finalize();
  ge::Status UpdateTableBuffer(const std::map<int64_t, CacheEntry> &cache_id_to_entry,
                               std::map<std::pair<uint64_t, uint64_t>, int64_t> &cache_key_to_id);
  std::pair<void *, size_t> GetDevBufferAndSize() const;

 private:
  static ge::Status ToBuffer(uint64_t version_num,
                             const std::map<int64_t, CacheEntry> &cache_id_to_entry,
                             std::map<std::pair<uint64_t, uint64_t>, int64_t> &cache_key_to_id,
                             std::vector<uint8_t> &buffer);

  uint64_t version_num_ = 0UL;
  void *dev_buffer_ = nullptr;
  size_t buffer_size_ = 0U;
  std::map<int64_t, CacheEntry> cache_id_to_entry_;
  std::map<std::pair<uint64_t, uint64_t>, int64_t> cache_key_to_cache_id_;
};

class CacheAccessTable {
 public:
  using TransferFunc = std::function<ge::Status(void *remote, void *local, size_t size, int32_t timeout)>;
  CacheAccessTable() = default;
  ~CacheAccessTable();

  ge::Status Initialize(bool remote_cache_accessible);
  ge::Status FindCacheEntry(const TransferCacheReq &request, CacheEntry &cache_entry);
  ge::Status CheckRemoteFlag(bool expected_flag) const;
  std::pair<void *, size_t> GetDevBufferAndSize() const;
  void SetTransferFunc(const TransferFunc &transfer_func, void *remote_dev_buffer);

 private:
  ge::Status SyncFromRemote(int32_t timeout);
  ge::Status LoadFromBuffer(const uint8_t *buffer, size_t buffer_size);

  static SharedDevBuffer shared_dev_buffer_;
  static std::mutex shared_mu_;

  uint64_t version_num_ = 0UL;
  void *remote_dev_buffer_ = nullptr;
  void *dev_buffer_ = nullptr;
  size_t buffer_size_ = 0UL;
  TransferFunc transfer_func_{};
  std::map<int64_t, CacheEntry> cache_id_to_entry_;
  std::map<std::pair<uint64_t, uint64_t>, int64_t> cache_key_to_cache_id_;
};
}  // namespace llm

#endif  // CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_UTILS_CACHE_ACCESS_TABLE_H_
