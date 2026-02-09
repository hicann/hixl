/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HIXL_SRC_LLM_DATADIST_TRANSFER_ENGINE_TRANSFER_ENGINE_H_
#define HIXL_SRC_LLM_DATADIST_TRANSFER_ENGINE_TRANSFER_ENGINE_H_

#include <memory>
#include <string>
#include <vector>
#include "llm_datadist/llm_error_codes.h"
#include "common/llm_inner_types.h"
#include "hccl/hccl_mem_comm.h"
#include "link_mgr/comm_entity_manager.h"
#include "cache_mgr/cache_manager.h"

namespace llm {
class TransferEngine {
 public:
  TransferEngine(uint64_t cluster_id) : cluster_id_(cluster_id) {};
  virtual ~TransferEngine() = default;
  virtual ge::Status Initialize(const std::map<ge::AscendString, ge::AscendString> &options) = 0;
  virtual void Finalize() = 0;

  virtual ge::Status RegisterMem(void *addr, uint64_t size, HcclMemType type, void *&handle) = 0;
  virtual ge::Status UnregisterMem(void *handle) = 0;

  virtual ge::Status LinkClusters(const std::vector<ClusterInfo> &clusters, std::vector<ge::Status> &rets,
                                  int32_t timeout) = 0;
  virtual ge::Status UnlinkClusters(const std::vector<ClusterInfo> &clusters, std::vector<ge::Status> &rets,
                                    int32_t timeout, bool force_flag = false) = 0;
  virtual ge::Status Link(std::string &cluster_name, const std::map<uint64_t, uint32_t> &cluster2rank, std::string &rank_table,
                          uint64_t &comm_id) = 0;
  virtual ge::Status Unlink(uint64_t comm_id) = 0;
  virtual void UnlinkAllClusters() = 0;

  virtual ge::Status QueryRegisterMemStatus(uint64_t comm_id, RegisterMemoryStatus &status) = 0;
  virtual ge::Status SwitchRole(const std::string &role, const std::map<std::string, std::string> &options) = 0;

  void SetCommEntityManager(CommEntityManager *comm_entity_manager);
  void SetCacheManager(CacheManager *cache_manager);

 protected:
  uint64_t cluster_id_;
  CommEntityManager *comm_entity_manager_{nullptr};
  CacheManager *cache_manager_{nullptr};
};

class TransferEngineFactory {
 public:
  static std::unique_ptr<TransferEngine> Create(const std::map<ge::AscendString, ge::AscendString> &options, uint64_t cluster_id);
};
}  // namespace llm
#endif  // HIXL_SRC_LLM_DATADIST_TRANSFER_ENGINE_TRANSFER_ENGINE_H_
