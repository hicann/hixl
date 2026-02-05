/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HIXL_SRC_LLM_DATADIST_TRANSFER_ENGINE_HIXL_TRANSFER_ENGINE_H_
#define HIXL_SRC_LLM_DATADIST_TRANSFER_ENGINE_HIXL_TRANSFER_ENGINE_H_

#include "transfer_engine.h"
#include "engine/engine.h"

namespace llm {
class HixlTransferEngine : public TransferEngine {
 public:
  HixlTransferEngine(uint64_t cluster_id) : TransferEngine(cluster_id) {};
  ~HixlTransferEngine() override = default;
  ge::Status Initialize(const std::map<ge::AscendString, ge::AscendString> &options) override;
  void Finalize() override;

  ge::Status RegisterMem(void *addr, uint64_t size, HcclMemType type, void *&handle) override;
  ge::Status UnregisterMem(void *handle) override;

  ge::Status LinkClusters(const std::vector<ClusterInfo> &clusters, std::vector<ge::Status> &rets,
                          int32_t timeout) override;
  ge::Status UnlinkClusters(const std::vector<ClusterInfo> &clusters, std::vector<ge::Status> &rets,
                                    int32_t timeout, bool force_flag = false) override;
  ge::Status Link(std::string &cluster_name, const std::map<uint64_t, uint32_t> &cluster2rank, std::string &rank_table,
                  uint64_t &comm_id) override;
  ge::Status Unlink(uint64_t comm_id) override;
  ge::Status QueryRegisterMemStatus(uint64_t comm_id, RegisterMemoryStatus &status) override;
  ge::Status SwitchRole(const std::string &role, const std::map<std::string, std::string> &options) override;

 private:
  ge::Status LinkCluster(const ClusterInfo &cluster, int32_t timeout);
  ge::Status UnlinkCluster(const ClusterInfo &cluster, int32_t timeout);
  void LLMDataDist2HixlOptions(const std::map<ge::AscendString, ge::AscendString> &llm_datdsist_options,
                               std::map<ge::AscendString, ge::AscendString> &hixl_options);
  ge::Status InitMsgProcessor();

  aclrtContext rt_context_;
  std::string local_engine_;
  std::unique_ptr<hixl::Engine> engine_ = nullptr;
};
}  // namespace llm
#endif  // HIXL_SRC_LLM_DATADIST_TRANSFER_ENGINE_HIXL_TRANSFER_ENGINE_H_
