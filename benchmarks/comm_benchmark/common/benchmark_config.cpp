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
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "acl/acl.h"

using hixl::AscendString;
using hixl::OPTION_BUFFER_POOL;
using hixl::OPTION_ENABLE_USE_FABRIC_MEM;
using hixl::OPTION_GLOBAL_RESOURCE_CONFIG;
using hixl::OPTION_LOCAL_COMM_RES;

namespace hixl_benchmark {

constexpr int32_t kPortMaxValue = 65535;
constexpr uint16_t kPeerCoordPortOffset = 10000U;

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

bool ExtractEndpointHostAndPort(const std::string &endpoint, std::string &host, uint16_t &port) {
  if (endpoint.empty()) {
    return false;
  }
  std::string host_part;
  std::string port_part;
  if (endpoint.front() == '[') {
    const auto close = endpoint.find(']');
    if (close == std::string::npos || close + 1U >= endpoint.size() || endpoint[close + 1U] != ':') {
      return false;
    }
    host_part = endpoint.substr(1, close - 1U);
    port_part = endpoint.substr(close + 2U);
  } else {
    const auto sep = endpoint.rfind(':');
    if (sep == std::string::npos || sep == 0U || sep + 1U >= endpoint.size()) {
      return false;
    }
    host_part = endpoint.substr(0, sep);
    port_part = endpoint.substr(sep + 1U);
  }
  uint64_t parsed_port = 0;
  try {
    std::size_t pos = 0;
    parsed_port = std::stoull(port_part, &pos, 10);
    if (pos != port_part.size()) {
      return false;
    }
  } catch (const std::invalid_argument &) {
    return false;
  } catch (const std::out_of_range &) {
    return false;
  }
  if (parsed_port == 0 || parsed_port > static_cast<uint64_t>(kPortMaxValue)) {
    return false;
  }
  host = host_part;
  port = static_cast<uint16_t>(parsed_port);
  return true;
}

uint16_t DerivePeerCoordPort(uint16_t engine_port) {
  if (engine_port <= static_cast<uint16_t>(kPortMaxValue - kPeerCoordPortOffset)) {
    return static_cast<uint16_t>(engine_port + kPeerCoordPortOffset);
  }
  return static_cast<uint16_t>(engine_port - kPeerCoordPortOffset);
}

namespace {

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
  cfg->peer_wait_sec = kDefaultAcceptWaitSec;
  cfg->peer_count = 1U;
  cfg->op = "read";
  cfg->transport = "hccs";
  cfg->soc_variant = "auto";
  cfg->initiator_memory_type = "device";
  cfg->target_memory_type = "device";
  cfg->transfer_size = kDefaultTotalSize;
  cfg->buffer_size = kDefaultBufferSize;
  cfg->block_sizes.clear();
  cfg->loops = kDefaultLoops;
  cfg->use_async = false;
  cfg->async_batch_num = 1U;
  cfg->connect_timeout_ms = kDefaultConnectTimeoutMs;
  cfg->memory_explicit = false;
  cfg->remote_memory_explicit = false;
  cfg->op_explicit = false;
}

using KvApplyFn = bool (*)(const std::string &val, BenchmarkConfig *cfg);

bool ApplyRoleKv(const std::string &val, BenchmarkConfig *cfg) {
  if (val == "initiator") {
    cfg->role = BenchmarkRole::kClient;
    cfg->role_name = val;
    return true;
  }
  if (val == "target") {
    cfg->role = BenchmarkRole::kServer;
    cfg->role_name = val;
    return true;
  }
  fprintf(stderr, "[ERROR] Invalid --role=%s (expect target|initiator)\n", val.c_str());
  return false;
}

bool ApplyBenchmarkGroupKv(const std::string &val, BenchmarkConfig *cfg) {
  cfg->benchmark_group = val;
  return true;
}

bool ApplyOutputDirKv(const std::string &val, BenchmarkConfig *cfg) {
  cfg->output_dir = val;
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

bool ApplyMemoryKv(const std::string &val, BenchmarkConfig *cfg) {
  if (val != "host" && val != "device") {
    fprintf(stderr, "[ERROR] Invalid --memory=%s (expect host|device)\n", val.c_str());
    return false;
  }
  if (cfg->role == BenchmarkRole::kServer) {
    cfg->target_memory_type = val;
  } else {
    cfg->initiator_memory_type = val;
  }
  cfg->memory_explicit = true;
  return true;
}

bool ApplyRemoteMemoryKv(const std::string &val, BenchmarkConfig *cfg) {
  if (val != "host" && val != "device") {
    fprintf(stderr, "[ERROR] Invalid --remote_memory=%s (expect host|device)\n", val.c_str());
    return false;
  }
  cfg->target_memory_type = val;
  cfg->remote_memory_explicit = true;
  return true;
}

bool ApplyOpKv(const std::string &val, BenchmarkConfig *cfg) {
  cfg->op = val;
  cfg->op_explicit = true;
  return true;
}

bool ApplyPeerWaitSecKv(const std::string &val, BenchmarkConfig *cfg) {
  uint32_t sec = 0;
  if (!ParseU32(val, &sec) || sec == 0U) {
    fprintf(stderr, "[ERROR] Invalid --peer_wait_s=%s (expect positive integer)\n", val.c_str());
    return false;
  }
  if (sec > kMaxAcceptWaitSec) {
    fprintf(stderr, "[ERROR] --peer_wait_s too large (max %u)\n", kMaxAcceptWaitSec);
    return false;
  }
  cfg->peer_wait_sec = sec;
  return true;
}

bool ApplyPeerCountKv(const std::string &val, BenchmarkConfig *cfg) {
  uint32_t n = 0;
  if (!ParseU32(val, &n) || n == 0U) {
    fprintf(stderr, "[ERROR] Invalid --peer_count=%s (expect integer 1..%" PRIu32 ")\n", val.c_str(),
            kTcpClientCountMax);
    return false;
  }
  if (n > kTcpClientCountMax) {
    fprintf(stderr, "[ERROR] --peer_count too large (max %" PRIu32 ")\n", kTcpClientCountMax);
    return false;
  }
  cfg->peer_count = n;
  return true;
}

bool ApplyTransportKv(const std::string &val, BenchmarkConfig *cfg) {
  cfg->transport = val;
  return true;
}

bool ApplyHostRoceIpKv(const std::string &val, BenchmarkConfig *cfg) {
  cfg->host_roce_ip = val;
  cfg->host_roce_ip_list = SplitCommaList(val);
  if (cfg->host_roce_ip_list.empty()) {
    fprintf(stderr, "[ERROR] host_roce_ip is empty\n");
    return false;
  }
  return true;
}

bool LookupSizeSuffixFactor(const std::string &suffix, uint64_t &factor) {
  struct SuffixEntry {
    const char *name;
    uint64_t value;
  };
  constexpr SuffixEntry kTable[] = {
      {"", 1U},
      {"k", kBytesPerKiB},
      {"kb", kBytesPerKiB},
      {"kib", kBytesPerKiB},
      {"m", kBytesPerMiB},
      {"mb", kBytesPerMiB},
      {"mib", kBytesPerMiB},
      {"g", kBytesPerGiB},
      {"gb", kBytesPerGiB},
      {"gib", kBytesPerGiB},
      {"t", kBytesPerTiB},
      {"tb", kBytesPerTiB},
      {"tib", kBytesPerTiB},
  };
  for (const auto &entry : kTable) {
    if (suffix == entry.name) {
      factor = entry.value;
      return true;
    }
  }
  return false;
}

bool ParseByteSize(const std::string &val, uint64_t &out) {
  if (val.empty()) {
    return false;
  }
  size_t digits = 0;
  while (digits < val.size() && std::isdigit(static_cast<unsigned char>(val[digits])) != 0) {
    ++digits;
  }
  if (digits == 0U) {
    return false;
  }
  uint64_t base = 0;
  if (!ParseU64(val.substr(0, digits), &base)) {
    return false;
  }
  std::string suffix = val.substr(digits);
  std::transform(suffix.begin(), suffix.end(), suffix.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  uint64_t factor = 1U;
  if (!LookupSizeSuffixFactor(suffix, factor)) {
    return false;
  }
  if (base == 0U || base > std::numeric_limits<uint64_t>::max() / factor) {
    return false;
  }
  out = base * factor;
  return true;
}

bool ApplyTransferSizeKv(const std::string &val, BenchmarkConfig *cfg) {
  if (!ParseByteSize(val, cfg->transfer_size)) {
    fprintf(stderr, "[ERROR] Invalid --transfer_size=%s\n", val.c_str());
    return false;
  }
  return true;
}

bool ApplyBufferSizeKv(const std::string &val, BenchmarkConfig *cfg) {
  if (!ParseByteSize(val, cfg->buffer_size)) {
    fprintf(stderr, "[ERROR] Invalid --buffer_size=%s\n", val.c_str());
    return false;
  }
  return true;
}

bool BuildPowerOfTwoBlockRange(uint64_t start, uint64_t end, std::vector<uint64_t> &out) {
  if (start == 0U || end == 0U || start > end) {
    return false;
  }
  uint64_t cur = start;
  while (cur <= end) {
    out.push_back(cur);
    if (cur == end) {
      return true;
    }
    if (cur > std::numeric_limits<uint64_t>::max() / 2U) {
      return false;
    }
    cur *= 2U;
    if (cur > end) {
      return false;
    }
  }
  return true;
}

bool ApplyBlockSizesKv(const std::string &val, BenchmarkConfig *cfg) {
  cfg->block_sizes.clear();
  const std::vector<std::string> entries = SplitCommaList(val);
  if (entries.empty()) {
    fprintf(stderr, "[ERROR] Invalid --block_sizes=%s\n", val.c_str());
    return false;
  }
  for (const std::string &entry : entries) {
    const auto sep = entry.find(':');
    if (sep == std::string::npos) {
      uint64_t block = 0;
      if (!ParseByteSize(entry, block)) {
        fprintf(stderr, "[ERROR] Invalid --block_sizes segment \"%s\"\n", entry.c_str());
        return false;
      }
      cfg->block_sizes.push_back(block);
      continue;
    }
    if (entry.find(':', sep + 1U) != std::string::npos) {
      fprintf(stderr, "[ERROR] Invalid --block_sizes range \"%s\"\n", entry.c_str());
      return false;
    }
    uint64_t start = 0;
    uint64_t end = 0;
    if (!ParseByteSize(entry.substr(0, sep), start) || !ParseByteSize(entry.substr(sep + 1U), end) ||
        !BuildPowerOfTwoBlockRange(start, end, cfg->block_sizes)) {
      fprintf(stderr, "[ERROR] Invalid --block_sizes range \"%s\"\n", entry.c_str());
      return false;
    }
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

void PopulateCoreKvHandlers(std::map<std::string, KvApplyFn> *table) {
  auto &t = *table;
  t["--role"] = ApplyRoleKv;
  t["-r"] = ApplyRoleKv;
  t["--group"] = ApplyBenchmarkGroupKv;
  t["--output_dir"] = ApplyOutputDirKv;
  t["--device_id"] = ApplyDeviceIdKv;
  t["-d"] = ApplyDeviceIdKv;
  t["--local_engine"] = ApplyLocalEngineKv;
  t["-l"] = ApplyLocalEngineKv;
  t["--remote_engine"] = ApplyRemoteEngineKv;
  t["-e"] = ApplyRemoteEngineKv;
  t["--peer_wait_s"] = ApplyPeerWaitSecKv;
  t["-w"] = ApplyPeerWaitSecKv;
  t["--peer_count"] = ApplyPeerCountKv;
  t["-c"] = ApplyPeerCountKv;
  t["--memory"] = ApplyMemoryKv;
  t["--remote_memory"] = ApplyRemoteMemoryKv;
  t["--op"] = ApplyOpKv;
  t["-o"] = ApplyOpKv;
  t["--transport"] = ApplyTransportKv;
  t["--host_roce_ip"] = ApplyHostRoceIpKv;
}

void PopulateBenchKvHandlers(std::map<std::string, KvApplyFn> *table) {
  auto &t = *table;
  t["--transfer_size"] = ApplyTransferSizeKv;
  t["-t"] = ApplyTransferSizeKv;
  t["--buffer_size"] = ApplyBufferSizeKv;
  t["--block_sizes"] = ApplyBlockSizesKv;
  t["--loops"] = ApplyLoopsKv;
  t["-n"] = ApplyLoopsKv;
  t["--use_async"] = ApplyUseAsyncKv;
  t["-x"] = ApplyUseAsyncKv;
  t["--async_batch_num"] = ApplyAsyncBatchNumKv;
  t["-y"] = ApplyAsyncBatchNumKv;
  t["--connect_timeout_ms"] = ApplyConnectTimeoutKv;
  t["-C"] = ApplyConnectTimeoutKv;
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
      fprintf(stderr, "[ERROR] Invalid --hixl_option value %s (expect KEY=VALUE with KEY non-empty)\n",
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

std::string BenchmarkConfig::ComputeDirection(const std::string &initiator_mem, const std::string &target_mem,
                                              const std::string &op_type) {
  if (op_type == "mix") {
    return "mix:" + ComputeDirection(initiator_mem, target_mem, "write") + "/" +
           ComputeDirection(initiator_mem, target_mem, "read");
  }
  const bool wr = (op_type == "write");
  if (initiator_mem == "device" && target_mem == "device") return wr ? "D2rD" : "rD2D";
  if (initiator_mem == "device" && target_mem == "host") return wr ? "D2rH" : "rH2D";
  if (initiator_mem == "host" && target_mem == "host") return wr ? "H2rH" : "rH2H";
  if (initiator_mem == "host" && target_mem == "device") return wr ? "H2rD" : "rD2H";
  return "unknown";
}

void BenchmarkConfigParser::PrintUsage(FILE *out) {
  fprintf(
      out,
      "Usage: hixl_comm_bench key=value ...\n"
      "Required: --role=target|initiator\n"
      "Role-specific required keys:\n"
      "  target:    --memory=host|device\n"
      "  initiator: --memory=host|device --remote_memory=host|device --op=read|write|mix\n"
      "Keys:\n"
      "  --role|-r            target|initiator\n"
      "  --group              result grouping name (default default)\n"
      "  --transport          hccs|roce|fabric_mem|uboe|ubg|ub "
      "(hccs: D2D everywhere; extra H2rD|rD2H on A3-class SOC only; fabric_mem adds EnableUseFabricMem=1; "
      "hccs/roce/uboe/ubg add GlobalResourceConfig protocol_desc by default unless LocalCommRes is set; "
      "ub adds LocalCommRes with version:1.3, only on A5; "
      "roce: RDMA over Converged Ethernet, supported on A2, A3 and A5; on A5 uses HixlCS LocalCommRes with "
      "protocol:roce placement:host and requires --host_roce_ip)\n"
      "  --host_roce_ip       Host RoCE NIC IP address for LocalCommRes endpoint (data plane; required for "
      "transport=roce\n"
      " on A5 unless -H LocalCommRes is used; ignored on A2/A3). Note: this is separate from host IP used by\n"
      " --local_engine/--remote_engine for HIXL engine/control-plane coordination.\n"
      "  --memory             local buffer memory type: host|device\n"
      "  --remote_memory      initiator only: target buffer memory type host|device\n"
      "  --op|-o              initiator only: read|write|mix\n"
      "  --output_dir         CSV/JSONL result output directory (default output)\n"
      "  --device_id|-d       device id (int), or comma list e.g. 0,1 (broadcast if single value)\n"
      "  --local_engine|-l    local HIXL endpoint(s), comma-separated; IPv6 use [ip]:port\n"
      "  --remote_engine|-e   initiator: one or more target host:port (comma-separated); HIXL uses this endpoint\n"
      "                        directly, peer TCP coordination uses an internally derived port\n"
      "                        target: optional remote hint; multi-value not supported\n"
      "  --peer_wait_s|-w     target only: max wall time for peer connect phase in seconds (default 30)\n"
      "  --peer_count|-c      target only: initiator peers to wait for before proceed (default 1, max %" PRIu32
      ")\n"
      "  --transfer_size|-t   bytes transferred per block-size step (default %" PRIu64
      ")\n"
      "  --buffer_size        allocation/register size (default %" PRIu64
      ")\n"
      "  --block_sizes        block size list/range, e.g. 4K,64K,1M or 4K:1M; default equals transfer_size\n"
      "                        size units: K/M/G/T, KB/MB/GB/TB, KiB/MiB/GiB/TiB (binary 1024)\n"
      "  --loops|-n           repeat full step ladder (default %u)\n"
      "  --use_async|-x       true|false (default false), enable async transfer mode\n"
      "  --async_batch_num|-y async requests per batch (default 1), requires: transfer_size %% async_batch_num == 0\n"
      "  --connect_timeout_ms|-C connect timeout in ms (default 60000)\n"
      "  --hixl_option|-H     HIXL Initialize() option, form KEY=VALUE (repeatable); "
      "KEY e.g. LocalCommRes, BufferPool, RdmaTrafficClass, RdmaServiceLevel, adxl.*\n"
      "                       If neither BufferPool nor adxl.BufferPool is set, BufferPool=0:0 is added (benchmark "
      "default).\n"
      "Peer TCP coordination port is derived from the engine port internally (+10000 or -10000); no separate "
      "--tcp_port is required.\n"
      "Multi-endpoint: lengths must match max(n_d,n_l,n_r) after broadcasting single values; target allows only one.\n"
      "Initiator: one local + many remotes uses one HIXL and concurrent TCP/HIXL per remote; many locals use one "
      "thread "
      "per lane.\n"
      "Defaults: initiator device_id=0 local_engine=127.0.0.1:16000 remote_engine=127.0.0.1:16001\n"
      "          target device_id=1 local_engine=127.0.0.1:16001 remote_engine=127.0.0.1\n",
      kTcpClientCountMax, kDefaultTotalSize, kDefaultBufferSize, kDefaultLoops);
}

namespace {

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
      fprintf(stderr, "[WARN] ACL SOC probe failed (ret=%d); assuming A2-class HCCS rules\n", static_cast<int>(er));
      return BenchSocKind::kA2;
    }
  }
  er = aclrtSetDevice(device_id);
  if (er != ACL_ERROR_NONE) {
    fprintf(stderr, "[WARN] aclrtSetDevice(%d) failed for SOC probe (ret=%d); assuming A2-class HCCS rules\n",
            static_cast<int>(device_id), static_cast<int>(er));
    return BenchSocKind::kA2;
  }
  const char *soc_cstr = aclrtGetSocName();
  if (soc_cstr == nullptr || soc_cstr[0] == '\0') {
    fprintf(stderr, "[WARN] aclrtGetSocName() empty; assuming A2-class HCCS rules\n");
    return BenchSocKind::kA2;
  }
  const std::string soc(soc_cstr);
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

bool HasInitOption(const std::map<std::string, std::string> &options, const std::string &key) {
  return options.find(key) != options.cend();
}

bool HasExplicitLocalCommRes(const BenchmarkConfig &cfg) {
  constexpr const char kAdxlLocalCommResKey[] = "adxl.LocalCommRes";
  return HasInitOption(cfg.hixl_init_options, OPTION_LOCAL_COMM_RES) ||
         HasInitOption(cfg.hixl_init_options, kAdxlLocalCommResKey);
}

AscendString BuildProtocolDescConfig(const std::string &protocol_desc) {
  return AscendString(("{\"comm_resource_config.protocol_desc\":[\"" + protocol_desc + "\"]}").c_str());
}

void AddDefaultProtocolDesc(const BenchmarkConfig &cfg, std::map<AscendString, AscendString> *options) {
  if (HasExplicitLocalCommRes(cfg) || HasInitOption(cfg.hixl_init_options, OPTION_GLOBAL_RESOURCE_CONFIG)) {
    return;
  }
  std::string protocol_desc;
  if (cfg.transport == "hccs") {
    protocol_desc = "hccs:device";
  } else if (cfg.transport == "roce") {
    protocol_desc = "roce:" + cfg.roce_endpoint_placement;
  } else if (cfg.transport == "uboe") {
    protocol_desc = "uboe:device";
  } else if (cfg.transport == "ubg") {
    protocol_desc = "ubg:device";
  } else {
    return;
  }
  (*options)[AscendString(OPTION_GLOBAL_RESOURCE_CONFIG)] = BuildProtocolDescConfig(protocol_desc);
}

}  // namespace

std::map<AscendString, AscendString> BenchmarkConfigParser::BuildInitializeOptions(const BenchmarkConfig &cfg,
                                                                                   size_t lane_index) {
  std::map<AscendString, AscendString> options;
  for (const auto &entry : cfg.hixl_init_options) {
    options[AscendString(entry.first.c_str())] = AscendString(entry.second.c_str());
  }
  constexpr const char kAdxlBufferPoolKey[] = "adxl.BufferPool";
  const bool has_explicit_buffer = cfg.hixl_init_options.find(OPTION_BUFFER_POOL) != cfg.hixl_init_options.cend() ||
                                   cfg.hixl_init_options.find(kAdxlBufferPoolKey) != cfg.hixl_init_options.cend();
  if (!has_explicit_buffer) {
    options[AscendString(OPTION_BUFFER_POOL)] = AscendString("0:0");
  }
  if (cfg.transport == "fabric_mem" &&
      cfg.hixl_init_options.find(OPTION_ENABLE_USE_FABRIC_MEM) == cfg.hixl_init_options.cend()) {
    options[AscendString(OPTION_ENABLE_USE_FABRIC_MEM)] = AscendString("1");
  }
  AddDefaultProtocolDesc(cfg, &options);
  if (cfg.transport == "ub" && !HasExplicitLocalCommRes(cfg)) {
    options[AscendString(OPTION_LOCAL_COMM_RES)] = AscendString("{\"version\":\"1.3\"}");
  }
  if (cfg.transport == "roce") {
    const BenchSocKind soc_kind = ResolveSocKindForHccs(&cfg);
    if (soc_kind == BenchSocKind::kA5) {
      const std::string &lane_roce_ip =
          (lane_index < cfg.expanded_host_roce_ips.size()) ? cfg.expanded_host_roce_ips[lane_index] : cfg.host_roce_ip;
      if (!HasExplicitLocalCommRes(cfg)) {
        std::string local_comm_res =
            "{\"version\":\"1.3\",\"net_instance_id\":\"default\",\"endpoint_list\":["
            "{\"protocol\":\"roce\",\"comm_id\":\"" +
            lane_roce_ip + "\",\"placement\":\"host\"}]}";
        options[AscendString(OPTION_LOCAL_COMM_RES)] = AscendString(local_comm_res.c_str());
      }
    }
  }
  return options;
}

bool BenchmarkConfigParser::ApplyTransportEnvironment(const BenchmarkConfig &cfg) {
  if (cfg.transport == "roce") {
    if (setenv("HCCL_INTRA_ROCE_ENABLE", "1", 1) != 0) {
      std::fprintf(stderr, "[ERROR] setenv HCCL_INTRA_ROCE_ENABLE=1 failed: %s\n", std::strerror(errno));
      return false;
    }
    return true;
  }
  // hccs / fabric_mem / uboe / ubg / ub: do not modify HCCL environment variables (fabric_mem uses init options only)
  return true;
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
    fprintf(stderr, "[ERROR] Missing required --role=target|initiator\n");
    BenchmarkConfigParser::PrintUsage(stderr);
    return false;
  }
  const std::string &rv = it_role->second;
  if (rv == "initiator") {
    cfg->role = BenchmarkRole::kClient;
  } else if (rv == "target") {
    cfg->role = BenchmarkRole::kServer;
  } else {
    fprintf(stderr, "[ERROR] Invalid --role=%s (expect target|initiator)\n", rv.c_str());
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
  if (cfg->block_sizes.empty()) {
    cfg->block_sizes.push_back(cfg->transfer_size);
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

bool ValidateListLengths(size_t nd, size_t nl, size_t nr, size_t n_max) {
  auto invalid_len = [](size_t n, size_t cap) { return n != 1U && n != cap; };
  if (invalid_len(nd, n_max) || invalid_len(nl, n_max) || invalid_len(nr, n_max)) {
    fprintf(stderr, "[ERROR] device_id/local_engine/remote_engine list lengths (%zu,%zu,%zu) must each be 1 or %zu\n",
            nd, nl, nr, n_max);
    return false;
  }
  return true;
}

bool ValidateMultiDeviceRequirement(size_t n_max, size_t nl, size_t nr) {
  if (n_max > 1U && nl == 1U && nr == 1U) {
    fprintf(stderr, "[ERROR] multiple device_id values require multiple local_engine or remote_engine entries\n");
    return false;
  }
  return true;
}

void ExpandConfigLists(BenchmarkConfig *cfg, size_t n_max, size_t nd, size_t nl, size_t nr) {
  cfg->expanded_device_ids.clear();
  cfg->expanded_local_engines.clear();
  cfg->expanded_remote_engines.clear();
  cfg->expanded_device_ids.reserve(n_max);
  cfg->expanded_local_engines.reserve(n_max);
  cfg->expanded_remote_engines.reserve(n_max);
  for (size_t i = 0; i < n_max; ++i) {
    cfg->expanded_device_ids.push_back(cfg->device_id_list[nd == 1U ? 0 : i]);
    cfg->expanded_local_engines.push_back(cfg->local_engine_list[nl == 1U ? 0 : i]);
    cfg->expanded_remote_engines.push_back(cfg->remote_engine_list[nr == 1U ? 0 : i]);
  }
  cfg->device_id = cfg->expanded_device_ids[0];
  if (!cfg->host_roce_ip_list.empty()) {
    const size_t nr_ip = cfg->host_roce_ip_list.size();
    cfg->expanded_host_roce_ips.clear();
    cfg->expanded_host_roce_ips.reserve(n_max);
    for (size_t i = 0; i < n_max; ++i) {
      cfg->expanded_host_roce_ips.push_back(cfg->host_roce_ip_list[nr_ip == 1U ? 0 : i]);
    }
    cfg->host_roce_ip = cfg->expanded_host_roce_ips[0];
  } else if (!cfg->host_roce_ip.empty()) {
    cfg->expanded_host_roce_ips.clear();
    cfg->expanded_host_roce_ips.push_back(cfg->host_roce_ip);
    cfg->host_roce_ip_list.push_back(cfg->host_roce_ip);
  }
}

bool ValidateClientRemoteEngines(const BenchmarkConfig *cfg) {
  for (size_t i = 0; i < cfg->expanded_remote_engines.size(); ++i) {
    const std::string &re = cfg->expanded_remote_engines[i];
    std::string host;
    uint16_t port = 0;
    if (!ExtractEndpointHostAndPort(re, host, port)) {
      fprintf(stderr, "[ERROR] initiator remote_engine[%zu] must be host:port (e.g. 127.0.0.1:16001), got \"%s\"\n", i,
              re.c_str());
      return false;
    }
  }
  return true;
}

bool ValidateServerConfig(const BenchmarkConfig *cfg, size_t n_max) {
  if (n_max > 1U) {
    fprintf(stderr, "[ERROR] target does not support multiple device_id/local_engine/remote_engine; use one each\n");
    return false;
  }
  std::string host;
  uint16_t port = 0;
  if (!ExtractEndpointHostAndPort(cfg->expanded_local_engines[0], host, port)) {
    fprintf(stderr, "[ERROR] target local_engine must be host:port\n");
    return false;
  }
  if (cfg->peer_wait_sec == 0U || cfg->peer_wait_sec > kMaxAcceptWaitSec) {
    fprintf(stderr, "[ERROR] target peer_wait_s out of range\n");
    return false;
  }
  if (cfg->peer_count == 0U || cfg->peer_count > kTcpClientCountMax) {
    fprintf(stderr, "[ERROR] target peer_count out of range\n");
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
    fprintf(stderr, "[ERROR] Invalid op: %s\n", op.c_str());
    return false;
  }
  return true;
}

bool ValidateTransport(const std::string &transport) {
  if (transport != "hccs" && transport != "roce" && transport != "fabric_mem" && transport != "uboe" &&
      transport != "ubg" && transport != "ub") {
    fprintf(stderr, "[ERROR] Invalid transport: %s (expect hccs|roce|fabric_mem|uboe|ubg|ub)\n", transport.c_str());
    return false;
  }
  return true;
}

bool ValidateHccsMemoryCombination(const BenchmarkConfig *cfg) {
  if (cfg->transport != "hccs") {
    return true;
  }
  const BenchSocKind kind = ResolveSocKindForHccs(cfg);
  if (kind == BenchSocKind::kA5) {
    fprintf(stderr, "[ERROR] transport=hccs is not supported on Ascend950-class (A5) SOC; use roce or fabric_mem\n");
    return false;
  }
  const std::string &im = cfg->initiator_memory_type;
  const std::string &tm = cfg->target_memory_type;
  if (im == "device" && tm == "device") {
    return true;
  }
  if (cfg->op == "mix") {
    fprintf(stderr,
            "[ERROR] HCCS transport does not support op=mix unless initiator_memory=device and "
            "target_memory=device\n");
    return false;
  }
  if (im == "host" && tm == "device") {
    if (kind == BenchSocKind::kA3 && (cfg->op == "read" || cfg->op == "write")) {
      return true;
    }
    fprintf(stderr,
            "[ERROR] HCCS host→device directions (H2rD/rD2H) require Ascend910-class SOC "
            "got initiator_memory=%s target_memory=%s op=%s\n",
            im.c_str(), tm.c_str(), cfg->op.c_str());
    return false;
  }
  fprintf(stderr,
          "[ERROR] HCCS: A2 allows D2D only; A3 adds H2rD|rD2H on host→device; "
          "got initiator_memory=%s target_memory=%s (use roce or fabric_mem for other directions)\n",
          im.c_str(), tm.c_str());
  return false;
}

bool ValidateBufferSize(const BenchmarkConfig *cfg) {
  if (cfg->buffer_size < cfg->transfer_size) {
    fprintf(stderr, "[ERROR] buffer_size (%" PRIu64 ") must be >= transfer_size (%" PRIu64 ")\n", cfg->buffer_size,
            cfg->transfer_size);
    return false;
  }
  return true;
}

bool ValidateBlockSizes(const BenchmarkConfig *cfg) {
  if (cfg->block_sizes.empty()) {
    fprintf(stderr, "[ERROR] block_sizes is empty\n");
    return false;
  }
  for (size_t i = 0; i < cfg->block_sizes.size(); ++i) {
    const uint64_t block = cfg->block_sizes[i];
    if (block == 0U) {
      fprintf(stderr, "[ERROR] computed block size is 0\n");
      return false;
    }
    if (block > cfg->transfer_size) {
      fprintf(stderr, "[ERROR] block size (%" PRIu64 ") at step %zu must be <= transfer_size (%" PRIu64 ")\n", block, i,
              cfg->transfer_size);
      return false;
    }
    if (cfg->transfer_size % block != 0) {
      fprintf(stderr, "[ERROR] transfer_size (%" PRIu64 ") must be divisible by block size (%" PRIu64 ") at step %zu\n",
              cfg->transfer_size, block, i);
      return false;
    }
  }
  return true;
}

bool ValidateAsyncConfig(const BenchmarkConfig *cfg) {
  if (!cfg->use_async) {
    return true;
  }
  if (cfg->transfer_size % cfg->async_batch_num != 0) {
    fprintf(stderr,
            "[ERROR] transfer_size (%" PRIu64 ") must be divisible by async_batch_num (%" PRIu32
            ") "
            "when use_async=true\n",
            cfg->transfer_size, cfg->async_batch_num);
    return false;
  }
  const uint64_t per_req_size = cfg->transfer_size / cfg->async_batch_num;
  for (size_t i = 0; i < cfg->block_sizes.size(); ++i) {
    const uint64_t block = cfg->block_sizes[i];
    if (per_req_size % block != 0) {
      fprintf(stderr,
              "[ERROR] per-request size (%" PRIu64 ") must be divisible by block size (%" PRIu64
              ") "
              "at step %zu when use_async=true\n",
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
  if (!ValidateListLengths(nd, nl, nr, n_max)) {
    return false;
  }
  if (!ValidateMultiDeviceRequirement(n_max, nl, nr)) {
    return false;
  }
  if (cfg->host_roce_ip_list.size() > 1U && cfg->host_roce_ip_list.size() != n_max) {
    fprintf(stderr,
            "[ERROR] --host_roce_ip comma-list length (%zu) must be 1 or %zu (same rule as multi local_engine / "
            "remote_engine)\n",
            cfg->host_roce_ip_list.size(), n_max);
    return false;
  }
  ExpandConfigLists(cfg, n_max, nd, nl, nr);
  if (cfg->role == BenchmarkRole::kClient && !ValidateClientRemoteEngines(cfg)) {
    return false;
  }
  if (cfg->role == BenchmarkRole::kServer && !ValidateServerConfig(cfg, n_max)) {
    return false;
  }
  return true;
}

bool ValidateRoleRequiredOptions(const BenchmarkConfig *cfg) {
  if (!cfg->memory_explicit) {
    fprintf(stderr, "[ERROR] --memory=host|device is required\n");
    return false;
  }
  if (cfg->role == BenchmarkRole::kServer) {
    if (cfg->remote_memory_explicit || cfg->op_explicit) {
      fprintf(stderr, "[ERROR] target role only accepts --memory; --remote_memory/--op are initiator-only\n");
      return false;
    }
    return true;
  }
  if (!cfg->remote_memory_explicit || !cfg->op_explicit) {
    fprintf(stderr, "[ERROR] initiator requires --memory, --remote_memory and --op\n");
    return false;
  }
  return true;
}

bool ValidateRoceConfig(BenchmarkConfig *cfg) {
  const BenchSocKind soc_kind = ResolveSocKindForHccs(cfg);
  cfg->roce_endpoint_placement = (soc_kind == BenchSocKind::kA5) ? "host" : "device";
  if (soc_kind == BenchSocKind::kA5) {
    if (cfg->hixl_init_options.find("LocalCommRes") == cfg->hixl_init_options.cend() && cfg->host_roce_ip.empty()) {
      fprintf(stderr, "[ERROR] transport=roce on A5 requires --host_roce_ip=<ROCE_NIC_IP> or -H LocalCommRes=...\n");
      return false;
    }
  } else if (soc_kind != BenchSocKind::kA3 && soc_kind != BenchSocKind::kA2) {
    fprintf(stderr, "[ERROR] transport=roce is only supported on A2, A3 and A5 platforms\n");
    return false;
  }
  return true;
}

bool ValidateBenchmarkWorkload(BenchmarkConfig *cfg) {
  if (cfg->soc_variant == "auto") {
    const BenchSocKind kind = ResolveSocKindForHccs(cfg);
    switch (kind) {
      case BenchSocKind::kA2:
        cfg->soc_variant = "a2";
        break;
      case BenchSocKind::kA3:
        cfg->soc_variant = "a3";
        break;
      case BenchSocKind::kA5:
        cfg->soc_variant = "a5";
        break;
    }
  }
  if (!ValidateRoleRequiredOptions(cfg) || !ValidateTransferOp(cfg->op)) {
    return false;
  }
  if (!ValidateTransport(cfg->transport) || !ValidateMemoryTypeValue(cfg->initiator_memory_type) ||
      !ValidateMemoryTypeValue(cfg->target_memory_type)) {
    return false;
  }
  if (!ValidateHccsMemoryCombination(cfg)) {
    return false;
  }
  if (cfg->transport == "roce" && !ValidateRoceConfig(cfg)) {
    return false;
  }
  if (!ValidateBufferSize(cfg)) {
    return false;
  }
  if (!ValidateBlockSizes(cfg)) {
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
  for (size_t i = 0; i < n; ++i) {
    const char *roce_ip_str = (i < cfg.expanded_host_roce_ips.size()) ? cfg.expanded_host_roce_ips[i].c_str() : "";
    std::string peer_host;
    uint16_t peer_engine_port = 0;
    const std::string &peer_engine =
        (cfg.role == BenchmarkRole::kServer) ? cfg.expanded_local_engines[i] : cfg.expanded_remote_engines[i];
    const bool has_peer_coord_port = ExtractEndpointHostAndPort(peer_engine, peer_host, peer_engine_port);
    if (cfg.transport == "roce" && roce_ip_str[0] != '\0') {
      fprintf(out, "[INFO]   [%zu] device_id=%d local_engine=%s remote_engine=%s host_roce_ip=%s", i,
              static_cast<int>(cfg.expanded_device_ids[i]), cfg.expanded_local_engines[i].c_str(),
              cfg.expanded_remote_engines[i].c_str(), roce_ip_str);
    } else {
      fprintf(out, "[INFO]   [%zu] device_id=%d local_engine=%s remote_engine=%s", i,
              static_cast<int>(cfg.expanded_device_ids[i]), cfg.expanded_local_engines[i].c_str(),
              cfg.expanded_remote_engines[i].c_str());
    }
    if (has_peer_coord_port) {
      fprintf(out, " peer_coord_port=%u", static_cast<unsigned>(DerivePeerCoordPort(peer_engine_port)));
    }
    fprintf(out, "\n");
  }
}

}  // namespace hixl_benchmark
