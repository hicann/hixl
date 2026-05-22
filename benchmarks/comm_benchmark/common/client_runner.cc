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
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "acl/acl.h"
#include <unistd.h>

#include "fabric_mem/fabric_mem_transfer_service.h"

using hixl::AscendString;
using hixl::FabricMemTransferService;
using hixl::Hixl;
using hixl::MemDesc;
using hixl::MemHandle;
using hixl::MemType;
using hixl::SUCCESS;
using hixl::TransferArgs;
using hixl::TransferOp;
using hixl::TransferOpDesc;
using hixl::TransferReq;
using hixl::TransferStatus;

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

// Bandwidth uses decimal GB/s (10^9 bytes per GB); microseconds → seconds use 10^6.
constexpr double kDecimalBytesPerGb = 1000.0 * 1000.0 * 1000.0;
constexpr double kMicrosecondsPerSecond = 1000.0 * 1000.0;
constexpr int32_t kMsPerSecond = 1000;
constexpr int32_t kWaitTransTimeSec = 60;
constexpr int32_t kTransferSyncTimeoutMs = kMsPerSecond * kWaitTransTimeSec;
// Binary IEC units for human-readable block sizes in logs only.
constexpr uint64_t kDisplayBytesPerGiB = 1024ULL * 1024ULL * 1024ULL;
constexpr uint64_t kDisplayBytesPerMiB = 1024ULL * 1024ULL;
constexpr uint64_t kDisplayBytesPerKiB = 1024ULL;

std::string FormatBlockSizeHuman(uint64_t bytes) {
  if (bytes >= kDisplayBytesPerGiB && bytes % kDisplayBytesPerGiB == 0) {
    return std::to_string(bytes / kDisplayBytesPerGiB) + " GiB";
  }
  if (bytes >= kDisplayBytesPerMiB && bytes % kDisplayBytesPerMiB == 0) {
    return std::to_string(bytes / kDisplayBytesPerMiB) + " MiB";
  }
  if (bytes >= kDisplayBytesPerKiB && bytes % kDisplayBytesPerKiB == 0) {
    return std::to_string(bytes / kDisplayBytesPerKiB) + " KiB";
  }
  return std::to_string(bytes) + " B";
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

void DeregisterMemHandles(Hixl &hixl_engine, const std::vector<MemHandle> &handles) {
  for (const auto &element : handles) {
    if (element == nullptr) {
      continue;
    }
    const auto ret = hixl_engine.DeregisterMem(element);
    if (ret != 0) {
      std::printf("[ERROR] DeregisterMem failed, ret = %u, errmsg: %s\n", ret, RecentErrMsg());
    } else {
      std::printf("[INFO] DeregisterMem success\n");
    }
  }
}

void FreeHostBuffers(const std::vector<void *> &buffers, const std::string &transport) {
  for (const auto &element : buffers) {
    if (element == nullptr) {
      continue;
    }
    if (transport == "fabric_mem") {
      (void)FabricMemTransferService::FreeMem(element);
    } else {
      (void)aclrtFreeHost(element);
    }
  }
}

void FreeDeviceBuffers(const std::vector<void *> &buffers) {
  for (const auto &element : buffers) {
    if (element == nullptr) {
      continue;
    }
    (void)aclrtFree(element);
  }
}

void ReleaseHixlResources(Hixl &hixl_engine, bool need_register, bool is_host, const std::vector<MemHandle> &handles,
                          const std::vector<void *> &buffers, const std::string &transport) {
  if (need_register) {
    DeregisterMemHandles(hixl_engine, handles);
  }
  if (is_host) {
    FreeHostBuffers(buffers, transport);
  } else {
    FreeDeviceBuffers(buffers);
  }
  hixl_engine.Finalize();
}

bool ShouldRegisterInitiatorMem(const BenchmarkConfig &cfg, bool is_host) {
  // fabric_mem device buffer is local-only; use TransferSync addresses without RegisterMem.
  return !(cfg.transport == "fabric_mem" && !is_host);
}

int32_t AllocLocalBuffer(const BenchmarkConfig &cfg, bool *is_host, void **out_src, size_t alloc_size) {
  *is_host = (cfg.initiator_memory_type == "host");
  void *tmp = nullptr;
  if (*is_host && cfg.transport == "fabric_mem") {
    auto status = FabricMemTransferService::MallocMem(MemType::MEM_HOST, alloc_size, &tmp);
    if (status != SUCCESS) {
      std::fprintf(stderr, "[ERROR] client fabric_mem host alloc failed status=%d\n", static_cast<int>(status));
      return -1;
    }
  } else if (*is_host) {
    aclError er = aclrtMallocHost(&tmp, alloc_size);
    if (er != ACL_ERROR_NONE) {
      std::fprintf(stderr, "[ERROR] client alloc host failed aclError=%d\n", static_cast<int>(er));
      return -1;
    }
  } else {
    aclError er = aclrtMalloc(&tmp, alloc_size, ACL_MEM_MALLOC_HUGE_ONLY);
    if (er != ACL_ERROR_NONE) {
      std::fprintf(stderr, "[ERROR] client alloc device failed aclError=%d\n", static_cast<int>(er));
      return -1;
    }
  }
  *out_src = tmp;
  return 0;
}

int32_t RegisterLocalMem(Hixl &hixl_engine, const BenchmarkConfig &cfg, void *src, bool is_host, bool need_register,
                         size_t register_len, MemHandle *handle) {
  if (!need_register || !ShouldRegisterInitiatorMem(cfg, is_host)) {
    return 0;
  }
  MemDesc desc{};
  desc.addr = reinterpret_cast<uintptr_t>(src);
  desc.len = register_len;
  std::printf("[DEBUG] RegisterMem: addr=0x%" PRIxPTR ", len=%zu, range=[0x%" PRIxPTR ", 0x%" PRIxPTR "), is_host=%d\n",
              desc.addr, register_len, desc.addr, desc.addr + register_len, is_host);
  const auto ret = hixl_engine.RegisterMem(desc, is_host ? MemType::MEM_HOST : MemType::MEM_DEVICE, *handle);
  if (ret != SUCCESS) {
    std::printf("[ERROR] RegisterMem failed, ret = %u, errmsg: %s\n", ret, RecentErrMsg());
    return -1;
  }
  std::printf("[INFO] RegisterMem success len=%zu\n", register_len);
  return 0;
}

bool GetRemoteAddr(TCPClient *tcp_client, const std::string &remote_engine, uint16_t tcp_port,
                    uint64_t *out_remote_addr, uint32_t connect_timeout_ms) {
  const std::string host = hixl_benchmark::ExtractTcpHost(remote_engine);
  if (host.empty()) {
    return false;
  }
  if (!tcp_client->ConnectToServer(host, tcp_port, connect_timeout_ms)) {
    return false;
  }
  if (!tcp_client->ReceiveUint64(out_remote_addr)) {
    return false;
  }
  std::printf("[DEBUG] GetRemoteAddr: received remote_addr=0x%" PRIx64 "\n", *out_remote_addr);
  if (!tcp_client->ReceiveTaskStatus()) {
    return false;
  }
  return true;
}

bool SendNotify(TCPClient *tcp_client) {
  return tcp_client->SendTaskStatus();
}

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

using hixl_benchmark::detail::BenchWorkerTag;
using hixl_benchmark::detail::TransferBenchRecord;

void PrintBenchRecords(const std::vector<TransferBenchRecord> &recs) {
  for (const auto &r : recs) {
    const char *worker_prefix = "";
    std::size_t worker_idx = 0;
    if (r.tag == BenchWorkerTag::kLane) {
      worker_prefix = "[lane ";
      worker_idx = r.worker_index;
    } else if (r.tag == BenchWorkerTag::kRemote) {
      worker_prefix = "[remote ";
      worker_idx = r.worker_index;
    }
    const std::string bs_log = FormatBlockSizeHuman(static_cast<uint64_t>(r.block_size));
    if (r.async_batch_num > 0) {
      if (r.tag != BenchWorkerTag::kSingle) {
        std::printf(
            "[INFO] %s%zu] Async transfer success, loop %u/%u, step %u, block size: %s, trans_num: %u, "
            "async_batch_num: %u, total time: %ld us (submit: %ld us, wait: %ld us), throughput: %.3lf GB/s\n",
            worker_prefix, worker_idx, r.loop_plus_one, r.loops_total, r.step_index, bs_log.c_str(), r.trans_num,
            r.async_batch_num, static_cast<long>(r.time_us), static_cast<long>(r.submit_time_us),
            static_cast<long>(r.wait_time_us), r.throughput_gbps);
      } else {
        std::printf(
            "[INFO] Async transfer success, loop %u/%u, step %u, block size: %s, trans_num: %u, "
            "async_batch_num: %u, total time: %ld us (submit: %ld us, wait: %ld us), throughput: %.3lf GB/s\n",
            r.loop_plus_one, r.loops_total, r.step_index, bs_log.c_str(), r.trans_num, r.async_batch_num,
            static_cast<long>(r.time_us), static_cast<long>(r.submit_time_us), static_cast<long>(r.wait_time_us),
            r.throughput_gbps);
      }
    } else {
      if (r.tag != BenchWorkerTag::kSingle) {
        std::printf(
            "[INFO] %s%zu] Transfer success, loop %u/%u, step %u, block size: %s, transfer num: %u, time cost: "
            "%ld us, throughput: %.3lf GB/s\n",
            worker_prefix, worker_idx, r.loop_plus_one, r.loops_total, r.step_index, bs_log.c_str(), r.trans_num,
            static_cast<long>(r.time_us), r.throughput_gbps);
      } else {
        std::printf(
            "[INFO] Transfer success, loop %u/%u, step %u, block size: %s, transfer num: %u, time cost: %ld us, "
            "throughput: %.3lf GB/s\n",
            r.loop_plus_one, r.loops_total, r.step_index, bs_log.c_str(), r.trans_num, static_cast<long>(r.time_us),
            r.throughput_gbps);
      }
    }
  }
}

std::string CommResultBasePath(const BenchmarkConfig &cfg) {
  return cfg.output_dir + "/comm_result_" + std::to_string(static_cast<long long>(getpid()));
}

void AppendCommResult(const BenchmarkConfig &cfg, const TransferBenchRecord &record) {
  static std::mutex result_mu;
  std::lock_guard<std::mutex> lock(result_mu);
  std::error_code ec;
  std::filesystem::create_directories(cfg.output_dir, ec);
  const std::string csv_path = CommResultBasePath(cfg) + ".csv";
  const bool need_header = !std::ifstream(csv_path).good();
  std::ofstream csv(csv_path, std::ios::app);
  if (!csv.good()) {
    return;
  }
  if (need_header) {
    csv << "benchmark,pattern,model,token_length,block_size,batch_size,threads,transport,direction,initiator_memory,"
           "target_memory,bandwidth_gbps,ops_per_sec,avg_latency_us,p99_us,error_count,consistency\n";
  }
  const double avg_us = record.trans_num == 0U ? 0.0 : static_cast<double>(record.time_us) / record.trans_num;
  const double ops =
      record.time_us == 0 ? 0.0 : static_cast<double>(record.trans_num) * kMicrosecondsPerSecond / record.time_us;
  csv << "hixl_comm_bench," << cfg.benchmark_group << ",,," << record.block_size << ','
      << (record.async_batch_num == 0U ? cfg.start_batch_size : record.async_batch_num) << ','
      << cfg.start_threads << ',' << cfg.transport << ','
      << BenchmarkConfig::ComputeDirection(cfg.initiator_memory_type, cfg.target_memory_type, cfg.transfer_op) << ','
      << cfg.initiator_memory_type << ',' << cfg.target_memory_type << ','
      << record.throughput_gbps << ','
      << ops << ',' << avg_us << ',' << avg_us << ",0," << (cfg.check_consistency ? "true" : "not_checked")
      << '\n';

  std::ofstream json(CommResultBasePath(cfg) + ".jsonl", std::ios::app);
  if (json.good()) {
    json << "{\"benchmark\":\"hixl_comm_bench\",\"pattern\":\"" << cfg.benchmark_group
         << "\",\"block_size\":" << record.block_size << ",\"batch_size\":"
         << (record.async_batch_num == 0U ? cfg.start_batch_size : record.async_batch_num)
         << ",\"threads\":" << cfg.start_threads << ",\"transport\":\"" << cfg.transport
         << "\",\"direction\":\""
         << BenchmarkConfig::ComputeDirection(cfg.initiator_memory_type, cfg.target_memory_type, cfg.transfer_op)
         << "\",\"initiator_memory\":\"" << cfg.initiator_memory_type
         << "\",\"target_memory\":\"" << cfg.target_memory_type
         << "\",\"bandwidth_gbps\":" << record.throughput_gbps
         << ",\"p99_us\":" << avg_us << "}\n";
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
  int32_t connect_timeout_ms = 60000;
};

void ApplyBenchWorkerIdentity(const TransferBlockStepCtx &ctx, TransferBenchRecord *rec) {
  if (ctx.bench_records != nullptr) {
    rec->tag = ctx.bench_worker_tag;
    rec->worker_index = ctx.bench_worker_index;
  }
}

void FillCommonStepFields(const TransferBlockStepCtx &ctx, uint32_t block_size, uint32_t trans_num, int64_t time_us,
                          double throughput, TransferBenchRecord *rec) {
  ApplyBenchWorkerIdentity(ctx, rec);
  rec->loop_plus_one = ctx.loop + 1U;
  rec->loops_total = ctx.cfg->loops;
  rec->step_index = ctx.step_index;
  rec->block_size = block_size;
  rec->trans_num = trans_num;
  rec->time_us = time_us;
  rec->throughput_gbps = throughput;
}

TransferBenchRecord MakeSyncTransferRecord(const TransferBlockStepCtx &ctx, uint32_t block_size, uint32_t trans_num,
                                           int64_t time_us, double throughput) {
  TransferBenchRecord rec{};
  FillCommonStepFields(ctx, block_size, trans_num, time_us, throughput, &rec);
  return rec;
}

TransferBenchRecord MakeAsyncTransferRecord(const TransferBlockStepCtx &ctx, uint32_t block_size, uint32_t trans_num,
                                            int64_t total_us, int64_t submit_us, int64_t wait_us, double throughput) {
  TransferBenchRecord rec{};
  FillCommonStepFields(ctx, block_size, trans_num, total_us, throughput, &rec);
  rec.async_batch_num = ctx.cfg->async_batch_num;
  rec.submit_time_us = submit_us;
  rec.wait_time_us = wait_us;
  return rec;
}

void PublishBenchRecord(const TransferBlockStepCtx &ctx, const TransferBenchRecord &rec) {
  AppendCommResult(*ctx.cfg, rec);
  if (ctx.bench_records != nullptr) {
    ctx.bench_records->push_back(rec);
  }
}

void LogSyncTransferSuccess(const TransferBlockStepCtx &ctx, uint32_t block_size, uint32_t trans_num, int64_t time_us,
                            double throughput) {
  const std::string bs_log = FormatBlockSizeHuman(static_cast<uint64_t>(block_size));
  std::printf(
      "[INFO] Transfer success, loop %u/%u, step %u, block size: %s, transfer num: %u, time cost: %ld us, "
      "throughput: %.3lf GB/s\n",
      ctx.loop + 1U, ctx.cfg->loops, ctx.step_index, bs_log.c_str(), trans_num, static_cast<long>(time_us),
      throughput);
}

void LogAsyncTransferSuccess(const TransferBlockStepCtx &ctx, uint32_t block_size, uint32_t trans_num,
                             int64_t total_us, int64_t submit_us, int64_t wait_us, double throughput) {
  const std::string bs_log = FormatBlockSizeHuman(static_cast<uint64_t>(block_size));
  std::printf("[INFO] Async transfer success, loop %u/%u, step %u, block size: %s, trans_num: %u, batch_num: %u, "
              "total: %ld us (submit: %ld, wait: %ld), %.3lf GB/s\n",
              ctx.loop + 1U, ctx.cfg->loops, ctx.step_index, bs_log.c_str(), trans_num, ctx.cfg->async_batch_num,
              static_cast<long>(total_us), static_cast<long>(submit_us), static_cast<long>(wait_us), throughput);
}

std::vector<TransferOpDesc> BuildSyncTransferDescriptors(const TransferBlockStepCtx &ctx, uint32_t block_size,
                                                        uint32_t trans_num) {
  std::vector<TransferOpDesc> descs;
  descs.reserve(trans_num);
  for (uint32_t j = 0; j < trans_num; j++) {
    TransferOpDesc desc{};
    desc.local_addr = ctx.base + static_cast<uintptr_t>(j) * block_size;
    desc.remote_addr = static_cast<uintptr_t>(ctx.dst_addr) + static_cast<uintptr_t>(j) * block_size;
    desc.len = block_size;
    descs.emplace_back(desc);
  }
  return descs;
}

void FinishSyncBenchStep(const TransferBlockStepCtx &ctx, uint32_t block_size, uint32_t trans_num, int64_t time_us,
                         double throughput) {
  TransferBenchRecord rec = MakeSyncTransferRecord(ctx, block_size, trans_num, time_us, throughput);
  PublishBenchRecord(ctx, rec);
  if (ctx.bench_records == nullptr) {
    LogSyncTransferSuccess(ctx, block_size, trans_num, time_us, throughput);
  }
}

int32_t TransferOneBlockStep(Hixl &hixl_engine, const TransferBlockStepCtx &ctx) {
  const auto block_size = static_cast<uint32_t>(ctx.block_size_u);
  if (static_cast<uint64_t>(block_size) != ctx.block_size_u) {
    std::printf("[ERROR] block size too large at step %u\n", ctx.step_index);
    return -1;
  }
  const auto trans_num = static_cast<uint32_t>(ctx.cfg->total_size / ctx.block_size_u);
  std::vector<TransferOpDesc> descs = BuildSyncTransferDescriptors(ctx, block_size, trans_num);
  const auto start = std::chrono::steady_clock::now();
  const auto ret =
      hixl_engine.TransferSync(AscendString(ctx.remote_engine), ctx.transfer_op, descs, kTransferSyncTimeoutMs);
  if (ret != SUCCESS) {
    std::printf("[ERROR] TransferSync failed, ret = %u, errmsg: %s\n", ret, RecentErrMsg());
    return -1;
  }
  const auto time_cost =
      std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
  const double time_second = static_cast<double>(time_cost) / kMicrosecondsPerSecond;
  const double throughput = static_cast<double>(ctx.cfg->total_size) / kDecimalBytesPerGb / time_second;
  FinishSyncBenchStep(ctx, block_size, trans_num, time_cost, throughput);
  return 0;
}

struct AsyncTransferContext {
  std::vector<TransferReq> reqs;
  std::chrono::steady_clock::time_point submit_start;
  std::chrono::steady_clock::time_point submit_end;
  std::chrono::steady_clock::time_point wait_end;
};

int32_t SubmitAsyncRequests(Hixl &hixl_engine, const TransferBlockStepCtx &ctx, uint64_t per_req_size,
                            uint32_t block_size, uint32_t per_req_trans_num, AsyncTransferContext &async_ctx) {
  async_ctx.submit_start = std::chrono::steady_clock::now();
  TransferArgs optional_args{};
  std::printf("[DEBUG] SubmitAsyncRequests: ctx.base=0x%" PRIxPTR ", ctx.dst_addr=0x%" PRIx64 ", per_req_size=%" PRIu64 ", block_size=%u, per_req_trans_num=%u, async_batch_num=%u\n",
              ctx.base, ctx.dst_addr, per_req_size, block_size, per_req_trans_num, ctx.cfg->async_batch_num);
  for (uint32_t batch_idx = 0; batch_idx < ctx.cfg->async_batch_num; ++batch_idx) {
    std::vector<TransferOpDesc> descs;
    descs.reserve(per_req_trans_num);
    const uintptr_t req_base = ctx.base + static_cast<uintptr_t>(batch_idx) * static_cast<uintptr_t>(per_req_size);
    const uintptr_t req_remote_base = ctx.dst_addr + static_cast<uintptr_t>(batch_idx) * static_cast<uintptr_t>(per_req_size);
    std::printf("[DEBUG] batch %u: req_base=0x%" PRIxPTR ", req_remote_base=0x%" PRIxPTR "\n", batch_idx, req_base, req_remote_base);
    for (uint32_t j = 0; j < per_req_trans_num; ++j) {
      const auto offset = static_cast<uintptr_t>(j) * static_cast<uintptr_t>(block_size);
      descs.push_back({req_base + offset, req_remote_base + offset, block_size});
    }
    for (uint32_t j = 0; j < per_req_trans_num; ++j) {
      std::printf("[DEBUG] batch %u desc[%u]: local=0x%" PRIxPTR ", remote=0x%" PRIxPTR ", len=%zu\n",
                  batch_idx, j, descs[j].local_addr, descs[j].remote_addr, descs[j].len);
    }
    TransferReq req = nullptr;
    if (hixl_engine.TransferAsync(AscendString(ctx.remote_engine), ctx.transfer_op, descs, optional_args, req) !=
        SUCCESS) {
      std::printf("[ERROR] TransferAsync failed at batch %u\n", batch_idx);
      return -1;
    }
    async_ctx.reqs.emplace_back(req);
  }
  async_ctx.submit_end = std::chrono::steady_clock::now();
  return 0;
}

int32_t CheckTransferStatus(Hixl &hixl_engine, const TransferReq &req, TransferStatus &status, size_t idx) {
  if (status != TransferStatus::WAITING) {
    return 0;
  }
  if (hixl_engine.GetTransferStatus(req, status) != SUCCESS) {
    std::printf("[ERROR] GetTransferStatus failed at req %zu\n", idx);
    return -1;
  }
  if (status == TransferStatus::WAITING) {
    return 1;
  }
  if (status != TransferStatus::COMPLETED) {
    std::printf("[ERROR] Transfer failed at req %zu, status=%d\n", idx, static_cast<int>(status));
    return -1;
  }
  std::printf("[DEBUG] req %zu transfer completed\n", idx);
  return 0;
}

int32_t WaitAsyncRequests(Hixl &hixl_engine, AsyncTransferContext &async_ctx) {
  std::vector<TransferStatus> statuses(async_ctx.reqs.size(), TransferStatus::WAITING);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(kWaitTransTimeSec);
  bool has_waiting = true;
  while (has_waiting && std::chrono::steady_clock::now() < deadline) {
    has_waiting = false;
    for (size_t i = 0; i < async_ctx.reqs.size(); ++i) {
      const auto ret = CheckTransferStatus(hixl_engine, async_ctx.reqs[i], statuses[i], i);
      if (ret < 0) {
        return -1;
      }
      if (ret == 1) {
        has_waiting = true;
      }
    }
    if (has_waiting) {
      std::this_thread::sleep_for(std::chrono::microseconds(1));
    }
  }
  async_ctx.wait_end = std::chrono::steady_clock::now();
  return has_waiting ? -1 : 0;
}

void RecordAsyncBenchResult(const TransferBlockStepCtx &ctx, uint32_t block_size, const AsyncTransferContext &actx) {
  const auto submit_us =
      std::chrono::duration_cast<std::chrono::microseconds>(actx.submit_end - actx.submit_start).count();
  const auto wait_us = std::chrono::duration_cast<std::chrono::microseconds>(actx.wait_end - actx.submit_end).count();
  const auto total_us = std::chrono::duration_cast<std::chrono::microseconds>(actx.wait_end - actx.submit_start).count();
  const double throughput = static_cast<double>(ctx.cfg->total_size) / kDecimalBytesPerGb /
                            (static_cast<double>(total_us) / kMicrosecondsPerSecond);
  const auto total_trans_num = static_cast<uint32_t>(ctx.cfg->total_size / ctx.block_size_u);
  TransferBenchRecord rec =
      MakeAsyncTransferRecord(ctx, block_size, total_trans_num, total_us, submit_us, wait_us, throughput);
  PublishBenchRecord(ctx, rec);
  if (ctx.bench_records == nullptr) {
    LogAsyncTransferSuccess(ctx, block_size, total_trans_num, total_us, submit_us, wait_us, throughput);
  }
}

int32_t TransferOneBlockStepAsync(Hixl &hixl_engine, const TransferBlockStepCtx &ctx) {
  const auto block_size = static_cast<uint32_t>(ctx.block_size_u);
  if (static_cast<uint64_t>(block_size) != ctx.block_size_u) {
    std::printf("[ERROR] block size too large at step %u\n", ctx.step_index);
    return -1;
  }
  const uint64_t per_req_size = ctx.cfg->total_size / ctx.cfg->async_batch_num;
  const auto per_req_trans_num = static_cast<uint32_t>(per_req_size / ctx.block_size_u);
  
  AsyncTransferContext async_ctx;
  if (SubmitAsyncRequests(hixl_engine, ctx, per_req_size, block_size, per_req_trans_num, async_ctx) != 0) {
    return -1;
  }
  if (WaitAsyncRequests(hixl_engine, async_ctx) != 0) {
    std::printf("[ERROR] Async transfer timeout at step %u\n", ctx.step_index);
    return -1;
  }
  RecordAsyncBenchResult(ctx, block_size, async_ctx);
  return 0;
}

int32_t RunTransfer(Hixl &hixl_engine, void *src_base, const char *remote_engine, uint64_t dst_addr,
                    const BenchmarkConfig &cfg, std::vector<TransferBenchRecord> *bench_records = nullptr,
                    BenchWorkerTag bench_worker_tag = BenchWorkerTag::kSingle, std::size_t bench_worker_index = 0) {
  const uintptr_t base = reinterpret_cast<uintptr_t>(src_base);
  TransferBlockStepCtx step_ctx{};
  step_ctx.base = base;
  step_ctx.remote_engine = remote_engine;
  step_ctx.dst_addr = dst_addr;
  step_ctx.cfg = &cfg;
  step_ctx.bench_records = bench_records;
  step_ctx.bench_worker_tag = bench_worker_tag;
  step_ctx.bench_worker_index = bench_worker_index;
  step_ctx.connect_timeout_ms = static_cast<int32_t>(cfg.connect_timeout_ms);
  for (uint32_t loop = 0; loop < cfg.loops; ++loop) {
    step_ctx.loop = loop;
    for (uint32_t i = 0; i < cfg.block_steps; ++i) {
      step_ctx.step_index = i;
      step_ctx.block_size_u = cfg.block_size << i;
      if (cfg.transfer_op == "mix") {
        step_ctx.transfer_op = ((loop + i) % 2U == 0U) ? TransferOp::READ : TransferOp::WRITE;
      } else {
        step_ctx.transfer_op = (cfg.transfer_op == "read") ? TransferOp::READ : TransferOp::WRITE;
      }
      int32_t step_ret = 0;
      if (cfg.use_async) {
        step_ret = TransferOneBlockStepAsync(hixl_engine, step_ctx);
      } else {
        step_ret = TransferOneBlockStep(hixl_engine, step_ctx);
      }
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
                                           std::vector<TransferBenchRecord> *bench_records, std::mutex *remote_mu) {
  const auto connect_ret = hixl->Connect(AscendString(remote.c_str()), static_cast<int32_t>(cfg.connect_timeout_ms));
  if (connect_ret != SUCCESS) {
    std::printf("[ERROR] [remote %zu] Connect failed ret=%u %s\n", idx, connect_ret, RecentErrMsg());
    (void)SendNotify(tcp_client);
    MarkFirstFail(first_fail, fail_mu);
    return false;
  }
  std::lock_guard<std::mutex> remote_lock(*remote_mu);
  if (RunTransfer(*hixl, slice_base, remote.c_str(), remote_addr, cfg, bench_records, BenchWorkerTag::kRemote, idx) !=
      0) {
    MarkFirstFail(first_fail, fail_mu);
    (void)hixl->Disconnect(AscendString(remote.c_str()));
    (void)SendNotify(tcp_client);
    return false;
  }
  return true;
}

void SharedRemoteWorker(size_t idx, int32_t device_id, Hixl *hixl, const BenchmarkConfig &cfg, void *slice_base,
                        std::atomic<int> *first_fail, std::mutex *fail_mu,
                        std::vector<TransferBenchRecord> *bench_records, std::mutex *remote_mu) {
  const std::string &remote = cfg.expanded_remote_engines[idx];
  std::printf("[INFO] [remote %zu] worker start -> %s\n", idx, remote.c_str());

  aclError ar = aclrtSetDevice(device_id);
  if (ar != ACL_ERROR_NONE) {
    std::printf("[ERROR] [remote %zu] aclrtSetDevice failed %d\n", idx, static_cast<int>(ar));
    MarkFirstFail(first_fail, fail_mu);
    return;
  }

  TCPClient tcp_client;
  uint64_t remote_addr = 0;
  if (!GetRemoteAddr(&tcp_client, remote, cfg.expanded_tcp_ports[idx], &remote_addr, cfg.connect_timeout_ms)) {
    MarkFirstFail(first_fail, fail_mu);
    (void)aclrtResetDevice(device_id);
    return;
  }
  if (remote_addr != 0U) {
    std::printf("[INFO] [remote %zu] server mem addr: 0x%" PRIx64 "\n", idx, remote_addr);
  }

  if (!SharedRemoteConnectTransferAndCleanup(hixl, idx, slice_base, remote, remote_addr, cfg, &tcp_client, first_fail,
                                             fail_mu, bench_records, remote_mu)) {
    (void)aclrtResetDevice(device_id);
    return;
  }

  const auto disconnect_ret = hixl->Disconnect(AscendString(remote.c_str()));
  if (disconnect_ret != SUCCESS) {
    std::printf("[ERROR] [remote %zu] Disconnect failed ret=%u\n", idx, disconnect_ret);
  }
  if (!SendNotify(&tcp_client)) {
    MarkFirstFail(first_fail, fail_mu);
  }
  (void)aclrtResetDevice(device_id);
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
      ReleaseHixlResources(p->hixl, p->need_register, p->is_host, {p->mem_handle}, {p->buffer}, p->transport);
    } else {
      ReleaseHixlResources(p->hixl, p->need_register, p->is_host, {p->mem_handle}, {}, p->transport);
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
  const size_t alloc_size = static_cast<size_t>(cfg.buffer_size);
  if (AllocLocalBuffer(cfg, &p->is_host, &p->buffer, alloc_size) != 0) {
    MarkFirstFail(first_fail, fail_mu);
    return false;
  }
  p->need_register = true;
  if (RegisterLocalMem(p->hixl, cfg, p->buffer, p->is_host, p->need_register, alloc_size, &p->mem_handle) != 0) {
    MarkFirstFail(first_fail, fail_mu);
    return false;
  }
  return true;
}

bool LaneWorkerRemoteTransferPhase(LaneState *p, const BenchmarkConfig &cfg, size_t lane_idx, const std::string &remote,
                                   std::atomic<int> *first_fail, std::mutex *fail_mu, std::mutex *remote_mu) {
  uint64_t remote_addr = 0;
  if (!GetRemoteAddr(&p->tcp_client, remote, cfg.expanded_tcp_ports[lane_idx], &remote_addr, cfg.connect_timeout_ms)) {
    MarkFirstFail(first_fail, fail_mu);
    return false;
  }
  if (remote_addr != 0U) {
    std::printf("[INFO] Success to receive server mem addr: 0x%" PRIx64 "\n", remote_addr);
  }

  const auto connect_ret = p->hixl.Connect(AscendString(remote.c_str()), static_cast<int32_t>(cfg.connect_timeout_ms));
  if (connect_ret != SUCCESS) {
    std::printf("[ERROR] Connect failed, ret = %u, errmsg: %s\n", connect_ret, RecentErrMsg());
    MarkFirstFail(first_fail, fail_mu);
    return false;
  }
  p->hixl_connected = true;

  std::lock_guard<std::mutex> remote_lock(*remote_mu);
  p->bench_records.clear();
  if (RunTransfer(p->hixl, p->buffer, remote.c_str(), remote_addr, cfg, &p->bench_records, BenchWorkerTag::kLane,
                  lane_idx) != 0) {
    MarkFirstFail(first_fail, fail_mu);
    return false;
  }
  return true;
}

void LaneWorkerEntry(size_t idx, LaneState *p, const BenchmarkConfig &cfg, std::atomic<int> *first_fail,
                     std::mutex *fail_mu, std::mutex *remote_mu) {
  const int32_t dev = cfg.expanded_device_ids[idx];
  const std::string &local = cfg.expanded_local_engines[idx];
  const std::string &remote = cfg.expanded_remote_engines[idx];
  p->transport = cfg.transport;
  std::printf("[INFO] [lane %zu] start device=%d\n", idx, static_cast<int>(dev));

  if (!LaneWorkerSetDevice(idx, dev, first_fail, fail_mu)) {
    (void)aclrtResetDevice(dev);
    return;
  }
  if (!LaneWorkerInitHixlEngine(p, cfg, local, first_fail, fail_mu)) {
    FinalizeLaneState(p, remote);
    (void)aclrtResetDevice(dev);
    return;
  }
  if (!LaneWorkerAllocAndRegisterMem(p, cfg, first_fail, fail_mu)) {
    FinalizeLaneState(p, remote);
    (void)aclrtResetDevice(dev);
    return;
  }
  if (!LaneWorkerRemoteTransferPhase(p, cfg, idx, remote, first_fail, fail_mu, remote_mu)) {
    FinalizeLaneState(p, remote);
    (void)aclrtResetDevice(dev);
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

  FinalizeLaneState(p, remote);
  (void)aclrtResetDevice(dev);
  std::printf("[INFO] [lane %zu] end\n", idx);
}

}  // namespace hixl_benchmark::detail

namespace hixl_benchmark {

std::mutex *ClientRunner::GetOrCreateRemoteMutex(const std::string &remote) {
  std::lock_guard<std::mutex> lk(remote_mutex_map_mu_);
  auto it = remote_mutexes_.find(remote);
  if (it != remote_mutexes_.end()) {
    return it->second.get();
  }
  auto mu = std::make_unique<std::mutex>();
  auto *ptr = mu.get();
  remote_mutexes_[remote] = std::move(mu);
  return ptr;
}

ClientRunner::~ClientRunner() {
  Shutdown();
}

void ClientRunner::ReleaseAllLaneRuntimes() {
  for (auto &t : multi_lane_threads_) {
    if (t.joinable()) {
      t.join();
    }
  }
  multi_lane_threads_.clear();
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
                                    cfg_.expanded_remote_engines.size() > 1U && cfg_.local_engine_list.size() == 1U;
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
    ReleaseHixlResources(lane_hixl_, lane_need_register_, lane_is_host_, {lane_mem_handle_}, {lane_buffer_},
                         cfg_.transport);
  } else {
    ReleaseHixlResources(lane_hixl_, lane_need_register_, lane_is_host_, {lane_mem_handle_}, {}, cfg_.transport);
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
    std::printf("[ERROR] ClientRunner aclrtSetDevice(%d) failed\n", static_cast<int>(device_id_));
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

  lane_need_register_ = true;
  if (RegisterLocalMem(lane_hixl_, cfg_, src_slice, lane_is_host_, lane_need_register_, register_len,
                       &lane_mem_handle_) != 0) {
    return -1;
  }

  uint64_t remote_addr = 0;
  if (!GetRemoteAddr(&lane_tcp_, remote, cfg_.expanded_tcp_ports[0], &remote_addr, cfg_.connect_timeout_ms)) {
    return -1;
  }
  lane_tcp_handshake_ok_ = true;
  if (remote_addr != 0U) {
    std::printf("[INFO] Success to receive server mem addr: 0x%" PRIx64 "\n", remote_addr);
  }
  std::printf("[INFO] Server RegisterMem success\n");

  const auto connect_ret = lane_hixl_.Connect(AscendString(remote.c_str()), static_cast<int32_t>(cfg_.connect_timeout_ms));
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

  const size_t alloc_size = static_cast<size_t>(cfg_.buffer_size);
  if (AllocLocalBuffer(cfg_, &lane_is_host_, &lane_buffer_, alloc_size) != 0) {
    return -1;
  }
  std::printf("[DEBUG] RunSingleDevice: allocated lane_buffer_=0x%" PRIxPTR ", alloc_size=%zu\n",
              reinterpret_cast<uintptr_t>(lane_buffer_), alloc_size);

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

  const size_t alloc_size = static_cast<size_t>(cfg_.buffer_size) * n;
  if (AllocLocalBuffer(cfg_, &lane_is_host_, &lane_buffer_, alloc_size) != 0) {
    return -1;
  }
  std::printf("[DEBUG] RunSharedMultiRemote: allocated lane_buffer_=0x%" PRIxPTR ", alloc_size=%zu, n=%zu\n",
              reinterpret_cast<uintptr_t>(lane_buffer_), alloc_size, n);

  lane_need_register_ = true;
  if (RegisterLocalMem(lane_hixl_, cfg_, lane_buffer_, lane_is_host_, lane_need_register_, alloc_size,
                       &lane_mem_handle_) != 0) {
    return -1;
  }

  return RunClientSharedRemoteWorkers();
}

int ClientRunner::RunClientSharedRemoteWorkers() {
  const size_t n = cfg_.expanded_remote_engines.size();
  const size_t slice = static_cast<size_t>(cfg_.buffer_size);

  std::vector<std::vector<detail::TransferBenchRecord>> per_remote(n);
  std::atomic<int> first_fail{0};
  std::mutex fail_mu;
  std::vector<std::thread> threads;
  threads.reserve(n);
  for (size_t i = 0; i < n; ++i) {
    void *sl = reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(lane_buffer_) + static_cast<uintptr_t>(i * slice));
    std::printf("[DEBUG] RunClientSharedRemoteWorkers: remote[%zu] slice_addr=0x%" PRIxPTR ", slice_size=%zu\n",
                i, reinterpret_cast<uintptr_t>(sl), slice);
    const std::string &remote = cfg_.expanded_remote_engines[i];
    std::mutex *remote_mu = GetOrCreateRemoteMutex(remote);
    threads.emplace_back(SharedRemoteWorker, i, device_id_, &lane_hixl_, std::cref(cfg_), sl, &first_fail, &fail_mu,
                         &per_remote[i], remote_mu);
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
    const std::string &remote = cfg_.expanded_remote_engines[i];
    std::mutex *remote_mu = GetOrCreateRemoteMutex(remote);
    multi_lane_threads_.emplace_back(detail::LaneWorkerEntry, i, lane_runtimes_[i].get(), std::cref(cfg_), &first_fail,
                                     &fail_mu, remote_mu);
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
  lane_runtimes_.clear();

  return first_fail.load() != 0 ? -1 : 0;
}

int ClientRunner::RunMultiLane() {
  return RunClientLaneWorkers();
}

}  // namespace hixl_benchmark
