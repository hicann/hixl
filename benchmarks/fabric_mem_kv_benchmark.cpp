/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

// FabricMem KV block benchmark: N processes (rank 0..N-1), one device per rank by default.
// Block size = 61*(128K+16K) bytes (DeepSeek-R1-like KV slice per block).
// API surface: adxl::AdxlEngine only (Initialize, RegisterMem, Connect, TransferSync, MallocMem, FreeMem, ...).
// Data plane: peer DRAM is MEM_HOST via AdxlEngine::MallocMem (VMM + fabric host PA);
// NPU uses device VA from aclrtReserveMemAddress / MallocPhysical / MapMem (not registered with AdxlEngine).
// Host/device pools are sized by rounding kRequiredDataBytes up to the next whole GiB (not exact-fit).
// Put (D2RH): rank 0 WRITE from local device to each peer's host buffer; local rank-0 slice uses D2H memcpy.
// Get (RH2D): each rank READ from remote host into local device buffer (skips local slice).
// Timings: kWarmupIterations warmup, then kTimedIterations timed runs; put avg on rank 0; get max across ranks per iter.
// Result lines: each rank prints its RH2D average; rank 0 adds a second line with put + get(max).

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "acl/acl.h"
#include "adxl/adxl_engine.h"
#include "hixl/hixl_types.h"

using namespace adxl;

namespace {
constexpr int32_t kDefaultWorldSize = 16;
constexpr int32_t kMinWorldSize = 2;
constexpr int32_t kMaxWorldSize = 64;
constexpr uint32_t kLayers = 61U;
constexpr uint32_t kLargeChunkBytes = 128U * 1024U;
constexpr uint32_t kSmallChunkBytes = 16U * 1024U;
constexpr size_t kBlockBytes = static_cast<size_t>(kLayers) * (kLargeChunkBytes + kSmallChunkBytes);
constexpr uint32_t kArgIndexDeviceId = 1U;
constexpr uint32_t kArgIndexRank = 2U;
constexpr uint32_t kArgIndexHostIp = 3U;
constexpr uint32_t kArgIndexBasePort = 4U;
constexpr uint32_t kArgIndexSyncDir = 5U;
constexpr uint32_t kArgIndexWorldSize = 6U;
// argc counts argv[0]; required args occupy argv[1 .. kArgIndexSyncDir], optional world_size at kArgIndexWorldSize.
constexpr int kArgcWithRequiredArgsOnly = 1 + static_cast<int>(kArgIndexSyncDir);
constexpr int kArgcWithOptionalWorldSize = 1 + static_cast<int>(kArgIndexWorldSize);
constexpr int32_t kConnectTimeoutMs = 120000;
constexpr int32_t kTransferTimeoutMs = 120000;
constexpr int32_t kWarmupIterations = 1;
constexpr int32_t kTimedIterations = 10;
constexpr uint64_t kBytesPerGiB = 1024ULL * 1024ULL * 1024ULL;

const int32_t kBlockCounts[] = {16, 32, 48, 64};
constexpr size_t kMaxTotalBlocks = 64U;
// Minimum bytes needed for kMaxTotalBlocks; actual allocation is rounded up to whole GiB.
constexpr size_t kRequiredDataBytes = kMaxTotalBlocks * kBlockBytes;
constexpr int kFileBarrierPollIntervalMs = 10;
// Upper bound on kFileBarrierPollIntervalMs sleep polls for file-based barriers (~100 minutes total).
constexpr int kFileBarrierPollMaxIterations = 600000;

size_t PoolBytesRoundUpGiB(size_t min_bytes) {
  return ((min_bytes + kBytesPerGiB - 1U) / kBytesPerGiB) * kBytesPerGiB;
}

#define CHECK_ACL(x)                                                                 \
  do {                                                                               \
    aclError aclRet = (x);                                                              \
    if (aclRet != ACL_ERROR_NONE) {                                                     \
      std::cerr << __FILE__ << ":" << __LINE__ << " aclError:" << aclRet << std::endl;  \
      return aclRet;                                                                    \
    }                                                                                \
  } while (0)

const char *AclErrMsg() {
  const char *m = aclGetRecentErrMsg();
  return m != nullptr ? m : "no error";
}

std::string EngineId(const std::string &host, int base_port, int rank) {
  std::ostringstream oss;
  oss << host << ":" << (base_port + rank);
  return oss.str();
}

constexpr const char *kSyncReadyFile = ".fabric_mem_sync_ready";

// Rank 0 removes existing sync_dir (clean state) and recreates it; other ranks wait until ready.
int PrepareSyncDirRank0(const std::string &path) {
  namespace fs = std::filesystem;
  std::error_code ec;
  if (fs::exists(path, ec)) {
    (void)fs::remove_all(path, ec);
    if (ec) {
      return -1;
    }
  }
  fs::create_directories(path, ec);
  if (ec) {
    return -1;
  }
  const std::string flag = path + "/" + kSyncReadyFile;
  std::ofstream f(flag);
  if (!f.good()) {
    return -1;
  }
  f << "1\n";
  return 0;
}

bool WaitForSyncDirReady(const std::string &path) {
  namespace fs = std::filesystem;
  const std::string flag = path + "/" + kSyncReadyFile;
  for (int i = 0; i < kFileBarrierPollMaxIterations; ++i) {
    std::error_code ec;
    if (fs::exists(flag, ec)) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(kFileBarrierPollIntervalMs));
  }
  std::cerr << "[ERROR] timeout waiting for sync_dir (rank 0 must create " << flag << ")\n";
  return false;
}

// Unique tag per (block-config index, iteration) so concurrent clears cannot remove another rank's .done.
std::string IterBarrierTag(const char *base, int bi, int it) {
  return std::string(base) + "_b" + std::to_string(bi) + "_i" + std::to_string(it);
}

struct BarrierTags {
  std::string pre_put;
  std::string data_ready;
  std::string put_done;
  std::string get_us;
  std::string get_start;
  std::string get_done;

  static BarrierTags ForIteration(int bi, int it) {
    return BarrierTags{IterBarrierTag("pre_put", bi, it), IterBarrierTag("data_ready", bi, it),
                       IterBarrierTag("put_done", bi, it), IterBarrierTag("get_us", bi, it),
                       IterBarrierTag("get_start", bi, it), IterBarrierTag("get_done", bi, it)};
  }
};

bool BarrierFile(const std::string &sync_dir, const std::string &tag, int rank, int world) {
  namespace fs = std::filesystem;
  if (world <= 0) {
    std::cerr << "[ERROR] BarrierFile: world must be positive\n";
    return false;
  }
  const fs::path root(sync_dir);
  {
    std::ofstream f(root / (tag + "_" + std::to_string(rank) + ".done"));
    f << "1\n";
  }
  for (int poll = 0; poll < kFileBarrierPollMaxIterations; ++poll) {
    bool all_ready = true;
    for (int r = 0; r < world; ++r) {
      if (!fs::exists(root / (tag + "_" + std::to_string(r) + ".done"))) {
        all_ready = false;
        break;
      }
    }
    if (all_ready) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(kFileBarrierPollIntervalMs));
  }
  std::cerr << "[ERROR] BarrierFile timeout (sync_dir=" << sync_dir << " tag=" << tag << " rank=" << rank
      << " world=" << world << ")\n";
  return false;
}

// Returns slice size in bytes. Division uses world_size; must not be zero.
size_t PeerSliceBytes(int total_blocks, int world_size) {
  if (world_size <= 0) {
    return 0;
  }
  const int ws = world_size;
  const int blocks_per_peer = total_blocks / ws;
  return static_cast<size_t>(blocks_per_peer) * kBlockBytes;
}

int AllocateFabricBuffer(int32_t device_id, size_t size, void *&va, aclrtDrvMemHandle &pa_handle) {
  CHECK_ACL(aclrtReserveMemAddress(&va, size, 0, nullptr, 1));
  aclrtPhysicalMemProp prop{};
  prop.handleType = ACL_MEM_HANDLE_TYPE_NONE;
  prop.allocationType = ACL_MEM_ALLOCATION_TYPE_PINNED;
  prop.reserve = 0;
  prop.memAttr = ACL_HBM_MEM_HUGE;
  prop.location.type = ACL_MEM_LOCATION_TYPE_DEVICE;
  prop.location.id = device_id;
  CHECK_ACL(aclrtMallocPhysical(&pa_handle, size, &prop, 0));
  CHECK_ACL(aclrtMapMem(va, size, 0, pa_handle, 0));
  return ACL_ERROR_NONE;
}

void FreeFabricBuffer(void *va, aclrtDrvMemHandle pa_handle) {
  (void)aclrtUnmapMem(va);
  (void)aclrtFreePhysical(pa_handle);
  (void)aclrtReleaseMemAddress(va);
}

Status AllocFabricHostPool(size_t size, void **host_ptr) {
  return AdxlEngine::MallocMem(MemType::MEM_HOST, size, host_ptr);
}

void FreeFabricHostPool(void *host_ptr) {
  (void)AdxlEngine::FreeMem(host_ptr);
}

Status InitAdxlEngine(AdxlEngine &engine, const std::string &local_engine) {
  std::map<AscendString, AscendString> options;
  options[AscendString(hixl::OPTION_ENABLE_USE_FABRIC_MEM)] = AscendString("1");
  options[AscendString(hixl::OPTION_BUFFER_POOL)] = AscendString("0:0");
  return engine.Initialize(AscendString(local_engine.c_str()), options);
}

Status ConnectAllPeers(AdxlEngine &engine, int my_rank, const std::vector<std::string> &peer_engines) {
  for (size_t peer = 0; peer < peer_engines.size(); ++peer) {
    if (static_cast<int>(peer) == my_rank) {
      continue;
    }
    Status st = engine.Connect(AscendString(peer_engines[peer].c_str()), kConnectTimeoutMs);
    if (st != SUCCESS) {
      std::cerr << "[ERROR] Connect failed peer " << peer << " ret=" << st << " " << AclErrMsg() << std::endl;
      return st;
    }
  }
  return SUCCESS;
}

void DisconnectAllPeers(AdxlEngine &engine, int my_rank, const std::vector<std::string> &peer_engines) {
  for (size_t peer = 0; peer < peer_engines.size(); ++peer) {
    if (static_cast<int>(peer) == my_rank) {
      continue;
    }
    (void)engine.Disconnect(AscendString(peer_engines[peer].c_str()), kConnectTimeoutMs);
  }
}

void WriteAddrFile(const std::string &path, uintptr_t addr) {
  std::ofstream o(path);
  o << addr << "\n";
}

uintptr_t ReadAddrFile(const std::string &path) {
  std::ifstream i(path);
  uintptr_t v = 0;
  i >> v;
  return v;
}

void FillPattern(uint8_t *base, size_t len, int seed) {
  for (size_t i = 0; i < len; ++i) {
    base[i] = static_cast<uint8_t>((seed + static_cast<int>(i)) & 0xFF);
  }
}

int64_t RunPutRank0D2Rh(AdxlEngine &engine, const std::vector<std::string> &peer_engines,
                        const std::vector<uintptr_t> &peer_host_va, uint8_t *dev_src, uint8_t *host_local,
                        int total_blocks, int world_size) {
  const size_t chunk = PeerSliceBytes(total_blocks, world_size);
  const auto t0 = std::chrono::steady_clock::now();
  for (int dst = 0; dst < world_size; ++dst) {
    const size_t off = static_cast<size_t>(dst) * chunk;
    if (dst == 0) {
      if (aclrtMemcpy(host_local + off, chunk, dev_src + off, chunk, ACL_MEMCPY_DEVICE_TO_HOST) != ACL_ERROR_NONE) {
        std::cerr << "[ERROR] aclrtMemcpy D2H for local slice failed\n";
        return -1;
      }
      continue;
    }
    TransferOpDesc desc{};
    desc.local_addr = reinterpret_cast<uintptr_t>(dev_src + off);
    desc.remote_addr = peer_host_va[static_cast<size_t>(dst)] + off;
    desc.len = chunk;
    Status st = engine.TransferSync(AscendString(peer_engines[static_cast<size_t>(dst)].c_str()), WRITE, {desc},
                                    kTransferTimeoutMs);
    if (st != SUCCESS) {
      std::cerr << "[ERROR] Put D2RH WRITE failed dst=" << dst << " ret=" << st << std::endl;
      return -1;
    }
  }
  const auto t1 = std::chrono::steady_clock::now();
  return std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
}

int64_t RunGetAllRanksRh2D(AdxlEngine &engine, int my_rank, const std::vector<std::string> &peer_engines,
                           const std::vector<uintptr_t> &peer_host_va, uint8_t *dev_dst, uint8_t *host_local,
                           int total_blocks, int world_size) {
  const size_t chunk = PeerSliceBytes(total_blocks, world_size);
  const auto t0 = std::chrono::steady_clock::now();
  for (int src = 0; src < world_size; ++src) {
    const size_t off = static_cast<size_t>(src) * chunk;
    if (src == my_rank) {
      if (aclrtMemcpy(dev_dst + off, chunk, host_local + off, chunk, ACL_MEMCPY_HOST_TO_DEVICE) !=
          ACL_ERROR_NONE) {
        std::cerr << "[ERROR] H2D for local RH2D slice failed rank=" << my_rank << std::endl;
        return -1;
      }
      continue;
    }
    TransferOpDesc desc{};
    desc.local_addr = reinterpret_cast<uintptr_t>(dev_dst + off);
    desc.remote_addr = peer_host_va[static_cast<size_t>(src)] + off;
    desc.len = chunk;
    Status st = engine.TransferSync(AscendString(peer_engines[static_cast<size_t>(src)].c_str()), READ, {desc},
                                    kTransferTimeoutMs);
    if (st != SUCCESS) {
      std::cerr << "[ERROR] Get RH2D READ failed src=" << src << " rank=" << my_rank << " ret=" << st << std::endl;
      return -1;
    }
  }
  const auto t1 = std::chrono::steady_clock::now();
  return std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
}

void WriteTimeFile(const std::string &sync_dir, const std::string &tag, int rank, int64_t us) {
  std::string p = sync_dir + "/" + tag + "_" + std::to_string(rank) + ".txt";
  std::ofstream o(p);
  o << us << "\n";
}

int64_t ReadMaxTime(const std::string &sync_dir, const std::string &tag, int world) {
  int64_t mx = 0;
  for (int r = 0; r < world; ++r) {
    std::string p = sync_dir + "/" + tag + "_" + std::to_string(r) + ".txt";
    std::ifstream i(p);
    int64_t v = 0;
    i >> v;
    if (v > mx) {
      mx = v;
    }
  }
  return mx;
}

// Effective GiB/s for moving total_bytes in time_us microseconds.
double GbpsFromBytesAndUs(uint64_t total_bytes, int64_t time_us) {
  if (time_us <= 0) {
    return 0.0;
  }
  const double sec = static_cast<double>(time_us) / 1.0e6;
  return static_cast<double>(total_bytes) / static_cast<double>(kBytesPerGiB) / sec;
}

void PrintResultsForBlockConfig(int rank, int total_blocks, uint64_t total_bytes, int64_t sum_put_us,
                                int64_t sum_get_max_us, int64_t sum_get_own_us) {
  const double avg_get_own =
      static_cast<double>(sum_get_own_us) / static_cast<double>(kTimedIterations);
  std::cout << "[RESULT] rank=" << rank << " blocks=" << total_blocks
      << " get_rh2d_time_avg_us=" << avg_get_own
      << " get_rh2d_bandwidth_GBps=" << GbpsFromBytesAndUs(total_bytes, static_cast<int64_t>(avg_get_own))
      << std::endl;
  std::cout.flush();
  if (rank != 0) {
    return;
  }
  const double avg_put = static_cast<double>(sum_put_us) / static_cast<double>(kTimedIterations);
  const double avg_get_max = static_cast<double>(sum_get_max_us) / static_cast<double>(kTimedIterations);
  std::cout << "[RESULT] rank=0 blocks=" << total_blocks << " put_d2rh_time_avg_us=" << avg_put
      << " put_d2rh_bandwidth_GBps=" << GbpsFromBytesAndUs(total_bytes, static_cast<int64_t>(avg_put))
      << " get_rh2d_time_max_avg_us=" << avg_get_max
      << " get_rh2d_bandwidth_GBps=" << GbpsFromBytesAndUs(total_bytes, static_cast<int64_t>(avg_get_max))
      << std::endl;
}

bool ValidateBlockConfig(int world_size, int total_blocks) {
  if (world_size <= 0) {
    std::cerr << "[ERROR] world_size must be positive (avoid division/modulo by zero)\n";
    return false;
  }
  const int ws = world_size;
  if (total_blocks % ws != 0) {
    std::cerr << "[ERROR] total_blocks not divisible by world size\n";
    return false;
  }
  return true;
}

bool Rank0FillHostAndH2D(int total_blocks, int pattern_seed, uint8_t *dev_pool, uint8_t *host_pool) {
  const size_t nbytes = static_cast<size_t>(total_blocks) * kBlockBytes;
  FillPattern(host_pool, nbytes, pattern_seed);
  if (aclrtMemcpy(dev_pool, nbytes, host_pool, nbytes, ACL_MEMCPY_HOST_TO_DEVICE) != ACL_ERROR_NONE) {
    std::cerr << "[ERROR] H2D fill failed\n";
    return false;
  }
  return true;
}

struct BlockConfigIterationSample {
  bool ok;
  int64_t put_us;
  int64_t gu;
};

BlockConfigIterationSample RunBlockConfigIteration(AdxlEngine &engine, int rank, int world_size,
                                                   const std::vector<std::string> &peer_engines,
                                                   const std::vector<uintptr_t> &peer_host_va,
                                                   uint8_t *dev_pool, uint8_t *host_pool,
                                                   const std::string &sync_dir, const BarrierTags &tags,
                                                   int total_blocks, int it) {
  if (!BarrierFile(sync_dir, tags.pre_put, rank, world_size)) {
    return {false, 0, 0};
  }
  if (rank == 0) {
    if (!Rank0FillHostAndH2D(total_blocks, it + total_blocks, dev_pool, host_pool)) {
      return {false, 0, 0};
    }
  }
  if (!BarrierFile(sync_dir, tags.data_ready, rank, world_size)) {
    return {false, 0, 0};
  }

  int64_t put_us = 0;
  if (rank == 0) {
    const int64_t pu =
        RunPutRank0D2Rh(engine, peer_engines, peer_host_va, dev_pool, host_pool, total_blocks, world_size);
    if (pu < 0) {
      return {false, 0, 0};
    }
    put_us = pu;
  }
  if (!BarrierFile(sync_dir, tags.put_done, rank, world_size)) {
    return {false, 0, 0};
  }
  if (!BarrierFile(sync_dir, tags.get_start, rank, world_size)) {
    return {false, 0, 0};
  }

  const int64_t gu = RunGetAllRanksRh2D(engine, rank, peer_engines, peer_host_va, dev_pool, host_pool,
                                        total_blocks, world_size);
  if (gu < 0) {
    return {false, 0, 0};
  }
  WriteTimeFile(sync_dir, tags.get_us, rank, gu);
  if (!BarrierFile(sync_dir, tags.get_done, rank, world_size)) {
    return {false, 0, 0};
  }
  return {true, put_us, gu};
}

bool RunBenchmarkForBlockConfig(AdxlEngine &engine, int rank, int world_size,
                                const std::vector<std::string> &peer_engines,
                                const std::vector<uintptr_t> &peer_host_va, uint8_t *dev_pool, uint8_t *host_pool,
                                const std::string &sync_dir, int bi, int total_blocks) {
  if (!ValidateBlockConfig(world_size, total_blocks)) {
    return false;
  }
  const uint64_t total_bytes = static_cast<uint64_t>(total_blocks) * static_cast<uint64_t>(kBlockBytes);
  int64_t sum_put_us = 0;
  int64_t sum_get_max_us = 0;
  int64_t sum_get_own_us = 0;

  for (int it = 0; it < kWarmupIterations + kTimedIterations; ++it) {
    const BarrierTags tags = BarrierTags::ForIteration(bi, it);
    const BlockConfigIterationSample sample = RunBlockConfigIteration(
        engine, rank, world_size, peer_engines, peer_host_va, dev_pool, host_pool, sync_dir, tags, total_blocks,
        it);
    if (!sample.ok) {
      break;
    }
    if (it < kWarmupIterations) {
      continue;
    }
    if (rank == 0) {
      sum_put_us += sample.put_us;
      sum_get_max_us += ReadMaxTime(sync_dir, tags.get_us, world_size);
    }
    sum_get_own_us += sample.gu;
  }

  PrintResultsForBlockConfig(rank, total_blocks, total_bytes, sum_put_us, sum_get_max_us, sum_get_own_us);
  return true;
}

void PrintUsage(const char *prog) {
  std::cerr << "Usage: " << prog
      << " <device_id> <rank> <host_ip> <base_port> <sync_dir> [world_size]\n"
      << "  world_size: default " << kDefaultWorldSize
      << ", must match number of processes (e.g. 2 for two-process smoke test).\n"
      << "sync_dir: if it exists, rank 0 removes it first (other ranks wait). Put timing on rank 0 only;\n"
      << "get runs on every rank — each rank prints its get avg; rank 0 also prints put + get(max).\n"
      << "Ranks may look \"stuck\" at file barriers until all world_size processes reach the same step.\n";
}

bool PrepareSyncDirForRank(int rank, const std::string &sync_dir) {
  if (rank == 0) {
    if (PrepareSyncDirRank0(sync_dir) != 0) {
      std::cerr << "[ERROR] prepare sync_dir failed (remove or create): " << sync_dir << std::endl;
      return false;
    }
  }
  return WaitForSyncDirReady(sync_dir);
}

std::vector<std::string> BuildPeerEngineIds(const std::string &host_ip, int base_port, int world_size) {
  std::vector<std::string> peer_engines(static_cast<size_t>(world_size));
  for (int r = 0; r < world_size; ++r) {
    peer_engines[static_cast<size_t>(r)] = EngineId(host_ip, base_port, r);
  }
  return peer_engines;
}

bool InitEngineOrCleanup(AdxlEngine &engine, const std::string &local_engine, int32_t device_id) {
  if (InitAdxlEngine(engine, local_engine) != SUCCESS) {
    std::cerr << "[ERROR] AdxlEngine::Initialize failed " << AclErrMsg() << std::endl;
    (void)aclrtResetDevice(device_id);
    aclFinalize();
    return false;
  }
  return true;
}

// Allocates host + device pools and registers MEM_HOST. On failure, tears down engine and ACL as needed.
bool AllocPoolsAndRegisterHost(AdxlEngine &engine, int32_t device_id, size_t pool_bytes, void *&host_raw,
                               uint8_t *&host_pool, void *&va, aclrtDrvMemHandle &pa_handle,
                               MemHandle &mem_host_handle) {
  host_raw = nullptr;
  va = nullptr;
  pa_handle = {};
  mem_host_handle = nullptr;
  if (AllocFabricHostPool(pool_bytes, &host_raw) != SUCCESS) {
    std::cerr << "[ERROR] AdxlEngine::MallocMem MEM_HOST failed\n";
    engine.Finalize();
    (void)aclrtResetDevice(device_id);
    aclFinalize();
    return false;
  }
  host_pool = reinterpret_cast<uint8_t *>(host_raw);
  if (AllocateFabricBuffer(device_id, pool_bytes, va, pa_handle) != ACL_ERROR_NONE) {
    FreeFabricHostPool(host_raw);
    engine.Finalize();
    (void)aclrtResetDevice(device_id);
    aclFinalize();
    return false;
  }
  MemDesc mem_host{};
  mem_host.addr = reinterpret_cast<uintptr_t>(host_pool);
  mem_host.len = pool_bytes;
  if (engine.RegisterMem(mem_host, MEM_HOST, mem_host_handle) != SUCCESS) {
    std::cerr << "[ERROR] RegisterMem MEM_HOST failed\n";
    FreeFabricBuffer(va, pa_handle);
    FreeFabricHostPool(host_raw);
    engine.Finalize();
    (void)aclrtResetDevice(device_id);
    aclFinalize();
    return false;
  }
  return true;
}

void LogRankEntered(int rank, int world_size, const std::string &sync_dir) {
  std::cerr << "[INFO] rank " << rank << "/" << world_size << " entered (sync_dir=" << sync_dir << ")\n";
  std::cerr.flush();
}

bool ExchangeHostAddrsAndConnect(AdxlEngine &engine, int rank, int world_size,
                                 const std::string &sync_dir,
                                 const std::vector<std::string> &peer_engines, uint8_t *host_pool,
                                 std::vector<uintptr_t> &peer_host_va) {
  const std::string addr_path = sync_dir + "/addr_host_" + std::to_string(rank) + ".txt";
  WriteAddrFile(addr_path, reinterpret_cast<uintptr_t>(host_pool));
  if (!BarrierFile(sync_dir, "reg", rank, world_size)) {
    return false;
  }
  peer_host_va.assign(static_cast<size_t>(world_size), 0);
  for (int r = 0; r < world_size; ++r) {
    peer_host_va[static_cast<size_t>(r)] =
        ReadAddrFile(sync_dir + "/addr_host_" + std::to_string(r) + ".txt");
  }
  if (ConnectAllPeers(engine, rank, peer_engines) != SUCCESS) {
    DisconnectAllPeers(engine, rank, peer_engines);
    return false;
  }
  return true;
}

void PrintRank0PoolBanner(int rank, int world_size, size_t pool_bytes) {
  if (rank != 0) {
    return;
  }
  const double pool_gib = static_cast<double>(pool_bytes) / static_cast<double>(kBytesPerGiB);
  const double need_gib = static_cast<double>(kRequiredDataBytes) / static_cast<double>(kBytesPerGiB);
  std::cout << "[INFO] FabricMem KV benchmark (AdxlEngine API): block_bytes=" << kBlockBytes
      << " (DeepSeek-R1-like: 61x128K + 61x16K), world=" << world_size
      << ", put=D2RH, get=RH2D"
      << ", pool=" << pool_gib << " GiB (>= min " << need_gib << " GiB, rounded up per GiB)"
      << std::endl;
}

void RunAllBlockConfigs(AdxlEngine &engine, int rank, int world_size,
                        const std::vector<std::string> &peer_engines,
                        const std::vector<uintptr_t> &peer_host_va, uint8_t *dev_pool, uint8_t *host_pool,
                        const std::string &sync_dir) {
  constexpr int kNumBlockConfigs = static_cast<int>(sizeof(kBlockCounts) / sizeof(kBlockCounts[0]));
  for (int bi = 0; bi < kNumBlockConfigs; ++bi) {
    if (!RunBenchmarkForBlockConfig(engine, rank, world_size, peer_engines, peer_host_va, dev_pool, host_pool,
                                    sync_dir, bi, kBlockCounts[bi])) {
      break;
    }
  }
}

void FinalizeBenchmark(AdxlEngine &engine, int rank,
                       const std::vector<std::string> &peer_engines, MemHandle mem_host_handle, void *va,
                       aclrtDrvMemHandle pa_handle, void *host_raw, int32_t device_id,
                       bool check_acl_on_reset) {
  DisconnectAllPeers(engine, rank, peer_engines);
  (void)engine.DeregisterMem(mem_host_handle);
  FreeFabricBuffer(va, pa_handle);
  FreeFabricHostPool(host_raw);
  engine.Finalize();
  if (check_acl_on_reset) {
    (void)aclrtResetDevice(device_id);
  } else {
    (void)aclrtResetDevice(device_id);
  }
  aclFinalize();
}

// Runs benchmark after CLI has been validated. Returns process exit code (0 success, 1 failure).
int RunFabricMemKvBenchmarkBody(int32_t device_id, int32_t rank, const std::string &host_ip, int32_t base_port,
                                const std::string &sync_dir, int32_t world_size) {
  if (!PrepareSyncDirForRank(rank, sync_dir)) {
    return 1;
  }
  CHECK_ACL(aclInit(nullptr));
  CHECK_ACL(aclrtSetDevice(device_id));
  const std::vector<std::string> peer_engines = BuildPeerEngineIds(host_ip, base_port, world_size);
  const std::string local_engine = peer_engines[static_cast<size_t>(rank)];
  AdxlEngine engine;
  if (!InitEngineOrCleanup(engine, local_engine, device_id)) {
    return 1;
  }
  const size_t pool_bytes = PoolBytesRoundUpGiB(kRequiredDataBytes);
  void *host_raw = nullptr;
  void *va = nullptr;
  aclrtDrvMemHandle pa_handle{};
  MemHandle mem_host_handle = nullptr;
  uint8_t *host_pool = nullptr;
  if (!AllocPoolsAndRegisterHost(engine, device_id, pool_bytes, host_raw, host_pool, va, pa_handle,
                                 mem_host_handle)) {
    return 1;
  }
  auto *dev_pool = reinterpret_cast<uint8_t *>(va);
  LogRankEntered(rank, world_size, sync_dir);
  std::vector<uintptr_t> peer_host_va;
  if (!ExchangeHostAddrsAndConnect(engine, rank, world_size, sync_dir, peer_engines, host_pool, peer_host_va)) {
    FinalizeBenchmark(engine, rank, peer_engines, mem_host_handle, va, pa_handle, host_raw, device_id, false);
    return 1;
  }
  PrintRank0PoolBanner(rank, world_size, pool_bytes);
  RunAllBlockConfigs(engine, rank, world_size, peer_engines, peer_host_va, dev_pool, host_pool, sync_dir);
  FinalizeBenchmark(engine, rank, peer_engines, mem_host_handle, va, pa_handle, host_raw, device_id, true);
  return 0;
}
} // namespace

int main(int argc, char **argv) {
  if (argc != kArgcWithRequiredArgsOnly && argc != kArgcWithOptionalWorldSize) {
    PrintUsage(argv[0]);
    return 1;
  }
  const int32_t device_id = std::stoi(argv[kArgIndexDeviceId]);
  const int32_t rank = std::stoi(argv[kArgIndexRank]);
  const std::string host_ip = argv[kArgIndexHostIp];
  const int32_t base_port = std::stoi(argv[kArgIndexBasePort]);
  const std::string sync_dir = argv[kArgIndexSyncDir];
  int32_t world_size = kDefaultWorldSize;
  if (argc == kArgcWithOptionalWorldSize) {
    world_size = std::stoi(argv[kArgIndexWorldSize]);
  }
  if (world_size < kMinWorldSize || world_size > kMaxWorldSize) {
    std::cerr << "[ERROR] world_size must be in [" << kMinWorldSize << ", " << kMaxWorldSize << "]\n";
    return 1;
  }
  if (rank < 0 || rank >= world_size) {
    std::cerr << "[ERROR] rank must be in [0, world_size-1]\n";
    return 1;
  }
  return RunFabricMemKvBenchmarkBody(device_id, rank, host_ip, base_port, sync_dir, world_size);
}