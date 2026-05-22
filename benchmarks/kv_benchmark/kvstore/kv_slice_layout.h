/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HIXL_KV_BENCHMARK_KV_SLICE_LAYOUT_H
#define HIXL_KV_BENCHMARK_KV_SLICE_LAYOUT_H

#include <cstdint>
#include <string>
#include <vector>

#include "kvstore.h"
#include "model_config.h"

namespace hixl_kv_benchmark {

struct KvSliceEntry {
  std::string placement_key;
  BufferView buffer;
};

std::string BuildSlicePlacementKey(std::uint32_t rank, std::uint64_t token_length, std::uint64_t key_index,
                                 std::uint32_t layer_index, const std::string &cache_name, bool shared);

/// Group key for placement: all slices of one KV block share the same segment.
std::string ExtractKeyPlacementGroupKey(const std::string &slice_placement_key);

std::vector<KvSliceEntry> BuildWorkloadSlicePlan(std::uintptr_t buffer_base, std::uint32_t rank,
                                                 std::uint64_t token_length, std::uint64_t key_count,
                                                 const ModelSpec &model);

}  // namespace hixl_kv_benchmark

#endif  // HIXL_KV_BENCHMARK_KV_SLICE_LAYOUT_H
