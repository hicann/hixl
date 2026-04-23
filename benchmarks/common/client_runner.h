/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HIXL_CLIENT_RUNNER_H
#define HIXL_CLIENT_RUNNER_H

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "benchmark_config.h"
#include "hixl/hixl.h"
#include "tcp_client_server.h"

namespace hixl_benchmark {

namespace detail {

enum class BenchWorkerTag : std::uint8_t { kSingle = 0, kLane, kRemote };

/// One successful block-step timing (filled during transfer; printed after worker threads join).
struct TransferBenchRecord {
  BenchWorkerTag tag = BenchWorkerTag::kSingle;
  std::size_t worker_index = 0;
  std::uint32_t loop_plus_one = 0;
  std::uint32_t loops_total = 0;
  std::uint32_t step_index = 0;
  std::uint32_t block_size = 0;
  std::uint32_t trans_num = 0;
  std::uint32_t async_batch_num = 0;
  std::int64_t time_us = 0;
  std::int64_t submit_time_us = 0;
  std::int64_t wait_time_us = 0;
  double throughput_gbps = 0;
};

/// Per-lane client state (multi local engine). Owned by `ClientRunner` until `Shutdown` / post-join cleanup.
struct LaneState {
  hixl::Hixl hixl;
  void *buffer = nullptr;
  hixl::MemHandle mem_handle = nullptr;
  bool is_host = false;
  bool need_register = false;
  bool hixl_initialized = false;
  bool hixl_connected = false;
  TCPClient tcp_client;
  std::vector<TransferBenchRecord> bench_records;
};

}  // namespace detail

/// Client benchmark: `Init()` 绑定 device（多 lane 拓扑除外）；`Run()` 执行业务；`Shutdown()` 统一回收并复位 device。
class ClientRunner {
 public:
  explicit ClientRunner(const BenchmarkConfig &cfg) : cfg_(cfg) {}
  ~ClientRunner();

  ClientRunner(const ClientRunner &) = delete;
  ClientRunner &operator=(const ClientRunner &) = delete;

  bool Init();
  int Run();
  void Shutdown();

 private:
  const BenchmarkConfig &cfg_;
  bool device_bound_ = false;
  int32_t device_id_ = 0;

  hixl::Hixl lane_hixl_;
  void *lane_buffer_ = nullptr;
  hixl::MemHandle lane_mem_handle_ = nullptr;
  bool lane_is_host_ = false;
  bool lane_need_register_ = false;
  bool lane_hixl_initialized_ = false;
  bool lane_hixl_connected_ = false;
  bool lane_resources_active_ = false;

  TCPClient lane_tcp_;
  bool lane_tcp_handshake_ok_ = false;
  bool lane_need_tcp_notify_ = false;

  /// When true, each `SharedRemoteWorker` already called `Disconnect` for its peer; skip bulk disconnect in cleanup.
  bool lane_shared_multi_remote_workers_disconnected_ = false;

  std::vector<std::unique_ptr<detail::LaneState>> lane_runtimes_;
  std::vector<std::thread> multi_lane_threads_;

  std::mutex remote_mutex_map_mu_;
  std::map<std::string, std::unique_ptr<std::mutex>> remote_mutexes_;

  std::mutex *GetOrCreateRemoteMutex(const std::string &remote);

  void ReleaseLaneResources();
  void ReleaseAllLaneRuntimes();
  int RunOnePair(const std::string &remote, void *src_slice, size_t register_len);
  int RunSingleDevice();
  int RunSharedMultiRemote();
  int RunMultiLane();
  int RunClientSharedRemoteWorkers();
  int RunClientLaneWorkers();
};

}  // namespace hixl_benchmark

#endif  // HIXL_CLIENT_RUNNER_H
