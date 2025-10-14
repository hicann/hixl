/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_COMMON_H_
#define CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_COMMON_H_

#include <vector>
#include <map>
#include "ge/ge_api_types.h"
#include "common/llm_inner_types.h"
namespace llm {
constexpr const char LLM_OPTION_MEM_POOL_CONFIG[] = "llm.MemPoolConfig";
constexpr const char LLM_OPTION_HOST_MEM_POOL_CONFIG[] = "llm.HostMemPoolConfig";

enum class FsmState : int32_t {
  FSM_INIT_STATE = 0,
  FSM_IDLE_STATE,
  FSM_RECEIVE_STATE,
  FSM_SEND_STATE,
  FSM_ERROR_STATE,
  FSM_DESTROYED_STATE,
  FSM_INVALID_STATE
};

struct BufferInfo {
  uint64_t block_start_index;
  uint64_t buffer_len;
};

union TransferInfo {
  BufferInfo buffer_info;
  void *dst_addr;
};

struct TransferCacheReq {
  uint32_t is_pull_block = 0U;
  uint32_t num_tensors = 0U;
  int64_t cache_id = -1L;
  uint64_t batch_index = 0UL;
  uint64_t req_id = 0UL;
  uint64_t prefix_id = UINT64_MAX;
  uint64_t model_id = 0UL;
  uint64_t block_size = 0U;
  uint64_t pull_size = 0UL;
  uint64_t max_block_index = 0;
  int32_t dst_placement = 0;  // CachePlacement, 1: device, 0: host
  int32_t timeout_in_ms = 1000;
  uint32_t dst_addr_count = 0U;
  uint64_t dst_buffer_size = 0UL;
  uint32_t buffer_info_count = 0U; // block index num or cache num
  uint32_t src_tensor_indices_size = 0U;  // used by pull with layer
  uint32_t src_tensor_start_index = 0U;   // used by pull with layer
  uint64_t req_size = 0U;
  TransferInfo transfer_infos[];
};

struct ResponseInfo {
  uint64_t req_id;
  uint64_t model_id;
  int32_t ret_code;
  uint32_t transfer_count;
  uint32_t block_size;
  uint64_t sync_flag_addresses[];
};

struct CacheEntry {
  uint64_t num_blocks = 0U; // > 0 means is blocks when cache_mem_type is not MIX
  uint32_t batch_size;
  int32_t seq_len_dim_index = -1;
  uint64_t tensor_size;
  uint64_t stride; // batch stride or block size
  int32_t ext_ref_count = 0;
  CachePlacement placement;
  std::vector<std::shared_ptr<void>> cache_addrs;
  std::map<uint64_t, std::pair<uint32_t, uint64_t>> id_to_batch_index_and_size;
  bool is_owned = false;
  bool remote_accessible = true;
  CacheMemType cache_mem_type = CacheMemType::CACHE;
};
}  // namespace llm

#endif  // CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_COMMON_H_
