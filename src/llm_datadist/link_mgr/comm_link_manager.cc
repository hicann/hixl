/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "comm_link_manager.h"
#include "comm_entity_manager.h"
#include "common/def_types.h"
#include "common/llm_checker.h"
#include "common/llm_utils.h"
#include "common/llm_scope_guard.h"

namespace llm {
namespace {
constexpr uint64_t kDefaultReqBufferSize = 112U * 1024U;
constexpr uint64_t kDefaultRespBufferSize = 16U * 1024U;
constexpr int64_t kDefaultSleepTime = 1;
constexpr uint32_t kMaxLinkNum = 512;
constexpr const char LLM_OPTION_RDMA_TRAFFIC_CLASS[] = "llm.RdmaTrafficClass";
constexpr const char LLM_OPTION_RDMA_SERVICE_LEVEL[] = "llm.RdmaServiceLevel";
constexpr const char LLM_OPTION_LINK_TOTAL_TIME[] = "llm.LinkTotalTime";
constexpr const char LLM_OPTION_LINK_RETRY_COUNT[] = "llm.LinkRetryCount";
}  // namespace

static void from_json(const nlohmann::json &j, ExchangeMemInfo &e) {
  j.at("cache_table_addr").get_to(e.cache_table_addr);
  j.at("cache_table_size").get_to(e.cache_table_size);
  j.at("req_addr").get_to(e.req_addr);
  j.at("req_size").get_to(e.req_size);
  j.at("resp_addr").get_to(e.resp_addr);
  j.at("resp_size").get_to(e.resp_size);
}

static void to_json(nlohmann::json &j, const ExchangeMemInfo &e) {
  j = nlohmann::json{};
  j["cache_table_addr"] = e.cache_table_addr;
  j["cache_table_size"] = e.cache_table_size;
  j["req_addr"] = e.req_addr;
  j["req_size"] = e.req_size;
  j["resp_addr"] = e.resp_addr;
  j["resp_size"] = e.resp_size;
}

ge::Status CommLinkManager::ExchangeMem(const EntityPtr &entity, uint32_t local_rank, uint32_t remote_rank) const {
  ExchangeMemInfo local_mem_info{};
  const auto &buffer_and_size = cache_manager_->GetCacheTableBufferAndSize();
  local_mem_info.cache_table_addr = PtrToValue(buffer_and_size.first);
  local_mem_info.cache_table_size = buffer_and_size.second;
  local_mem_info.req_addr = PtrToValue(entity->GetReq());
  local_mem_info.req_size = kDefaultReqBufferSize;
  local_mem_info.resp_addr = PtrToValue(entity->GetResp());
  local_mem_info.resp_size = kDefaultRespBufferSize;
  std::string mem_info_str;
  try {
    nlohmann::json j = local_mem_info;
    mem_info_str = j.dump();
  } catch (const nlohmann::json::exception &e) {
    LLMLOGE(ge::LLM_PARAM_INVALID, "Failed to dump msg to str, exception:%s", e.what());
    return ge::LLM_PARAM_INVALID;
  }
  HcclMemDesc local_desc{};
  local_desc.localRank = local_rank;
  local_desc.remoteRank = remote_rank;
  HcclMemDesc remote_desc{};
  LLM_ASSERT_EOK(strcpy_s(local_desc.desc, HCCL_MEM_DESC_LENGTH, mem_info_str.c_str()));
  HcclMemDescs local_descs{&local_desc, 1U};
  HcclMemDescs remote_descs{&remote_desc, 1U};
  uint32_t remote_mem_num = 0U;
  LLMLOGI("start call HcclExchangeMemDesc, remote_rank:%u", remote_rank);
  int32_t timeout = link_total_time_ > 0 ? link_total_time_ : 30;
  HcclResult ret = HcclAdapter::GetInstance().HcclExchangeMemDesc(entity->GetComm(), remote_rank, &local_descs,
                                                                  timeout, &remote_descs, &remote_mem_num);
  LLM_CHK_BOOL_RET_STATUS(ret == HcclResult::HCCL_SUCCESS, ge::LLM_LINK_FAILED,
                          "Call HcclExchangeMemDesc failed, ret:%d", ret);
  LLMLOGI("HcclExchangeMemDesc suc, remote num:%u", remote_mem_num);
  ExchangeMemInfo remote_mem_info{};
  try {
    auto j = nlohmann::json::parse(&remote_desc.desc[0]);
    remote_mem_info = j.get<ExchangeMemInfo>();
  } catch (const nlohmann::json::exception &e) {
    LLMLOGE(ge::LLM_PARAM_INVALID, "Failed to load exchange mem info, exception:%s", e.what());
    return ge::LLM_PARAM_INVALID;
  }
  std::vector<HcclMem> &remote_mems = entity->GetRemoteMems();
  HcclMem mem{};
  SetMemAttribute(remote_mem_info, remote_mems, mem);
  return ge::SUCCESS;
}

void CommLinkManager::SetMemAttribute(const ExchangeMemInfo &remote_mem_info, 
                                      std::vector<HcclMem> &remote_mems, 
                                      HcclMem &mem) {
  mem.type = HcclMemType::HCCL_MEM_TYPE_DEVICE;
  mem.addr = ValueToPtr(remote_mem_info.cache_table_addr);
  mem.size = remote_mem_info.cache_table_size;
  remote_mems.emplace_back(mem);
  mem.type = HcclMemType::HCCL_MEM_TYPE_HOST;
  mem.addr = ValueToPtr(remote_mem_info.req_addr);
  mem.size = remote_mem_info.req_size;
  remote_mems.emplace_back(mem);
  mem.type = HcclMemType::HCCL_MEM_TYPE_HOST;
  mem.addr = ValueToPtr(remote_mem_info.resp_addr);
  mem.size = remote_mem_info.resp_size;
  remote_mems.emplace_back(mem);
}

void CommLinkManager::CheckUnlink(PrepareMemArg &req, bool &check_unlink_flag) {
  std::lock_guard<std::mutex> lock(map_mutex_);
  if (comm_to_status_[req.comm_id].unlink_flag.load()) {
    comm_to_status_[req.comm_id].prepare_mem_flag.store(false);
    LLMLOGI("Check exist unlink flag, abort preparing memory.");
    check_unlink_flag = true;
  }
}

ge::Status CommLinkManager::DestroyRes(EntityCommInfoPtr comm_ptr, std::vector<EntityPtr> &comm_entities) const {
  LLMLOGI("Start call DestroyRes");
  ge::Status unlink_ret = ge::SUCCESS;
  LLM_CHK_BOOL_EXEC(aclrtSetCurrentContext(aclrt_context_) == ACL_ERROR_NONE, unlink_ret = ge::LLM_UNLINK_FAILED);
  for (const auto &entity : comm_entities) {
    std::lock_guard<std::mutex> pull_lock(entity->GetPullMutex());
    std::lock_guard<std::mutex> process_lock(entity->GetProcessMutex());
    auto entity_ret = entity->Finalize();
    unlink_ret = entity_ret != ge::SUCCESS ? ge::LLM_UNLINK_FAILED : unlink_ret;
    entity->MarkEntityDestroyed();
  }
  if (remote_cache_accessible_) {
    LLMLOGI("Start delete entities.");
    comm_entity_manager_->DeleteEntities();
  }
  if (comm_ptr != nullptr) {
    auto comm_ret = comm_ptr->Finalize();
    unlink_ret = comm_ret != ge::SUCCESS ? ge::LLM_UNLINK_FAILED : unlink_ret;
  }
  return unlink_ret;
}

void CommLinkManager::FreeFlagGuard(PrepareMemArg &req) {
  std::lock_guard<std::mutex> lock(map_mutex_);
  if (comm_to_status_.find(req.comm_id) != comm_to_status_.end()) {
    comm_to_status_[req.comm_id].prepare_mem_flag.store(false);
  }
}

void CommLinkManager::FlagGuard(PrepareMemArg &req) {
  std::lock_guard<std::mutex> lock(map_mutex_);
  if (comm_to_status_.find(req.comm_id) != comm_to_status_.end()) {
    comm_to_status_[req.comm_id].prepare_mem_flag.store(true);
  }
}

ge::Status CommLinkManager::PrepareComm(const PrepareMemArg &req,
                                        EntityCommInfoPtr &comm_info_ptr) {
  comm_info_ptr = MakeShared<EntityCommInfo>(req.comm, req.mem_handles, req.link_total_time, req.link_retry_count);
  LLM_CHECK_NOTNULL(comm_info_ptr);
  {
    std::lock_guard<std::mutex> lock(map_mutex_);
    if (comm_to_status_.find(req.comm_id) != comm_to_status_.end()) {
      comm_to_status_[req.comm_id].comm_ptr = comm_info_ptr;
    }
  }
  LLM_CHK_STATUS_RET(comm_info_ptr->Initialize(), "Failed to init comm info");
  return ge::SUCCESS;
}

ge::Status CommLinkManager::CreateClustersEntity(PrepareMemArg &req,
                                                 std::map<uint64_t, EntityPtr> &cluster2entity) {
  auto local_rank_id = req.cluster2rank[cluster_id_];
  for (auto &iter : req.cluster2rank) {
    if (iter.first != cluster_id_) {
      // create comm entity
      EntityPtr entity = nullptr;
      CommEntityParams params{};
      params.comm_id = req.comm_id;
      params.peer_cluster_id = iter.first;
      params.peer_rank_id = iter.second;
      params.local_cluster_id = cluster_id_;
      params.local_rank_id = local_rank_id;
      params.remote_cache_accessible = remote_cache_accessible_;
      LLM_CHK_STATUS_RET(comm_entity_manager_->CreateEntity(params, entity), "Failed to create entity");
      LLM_CHK_BOOL_RET_STATUS(entity != nullptr, ge::FAILED, "CreateEntity failed.");
      LLMLOGI("Success to create comm entity:%s", entity->GetDesc().c_str());
      auto mem_info_ptr = MakeUnique<EntityMemInfo>(remote_cache_accessible_,
                                                    comm_entity_manager_->GetHostRegPool(),
                                                    comm_entity_manager_->GetDeviceRegPool());
      LLM_CHECK_NOTNULL(mem_info_ptr);
      LLM_CHK_STATUS_RET(mem_info_ptr->Initialize(), "Failed to init mem info");
      entity->SetEntityMemInfo(mem_info_ptr);
      entity->SetCacheManager(cache_manager_);
      entity->SetContext(aclrt_context_);
      cluster2entity[iter.first] = entity;
    }
  }
  return ge::SUCCESS;
}

ge::Status CommLinkManager::PrepareMem(PrepareMemArg &req) {
  {
    std::lock_guard<std::mutex> lock(map_mutex_);
    // already unlinked.
    LLM_CHK_BOOL_RET_STATUS_NOLOG(comm_to_status_.find(req.comm_id) != comm_to_status_.end(), ge::FAILED);
    comm_to_status_[req.comm_id].prepare_mem_flag.store(true);
  }
  bool check_unlink_flag = false;
  CheckUnlink(req, check_unlink_flag);
  LLM_CHK_BOOL_RET_STATUS_NOLOG(!check_unlink_flag, ge::FAILED);
  LLM_MAKE_GUARD(free_flag, ([this, &req]() { FreeFlagGuard(req); }));
  LLM_ASSERT_RT_OK(aclrtSetCurrentContext(aclrt_context_));
  std::map<uint64_t, EntityPtr> cluster2entity;
  LLM_CHK_STATUS_RET(CreateClustersEntity(req, cluster2entity), "Failed to create clusters entity.");
  EntityCommInfoPtr comm_info_ptr = nullptr;
  FreeFlagGuard(req);
  LLM_CHK_STATUS_RET(PrepareComm(req, comm_info_ptr), "Failed to prepare comm info");
  FlagGuard(req);
  std::vector<uint64_t> cluster_ids;
  std::vector<uint32_t> rank_ids;
  for (auto &iter : req.cluster2rank) {
    (void)cluster_ids.emplace_back(iter.first);
    (void)rank_ids.emplace_back(iter.second);
  }
  LLMEVENT("Begin to prepare memory for clusters[%s], ranks[%s].", ToString(cluster_ids).c_str(),
          ToString(rank_ids).c_str());
  std::vector<EntityPtr> new_entities;
  auto local_rank_id = req.cluster2rank[cluster_id_];
  for (auto &iter : req.cluster2rank) {
    if (iter.first != cluster_id_) {
      CheckUnlink(req, check_unlink_flag);
      auto entity = cluster2entity[iter.first];
      entity->SetEntityCommInfo(comm_info_ptr);
      LLM_CHK_STATUS_RET(ExchangeMem(entity, local_rank_id, iter.second), "Failed to exchange mem");
      LLM_CHK_STATUS_RET(entity->SetInfo(), "Failed to set entity info");
      (void)new_entities.emplace_back(entity);
    }
  }
  for (auto &entity : new_entities) {
    std::lock_guard<std::mutex> process_lock(entity->GetProcessMutex());
    entity->MarkEntityIdle();
  }
  return ge::SUCCESS;
}

ge::Status CommLinkManager::PrepareMemTask(CommLinkManager *link_manager, PrepareMemArg request) {
  LLMLOGI("New prepare mem request arrived.");
  const auto start = std::chrono::steady_clock::now();
  auto ret = link_manager->PrepareMem(request);
  const auto end = std::chrono::steady_clock::now();
  auto count = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
  LLMLOGI("Prepare mem time cost:%ld us", count);
  return ret;
}

void CommLinkManager::Finalize() {
  LLMLOGI("CommLinkManager finalize start, start stop thread.");
  running_ = false;
  std::vector<uint64_t> comm_ids;
  {
    std::lock_guard<std::mutex> map_lock(map_mutex_);
    for (auto &comm_to_status : comm_to_status_) {
      comm_ids.emplace_back(comm_to_status.first);
    }
  }
  for (auto comm_id : comm_ids) {
    Unlink(comm_id);
  }
  thread_pool_.Destroy();
}

ge::Status CommLinkManager::Initialize(const std::map<ge::AscendString, ge::AscendString> &options) {
  LLM_ASSERT_RT_OK(aclrtGetCurrentContext(&aclrt_context_));
  const auto it = options.find(LLM_OPTION_RDMA_TRAFFIC_CLASS);
  if (it != options.end()) {
    LLM_CHK_STATUS_RET(LLMUtils::ToNumber(it->second.GetString(), rdmaTrafficClass_),
                      "llm.RdmaTrafficClass is invalid, value = %s",
                      it->second.GetString());
    hasTrafficClass_ = true;
  }
  const auto serviceLevelIter = options.find(LLM_OPTION_RDMA_SERVICE_LEVEL);
  if (serviceLevelIter != options.end()) {
    LLM_CHK_STATUS_RET(LLMUtils::ToNumber(serviceLevelIter->second.GetString(), rdmaServiceLevel_),
                      "llm.RdmaServiceLevel is invalid, value = %s", serviceLevelIter->second.GetString());
    hasServiceLevel_ = true;
  }
  const auto total_time = options.find(LLM_OPTION_LINK_TOTAL_TIME);
  if (total_time != options.end()) {
    LLM_CHK_STATUS_RET(LLMUtils::ToNumber(total_time->second.GetString(), link_total_time_),
                      "llm.linkTotalTime is invalid, value = %s",
                      total_time->second.GetString());
  }
  const auto retry_count = options.find(LLM_OPTION_LINK_RETRY_COUNT);
  if (retry_count != options.end()) {
    LLM_CHK_STATUS_RET(LLMUtils::ToNumber(retry_count->second.GetString(), link_retry_count_),
                      "llm.linkRetryCount is invalid, value = %s",
                      retry_count->second.GetString());
  }
  return ge::SUCCESS;
}

uint64_t CommLinkManager::GenerateCommId() {
  return comm_id_gen_.fetch_add(1UL, std::memory_order::memory_order_relaxed);
}

ge::Status CommLinkManager::Link(std::string &cluster_name, const std::map<uint64_t, uint32_t> &cluster2rank,
                                 std::string &rank_table, uint64_t &comm_id) {
  LLM_CHK_BOOL_RET_STATUS(!cluster_name.empty(), ge::LLM_PARAM_INVALID,
                         "param cluster_name can not be empty.");
  LLM_CHK_BOOL_RET_STATUS(cluster_name.size() < COMM_NAME_MAX_LENGTH, ge::LLM_PARAM_INVALID,
                         "param cluster_name size should be smaller than:%u", COMM_NAME_MAX_LENGTH);
  LLM_CHK_BOOL_RET_STATUS(cluster2rank.find(cluster_id_) != cluster2rank.end(), ge::LLM_PARAM_INVALID,
                         "local cluster id does not exist in cluster2rank");
  // lock for process
  std::lock_guard<std::mutex> lock(mutex_);
  {
    std::lock_guard<std::mutex> map_lock(map_mutex_);
    LLM_CHK_BOOL_RET_STATUS(comm_to_status_.size() < kMaxLinkNum, ge::LLM_PARAM_INVALID,
                           "Link num is over limit:%u.", kMaxLinkNum);
  }
  auto local_rank = cluster2rank.at(cluster_id_);
  LLM_CHK_BOOL_RET_STATUS(aclrtSetCurrentContext(aclrt_context_) == ACL_ERROR_NONE, ge::LLM_UNLINK_FAILED,
                         "Set aclrt context failed.");
  HcclCommConfig config{};
  HcclAdapter::GetInstance().HcclCommConfigInit(&config);
  LLM_ASSERT_EOK(strcpy_s(config.hcclCommName, COMM_NAME_MAX_LENGTH, cluster_name.data()));
  if (hasTrafficClass_) {
    LLMLOGI("Set rdma traffic class to %u.", rdmaTrafficClass_);
    config.hcclRdmaTrafficClass = rdmaTrafficClass_;
  }
  if (hasServiceLevel_) {
    LLMLOGI("Set rdma service level to %u.", rdmaServiceLevel_);
    config.hcclRdmaServiceLevel = rdmaServiceLevel_;
  }

  LLMLOGI("HcclCommInitClusterInfoMemConfig begin, comm_name=%s, local rank_id=%u, rank_table=%s",
         config.hcclCommName, local_rank, rank_table.c_str());
  HcclComm comm{};
  HcclResult ret = HcclAdapter::GetInstance().HcclCommInitClusterInfoMemConfig(rank_table.c_str(), local_rank,
                                                                               &config, &comm);
  LLM_CHK_BOOL_RET_STATUS(ret == HcclResult::HCCL_SUCCESS, ge::LLM_LINK_FAILED,
                         "Call HcclCommInitClusterInfoMemConfig failed, ret:%d.", ret);

  comm_id = GenerateCommId();
  {
    std::lock_guard<std::mutex> map_lock(map_mutex_);
    auto &comm_status = comm_to_status_[comm_id];
    comm_status.unlink_flag.store(false);
    comm_status.prepare_mem_flag.store(false);
    comm_status.status = RegisterMemoryStatus::PREPARING;
    PrepareMemArg prepare_mem_arg{comm_id, comm, cluster2rank, comm_mem_manager_->GetAllRegisterMemHandles(), 
                                  link_total_time_, link_retry_count_};
    auto fut = thread_pool_.commit(&CommLinkManager::PrepareMemTask, this, prepare_mem_arg);
    comm_status.task_fut = std::move(fut);
  }
  LLMLOGI("Init comm success, comm id:%lu.", comm_id);
  return ge::SUCCESS;
}

ge::Status CommLinkManager::Unlink(uint64_t comm_id) {
  ge::Status ret;
  // lock for process
  std::lock_guard<std::mutex> lock(mutex_);
  {
    std::lock_guard<std::mutex> map_lock(map_mutex_);
    auto iter = comm_to_status_.find(comm_id);
    LLM_CHK_BOOL_RET_STATUS(iter != comm_to_status_.end(), ge::LLM_NOT_YET_LINK, "Comm:%lu is not yet link.", comm_id);
    iter->second.unlink_flag.store(true);
  }
  LLMLOGI("Start unlink, comm_id:%lu", comm_id);
  while (true) {
    {
      std::lock_guard<std::mutex> map_lock(map_mutex_);
      // check exist before.
      auto &comm_status = comm_to_status_[comm_id];
      if (!comm_status.prepare_mem_flag.load()) {
        LLMLOGI("Can unlink now.");
        std::vector<EntityPtr> entities = comm_entity_manager_->QueryEntityByCommId(comm_id);
        ret = DestroyRes(comm_status.comm_ptr, entities);
        break;
      }
    }
    LLMLOGD("Wait prepare mem end.");
    // wait prepare mem thread abort
    std::this_thread::sleep_for(std::chrono::milliseconds(kDefaultSleepTime));
  }

  std::future<ge::Status> task_fut;
  {
    std::lock_guard<std::mutex> map_lock(map_mutex_);
    auto &comm_status = comm_to_status_[comm_id];
    task_fut = std::move(comm_status.task_fut);
    (void)comm_to_status_.erase(comm_id);
  }

  if (task_fut.valid()) {
    task_fut.get();
  }
  LLMLOGI("Call unlink done for comm id:%lu", comm_id);
  return ret;
}

ge::Status CommLinkManager::QueryRegisterMemStatus(uint64_t comm_id, RegisterMemoryStatus &status) {
  auto ret = ge::LLM_NOT_YET_LINK;
  std::lock_guard<std::mutex> lock(map_mutex_);
  auto iter = comm_to_status_.find(comm_id);
  if (iter != comm_to_status_.end()) {
    status = iter->second.status;
    if (status == RegisterMemoryStatus::PREPARING &&
        iter->second.task_fut.valid() &&
        iter->second.task_fut.wait_for(std::chrono::microseconds(1)) == std::future_status::ready) {
      auto prepare_ret = iter->second.task_fut.get();
      status = (prepare_ret == ge::SUCCESS) ? RegisterMemoryStatus::OK : RegisterMemoryStatus::FAILED;
      iter->second.status = status;
    }
    ret = ge::SUCCESS;
  }
  return ret;
}

void CommLinkManager::SetCommEntityManager(CommEntityManager *comm_entity_manager) {
  comm_entity_manager_ = comm_entity_manager;
}

void CommLinkManager::SetCommMemManager(CommMemManager *comm_mem_manager) {
  comm_mem_manager_ = comm_mem_manager;
}

void CommLinkManager::SetCacheManager(CacheManager *cache_manager) {
  cache_manager_ = cache_manager;
}

}  // namespace llm
