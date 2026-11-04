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
 * @brief hixl_tool local_comm_res subcommand
 *
 * Generation goes through EndpointGenerator::AutoGenEndpointList (SoC dispatch; A5 default topo is inside).
 * This file only handles CLI parsing, batch devices, LocalCommRes generation, JSON write, and logs.
 */

#include "local_comm_res.h"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>
#include "acl/acl.h"
#include "aclrt_proxy.h"
#include "endpoint_generator/endpoint_generator.h"
#include "endpoint_generator/local_comm_res_generator_v1.h"
#include "engine/hixl_options.h"
#include "hixl_utils.h"

namespace hixl_tool {

namespace {

// Literal-backed string_views: data() is null-terminated, safe for printf("%s").
constexpr std::string_view kDefaultOutputDir = "/etc/";
constexpr std::string_view kDefaultFileNamePrefix = "local_comm_res";
constexpr char kProtocolDescSep = ',';

void PrintLocalCommResUsage() {
  std::printf("Usage: hixl_tool local_comm_res [options]\n\n");
  std::printf("Options:\n");
  std::printf("  --topo_file_path <path>       Hardware topology JSON file path\n");
  std::printf("                                (optional; default: use auto-detected topo)\n");
  std::printf("  --comm_resource_config <json|path>\n");
  std::printf("                                comm_resource_config fields as a JSON object or file.\n");
  std::printf("                                e.g. {\"nic_topo_path\":\"/path\"}.\n");
  std::printf("  --device_id <id1,id2,...>     Target user device IDs (default: auto-detect all)\n");
  std::printf("  --protocol_desc <desc>        Protocol descriptor, e.g. \"ub_rtp:device,ub_ctp\"\n");
  std::printf("                                Multiple values separated by comma.\n");
  std::printf("                                (default: ScaleOut by InterconType + ub_ctp:device)\n");
  std::printf("                                HCCL_INTRA_ROCE_ENABLE=1 generates roce:host only\n");
  std::printf("                                and ignores --protocol_desc.\n");
  std::printf("  --output <dir>                Output directory (default: %s)\n", kDefaultOutputDir.data());
  std::printf("  --file_name_prefix <prefix>   Output file name prefix (default: %s)\n", kDefaultFileNamePrefix.data());
  std::printf("\nOutput: {output}/{prefix}_{device_id}_{phy_id}.json (one per device)\n");
  std::printf("\nNote: generation uses EndpointGenerator::AutoGenEndpointList (SoC-dispatch entry).\n");
  std::printf("      host_route.json is not required; use ub_ctp or ub_ctp:host for Host URMA resources.\n");
}

struct LocalCommResArgs {
  nlohmann::json comm_resource_config = nlohmann::json::object();
  std::string topo_file_path;
  std::string output_dir{kDefaultOutputDir};
  std::string file_name_prefix{kDefaultFileNamePrefix};
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

bool LooksLikeJsonObject(const std::string &text) {
  size_t begin = 0;
  while (begin < text.size() && (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\n')) {
    ++begin;
  }
  return begin < text.size() && text[begin] == '{';
}

bool LoadCommResourceConfig(const std::string &arg, nlohmann::json &out) {
  std::string text = arg;
  if (!LooksLikeJsonObject(arg)) {
    std::ifstream ifs(arg);
    if (!ifs.is_open()) {
      std::printf("[ERROR] Failed to open comm_resource_config file: %s\n", arg.c_str());
      return false;
    }
    text.assign((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
  }
  try {
    out = nlohmann::json::parse(text);
  } catch (const nlohmann::json::exception &e) {
    std::printf("[ERROR] Invalid comm_resource_config JSON: %s\n", e.what());
    return false;
  }
  if (!out.is_object()) {
    std::printf("[ERROR] comm_resource_config must be a JSON object\n");
    return false;
  }
  return true;
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

constexpr const char kInnerProtocolDesc[] = "protocol_desc";
constexpr const char kCommResourceKeyPrefix[] = "comm_resource_config.";

bool ApplyCliProtocolDesc(LocalCommResArgs &args) {
  const std::vector<std::string> tokens = SplitProtocolDesc(args.protocol_desc);
  if (tokens.empty()) {
    return true;
  }
  if (args.comm_resource_config.contains(kInnerProtocolDesc) ||
      args.comm_resource_config.contains(hixl::kGlobalResCfgProtocolDesc)) {
    std::printf("[ERROR] Do not set protocol_desc in --comm_resource_config when --protocol_desc is used\n");
    return false;
  }
  args.comm_resource_config[kInnerProtocolDesc] = tokens;
  return true;
}

// CLI JSON uses keys inside comm_resource_config. Initialize still expects the full key.
nlohmann::json PrefixCommResourceKeys(const nlohmann::json &config) {
  nlohmann::json prefixed = nlohmann::json::object();
  for (auto it = config.begin(); it != config.end(); ++it) {
    const std::string &key = it.key();
    const bool already_prefixed =
        key.compare(0, std::char_traits<char>::length(kCommResourceKeyPrefix), kCommResourceKeyPrefix) == 0;
    const std::string full_key = already_prefixed ? key : std::string(kCommResourceKeyPrefix) + key;
    prefixed[full_key] = it.value();
  }
  return prefixed;
}

// topo_file_path and comm_resource_config are both fields of GlobalResourceConfig.
nlohmann::json BuildGlobalResourceConfig(const LocalCommResArgs &args) {
  nlohmann::json global_cfg = PrefixCommResourceKeys(args.comm_resource_config);
  if (!args.topo_file_path.empty()) {
    global_cfg[hixl::kGlobalResCfgTopoFilePath] = args.topo_file_path;
  }
  return global_cfg;
}

// Parse a comma-separated --device_id list; return false on failure.
bool ParseDeviceIdArg(const std::string &id_str, LocalCommResArgs &args) {
  args.device_id_specified = true;
  std::istringstream iss(id_str);
  std::string token;
  while (std::getline(iss, token, ',')) {
    token = TrimToken(token);
    if (token.empty()) {
      std::printf("[ERROR] Empty device_id in list\n");
      return false;
    }
    size_t parsed = 0;
    try {
      const int32_t device_id = std::stoi(token, &parsed);
      if (parsed != token.size() || device_id < 0) {
        std::printf("[ERROR] Invalid device_id: %s\n", token.c_str());
        return false;
      }
      args.device_ids.push_back(device_id);
    } catch (const std::exception &) {
      std::printf("[ERROR] Invalid device_id: %s\n", token.c_str());
      return false;
    }
  }
  return true;
}

bool ParseLocalCommResArgs(const std::vector<std::string> &args, LocalCommResArgs &out_args) {
  for (size_t i = 1; i < args.size(); ++i) {
    const std::string &arg = args[i];
    if (arg == "--help" || arg == "-h") {
      PrintLocalCommResUsage();
      return false;
    }
    if (i + 1 >= args.size()) {
      std::printf("[ERROR] Missing value for option: %s\n", arg.c_str());
      return false;
    }
    if (arg == "--topo_file_path") {
      out_args.topo_file_path = args[++i];
    } else if (arg == "--comm_resource_config") {
      if (!LoadCommResourceConfig(args[++i], out_args.comm_resource_config)) {
        return false;
      }
    } else if (arg == "--device_id") {
      if (!ParseDeviceIdArg(args[++i], out_args)) {
        return false;
      }
    } else if (arg == "--protocol_desc") {
      out_args.protocol_desc = args[++i];
    } else if (arg == "--host_route_file_path") {
      ++i;
      std::printf("[WARN] --host_route_file_path is obsolete; route_data is auto-generated inside engine\n");
    } else if (arg == "--output") {
      out_args.output_dir = args[++i];
    } else if (arg == "--file_name_prefix") {
      out_args.file_name_prefix = args[++i];
    } else {
      std::printf("[ERROR] Unknown option: %s\n", arg.c_str());
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
    std::printf("[ERROR] aclrtGetDeviceCount failed or no device, ret=%d\n", static_cast<int>(acl_ret));
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
    std::printf("[ERROR] Failed to serialize LocalCommRes to JSON\n");
    return false;
  }

  std::ofstream file(output_path, std::ios::out | std::ios::trunc);
  if (!file.is_open()) {
    std::printf("[ERROR] Failed to open output file: %s\n", output_path.c_str());
    return false;
  }
  file << json_str;
  if (!file.good()) {
    std::printf("[ERROR] Failed to write output file: %s\n", output_path.c_str());
    return false;
  }
  file.close();
  return true;
}

// Generate LocalCommRes for a device: parse one GlobalResourceConfig JSON into HixlOptions and call
// AutoGenEndpointList. Caller must have set the device with aclrtSetDevice; device_id/phyid are obtained via
// aclrtGetDevice inside the engine.
bool GenerateLocalCommResForDevice(int32_t device_id, const nlohmann::json &global_cfg,
                                   hixl::LocalCommRes &local_comm_res) {
  local_comm_res = hixl::LocalCommRes{};
  local_comm_res.version = "1.3";

  std::map<hixl::AscendString, hixl::AscendString> options_map;
  options_map[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = hixl::AscendString(global_cfg.dump().c_str());
  hixl::HixlOptions options;
  if (hixl::HixlOptions::Parse(options_map, options) != hixl::SUCCESS) {
    std::printf("[ERROR] HixlOptions::Parse failed for device_id=%d\n", device_id);
    return false;
  }

  std::vector<hixl::EndpointConfig> endpoint_list;
  // local_engine is an empty placeholder; topo_file_path is carried by options.
  const hixl::Status ret = hixl::EndpointGenerator::AutoGenEndpointList(options, "", endpoint_list);
  if (ret != hixl::SUCCESS) {
    std::printf("[ERROR] AutoGenEndpointList failed for device_id=%d, ret=%u\n", device_id, static_cast<unsigned>(ret));
    return false;
  }
  if (endpoint_list.empty()) {
    std::printf("[ERROR] endpoint_list is empty after AutoGenEndpointList, device_id=%d\n", device_id);
    return false;
  }

  // Backfill net_instance_id / server_id from generated endpoints (JSON keeps them at top level).
  for (const auto &ep : endpoint_list) {
    if (local_comm_res.net_instance_id.empty() && !ep.net_instance_id.empty()) {
      local_comm_res.net_instance_id = ep.net_instance_id;
    }
    if (local_comm_res.server_id.empty() && !ep.server_id.empty()) {
      local_comm_res.server_id = ep.server_id;
    }
  }
  local_comm_res.endpoint_list = std::move(endpoint_list);
  return true;
}

// Generate LocalCommRes for one device and write the file; return false on failure.
bool ProcessOneDevice(int32_t device_id, const LocalCommResArgs &args) {
  aclError acl_ret = aclrtSetDevice(device_id);
  if (acl_ret != ACL_SUCCESS) {
    std::printf("[ERROR] aclrtSetDevice(%d) failed, ret=%d\n", device_id, static_cast<int>(acl_ret));
    return false;
  }

  int32_t phy_id = -1;
  acl_ret = hixl::AclrtProxy::GetPhyDevIdByUserDevId(device_id, &phy_id);
  if (acl_ret != ACL_SUCCESS) {
    std::printf("[ERROR] AclrtProxy::GetPhyDevIdByUserDevId(%d) failed, ret=%d\n", device_id,
                static_cast<int>(acl_ret));
    aclrtResetDevice(device_id);
    return false;
  }

  std::printf("[INFO] Generating LocalCommRes for device_id=%d (phy_id=%d)\n", device_id, phy_id);

  hixl::LocalCommRes local_comm_res;
  const nlohmann::json global_cfg = BuildGlobalResourceConfig(args);
  if (!GenerateLocalCommResForDevice(device_id, global_cfg, local_comm_res)) {
    std::printf("[ERROR] GenerateLocalCommResForDevice failed for device_id=%d\n", device_id);
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

int RunLocalCommRes(const std::vector<std::string> &args) {
  LocalCommResArgs res_args;
  if (!ParseLocalCommResArgs(args, res_args)) {
    return 1;
  }
  if (!ApplyCliProtocolDesc(res_args)) {
    return 1;
  }

  if (hixl::IsIntraRoceEnabled()) {
    std::printf("[INFO] HCCL_INTRA_ROCE_ENABLE=1: generate roce:host LocalCommRes only\n");
  } else if (res_args.protocol_desc.empty()) {
    std::printf("[INFO] protocol_desc not set: auto mode (ScaleOut by InterconType + ub_ctp:device)\n");
  } else {
    std::printf("[INFO] protocol_desc=%s\n", res_args.protocol_desc.c_str());
  }
  if (res_args.topo_file_path.empty()) {
    std::printf("[INFO] topo path not set, use default topo (auto-detected)\n");
  } else {
    std::printf("[INFO] topo=%s\n", res_args.topo_file_path.c_str());
  }
  if (!res_args.comm_resource_config.empty()) {
    const std::string dumped = res_args.comm_resource_config.dump();
    std::printf("[INFO] comm_resource_config=%s\n", dumped.c_str());
  }

  std::vector<int32_t> device_ids;
  if (res_args.device_id_specified) {
    device_ids = res_args.device_ids;
  } else {
    if (EnumerateDeviceIds(device_ids) != 0) {
      return 1;
    }
  }
  std::printf("[INFO] Processing %zu device(s)\n", device_ids.size());

  bool has_error = false;
  for (int32_t device_id : device_ids) {
    if (!ProcessOneDevice(device_id, res_args)) {
      has_error = true;
    }
  }

  PrintUsageHints(res_args.output_dir, res_args.file_name_prefix);
  return has_error ? 1 : 0;
}

}  // namespace hixl_tool
