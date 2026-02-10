/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "llm_link_manager.h"
#include "common/llm_checker.h"
#include "common/llm_utils.h"
#include "common/llm_thread_pool.h"

namespace llm {
void LLMLinkManager::Finalize() {
  LLMLOGI("LLMLinkManager finalize start");
  msg_handler_.Finalize();
  CommLinkManager::Finalize();
}

ge::Status LLMLinkManager::Initialize(const std::map<ge::AscendString, ge::AscendString> &options) {
  LLM_ASSERT_RT_OK(aclrtGetCurrentContext(&aclrt_context_));
  CommLinkManager::Initialize(options);
  msg_handler_.SetMemHandlesCallback([this]() -> std::vector<void *> {
    return this->GetAllRegisterMemHandles();
  });
  LLM_CHK_STATUS_RET(msg_handler_.Initialize(options), "Failed to init msg handler");
  const auto &iter = options.find(kLlmOptionListenPort);
  if (iter != options.cend()) {
    const auto &ip_it = options.find(kLlmOptionListenIp);
    LLM_CHK_BOOL_RET_STATUS(ip_it != options.cend(), ge::LLM_PARAM_INVALID,
                            "%s is not assigned", kLlmOptionListenIp);
    uint32_t port = 0U;
    std::string ip = ip_it->second.GetString();
    LLM_CHK_STATUS_RET(LLMUtils::ToNumber(iter->second.GetString(), port),
                      "Option %s is invalid: [%s]",
                      kLlmOptionListenPort,
                      iter->second.GetString());
    LLM_CHK_STATUS_RET(msg_handler_.StartDaemon(ip, port), "Failed to start listen deamon, port = %u", port);
    listen_port_ = iter->second.GetString();
    LLMEVENT("start daemon success, listen on port:%u", port);
  }
  return ge::SUCCESS;
}

ge::Status LLMLinkManager::LinkClusters(const std::vector<ClusterInfo> &clusters,
                                        std::vector<ge::Status> &rets,
                                        int32_t timeout) {
  LLM_CHK_BOOL_RET_STATUS(clusters.size() > 0, ge::LLM_PARAM_INVALID, "clusters size must > 0");
  LLMThreadPool thread_pool("llm_link_mem", 16U);
  std::vector<std::future<ge::Status>> fut_rets;
  for (const auto &cluster : clusters) {
    auto fut = thread_pool.commit([this, cluster, timeout]() -> ge::Status {
      LLM_CHK_BOOL_RET_STATUS(aclrtSetCurrentContext(aclrt_context_) == ACL_ERROR_NONE, ge::LLM_PARAM_INVALID,
                             "Set aclrt context failed.");
      LLM_CHK_STATUS_RET(msg_handler_.LinkCluster(cluster, timeout),
                        "Failed to link cluster, remote_cluster_id = %lu, remote_role_type = %d.",
                        cluster.remote_cluster_id, cluster.remote_role_type);
      return ge::SUCCESS;
    });
    fut_rets.emplace_back(std::move(fut));
  }

  auto ret = ge::SUCCESS;
  for (size_t i = 0; i < fut_rets.size(); ++i) {
    auto fut_ret = fut_rets[i].get();
    ret = fut_ret != ge::SUCCESS ? fut_ret : ret;
    LLM_CHK_STATUS(fut_ret, "Failed to link clusters, index = %zu", i);
    rets.emplace_back(fut_ret);
  }
  return ret;
}

void LLMLinkManager::UnlinkAllClusters() {
  comm_entity_manager_->DeleteEntities();
  CommLinkManager::UnlinkAll();
}

ge::Status LLMLinkManager::UnlinkClusters(const std::vector<ClusterInfo> &clusters,
                                          std::vector<ge::Status> &rets,
                                          int32_t timeout,
                                          bool force_flag) {
  LLM_CHK_BOOL_RET_STATUS(clusters.size() > 0, ge::LLM_PARAM_INVALID, "clusters size must > 0");
  LLMThreadPool thread_pool("llm_link_mem", 16U);
  std::vector<std::future<ge::Status>> fut_rets;
  for (const auto &cluster : clusters) {
    auto fut = thread_pool.commit([this, cluster, timeout, force_flag]() -> ge::Status {
      LLM_CHK_BOOL_RET_STATUS(aclrtSetCurrentContext(aclrt_context_) == ACL_ERROR_NONE, ge::LLM_PARAM_INVALID,
                             "Set aclrt context failed.");
      LLM_CHK_STATUS_RET(msg_handler_.UnlinkCluster(cluster, timeout, force_flag),
                        "Failed to unlink cluster, remote_cluster_id = %lu, remote_role_type = %d.",
                        cluster.remote_cluster_id, cluster.remote_role_type);
      return ge::SUCCESS;
    });
    fut_rets.emplace_back(std::move(fut));
  }

  auto ret = ge::SUCCESS;
  for (size_t i = 0; i < fut_rets.size(); ++i) {
    auto fut_ret = fut_rets[i].get();
    ret = fut_ret != ge::SUCCESS ? fut_ret : ret;
    LLM_CHK_STATUS(fut_ret, "Failed to unlink clusters, index = %zu", i);
    rets.emplace_back(fut_ret);
  }
  return ret;
}

ge::Status LLMLinkManager::SwitchRole(const std::string &role, const std::map<std::string, std::string> &options) {
  (void) role;
  LLM_CHK_BOOL_RET_STATUS(msg_handler_.GetLinkSize() == 0, ge::LLM_EXIST_LINK,
                         "Can not switch role when link size > 0, please unlink with cluster:%lu first.",
                         cluster_id_);
  const auto &iter = options.find(kLlmOptionListenPort);
  if (iter != options.cend()) {
    uint32_t port = 0U;
    LLM_CHK_STATUS_RET(LLMUtils::ToNumber(iter->second, port),
                      "Option %s is invalid: [%s]",
                      kLlmOptionListenPort,
                      iter->second.c_str());
    const auto &ip_it = options.find(kLlmOptionListenIp);
    LLM_CHK_BOOL_RET_STATUS(ip_it != options.cend(), ge::LLM_PARAM_INVALID,
                            "%s is not assigned", kLlmOptionListenIp);
    const auto &ip = ip_it->second;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!listen_port_.empty()) {
      if (listen_port_ != iter->second) {
        LLM_CHK_STATUS_RET(msg_handler_.StopDaemon(), "Failed to close listen daemon.");
        LLMEVENT("stop listen daemon success, listen on port:%s", listen_port_.c_str());
        listen_port_ = "";
      } else {
        // already listen, do nothin
        return ge::SUCCESS;
      }
    }

    LLM_CHK_STATUS_RET(msg_handler_.StartDaemon(ip, port),
                       "Failed to start listen deamon, ip:%s, port:%u", ip.c_str(), port);
    LLMEVENT("start daemon success, listen on %s:%u", ip.c_str(), port);
    listen_port_ = iter->second;
  } else {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!listen_port_.empty()) {
      LLM_CHK_STATUS_RET(msg_handler_.StopDaemon(), "Failed to close listen daemon.");
      LLMEVENT("stop listen daemon success, listen on port:%s", listen_port_.c_str());
      listen_port_ = "";
    }
  }
  return ge::SUCCESS;
}
}  // namespace llm
