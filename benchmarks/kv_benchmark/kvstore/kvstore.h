/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HIXL_KV_BENCHMARK_KVSTORE_H
#define HIXL_KV_BENCHMARK_KVSTORE_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "segment_manager.h"

namespace hixl_kv_benchmark {

struct BufferView {
  std::uintptr_t addr = 0U;
  std::uint64_t size = 0U;
};

struct KeyPlacement {
  std::string key;
  std::uint32_t segment_id = 0U;
  std::uint64_t offset = 0U;
  std::uint64_t size = 0U;
};

class KvStore {
 public:
  explicit KvStore(SegmentManager segment_manager);

  bool EnsurePlacements(const std::vector<std::string> &keys, const std::vector<BufferView> &buffers);

  const std::map<std::string, KeyPlacement> &Placements() const { return placements_; }

 private:
  bool CheckInput(const std::vector<std::string> &keys, const std::vector<BufferView> &buffers) const;
  bool EnsurePlacement(const std::string &key, std::uint64_t size, KeyPlacement *placement);

  std::uint32_t ResolveSegmentForKeyGroup(const std::string &group_key);

  SegmentManager segment_manager_;
  std::map<std::string, KeyPlacement> placements_;
  /// One round-robin assigned segment per KV block (key); all slices of that key use it.
  std::map<std::string, std::uint32_t> key_group_segments_;
  std::uint32_t next_segment_ = 0U;
};

}  // namespace hixl_kv_benchmark

#endif  // HIXL_KV_BENCHMARK_KVSTORE_H
