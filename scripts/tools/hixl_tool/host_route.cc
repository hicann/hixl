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
 * @file host_route.cc
 * @brief hixl_tool host_route subcommand
 *
 * New flow (no procfs):
 * 1. Enumerate all local NPUs (aclrtGetDeviceCount + aclrtGetPhyDevIdByUserDevId)
 * 2. Get product form (is_server) and call GenerateRouteDataViaDsmi
 *    (topo_path may be empty; empty means default topo is resolved inside the API)
 * 3. Write host_route.json (same as RouteEntry: local_eid=Host/CPU, remote_eid=NPU)
 *
 * Note: local_comm_res no longer depends on this artifact; this subcommand is an optional offline EID inventory export.
 */

#include "host_route.h"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <set>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#include "acl/acl.h"
#include "endpoint_generator/local_comm_res_generator_v1.h"
#include "endpoint_generator/route_conf_generator.h"

namespace hixl_tool {

namespace {

// Literal-backed string_views: data() is null-terminated, safe for printf("%s").
constexpr std::string_view kDefaultOutputDir = "/etc/";
constexpr std::string_view kDefaultFileName = "host_route.json";

void PrintHostRouteUsage() {
  std::printf("Usage: hixl_tool host_route [--output <dir>] [--topo_file_path <path>]\n\n");
  std::printf("Options:\n");
  std::printf("  --output <dir>        Output directory (default: %s)\n", kDefaultOutputDir.data());
  std::printf("  --topo_file_path      Topology JSON file path (default: resolved by mainboard_id)\n");
  std::printf("\nOutput: {output}/%s\n", kDefaultFileName.data());
  std::printf("\nNote: route_data is generated via DSMI + urma_admin + DCMI (no procfs);\n");
  std::printf("      die is parsed from the topo file.\n");
}

struct HostRouteArgs {
  std::string output_dir{kDefaultOutputDir};
  std::string topo_file_path;
};

bool ParseHostRouteArgs(const std::vector<std::string> &args, HostRouteArgs &out_args) {
  for (size_t i = 1; i < args.size(); ++i) {
    const std::string &arg = args[i];
    if (arg == "--help" || arg == "-h") {
      PrintHostRouteUsage();
      return false;
    }
    if (arg == "--output" && i + 1 < args.size()) {
      out_args.output_dir = args[++i];
    } else if (arg == "--topo_file_path" && i + 1 < args.size()) {
      out_args.topo_file_path = args[++i];
    } else {
      std::printf("[ERROR] Unknown or incomplete option: %s\n", arg.c_str());
      PrintHostRouteUsage();
      return false;
    }
  }
  return true;
}

int32_t EnumerateAllNpuIds(std::vector<int32_t> &npu_ids) {
  // ID enumeration/mapping only; aclInit is not required.
  uint32_t device_count = 0;
  aclError acl_ret = aclrtGetDeviceCount(&device_count);
  if (acl_ret != ACL_SUCCESS || device_count == 0) {
    std::printf("[ERROR] aclrtGetDeviceCount failed or no device, ret=%d\n", static_cast<int>(acl_ret));
    return -1;
  }

  std::set<int32_t> unique_ids;
  for (uint32_t i = 0; i < device_count; ++i) {
    int32_t phy_id = -1;
    acl_ret = aclrtGetPhyDevIdByUserDevId(static_cast<int32_t>(i), &phy_id);
    if (acl_ret != ACL_SUCCESS) {
      std::printf("[WARN] aclrtGetPhyDevIdByUserDevId(%u) failed, ret=%d, skipping\n", i, static_cast<int>(acl_ret));
      continue;
    }
    unique_ids.insert(phy_id);
  }

  npu_ids.assign(unique_ids.begin(), unique_ids.end());
  std::printf("[INFO] Found %zu NPU(s)\n", npu_ids.size());
  return npu_ids.empty() ? -1 : 0;
}

// Generate route_data per group and merge into host_route_data; non-zero on failure.
int32_t GenerateHostRouteData(const std::vector<int32_t> &npu_ids, const std::string &topo_path, bool is_server,
                              hixl::HostRouteData &host_route_data) {
  std::set<int32_t> covered_npu_ids;
  for (int32_t phy_id : npu_ids) {
    if (covered_npu_ids.count(phy_id) > 0) {
      continue;
    }
    hixl::RouteGenResult route_gen;
    if (hixl::GenerateRouteDataViaDsmi(phy_id, topo_path, is_server, route_gen) != hixl::SUCCESS) {
      std::printf("[ERROR] GenerateRouteDataViaDsmi failed for phy_id=%d\n", phy_id);
      return 1;
    }
    for (int32_t id : route_gen.related_npu_ids) {
      covered_npu_ids.insert(id);
    }
    for (const auto &entry : route_gen.route_data.entries) {
      hixl::HostRouteEntry hr_entry;
      hr_entry.device_id = entry.device_id;
      // Same as RouteEntry / auto-generated localcommres: local=Host (CPU 2-port PG), remote=NPU.
      hr_entry.local_eid = entry.local_eid;
      hr_entry.remote_eid = entry.remote_eid;
      host_route_data.devices.push_back(hr_entry);
      std::printf("[INFO] device_id=%d, local_eid(host)=%s, remote_eid(npu)=%s\n", entry.device_id,
                  hr_entry.local_eid.c_str(), hr_entry.remote_eid.c_str());
    }
  }
  return host_route_data.devices.empty() ? 1 : 0;
}

// Write host_route.json (mode 0600); non-zero on failure.
int32_t WriteHostRouteOutput(const HostRouteArgs &args, const hixl::HostRouteData &host_route_data) {
  std::string json_str;
  if (hixl::RouteConfGenerator::SerializeHostRouteJson(host_route_data, json_str) != hixl::SUCCESS) {
    std::printf("[ERROR] Failed to serialize host_route data to JSON\n");
    return 1;
  }

  std::string output_path = args.output_dir;
  if (!output_path.empty() && output_path.back() != '/') {
    output_path += "/";
  }
  output_path += kDefaultFileName;

  constexpr mode_t kFileMode = S_IRUSR | S_IWUSR;  // 0600
  int fd = open(output_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, kFileMode);
  if (fd < 0) {
    const std::string err_msg = strerror(errno);
    std::printf("[ERROR] Failed to open %s, errno=%d(%s)\n", output_path.c_str(), errno, err_msg.c_str());
    return 1;
  }
  ssize_t written = write(fd, json_str.c_str(), json_str.size());
  if (written != static_cast<ssize_t>(json_str.size())) {
    std::printf("[ERROR] Failed to write %s, written=%zd, expected=%zu\n", output_path.c_str(), written,
                json_str.size());
    close(fd);
    return 1;
  }
  close(fd);

  std::printf("[INFO] host_route.json generated: %s (%zu devices)\n", output_path.c_str(),
              host_route_data.devices.size());
  std::printf("[INFO] local_comm_res no longer requires this file; optional offline EID inventory only.\n");
  return 0;
}

}  // namespace

int RunHostRoute(const std::vector<std::string> &args) {
  HostRouteArgs route_args;
  if (!ParseHostRouteArgs(args, route_args)) {
    return 1;
  }

  // 1. Enumerate all NPUs.
  std::vector<int32_t> npu_ids;
  if (EnumerateAllNpuIds(npu_ids) != 0) {
    return 1;
  }

  // 2. Use the first NPU for product form; pass topo path (may be empty) to GenerateRouteDataViaDsmi.
  const int32_t seed_phy_id = npu_ids.front();
  uint32_t mainboard_id = 0;
  if (hixl::GetMainboardId(seed_phy_id, mainboard_id) != hixl::SUCCESS) {
    std::printf("[ERROR] GetMainboardId failed for phy_id=%d\n", seed_phy_id);
    return 1;
  }
  const bool is_server = hixl::TopoFileFinder::IsProductServer(mainboard_id);
  std::printf("[INFO] mainboard_id=0x%x, is_server=%d\n", mainboard_id, static_cast<int>(is_server));
  if (!route_args.topo_file_path.empty()) {
    std::printf("[INFO] Using user-provided topo: %s\n", route_args.topo_file_path.c_str());
  }

  hixl::HostRouteData host_route_data;
  if (GenerateHostRouteData(npu_ids, route_args.topo_file_path, is_server, host_route_data) != 0) {
    std::printf("[ERROR] No valid host_route entries generated\n");
    return 1;
  }

  // 3. Write host_route.json.
  return WriteHostRouteOutput(route_args, host_route_data);
}

}  // namespace hixl_tool
