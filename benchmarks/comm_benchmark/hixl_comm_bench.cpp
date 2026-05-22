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
#include <cstdlib>
#include <cstdio>
#include <map>
#include <string>

#include "benchmark_config.h"
#include "client_runner.h"
#include "hixl/hixl.h"
#include "server_runner.h"

using hixl::AscendString;
using hixl_benchmark::BenchmarkConfig;
using hixl_benchmark::BenchmarkConfigParser;
using hixl_benchmark::BenchmarkRole;
using hixl_benchmark::ClientRunner;
using hixl_benchmark::ServerRunner;

void LogCommBenchConfig(const BenchmarkConfig &cfg) {
  std::printf(
      "[INFO] role=%s group=%s transport=%s initiator_memory=%s target_memory=%s device_id=%d "
      "local_engine=%s remote_engine=%s "
      "tcp_port=%u tcp_accept_wait_s=%" PRIu32 " tcp_client_count=%" PRIu32 " transfer_op=%s direction=%s "
      "total_size=%" PRIu64 " buffer_size=%" PRIu64 " block_size=%" PRIu64 " block_steps=%u loops=%u\n",
      cfg.role_name.empty() ? (cfg.role == BenchmarkRole::kClient ? "client" : "server") : cfg.role_name.c_str(),
      cfg.benchmark_group.c_str(), cfg.transport.c_str(),
      cfg.initiator_memory_type.c_str(), cfg.target_memory_type.c_str(),
      static_cast<int>(cfg.device_id), cfg.local_engine.c_str(), cfg.remote_engine.c_str(),
      static_cast<unsigned>(cfg.tcp_port), cfg.tcp_accept_wait_sec, cfg.tcp_client_count, cfg.transfer_op.c_str(),
      BenchmarkConfig::ComputeDirection(cfg.initiator_memory_type, cfg.target_memory_type, cfg.transfer_op).c_str(),
      cfg.total_size, cfg.buffer_size, cfg.block_size, cfg.block_steps, cfg.loops);
  std::printf("[INFO] HCCL_INTRA_ROCE_ENABLE=%s\n", std::getenv("HCCL_INTRA_ROCE_ENABLE"));
  BenchmarkConfigParser::LogExpandedEndpoints(stdout, cfg);
  if (cfg.loops == 1U) {
    std::printf(
        "[INFO] loops=1: the first transfer is often warm-up; for steady throughput use the second repeat's "
        "metrics or set loops>1 (--loops|-n).\n");
  }
  const std::map<AscendString, AscendString> effective_options = BenchmarkConfigParser::BuildInitializeOptions(cfg);
  if (effective_options.empty()) {
    std::printf("[INFO] hixl_init_options (effective): none\n");
  } else {
    std::printf("[INFO] hixl_init_options (effective):\n");
    for (const auto &option : effective_options) {
      std::printf("[INFO]   %s=%s\n", option.first.GetString(), option.second.GetString());
    }
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
