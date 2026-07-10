/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <cinttypes>
#include <cstdio>
#include <string>
#include <vector>

#include "benchmark_config.h"
#include "client_runner.h"
#include "hixl/hixl.h"
#include "server_runner.h"

using hixl_benchmark::BenchmarkConfig;
using hixl_benchmark::BenchmarkConfigParser;
using hixl_benchmark::BenchmarkRole;
using hixl_benchmark::ClientRunner;
using hixl_benchmark::ServerRunner;

std::string FormatSize(uint64_t bytes) {
  using hixl_benchmark::kBytesPerGiB;
  using hixl_benchmark::kBytesPerKiB;
  using hixl_benchmark::kBytesPerMiB;
  using hixl_benchmark::kBytesPerTiB;
  if (bytes >= kBytesPerTiB && bytes % kBytesPerTiB == 0U) {
    return std::to_string(bytes / kBytesPerTiB) + "T";
  }
  if (bytes >= kBytesPerGiB && bytes % kBytesPerGiB == 0U) {
    return std::to_string(bytes / kBytesPerGiB) + "G";
  }
  if (bytes >= kBytesPerMiB && bytes % kBytesPerMiB == 0U) {
    return std::to_string(bytes / kBytesPerMiB) + "M";
  }
  if (bytes >= kBytesPerKiB && bytes % kBytesPerKiB == 0U) {
    return std::to_string(bytes / kBytesPerKiB) + "K";
  }
  return std::to_string(bytes);
}

std::string FormatBlockSizes(const std::vector<uint64_t> &block_sizes) {
  std::string out;
  for (uint64_t block : block_sizes) {
    if (!out.empty()) {
      out += ",";
    }
    out += FormatSize(block);
  }
  return out;
}

std::string FormatPeerCoordPort(const std::string &engine) {
  std::string host;
  uint16_t port = 0;
  if (!hixl_benchmark::ExtractEndpointHostAndPort(engine, host, port)) {
    return "";
  }
  return std::to_string(static_cast<unsigned>(hixl_benchmark::DerivePeerCoordPort(port)));
}

void LogTargetConfig(const BenchmarkConfig &cfg) {
  const std::string peer_coord_port = FormatPeerCoordPort(cfg.local_engine);
  std::printf(
      "[INFO] role=target group=%s transport=%s target_memory=%s device_id=%d local_engine=%s "
      "peer_tcp_port=%s peer_wait_s=%" PRIu32 " peer_count=%" PRIu32 " buffer_size=%s\n",
      cfg.benchmark_group.c_str(), cfg.transport.c_str(), cfg.target_memory_type.c_str(),
      static_cast<int>(cfg.device_id), cfg.local_engine.c_str(), peer_coord_port.c_str(), cfg.peer_wait_sec,
      cfg.peer_count, FormatSize(cfg.buffer_size).c_str());
}

void LogInitiatorConfig(const BenchmarkConfig &cfg) {
  const std::string block_sizes = FormatBlockSizes(cfg.block_sizes);
  const std::string transfer_size = FormatSize(cfg.transfer_size);
  const std::string buffer_size = FormatSize(cfg.buffer_size);
  const std::string peer_coord_port = FormatPeerCoordPort(cfg.remote_engine);
  std::printf(
      "[INFO] role=initiator group=%s transport=%s memory=%s remote_memory=%s op=%s direction=%s device_id=%d "
      "local_engine=%s remote_engine=%s peer_tcp_port=%s "
      "transfer_size=%s buffer_size=%s block_sizes=%s loops=%u\n",
      cfg.benchmark_group.c_str(), cfg.transport.c_str(), cfg.initiator_memory_type.c_str(),
      cfg.target_memory_type.c_str(), cfg.op.c_str(),
      BenchmarkConfig::ComputeDirection(cfg.initiator_memory_type, cfg.target_memory_type, cfg.op).c_str(),
      static_cast<int>(cfg.device_id), cfg.local_engine.c_str(), cfg.remote_engine.c_str(), peer_coord_port.c_str(),
      transfer_size.c_str(), buffer_size.c_str(), block_sizes.c_str(), cfg.loops);
}

void LogCommBenchConfig(const BenchmarkConfig &cfg) {
  if (cfg.role == BenchmarkRole::kServer) {
    LogTargetConfig(cfg);
  } else {
    LogInitiatorConfig(cfg);
  }
  if (cfg.expanded_device_ids.size() > 1U) {
    BenchmarkConfigParser::LogExpandedEndpoints(stdout, cfg);
  }
  if (cfg.role == BenchmarkRole::kClient && cfg.loops == 1U) {
    std::printf(
        "[INFO] loops=1: the first transfer is often warm-up; for steady throughput use the second repeat's "
        "metrics or set loops>1 (--loops|-n).\n");
  }
}

int32_t RunCommBench(const BenchmarkConfig &cfg) {
  if (cfg.expanded_device_ids.empty()) {
    std::fprintf(stderr, "[ERROR] expanded endpoints empty\n");
    return -1;
  }
  if (cfg.role == BenchmarkRole::kServer) {
    ServerRunner server_runner(cfg);
    if (!server_runner.Init()) {
      return -1;
    }
    const int ret = server_runner.Run();
    server_runner.Shutdown();
    return ret;
  }
  ClientRunner client_runner(cfg);
  if (!client_runner.Init()) {
    return -1;
  }
  const int ret = client_runner.Run();
  client_runner.Shutdown();
  return ret;
}

int32_t main(int32_t argc, char **argv) {
  if (argc <= 1) {
    BenchmarkConfigParser::PrintUsage(stderr);
    return -1;
  }
  BenchmarkConfig cfg;
  if (!BenchmarkConfigParser::BuildFromArgv(argc, argv, &cfg)) {
    return -1;
  }
  if (!BenchmarkConfigParser::Validate(&cfg)) {
    return -1;
  }
  if (!BenchmarkConfigParser::ApplyTransportEnvironment(cfg)) {
    return -1;
  }

  LogCommBenchConfig(cfg);
  return RunCommBench(cfg);
}
