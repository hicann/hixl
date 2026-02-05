/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_COMM_MEM_MANAGER_H_
#define CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_COMM_MEM_MANAGER_H_

#include <vector>
#include <mutex>
#include <map>
#include <set>
#include "common/llm_inner_types.h"
#include "hccl/hccl_adapter.h"
#include "transfer_engine/transfer_engine.h"

namespace llm {
class GlobalMemManager {
 public:
  static GlobalMemManager &GetInstance();
  ~GlobalMemManager() = default;
  GlobalMemManager(const GlobalMemManager &) = delete;
  GlobalMemManager(const GlobalMemManager &&) = delete;
  GlobalMemManager &operator=(const GlobalMemManager &) = delete;
  GlobalMemManager &operator=(const GlobalMemManager &&) = delete;

  ge::Status Initialize(TransferEngine *transfer_engine);
  void Finalize();
  ge::Status RegisterMem(void *addr, uint64_t size, HcclMemType type, void *&handle);
  ge::Status UnregisterMem(void *handle);

 private:
  GlobalMemManager() = default;

  std::mutex mutex_;
  std::set<void *> handles_;
  TransferEngine *transfer_engine_ = nullptr;
};

class CommMemManager {
 public:
  struct RegisterMems {
    std::vector<std::pair<void *, int64_t>> mem_addrs;
    std::vector<void *> mem_handles;
  };

  CommMemManager() = default;
  ~CommMemManager() = default;
  ge::Status Initialize(TransferEngine *transfer_engine);
  void Finalize();
  ge::Status RegisterCacheMem(int64_t cache_id, const CacheDesc &cache_desc, const std::vector<uintptr_t> &addrs,
                              int64_t tensor_size);
  ge::Status UnregisterCacheMem(int64_t cache_id);

 private:
  std::mutex mutex_;
  std::map<int64_t, RegisterMems> cache_id_to_mems_;
  std::set<std::pair<void *, int64_t>> registered_cache_mem_;
  TransferEngine *transfer_engine_ = nullptr;
};
}  // namespace llm
#endif  // CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_COMM_MEM_MANAGER_H_
