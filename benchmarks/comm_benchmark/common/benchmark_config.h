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

constexpr uint64_t kBytesPerKiB = 1024ULL;
constexpr uint64_t kBytesPerMiB = 1024ULL * kBytesPerKiB;
constexpr uint64_t kBytesPerGiB = 1024ULL * kBytesPerMiB;
constexpr uint64_t kBytesPerTiB = 1024ULL * kBytesPerGiB;
constexpr uint64_t kDefaultTotalSize = 128ULL * kBytesPerMiB;  // 128 MiB
constexpr uint64_t kDefaultBufferSize = kBytesPerGiB;
constexpr uint32_t kDefaultLoops = 1U;
constexpr uint32_t kTcpClientCountMax = 65535U;
constexpr uint32_t kDefaultAcceptWaitSec = 30U;
constexpr uint32_t kMaxAcceptWaitSec = 86400U;
constexpr uint32_t kDefaultConnectTimeoutMs = 60000U;
constexpr uint16_t kDefaultClientEnginePort = 16000U;
constexpr uint16_t kDefaultServerEnginePort = 16001U;

enum class BenchmarkRole { kUnknown, kClient, kServer };

struct BenchmarkConfig {
  BenchmarkRole role = BenchmarkRole::kUnknown;
  std::string role_name;
  std::string benchmark_group = "default";
  std::string output_dir = "output";
  int32_t device_id = 0;
  std::string local_engine;
  std::string remote_engine;
  /// Target: wall-clock budget for peer connect phase (from listen ready), default 30 seconds.
  uint32_t peer_wait_sec = kDefaultAcceptWaitSec;
  /// Target: initiator peers to accept before connect phase succeeds (default 1).
  uint32_t peer_count = 1U;
  std::string op = "read";
  std::string transport = "hccs";
  /// SOC class for HCCS / transport hints. Resolved automatically by ACL probe.
  std::string soc_variant;
  /// Resolved in Validate(): RoCE endpoint placement ("host" on A5 host-NIC DMA, "device" on NPU RoCE NIC).
  /// Drives host-buffer alloc/free path; decouples alloc logic from soc_variant.
  std::string roce_endpoint_placement = "device";
  std::string initiator_memory_type = "device";
  std::string target_memory_type = "device";
  bool use_async = false;
  uint32_t async_batch_num = 1U;
  uint32_t connect_timeout_ms = kDefaultConnectTimeoutMs;
  uint64_t transfer_size = kDefaultTotalSize;
  uint64_t buffer_size = kDefaultBufferSize;
  std::vector<uint64_t> block_sizes;
  uint32_t loops = kDefaultLoops;
  bool memory_explicit = false;
  bool remote_memory_explicit = false;
  bool op_explicit = false;
  /// Parsed from repeatable --hixl_option=KEY=VALUE / -H=KEY=VALUE (KEY is full HIXL option name).
  std::map<std::string, std::string> hixl_init_options;

  /// Compute human-readable direction from initiator/target memory and op type.
  /// Returns "D2rD", "rD2D", "D2rH", "rH2D", "H2rH", "rH2H", "H2rD", "rD2H", or "mix:<write>/<read>".
  static std::string ComputeDirection(const std::string &initiator_mem, const std::string &target_mem,
                                      const std::string &op_type);

  /// Comma-separated CLI values (filled after role defaults + overrides; see EnsureEndpointLists).
  std::vector<int32_t> device_id_list;
  std::vector<std::string> local_engine_list;
  std::vector<std::string> remote_engine_list;
  /// Filled by Validate(): length N, broadcast scalars to align lists.
  std::vector<int32_t> expanded_device_ids;
  std::vector<std::string> expanded_local_engines;
  std::vector<std::string> expanded_remote_engines;
  /// Host RoCE NIC IP address, used to build LocalCommRes when transport=roce.
  std::string host_roce_ip;
  /// Parsed from `--host_roce_ip` (comma-separated); if empty before Validate, set to `{host_roce_ip}`.
  std::vector<std::string> host_roce_ip_list;
  /// After Validate: same length as expanded_*; per-lane RoCE NIC IP.
  std::vector<std::string> expanded_host_roce_ips;
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

bool ExtractEndpointHostAndPort(const std::string &endpoint, std::string &host, uint16_t &port);

uint16_t DerivePeerCoordPort(uint16_t engine_port);

class BenchmarkConfigParser {
 public:
  static void PrintUsage(FILE *out);
  static bool BuildFromArgv(int argc, char **argv, BenchmarkConfig *cfg);
  /// Validates cfg and fills expanded_* vectors (same length N).
  static bool Validate(BenchmarkConfig *cfg);
  static void LogExpandedEndpoints(FILE *out, const BenchmarkConfig &cfg);
  static std::map<hixl::AscendString, hixl::AscendString> BuildInitializeOptions(const BenchmarkConfig &cfg,
                                                                                 size_t lane_index = 0U);
  static bool ApplyTransportEnvironment(const BenchmarkConfig &cfg);
};

}  // namespace hixl_benchmark

#endif  // HIXL_BENCHMARK_CONFIG_H
