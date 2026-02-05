/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_LINK_MANAGER_H_
#define CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_LINK_MANAGER_H_

#include <vector>
#include <thread>
#include "llm_datadist/llm_error_codes.h"
#include "common/llm_inner_types.h"
#include "common/llm_thread_pool.h"
#include "comm_entity_manager.h"
#include "cache_mgr/cache_manager.h"

namespace llm {
constexpr const char_t *kLinkThreadNamePrefix = "ge_llm_link";
constexpr size_t kLinkThreadNum = 16U;

struct PrepareMemArg {
  uint64_t comm_id;
  HcclComm comm;
  std::map<uint64_t, uint32_t> cluster2rank;
  std::vector<void *> mem_handles;
  int32_t link_total_time = 0;
  int32_t link_retry_count = 1;
};
struct CommStatus {
  EntityCommInfoPtr comm_ptr;
  std::atomic_bool unlink_flag;
  std::atomic_bool prepare_mem_flag;
  RegisterMemoryStatus status;
  std::future<ge::Status> task_fut;
};

struct ExchangeMemInfo {
  uint64_t cache_table_addr;
  uint64_t cache_table_size;
  uint64_t req_addr;
  uint64_t req_size;
  uint64_t resp_addr;
  uint64_t resp_size;
};

class CommLinkManager {
 public:
  explicit CommLinkManager(uint64_t cluster_id, bool remote_cache_accessible = false) :
      cluster_id_(cluster_id), remote_cache_accessible_(remote_cache_accessible) {};

  virtual ~CommLinkManager() = default;

  virtual ge::Status Initialize(const std::map<ge::AscendString, ge::AscendString> &options);

  ge::Status Link(std::string &cluster_name, const std::map<uint64_t, uint32_t> &cluster2rank, std::string &rank_table,
                  uint64_t &comm_id);

  ge::Status Unlink(uint64_t comm_id);

  ge::Status QueryRegisterMemStatus(uint64_t comm_id, RegisterMemoryStatus &status);

  virtual void Finalize();

  void SetCommEntityManager(CommEntityManager *comm_entity_manager);

  void SetCacheManager(CacheManager *cache_manager);

  static ge::Status PrepareMemTask(CommLinkManager *link_manager, PrepareMemArg request);

  ge::Status RegisterMem(HcclMem *mem, void **mem_handle);
  ge::Status DeregisterGlobalMem(void *mem_handle);
  std::vector<void *> GetAllRegisterMemHandles();

 protected:
  std::mutex handles_mutex_;
  std::set<void *> handles_;

 private:
  void FreeFlagGuard(PrepareMemArg &req);
  void FlagGuard(PrepareMemArg &req);
  void CheckUnlink(PrepareMemArg &req, bool &check_unlink_flag);
  ge::Status PrepareMem(PrepareMemArg &req);
  ge::Status ExchangeMem(const EntityPtr &entity, uint32_t local_rank, uint32_t remote_rank) const;
  static void SetMemAttribute(const ExchangeMemInfo &remote_mem_info, std::vector<HcclMem> &remote_mems, HcclMem &mem);
  ge::Status DestroyRes(EntityCommInfoPtr comm_ptr, std::vector<EntityPtr> &comm_entities) const;
  ge::Status PrepareComm(const PrepareMemArg &req, EntityCommInfoPtr &comm_info_ptr);
  uint64_t GenerateCommId();
  ge::Status CreateClustersEntity(PrepareMemArg &req, std::map<uint64_t, EntityPtr> &cluster2entity);

  std::atomic_bool running_{true};
  LLMThreadPool thread_pool_{kLinkThreadNamePrefix, kLinkThreadNum};
  std::atomic_uint64_t comm_id_gen_{1UL};
  uint64_t cluster_id_;
  int32_t link_total_time_ = 0;
  int32_t link_retry_count_ = 1;
  CommEntityManager *comm_entity_manager_{};
  CacheManager *cache_manager_{};
  std::map<uint64_t, CommStatus> comm_to_status_{};
  int32_t device_id_{-1};
  aclrtContext aclrt_context_{nullptr};
  bool remote_cache_accessible_;
  // process mutex
  std::mutex mutex_;
  std::mutex map_mutex_;
  uint32_t rdmaTrafficClass_{0U};
  uint32_t rdmaServiceLevel_{0U};
  bool hasTrafficClass_{false};
  bool hasServiceLevel_{false};
};
}  // namespace llm
#endif  // CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_LINK_MANAGER_H_
