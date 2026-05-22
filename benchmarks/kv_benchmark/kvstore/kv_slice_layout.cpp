/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "kv_slice_layout.h"

#include <limits>
#include <stdexcept>

namespace hixl_kv_benchmark {
namespace {

bool WillAddOverflowUInt64(std::uint64_t lhs, std::uint64_t rhs) {
  return lhs > (std::numeric_limits<std::uint64_t>::max() - rhs);
}

}  // namespace

std::string BuildSlicePlacementKey(std::uint32_t rank, std::uint64_t token_length, std::uint64_t key_index,
                                   std::uint32_t layer_index, const std::string &cache_name, bool shared) {
  const std::string suffix = "_L" + std::to_string(layer_index) + "_" + cache_name;
  if (shared) {
    return "tokens" + std::to_string(token_length) + "_key" + std::to_string(key_index) + suffix;
  }
  return "rank" + std::to_string(rank) + "_tokens" + std::to_string(token_length) + "_key" +
         std::to_string(key_index) + suffix;
}

std::string ExtractKeyPlacementGroupKey(const std::string &slice_placement_key) {
  const auto layer_marker = slice_placement_key.find("_L");
  if (layer_marker == std::string::npos || layer_marker == 0U) {
    throw std::invalid_argument("invalid slice placement key: " + slice_placement_key);
  }
  return slice_placement_key.substr(0, layer_marker);
}

std::vector<KvSliceEntry> BuildWorkloadSlicePlan(const std::uintptr_t buffer_base, const std::uint32_t rank,
                                                   const std::uint64_t token_length, const std::uint64_t key_count,
                                                   const ModelSpec &model) {
  std::vector<KvSliceEntry> entries;
  std::uint64_t offset = 0U;
  for (std::uint64_t key_index = 0U; key_index < key_count; ++key_index) {
    std::vector<KvCacheSlice> slices;
    model.CollectCacheSlicesForKey(key_index, &slices);
    for (const auto &slice : slices) {
      KvSliceEntry entry;
      entry.placement_key =
          BuildSlicePlacementKey(rank, token_length, key_index, slice.layer_index, slice.cache_name, model.IsShared());
      if (WillAddOverflowUInt64(offset, slice.size_bytes)) {
        throw std::overflow_error("KV slice layout offset overflow");
      }
      entry.buffer.addr = buffer_base + static_cast<std::uintptr_t>(offset);
      entry.buffer.size = slice.size_bytes;
      offset += slice.size_bytes;
      entries.push_back(entry);
    }
  }
  return entries;
}

}  // namespace hixl_kv_benchmark
