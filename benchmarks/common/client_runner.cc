/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "client_runner.h"

#include <atomic>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "acl/acl.h"

using hixl::AscendString;
using hixl::Hixl;
using hixl::MemDesc;
using hixl::MemHandle;
using hixl::MemType;
using hixl::SUCCESS;
using hixl::TransferOp;
using hixl::TransferOpDesc;

namespace {

using hixl_benchmark::BenchmarkConfig;
using hixl_benchmark::BenchmarkConfigParser;

const char *RecentErrMsg() {
  const char *errmsg = aclGetRecentErrMsg();
  if (errmsg == nullptr) {
    return "no error";
  }
  return errmsg;
}

int32_t InitializeHixl(const std::string &local_engine, const BenchmarkConfig &cfg, Hixl *hixl) {
  const std::map<AscendString, AscendString> init_options = BenchmarkConfigParser::BuildInitializeOptions(cfg);
  const auto ret = hixl->Initialize(AscendString(local_engine.c_str()), init_options);
  if (ret != SUCCESS) {
    std::printf("[ERROR] Initialize failed, ret = %u, errmsg: %s\n", ret, RecentErrMsg());
    return -1;
  }
  std::printf("[INFO] Initialize success local_engine=%s\n", local_engine.c_str());
  return 0;
}

void ReleaseHixlResources(Hixl &hixl_engine, bool need_register, bool is_host, const std::vector<MemHandle> &handles,
                          const std::vector<void *> &buffers) {
  if (need_register) {
    for (const auto &element : handles) {
      if (element == nullptr) {
        continue;
      }
      auto ret = hixl_engine.DeregisterMem(element);
      if (ret != 0) {
        std::printf("[ERROR] DeregisterMem failed, ret = %u, errmsg: %s\n", ret, RecentErrMsg());
      } else {
        std::printf("[INFO] DeregisterMem success\n");
      }
    }
  }
  if (is_host) {
    for (const auto &element : buffers) {
      if (element != nullptr) {
        (void)aclrtFreeHost(element);
      }
    }
  } else {
    for (const auto &element : buffers) {
      if (element != nullptr) {
        (void)aclrtFree(element);
      }
    }
  }
  hixl_engine.Finalize();
}

int32_t AllocLocalBuffer(const BenchmarkConfig &cfg, bool *is_host, void **out_src, size_t alloc_size) {
  *is_host = (cfg.transfer_mode == "h2d" || cfg.transfer_mode == "h2h");
  void *tmp = nullptr;
  aclError er = ACL_ERROR_NONE;
  if (*is_host) {
    er = aclrtMallocHost(&tmp, alloc_size);
  } else {
    er = aclrtMalloc(&tmp, alloc_size, ACL_MEM_MALLOC_HUGE_ONLY);
  }
  if (er != ACL_ERROR_NONE) {
    std::fprintf(stderr, "[ERROR] client alloc failed aclError=%d\n", static_cast<int>(er));
    return -1;
  }
  *out_src = tmp;
  return 0;
}

int32_t RegisterLocalMem(Hixl &hixl_engine, const BenchmarkConfig &cfg, void *src, bool is_host,
                         bool need_register, size_t register_len, MemHandle *handle) {
  (void)cfg;
  if (!need_register) {
    return 0;
  }
  MemDesc desc{};
  desc.addr = reinterpret_cast<uintptr_t>(src);
  desc.len = register_len;
  const auto ret = hixl_engine.RegisterMem(desc, is_host ? MemType::MEM_HOST : MemType::MEM_DEVICE, *handle);
  if (ret != SUCCESS) {
    std::printf("[ERROR] RegisterMem failed, ret = %u, errmsg: %s\n", ret, RecentErrMsg());
    return -1;
  }
  std::printf("[INFO] RegisterMem success len=%zu\n", register_len);
  return 0;
}

bool GetRemoteAddr(TCPClient *tcp_client, const std::string &remote_engine, uint16_t tcp_port, uint64_t *out_remote_addr) {
  const std::string host = hixl_benchmark::ExtractTcpHost(remote_engine);
  if (host.empty()) {
    return false;
  }
  if (!tcp_client->ConnectToServer(host, tcp_port)) {
    return false;
  }
  if (!tcp_client->ReceiveUint64(out_remote_addr)) {
    return false;
  }
  if (!tcp_client->ReceiveTaskStatus()) {
    return false;
  }
  return true;
}

bool SendNotify(TCPClient *tcp_client) { return tcp_client->SendTaskStatus(); }

void DisconnectAllRemoteEngines(Hixl &hixl, const std::vector<std::string> &remotes) {
  for (const auto &re : remotes) {
    const auto ret = hixl.Disconnect(AscendString(re.c_str()));
    if (ret != SUCCESS) {
      std::printf("[ERROR] Disconnect failed for %s, ret = %u, errmsg: %s\n", re.c_str(), ret, RecentErrMsg());
      continue;
    }
    std::printf("[INFO] Disconnect success\n");
  }
}

void MarkFirstFail(std::atomic<int> *first_fail, std::mutex *fail_mu) {
  std::lock_guard<std::mutex> lk(*fail_mu);
  if (first_fail->load() == 0) {
    first_fail->store(1);
  }
}

constexpr int32_t kWaitTransTime = 20;

using hixl_benchmark::detail::BenchWorkerTag;
using hixl_benchmark::detail::TransferBenchRecord;

void PrintBenchRecords(const std::vector<TransferBenchRecord> &recs) {
  for (const auto &r : recs) {
    if (r.tag == BenchWorkerTag::kLane) {
      std::printf(
          "[INFO] [lane %zu] Transfer success, loop %u/%u, step %u, block size: %u Bytes, transfer num: %u, time cost: "
          "%ld us, throughput: %.3lf GB/s\n",
          r.worker_index, r.loop_plus_one, r.loops_total, r.step_index, r.block_size, r.trans_num,
          static_cast<long>(r.time_us), r.throughput_gbps);
    } else if (r.tag == BenchWorkerTag::kRemote) {
      std::printf(
          "[INFO] [remote %zu] Transfer success, loop %u/%u, step %u, block size: %u Bytes, transfer num: %u, time "
          "cost: %ld us, throughput: %.3lf GB/s\n",
          r.worker_index, r.loop_plus_one, r.loops_total, r.step_index, r.block_size, r.trans_num,
          static_cast<long>(r.time_us), r.throughput_gbps);
    } else {
      std::printf(
          "[INFO] Transfer success, loop %u/%u, step %u, block size: %u Bytes, transfer num: %u, time cost: %ld us, "
          "throughput: %.3lf GB/s\n",
          r.loop_plus_one, r.loops_total, r.step_index, r.block_size, r.trans_num, static_cast<long>(r.time_us),
          r.throughput_gbps);
    }
  }
}

struct TransferBlockStepCtx {
  uintptr_t base = 0;
  const char *remote_engine = nullptr;
  uint64_t dst_addr = 0;
  const BenchmarkConfig *cfg = nullptr;
  TransferOp transfer_op = TransferOp::READ;
  uint32_t loop = 0;
  uint32_t step_index = 0;
  uint64_t block_size_u = 0;
  std::vector<TransferBenchRecord> *bench_records = nullptr;
  BenchWorkerTag bench_worker_tag = BenchWorkerTag::kSingle;
  std::size_t bench_worker_index = 0;
};

int32_t TransferOneBlockStep(Hixl &hixl_engine, const TransferBlockStepCtx &ctx) {
  const auto block_size = static_cast<uint32_t>(ctx.block_size_u);
  if (static_cast<uint64_t>(block_size) != ctx.block_size_u) {
    std::printf("[ERROR] block size too large at step %u\n", ctx.step_index);
    return -1;
  }
  const auto trans_num = static_cast<uint32_t>(ctx.cfg->total_size / ctx.block_size_u);
  std::vector<TransferOpDesc> descs;
  descs.reserve(trans_num);
  for (uint32_t j = 0; j < trans_num; j++) {
    TransferOpDesc desc{};
    desc.local_addr = ctx.base + static_cast<uintptr_t>(j) * block_size;
    desc.remote_addr = static_cast<uintptr_t>(ctx.dst_addr) + static_cast<uintptr_t>(j) * block_size;
    desc.len = block_size;
    descs.emplace_back(desc);
  }
  const auto start = std::chrono::steady_clock::now();
  const auto ret =
      hixl_engine.TransferSync(AscendString(ctx.remote_engine), ctx.transfer_op, descs, 1000 * kWaitTransTime);
  if (ret != SUCCESS) {
    std::printf("[ERROR] TransferSync failed, ret = %u, errmsg: %s\n", ret, RecentErrMsg());
    return -1;
  }
  const auto time_cost =
      std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
  const double time_second = static_cast<double>(time_cost) / 1000 / 1000;
  const double throughput = static_cast<double>(ctx.cfg->total_size) / 1024 / 1024 / 1024 / time_second;
  if (ctx.bench_records != nullptr) {
    TransferBenchRecord rec{};
    rec.tag = ctx.bench_worker_tag;
    rec.worker_index = ctx.bench_worker_index;
    rec.loop_plus_one = ctx.loop + 1U;
    rec.loops_total = ctx.cfg->loops;
    rec.step_index = ctx.step_index;
    rec.block_size = block_size;
    rec.trans_num = trans_num;
    rec.time_us = time_cost;
    rec.throughput_gbps = throughput;
    ctx.bench_records->push_back(rec);
  } else {
    std::printf(
        "[INFO] Transfer success, loop %u/%u, step %u, block size: %u Bytes, transfer num: %u, time cost: %ld us, "
        "throughput: %.3lf GB/s\n",
        ctx.loop + 1U, ctx.cfg->loops, ctx.step_index, block_size, trans_num, static_cast<long>(time_cost), throughput);
  }
  return 0;
}

int32_t RunTransfer(Hixl &hixl_engine, void *src_base, const char *remote_engine, uint64_t dst_addr,
                    const BenchmarkConfig &cfg, std::vector<TransferBenchRecord> *bench_records = nullptr,
                    BenchWorkerTag bench_worker_tag = BenchWorkerTag::kSingle,
                    std::size_t bench_worker_index = 0) {
  const TransferOp transfer_op = (cfg.transfer_op == "read") ? TransferOp::READ : TransferOp::WRITE;
  const uintptr_t base = reinterpret_cast<uintptr_t>(src_base);
  TransferBlockStepCtx step_ctx{};
  step_ctx.base = base;
  step_ctx.remote_engine = remote_engine;
  step_ctx.dst_addr = dst_addr;
  step_ctx.cfg = &cfg;
  step_ctx.transfer_op = transfer_op;
  step_ctx.bench_records = bench_records;
  step_ctx.bench_worker_tag = bench_worker_tag;
  step_ctx.bench_worker_index = bench_worker_index;
  for (uint32_t loop = 0; loop < cfg.loops; ++loop) {
    step_ctx.loop = loop;
    for (uint32_t i = 0; i < cfg.block_steps; ++i) {
      step_ctx.step_index = i;
      step_ctx.block_size_u = cfg.block_size << i;
      const int32_t step_ret = TransferOneBlockStep(hixl_engine, step_ctx);
      if (step_ret != 0) {
        return step_ret;
      }
    }
  }
  return 0;
}

bool SharedRemoteConnectTransferAndCleanup(Hixl *hixl, size_t idx, void *slice_base, const std::string &remote,
                                           uint64_t remote_addr, const BenchmarkConfig &cfg, TCPClient *tcp_client,
                                           std::atomic<int> *first_fail, std::mutex *fail_mu,
                                           std::vector<TransferBenchRecord> *bench_records) {
  const auto connect_ret = hixl->Connect(AscendString(remote.c_str()));
  if (connect_ret != SUCCESS) {
    std::printf("[ERROR] [remote %zu] Connect failed ret=%u %s\n", idx, connect_ret, RecentErrMsg());
    (void)SendNotify(tcp_client);
    MarkFirstFail(first_fail, fail_mu);
    return false;
  }
  if (RunTransfer(*hixl, slice_base, remote.c_str(), remote_addr, cfg, bench_records, BenchWorkerTag::kRemote,
                  idx) != 0) {
    MarkFirstFail(first_fail, fail_mu);
    (void)hixl->Disconnect(AscendString(remote.c_str()));
    (void)SendNotify(tcp_client);
    return false;
  }
  return true;
}

void SharedRemoteWorker(size_t idx, Hixl *hixl, const BenchmarkConfig &cfg, void *slice_base, std::atomic<int> *first_fail,
                        std::mutex *fail_mu, std::vector<TransferBenchRecord> *bench_records) {
  const std::string &remote = cfg.expanded_remote_engines[idx];
  std::printf("[INFO] [remote %zu] worker start -> %s\n", idx, remote.c_str());

  TCPClient tcp_client;
  uint64_t remote_addr = 0;
  if (!GetRemoteAddr(&tcp_client, remote, cfg.expanded_tcp_ports[idx], &remote_addr)) {
    MarkFirstFail(first_fail, fail_mu);
    return;
  }
  if (remote_addr != 0U) {
    std::printf("[INFO] [remote %zu] server mem addr: 0x%" PRIx64 "\n", idx, remote_addr);
  }

  if (!SharedRemoteConnectTransferAndCleanup(hixl, idx, slice_base, remote, remote_addr, cfg, &tcp_client, first_fail,
                                             fail_mu, bench_records)) {
    return;
  }

  const auto disconnect_ret = hixl->Disconnect(AscendString(remote.c_str()));
  if (disconnect_ret != SUCCESS) {
    std::printf("[ERROR] [remote %zu] Disconnect failed ret=%u\n", idx, disconnect_ret);
  }
  if (!SendNotify(&tcp_client)) {
    MarkFirstFail(first_fail, fail_mu);
  }
  std::printf("[INFO] [remote %zu] worker done\n", idx);
}

}  // namespace

namespace hixl_benchmark::detail {

void FinalizeLaneState(LaneState *p, const std::string &remote_engine) {
  if (p->hixl_connected) {
    const auto ret = p->hixl.Disconnect(AscendString(remote_engine.c_str()));
    if (ret != SUCCESS) {
      std::printf("[ERROR] Disconnect failed, ret = %u, errmsg: %s\n", ret, RecentErrMsg());
    } else {
      std::printf("[INFO] Disconnect success\n");
    }
    p->hixl_connected = false;
  }
  if (p->hixl_initialized) {
    if (p->buffer != nullptr) {
      ReleaseHixlResources(p->hixl, p->need_register, p->is_host, {p->mem_handle}, {p->buffer});
    } else {
      ReleaseHixlResources(p->hixl, p->need_register, p->is_host, {p->mem_handle}, {});
    }
    p->hixl_initialized = false;
    p->buffer = nullptr;
    p->mem_handle = nullptr;
  }
}

bool LaneWorkerSetDevice(size_t idx, int32_t dev, std::atomic<int> *first_fail, std::mutex *fail_mu) {
  aclError ar = aclrtSetDevice(dev);
  if (ar != ACL_ERROR_NONE) {
    std::printf("[ERROR] [lane %zu] aclrtSetDevice failed %d\n", idx, static_cast<int>(ar));
    MarkFirstFail(first_fail, fail_mu);
    return false;
  }
  return true;
}

bool LaneWorkerInitHixlEngine(LaneState *p, const BenchmarkConfig &cfg, const std::string &local,
                              std::atomic<int> *first_fail, std::mutex *fail_mu) {
  if (InitializeHixl(local, cfg, &p->hixl) != 0) {
    p->hixl.Finalize();
    MarkFirstFail(first_fail, fail_mu);
    return false;
  }
  p->hixl_initialized = true;
  return true;
}

bool LaneWorkerAllocAndRegisterMem(LaneState *p, const BenchmarkConfig &cfg, std::atomic<int> *first_fail,
                                   std::mutex *fail_mu) {
  const size_t alloc_size = static_cast<size_t>(cfg.total_size);
  if (AllocLocalBuffer(cfg, &p->is_host, &p->buffer, alloc_size) != 0) {
    MarkFirstFail(first_fail, fail_mu);
    return false;
  }
  p->need_register = !(p->is_host && cfg.use_buffer_pool);
  if (RegisterLocalMem(p->hixl, cfg, p->buffer, p->is_host, p->need_register, alloc_size, &p->mem_handle) != 0) {
    MarkFirstFail(first_fail, fail_mu);
    return false;
  }
  return true;
}

bool LaneWorkerRemoteTransferPhase(LaneState *p, const BenchmarkConfig &cfg, size_t lane_idx,
                                   const std::string &remote, std::atomic<int> *first_fail, std::mutex *fail_mu) {
  uint64_t remote_addr = 0;
  if (!GetRemoteAddr(&p->tcp_client, remote, cfg.expanded_tcp_ports[lane_idx], &remote_addr)) {
    MarkFirstFail(first_fail, fail_mu);
    return false;
  }
  if (remote_addr != 0U) {
    std::printf("[INFO] Success to receive server mem addr: 0x%" PRIx64 "\n", remote_addr);
  }

  const auto connect_ret = p->hixl.Connect(AscendString(remote.c_str()));
  if (connect_ret != SUCCESS) {
    std::printf("[ERROR] Connect failed, ret = %u, errmsg: %s\n", connect_ret, RecentErrMsg());
    MarkFirstFail(first_fail, fail_mu);
    return false;
  }
  p->hixl_connected = true;

  p->bench_records.clear();
  if (RunTransfer(p->hixl, p->buffer, remote.c_str(), remote_addr, cfg, &p->bench_records, BenchWorkerTag::kLane,
                  lane_idx) != 0) {
    MarkFirstFail(first_fail, fail_mu);
    return false;
  }
  return true;
}

void LaneWorkerEntry(size_t idx, LaneState *p, const BenchmarkConfig &cfg, std::atomic<int> *first_fail,
                     std::mutex *fail_mu) {
  const int32_t dev = cfg.expanded_device_ids[idx];
  const std::string &local = cfg.expanded_local_engines[idx];
  const std::string &remote = cfg.expanded_remote_engines[idx];
  std::printf("[INFO] [lane %zu] start device=%d\n", idx, static_cast<int>(dev));

  if (!LaneWorkerSetDevice(idx, dev, first_fail, fail_mu)) {
    return;
  }
  if (!LaneWorkerInitHixlEngine(p, cfg, local, first_fail, fail_mu)) {
    return;
  }
  if (!LaneWorkerAllocAndRegisterMem(p, cfg, first_fail, fail_mu)) {
    return;
  }
  if (!LaneWorkerRemoteTransferPhase(p, cfg, idx, remote, first_fail, fail_mu)) {
    return;
  }

  const auto disconnect_ret = p->hixl.Disconnect(AscendString(remote.c_str()));
  if (disconnect_ret != SUCCESS) {
    std::printf("[ERROR] Disconnect failed, ret = %u, errmsg: %s\n", disconnect_ret, RecentErrMsg());
  } else {
    std::printf("[INFO] Disconnect success\n");
  }
  p->hixl_connected = false;
  (void)SendNotify(&p->tcp_client);

  std::printf("[INFO] [lane %zu] end\n", idx);
}

}  // namespace hixl_benchmark::detail

namespace hixl_benchmark {

ClientRunner::~ClientRunner() { Shutdown(); }

void ClientRunner::ReleaseAllLaneRuntimes() {
  for (auto &t : multi_lane_threads_) {
    if (t.joinable()) {
      t.join();
    }
  }
  multi_lane_threads_.clear();

  const size_t n = lane_runtimes_.size();
  for (size_t i = 0; i < n; ++i) {
    if (lane_runtimes_[i]) {
      detail::FinalizeLaneState(lane_runtimes_[i].get(), cfg_.expanded_remote_engines[i]);
      (void)aclrtResetDevice(cfg_.expanded_device_ids[i]);
    }
  }
  lane_runtimes_.clear();
}

void ClientRunner::ReleaseLaneResources() {
  if (!lane_resources_active_) {
    return;
  }
  if (cfg_.expanded_remote_engines.empty()) {
    lane_resources_active_ = false;
    return;
  }
  if (!lane_hixl_initialized_) {
    lane_resources_active_ = false;
    return;
  }

  const bool skip_bulk_disconnect = lane_shared_multi_remote_workers_disconnected_ &&
                                   cfg_.expanded_remote_engines.size() > 1U &&
                                   cfg_.local_engine_list.size() == 1U;
  lane_shared_multi_remote_workers_disconnected_ = false;
  if (!skip_bulk_disconnect) {
    DisconnectAllRemoteEngines(lane_hixl_, cfg_.expanded_remote_engines);
  }
  lane_hixl_connected_ = false;

  if (lane_need_tcp_notify_) {
    (void)SendNotify(&lane_tcp_);
    lane_need_tcp_notify_ = false;
  }
  if (lane_tcp_handshake_ok_) {
    lane_tcp_.Disconnect();
    lane_tcp_handshake_ok_ = false;
  }

  if (lane_buffer_ != nullptr) {
    ReleaseHixlResources(lane_hixl_, lane_need_register_, lane_is_host_, {lane_mem_handle_}, {lane_buffer_});
  } else {
    ReleaseHixlResources(lane_hixl_, lane_need_register_, lane_is_host_, {lane_mem_handle_}, {});
  }
  lane_hixl_initialized_ = false;
  lane_buffer_ = nullptr;
  lane_mem_handle_ = nullptr;
  lane_resources_active_ = false;
}

bool ClientRunner::Init() {
  const bool multi_local = cfg_.local_engine_list.size() > 1U;
  if (multi_local) {
    return true;
  }
  device_id_ = cfg_.expanded_device_ids[0];
  if (aclrtSetDevice(device_id_) != ACL_ERROR_NONE) {
    return false;
  }
  device_bound_ = true;
  return true;
}

void ClientRunner::Shutdown() {
  ReleaseLaneResources();
  ReleaseAllLaneRuntimes();
  if (device_bound_) {
    (void)aclrtResetDevice(device_id_);
    device_bound_ = false;
  }
}

int ClientRunner::RunOnePair(const std::string &remote, void *src_slice, size_t register_len) {
  std::printf("[INFO] client start remote=%s\n", remote.c_str());

  lane_need_register_ = !(lane_is_host_ && cfg_.use_buffer_pool);
  if (RegisterLocalMem(lane_hixl_, cfg_, src_slice, lane_is_host_, lane_need_register_, register_len,
                       &lane_mem_handle_) != 0) {
    return -1;
  }

  uint64_t remote_addr = 0;
  if (!GetRemoteAddr(&lane_tcp_, remote, cfg_.expanded_tcp_ports[0], &remote_addr)) {
    return -1;
  }
  lane_tcp_handshake_ok_ = true;
  if (remote_addr != 0U) {
    std::printf("[INFO] Success to receive server mem addr: 0x%" PRIx64 "\n", remote_addr);
  }
  std::printf("[INFO] Server RegisterMem success\n");

  const auto connect_ret = lane_hixl_.Connect(AscendString(remote.c_str()));
  if (connect_ret != SUCCESS) {
    std::printf("[ERROR] Connect failed, ret = %u, errmsg: %s\n", connect_ret, RecentErrMsg());
    return -1;
  }
  std::printf("[INFO] Connect success\n");
  lane_hixl_connected_ = true;

  if (RunTransfer(lane_hixl_, src_slice, remote.c_str(), remote_addr, cfg_) != 0) {
    return -1;
  }

  lane_need_tcp_notify_ = true;
  std::printf("[INFO] Client Sample end\n");
  return 0;
}

int ClientRunner::Run() {
  if (cfg_.expanded_device_ids.empty()) {
    return -1;
  }
  const bool multi_local = cfg_.local_engine_list.size() > 1U;
  if (multi_local) {
    return RunMultiLane();
  }
  return RunSingleDevice();
}

int ClientRunner::RunSingleDevice() {
  const size_t n = cfg_.expanded_remote_engines.size();
  if (n > 1U) {
    return RunSharedMultiRemote();
  }

  const std::string &local = cfg_.expanded_local_engines[0];
  const std::string &remote = cfg_.expanded_remote_engines[0];

  if (InitializeHixl(local, cfg_, &lane_hixl_) != 0) {
    return -1;
  }
  lane_hixl_initialized_ = true;
  lane_resources_active_ = true;

  const size_t alloc_size = static_cast<size_t>(cfg_.total_size);
  if (AllocLocalBuffer(cfg_, &lane_is_host_, &lane_buffer_, alloc_size) != 0) {
    return -1;
  }

  return RunOnePair(remote, lane_buffer_, alloc_size);
}

int ClientRunner::RunSharedMultiRemote() {
  const size_t n = cfg_.expanded_remote_engines.size();
  const std::string &local = cfg_.expanded_local_engines[0];
  lane_shared_multi_remote_workers_disconnected_ = false;

  if (InitializeHixl(local, cfg_, &lane_hixl_) != 0) {
    return -1;
  }
  lane_hixl_initialized_ = true;
  lane_resources_active_ = true;

  const size_t alloc_size = static_cast<size_t>(cfg_.total_size) * n;
  if (AllocLocalBuffer(cfg_, &lane_is_host_, &lane_buffer_, alloc_size) != 0) {
    return -1;
  }

  lane_need_register_ = !(lane_is_host_ && cfg_.use_buffer_pool);
  if (RegisterLocalMem(lane_hixl_, cfg_, lane_buffer_, lane_is_host_, lane_need_register_, alloc_size,
                       &lane_mem_handle_) != 0) {
    return -1;
  }

  return RunClientSharedRemoteWorkers();
}

int ClientRunner::RunClientSharedRemoteWorkers() {
  const size_t n = cfg_.expanded_remote_engines.size();
  const size_t slice = static_cast<size_t>(cfg_.total_size);

  std::vector<std::vector<detail::TransferBenchRecord>> per_remote(n);
  std::atomic<int> first_fail{0};
  std::mutex fail_mu;
  std::vector<std::thread> threads;
  threads.reserve(n);
  for (size_t i = 0; i < n; ++i) {
    void *sl =
        reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(lane_buffer_) + static_cast<uintptr_t>(i * slice));
    threads.emplace_back(SharedRemoteWorker, i, &lane_hixl_, std::cref(cfg_), sl, &first_fail, &fail_mu,
                         &per_remote[i]);
  }
  for (auto &t : threads) {
    t.join();
  }
  if (first_fail.load() == 0) {
    lane_shared_multi_remote_workers_disconnected_ = true;
  }
  for (size_t i = 0; i < n; ++i) {
    PrintBenchRecords(per_remote[i]);
  }
  return first_fail.load() != 0 ? -1 : 0;
}

int ClientRunner::RunClientLaneWorkers() {
  const size_t n = cfg_.expanded_local_engines.size();
  lane_runtimes_.clear();
  multi_lane_threads_.clear();
  lane_runtimes_.reserve(n);
  for (size_t i = 0; i < n; ++i) {
    lane_runtimes_.push_back(std::make_unique<detail::LaneState>());
  }

  std::atomic<int> first_fail{0};
  std::mutex fail_mu;
  multi_lane_threads_.reserve(n);
  for (size_t i = 0; i < n; ++i) {
    multi_lane_threads_.emplace_back(detail::LaneWorkerEntry, i, lane_runtimes_[i].get(), std::cref(cfg_), &first_fail,
                                     &fail_mu);
  }
  for (auto &t : multi_lane_threads_) {
    t.join();
  }
  multi_lane_threads_.clear();

  for (size_t i = 0; i < n; ++i) {
    if (lane_runtimes_[i]) {
      PrintBenchRecords(lane_runtimes_[i]->bench_records);
    }
  }

  for (size_t i = 0; i < n; ++i) {
    if (lane_runtimes_[i]) {
      detail::FinalizeLaneState(lane_runtimes_[i].get(), cfg_.expanded_remote_engines[i]);
      (void)aclrtResetDevice(cfg_.expanded_device_ids[i]);
    }
  }
  lane_runtimes_.clear();

  return first_fail.load() != 0 ? -1 : 0;
}

int ClientRunner::RunMultiLane() { return RunClientLaneWorkers(); }

}  // namespace hixl_benchmark
