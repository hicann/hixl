/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "common/llm_common.h"
#include <set>
#include "llm_datadist/llm_error_codes.h"
#include "common/llm_log.h"

namespace llm {
namespace {
const std::set<ge::Status> kLlmStatus = {ge::SUCCESS,
                                         ge::FAILED,
                                         ge::LLM_WAIT_PROC_TIMEOUT,
                                         ge::LLM_KV_CACHE_NOT_EXIST,
                                         ge::LLM_REPEAT_REQUEST,
                                         ge::LLM_REQUEST_ALREADY_COMPLETED,
                                         ge::LLM_PARAM_INVALID,
                                         ge::LLM_ENGINE_FINALIZED,
                                         ge::LLM_NOT_YET_LINK,
                                         ge::LLM_ALREADY_LINK,
                                         ge::LLM_LINK_FAILED,
                                         ge::LLM_UNLINK_FAILED,
                                         ge::LLM_NOTIFY_PROMPT_UNLINK_FAILED,
                                         ge::LLM_CLUSTER_NUM_EXCEED_LIMIT,
                                         ge::LLM_PROCESSING_LINK,
                                         ge::LLM_DEVICE_OUT_OF_MEMORY,
                                         ge::LLM_PREFIX_ALREADY_EXIST,
                                         ge::LLM_PREFIX_NOT_EXIST,
                                         ge::LLM_SEQ_LEN_OVER_LIMIT,
                                         ge::LLM_NO_FREE_BLOCK,
                                         ge::LLM_BLOCKS_OUT_OF_MEMORY,
                                         ge::LLM_EXIST_LINK,
                                         ge::LLM_FEATURE_NOT_ENABLED,
                                         ge::LLM_LINK_BUSY,
                                         ge::LLM_OUT_OF_MEMORY
};
}
ge::Status TransRetToLlmCodes(const ge::Status &ret) {
  if (kLlmStatus.find(ret) == kLlmStatus.cend()) {
    return ge::FAILED;
  }
  return ret;
}
}  // namespace llm
