/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hccl_transfer_engine.h"
#include "common/llm_utils.h"
#include "common/llm_log.h"

namespace llm {
HcclTransferEngine::~HcclTransferEngine() {}

ge::Status HcclTransferEngine::Initialize(const std::map<ge::AscendString, ge::AscendString> &options) {
  LLM_CHK_STATUS_RET(HcclAdapter::GetInstance().Initialize(), "HcclSoManager initialize failed.");
  bool remote_cache_accessible = false;
  LLM_CHK_STATUS_RET(LLMUtils::ParseFlag(kLlmOptionEnableRemoteCacheAccessible,
                                         options,
                                         remote_cache_accessible),
                     "Failed to parse option %s", kLlmOptionEnableRemoteCacheAccessible);
  int32_t device_id = 0;
  LLM_CHK_STATUS_RET(LLMUtils::ParseDeviceId(options, device_id), "Failed to get device id");
  llm_link_mgr_ = MakeUnique<LLMLinkManager>(cluster_id_, device_id, comm_entity_manager_,
                                             cache_manager_, remote_cache_accessible);
  LLM_CHECK_NOTNULL(llm_link_mgr_);
  llm_link_mgr_->SetCommEntityManager(comm_entity_manager_);
  llm_link_mgr_->SetCacheManager(cache_manager_);
  LLM_CHK_STATUS_RET(llm_link_mgr_->Initialize(options), "Failed to initialize LLMLinkManager");
  return ge::SUCCESS;
}

void HcclTransferEngine::Finalize() {
  (void) llm_link_mgr_->Finalize();
}

ge::Status HcclTransferEngine::RegisterMem(void *addr, uint64_t size, HcclMemType type, void *&handle) {
  HcclMem mem = {};
  mem.addr = addr;
  mem.size = size;
  mem.type = type;
  LLM_CHK_STATUS_RET(llm_link_mgr_->RegisterMem(&mem, &handle),
                     "Failed to register mem, addr:%p, size:%lu, type:%d", addr, size, static_cast<int32_t>(type));
  return ge::SUCCESS;
}

ge::Status HcclTransferEngine::UnregisterMem(void *handle) {
  LLM_CHK_STATUS_RET(llm_link_mgr_->DeregisterGlobalMem(handle), "Failed to unregister mem, handle:%p", handle);
  return ge::SUCCESS;
}

ge::Status HcclTransferEngine::LinkClusters(const std::vector<ClusterInfo> &clusters, std::vector<ge::Status> &rets,
                                            int32_t timeout) {
  LLM_CHK_STATUS_RET(llm_link_mgr_->LinkClusters(clusters, rets, timeout),
                     "Failed to link clusters, clusters size:%zu, timeout:%d", clusters.size(), timeout);
  return ge::SUCCESS;
}

ge::Status HcclTransferEngine::UnlinkClusters(const std::vector<ClusterInfo> &clusters, std::vector<ge::Status> &rets,
                                              int32_t timeout, bool force_flag) {
  LLM_CHK_STATUS_RET(llm_link_mgr_->UnlinkClusters(clusters, rets, timeout, force_flag),
                     "Failed to unlink clusters, clusters size:%zu, timeout:%d, force_flag:%d",
                     clusters.size(), timeout, static_cast<int32_t>(force_flag));
  return ge::SUCCESS;
}

ge::Status HcclTransferEngine::Link(std::string &cluster_name, const std::map<uint64_t, uint32_t> &cluster2rank, std::string &rank_table,
                                    uint64_t &comm_id) {
  LLM_CHK_STATUS_RET(llm_link_mgr_->Link(cluster_name, cluster2rank, rank_table, comm_id),
                     "Failed to link, cluster name:%s, rank_table:%s",
                     cluster_name.c_str(), rank_table.c_str());
  return ge::SUCCESS;
}

ge::Status HcclTransferEngine::Unlink(uint64_t comm_id) {
  LLM_CHK_STATUS_RET(llm_link_mgr_->Unlink(comm_id), "Failed to unlink, comm_id:%lu", comm_id);
  return ge::SUCCESS;
}

ge::Status HcclTransferEngine::QueryRegisterMemStatus(uint64_t comm_id, RegisterMemoryStatus &status) {
  LLM_CHK_STATUS_RET(llm_link_mgr_->QueryRegisterMemStatus(comm_id, status),
                     "Failed to query link status, comm_id:%lu", comm_id);
  return ge::SUCCESS;
}

ge::Status HcclTransferEngine::SwitchRole(const std::string &role, const std::map<std::string, std::string> &options) {
  LLM_CHK_STATUS_RET(llm_link_mgr_->SwitchRole(role, options), "Failed to switch role, role:%s", role.c_str());
  return ge::SUCCESS;
}
}  // namespace llm
