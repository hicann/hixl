/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <cinttypes>
#include <cstdio>
#include <map>
#include <string>

#include "common/benchmark_config.h"
#include "common/client_runner.h"
#include "common/server_runner.h"
#include "hixl/hixl.h"

using hixl::AscendString;
using hixl_benchmark::BenchmarkConfig;
using hixl_benchmark::BenchmarkConfigParser;
using hixl_benchmark::BenchmarkRole;
using hixl_benchmark::ClientRunner;
using hixl_benchmark::ServerRunner;

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

  std::printf(
      "[INFO] role=%s device_id=%d local_engine=%s remote_engine=%s tcp_port=%u tcp_accept_wait_s=%" PRIu32
      " tcp_client_count=%" PRIu32 " transfer_mode=%s transfer_op=%s "
      "use_buffer_pool=%s total_size=%" PRIu64 " block_size=%" PRIu64 " block_steps=%u loops=%u\n",
      cfg.role == BenchmarkRole::kClient ? "client" : "server", static_cast<int>(cfg.device_id), cfg.local_engine.c_str(),
      cfg.remote_engine.c_str(), static_cast<unsigned>(cfg.tcp_port), cfg.tcp_accept_wait_sec, cfg.tcp_client_count,
      cfg.transfer_mode.c_str(), cfg.transfer_op.c_str(), cfg.use_buffer_pool ? "true" : "false", cfg.total_size,
      cfg.block_size, cfg.block_steps, cfg.loops);
  BenchmarkConfigParser::LogExpandedEndpoints(stdout, cfg);

  if (cfg.loops == 1U) {
    std::printf(
        "[INFO] loops=1: the first transfer is often warm-up; for steady throughput use the second repeat's "
        "metrics or set loops>1 (--loops|-n).\n");
  }
  {
    const std::map<AscendString, AscendString> eff = BenchmarkConfigParser::BuildInitializeOptions(cfg);
    if (eff.empty()) {
      std::printf("[INFO] hixl_init_options (effective): none\n");
    } else {
      std::printf("[INFO] hixl_init_options (effective):\n");
      for (const auto &p : eff) {
        std::printf("[INFO]   %s=%s\n", p.first.GetString(), p.second.GetString());
      }
    }
  }

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
