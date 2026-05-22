/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <limits>
#include <map>
#include <numeric>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "acl/acl.h"
#include "fabric_mem/fabric_mem_transfer_service.h"
#include "hixl/hixl.h"
#include "kv_transfer_executor.h"
#include "kvstore.h"
#include "kv_slice_layout.h"
#include "model_config.h"

namespace {

constexpr std::uint64_t kBytesPerKiB = 1024ULL;
constexpr std::uint64_t kBytesPerMiB = kBytesPerKiB * 1024ULL;
constexpr std::uint64_t kBytesPerGiB = kBytesPerMiB * 1024ULL;
/// Bandwidth columns use decimal GB/s (10^9 bytes per GB).
constexpr double kDecimalGigaFactor = 1000.0;
constexpr double kDecimalBytesPerGb = kDecimalGigaFactor * kDecimalGigaFactor * kDecimalGigaFactor;
constexpr double kMicrosecondsPerSecond = kDecimalGigaFactor * kDecimalGigaFactor;
constexpr double kNanosecondsPerMicrosecond = kDecimalGigaFactor;
constexpr double kFormatSizeWholeNumberThreshold = 10.0;
constexpr std::uint32_t kFormatSizeWholePrecision = 0U;
constexpr std::uint32_t kFormatSizeFractionPrecision = 2U;
constexpr std::uint32_t kSyncPollIntervalMs = 10U;
constexpr std::uintptr_t kFakeBufferBase = 0x100000000ULL;
constexpr std::uint32_t kDefaultProcessCount = 8U;
constexpr std::uint32_t kDefaultBasePort = 19000U;
constexpr std::uint32_t kDefaultWarmup = 1U;
constexpr std::uint32_t kDefaultRepeat = 10U;
constexpr std::uint32_t kDefaultSyncTimeoutSec = 300U;
constexpr std::int32_t kDefaultConnectTimeoutMs = 60000;
constexpr std::int32_t kDefaultTransferTimeoutMs = 600000;
/// Minimum device local buffer allocation (bytes); workload spans may be smaller non-power-of-two totals.
constexpr std::uint64_t kDefaultLocalBufferMinBytes = kBytesPerGiB;

/// Transport type identifiers.
constexpr const char *kTransportRdma = "rdma";
constexpr const char *kTransportFabricMem = "fabric_mem";
constexpr const char *kTransportHccs = "hccs";
constexpr const char *kPoolMemoryHost = "host";
constexpr const char *kDefaultModel = "deepseek-r1";
constexpr const char *kDefaultKeyCounts = "16,32,48,64";
constexpr std::uint32_t kPercentileIndexNumerator = 99U;
constexpr std::uint32_t kPercentileIndexDenominator = 100U;
constexpr std::uint32_t kTraceRank = 0U;

using hixl::AscendString;
using hixl::FabricMemTransferService;
using hixl::Hixl;
using hixl::MemDesc;
using hixl::MemHandle;
using hixl::MemType;
using hixl::SUCCESS;
using hixl::TransferOp;
using hixl::TransferOpDesc;
using hixl_kv_benchmark::BufferView;
using hixl_kv_benchmark::BuildWorkloadSlicePlan;
using hixl_kv_benchmark::FindModelSpec;
using hixl_kv_benchmark::KeyTransferTask;
using hixl_kv_benchmark::KvSliceEntry;
using hixl_kv_benchmark::KvStore;
using hixl_kv_benchmark::KvTransferExecutor;
using hixl_kv_benchmark::LoadModelSpecsFromJson;
using hixl_kv_benchmark::ModelSpec;
using hixl_kv_benchmark::ParseTokenLength;
using hixl_kv_benchmark::RankMeta;
using hixl_kv_benchmark::SegmentManager;
using hixl_kv_benchmark::SupportedModelNames;

struct KvBenchConfig {
  std::uint32_t rank = 0U;
  std::uint32_t num_processes = kDefaultProcessCount;
  std::int32_t device_id = 0;
  /// Lower bound for device `local_buffer` size; actual size is max(this, max workload bytes).
  std::uint64_t local_buffer_min = kDefaultLocalBufferMinBytes;
  std::string pool_memory = kPoolMemoryHost;
  std::string model = kDefaultModel;
  std::string model_config = "kv_benchmark/config/models.json";
  std::string key_counts;
  std::string transport = kTransportRdma;
  std::string output_dir = "kv_benchmark/output";
  std::string run_id = "manual";
  std::string listen_host = "127.0.0.1";
  std::string connect_host = "127.0.0.1";
  std::uint32_t base_port = kDefaultBasePort;
  std::uint32_t warmup = kDefaultWarmup;
  std::uint32_t repeat = kDefaultRepeat;
  std::uint32_t sync_timeout_sec = kDefaultSyncTimeoutSec;
  std::uint32_t transfer_threads = hixl_kv_benchmark::kDefaultTransferThreads;
  bool enable_trace = false;
};

bool IsTraceRank(const KvBenchConfig &cfg) {
  return cfg.enable_trace && cfg.rank == kTraceRank;
}

struct KvWorkload {
  std::uint64_t token_length = 0U;
  std::uint64_t key_count = 0U;
  std::uint64_t max_slice_bytes = 0U;
  std::uint64_t slice_count = 0U;
  std::uint64_t total_bytes = 0U;
};

struct TimingStats {
  double avg_us = 0.0;
  double p99_us = 0.0;
};

struct TransferStageTiming {
  std::uint64_t plan_us = 0U;
  std::uint64_t transfer_us = 0U;
};

struct PreparedTransferSlice {
  std::uint64_t key_index = 0U;
  std::uintptr_t local_addr = 0U;
  std::uint64_t size = 0U;
};

struct PreparedSlicePlacement {
  std::uint32_t segment_id = 0U;
  std::uint64_t offset = 0U;
  std::uint64_t size = 0U;
};

struct WorkloadTransferState {
  std::vector<PreparedTransferSlice> slices;
  std::vector<PreparedSlicePlacement> placements;
  bool placements_ready = false;
};

struct KvBenchResult {
  std::string model;
  std::uint64_t token_length = 0U;
  std::uint64_t key_count = 0U;
  std::uint32_t tokens_per_key = 0U;
  std::uint64_t max_slice_bytes = 0U;
  std::uint64_t slice_count = 0U;
  std::uint64_t total_bytes = 0U;
  double put_bandwidth_gbps = 0.0;
  double get_bandwidth_gbps = 0.0;
  double put_avg_us = 0.0;
  double get_avg_us = 0.0;
  double put_p99_us = 0.0;
  double get_p99_us = 0.0;
  std::vector<std::uint64_t> key_distribution;
};

struct KvRuntime {
  Hixl hixl;
  void *local_buffer = nullptr;
  void *pool_buffer = nullptr;
  MemHandle local_handle = nullptr;
  MemHandle pool_handle = nullptr;
  aclrtContext aclrt_context = nullptr;
  bool device_bound = false;
  bool hixl_initialized = false;
  bool local_registered = false;
  bool pool_registered = false;
};

const char *RecentErrMsg() {
  const char *errmsg = aclGetRecentErrMsg();
  return errmsg == nullptr ? "no error" : errmsg;
}

std::vector<std::string> SplitComma(const std::string &value) {
  std::vector<std::string> parts;
  std::string current;
  for (char ch : value) {
    if (ch == ',') {
      if (!current.empty()) {
        parts.push_back(current);
      }
      current.clear();
      continue;
    }
    if (ch != ' ') {
      current.push_back(ch);
    }
  }
  if (!current.empty()) {
    parts.push_back(current);
  }
  return parts;
}

std::uint64_t ParseSize(const std::string &value) {
  if (value.empty()) {
    throw std::invalid_argument("empty size");
  }
  std::string digits = value;
  std::uint64_t multiplier = 1U;
  const char suffix = static_cast<char>(std::toupper(static_cast<unsigned char>(digits.back())));
  if (suffix == 'K' || suffix == 'M' || suffix == 'G') {
    digits.pop_back();
    multiplier = suffix == 'K' ? kBytesPerKiB : (suffix == 'M' ? kBytesPerMiB : kBytesPerGiB);
  }
  std::size_t pos = 0U;
  const auto parsed = std::stoull(digits, &pos, 10);
  if (pos != digits.size()) {
    throw std::invalid_argument("invalid size: " + value);
  }
  if (parsed != 0U && multiplier > (std::numeric_limits<std::uint64_t>::max() / parsed)) {
    throw std::overflow_error("size overflow: " + value);
  }
  return parsed * multiplier;
}

std::string FormatBytesKiB(std::uint64_t bytes) {
  std::ostringstream out;
  out << std::fixed;
  if (bytes >= kBytesPerGiB) {
    const auto value = static_cast<double>(bytes) / static_cast<double>(kBytesPerGiB);
    out << std::setprecision(value >= kFormatSizeWholeNumberThreshold ? kFormatSizeWholePrecision
                                                                      : kFormatSizeFractionPrecision)
        << value << "GiB";
  } else if (bytes >= kBytesPerMiB) {
    const auto value = static_cast<double>(bytes) / static_cast<double>(kBytesPerMiB);
    out << std::setprecision(value >= kFormatSizeWholeNumberThreshold ? kFormatSizeWholePrecision
                                                                      : kFormatSizeFractionPrecision)
        << value << "MiB";
  } else if (bytes >= kBytesPerKiB) {
    const auto value = static_cast<double>(bytes) / static_cast<double>(kBytesPerKiB);
    out << std::setprecision(value >= kFormatSizeWholeNumberThreshold ? kFormatSizeWholePrecision
                                                                      : kFormatSizeFractionPrecision)
        << value << "KiB";
  } else {
    out << std::setprecision(0) << bytes << "B";
  }
  return out.str();
}

/// Round allocation size up to a multiple of align_bytes (e.g. 1GiB huge-page style sizing).
std::uint64_t AlignAllocSizeUp(std::uint64_t nbytes, std::uint64_t align_bytes) {
  if (align_bytes == 0U) {
    return nbytes;
  }
  const std::uint64_t rem = nbytes % align_bytes;
  if (rem == 0U) {
    return nbytes;
  }
  return nbytes + (align_bytes - rem);
}

std::map<std::string, std::string> CollectArgs(const std::vector<std::string> &argv) {
  std::map<std::string, std::string> args;
  for (const auto &arg : argv) {
    const auto pos = arg.find('=');
    if (pos == std::string::npos || pos == 0U) {
      throw std::invalid_argument("expect --key=value, got: " + arg);
    }
    args[arg.substr(0, pos)] = arg.substr(pos + 1U);
  }
  return args;
}

std::uint32_t ParseU32(const std::map<std::string, std::string> &args, const std::string &key, std::uint32_t value) {
  const auto it = args.find(key);
  if (it == args.end()) {
    return value;
  }
  const auto parsed = std::stoul(it->second);
  if (parsed > static_cast<unsigned long>(std::numeric_limits<std::uint32_t>::max())) {
    throw std::out_of_range("value out of range for uint32: " + key);
  }
  return static_cast<std::uint32_t>(parsed);
}

KvBenchConfig ParseConfig(const std::vector<std::string> &argv) {
  const auto args = CollectArgs(argv);
  KvBenchConfig cfg;
  cfg.rank = ParseU32(args, "--rank", cfg.rank);
  cfg.num_processes = ParseU32(args, "--num_processes", cfg.num_processes);
  cfg.device_id = static_cast<std::int32_t>(ParseU32(args, "--device_id", static_cast<std::uint32_t>(cfg.device_id)));
  cfg.base_port = ParseU32(args, "--base_port", cfg.base_port);
  cfg.warmup = ParseU32(args, "--warmup", cfg.warmup);
  cfg.repeat = ParseU32(args, "--repeat", cfg.repeat);
  cfg.sync_timeout_sec = ParseU32(args, "--sync_timeout_sec", cfg.sync_timeout_sec);
  cfg.transfer_threads = ParseU32(args, "--transfer_threads", cfg.transfer_threads);
  cfg.enable_trace = ParseU32(args, "--trace", 0U) != 0U;
  if (args.count("--local_buffer_min") != 0U) cfg.local_buffer_min = ParseSize(args.at("--local_buffer_min"));
  if (args.count("--pool_memory") != 0U) cfg.pool_memory = args.at("--pool_memory");
  if (args.count("--model") != 0U) cfg.model = args.at("--model");
  if (args.count("--model_config") != 0U) cfg.model_config = args.at("--model_config");
  if (args.count("--key_counts") != 0U) cfg.key_counts = args.at("--key_counts");
  if (args.count("--transport") != 0U) cfg.transport = args.at("--transport");
  if (args.count("--output_dir") != 0U) cfg.output_dir = args.at("--output_dir");
  if (args.count("--run_id") != 0U) cfg.run_id = args.at("--run_id");
  if (args.count("--listen_host") != 0U) cfg.listen_host = args.at("--listen_host");
  if (args.count("--connect_host") != 0U) cfg.connect_host = args.at("--connect_host");
  if (cfg.key_counts.empty()) {
    cfg.key_counts = kDefaultKeyCounts;
  }
  return cfg;
}

bool ValidateConfig(const KvBenchConfig &cfg) {
  // KV workload uses host-side pool memory; HCCS comm path is restricted to D2D-only in benchmarks.
  const bool transport_ok = cfg.transport == kTransportRdma || cfg.transport == kTransportFabricMem;
  const bool workload_ok = !cfg.key_counts.empty();
  return cfg.num_processes > 0U && cfg.rank < cfg.num_processes && cfg.transfer_threads > 0U && cfg.repeat > 0U &&
         cfg.local_buffer_min > 0U && cfg.pool_memory == kPoolMemoryHost && transport_ok && workload_ok;
}

void ApplyTransportEnvironment(const KvBenchConfig &cfg) {
  if (cfg.transport != kTransportRdma) {
    return;
  }
  if (setenv("HCCL_INTRA_ROCE_ENABLE", "1", 1) != 0) {
    throw std::runtime_error("set HCCL_INTRA_ROCE_ENABLE=1 failed");
  }
}

std::map<AscendString, AscendString> BuildInitializeOptions(const KvBenchConfig &cfg) {
  std::map<AscendString, AscendString> options;
  options[AscendString(hixl::OPTION_BUFFER_POOL)] = AscendString("0:0");
  if (cfg.transport == kTransportFabricMem) {
    options[AscendString(hixl::OPTION_ENABLE_USE_FABRIC_MEM)] = AscendString("1");
  }
  return options;
}

SegmentManager BuildSegmentManagerUniform(std::uint32_t num_segments, std::uint64_t segment_bytes) {
  SegmentManager manager;
  for (std::uint32_t i = 0U; i < num_segments; ++i) {
    manager.AddSegment(i, segment_bytes);
  }
  return manager;
}

SegmentManager BuildSegmentManagerFromPoolSizes(const std::vector<std::uint64_t> &rank_pool_sizes) {
  SegmentManager manager;
  for (std::uint32_t i = 0U; i < rank_pool_sizes.size(); ++i) {
    manager.AddSegment(i, rank_pool_sizes[i]);
  }
  return manager;
}

std::vector<KvWorkload> BuildWorkloads(const KvBenchConfig &cfg, const ModelSpec &model) {
  std::vector<KvWorkload> workloads;
  if (cfg.key_counts.empty()) {
    throw std::invalid_argument("key_counts must be non-empty");
  }
  for (const auto &key_count_text : SplitComma(cfg.key_counts)) {
    const auto key_count = ParseTokenLength(key_count_text);
    if (key_count == 0U) {
      throw std::invalid_argument("key_count must be greater than zero");
    }
    workloads.push_back(KvWorkload{key_count * model.tokens_per_key, key_count,
                                   model.MaxSliceBytesForKeys(key_count),
                                   model.CountTransferSlicesForKeys(key_count),
                                   model.TransferBytesForKeys(key_count)});
  }
  return workloads;
}

void PlaceSlicePlan(KvStore *store, const std::vector<KvSliceEntry> &slice_plan) {
  std::vector<std::string> keys;
  std::vector<BufferView> buffers;
  keys.reserve(slice_plan.size());
  buffers.reserve(slice_plan.size());
  for (const auto &entry : slice_plan) {
    keys.push_back(entry.placement_key);
    buffers.push_back(entry.buffer);
  }
  if (!store->EnsurePlacements(keys, buffers)) {
    throw std::runtime_error("failed to place KV cache slices");
  }
}

std::vector<PreparedTransferSlice> BuildPreparedTransferSlices(std::uintptr_t local_base,
                                                               const KvWorkload &workload,
                                                               const ModelSpec &model) {
  std::vector<PreparedTransferSlice> slices;
  slices.reserve(static_cast<std::size_t>(workload.slice_count));
  std::uint64_t offset = 0U;
  for (std::uint64_t key_index = 0U; key_index < workload.key_count; ++key_index) {
    std::vector<hixl_kv_benchmark::KvCacheSlice> cache_slices;
    model.CollectCacheSlicesForKey(key_index, &cache_slices);
    for (const auto &cache_slice : cache_slices) {
      if (offset > (std::numeric_limits<std::uint64_t>::max() - cache_slice.size_bytes)) {
        throw std::overflow_error("KV prepared slice offset overflow");
      }
      slices.push_back(PreparedTransferSlice{key_index, local_base + static_cast<std::uintptr_t>(offset),
                                             cache_slice.size_bytes});
      offset += cache_slice.size_bytes;
    }
  }
  return slices;
}

WorkloadTransferState BuildWorkloadTransferState(void *local_buffer, const KvWorkload &workload,
                                                 const ModelSpec &model) {
  WorkloadTransferState state;
  state.slices = BuildPreparedTransferSlices(reinterpret_cast<std::uintptr_t>(local_buffer), workload, model);
  return state;
}

std::vector<std::uint64_t> ComputeMaxSegmentUsage(const KvBenchConfig &cfg, const ModelSpec &model,
                                                  const std::vector<KvWorkload> &workloads,
                                                  std::uint64_t uniform_segment_capacity) {
  std::vector<std::uint64_t> max_usage(cfg.num_processes, 0U);
  for (const auto &workload : workloads) {
    const auto slice_plan = BuildWorkloadSlicePlan(kFakeBufferBase, cfg.rank, workload.token_length,
                                                   workload.key_count, model);
    KvStore store(BuildSegmentManagerUniform(cfg.num_processes, uniform_segment_capacity));
    PlaceSlicePlan(&store, slice_plan);
    for (const auto &item : store.Placements()) {
      const auto &placement = item.second;
      if (placement.segment_id >= max_usage.size()) {
        throw std::runtime_error("invalid placement segment id");
      }
      max_usage[placement.segment_id] = std::max(max_usage[placement.segment_id], placement.offset + placement.size);
    }
  }
  return max_usage;
}

void VerifyRankPoolLayouts(const KvBenchConfig &cfg, const ModelSpec &model,
                           const std::vector<KvWorkload> &workloads,
                           const std::vector<std::uint64_t> &rank_pool_sizes) {
  for (const auto &workload : workloads) {
    const auto slice_plan = BuildWorkloadSlicePlan(kFakeBufferBase, cfg.rank, workload.token_length,
                                                   workload.key_count, model);
    KvStore store(BuildSegmentManagerFromPoolSizes(rank_pool_sizes));
    PlaceSlicePlan(&store, slice_plan);
  }
}

std::uint64_t MaxLocalBytes(const std::vector<KvWorkload> &workloads) {
  std::uint64_t value = 0U;
  for (const auto &workload : workloads) {
    value = std::max(value, workload.total_bytes);
  }
  return value;
}

std::filesystem::path SyncDir(const KvBenchConfig &cfg) {
  return std::filesystem::path(cfg.output_dir) / ".kv_sync" / cfg.run_id;
}

std::filesystem::path RankMetaPath(const KvBenchConfig &cfg, std::uint32_t rank) {
  return SyncDir(cfg) / ("rank" + std::to_string(rank) + ".meta");
}

std::string LocalListenEndpoint(const KvBenchConfig &cfg) {
  return cfg.listen_host + ":" + std::to_string(cfg.base_port + cfg.rank);
}

std::string LocalConnectEndpoint(const KvBenchConfig &cfg) {
  return cfg.connect_host + ":" + std::to_string(cfg.base_port + cfg.rank);
}

void WriteTextFileAtomically(const std::filesystem::path &path, const std::string &text) {
  std::filesystem::create_directories(path.parent_path());
  const auto tmp = path.string() + ".tmp";
  {
    std::ofstream out(tmp);
    if (!out.good()) {
      throw std::runtime_error("failed to open " + tmp);
    }
    out << text;
  }
  std::filesystem::rename(tmp, path);
}

void WriteRankMeta(const KvBenchConfig &cfg, const KvRuntime &runtime, std::uint64_t pool_size) {
  std::string text;
  text += "rank=" + std::to_string(cfg.rank) + "\n";
  text += "endpoint=" + LocalConnectEndpoint(cfg) + "\n";
  text += "pool_addr=" + std::to_string(reinterpret_cast<std::uintptr_t>(runtime.pool_buffer)) + "\n";
  text += "pool_size=" + std::to_string(pool_size) + "\n";
  WriteTextFileAtomically(RankMetaPath(cfg, cfg.rank), text);
}

RankMeta ReadRankMeta(const std::filesystem::path &path) {
  std::ifstream in(path);
  if (!in.good()) {
    throw std::runtime_error("failed to open rank meta: " + path.string());
  }
  std::map<std::string, std::string> kv;
  std::string line;
  while (std::getline(in, line)) {
    const auto pos = line.find('=');
    if (pos == std::string::npos) {
      continue;
    }
    kv[line.substr(0, pos)] = line.substr(pos + 1U);
  }
  RankMeta meta;
  meta.rank = static_cast<std::uint32_t>(std::stoul(kv.at("rank")));
  meta.endpoint = kv.at("endpoint");
  meta.pool_addr = static_cast<std::uintptr_t>(std::stoull(kv.at("pool_addr")));
  meta.pool_size = static_cast<std::uint64_t>(std::stoull(kv.at("pool_size")));
  return meta;
}

void WaitForFiles(const std::vector<std::filesystem::path> &paths, std::uint32_t timeout_sec) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_sec);
  while (std::chrono::steady_clock::now() < deadline) {
    bool all_exist = true;
    for (const auto &path : paths) {
      if (!std::filesystem::exists(path)) {
        all_exist = false;
        break;
      }
    }
    if (all_exist) {
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(kSyncPollIntervalMs));
  }
  throw std::runtime_error("timeout waiting for benchmark peers");
}

std::vector<RankMeta> LoadAllRankMeta(const KvBenchConfig &cfg) {
  std::vector<std::filesystem::path> paths;
  for (std::uint32_t rank = 0U; rank < cfg.num_processes; ++rank) {
    paths.push_back(RankMetaPath(cfg, rank));
  }
  WaitForFiles(paths, cfg.sync_timeout_sec);

  std::vector<RankMeta> metas(cfg.num_processes);
  for (std::uint32_t rank = 0U; rank < cfg.num_processes; ++rank) {
    metas[rank] = ReadRankMeta(RankMetaPath(cfg, rank));
    if (metas[rank].rank != rank) {
      throw std::runtime_error("rank meta mismatch");
    }
  }
  return metas;
}

void Barrier(const KvBenchConfig &cfg, const std::string &name) {
  std::filesystem::path dir = SyncDir(cfg);
  dir.append(name);
  const auto path = dir / ("rank" + std::to_string(cfg.rank));
  if (IsTraceRank(cfg)) {
    std::cout << "[TRACE] rank=" << cfg.rank << " barrier_enter name=" << name << std::endl;
  }
  WriteTextFileAtomically(path, "ready\n");

  std::vector<std::filesystem::path> paths;
  for (std::uint32_t rank = 0U; rank < cfg.num_processes; ++rank) {
    paths.push_back(dir / ("rank" + std::to_string(rank)));
  }
  WaitForFiles(paths, cfg.sync_timeout_sec);
  if (IsTraceRank(cfg)) {
    std::cout << "[TRACE] rank=" << cfg.rank << " barrier_exit name=" << name << std::endl;
  }
}

void AllocHostBuffer(const KvBenchConfig &cfg, std::uint64_t size, void **buffer) {
  if (cfg.transport == kTransportFabricMem) {
    const auto status =
        FabricMemTransferService::MallocMem(MemType::MEM_HOST, static_cast<size_t>(size), buffer);
    if (status != SUCCESS) {
      throw std::runtime_error("fabric_mem host allocation failed");
    }
    return;
  }
  const auto ret = aclrtMallocHost(buffer, static_cast<size_t>(size));
  if (ret != ACL_ERROR_NONE) {
    throw std::runtime_error("aclrtMallocHost failed");
  }
}

void FreeHostBuffer(const KvBenchConfig &cfg, void *buffer) {
  if (buffer == nullptr) {
    return;
  }
  if (cfg.transport == kTransportFabricMem) {
    (void)FabricMemTransferService::FreeMem(buffer);
  } else {
    (void)aclrtFreeHost(buffer);
  }
}

void RegisterMem(Hixl &hixl, void *buffer, std::uint64_t size, MemType type, MemHandle *handle) {
  MemDesc desc{};
  desc.addr = reinterpret_cast<std::uintptr_t>(buffer);
  desc.len = static_cast<size_t>(size);
  const auto ret = hixl.RegisterMem(desc, type, *handle);
  if (ret != SUCCESS) {
    throw std::runtime_error("RegisterMem failed, ret=" + std::to_string(ret) + ", errmsg: " + RecentErrMsg());
  }
}

void InitRuntime(const KvBenchConfig &cfg, std::uint64_t local_size, std::uint64_t pool_size, KvRuntime *runtime) {
  if (aclrtSetDevice(cfg.device_id) != ACL_ERROR_NONE) {
    throw std::runtime_error("aclrtSetDevice failed");
  }
  runtime->device_bound = true;
  const auto ctx_ret = aclrtGetCurrentContext(&runtime->aclrt_context);
  if (ctx_ret != ACL_ERROR_NONE || runtime->aclrt_context == nullptr) {
    throw std::runtime_error("aclrtGetCurrentContext failed, ret=" + std::to_string(ctx_ret) +
                             ", errmsg: " + RecentErrMsg());
  }

  const auto init_options = BuildInitializeOptions(cfg);
  const auto init_ret = runtime->hixl.Initialize(AscendString(LocalListenEndpoint(cfg).c_str()), init_options);
  if (init_ret != SUCCESS) {
    throw std::runtime_error("Hixl Initialize failed, ret=" + std::to_string(init_ret) + ", errmsg: " + RecentErrMsg());
  }
  runtime->hixl_initialized = true;

  if (aclrtMalloc(&runtime->local_buffer, static_cast<size_t>(local_size), ACL_MEM_MALLOC_HUGE_ONLY) !=
      ACL_ERROR_NONE) {
    throw std::runtime_error("aclrtMalloc device buffer failed");
  }
  if (runtime->local_buffer == nullptr) {
    throw std::runtime_error("device buffer allocation succeeded but returned null");
  }
  // fabric_mem: only host pool is registered; aclrtMalloc device local_buffer is used via TransferSync addresses only.
  if (cfg.transport != kTransportFabricMem) {
    RegisterMem(runtime->hixl, runtime->local_buffer, local_size, MemType::MEM_DEVICE, &runtime->local_handle);
    runtime->local_registered = true;
  }

  AllocHostBuffer(cfg, pool_size, &runtime->pool_buffer);
  if (runtime->pool_buffer == nullptr) {
    throw std::runtime_error("host pool buffer allocation succeeded but returned null");
  }
  RegisterMem(runtime->hixl, runtime->pool_buffer, pool_size, MemType::MEM_HOST, &runtime->pool_handle);
  runtime->pool_registered = true;
}

void CleanupRuntime(const KvBenchConfig &cfg, KvRuntime *runtime, const std::vector<RankMeta> &metas) {
  if (runtime->hixl_initialized) {
    const bool disconnect_self = cfg.transport == kTransportFabricMem;
    for (const auto &meta : metas) {
      if (meta.rank == cfg.rank && !disconnect_self) {
        continue;
      }
      (void)runtime->hixl.Disconnect(AscendString(meta.endpoint.c_str()));
    }
    if (runtime->local_registered) {
      (void)runtime->hixl.DeregisterMem(runtime->local_handle);
      runtime->local_registered = false;
    }
    if (runtime->pool_registered) {
      (void)runtime->hixl.DeregisterMem(runtime->pool_handle);
      runtime->pool_registered = false;
    }
    runtime->hixl.Finalize();
    runtime->hixl_initialized = false;
  }
  if (runtime->local_buffer != nullptr) {
    (void)aclrtFree(runtime->local_buffer);
    runtime->local_buffer = nullptr;
  }
  FreeHostBuffer(cfg, runtime->pool_buffer);
  runtime->pool_buffer = nullptr;
  if (runtime->device_bound) {
    (void)aclrtResetDevice(cfg.device_id);
    runtime->aclrt_context = nullptr;
    runtime->device_bound = false;
  }
}

void ConnectPeers(const KvBenchConfig &cfg, KvRuntime *runtime, const std::vector<RankMeta> &metas) {
  const bool connect_self = cfg.transport == kTransportFabricMem;
  for (const auto &meta : metas) {
    if (meta.rank == cfg.rank && !connect_self) {
      continue;
    }
    if (IsTraceRank(cfg)) {
      std::cout << "[TRACE] rank=" << cfg.rank << " connect_begin peer_rank=" << meta.rank
                << " endpoint=" << meta.endpoint << std::endl;
    }
    const auto ret = runtime->hixl.Connect(AscendString(meta.endpoint.c_str()), kDefaultConnectTimeoutMs);
    if (ret != SUCCESS && ret != hixl::ALREADY_CONNECTED) {
      throw std::runtime_error("Connect failed to " + meta.endpoint + ", ret=" + std::to_string(ret) +
                               ", errmsg: " + RecentErrMsg());
    }
    if (IsTraceRank(cfg)) {
      std::cout << "[TRACE] rank=" << cfg.rank << " connect_end peer_rank=" << meta.rank
                << " endpoint=" << meta.endpoint << " ret=" << ret << std::endl;
    }
  }
}

std::map<std::uint32_t, RankMeta> BuildRankMetaByRank(const std::vector<RankMeta> &metas) {
  std::map<std::uint32_t, RankMeta> out;
  for (const auto &meta : metas) {
    out[meta.rank] = meta;
  }
  return out;
}

const char *TransferOpName(TransferOp op) {
  return op == hixl::WRITE ? "WRITE" : "READ";
}

std::uint64_t SumTransferBytes(const std::vector<TransferOpDesc> &descs) {
  std::uint64_t total = 0U;
  for (const auto &desc : descs) {
    total += static_cast<std::uint64_t>(desc.len);
  }
  return total;
}

std::uint64_t ElapsedUs(const std::chrono::steady_clock::time_point &start,
                        const std::chrono::steady_clock::time_point &end) {
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
}

void PrintTransferPlanSummary(const KvBenchConfig &cfg, const std::vector<KeyTransferTask> &tasks, TransferOp op,
                              const KvWorkload &workload) {
  if (!IsTraceRank(cfg)) {
    return;
  }
  std::cout << "[TRACE] rank=" << cfg.rank << " transfer_plan op=" << TransferOpName(op)
            << " model=" << cfg.model << " key_count=" << workload.key_count << " tasks=" << tasks.size()
            << std::endl;
  for (const auto &task : tasks) {
    std::cout << "[TRACE] rank=" << cfg.rank << " transfer_key op=" << TransferOpName(op) << " key=" << task.key_index
              << " segment=" << task.segment_id << " self=" << task.is_self << " descs=" << task.descs.size()
              << " bytes=" << SumTransferBytes(task.descs) << std::endl;
  }
}

void PrintStageTiming(const KvBenchConfig &cfg, TransferOp op, const KvWorkload &workload, std::uint64_t plan_us,
                      std::uint64_t transfer_us) {
  if (!IsTraceRank(cfg)) {
    return;
  }
  std::cout << "[TRACE] rank=" << cfg.rank << " transfer_stage op=" << TransferOpName(op)
            << " model=" << cfg.model << " key_count=" << workload.key_count
            << " plan_us=" << plan_us << " transfer_us=" << transfer_us
            << " total_us=" << (plan_us + transfer_us) << std::endl;
}

void GeneratePlacements(const std::vector<std::uint64_t> &rank_pool_sizes,
                        WorkloadTransferState *state) {
  SegmentManager manager = BuildSegmentManagerFromPoolSizes(rank_pool_sizes);
  state->placements.clear();
  state->placements.reserve(state->slices.size());
  std::uint64_t current_key = std::numeric_limits<std::uint64_t>::max();
  std::uint32_t selected_segment = 0U;
  std::uint32_t next_segment = 0U;
  const auto segment_count = static_cast<std::uint32_t>(rank_pool_sizes.size());
  for (const auto &slice : state->slices) {
    if (slice.key_index != current_key) {
      current_key = slice.key_index;
      selected_segment = next_segment % segment_count;
      ++next_segment;
    }
    const auto allocation = manager.AllocateFrom(selected_segment, slice.size);
    if (!allocation.has_value()) {
      throw std::runtime_error("failed to place KV cache slice");
    }
    state->placements.push_back(PreparedSlicePlacement{allocation->segment_id, allocation->offset, allocation->size});
  }
  state->placements_ready = true;
}

void EnsurePlacementMetadata(const std::vector<std::uint64_t> &rank_pool_sizes,
                             WorkloadTransferState *state) {
  if (state->placements_ready) {
    return;
  }
  GeneratePlacements(rank_pool_sizes, state);
}

std::vector<std::uint64_t> BuildKeyDistribution(std::uint32_t segment_count,
                                                const WorkloadTransferState &state) {
  std::vector<std::uint64_t> distribution(segment_count, 0U);
  if (!state.placements_ready) {
    throw std::runtime_error("missing KV placement metadata before building key distribution");
  }
  if (state.slices.size() != state.placements.size()) {
    throw std::runtime_error("KV placement metadata size mismatch");
  }
  std::uint64_t current_key = std::numeric_limits<std::uint64_t>::max();
  for (std::size_t i = 0U; i < state.slices.size(); ++i) {
    if (state.slices[i].key_index == current_key) {
      continue;
    }
    current_key = state.slices[i].key_index;
    const auto segment_id = state.placements[i].segment_id;
    if (segment_id >= distribution.size()) {
      throw std::runtime_error("KV key placement segment is out of range");
    }
    ++distribution[segment_id];
  }
  return distribution;
}

KeyTransferTask MakeKeyTransferTask(std::uint64_t key_index, std::uint32_t segment_id, const RankMeta &meta,
                                    std::uint32_t self_rank, bool local_copy_for_self) {
  KeyTransferTask task;
  task.key_index = key_index;
  task.segment_id = segment_id;
  if (segment_id == self_rank && local_copy_for_self) {
    task.is_self = true;
    return task;
  }
  task.endpoint = meta.endpoint;
  return task;
}

TransferOpDesc MakeTransferOpDesc(const PreparedTransferSlice &slice, const PreparedSlicePlacement &placement,
                                  const RankMeta &meta) {
  if (placement.offset + placement.size > meta.pool_size) {
    throw std::runtime_error("KV slice placement exceeds registered remote pool");
  }
  TransferOpDesc desc{};
  desc.local_addr = slice.local_addr;
  desc.remote_addr = meta.pool_addr + static_cast<std::uintptr_t>(placement.offset);
  desc.len = static_cast<size_t>(placement.size);
  return desc;
}

std::vector<KeyTransferTask> BuildKeyTransferTasks(const std::vector<RankMeta> &metas,
                                                   const WorkloadTransferState &state, std::uint32_t self_rank,
                                                   bool local_copy_for_self) {
  if (!state.placements_ready) {
    throw std::runtime_error("missing KV placement metadata before building key transfer tasks");
  }
  if (state.slices.size() != state.placements.size()) {
    throw std::runtime_error("KV placement metadata size mismatch");
  }
  std::vector<KeyTransferTask> tasks;
  std::uint64_t current_key = std::numeric_limits<std::uint64_t>::max();
  for (std::size_t i = 0U; i < state.slices.size(); ++i) {
    const auto &slice = state.slices[i];
    const auto &placement = state.placements[i];
    if (slice.key_index != current_key) {
      current_key = slice.key_index;
      tasks.push_back(MakeKeyTransferTask(current_key, placement.segment_id, metas.at(placement.segment_id),
                                          self_rank, local_copy_for_self));
    }
    tasks.back().descs.push_back(MakeTransferOpDesc(slice, placement, metas.at(placement.segment_id)));
  }
  return tasks;
}

TransferStageTiming ExecuteKvTransfer(const KvBenchConfig &cfg, KvTransferExecutor *transfer_executor,
                                      const std::vector<RankMeta> &metas, const KvWorkload &workload, TransferOp op,
                                      const std::vector<std::uint64_t> &rank_pool_sizes, WorkloadTransferState *state,
                                      bool trace_transfer) {
  const auto plan_start = std::chrono::steady_clock::now();
  const bool local_copy_for_self = cfg.transport != kTransportFabricMem;
  if (op == hixl::WRITE) {
    GeneratePlacements(rank_pool_sizes, state);
  } else if (!state->placements_ready) {
    throw std::runtime_error("missing KV placement metadata before get");
  }
  auto tasks = BuildKeyTransferTasks(metas, *state, cfg.rank, local_copy_for_self);
  const auto plan_us = ElapsedUs(plan_start, std::chrono::steady_clock::now());

  const bool trace_enabled = trace_transfer && IsTraceRank(cfg);
  if (trace_enabled) {
    PrintTransferPlanSummary(cfg, tasks, op, workload);
  }
  const auto transfer_start = std::chrono::steady_clock::now();
  transfer_executor->Transfer(op, std::move(tasks), trace_enabled);
  const auto transfer_us = ElapsedUs(transfer_start, std::chrono::steady_clock::now());
  return TransferStageTiming{plan_us, transfer_us};
}

double Percentile99(std::vector<double> values) {
  if (values.empty()) {
    return 0.0;
  }
  std::sort(values.begin(), values.end());
  const auto idx = static_cast<std::size_t>(
      (values.size() * kPercentileIndexNumerator + kPercentileIndexDenominator - 1U) / kPercentileIndexDenominator -
      1U);
  return values[std::min(idx, values.size() - 1U)];
}

TimingStats MeasureRepeated(const KvBenchConfig &cfg, const std::string &name,
                            const std::function<TransferStageTiming(bool)> &fn, bool sync_all_ranks,
                            const std::function<void(const TransferStageTiming &)> &after_iteration) {
  std::vector<double> samples;
  const auto total = cfg.warmup + cfg.repeat;
  for (std::uint32_t i = 0U; i < total; ++i) {
    if (sync_all_ranks) {
      Barrier(cfg, name + "_ready_" + std::to_string(i));
    }
    const auto start = std::chrono::steady_clock::now();
    const auto stage_timing = fn(IsTraceRank(cfg));
    const auto end = std::chrono::steady_clock::now();
    if (sync_all_ranks) {
      Barrier(cfg, name + "_done_" + std::to_string(i));
    }
    after_iteration(stage_timing);
    if (i >= cfg.warmup) {
      samples.push_back(static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count()) /
                        kNanosecondsPerMicrosecond);
    }
  }
  if (samples.empty()) {
    return TimingStats{};
  }
  const double sum = std::accumulate(samples.begin(), samples.end(), 0.0);
  return TimingStats{sum / static_cast<double>(samples.size()), Percentile99(samples)};
}

double BandwidthGbps(std::uint64_t bytes, double us) {
  const double duration_us = us;
  if (duration_us <= 0.0) {
    return 0.0;
  }
  const double bytes_per_second = static_cast<double>(bytes) * kMicrosecondsPerSecond / duration_us;
  return bytes_per_second / kDecimalBytesPerGb;
}

TimingStats RunWorkloadPut(const KvBenchConfig &cfg, KvTransferExecutor *transfer_executor,
                         const std::vector<RankMeta> &metas, const ModelSpec &model, const KvWorkload &workload,
                         const std::vector<std::uint64_t> &rank_pool_sizes, WorkloadTransferState *transfer_state) {
  if (model.IsShared() && cfg.rank != 0U) {
    return TimingStats{};
  }
  const auto put_start = std::chrono::steady_clock::now();
  const auto put_timing = ExecuteKvTransfer(cfg, transfer_executor, metas, workload, hixl::WRITE, rank_pool_sizes,
                                            transfer_state, IsTraceRank(cfg));
  const auto put_end = std::chrono::steady_clock::now();
  const double put_us =
      static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(put_end - put_start).count()) /
      kNanosecondsPerMicrosecond;
  PrintStageTiming(cfg, hixl::WRITE, workload, put_timing.plan_us, put_timing.transfer_us);
  return TimingStats{put_us, put_us};
}

KvBenchResult RunWorkload(const KvBenchConfig &cfg, KvRuntime *runtime, KvTransferExecutor *transfer_executor,
                          const std::vector<RankMeta> &metas, const ModelSpec &model, const KvWorkload &workload,
                          std::size_t index,
                          const std::vector<std::uint64_t> &rank_pool_sizes) {
  WorkloadTransferState transfer_state = BuildWorkloadTransferState(runtime->local_buffer, workload, model);

  Barrier(cfg, "workload_" + std::to_string(index) + "_ready");
  const TimingStats put =
      RunWorkloadPut(cfg, transfer_executor, metas, model, workload, rank_pool_sizes, &transfer_state);
  Barrier(cfg, "workload_" + std::to_string(index) + "_put_done");
  EnsurePlacementMetadata(rank_pool_sizes, &transfer_state);

  const TimingStats get = MeasureRepeated(
      cfg, "workload_" + std::to_string(index) + "_get",
      [&](bool trace_transfer) {
        return ExecuteKvTransfer(cfg, transfer_executor, metas, workload, hixl::READ, rank_pool_sizes,
                                 &transfer_state, trace_transfer);
      },
      true,
      [&](const TransferStageTiming &timing) {
        PrintStageTiming(cfg, hixl::READ, workload, timing.plan_us, timing.transfer_us);
      });
  Barrier(cfg, "workload_" + std::to_string(index) + "_done");
  const auto key_distribution = BuildKeyDistribution(cfg.num_processes, transfer_state);

  return KvBenchResult{model.name,
                       workload.token_length,
                       workload.key_count,
                       model.tokens_per_key,
                       workload.max_slice_bytes,
                       workload.slice_count,
                       workload.total_bytes,
                       BandwidthGbps(workload.total_bytes, put.avg_us),
                       BandwidthGbps(workload.total_bytes, get.avg_us),
                       put.avg_us,
                       get.avg_us,
                       put.p99_us,
                       get.p99_us,
                       key_distribution};
}

std::vector<KvBenchResult> RunBenchmark(const KvBenchConfig &cfg, KvRuntime *runtime,
                                        KvTransferExecutor *transfer_executor, const std::vector<RankMeta> &metas,
                                        const ModelSpec &model,
                                        const std::vector<KvWorkload> &workloads,
                                        const std::vector<std::uint64_t> &rank_pool_sizes) {
  std::vector<KvBenchResult> results;
  for (std::size_t i = 0U; i < workloads.size(); ++i) {
    results.push_back(RunWorkload(cfg, runtime, transfer_executor, metas, model, workloads[i], i, rank_pool_sizes));
  }
  return results;
}

void WriteCsv(const KvBenchConfig &cfg, const std::vector<KvBenchResult> &results,
              std::uint64_t rank_pool_size_bytes) {
  std::filesystem::create_directories(cfg.output_dir);
  std::ofstream out(cfg.output_dir + "/kv_result_rank" + std::to_string(cfg.rank) + ".csv");
  out << "rank,model,token_length,key_count,tokens_per_key,max_slice_bytes,slice_count,total_bytes,transfer_threads,"
         "process_count,device_count,segment_count,pool_size_bytes,pool_memory,put_transfer_type,get_transfer_type,"
         "transport,warmup,repeat,put_bandwidth_gbps,get_bandwidth_gbps,put_avg_us,get_avg_us,put_p99_us,get_p99_us\n";
  for (const auto &result : results) {
    out << cfg.rank << ',' << result.model << ',' << result.token_length << ',' << result.key_count << ','
        << result.tokens_per_key << ',' << result.max_slice_bytes << ',' << result.slice_count << ','
        << result.total_bytes << ',' << cfg.transfer_threads << ',' << cfg.num_processes << ','
        << cfg.num_processes << ',' << cfg.num_processes << ',' << rank_pool_size_bytes
        << ','
        << cfg.pool_memory << ",d2rh,rh2d," << cfg.transport << ',' << cfg.warmup << ',' << cfg.repeat << ','
        << result.put_bandwidth_gbps << ',' << result.get_bandwidth_gbps << ',' << result.put_avg_us << ','
        << result.get_avg_us << ',' << result.put_p99_us << ',' << result.get_p99_us << '\n';
  }
}

void WriteJson(const KvBenchConfig &cfg, const std::vector<KvBenchResult> &results) {
  std::ofstream out(cfg.output_dir + "/kv_result_rank" + std::to_string(cfg.rank) + ".json");
  out << "{\"benchmark_name\":\"hixl_kv_bench\",\"rank\":" << cfg.rank << ",\"results\":[";
  for (std::size_t i = 0U; i < results.size(); ++i) {
    const auto &r = results[i];
    out << (i == 0U ? "" : ",") << "{\"model\":\"" << r.model << "\",\"token_length\":" << r.token_length
        << ",\"key_count\":" << r.key_count << ",\"max_slice_bytes\":" << r.max_slice_bytes
        << ",\"slice_count\":" << r.slice_count << ",\"total_bytes\":" << r.total_bytes
        << ",\"put_avg_us\":" << r.put_avg_us
        << ",\"get_avg_us\":" << r.get_avg_us << ",\"put_p99_us\":" << r.put_p99_us
        << ",\"get_p99_us\":" << r.get_p99_us << ",\"put_transfer_type\":\"d2rh\",\"get_transfer_type\":\"rh2d\"}";
  }
  out << "]}\n";
}

void PrintWorkloadTransferPlan(const std::string &model_name, const std::vector<KvWorkload> &workloads) {
  for (const auto &workload : workloads) {
    std::cout << "[INFO] model=" << model_name << " key_count=" << workload.key_count
              << " token_length=" << workload.token_length
              << " total_transfer=" << FormatBytesKiB(workload.total_bytes)
              << " slice_count=" << workload.slice_count
              << " max_slice=" << FormatBytesKiB(workload.max_slice_bytes) << std::endl;
  }
}

std::string FormatKeyDistribution(const std::vector<std::uint64_t> &distribution) {
  std::ostringstream text;
  for (std::size_t i = 0U; i < distribution.size(); ++i) {
    if (i != 0U) {
      text << ',';
    }
    text << i << ':' << distribution[i];
  }
  return text.str();
}

void PrintSummary(const KvBenchConfig &cfg, const std::vector<KvBenchResult> &results) {
  for (const auto &result : results) {
    std::cout << "[INFO] rank=" << cfg.rank << " model=" << result.model << " key_count=" << result.key_count
              << " total_transfer=" << FormatBytesKiB(result.total_bytes) << " slice_count=" << result.slice_count
              << " max_slice=" << FormatBytesKiB(result.max_slice_bytes) << " token_length=" << result.token_length
              << " put=d2rh get=rh2d"
              << " put_avg_us=" << result.put_avg_us << " get_avg_us=" << result.get_avg_us
              << " put_p99_us=" << result.put_p99_us << " get_p99_us=" << result.get_p99_us
              << " segment_key_distribution=" << FormatKeyDistribution(result.key_distribution) << std::endl;
  }
}

std::string JoinNames(const std::vector<std::string> &names) {
  std::string text;
  for (std::size_t i = 0U; i < names.size(); ++i) {
    if (i != 0U) {
      text += ",";
    }
    text += names[i];
  }
  return text;
}

std::vector<std::uint64_t> BuildAlignedRankPoolSizes(const KvBenchConfig &cfg,
                                                     const std::vector<std::uint64_t> &max_usage_per_rank) {
  std::vector<std::uint64_t> out(cfg.num_processes);
  for (std::uint32_t r = 0U; r < cfg.num_processes; ++r) {
    out[r] = AlignAllocSizeUp(std::max<std::uint64_t>(max_usage_per_rank.at(r), 1U), kBytesPerGiB);
  }
  return out;
}

void PrintKvBufferPlan(const KvBenchConfig &cfg, std::uint64_t local_size, std::uint64_t pool_size) {
  std::cout << "[INFO] rank=" << cfg.rank << " device_id=" << cfg.device_id
            << " local_engine=" << LocalListenEndpoint(cfg) << " local_buffer_size=" << FormatBytesKiB(local_size)
            << " pool_size=" << FormatBytesKiB(pool_size) << std::endl;
}

std::vector<KvBenchResult> ExecuteKvBenchmark(const KvBenchConfig &cfg, KvRuntime *runtime,
                                              const std::vector<RankMeta> &metas, const ModelSpec &model,
                                              const std::vector<KvWorkload> &workloads,
                                              const std::vector<std::uint64_t> &rank_pool_sizes) {
  const bool local_copy_for_self = cfg.transport != kTransportFabricMem;
  KvTransferExecutor transfer_executor(&runtime->hixl, BuildRankMetaByRank(metas), cfg.rank, cfg.transfer_threads,
                                       kDefaultTransferTimeoutMs, runtime->aclrt_context, RecentErrMsg,
                                       local_copy_for_self);
  return RunBenchmark(cfg, runtime, &transfer_executor, metas, model, workloads, rank_pool_sizes);
}

int RunKvBenchParsed(KvBenchConfig &cfg, KvRuntime *runtime, std::vector<RankMeta> *metas) {
  if (cfg.transport == kTransportHccs) {
    std::cerr << "[ERROR] KV benchmark does not support transport=hccs (HCCS is D2D-only; use rdma or fabric_mem)\n";
    return 1;
  }
  if (!ValidateConfig(cfg)) {
    std::cerr << "[ERROR] invalid kv benchmark config\n";
    return 1;
  }
  ApplyTransportEnvironment(cfg);

  const auto models = LoadModelSpecsFromJson(cfg.model_config);
  const ModelSpec *model = FindModelSpec(models, cfg.model);
  if (model == nullptr) {
    std::cerr << "[ERROR] unsupported model: " << cfg.model
              << " (supported: " << JoinNames(SupportedModelNames(models)) << ")" << std::endl;
    return 1;
  }
  const auto workloads = BuildWorkloads(cfg, *model);
  if (cfg.rank == 0U) {
    PrintWorkloadTransferPlan(model->name, workloads);
  }
  const auto workload_local_bytes = MaxLocalBytes(workloads);
  const auto bootstrap_segment_capacity =
      AlignAllocSizeUp(std::max<std::uint64_t>(workload_local_bytes, 1U), kBytesPerGiB);
  const auto max_segment_usage =
      ComputeMaxSegmentUsage(cfg, *model, workloads, bootstrap_segment_capacity);
  const auto rank_pool_sizes = BuildAlignedRankPoolSizes(cfg, max_segment_usage);
  VerifyRankPoolLayouts(cfg, *model, workloads, rank_pool_sizes);
  const auto local_size =
      AlignAllocSizeUp(std::max(workload_local_bytes, cfg.local_buffer_min), kBytesPerGiB);
  const auto pool_size = rank_pool_sizes.at(cfg.rank);

  PrintKvBufferPlan(cfg, local_size, pool_size);
  InitRuntime(cfg, local_size, pool_size, runtime);
  WriteRankMeta(cfg, *runtime, pool_size);
  *metas = LoadAllRankMeta(cfg);
  ConnectPeers(cfg, runtime, *metas);
  Barrier(cfg, "all_connected");

  const auto results = ExecuteKvBenchmark(cfg, runtime, *metas, *model, workloads, rank_pool_sizes);
  WriteCsv(cfg, results, pool_size);
  WriteJson(cfg, results);
  PrintSummary(cfg, results);
  Barrier(cfg, "all_done");
  CleanupRuntime(cfg, runtime, *metas);
  return 0;
}

}  // namespace

int main(int argc, char **argv) {
  std::vector<std::string> args;
  if (argc > 1) {
    args.reserve(static_cast<std::size_t>(argc - 1));
    for (int i = 1; i < argc; ++i) {
      args.emplace_back(argv[i]);
    }
  }
  KvBenchConfig cfg{};
  KvRuntime runtime{};
  std::vector<RankMeta> metas;
  try {
    cfg = ParseConfig(args);
    return RunKvBenchParsed(cfg, &runtime, &metas);
  } catch (const std::exception &e) {
    std::cerr << "[ERROR] " << e.what() << std::endl;
    CleanupRuntime(cfg, &runtime, metas);
    return 1;
  }
}
