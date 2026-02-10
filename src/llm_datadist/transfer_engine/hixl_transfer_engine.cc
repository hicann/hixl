/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hixl_transfer_engine.h"
#include "llm_datadist/llm_datadist.h"
#include "common/llm_log.h"
#include "common/llm_utils.h"
#include "engine/engine_factory.h"
#include "common/ctrl_msg.h"
#include "common/ctrl_msg_plugin.h"
#include "link_mgr/hixl_entity.h"
#include "common/llm_scope_guard.h"

namespace llm {
namespace {
constexpr const char LLM_OPTION_RDMA_TRAFFIC_CLASS[] = "llm.RdmaTrafficClass";
constexpr const char LLM_OPTION_RDMA_SERVICE_LEVEL[] = "llm.RdmaServiceLevel";
constexpr const char ADXL_OPTION_LOCAL_COMM_RES[] = "adxl.LocalCommRes";
}

void HixlTransferEngine::LLMDataDist2HixlOptions(
    const std::map<ge::AscendString, ge::AscendString> &llm_datdsist_options,
    std::map<ge::AscendString, ge::AscendString> &hixl_options) {
  const static std::map<ge::AscendString, ge::AscendString> llm_to_hixl_options = {
    {LLM_OPTION_RDMA_TRAFFIC_CLASS, hixl::OPTION_RDMA_TRAFFIC_CLASS},
    {LLM_OPTION_RDMA_SERVICE_LEVEL, hixl::OPTION_RDMA_SERVICE_LEVEL},
    {llm_datadist::OPTION_LOCAL_COMM_RES, ADXL_OPTION_LOCAL_COMM_RES},
  };
  for (const auto &option : llm_datdsist_options) {
    const auto &iter = llm_to_hixl_options.find(option.first);
    if (iter != llm_to_hixl_options.cend()) {
      hixl_options[iter->second] = option.second;
    } else {
      LLMLOGI("Option:%s is not supported by HIXL, will be ignored.", option.first.GetString());
    }
  }
}

ge::Status HixlTransferEngine::InitMsgProcessor() {
  hixl::CtrlMsgPlugin::Initialize();
  hixl::CallbackProcessor callback = [this](
      int32_t fd, const char *msg, uint64_t msg_len, bool &keep_fd) -> hixl::Status {
    (void)msg;
    (void)msg_len;
    keep_fd = false;
    CacheTableInfo cache_table_info{};
    const auto &buffer_and_size = cache_manager_->GetCacheTableBufferAndSize();
    cache_table_info.cache_table_addr = PtrToValue(buffer_and_size.first);
    cache_table_info.cache_table_size = buffer_and_size.second;
    hixl::CtrlMsgHeader header{};
    header.magic = hixl::kMagicNumber;
    header.body_size = static_cast<uint64_t>(sizeof(hixl::CtrlMsgType) + sizeof(CacheTableInfo));
    hixl::CtrlMsgType msg_type = hixl::CtrlMsgType::kGetCacheTableResp;
    HIXL_CHK_STATUS_RET(hixl::CtrlMsgPlugin::Send(fd, &header, static_cast<uint64_t>(sizeof(header))),
                        "Failed to send cache table msg header");
    HIXL_CHK_STATUS_RET(hixl::CtrlMsgPlugin::Send(fd, &msg_type, static_cast<uint64_t>(sizeof(msg_type))),
                        "Failed to send cache table msg type");
    HIXL_CHK_STATUS_RET(hixl::CtrlMsgPlugin::Send(fd, &cache_table_info, static_cast<uint64_t>(sizeof(CacheTableInfo))),
                        "Failed to send cache table msg body");
    return hixl::SUCCESS;
  };
  LLM_CHK_HIXL_RET(engine_->RegisterCallbackProcessor(
      static_cast<int32_t>(hixl::CtrlMsgType::kGetCacheTableReq), callback),
      "Failed to initialize Hixl engine.");
  return ge::SUCCESS;
}

ge::Status HixlTransferEngine::Initialize(const std::map<ge::AscendString, ge::AscendString> &options) {
  bool remote_cache_accessible = false;
  LLM_CHK_STATUS_RET(LLMUtils::ParseFlag(kLlmOptionEnableRemoteCacheAccessible,
                                         options,
                                         remote_cache_accessible),
                     "Failed to parse option %s", kLlmOptionEnableRemoteCacheAccessible);
  LLM_CHK_BOOL_RET_STATUS(remote_cache_accessible, ge::LLM_PARAM_INVALID,
                          "When using hixl backend, option:%s needs to be specified to true.",
                          kLlmOptionEnableRemoteCacheAccessible);

  const auto &port_iter = options.find(kLlmOptionListenPort);
  const auto &ip_iter = options.find(kLlmOptionListenIp);
  LLM_CHK_BOOL_RET_STATUS(port_iter != options.cend() && ip_iter != options.cend(), ge::LLM_PARAM_INVALID,
                          "When using hixl backend, option:%s needs to be specified.",
                          llm_datadist::OPTION_LISTEN_IP_INFO);
  std::string ip = ip_iter->second.GetString();
  uint32_t port = 0U;
  LLM_CHK_STATUS_RET(LLMUtils::ToNumber(port_iter->second.GetString(), port),
                     "Option %s is invalid: [%s]",
                     kLlmOptionListenPort,
                     port_iter->second.GetString());
  local_engine_ = ip + ":" + std::to_string(port);
  LLMLOGI("listen option %s=%s", llm_datadist::OPTION_LISTEN_IP_INFO, local_engine_.c_str());
  LLM_ASSERT_RT_OK(aclrtGetCurrentContext(&rt_context_));
  std::map<ge::AscendString, ge::AscendString> hixl_options{};
  hixl_options[hixl::OPTION_BUFFER_POOL] = "0:0";
  LLMDataDist2HixlOptions(options, hixl_options);
  engine_ = hixl::EngineFactory::CreateEngine(local_engine_, hixl_options);
  LLM_CHECK_NOTNULL(engine_);
  LLM_CHK_HIXL_RET(engine_->Initialize(hixl_options), "Failed to initialize Hixl engine.");
  LLM_CHK_STATUS_RET(InitMsgProcessor(), "Failed to init msg processor");
  return ge::SUCCESS;
}

void HixlTransferEngine::Finalize() {
  LLMLOGI("Begin to transfer engine finalize.");
  engine_->Finalize();
}

ge::Status HixlTransferEngine::RegisterMem(void *addr, uint64_t size, HcclMemType type, void *&handle) {
  hixl::MemDesc desc{};
  desc.addr = reinterpret_cast<uintptr_t>(addr);
  desc.len = size;
  hixl::MemType mem_type = type == HCCL_MEM_TYPE_HOST ? hixl::MEM_HOST : hixl::MEM_DEVICE;
  LLM_CHK_HIXL_RET(engine_->RegisterMem(desc, mem_type, handle),
                   "Failed to register mem, addr:%p, size:%lu, type:%d.",
                   addr, size, static_cast<int32_t>(type));
  LLMLOGI("Regiter mem success, addr:%p, size:%lu, type:%d", addr, size, static_cast<int32_t>(type));
  return ge::SUCCESS;
}

ge::Status HixlTransferEngine::UnregisterMem(void *handle) {
  LLM_CHK_HIXL_RET(engine_->DeregisterMem(handle), "Failed to deregister mem, handle:%p.", handle);
  return ge::SUCCESS;
}

ge::Status HixlTransferEngine::LinkCluster(const ClusterInfo &cluster, int32_t timeout) {
  LLM_CHK_BOOL_RET_STATUS(cluster.remote_ip_infos.size() == 1U, ge::LLM_PARAM_INVALID,
                          "remote_ip_infos size != 1 is unsupported.");
  std::string remote_ip_str;
  LLM_CHK_STATUS_RET(LLMUtils::IntToIp(cluster.remote_ip_infos[0].ip, remote_ip_str), "Failed to covert remote ip.");
  uint32_t remote_port = static_cast<uint32_t>(cluster.remote_ip_infos[0].port);
  LLMLOGI("Start to link cluster, remote cluster_id:%lu, remote info %s:%u, timeout:%d ms.",
          cluster.remote_cluster_id, remote_ip_str.c_str(), remote_port, timeout);
  auto entity = llm::MakeShared<HixlEntity>(remote_ip_str, remote_port, engine_.get());
  LLM_CHECK_NOTNULL(entity);
  auto mem_info_ptr = MakeUnique<EntityMemInfo>(true,
                                                comm_entity_manager_->GetHostRegPool(),
                                                comm_entity_manager_->GetDeviceRegPool());
  LLM_CHECK_NOTNULL(mem_info_ptr);
  LLM_CHK_STATUS_RET(mem_info_ptr->Initialize(), "Failed to init mem info");
  entity->SetEntityMemInfo(mem_info_ptr);
  entity->SetCacheManager(cache_manager_);
  entity->SetContext(rt_context_);
  LLM_CHK_STATUS_RET(entity->Initialize(timeout), "Failed to init hixl entity");
  ScopeGuard guard([entity]() {
    entity->Finalize();
  });
  LLM_CHK_STATUS_RET(entity->SetInfo(), "Failed to set entity info");
  entity->MarkEntityIdle();
  LLM_CHK_STATUS_RET(comm_entity_manager_->AddEntity(cluster.remote_cluster_id, entity),
                    "Failed to create entity, remote_cluster_id:%lu", cluster.remote_cluster_id);
  guard.Dismiss();
  return ge::SUCCESS;
}

ge::Status HixlTransferEngine::LinkClusters(const std::vector<ClusterInfo> &clusters, std::vector<ge::Status> &rets,
                                            int32_t timeout) {
  LLM_CHK_BOOL_RET_STATUS(clusters.size() > 0, ge::LLM_PARAM_INVALID, "clusters size must > 0");
  LLMThreadPool thread_pool("llm_link_mem", 16U);
  std::vector<std::future<ge::Status>> future_rets;
  for (const auto &cluster : clusters) {
    auto fut = thread_pool.commit([this, cluster, timeout]() -> ge::Status {
      LLM_CHK_BOOL_RET_STATUS(aclrtSetCurrentContext(rt_context_) == ACL_ERROR_NONE, ge::LLM_PARAM_INVALID,
                              "Set runtime context failed.");
      LLM_CHK_STATUS_RET(LinkCluster(cluster, timeout),
                         "Failed to link cluster, remote_cluster_id = %lu, remote_role_type = %d.",
                         cluster.remote_cluster_id, cluster.remote_role_type);
      return ge::SUCCESS;
    });
    future_rets.emplace_back(std::move(fut));
  }

  auto ret = ge::SUCCESS;
  for (size_t i = 0; i < future_rets.size(); ++i) {
    auto fut_ret = future_rets[i].get();
    ret = fut_ret != ge::SUCCESS ? fut_ret : ret;
    LLM_CHK_STATUS(fut_ret, "Failed to link clusters, index = %zu", i);
    rets.emplace_back(fut_ret);
  }
  return ret;
}

ge::Status HixlTransferEngine::UnlinkCluster(const ClusterInfo &cluster, int32_t timeout) {
  LLM_CHK_BOOL_RET_STATUS(cluster.remote_ip_infos.size() == 1U, ge::LLM_PARAM_INVALID,
                          "remote_ip_infos size != 1 is unsupported.");
  std::string remote_ip_str;
  LLM_CHK_STATUS_RET(LLMUtils::IntToIp(cluster.remote_ip_infos[0].ip, remote_ip_str), "Failed to covert remote ip.");
  uint32_t remote_port = static_cast<uint32_t>(cluster.remote_ip_infos[0].port);
  LLMLOGI("Start to unlink cluster, remote cluster_id:%lu, remote info %s:%u, timeout:%d ms.",
          cluster.remote_cluster_id, remote_ip_str.c_str(), remote_port, timeout);
  LLM_CHK_STATUS_RET(comm_entity_manager_->DestroyEntity(cluster.remote_cluster_id),
                    "Failed to destroy entity, remote_cluster_id:%lu", cluster.remote_cluster_id);
  return ge::SUCCESS;
}

ge::Status HixlTransferEngine::UnlinkClusters(const std::vector<ClusterInfo> &clusters, std::vector<ge::Status> &rets,
                                              int32_t timeout, bool force_flag) {
  (void) force_flag;
  LLM_CHK_BOOL_RET_STATUS(clusters.size() > 0, ge::LLM_PARAM_INVALID, "clusters size must > 0");
  LLMThreadPool thread_pool("llm_link_mem", 16U);
  std::vector<std::future<ge::Status>> future_rets;
  for (const auto &cluster : clusters) {
    auto fut = thread_pool.commit([this, cluster, timeout]() -> ge::Status {
      LLM_CHK_BOOL_RET_STATUS(aclrtSetCurrentContext(rt_context_) == ACL_ERROR_NONE, ge::LLM_PARAM_INVALID,
                              "Set runtime context failed.");
      LLM_CHK_STATUS_RET(UnlinkCluster(cluster, timeout),
                         "Failed to unlink cluster, remote_cluster_id = %lu, remote_role_type = %d.",
                         cluster.remote_cluster_id, cluster.remote_role_type);
      return ge::SUCCESS;
    });
    future_rets.emplace_back(std::move(fut));
  }

  auto ret = ge::SUCCESS;
  for (size_t i = 0; i < future_rets.size(); ++i) {
    auto fut_ret = future_rets[i].get();
    ret = fut_ret != ge::SUCCESS ? fut_ret : ret;
    LLM_CHK_STATUS(fut_ret, "Failed to unlink clusters, index = %zu", i);
    rets.emplace_back(fut_ret);
  }
  return ret;
}

void HixlTransferEngine::UnlinkAllClusters() {
  LLMLOGI("Begin to unlink all clusters.");
  comm_entity_manager_->DeleteEntities();
}

ge::Status HixlTransferEngine::Link(std::string &cluster_name, const std::map<uint64_t, uint32_t> &cluster2rank, std::string &rank_table,
                                    uint64_t &comm_id) {
  (void) cluster_name;
  (void) cluster2rank;
  (void) rank_table;
  (void) comm_id;
  LLMLOGE(llm_datadist::LLM_FEATURE_NOT_ENABLED, "The feature is not supported.");
  return llm_datadist::LLM_FEATURE_NOT_ENABLED;
}

ge::Status HixlTransferEngine::Unlink(uint64_t comm_id) {
  (void) comm_id;
  LLMLOGE(llm_datadist::LLM_FEATURE_NOT_ENABLED, "The feature is not supported.");
  return llm_datadist::LLM_FEATURE_NOT_ENABLED;
  return ge::SUCCESS;
}

ge::Status HixlTransferEngine::QueryRegisterMemStatus(uint64_t comm_id, RegisterMemoryStatus &status) {
  (void) comm_id;
  (void) status;
  LLMLOGE(llm_datadist::LLM_FEATURE_NOT_ENABLED, "The feature is not supported.");
  return llm_datadist::LLM_FEATURE_NOT_ENABLED;
  return ge::SUCCESS;
}

ge::Status HixlTransferEngine::SwitchRole(const std::string &role, const std::map<std::string, std::string> &options) {
  (void) role;
  const auto &port_iter = options.find(kLlmOptionListenPort);
  const auto &ip_iter = options.find(kLlmOptionListenIp);
  if (port_iter != options.cend() && ip_iter != options.cend()) {
    std::string ip = ip_iter->second;
    uint32_t port = 0U;
    LLM_CHK_STATUS_RET(LLMUtils::ToNumber(port_iter->second, port),
                       "Option %s is invalid: [%s]",
                       kLlmOptionListenPort,
                       port_iter->second.c_str());
    auto local_engine = ip + ":" + std::to_string(port);
    LLM_CHK_BOOL_RET_STATUS(local_engine == local_engine_, ge::LLM_FEATURE_NOT_ENABLED,
                            "listen ip info:%s is not the same with init listen ip info:%s.",
                            local_engine.c_str(), local_engine_.c_str());
  }
  return ge::SUCCESS;
}
}  // namespace llm
