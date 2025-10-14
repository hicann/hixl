/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_ENTITY_MANAGER_H_
#define CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_ENTITY_MANAGER_H_

#include <vector>
#include <thread>
#include "common/llm_inner_types.h"
#include "comm_entity.h"
#include "cache_mgr/comm_mem_manager.h"
#include "common/aligned_ptr.h"

namespace llm {
using EntityPtr = std::shared_ptr<CommEntity>;
constexpr uint64_t kMaxEntitySize = 512U;

struct CommEntityParams {
  uint64_t comm_id;
  uint64_t peer_cluster_id;
  uint32_t peer_rank_id;
  uint64_t local_cluster_id;
  uint32_t local_rank_id;
  bool remote_cache_accessible;
};

class CommEntityManager {
 public:
  CommEntityManager() = default;
  ~CommEntityManager() = default;
  ge::Status Initialize(bool start_service = true);
  void Finalize();
  ge::Status CreateEntity(const CommEntityParams &entity_params, EntityPtr &entity_ptr);
  ge::Status CreateEntity(const CommEntityParams &entity_params,
                          const EntityCommInfo::CommParams &comm_params,
                          EntityPtr &entity_ptr);
  ge::Status DestroyEntity(uint64_t peer_cluster_id);
  EntityPtr GetEntityByRemoteClusterId(const uint64_t remote_cluster_id);
  std::vector<EntityPtr> QueryEntityByCommId(uint64_t comm_id);
  void Dump();
  void DeleteEntities();
  void SetCommMemManager(CommMemManager *comm_mem_manager_);
  size_t GetEntitySize();
  ge::Status RemapRegisteredMemory(const std::vector<LLMMemInfo> &mem_infos);
  RegBufferPool *GetHostRegPool();
  RegBufferPool *GetDeviceRegPool();

 private:
  void HandleCacheRequest();
  void HandleAllEntities();
  std::atomic_bool running_{true};
  bool need_handle_request_ = false;
  std::thread cache_engine_thread_;
  std::unique_ptr<LlmMemPool> host_mem_pool_{};
  std::shared_ptr<AlignedPtr> host_buffer_;
  rtContext_t rt_context_{};
  std::atomic_uint64_t entity_id_gen_{1LU};
  CommMemManager *comm_mem_manager_{};
  std::atomic_bool mgr_high_priority_flag_{false};
  std::mutex mutex_;
  std::unordered_map<uint64_t, EntityPtr> entity_map_{};
  std::map<uint64_t, uint64_t> cluster_id_to_entity_id_{};
  bool start_service_{false};
  RegBufferPool host_reg_pool_{kMaxEntitySize, true};
  RegBufferPool device_reg_pool_{kMaxEntitySize, false};
};
}  // namespace llm
#endif  // CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_ENTITY_MANAGER_H_
