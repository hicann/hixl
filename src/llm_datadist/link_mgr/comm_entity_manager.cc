/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "comm_entity_manager.h"
#include "common/def_types.h"
#include "llm_datadist/llm_error_codes.h"
#include "common/llm_checker.h"
#include "common/mem_utils.h"
#include "data_transfer/data_transfer_job.h"
#include "fsm/state_manager.h"
#include "common/llm_scope_guard.h"

namespace llm {
namespace {
constexpr size_t kHostBufferSize = 1UL * 1024 * 1024 * 1024;
constexpr size_t kAlignment = 4096U;
}  // namespace

ge::Status CommEntityManager::CreateEntity(const CommEntityParams &entity_params,
                                           const EntityCommInfo::CommParams &comm_params,
                                           EntityPtr &entity_ptr) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto &it = cluster_id_to_entity_id_.find(entity_params.peer_cluster_id);
    LLM_CHK_BOOL_RET_STATUS(it == cluster_id_to_entity_id_.cend(), ge::LLM_ALREADY_LINK,
                           "Link already exists, peer cluster id:%lu", entity_params.peer_cluster_id);
  }
  EntityPtr entity = MakeShared<CommEntity>(entity_params.comm_id, entity_params.peer_cluster_id,
                                            entity_params.peer_rank_id, entity_params.local_cluster_id,
                                            entity_params.local_rank_id);
  LLM_CHECK_NOTNULL(entity);
  entity->SetHostMemPool(host_mem_pool_.get());
  LLM_CHK_STATUS_RET(entity->Initialize(entity_params.remote_cache_accessible, comm_params),
                    "Failed to init entity");
  auto entity_id = entity_id_gen_.fetch_add(1UL, std::memory_order::memory_order_relaxed);
  mgr_high_priority_flag_.store(true);
  std::lock_guard<std::mutex> lock(mutex_);
  (void)entity_map_.emplace(entity_id, entity);
  cluster_id_to_entity_id_[entity_params.peer_cluster_id] = entity_id;
  mgr_high_priority_flag_.store(false);
  entity_ptr = entity;
  LLMLOGI("Create entity success, peer cluster id:%lu, local cluster id:%lu",
         entity_params.peer_cluster_id, entity_params.local_cluster_id);
  return ge::SUCCESS;
}

ge::Status CommEntityManager::CreateEntity(const CommEntityParams &entity_params, EntityPtr &entity_ptr) {
  EntityPtr entity = MakeShared<CommEntity>(entity_params.comm_id, entity_params.peer_cluster_id,
                                            entity_params.peer_rank_id, entity_params.local_cluster_id,
                                            entity_params.local_rank_id);
  LLM_CHECK_NOTNULL(entity);
  entity->SetHostMemPool(host_mem_pool_.get());
  LLM_CHK_STATUS_RET(entity->Initialize(entity_params.remote_cache_accessible),
                    "Failed to init entity");
  auto entity_id = entity_id_gen_.fetch_add(1UL, std::memory_order::memory_order_relaxed);
  mgr_high_priority_flag_.store(true);
  std::lock_guard<std::mutex> lock(mutex_);
  (void)entity_map_.emplace(entity_id, entity);
  cluster_id_to_entity_id_[entity_params.peer_cluster_id] = entity_id;
  mgr_high_priority_flag_.store(false);
  entity_ptr = entity;
  LLMLOGI("Create entity success, peer cluster id:%lu, local cluster id:%lu",
         entity_params.peer_cluster_id, entity_params.local_cluster_id);
  return ge::SUCCESS;
}

ge::Status CommEntityManager::DestroyEntity(uint64_t peer_cluster_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto ret = ge::SUCCESS;
  const auto &it = cluster_id_to_entity_id_.find(peer_cluster_id);
  if (it != cluster_id_to_entity_id_.cend()) {
    auto entity_id = it->second;
    auto entity = entity_map_[entity_id];
    std::lock_guard<std::mutex> pull_lock(entity->GetPullMutex());
    auto entity_ret = entity->Finalize();
    ret = entity_ret != ge::SUCCESS ? entity_ret : ret;
    if (start_service_) {
      std::lock_guard<std::mutex> process_lock(entity->GetProcessMutex());
      entity->MarkEntityDestroyed();
    } else {
      cluster_id_to_entity_id_.erase(it);
      entity_map_.erase(entity_id);
    }
    LLMLOGI("Destroy entity end, peer cluster id = %lu", peer_cluster_id);
  }
  return ret;
}

EntityPtr CommEntityManager::GetEntityByRemoteClusterId(const uint64_t remote_cluster_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto &it = cluster_id_to_entity_id_.find(remote_cluster_id);
  if (it == cluster_id_to_entity_id_.cend()) {
    return nullptr;
  }
  auto entity_id = it->second;
  auto entity = entity_map_[entity_id];
  // destroy or memory not prepared
  if (entity->GetCurState() == FsmState::FSM_DESTROYED_STATE ||
      entity->GetCurState() == FsmState::FSM_INIT_STATE) {
    return nullptr;
  }
  return entity;
}

std::vector<EntityPtr> CommEntityManager::QueryEntityByCommId(uint64_t comm_id) {
  std::vector<EntityPtr> entities;
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto &entity_pair : entity_map_) {
    if (entity_pair.second->GetCommId() == comm_id) {
      entities.emplace_back(entity_pair.second);
    }
  }
  return entities;
}

void CommEntityManager::DeleteEntities() {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto it = entity_map_.begin(); it != entity_map_.end();) {
    auto entity = it->second;
    if (entity->GetCurState() == FsmState::FSM_DESTROYED_STATE) {
      cluster_id_to_entity_id_.erase(entity->GetClusterId());
      it = entity_map_.erase(it);
      continue;
    }
    it++;
  }
}

size_t CommEntityManager::GetEntitySize() {
  std::lock_guard<std::mutex> lock(mutex_);
  size_t num = 0U;
  for (auto &entity_pair : entity_map_) {
    if (entity_pair.second->GetCurState() != FsmState::FSM_DESTROYED_STATE) {
      num++;
    }
  }
  return num;
}

void CommEntityManager::HandleAllEntities() {
  if (mgr_high_priority_flag_.load()) {
    return;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto it = entity_map_.begin(); it != entity_map_.end();) {
    auto entity = it->second;
    if ((entity->GetCurState() == FsmState::FSM_INIT_STATE) ||
        (entity->GetCurState() == FsmState::FSM_ERROR_STATE)) {
      it++;
      continue;
    }
    if (entity->GetProcessMutex().try_lock()) {
      std::lock_guard<std::mutex> process_lock(entity->GetProcessMutex(), std::adopt_lock);
      if (entity->GetCurState() == FsmState::FSM_DESTROYED_STATE) {
        cluster_id_to_entity_id_.erase(entity->GetClusterId());
        it = entity_map_.erase(it);
        continue;
      }
      LLM_CHK_BOOL_EXEC(entity->ProcessState() == ge::SUCCESS, entity->MarkEntityError(),
                       "Failed to process state");
    }
    it++;
  }
}

void CommEntityManager::HandleCacheRequest() {
  (void) pthread_setname_np(pthread_self(), "ge_llm_fsm");
  LLM_CHK_ACL(rtCtxSetCurrent(rt_context_));
  while (running_) {
    HandleAllEntities();
  }
}

void CommEntityManager::SetCommMemManager(CommMemManager *comm_mem_manager) {
  comm_mem_manager_ = comm_mem_manager;
}

ge::Status CommEntityManager::Initialize(bool start_service) {
  start_service_ = start_service;
  if (!start_service) {
    LLMLOGI("No need to start FSM thread");
    return ge::SUCCESS;
  }
  LLM_CHK_ACL_RET(rtCtxGetCurrent(&rt_context_));
  cache_engine_thread_ = std::thread(&CommEntityManager::HandleCacheRequest, this);
  ScalableConfig config{};
  config.page_mem_size_total_threshold = kHostBufferSize;
  host_mem_pool_ = MakeUnique<LlmMemPool>(config);
  LLM_CHECK_NOTNULL(host_mem_pool_);
  host_buffer_ = MakeShared<AlignedPtr>(kHostBufferSize, kAlignment);
  LLM_CHECK_NOTNULL(host_buffer_);
  LLM_CHK_STATUS_RET(host_mem_pool_->Initialize(host_buffer_->MutableGet(), kHostBufferSize),
                    "Failed to initialize host memory pool");
  LLM_CHECK_NOTNULL(comm_mem_manager_);
  LLM_CHK_STATUS_RET(comm_mem_manager_->RegisterCommMemAddr(host_buffer_->MutableGet(),
                                                           kHostBufferSize, HCCL_MEM_TYPE_HOST));
  LLM_CHK_STATUS_RET(host_reg_pool_.Initialize(), "Failed to init host reg buffer pool");
  LLM_CHK_STATUS_RET(device_reg_pool_.Initialize(), "Failed to init device reg buffer pool");
  return ge::SUCCESS;
}

void CommEntityManager::Finalize() {
  LLMLOGI("CommEntityManager finalize start");
  running_ = false;
  if (cache_engine_thread_.joinable()) {
    cache_engine_thread_.join();
  }
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto &entity_pair : entity_map_) {
    entity_pair.second->Finalize();
  }
  entity_map_.clear();
  cluster_id_to_entity_id_.clear();
  host_reg_pool_.Finalize();
  device_reg_pool_.Finalize();
}

void CommEntityManager::Dump() {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto &entity_pair : entity_map_) {
    entity_pair.second->Dump();
  }
}

ge::Status CommEntityManager::RemapRegisteredMemory(const std::vector<LLMMemInfo> &mem_infos) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<HcclComm> comms;
  for (const auto &it : entity_map_) {
    if (it.second->GetCurState() != FsmState::FSM_DESTROYED_STATE) {
      comms.emplace_back(it.second->GetComm());
    }
  }
  LLM_CHK_BOOL_RET_STATUS(!comms.empty(), ge::LLM_NOT_YET_LINK, "No comm link yet");
  std::vector<HcclMem> hccl_mems;
  for (const auto &mem_info : mem_infos) {
    HcclMem hccl_mem = {};
    hccl_mem.type = static_cast<HcclMemType>(mem_info.mem_type);
    hccl_mem.addr = ValueToPtr(mem_info.addr);
    hccl_mem.size = mem_info.size;
    hccl_mems.emplace_back(hccl_mem);
  }
  const auto start = std::chrono::steady_clock::now();
  auto ret = HcclAdapter::GetInstance().HcclRemapRegisteredMemory(&comms[0], &hccl_mems[0],
      comms.size(), hccl_mems.size());
  LLM_CHK_BOOL_RET_STATUS(ret == HCCL_SUCCESS, ge::FAILED,
                         "Failed to invoke HcclRemapRegisteredMemory, ret = %d",
                         static_cast<int32_t>(ret));
  const auto end = std::chrono::steady_clock::now();
  const auto cost = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
  LLMLOGI("RemapRegisteredMemory success, mem info size = %zu, cost = %ld us.", mem_infos.size(), cost);
  return ge::SUCCESS;
}

RegBufferPool *CommEntityManager::GetHostRegPool() {
  return &host_reg_pool_;
}

RegBufferPool *CommEntityManager::GetDeviceRegPool() {
  return &device_reg_pool_;
}
}  // namespace llm
