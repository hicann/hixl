/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "transfer_engine.h"
#include "llm_datadist/llm_datadist.h"
#include "common/llm_log.h"
#include "hccl_transfer_engine.h"
#include "hixl_transfer_engine.h"

namespace llm {
namespace {
constexpr const char kOptionHixlTransferBackend[] = "hixl";
}

void TransferEngine::SetCommEntityManager(CommEntityManager *comm_entity_manager) {
  comm_entity_manager_ = comm_entity_manager;
}

void TransferEngine::SetCacheManager(CacheManager *cache_manager) {
  cache_manager_ = cache_manager;
}

std::unique_ptr<TransferEngine> TransferEngineFactory::Create(
    const std::map<ge::AscendString, ge::AscendString> &options, uint64_t cluster_id) {
  const auto iter = options.find(llm_datadist::OPTION_TRANSFER_BACKEND);
  if (iter == options.cend()) {
    return MakeUnique<HcclTransferEngine>(cluster_id);
  }
  const std::string &backend = iter->second.GetString();
  if (backend != kOptionHixlTransferBackend) {
    LLMLOGE(ge::LLM_PARAM_INVALID, "Transfer backend:%s is not supported.", backend.c_str());
    return nullptr;
  }
  return MakeUnique<HixlTransferEngine>(cluster_id);
}
}  // namespace llm
