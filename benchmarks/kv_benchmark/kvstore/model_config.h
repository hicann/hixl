/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HIXL_KV_BENCHMARK_MODEL_CONFIG_H
#define HIXL_KV_BENCHMARK_MODEL_CONFIG_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace hixl_kv_benchmark {

constexpr std::uint32_t kDefaultTokensPerKey = 128U;

struct CacheSpec {
  std::string name;
  std::size_t bytes_per_key = 0U;
  std::uint64_t max_key_count = 0U;

  std::uint64_t BytesForKey(std::uint64_t key_index) const;
  std::uint64_t BytesForKeys(std::uint64_t key_count) const;
};

struct LayerSpec {
  std::vector<CacheSpec> caches;
};

/// One transferable KV cache tensor slice (layer + cache name + payload size).
struct KvCacheSlice {
  std::uint64_t key_index = 0U;
  std::uint32_t layer_index = 0U;
  std::string cache_name;
  std::uint64_t size_bytes = 0U;
};

struct ModelSpec {
  std::string name;
  std::vector<LayerSpec> layers;
  std::uint32_t tokens_per_key = kDefaultTokensPerKey;
  std::string kv_strategy = "independent";

  std::size_t BytesPerKey() const;
  std::uint64_t BytesForKey(std::uint64_t key_index) const;
  std::uint64_t BytesForKeys(std::uint64_t key_count) const;
  /// Total bytes transferred for all keys (respects max_key_count, e.g. DS-V4 SWA once per layer).
  std::uint64_t TransferBytesForKeys(std::uint64_t key_count) const;
  void CollectCacheSlicesForKey(std::uint64_t key_index, std::vector<KvCacheSlice> *out) const;
  std::uint64_t MaxSliceBytesForKeys(std::uint64_t key_count) const;
  std::uint64_t CountTransferSlicesForKeys(std::uint64_t key_count) const;
  bool IsShared() const { return kv_strategy == "shared"; }
};

std::vector<ModelSpec> LoadModelSpecsFromJson(const std::string &config_path);
const ModelSpec *FindModelSpec(const std::vector<ModelSpec> &models, const std::string &name);
std::vector<std::string> SupportedModelNames(const std::vector<ModelSpec> &models);
std::uint64_t ParseTokenLength(const std::string &token_length);

}  // namespace hixl_kv_benchmark

#endif  // HIXL_KV_BENCHMARK_MODEL_CONFIG_H
