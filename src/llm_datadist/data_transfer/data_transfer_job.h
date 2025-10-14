/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_GRAPH_ENGINE_RUNTIME_LLM_ENGINE_V2_DATA_TRANSFER_DATA_TRANSFER_JOB_H_
#define CANN_GRAPH_ENGINE_RUNTIME_LLM_ENGINE_V2_DATA_TRANSFER_DATA_TRANSFER_JOB_H_

#include <future>
#include "llm_datadist/llm_error_codes.h"
#include "ge_common/ge_api_types.h"
#include "link_mgr/comm_entity.h"

namespace llm {
class DataTransferJob {
 public:
  DataTransferJob() = default;
  virtual ~DataTransferJob() = default;
  // read request, parse parameters
  virtual ge::Status Initialize(const CacheEntry &cache_entry, CommEntity &comm_entity, uint64_t offset) = 0;
  virtual ge::Status Process(bool &is_done) = 0;
  virtual ge::Status PullCache() {
    return ge::LLM_FEATURE_NOT_ENABLED;
  }
};
}  // namespace
#endif  // CANN_GRAPH_ENGINE_RUNTIME_LLM_ENGINE_V2_DATA_TRANSFER_DATA_TRANSFER_JOB_H_
