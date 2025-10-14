/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef LLM_ENGINE_INC_EXTERNAL_LLM_ENGINE_TYPES_H
#define LLM_ENGINE_INC_EXTERNAL_LLM_ENGINE_TYPES_H

namespace llm {
constexpr const char kPrompt[] = "Prompt";
constexpr const char kDecoder[] = "Decoder";

constexpr const char LLM_OPTION_CLUSTER_INFO[] = "llm.ClusterInfo";
constexpr const char LLM_OPTION_ROLE[] = "llm.Role";
constexpr const char LLM_OPTION_SYNC_KV_CACHE_WAIT_TIME[] = "llm.SyncKvCacheWaitTime";
constexpr const char LLM_OPTION_OUTPUT_MAX_SIZE[] = "llm.OutputMaxSize";
constexpr const char LLM_OPTION_ENABLE_SWITCH_ROLE[] = "llm.EnableSwitchRole";
constexpr const char LLM_OPTION_BUF_POOL_CFG[] = "llm.BufPoolCfg";
}  // namespace llm

#endif  // LLM_ENGINE_INC_EXTERNAL_LLM_ENGINE_TYPES_H
