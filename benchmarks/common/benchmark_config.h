/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HIXL_BENCHMARK_CONFIG_H
#define HIXL_BENCHMARK_CONFIG_H

#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "hixl/hixl.h"

namespace hixl_benchmark {

constexpr uint64_t kDefaultTotalSize = 134217728ULL;  // 128 MiB
constexpr uint32_t kDefaultBlockSteps = 1U;
constexpr uint32_t kDefaultLoops = 1U;
constexpr uint32_t kTcpClientCountMax = 65535U;

enum class BenchmarkRole { kUnknown, kClient, kServer };

struct BenchmarkConfig {
  BenchmarkRole role = BenchmarkRole::kUnknown;
  int32_t device_id = 0;
  std::string local_engine;
  std::string remote_engine;
  uint16_t tcp_port = 20000;
  /// Parsed from `--tcp_port` (comma-separated); if empty before Validate, set to `{tcp_port}`.
  std::vector<uint16_t> tcp_port_list;
  /// After Validate: same length as expanded_*; per-lane/per-remote TCP coordination port.
  std::vector<uint16_t> expanded_tcp_ports;
  /// Server: wall-clock budget for TCP connect phase (from listen ready), default 30 seconds.
  uint32_t tcp_accept_wait_sec = 30U;
  /// Server: TCP peers to accept before connect phase succeeds (default 1).
  uint32_t tcp_client_count = 1U;
  std::string transfer_mode = "d2d";
  std::string transfer_op = "read";
  bool use_buffer_pool = false;
  bool use_async = false;
  uint32_t async_batch_num = 1U;
  uint32_t connect_timeout_ms = 60000U;
  uint64_t total_size = kDefaultTotalSize;
  uint64_t block_size = kDefaultTotalSize;
  uint32_t block_steps = kDefaultBlockSteps;
  uint32_t loops = kDefaultLoops;
  /// True if --block_size / -k was set on the command line.
  bool block_size_explicit = false;
  /// Parsed from repeatable --hixl_option=KEY=VALUE / -H=KEY=VALUE (KEY is full HIXL option name).
  std::map<std::string, std::string> hixl_init_options;

  /// Comma-separated CLI values (filled after role defaults + overrides; see EnsureEndpointLists).
  std::vector<int32_t> device_id_list;
  std::vector<std::string> local_engine_list;
  std::vector<std::string> remote_engine_list;
  /// Filled by Validate(): length N, broadcast scalars to align lists.
  std::vector<int32_t> expanded_device_ids;
  std::vector<std::string> expanded_local_engines;
  std::vector<std::string> expanded_remote_engines;
};

/// Split on comma; trim ASCII spaces; drop empty segments. IPv6 endpoints should use `[ip]:port`.
std::vector<std::string> SplitCommaList(const std::string &value);

inline std::string ExtractTcpHost(const std::string &remote_engine) {
  const auto pos = remote_engine.find(':');
  if (pos == std::string::npos || pos == 0U) {
    return {};
  }
  return remote_engine.substr(0, pos);
}

class BenchmarkConfigParser {
 public:
  static void PrintUsage(FILE *out);
  static bool BuildFromArgv(int argc, char **argv, BenchmarkConfig *cfg);
  /// Validates cfg and fills expanded_* vectors (same length N).
  static bool Validate(BenchmarkConfig *cfg);
  static void LogExpandedEndpoints(FILE *out, const BenchmarkConfig &cfg);
  static std::map<hixl::AscendString, hixl::AscendString> BuildInitializeOptions(const BenchmarkConfig &cfg);
};

}  // namespace hixl_benchmark

#endif  // HIXL_BENCHMARK_CONFIG_H
