/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "kvstore.h"

#include <stdexcept>

#include "kv_slice_layout.h"

namespace hixl_kv_benchmark {

KvStore::KvStore(SegmentManager segment_manager)
    : segment_manager_(std::move(segment_manager)) {}

bool KvStore::CheckInput(const std::vector<std::string> &keys, const std::vector<BufferView> &buffers) const {
  if (keys.empty() || keys.size() != buffers.size()) {
    return false;
  }
  for (const auto &buffer : buffers) {
    if (buffer.addr == 0U || buffer.size == 0U) {
      return false;
    }
  }
  return true;
}

std::uint32_t KvStore::ResolveSegmentForKeyGroup(const std::string &group_key) {
  const auto it = key_group_segments_.find(group_key);
  if (it != key_group_segments_.end()) {
    return it->second;
  }
  const auto segment_count = static_cast<std::uint32_t>(segment_manager_.Segments().size());
  if (segment_count == 0U) {
    throw std::runtime_error("no segments available for key placement");
  }
  const auto segment_id = next_segment_ % segment_count;
  ++next_segment_;
  key_group_segments_[group_key] = segment_id;
  return segment_id;
}

bool KvStore::EnsurePlacement(const std::string &key, std::uint64_t size, KeyPlacement *placement) {
  const auto it = placements_.find(key);
  if (it != placements_.end()) {
    *placement = it->second;
    return it->second.size == size;
  }
  const auto segment_count = static_cast<std::uint32_t>(segment_manager_.Segments().size());
  if (segment_count == 0U) {
    return false;
  }
  const auto group_key = ExtractKeyPlacementGroupKey(key);
  const auto segment_id = ResolveSegmentForKeyGroup(group_key);
  const auto allocation = segment_manager_.AllocateFrom(segment_id, size);
  if (!allocation.has_value()) {
    return false;
  }
  *placement = KeyPlacement{key, allocation->segment_id, allocation->offset, allocation->size};
  placements_[key] = *placement;
  return true;
}

bool KvStore::EnsurePlacements(const std::vector<std::string> &keys, const std::vector<BufferView> &buffers) {
  if (!CheckInput(keys, buffers)) {
    return false;
  }
  for (std::size_t i = 0; i < keys.size(); ++i) {
    KeyPlacement placement;
    if (!EnsurePlacement(keys[i], buffers[i].size, &placement)) {
      return false;
    }
  }
  return true;
}

}  // namespace hixl_kv_benchmark
