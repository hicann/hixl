/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_LLM_LINK_MANAGER_H_
#define CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_LLM_LINK_MANAGER_H_

#include <mutex>
#include "llm_datadist/llm_error_codes.h"
#include "common/llm_inner_types.h"
#include "link_msg_handler.h"
#include "comm_link_manager.h"

namespace llm {
class LLMLinkManager : public CommLinkManager {
 public:
  LLMLinkManager(uint64_t cluster_id, int32_t device_id, CommEntityManager *comm_entity_manager,
                 CacheManager *cache_manager, bool remote_cache_accessible)
      : CommLinkManager(cluster_id, remote_cache_accessible),
        cluster_id_(cluster_id),
        aclrt_context_(nullptr),
        msg_handler_(cluster_id, device_id, comm_entity_manager, cache_manager),
        comm_entity_manager_(comm_entity_manager) {};
  ~LLMLinkManager() override = default;
  ge::Status Initialize(const std::map<ge::AscendString, ge::AscendString> &options) override;
  void Finalize() override;
  ge::Status LinkClusters(const std::vector<ClusterInfo> &clusters, std::vector<ge::Status> &rets,
                          int32_t timeout);
  ge::Status UnlinkClusters(const std::vector<ClusterInfo> &clusters, std::vector<ge::Status> &rets,
                            int32_t timeout, bool force_flag = false);
  void UnlinkAllClusters();
  ge::Status SwitchRole(const std::string &role, const std::map<std::string, std::string> &options);

 private:
  uint64_t cluster_id_;
  aclrtContext aclrt_context_;
  LinkMsgHandler msg_handler_;
  std::mutex mutex_;
  std::string listen_port_;
  CommEntityManager *comm_entity_manager_;
};
}  // namespace llm
#endif  // CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_LLM_LINK_MANAGER_H_
