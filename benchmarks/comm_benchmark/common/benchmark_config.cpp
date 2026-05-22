/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "benchmark_config.h"

#include <algorithm>
#include <cctype>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "acl/acl.h"

using hixl::AscendString;
using hixl::OPTION_BUFFER_POOL;
using hixl::OPTION_ENABLE_USE_FABRIC_MEM;

namespace hixl_benchmark {

std::vector<std::string> SplitCommaList(const std::string &value) {
  std::vector<std::string> parts;
  std::string cur;
  for (char ch : value) {
    if (ch == ',') {
      while (!cur.empty() && static_cast<unsigned char>(cur.front()) == ' ') {
        cur.erase(0, 1);
      }
      while (!cur.empty() && static_cast<unsigned char>(cur.back()) == ' ') {
        cur.pop_back();
      }
      if (!cur.empty()) {
        parts.push_back(cur);
      }
      cur.clear();
    } else {
      cur.push_back(ch);
    }
  }
  while (!cur.empty() && static_cast<unsigned char>(cur.front()) == ' ') {
    cur.erase(0, 1);
  }
  while (!cur.empty() && static_cast<unsigned char>(cur.back()) == ' ') {
    cur.pop_back();
  }
  if (!cur.empty()) {
    parts.push_back(cur);
  }
  return parts;
}

namespace {

constexpr int32_t kPortMaxValue = 65535;

void EnsureEndpointLists(BenchmarkConfig *cfg) {
  if (cfg->device_id_list.empty()) {
    cfg->device_id_list.push_back(cfg->device_id);
  }
  if (cfg->local_engine_list.empty()) {
    cfg->local_engine_list.push_back(cfg->local_engine);
  }
  if (cfg->remote_engine_list.empty()) {
    cfg->remote_engine_list.push_back(cfg->remote_engine);
  }
}

bool ParseBool(const std::string &value, bool *out) {
  if (value == "true" || value == "1") {
    *out = true;
    return true;
  }
  if (value == "false" || value == "0") {
    *out = false;
    return true;
  }
  return false;
}

bool ParseU64(const std::string &value, uint64_t *out) {
  if (value.empty()) {
    return false;
  }
  try {
    std::size_t pos = 0;
    const unsigned long long v = std::stoull(value, &pos, 10);
    if (pos != value.size()) {
      return false;
    }
    *out = static_cast<uint64_t>(v);
    return true;
  } catch (const std::invalid_argument &) {
    return false;
  } catch (const std::out_of_range &) {
    return false;
  }
}

bool ParseU32(const std::string &value, uint32_t *out) {
  uint64_t v = 0;
  if (!ParseU64(value, &v) || v > static_cast<uint64_t>(UINT32_MAX)) {
    return false;
  }
  *out = static_cast<uint32_t>(v);
  return true;
}

bool ParseI32(const std::string &value, int32_t *out) {
  if (value.empty()) {
    return false;
  }
  try {
    std::size_t pos = 0;
    const long long v = std::stoll(value, &pos, 10);
    if (pos != value.size()) {
      return false;
    }
    if (v < static_cast<long long>(INT32_MIN) || v > static_cast<long long>(INT32_MAX)) {
      return false;
    }
    *out = static_cast<int32_t>(v);
    return true;
  } catch (const std::invalid_argument &) {
    return false;
  } catch (const std::out_of_range &) {
    return false;
  }
}

bool CollectArgs(int argc, char **argv, std::map<std::string, std::string> *kv,
                 std::vector<std::string> *hixl_option_payloads) {
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-h" || arg == "--help") {
      BenchmarkConfigParser::PrintUsage(stdout);
      return false;
    }
    auto pos = arg.find('=');
    if (pos == std::string::npos) {
      fprintf(stderr, "[ERROR] Expected key=value, got: %s\n", arg.c_str());
      return false;
    }
    std::string key = arg.substr(0, pos);
    std::string val = arg.substr(pos + 1);
    if (key == "--hixl_option" || key == "-H") {
      hixl_option_payloads->push_back(val);
    } else {
      (*kv)[key] = val;
    }
  }
  return true;
}

void ApplyRoleDefaults(BenchmarkConfig *cfg) {
  if (cfg->role == BenchmarkRole::kClient) {
    cfg->role_name = "initiator";
    cfg->device_id = 0;
    cfg->local_engine = "127.0.0.1:" + std::to_string(kDefaultClientEnginePort);
    cfg->remote_engine = "127.0.0.1:" + std::to_string(kDefaultServerEnginePort);
  } else if (cfg->role == BenchmarkRole::kServer) {
    cfg->role_name = "target";
    cfg->device_id = 1;
    cfg->local_engine = "127.0.0.1:" + std::to_string(kDefaultServerEnginePort);
    cfg->remote_engine = "127.0.0.1";
  }
}

void ApplyCommonDefaults(BenchmarkConfig *cfg) {
  cfg->tcp_port = kDefaultTcpPort;
  cfg->tcp_accept_wait_sec = kDefaultAcceptWaitSec;
  cfg->tcp_client_count = 1U;
  cfg->transfer_op = "read";
  cfg->transport = "hccs";
  cfg->initiator_memory_type = "device";
  cfg->target_memory_type = "device";
  cfg->check_consistency = false;
  cfg->total_size = kDefaultTotalSize;
  cfg->buffer_size = kDefaultBufferSize;
  cfg->block_size = kDefaultTotalSize;
  cfg->start_block_size = kDefaultTotalSize;
  cfg->max_block_size = kDefaultTotalSize;
  cfg->start_batch_size = 1U;
  cfg->max_batch_size = 1U;
  cfg->start_threads = 1U;
  cfg->max_threads = 1U;
  cfg->seed = 0U;
  cfg->block_steps = kDefaultBlockSteps;
  cfg->loops = kDefaultLoops;
  cfg->use_async = false;
  cfg->async_batch_num = 1U;
  cfg->connect_timeout_ms = kDefaultConnectTimeoutMs;
  cfg->warmup_duration_sec = kDefaultWarmupDurationSec;
}

using KvApplyFn = bool (*)(const std::string &val, BenchmarkConfig *cfg);

bool ApplyRoleKv(const std::string &val, BenchmarkConfig *cfg) {
  if (val == "client" || val == "initiator") {
    cfg->role = BenchmarkRole::kClient;
    cfg->role_name = val;
    return true;
  }
  if (val == "server" || val == "target") {
    cfg->role = BenchmarkRole::kServer;
    cfg->role_name = val;
    return true;
  }
  fprintf(stderr, "[ERROR] Invalid --role=%s (expect target|initiator|client|server)\n", val.c_str());
  return false;
}

bool ApplyBenchmarkGroupKv(const std::string &val, BenchmarkConfig *cfg) {
  cfg->benchmark_group = val;
  return true;
}

bool ApplySocVariantKv(const std::string &val, BenchmarkConfig *cfg) {
  std::string v = val;
  std::transform(v.begin(), v.end(), v.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  });
  if (v != "auto" && v != "a2" && v != "a3" && v != "a5") {
    fprintf(stderr, "[ERROR] Invalid --soc_variant=%s (expect auto|a2|a3|a5)\n", val.c_str());
    return false;
  }
  cfg->soc_variant = v;
  return true;
}

bool ApplyOutputDirKv(const std::string &val, BenchmarkConfig *cfg) {
  cfg->output_dir = val;
  return true;
}

bool ApplyTargetIdKv(const std::string &val, BenchmarkConfig *cfg) {
  cfg->target_id = val;
  if (!val.empty()) {
    cfg->remote_engine = val;
    cfg->remote_engine_list = SplitCommaList(val);
  }
  return true;
}

bool ApplyDeviceIdKv(const std::string &val, BenchmarkConfig *cfg) {
  cfg->device_id_list.clear();
  for (const std::string &part : SplitCommaList(val)) {
    int32_t id = 0;
    if (!ParseI32(part, &id)) {
      fprintf(stderr, "[ERROR] Invalid --device_id segment \"%s\" in %s\n", part.c_str(), val.c_str());
      return false;
    }
    cfg->device_id_list.push_back(id);
  }
  if (cfg->device_id_list.empty()) {
    fprintf(stderr, "[ERROR] Invalid --device_id=%s\n", val.c_str());
    return false;
  }
  cfg->device_id = cfg->device_id_list[0];
  return true;
}

bool ApplyLocalEngineKv(const std::string &val, BenchmarkConfig *cfg) {
  cfg->local_engine = val;
  cfg->local_engine_list = SplitCommaList(val);
  if (cfg->local_engine_list.empty()) {
    fprintf(stderr, "[ERROR] local_engine is empty\n");
    return false;
  }
  return true;
}

bool ApplyRemoteEngineKv(const std::string &val, BenchmarkConfig *cfg) {
  cfg->remote_engine = val;
  cfg->remote_engine_list = SplitCommaList(val);
  if (cfg->remote_engine_list.empty()) {
    fprintf(stderr, "[ERROR] remote_engine is empty\n");
    return false;
  }
  return true;
}

bool ApplyTcpPortKv(const std::string &val, BenchmarkConfig *cfg) {
  cfg->tcp_port_list.clear();
  for (const std::string &part : SplitCommaList(val)) {
    int32_t port = 0;
    if (!ParseI32(part, &port) || port < 0 || port > kPortMaxValue) {
      fprintf(stderr, "[ERROR] Invalid --tcp_port segment \"%s\" in %s\n", part.c_str(), val.c_str());
      return false;
    }
    cfg->tcp_port_list.push_back(static_cast<uint16_t>(port));
  }
  if (cfg->tcp_port_list.empty()) {
    fprintf(stderr, "[ERROR] Invalid --tcp_port=%s\n", val.c_str());
    return false;
  }
  cfg->tcp_port = cfg->tcp_port_list[0];
  return true;
}

bool ApplyTcpAcceptWaitSecKv(const std::string &val, BenchmarkConfig *cfg) {
  uint32_t sec = 0;
  if (!ParseU32(val, &sec) || sec == 0U) {
    fprintf(stderr, "[ERROR] Invalid --tcp_accept_wait_s=%s (expect positive integer)\n", val.c_str());
    return false;
  }
  if (sec > kMaxAcceptWaitSec) {
    fprintf(stderr, "[ERROR] --tcp_accept_wait_s too large (max %u)\n", kMaxAcceptWaitSec);
    return false;
  }
  cfg->tcp_accept_wait_sec = sec;
  return true;
}

bool ApplyTcpClientCountKv(const std::string &val, BenchmarkConfig *cfg) {
  uint32_t n = 0;
  if (!ParseU32(val, &n) || n == 0U) {
    fprintf(stderr, "[ERROR] Invalid --tcp_client_count=%s (expect integer 1..%" PRIu32 ")\n", val.c_str(),
            kTcpClientCountMax);
    return false;
  }
  if (n > kTcpClientCountMax) {
    fprintf(stderr, "[ERROR] --tcp_client_count too large (max %" PRIu32 ")\n", kTcpClientCountMax);
    return false;
  }
  cfg->tcp_client_count = n;
  return true;
}

bool ApplyInitiatorMemoryKv(const std::string &val, BenchmarkConfig *cfg) {
  cfg->initiator_memory_type = val;
  return true;
}

bool ApplyTargetMemoryKv(const std::string &val, BenchmarkConfig *cfg) {
  cfg->target_memory_type = val;
  return true;
}

bool ApplyTransferOpKv(const std::string &val, BenchmarkConfig *cfg) {
  cfg->transfer_op = val;
  return true;
}

bool ApplyTransportKv(const std::string &val, BenchmarkConfig *cfg) {
  cfg->transport = val;
  return true;
}

bool ApplyOpTypeKv(const std::string &val, BenchmarkConfig *cfg) {
  cfg->transfer_op = val;
  return true;
}

bool ApplyTotalSizeKv(const std::string &val, BenchmarkConfig *cfg) {
  if (!ParseU64(val, &cfg->total_size) || cfg->total_size == 0) {
    fprintf(stderr, "[ERROR] Invalid --total_size=%s\n", val.c_str());
    return false;
  }
  return true;
}

bool ApplyBufferSizeKv(const std::string &val, BenchmarkConfig *cfg) {
  if (!ParseU64(val, &cfg->buffer_size) || cfg->buffer_size == 0) {
    fprintf(stderr, "[ERROR] Invalid --buffer_size=%s\n", val.c_str());
    return false;
  }
  return true;
}

bool ApplyBlockSizeKv(const std::string &val, BenchmarkConfig *cfg) {
  if (!ParseU64(val, &cfg->block_size) || cfg->block_size == 0) {
    fprintf(stderr, "[ERROR] Invalid --block_size=%s\n", val.c_str());
    return false;
  }
  cfg->block_size_explicit = true;
  return true;
}

bool ApplyBlockStepsKv(const std::string &val, BenchmarkConfig *cfg) {
  if (!ParseU32(val, &cfg->block_steps) || cfg->block_steps == 0) {
    fprintf(stderr, "[ERROR] Invalid --block_steps=%s\n", val.c_str());
    return false;
  }
  return true;
}

bool ApplyLoopsKv(const std::string &val, BenchmarkConfig *cfg) {
  if (!ParseU32(val, &cfg->loops) || cfg->loops == 0) {
    fprintf(stderr, "[ERROR] Invalid --loops=%s\n", val.c_str());
    return false;
  }
  return true;
}

bool ApplyUseAsyncKv(const std::string &val, BenchmarkConfig *cfg) {
  if (!ParseBool(val, &cfg->use_async)) {
    fprintf(stderr, "[ERROR] Invalid --use_async=%s (expect true|false|0|1)\n", val.c_str());
    return false;
  }
  return true;
}

bool ApplyAsyncBatchNumKv(const std::string &val, BenchmarkConfig *cfg) {
  if (!ParseU32(val, &cfg->async_batch_num) || cfg->async_batch_num == 0) {
    fprintf(stderr, "[ERROR] Invalid --async_batch_num=%s (expect positive integer)\n", val.c_str());
    return false;
  }
  return true;
}

bool ApplyConnectTimeoutKv(const std::string &val, BenchmarkConfig *cfg) {
  if (!ParseU32(val, &cfg->connect_timeout_ms) || cfg->connect_timeout_ms == 0) {
    fprintf(stderr, "[ERROR] Invalid --connect_timeout=%s (expect positive integer ms)\n", val.c_str());
    return false;
  }
  return true;
}

bool ApplyWarmupDurationKv(const std::string &val, BenchmarkConfig *cfg) {
  if (!ParseU32(val, &cfg->warmup_duration_sec)) {
    fprintf(stderr, "[ERROR] Invalid --warmup_duration=%s\n", val.c_str());
    return false;
  }
  return true;
}

bool ApplyCheckConsistencyKv(const std::string &val, BenchmarkConfig *cfg) {
  if (!ParseBool(val, &cfg->check_consistency)) {
    fprintf(stderr, "[ERROR] Invalid --check_consistency=%s (expect true|false|0|1)\n", val.c_str());
    return false;
  }
  return true;
}

bool ApplySeedKv(const std::string &val, BenchmarkConfig *cfg) {
  if (!ParseU64(val, &cfg->seed)) {
    fprintf(stderr, "[ERROR] Invalid --seed=%s\n", val.c_str());
    return false;
  }
  return true;
}

bool ApplyStartBlockSizeKv(const std::string &val, BenchmarkConfig *cfg) {
  if (!ParseU64(val, &cfg->start_block_size) || cfg->start_block_size == 0) {
    fprintf(stderr, "[ERROR] Invalid --start_block_size=%s\n", val.c_str());
    return false;
  }
  cfg->block_size = cfg->start_block_size;
  cfg->block_size_explicit = true;
  return true;
}

bool ApplyMaxBlockSizeKv(const std::string &val, BenchmarkConfig *cfg) {
  if (!ParseU64(val, &cfg->max_block_size) || cfg->max_block_size == 0) {
    fprintf(stderr, "[ERROR] Invalid --max_block_size=%s\n", val.c_str());
    return false;
  }
  return true;
}

bool ApplyStartBatchSizeKv(const std::string &val, BenchmarkConfig *cfg) {
  if (!ParseU32(val, &cfg->start_batch_size) || cfg->start_batch_size == 0U) {
    fprintf(stderr, "[ERROR] Invalid --start_batch_size=%s\n", val.c_str());
    return false;
  }
  cfg->async_batch_num = cfg->start_batch_size;
  return true;
}

bool ApplyMaxBatchSizeKv(const std::string &val, BenchmarkConfig *cfg) {
  if (!ParseU32(val, &cfg->max_batch_size) || cfg->max_batch_size == 0U) {
    fprintf(stderr, "[ERROR] Invalid --max_batch_size=%s\n", val.c_str());
    return false;
  }
  return true;
}

bool ApplyStartThreadsKv(const std::string &val, BenchmarkConfig *cfg) {
  if (!ParseU32(val, &cfg->start_threads) || cfg->start_threads == 0U) {
    fprintf(stderr, "[ERROR] Invalid --start_threads=%s\n", val.c_str());
    return false;
  }
  return true;
}

bool ApplyMaxThreadsKv(const std::string &val, BenchmarkConfig *cfg) {
  if (!ParseU32(val, &cfg->max_threads) || cfg->max_threads == 0U) {
    fprintf(stderr, "[ERROR] Invalid --max_threads=%s\n", val.c_str());
    return false;
  }
  return true;
}

void PopulateCoreKvHandlers(std::map<std::string, KvApplyFn> *table) {
  auto &t = *table;
  t["--role"] = ApplyRoleKv;
  t["-r"] = ApplyRoleKv;
  t["--benchmark_group"] = ApplyBenchmarkGroupKv;
  t["--soc_variant"] = ApplySocVariantKv;
  t["--output_dir"] = ApplyOutputDirKv;
  t["--target_id"] = ApplyTargetIdKv;
  t["--device_id"] = ApplyDeviceIdKv;
  t["-d"] = ApplyDeviceIdKv;
  t["--local_engine"] = ApplyLocalEngineKv;
  t["-l"] = ApplyLocalEngineKv;
  t["--remote_engine"] = ApplyRemoteEngineKv;
  t["-e"] = ApplyRemoteEngineKv;
  t["--tcp_port"] = ApplyTcpPortKv;
  t["-p"] = ApplyTcpPortKv;
  t["--tcp_accept_wait_s"] = ApplyTcpAcceptWaitSecKv;
  t["-a"] = ApplyTcpAcceptWaitSecKv;
  t["--tcp_client_count"] = ApplyTcpClientCountKv;
  t["-c"] = ApplyTcpClientCountKv;
  t["--initiator_memory"] = ApplyInitiatorMemoryKv;
  t["--target_memory"] = ApplyTargetMemoryKv;
  t["--transfer_op"] = ApplyTransferOpKv;
  t["-o"] = ApplyTransferOpKv;
  t["--op_type"] = ApplyOpTypeKv;
  t["--transport"] = ApplyTransportKv;
}

void PopulateBenchKvHandlers(std::map<std::string, KvApplyFn> *table) {
  auto &t = *table;
  t["--total_size"] = ApplyTotalSizeKv;
  t["-t"] = ApplyTotalSizeKv;
  t["--buffer_size"] = ApplyBufferSizeKv;
  t["--block_size"] = ApplyBlockSizeKv;
  t["-k"] = ApplyBlockSizeKv;
  t["--block_steps"] = ApplyBlockStepsKv;
  t["-s"] = ApplyBlockStepsKv;
  t["--loops"] = ApplyLoopsKv;
  t["-n"] = ApplyLoopsKv;
  t["--use_async"] = ApplyUseAsyncKv;
  t["-x"] = ApplyUseAsyncKv;
  t["--async_batch_num"] = ApplyAsyncBatchNumKv;
  t["-y"] = ApplyAsyncBatchNumKv;
  t["--connect_timeout"] = ApplyConnectTimeoutKv;
  t["-C"] = ApplyConnectTimeoutKv;
  t["--warmup_duration"] = ApplyWarmupDurationKv;
  t["--check_consistency"] = ApplyCheckConsistencyKv;
  t["--seed"] = ApplySeedKv;
  t["--start_block_size"] = ApplyStartBlockSizeKv;
  t["--max_block_size"] = ApplyMaxBlockSizeKv;
  t["--start_batch_size"] = ApplyStartBatchSizeKv;
  t["--max_batch_size"] = ApplyMaxBatchSizeKv;
  t["--start_threads"] = ApplyStartThreadsKv;
  t["--max_threads"] = ApplyMaxThreadsKv;
}

const std::map<std::string, KvApplyFn> &KvHandlerTable() {
  static const std::map<std::string, KvApplyFn> kTable = []() {
    std::map<std::string, KvApplyFn> table;
    PopulateCoreKvHandlers(&table);
    PopulateBenchKvHandlers(&table);
    return table;
  }();
  return kTable;
}

bool ApplyKvOverrides(const std::map<std::string, std::string> &kv, BenchmarkConfig *cfg) {
  const auto &table = KvHandlerTable();
  for (const auto &entry : kv) {
    const auto it = table.find(entry.first);
    if (it == table.end()) {
      fprintf(stderr, "[ERROR] Unknown option: %s\n", entry.first.c_str());
      return false;
    }
    if (!it->second(entry.second, cfg)) {
      return false;
    }
  }
  return true;
}

bool ParseHixlOptionPayloads(const std::vector<std::string> &payloads, BenchmarkConfig *cfg) {
  for (const std::string &payload : payloads) {
    const auto inner_eq = payload.find('=');
    if (inner_eq == std::string::npos || inner_eq == 0U) {
      fprintf(stderr,
              "[ERROR] Invalid --hixl_option value %s (expect KEY=VALUE with KEY non-empty)\n",
              payload.c_str());
      return false;
    }
    const std::string opt_key = payload.substr(0, inner_eq);
    const std::string opt_val = payload.substr(inner_eq + 1);
    cfg->hixl_init_options[opt_key] = opt_val;
  }
  return true;
}

}  // namespace

std::string BenchmarkConfig::ComputeDirection(const std::string &initiator_mem,
                                               const std::string &target_mem,
                                               const std::string &op_type) {
  const bool wr = (op_type == "write");
  if (initiator_mem == "device" && target_mem == "device") return wr ? "D2rD" : "rD2D";
  if (initiator_mem == "device" && target_mem == "host")   return wr ? "D2rH" : "rH2D";
  if (initiator_mem == "host"   && target_mem == "host")   return wr ? "H2rH" : "rH2H";
  if (initiator_mem == "host"   && target_mem == "device") return wr ? "H2rD" : "rD2H";
  return "unknown";
}

void BenchmarkConfigParser::PrintUsage(FILE *out) {
  fprintf(out,
          "Usage: hixl_comm_bench key=value ...\n"
          "Required: --role=target|initiator (legacy: client|server) or -r=client|server\n"
          "Keys (all lowercase, value is decimal bytes where noted):\n"
          "  --role|-r            target|initiator|client|server\n"
          "  --benchmark_group    result grouping name (default default)\n"
          "  --soc_variant        auto|a2|a3|a5 — HCCS rules & SOC class (default auto: ACL SOC probe; a5 forbids HCCS)\n"
          "  --transport          hccs|rdma|fabric_mem "
          "(hccs: D2D everywhere; extra H2rD|rD2H on A3-class SOC only; fabric_mem adds EnableUseFabricMem=1)\n"
          "  --initiator_memory   host|device (default device) — initiator-side buffer\n"
          "  --target_memory      host|device (default device) — target-side buffer\n"
          "  --op_type            read|write|mix (alias of --transfer_op)\n"
          "  --start_block_size   first block size in bytes (alias of --block_size)\n"
          "  --max_block_size     max block size in bytes; block size doubles until this value\n"
          "  --output_dir         CSV/JSONL result output directory (default output)\n"
          "  --device_id|-d       device id (int), or comma list e.g. 0,1 (broadcast if single value)\n"
          "  --local_engine|-l    local HIXL endpoint(s), comma-separated; IPv6 use [ip]:port\n"
          "  --remote_engine|-e   client: one or more host:port (comma-separated); TCP uses each host + tcp coord port\n"
          "                        server: optional (TCP binds --tcp_port only; default kept); multi-value not supported\n"
          "  --tcp_port|-p        TCP coordination port(s): one value, or comma list matching multi-endpoint count\n"
          "                        (e.g. two servers on one host need distinct ports: -p=20000,20001)\n"
          "  --tcp_accept_wait_s|-a  server only: max wall time for TCP connect phase in seconds (default 30)\n"
          "  --tcp_client_count|-c  server only: TCP clients to wait for before proceed (default 1, max %" PRIu32 ")\n"
          "  --transfer_op|-o     read|write\n"
          "  --total_size|-t      bytes transferred per block-size step (default %" PRIu64 ")\n"
          "  --buffer_size        allocation/register size in bytes (default %" PRIu64 ")\n"
          "  --block_size|-k      first block size in bytes (default: same as total_size)\n"
          "  --block_steps|-s     block count: sizes are block_size<<i, i in [0,steps-1] (default %u)\n"
          "  --loops|-n           repeat full step ladder (default %u)\n"
          "  --use_async|-x       true|false (default false), enable async transfer mode\n"
          "  --async_batch_num|-y async requests per batch (default 1), requires: total_size %% async_batch_num == 0\n"
          "  --connect_timeout|-C connect timeout in ms (default 60000)\n"
          "  --hixl_option|-H     HIXL Initialize() option, form KEY=VALUE (repeatable); "
          "KEY e.g. LocalCommRes, BufferPool, RdmaTrafficClass, RdmaServiceLevel, adxl.*\n"
          "                       If neither BufferPool nor adxl.BufferPool is set, BufferPool=0:0 is added (benchmark default).\n"
          "Multi-endpoint: lengths must match max(n_d,n_l,n_r) after broadcasting single values; server allows only one.\n"
          "Client: one local + many remotes uses one Hixl and concurrent TCP/HIXL per remote; many locals use one thread per lane.\n"
          "Defaults: client device_id=0 local_engine=127.0.0.1:16000 remote_engine=127.0.0.1:16001\n"
          "          server device_id=1 local_engine=127.0.0.1:16001 remote_engine=127.0.0.1\n"
          "          initiator_memory=device target_memory=device transfer_op=read tcp_port=20000 tcp_accept_wait_s=30 "
          "tcp_client_count=1\n",
          kTcpClientCountMax, kDefaultTotalSize, kDefaultBufferSize, kDefaultBlockSteps, kDefaultLoops);
}

std::map<AscendString, AscendString> BenchmarkConfigParser::BuildInitializeOptions(const BenchmarkConfig &cfg) {
  std::map<AscendString, AscendString> options;
  for (const auto &entry : cfg.hixl_init_options) {
    options[AscendString(entry.first.c_str())] = AscendString(entry.second.c_str());
  }
  constexpr const char kAdxlBufferPoolKey[] = "adxl.BufferPool";
  const bool has_explicit_buffer =
      cfg.hixl_init_options.find(OPTION_BUFFER_POOL) != cfg.hixl_init_options.cend() ||
      cfg.hixl_init_options.find(kAdxlBufferPoolKey) != cfg.hixl_init_options.cend();
  if (!has_explicit_buffer) {
    options[AscendString(OPTION_BUFFER_POOL)] = AscendString("0:0");
  }
  if (cfg.transport == "fabric_mem" &&
      cfg.hixl_init_options.find(OPTION_ENABLE_USE_FABRIC_MEM) == cfg.hixl_init_options.cend()) {
    options[AscendString(OPTION_ENABLE_USE_FABRIC_MEM)] = AscendString("1");
  }
  return options;
}

bool BenchmarkConfigParser::ApplyTransportEnvironment(const BenchmarkConfig &cfg) {
  if (cfg.transport == "rdma") {
    if (setenv("HCCL_INTRA_ROCE_ENABLE", "1", 1) != 0) {
      std::fprintf(stderr, "[ERROR] setenv HCCL_INTRA_ROCE_ENABLE=1 failed: %s\n", std::strerror(errno));
      return false;
    }
    return true;
  }
  // hccs / fabric_mem: do not modify HCCL environment variables (fabric_mem uses init options only)
  return true;
}

namespace {

uint32_t ComputeBlockSteps(uint64_t start, uint64_t max) {
  uint32_t steps = 1U;
  uint64_t cur = start;
  while (cur < max && cur <= (UINT64_MAX >> 1U)) {
    cur <<= 1U;
    ++steps;
  }
  return steps;
}

}  // namespace

bool BenchmarkConfigParser::BuildFromArgv(int argc, char **argv, BenchmarkConfig *cfg) {
  std::map<std::string, std::string> kv;
  std::vector<std::string> hixl_option_payloads;
  if (!CollectArgs(argc, argv, &kv, &hixl_option_payloads)) {
    return false;
  }
  auto it_role = kv.find("--role");
  if (it_role == kv.end()) {
    it_role = kv.find("-r");
  }
  if (it_role == kv.end()) {
    fprintf(stderr, "[ERROR] Missing required --role=client|server\n");
    BenchmarkConfigParser::PrintUsage(stderr);
    return false;
  }
  const std::string &rv = it_role->second;
  if (rv == "client" || rv == "initiator") {
    cfg->role = BenchmarkRole::kClient;
  } else if (rv == "server" || rv == "target") {
    cfg->role = BenchmarkRole::kServer;
  } else {
    fprintf(stderr, "[ERROR] Invalid --role=%s\n", rv.c_str());
    return false;
  }
  ApplyCommonDefaults(cfg);
  ApplyRoleDefaults(cfg);
  if (!ApplyKvOverrides(kv, cfg)) {
    return false;
  }
  if (!ParseHixlOptionPayloads(hixl_option_payloads, cfg)) {
    return false;
  }
  if (!cfg->block_size_explicit) {
    cfg->block_size = cfg->total_size;
    cfg->start_block_size = cfg->block_size;
  }
  if (cfg->max_block_size < cfg->block_size) {
    cfg->max_block_size = cfg->block_size;
  }
  if (cfg->max_block_size != cfg->block_size) {
    cfg->block_steps = ComputeBlockSteps(cfg->block_size, cfg->max_block_size);
  }
  EnsureEndpointLists(cfg);
  return true;
}

namespace {

bool ValidateEnginesNotEmpty(const BenchmarkConfig *cfg) {
  if (cfg->local_engine.empty()) {
    fprintf(stderr, "[ERROR] local_engine is empty\n");
    return false;
  }
  if (cfg->remote_engine.empty()) {
    fprintf(stderr, "[ERROR] remote_engine is empty\n");
    return false;
  }
  return true;
}

bool ValidateListLengths(size_t nd, size_t nl, size_t nr, size_t nt, size_t n_max) {
  auto invalid_len = [](size_t n, size_t cap) { return n != 1U && n != cap; };
  if (invalid_len(nd, n_max) || invalid_len(nl, n_max) || invalid_len(nr, n_max)) {
    fprintf(stderr, "[ERROR] device_id/local_engine/remote_engine list lengths (%zu,%zu,%zu) must each be 1 or %zu\n",
            nd, nl, nr, n_max);
    return false;
  }
  if (invalid_len(nt, n_max)) {
    fprintf(stderr,
            "[ERROR] --tcp_port comma-list length (%zu) must be 1 or %zu (same rule as multi local_engine / "
            "remote_engine)\n",
            nt, n_max);
    return false;
  }
  return true;
}

bool ValidateMultiDeviceRequirement(size_t n_max, size_t nl, size_t nr) {
  if (n_max > 1U && nl == 1U && nr == 1U) {
    fprintf(stderr,
            "[ERROR] multiple device_id values require multiple local_engine or remote_engine entries\n");
    return false;
  }
  return true;
}

void ExpandConfigLists(BenchmarkConfig *cfg, size_t n_max, size_t nd, size_t nl, size_t nr, size_t nt) {
  cfg->expanded_device_ids.clear();
  cfg->expanded_local_engines.clear();
  cfg->expanded_remote_engines.clear();
  cfg->expanded_tcp_ports.clear();
  cfg->expanded_device_ids.reserve(n_max);
  cfg->expanded_local_engines.reserve(n_max);
  cfg->expanded_remote_engines.reserve(n_max);
  cfg->expanded_tcp_ports.reserve(n_max);
  for (size_t i = 0; i < n_max; ++i) {
    cfg->expanded_device_ids.push_back(cfg->device_id_list[nd == 1U ? 0 : i]);
    cfg->expanded_local_engines.push_back(cfg->local_engine_list[nl == 1U ? 0 : i]);
    cfg->expanded_remote_engines.push_back(cfg->remote_engine_list[nr == 1U ? 0 : i]);
    cfg->expanded_tcp_ports.push_back(cfg->tcp_port_list[nt == 1U ? 0 : i]);
  }
  cfg->device_id = cfg->expanded_device_ids[0];
  cfg->tcp_port = cfg->expanded_tcp_ports[0];
}

bool ValidateClientRemoteEngines(const BenchmarkConfig *cfg) {
  for (size_t i = 0; i < cfg->expanded_remote_engines.size(); ++i) {
    const std::string &re = cfg->expanded_remote_engines[i];
    if (re.find(':') == std::string::npos) {
      fprintf(stderr,
              "[ERROR] client remote_engine[%zu] must be host:port (e.g. 127.0.0.1:16001), got \"%s\"\n", i,
              re.c_str());
      return false;
    }
  }
  return true;
}

bool ValidateServerConfig(const BenchmarkConfig *cfg, size_t n_max) {
  if (n_max > 1U) {
    fprintf(stderr, "[ERROR] server does not support multiple device_id/local_engine/remote_engine; use one each\n");
    return false;
  }
  if (cfg->tcp_accept_wait_sec == 0U || cfg->tcp_accept_wait_sec > 86400U) {
    fprintf(stderr, "[ERROR] server tcp_accept_wait_s out of range\n");
    return false;
  }
  if (cfg->tcp_client_count == 0U || cfg->tcp_client_count > kTcpClientCountMax) {
    fprintf(stderr, "[ERROR] server tcp_client_count out of range\n");
    return false;
  }
  return true;
}

bool ValidateMemoryTypeValue(const std::string &mem) {
  if (mem != "host" && mem != "device") {
    fprintf(stderr, "[ERROR] Invalid memory type: %s\n", mem.c_str());
    return false;
  }
  return true;
}

bool ValidateTransferOp(const std::string &op) {
  if (op != "write" && op != "read" && op != "mix") {
    fprintf(stderr, "[ERROR] Invalid transfer_op/op_type: %s\n", op.c_str());
    return false;
  }
  return true;
}

bool ValidateTransport(const std::string &transport) {
  if (transport != "hccs" && transport != "rdma" && transport != "fabric_mem") {
    fprintf(stderr, "[ERROR] Invalid transport: %s\n", transport.c_str());
    return false;
  }
  return true;
}

enum class BenchSocKind { kA2, kA3, kA5 };

BenchSocKind ClassifySocNameForBenchRules(const std::string &soc_name) {
  // Ascend950* → A5 (no HCCS in comm benchmarks).
  if (soc_name.find("Ascend950") != std::string::npos) {
    return BenchSocKind::kA5;
  }
  // Ascend910B* / *910B* → A2-class.
  if (soc_name.find("910B") != std::string::npos) {
    return BenchSocKind::kA2;
  }
  // Ascend910_* / Ascend910 without B → A3-class.
  if (soc_name.find("Ascend910") != std::string::npos) {
    return BenchSocKind::kA3;
  }
  return BenchSocKind::kA2;
}

BenchSocKind ProbeSocKindViaAcl(int32_t device_id) {
  aclError er = aclInit(nullptr);
  if (er != ACL_ERROR_NONE) {
    // CANN returns ACL_ERROR_REPEAT_INITIALIZE when ACL was already initialized (often by peer tooling).
    constexpr aclError kAclRepeatInitialize = static_cast<aclError>(100002);
    if (er != kAclRepeatInitialize) {
      fprintf(stderr,
              "[WARN] aclInit failed for --soc_variant=auto probe (ret=%d); assuming A2-class HCCS rules "
              "(override with --soc_variant=a3|a5 on Ascend910 / Ascend950 silicon)\n",
              static_cast<int>(er));
      return BenchSocKind::kA2;
    }
  }
  er = aclrtSetDevice(device_id);
  if (er != ACL_ERROR_NONE) {
    fprintf(stderr,
            "[WARN] aclrtSetDevice(%d) failed for SOC probe (ret=%d); assuming A2-class HCCS rules "
            "(override with --soc_variant=a3|a5)\n",
            static_cast<int>(device_id), static_cast<int>(er));
    return BenchSocKind::kA2;
  }
  const char *soc_cstr = aclrtGetSocName();
  if (soc_cstr == nullptr || soc_cstr[0] == '\0') {
    fprintf(stderr,
            "[WARN] aclrtGetSocName() empty; assuming A2-class HCCS rules "
            "(override with --soc_variant=a3|a5)\n");
    return BenchSocKind::kA2;
  }
  const std::string soc(soc_cstr);
  fprintf(stdout, "[INFO] SOC probe for HCCS rules: aclrtGetSocName()=%s\n", soc_cstr);
  return ClassifySocNameForBenchRules(soc);
}

BenchSocKind ResolveSocKindForHccs(const BenchmarkConfig *cfg) {
  const std::string &sv = cfg->soc_variant;
  if (sv == "a2") {
    return BenchSocKind::kA2;
  }
  if (sv == "a3") {
    return BenchSocKind::kA3;
  }
  if (sv == "a5") {
    return BenchSocKind::kA5;
  }
  int32_t dev_id = cfg->device_id;
  if (!cfg->expanded_device_ids.empty()) {
    dev_id = cfg->expanded_device_ids[0];
  }
  return ProbeSocKindViaAcl(dev_id);
}

bool ValidateHccsMemoryCombination(const BenchmarkConfig *cfg) {
  if (cfg->transport != "hccs") {
    return true;
  }
  const BenchSocKind kind = ResolveSocKindForHccs(cfg);
  if (kind == BenchSocKind::kA5) {
    fprintf(stderr,
            "[ERROR] transport=hccs is not supported on Ascend950-class (A5) SOC; use rdma or fabric_mem\n");
    return false;
  }
  const std::string &im = cfg->initiator_memory_type;
  const std::string &tm = cfg->target_memory_type;
  if (im == "device" && tm == "device") {
    return true;
  }
  if (cfg->transfer_op == "mix") {
    fprintf(stderr,
            "[ERROR] HCCS transport does not support transfer_op=mix unless initiator_memory=device and "
            "target_memory=device\n");
    return false;
  }
  if (im == "host" && tm == "device") {
    if (kind == BenchSocKind::kA3 && (cfg->transfer_op == "read" || cfg->transfer_op == "write")) {
      return true;
    }
    fprintf(stderr,
            "[ERROR] HCCS host→device directions (H2rD/rD2H) require Ascend910-class SOC "
            "(override mis-detection with --soc_variant=a3); "
            "got initiator_memory=%s target_memory=%s transfer_op=%s\n",
            im.c_str(), tm.c_str(), cfg->transfer_op.c_str());
    return false;
  }
  fprintf(stderr,
          "[ERROR] HCCS: A2 allows D2D only; A3 adds H2rD|rD2H on host→device; "
          "got initiator_memory=%s target_memory=%s (use rdma or fabric_mem for other directions)\n",
          im.c_str(), tm.c_str());
  return false;
}

bool ValidateBufferSize(const BenchmarkConfig *cfg) {
  if (cfg->buffer_size < cfg->total_size) {
    fprintf(stderr,
            "[ERROR] buffer_size (%" PRIu64 ") must be >= total_size (%" PRIu64 ")\n",
            cfg->buffer_size, cfg->total_size);
    return false;
  }
  return true;
}

bool ValidateBlockSteps(const BenchmarkConfig *cfg) {
  for (uint32_t i = 0; i < cfg->block_steps; ++i) {
    if (cfg->block_size > (UINT64_MAX >> i)) {
      fprintf(stderr, "[ERROR] block_size<<%u overflows\n", i);
      return false;
    }
    const uint64_t block = cfg->block_size << i;
    if (block == 0) {
      fprintf(stderr, "[ERROR] computed block size is 0\n");
      return false;
    }
    if (block > cfg->total_size) {
      fprintf(stderr,
              "[ERROR] block size (%" PRIu64 ") at step %u must be <= total_size (%" PRIu64 ")\n",
              block, i, cfg->total_size);
      return false;
    }
    if (cfg->total_size % block != 0) {
      fprintf(stderr,
              "[ERROR] total_size (%" PRIu64 ") must be divisible by block size (%" PRIu64 ") at step %u\n",
              cfg->total_size, block, i);
      return false;
    }
  }
  return true;
}

bool ValidateAsyncConfig(const BenchmarkConfig *cfg) {
  if (!cfg->use_async) {
    return true;
  }
  if (cfg->total_size % cfg->async_batch_num != 0) {
    fprintf(stderr,
            "[ERROR] total_size (%" PRIu64 ") must be divisible by async_batch_num (%" PRIu32 ") "
            "when use_async=true\n",
            cfg->total_size, cfg->async_batch_num);
    return false;
  }
  const uint64_t per_req_size = cfg->total_size / cfg->async_batch_num;
  for (uint32_t i = 0; i < cfg->block_steps; ++i) {
    const uint64_t block = cfg->block_size << i;
    if (per_req_size % block != 0) {
      fprintf(stderr,
              "[ERROR] per-request size (%" PRIu64 ") must be divisible by block size (%" PRIu64 ") "
              "at step %u when use_async=true\n",
              per_req_size, block, i);
      return false;
    }
  }
  return true;
}

bool ValidateBenchmarkTopology(BenchmarkConfig *cfg) {
  if (!ValidateEnginesNotEmpty(cfg)) {
    return false;
  }
  const size_t nd = cfg->device_id_list.size();
  const size_t nl = cfg->local_engine_list.size();
  const size_t nr = cfg->remote_engine_list.size();
  const size_t n_max = std::max(nd, std::max(nl, nr));
  if (cfg->tcp_port_list.empty()) {
    cfg->tcp_port_list.push_back(cfg->tcp_port);
  }
  const size_t nt = cfg->tcp_port_list.size();
  if (!ValidateListLengths(nd, nl, nr, nt, n_max)) {
    return false;
  }
  if (!ValidateMultiDeviceRequirement(n_max, nl, nr)) {
    return false;
  }
  ExpandConfigLists(cfg, n_max, nd, nl, nr, nt);
  if (cfg->role == BenchmarkRole::kClient && !ValidateClientRemoteEngines(cfg)) {
    return false;
  }
  if (cfg->role == BenchmarkRole::kServer && !ValidateServerConfig(cfg, n_max)) {
    return false;
  }
  return true;
}

bool ValidateBenchmarkWorkload(BenchmarkConfig *cfg) {
  if (!ValidateTransferOp(cfg->transfer_op)) {
    return false;
  }
  if (!ValidateTransport(cfg->transport) ||
      !ValidateMemoryTypeValue(cfg->initiator_memory_type) ||
      !ValidateMemoryTypeValue(cfg->target_memory_type)) {
    return false;
  }
  if (!ValidateHccsMemoryCombination(cfg)) {
    return false;
  }
  if (!ValidateBufferSize(cfg)) {
    return false;
  }
  if (!ValidateBlockSteps(cfg)) {
    return false;
  }
  if (!ValidateAsyncConfig(cfg)) {
    return false;
  }
  return true;
}

}  // namespace

bool BenchmarkConfigParser::Validate(BenchmarkConfig *cfg) {
  return ValidateBenchmarkTopology(cfg) && ValidateBenchmarkWorkload(cfg);
}

void BenchmarkConfigParser::LogExpandedEndpoints(FILE *out, const BenchmarkConfig &cfg) {
  const size_t n = cfg.expanded_device_ids.size();
  fprintf(out, "[INFO] endpoint_pairs=%zu\n", n);
  for (size_t i = 0; i < n; ++i) {
    const unsigned tcp_p =
        (i < cfg.expanded_tcp_ports.size()) ? static_cast<unsigned>(cfg.expanded_tcp_ports[i]) : 0U;
    fprintf(out, "[INFO]   [%zu] device_id=%d local_engine=%s remote_engine=%s tcp_coord_port=%u\n", i,
            static_cast<int>(cfg.expanded_device_ids[i]), cfg.expanded_local_engines[i].c_str(),
            cfg.expanded_remote_engines[i].c_str(), tcp_p);
  }
}

}  // namespace hixl_benchmark
