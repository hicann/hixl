/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HIXL_SRC_LLM_DATADIST_LINK_MGR_HIXL_ENTITY_H_
#define HIXL_SRC_LLM_DATADIST_LINK_MGR_HIXL_ENTITY_H_

#include "llm_datadist/llm_datadist.h"
#include "engine/engine.h"
#include "comm_entity.h"

namespace llm {
struct CacheTableInfo {
  uint64_t cache_table_addr;
  uint64_t cache_table_size;
};

class HixlEntity : public CommEntity {
 public:
  HixlEntity(const std::string &remote_ip, uint32_t remote_port, hixl::Engine *engine)
      : CommEntity(0U, 0U, 0U, 0U, 0U), remote_ip_(remote_ip), remote_port_(remote_port), engine_(engine) {}
  ~HixlEntity() override = default;
  ge::Status Initialize(int32_t timeout_ms);
  ge::Status Finalize() override;
  ge::Status BatchTransfer(std::list<HcclOneSideOpDesc> &tasks, bool is_put, bool reversed, int32_t timeout_ms) override;

 private:
  ge::Status RecvCacheTableResp(int32_t fd, CacheTableInfo &cache_table_info, int32_t timeout_ms);

  std::string remote_ip_;
  uint32_t remote_port_ = 0U;
  std::string remote_engine_;
  hixl::Engine *engine_ = nullptr;
};
}  // namespace llm
#endif  // HIXL_SRC_LLM_DATADIST_LINK_MGR_HIXL_ENTITY_H_
