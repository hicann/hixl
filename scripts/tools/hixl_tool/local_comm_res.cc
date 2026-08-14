/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

/**
 * @file local_comm_res.cc
 * @brief hixl_tool local_comm_res 子命令实现
 *
 * 生成逻辑统一走 EndpointGenerator::AutoGenEndpointList（按 SoC 类型分发，A5 缺省 topo 逻辑在内）。
 * 本文件仅负责 CLI 解析、批量 device、生成 LocalCommRes、写 JSON 与日志。
 */

#include "local_comm_res.h"
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>
#include "acl/acl.h"
#include "endpoint_generator/endpoint_generator.h"
#include "endpoint_generator/local_comm_res_generator_v1.h"
#include "engine/hixl_options.h"

namespace hixl_tool {

namespace {

constexpr const char *kDefaultOutputDir = "/etc/";
constexpr const char *kDefaultFileNamePrefix = "local_comm_res";
constexpr char kProtocolDescSep = ',';

void PrintLocalCommResUsage() {
  std::printf("Usage: hixl_tool local_comm_res [options]\n\n");
  std::printf("Options:\n");
  std::printf("  --topo_file_path <path>       Hardware topology JSON file path\n");
  std::printf("                                (optional; default: use auto-detected topo)\n");
  std::printf("  --device_id <id1,id2,...>     Target user device IDs (default: auto-detect all)\n");
  std::printf("  --protocol_desc <desc>        Protocol descriptor, e.g. \"ub_rtp:device,ub_ctp:device\"\n");
  std::printf("                                Multiple values separated by comma.\n");
  std::printf("                                (default: ScaleOut by InterconType + ub_ctp:device)\n");
  std::printf("  --output <dir>                Output directory (default: %s)\n", kDefaultOutputDir);
  std::printf("  --file_name_prefix <prefix>   Output file name prefix (default: %s)\n", kDefaultFileNamePrefix);
  std::printf("\nOutput: {output}/{prefix}_{device_id}_{phy_id}.json (one per device)\n");
  std::printf("\nNote: generation uses EndpointGenerator::AutoGenEndpointList (SoC-dispatch entry).\n");
  std::printf("      host_route.json is not required; ub_ctp host edges need ub_ctp:host in protocol_desc.\n");
}

struct LocalCommResArgs {
  std::string topo_file_path;
  std::string output_dir = kDefaultOutputDir;
  std::string file_name_prefix = kDefaultFileNamePrefix;
  std::string protocol_desc;
  std::vector<int32_t> device_ids;
  bool device_id_specified = false;
};

std::string TrimToken(const std::string &s) {
  size_t begin = 0;
  while (begin < s.size() && (s[begin] == ' ' || s[begin] == '\t')) {
    ++begin;
  }
  size_t end = s.size();
  while (end > begin && (s[end - 1] == ' ' || s[end - 1] == '\t')) {
    --end;
  }
  return s.substr(begin, end - begin);
}

std::vector<std::string> SplitProtocolDesc(const std::string &protocol_desc) {
  std::vector<std::string> tokens;
  if (protocol_desc.empty()) {
    return tokens;
  }
  std::istringstream iss(protocol_desc);
  std::string token;
  while (std::getline(iss, token, kProtocolDescSep)) {
    token = TrimToken(token);
    if (!token.empty()) {
      tokens.push_back(token);
    }
  }
  return tokens;
}

// 解析 --device_id 的逗号分隔数字列表；失败返回 false
bool ParseDeviceIdArg(const std::string &id_str, LocalCommResArgs &args) {
  args.device_id_specified = true;
  std::istringstream iss(id_str);
  std::string token;
  while (std::getline(iss, token, ',')) {
    token = TrimToken(token);
    if (token.empty()) {
      std::fprintf(stderr, "[ERROR] Empty device_id in list\n");
      return false;
    }
    size_t parsed = 0;
    try {
      const int32_t device_id = std::stoi(token, &parsed);
      if (parsed != token.size() || device_id < 0) {
        std::fprintf(stderr, "[ERROR] Invalid device_id: %s\n", token.c_str());
        return false;
      }
      args.device_ids.push_back(device_id);
    } catch (const std::exception &) {
      std::fprintf(stderr, "[ERROR] Invalid device_id: %s\n", token.c_str());
      return false;
    }
  }
  return true;
}

bool ParseLocalCommResArgs(int argc, char *argv[], LocalCommResArgs &args) {
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      PrintLocalCommResUsage();
      return false;
    }
    if (i + 1 >= argc) {
      std::fprintf(stderr, "[ERROR] Missing value for option: %s\n", arg.c_str());
      return false;
    }
    if (arg == "--topo_file_path") {
      args.topo_file_path = argv[++i];
    } else if (arg == "--device_id") {
      if (!ParseDeviceIdArg(argv[++i], args)) {
        return false;
      }
    } else if (arg == "--protocol_desc") {
      args.protocol_desc = argv[++i];
    } else if (arg == "--host_route_file_path") {
      ++i;
      std::fprintf(stderr, "[WARN] --host_route_file_path is obsolete; route_data is auto-generated inside engine\n");
    } else if (arg == "--output") {
      args.output_dir = argv[++i];
    } else if (arg == "--file_name_prefix") {
      args.file_name_prefix = argv[++i];
    } else {
      std::fprintf(stderr, "[ERROR] Unknown option: %s\n", arg.c_str());
      PrintLocalCommResUsage();
      return false;
    }
  }
  return true;
}

int32_t EnumerateDeviceIds(std::vector<int32_t> &device_ids) {
  uint32_t device_count = 0;
  aclError acl_ret = aclrtGetDeviceCount(&device_count);
  if (acl_ret != ACL_SUCCESS || device_count == 0) {
    std::fprintf(stderr, "[ERROR] aclrtGetDeviceCount failed or no device, ret=%d\n", static_cast<int>(acl_ret));
    return -1;
  }
  for (uint32_t i = 0; i < device_count; ++i) {
    device_ids.push_back(static_cast<int32_t>(i));
  }
  return 0;
}

void PrintUsageHints(const std::string &output_dir, const std::string &prefix) {
  std::string dir = output_dir;
  if (!dir.empty() && dir.back() != '/') {
    dir += "/";
  }
  std::printf("\n[INFO] === Usage Guide ===\n");
  std::printf("[INFO] Generated files: %s%s_<device_id>_<phy_id>.json\n", dir.c_str(), prefix.c_str());
  std::printf("[INFO] For Mooncake: configure the generated JSON file path as the Mooncake Envs.\n");
  std::printf("[INFO] For direct HIXL API: pass the JSON content as the LocalCommRes option value.\n");
  std::printf("[INFO] Container mapping required: /usr/local/Ascend/driver/topo/950/\n");
}

bool WriteLocalCommResJson(const std::string &output_path, const hixl::LocalCommRes &local_comm_res) {
  std::string json_str;
  if (hixl::SerializeLocalCommResJson(local_comm_res, json_str) != hixl::SUCCESS) {
    std::fprintf(stderr, "[ERROR] Failed to serialize LocalCommRes to JSON\n");
    return false;
  }

  std::ofstream file(output_path, std::ios::out | std::ios::trunc);
  if (!file.is_open()) {
    std::fprintf(stderr, "[ERROR] Failed to open output file: %s\n", output_path.c_str());
    return false;
  }
  file << json_str;
  if (!file.good()) {
    std::fprintf(stderr, "[ERROR] Failed to write output file: %s\n", output_path.c_str());
    return false;
  }
  file.close();
  return true;
}

// 为指定 device 生成 LocalCommRes：构造 HixlOptions（protocol_desc）并走 AutoGenEndpointList
// 目标 device 依赖调用方已 aclrtSetDevice；device_id/phyid 由引擎内部经 aclrtGetDevice 获取
bool GenerateLocalCommResForDevice(int32_t device_id, const std::string &topo_path,
                                   const std::vector<std::string> &protocol_tokens,
                                   hixl::LocalCommRes &local_comm_res) {
  local_comm_res = hixl::LocalCommRes{};
  local_comm_res.version = "1.3";

  // 把 protocol_desc 拼进 GlobalResourceConfig 构造 HixlOptions（扁平 key 形式，见 HixlOptions::from_json）
  nlohmann::json config = nlohmann::json::object();
  if (!protocol_tokens.empty()) {
    config["comm_resource_config.protocol_desc"] = nlohmann::json::array();
    for (const auto &desc : protocol_tokens) {
      config["comm_resource_config.protocol_desc"].push_back(desc);
    }
  }
  std::map<hixl::AscendString, hixl::AscendString> options_map;
  options_map[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = hixl::AscendString(config.dump().c_str());
  hixl::HixlOptions options;
  if (hixl::HixlOptions::Parse(options_map, options) != hixl::SUCCESS) {
    std::fprintf(stderr, "[ERROR] HixlOptions::Parse failed for device_id=%d\n", device_id);
    return false;
  }

  std::vector<hixl::EndpointConfig> endpoint_list;
  // local_engine 传空占位（工具当前仅 A5 自动生成）
  const hixl::Status ret = hixl::EndpointGenerator::AutoGenEndpointList(options, "", endpoint_list, topo_path);
  if (ret != hixl::SUCCESS) {
    std::fprintf(stderr, "[ERROR] AutoGenEndpointList failed for device_id=%d, ret=%u\n", device_id,
                 static_cast<unsigned>(ret));
    return false;
  }
  if (endpoint_list.empty()) {
    std::fprintf(stderr, "[ERROR] endpoint_list is empty after AutoGenEndpointList, device_id=%d\n", device_id);
    return false;
  }

  // net_instance_id 从已生成端点回填
  for (const auto &ep : endpoint_list) {
    if (!ep.net_instance_id.empty()) {
      local_comm_res.net_instance_id = ep.net_instance_id;
      break;
    }
  }
  local_comm_res.endpoint_list = std::move(endpoint_list);
  return true;
}

// 为单个 device 生成 LocalCommRes 并写文件；失败返回 false
bool ProcessOneDevice(int32_t device_id, const LocalCommResArgs &args,
                      const std::vector<std::string> &protocol_tokens) {
  aclError acl_ret = aclrtSetDevice(device_id);
  if (acl_ret != ACL_SUCCESS) {
    std::fprintf(stderr, "[ERROR] aclrtSetDevice(%d) failed, ret=%d\n", device_id, static_cast<int>(acl_ret));
    return false;
  }

  int32_t phy_id = -1;
  acl_ret = aclrtGetPhyDevIdByUserDevId(device_id, &phy_id);
  if (acl_ret != ACL_SUCCESS) {
    std::fprintf(stderr, "[ERROR] aclrtGetPhyDevIdByUserDevId(%d) failed, ret=%d\n", device_id,
                 static_cast<int>(acl_ret));
    aclrtResetDevice(device_id);
    return false;
  }

  std::printf("[INFO] Generating LocalCommRes for device_id=%d (phy_id=%d)\n", device_id, phy_id);

  hixl::LocalCommRes local_comm_res;
  if (!GenerateLocalCommResForDevice(device_id, args.topo_file_path, protocol_tokens, local_comm_res)) {
    std::fprintf(stderr, "[ERROR] GenerateLocalCommResForDevice failed for device_id=%d\n", device_id);
    aclrtResetDevice(device_id);
    return false;
  }

  std::string output_path = args.output_dir;
  if (!output_path.empty() && output_path.back() != '/') {
    output_path += "/";
  }
  output_path += args.file_name_prefix + "_" + std::to_string(device_id) + "_" + std::to_string(phy_id) + ".json";

  if (!WriteLocalCommResJson(output_path, local_comm_res)) {
    aclrtResetDevice(device_id);
    return false;
  }
  std::printf("[INFO] Written: %s (%zu endpoints)\n", output_path.c_str(), local_comm_res.endpoint_list.size());
  aclrtResetDevice(device_id);
  return true;
}

}  // namespace

int RunLocalCommRes(int argc, char *argv[]) {
  LocalCommResArgs args;
  if (!ParseLocalCommResArgs(argc, argv, args)) {
    return 1;
  }

  const std::vector<std::string> protocol_tokens = SplitProtocolDesc(args.protocol_desc);
  if (args.protocol_desc.empty()) {
    std::printf("[INFO] protocol_desc not set: auto mode (ScaleOut by InterconType + ub_ctp:device)\n");
  } else {
    std::printf("[INFO] protocol_desc=%s (%zu token(s))\n", args.protocol_desc.c_str(), protocol_tokens.size());
  }
  if (args.topo_file_path.empty()) {
    std::printf("[INFO] topo path not set, use default topo (auto-detected)\n");
  } else {
    std::printf("[INFO] topo=%s\n", args.topo_file_path.c_str());
  }

  std::vector<int32_t> device_ids;
  if (args.device_id_specified) {
    device_ids = args.device_ids;
  } else {
    if (EnumerateDeviceIds(device_ids) != 0) {
      return 1;
    }
  }
  std::printf("[INFO] Processing %zu device(s)\n", device_ids.size());

  bool has_error = false;
  for (int32_t device_id : device_ids) {
    if (!ProcessOneDevice(device_id, args, protocol_tokens)) {
      has_error = true;
    }
  }

  PrintUsageHints(args.output_dir, args.file_name_prefix);
  return has_error ? 1 : 0;
}

}  // namespace hixl_tool
