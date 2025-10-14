/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "llm_datadist/llm_error_codes.h"

// register error desc
namespace ge {
namespace {
GE_ERRORNO_EXTERNAL(LLM_WAIT_PROC_TIMEOUT, "request wait to be processed timeout!");
GE_ERRORNO_EXTERNAL(LLM_KV_CACHE_NOT_EXIST, "kv cache not exit!");
GE_ERRORNO_EXTERNAL(LLM_REPEAT_REQUEST, "repeat request!");
GE_ERRORNO_EXTERNAL(LLM_REQUEST_ALREADY_COMPLETED, "request already complete!");
GE_ERRORNO_EXTERNAL(LLM_PARAM_INVALID, "parameter is invalid!");
GE_ERRORNO_EXTERNAL(LLM_ENGINE_FINALIZED, "llm engine finalized!");
GE_ERRORNO_EXTERNAL(LLM_NOT_YET_LINK, "local cluster is not linked with remote cluster!");
GE_ERRORNO_EXTERNAL(LLM_ALREADY_LINK, "local cluster is already linked with remote cluster!");
GE_ERRORNO_EXTERNAL(LLM_LINK_FAILED, "local cluster link with remote cluster failed!");
GE_ERRORNO_EXTERNAL(LLM_UNLINK_FAILED, "local cluster unlink with remote cluster failed!");
GE_ERRORNO_EXTERNAL(LLM_NOTIFY_PROMPT_UNLINK_FAILED, "local cluster notify remote cluster do unlink failed!");
GE_ERRORNO_EXTERNAL(LLM_CLUSTER_NUM_EXCEED_LIMIT, "cluster num exceed limit!");
GE_ERRORNO_EXTERNAL(LLM_PROCESSING_LINK, "link is current processing, try again later!");
GE_ERRORNO_EXTERNAL(LLM_DEVICE_OUT_OF_MEMORY, "device out of memory!");
GE_ERRORNO_EXTERNAL(LLM_PREFIX_ALREADY_EXIST, "Prefix has already existed.");
GE_ERRORNO_EXTERNAL(LLM_PREFIX_NOT_EXIST, "Prefix does not exist.");
GE_ERRORNO_EXTERNAL(LLM_SEQ_LEN_OVER_LIMIT, "Sequence length exceed limit.");
GE_ERRORNO_EXTERNAL(LLM_NO_FREE_BLOCK, "No free block.");
GE_ERRORNO_EXTERNAL(LLM_BLOCKS_OUT_OF_MEMORY, "Block is out of memory.");
GE_ERRORNO_EXTERNAL(LLM_EXIST_LINK, "Link existed when switching role, unlink before switch.");
GE_ERRORNO_EXTERNAL(LLM_FEATURE_NOT_ENABLED, "Feature is not enabled.");
GE_ERRORNO_EXTERNAL(LLM_TIMEOUT, "Process timeout.");
GE_ERRORNO_EXTERNAL(LLM_LINK_BUSY, "Current link is busy.");
GE_ERRORNO_EXTERNAL(LLM_OUT_OF_MEMORY, "Out of memory.");
}  // namespace
}  // namespace ge
