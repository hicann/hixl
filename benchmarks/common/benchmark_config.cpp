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
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

using hixl::AscendString;
using hixl::OPTION_BUFFER_POOL;

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
      std::exit(0);
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
    cfg->device_id = 0;
    cfg->local_engine = "127.0.0.1:16000";
    cfg->remote_engine = "127.0.0.1:16001";
  } else if (cfg->role == BenchmarkRole::kServer) {
    cfg->device_id = 1;
    cfg->local_engine = "127.0.0.1:16001";
    cfg->remote_engine = "127.0.0.1";
  }
}

void ApplyCommonDefaults(BenchmarkConfig *cfg) {
  cfg->tcp_port = 20000;
  cfg->tcp_accept_wait_sec = 30U;
  cfg->tcp_client_count = 1U;
  cfg->transfer_mode = "d2d";
  cfg->transfer_op = "read";
  cfg->use_buffer_pool = false;
  cfg->total_size = kDefaultTotalSize;
  cfg->block_size = kDefaultTotalSize;
  cfg->block_steps = kDefaultBlockSteps;
  cfg->loops = kDefaultLoops;
}

using KvApplyFn = bool (*)(const std::string &val, BenchmarkConfig *cfg);

bool ApplyRoleKv(const std::string &val, BenchmarkConfig *cfg) {
  if (val == "client") {
    cfg->role = BenchmarkRole::kClient;
    return true;
  }
  if (val == "server") {
    cfg->role = BenchmarkRole::kServer;
    return true;
  }
  fprintf(stderr, "[ERROR] Invalid --role=%s (expect client|server)\n", val.c_str());
  return false;
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
  if (sec > 86400U) {
    fprintf(stderr, "[ERROR] --tcp_accept_wait_s too large (max 86400)\n");
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

bool ApplyTransferModeKv(const std::string &val, BenchmarkConfig *cfg) {
  cfg->transfer_mode = val;
  return true;
}

bool ApplyTransferOpKv(const std::string &val, BenchmarkConfig *cfg) {
  cfg->transfer_op = val;
  return true;
}

bool ApplyUseBufferPoolKv(const std::string &val, BenchmarkConfig *cfg) {
  if (!ParseBool(val, &cfg->use_buffer_pool)) {
    fprintf(stderr, "[ERROR] Invalid --use_buffer_pool=%s (expect true|false|0|1)\n", val.c_str());
    return false;
  }
  return true;
}

bool ApplyTotalSizeKv(const std::string &val, BenchmarkConfig *cfg) {
  if (!ParseU64(val, &cfg->total_size) || cfg->total_size == 0) {
    fprintf(stderr, "[ERROR] Invalid --total_size=%s\n", val.c_str());
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

const std::map<std::string, KvApplyFn> &KvHandlerTable() {
  static const std::map<std::string, KvApplyFn> kTable = {
      {"--role", ApplyRoleKv},
      {"-r", ApplyRoleKv},
      {"--device_id", ApplyDeviceIdKv},
      {"-d", ApplyDeviceIdKv},
      {"--local_engine", ApplyLocalEngineKv},
      {"-l", ApplyLocalEngineKv},
      {"--remote_engine", ApplyRemoteEngineKv},
      {"-e", ApplyRemoteEngineKv},
      {"--tcp_port", ApplyTcpPortKv},
      {"-p", ApplyTcpPortKv},
      {"--tcp_accept_wait_s", ApplyTcpAcceptWaitSecKv},
      {"-a", ApplyTcpAcceptWaitSecKv},
      {"--tcp_client_count", ApplyTcpClientCountKv},
      {"-c", ApplyTcpClientCountKv},
      {"--transfer_mode", ApplyTransferModeKv},
      {"-m", ApplyTransferModeKv},
      {"--transfer_op", ApplyTransferOpKv},
      {"-o", ApplyTransferOpKv},
      {"--use_buffer_pool", ApplyUseBufferPoolKv},
      {"-b", ApplyUseBufferPoolKv},
      {"--total_size", ApplyTotalSizeKv},
      {"-t", ApplyTotalSizeKv},
      {"--block_size", ApplyBlockSizeKv},
      {"-k", ApplyBlockSizeKv},
      {"--block_steps", ApplyBlockStepsKv},
      {"-s", ApplyBlockStepsKv},
      {"--loops", ApplyLoopsKv},
      {"-n", ApplyLoopsKv},
  };
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

void BenchmarkConfigParser::PrintUsage(FILE *out) {
  fprintf(out,
          "Usage: benchmark key=value ...\n"
          "Required: --role=client|server  or  -r=client|server\n"
          "Keys (all lowercase, value is decimal bytes where noted):\n"
          "  --role|-r            client|server\n"
          "  --device_id|-d       device id (int), or comma list e.g. 0,1 (broadcast if single value)\n"
          "  --local_engine|-l    local HIXL endpoint(s), comma-separated; IPv6 use [ip]:port\n"
          "  --remote_engine|-e   client: one or more host:port (comma-separated); TCP uses each host + tcp coord port\n"
          "                        server: optional (TCP binds --tcp_port only; default kept); multi-value not supported\n"
          "  --tcp_port|-p        TCP coordination port(s): one value, or comma list matching multi-endpoint count\n"
          "                        (e.g. two servers on one host need distinct ports: -p=20000,20001)\n"
          "  --tcp_accept_wait_s|-a  server only: max wall time for TCP connect phase in seconds (default 30)\n"
          "  --tcp_client_count|-c  server only: TCP clients to wait for before proceed (default 1, max %" PRIu32 ")\n"
          "  --transfer_mode|-m   d2d|h2d|d2h|h2h\n"
          "  --transfer_op|-o     read|write\n"
          "  --use_buffer_pool|-b true|false\n"
          "  --total_size|-t      total buffer size in bytes (default %" PRIu64 ")\n"
          "  --block_size|-k      first block size in bytes (default: same as total_size)\n"
          "  --block_steps|-s     block count: sizes are block_size<<i, i in [0,steps-1] (default %u)\n"
          "  --loops|-n           repeat full step ladder (default %u)\n"
          "  --hixl_option|-H     HIXL Initialize() option, form KEY=VALUE (repeatable); "
          "KEY e.g. LocalCommRes, BufferPool, RdmaTrafficClass, RdmaServiceLevel, adxl.*\n"
          "                       If neither BufferPool nor adxl.BufferPool is set and "
          "--use_buffer_pool=false, BufferPool=0:0 is added (legacy).\n"
          "Multi-endpoint: lengths must match max(n_d,n_l,n_r) after broadcasting single values; server allows only one.\n"
          "Client: one local + many remotes uses one Hixl and concurrent TCP/HIXL per remote; many locals use one thread per lane.\n"
          "Defaults: client device_id=0 local_engine=127.0.0.1:16000 remote_engine=127.0.0.1:16001\n"
          "          server device_id=1 local_engine=127.0.0.1:16001 remote_engine=127.0.0.1\n"
          "          transfer_mode=d2d transfer_op=read use_buffer_pool=false tcp_port=20000 tcp_accept_wait_s=30 "
          "tcp_client_count=1\n",
          kTcpClientCountMax, kDefaultTotalSize, kDefaultBlockSteps, kDefaultLoops);
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
  if (!has_explicit_buffer && !cfg.use_buffer_pool) {
    options[AscendString(OPTION_BUFFER_POOL)] = AscendString("0:0");
  }
  return options;
}

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
  if (rv == "client") {
    cfg->role = BenchmarkRole::kClient;
  } else if (rv == "server") {
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

bool ValidateTransferMode(const std::string &mode) {
  if (mode != "d2d" && mode != "h2d" && mode != "d2h" && mode != "h2h") {
    fprintf(stderr, "[ERROR] Invalid transfer_mode: %s\n", mode.c_str());
    return false;
  }
  return true;
}

bool ValidateTransferOp(const std::string &op) {
  if (op != "write" && op != "read") {
    fprintf(stderr, "[ERROR] Invalid transfer_op: %s\n", op.c_str());
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
    if (cfg->total_size % block != 0) {
      fprintf(stderr,
              "[ERROR] total_size (%" PRIu64 ") must be divisible by block size (%" PRIu64 ") at step %u\n",
              cfg->total_size, block, i);
      return false;
    }
  }
  return true;
}

}  // namespace

bool BenchmarkConfigParser::Validate(BenchmarkConfig *cfg) {
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
  if (!ValidateTransferMode(cfg->transfer_mode)) {
    return false;
  }
  if (!ValidateTransferOp(cfg->transfer_op)) {
    return false;
  }
  if (!ValidateBlockSteps(cfg)) {
    return false;
  }
  return true;
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
