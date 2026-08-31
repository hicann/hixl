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
 * @file local_comm_res_tool.cc
 * @brief LocalCommRes generator implementation
 *
 * This file covers:
 * - DCMI wrappers (GetUBEntityList, GetMainboardId)
 * - File parsing (ParseTopoFile, ParseRouteFile)
 * - Edge generation (GenerateD2DEdges, GenerateH2DEdges, GenerateD2HEdges)
 * - Core flow (GenerateLocalCommRes)
 *
 * RootInfo construction lives in the rootinfo_builder module
 */

#include "local_comm_res_generator_v1.h"
#include <fstream>
#include <memory>
#include <sstream>
#include <algorithm>
#include <array>
#include <cstring>
#include <cerrno>
#include <cstdio>
#include <set>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/hixl_utils.h"
#include "common/scope_guard.h"
#include "dsmi_proxy.h"
#include "nlohmann/json.hpp"

namespace hixl {

// ============ File-local constants and helpers ============

namespace {

// Product-form constants
constexpr uint32_t kMainboardIdPod1 = 0x3;
constexpr uint32_t kMainboardIdPod2 = 0x5;
constexpr uint32_t kMainboardIdPod3 = 0x7;
constexpr uint32_t kMainboardIdServerMin1 = 0x21;
constexpr uint32_t kMainboardIdServerMax1 = 0x2B;
constexpr uint32_t kMainboardIdServerMin2 = 0x40;
constexpr uint32_t kMainboardIdServerMax2 = 0x46;

// Topology constants
constexpr const char *kLinkTypePeer2Peer = "PEER2PEER";
constexpr const char *kTopoType1DMesh = "1DMESH";

// Topology net_layer: 0 = Full Mesh, 1 = CLOS
constexpr int32_t kTopoNetLayerMesh = 0;
constexpr int32_t kTopoNetLayerClos = 1;
constexpr int32_t kTopoDieIdMax = 1;  // Dual-die chip; valid die_id is 0 or 1

// Plane constants
constexpr const char *kPlanePg0 = "plane_pg_0";
constexpr const char *kPlanePg1 = "plane_pg_1";

// Network instance prefix
constexpr const char *kNetInstancePrefix = "superpod_";

// Default paths
constexpr const char *kDefaultTopoDir = "/usr/local/Ascend/driver/topo/950/";
constexpr const char *kDefaultRoutePath = "/lib/route.conf";

// File-existence helper
inline bool IsFileExists(const std::string &path) {
  struct stat buffer;
  return (stat(path.c_str(), &buffer) == 0);
}

// Magic-number constants
constexpr size_t kHexPrefixLength = 2;     // Length of the "0x" prefix
constexpr size_t kNpuGroupSize = 8;        // NPU group size
constexpr size_t kPgEidSecondIndex = 1;    // Second PG EID index
constexpr size_t kSecondElementSize = 2;   // Size check for a second element
constexpr uint32_t kOddParity = 1;         // Odd parity
constexpr uint32_t kEvenParity = 0;        // Even parity
constexpr uint32_t kParityModuloBase = 2;  // Parity modulo base

// Product-form helper
inline bool IsProductServer(uint32_t mainboard_id) {
  return ((mainboard_id >= kMainboardIdServerMin1 && mainboard_id <= kMainboardIdServerMax1 &&
           (mainboard_id % kParityModuloBase == kOddParity)) ||
          (mainboard_id >= kMainboardIdServerMin2 && mainboard_id <= kMainboardIdServerMax2 &&
           (mainboard_id % kParityModuloBase == kEvenParity)));
}

// Topo file names
constexpr const char *kTopoFileAtlas950 = "atlas_950_1.json";
constexpr const char *kTopoFileAtlas850 = "atlas_850_1.json";

// Procfs paths
constexpr const char *kProcPathAscendUb = "/proc/ascend_ub";
constexpr const char *kProcPathAsdrvUb = "/proc/asdrv_ub";

// urma_admin command path
constexpr const char *kUrmaAdminPath = "/usr/local/sbin/urma_admin";
constexpr const char *kProcDevIdFile = "dev_id";
constexpr const char *kProcPairInfoFile = "pair_info";

// Procfs delay (microseconds)
constexpr useconds_t kProcfsWriteDelayUs = 100000;  // 100ms

// DCMI main and sub commands
enum class DcmiMainCmd {
  CHIP_INF = 12,
};

enum class DcmiChipInfoSubCmd {
  SPOD_INFO = 1,
};

// D2D edge-match input
struct D2DEdgeMatchInput {
  const NpuRootInfo &self_rootinfo;
  const NpuRootInfo &peer_rootinfo;
  int32_t peer_id;
  const std::vector<std::string> &local_ports;
  const std::vector<std::string> &peer_ports;
};

// TopoFileFinder helpers now live in namespace hixl

// Strip colons from a formatted EID
std::string FormatEidFromUrma(const std::string &eid_with_colons) {
  std::string result;
  for (char c : eid_with_colons) {
    if (c != ':') {
      result += c;
    }
  }
  return result;
}

// Single EID record from urma_admin show
struct UrmaEidEntry {
  std::string udma_name;  // "udma3"
  int eid_index = 0;      // eid0, eid1, ...
  std::string eid;        // Raw EID with colons
};

// Resolve urma_admin path (same lookup as hccn_tool)
std::string GetUrmaAdminPath() {
  // Prefer the absolute path when it exists
  if (access(kUrmaAdminPath, F_OK) == 0) {
    return kUrmaAdminPath;
  }
  // Fall back to command -v lookup on PATH
  std::string check_cmd = "command -v urma_admin > /dev/null 2>&1";
  if (system(check_cmd.c_str()) != 0) {
    HIXL_LOGW("[GetUrmaAdminPath] urma_admin not found in default path or PATH");
    return "";
  }
  return "urma_admin";  // Relative name; resolved from PATH
}

// Execute an urma_admin command
Status DefaultUrmaAdminExec(const std::string &cmd, std::string &output) {
  auto urma_path = GetUrmaAdminPath();
  HIXL_LOGI("[DefaultUrmaAdminExec] resolved urma_admin path: '%s'", urma_path.c_str());
  HIXL_CHK_BOOL_RET_STATUS(!urma_path.empty(), FAILED, "[DefaultUrmaAdminExec] urma_admin not found");
  std::string full_cmd = urma_path + " " + cmd;
  HIXL_LOGI("[DefaultUrmaAdminExec] exec cmd: '%s'", full_cmd.c_str());
  FILE *raw_pipe = popen(full_cmd.c_str(), "r");
  HIXL_CHK_BOOL_RET_STATUS(raw_pipe != nullptr, FAILED,
                           "[DefaultUrmaAdminExec] Call api:popen failed, cmd:%s, errno=%d(%s)", full_cmd.c_str(),
                           errno, strerror(errno));
  auto pipe_deleter = [](FILE *f) {
    if (f) {
      pclose(f);
    }
  };
  std::unique_ptr<FILE, decltype(pipe_deleter)> pipe(raw_pipe, pipe_deleter);

  char buf[512];
  while (fgets(buf, sizeof(buf), pipe.get()) != nullptr) {
    output += buf;
  }
  HIXL_LOGI("[DefaultUrmaAdminExec] output length: %zu bytes, raw output:\n%s", output.size(), output.c_str());
  return SUCCESS;
}

// Parse urma_admin show output and collect EID entries
Status ParseUrmaAdminOutput(const std::string &cmd_output, std::vector<UrmaEidEntry> &all_entries) {
  HIXL_LOGI("[ParseUrmaAdminOutput] urma_admin output length: %zu bytes", cmd_output.length());
  HIXL_CHK_BOOL_RET_STATUS(!cmd_output.empty(), FAILED, "[ParseUrmaAdminOutput] urma_admin output is empty");
  // Log the first 500 characters for debugging
  std::string output_preview = cmd_output.length() > 500 ? cmd_output.substr(0, 500) + "..." : cmd_output;
  HIXL_LOGI("[ParseUrmaAdminOutput] urma_admin output preview:\n%s", output_preview.c_str());

  std::istringstream stream(cmd_output);
  std::string line;
  while (std::getline(stream, line)) {
    // Skip table header and separator lines
    if ((line.find("num") != std::string::npos && line.find("ubep_dev") != std::string::npos) ||
        line.find("---") != std::string::npos || line.find("eid") == std::string::npos) {
      continue;
    }

    // Line format: "0 udma3 UB eid1 0000:0000:003f:0600:0010:0000:df00:1001 ACTIVE"
    std::istringstream iss(line);
    std::string num_str, udma_name, tp_type, eid_name, eid_value, link_status;
    iss >> num_str >> udma_name >> tp_type >> eid_name >> eid_value >> link_status;

    if (udma_name.empty() || eid_name.empty() || eid_value.empty()) {
      continue;
    }

    // Extract eid_index: "eid1" → 1
    int eid_index = 0;
    try {
      // Skip the 3-character "eid" prefix and parse the index
      eid_index = std::stoi(eid_name.substr(std::strlen("eid")));
    } catch (const std::exception &) {
      continue;
    }

    UrmaEidEntry entry;
    entry.udma_name = udma_name;
    entry.eid_index = eid_index;
    entry.eid = eid_value;
    all_entries.push_back(entry);
  }

  HIXL_CHK_BOOL_RET_STATUS(!all_entries.empty(), FAILED,
                           "[ParseUrmaAdminOutput] No entries parsed from urma_admin show output");
  HIXL_LOGI("[ParseUrmaAdminOutput] Successfully parsed %zu entries", all_entries.size());
  return SUCCESS;
}

// Extract CPU+die key (e.g. "c1d1") from UB dev name like "udmac1d1e2".
// The key is the concatenation of the 'c' segment and 'd' segment (each char + its digit).
std::string ExtractCpuDieKey(const std::string &ub_dev_name) {
  size_t c_pos = ub_dev_name.find('c');
  size_t d_pos = ub_dev_name.find('d', (c_pos == std::string::npos) ? 0 : c_pos + 1);
  if (c_pos == std::string::npos || d_pos == std::string::npos || d_pos + 1 >= ub_dev_name.size()) {
    return "";
  }
  // c segment: "cX", d segment: "dY" → "cXdY"
  return ub_dev_name.substr(c_pos, 2) +
         ub_dev_name.substr(d_pos, 2);  // CPU and die information each occupy 2 characters.
}

// Build CPU+die key → Host PG EID map from urma_admin entries.
// A Host 8-port PG group has eid_count >= 8 and contains a PG EID.
Status BuildCpuDieToHostPgEidMap(const std::vector<UrmaEidEntry> &all_entries,
                                 std::map<std::string, std::string> &cpu_die_to_pg_eid) {
  std::map<std::string, std::vector<UrmaEidEntry>> udma_groups;
  for (const auto &entry : all_entries) {
    udma_groups[entry.udma_name].push_back(entry);
  }

  for (const auto &[name, entries] : udma_groups) {
    if (entries.size() < kNpuGroupSize) {
      continue;
    }
    std::string cpu_die_key = ExtractCpuDieKey(name);
    if (cpu_die_key.empty()) {
      continue;
    }
    HIXL_LOGI("[BuildCpuDieToHostPgEidMap] Host UDMA group: %s (eid_count=%zu, cpu_die_key=%s)", name.c_str(),
              entries.size(), cpu_die_key.c_str());
    for (const auto &entry : entries) {
      std::string eid_no_colon = FormatEidFromUrma(entry.eid);
      auto info = ParseEidByte6(eid_no_colon);
      if (info.is_pg_eid) {
        cpu_die_to_pg_eid[cpu_die_key] = eid_no_colon;
        HIXL_LOGI("[BuildCpuDieToHostPgEidMap] PG EID for %s: %s", cpu_die_key.c_str(), eid_no_colon.c_str());
        break;
      }
    }
  }

  HIXL_CHK_BOOL_RET_STATUS(!cpu_die_to_pg_eid.empty(), FAILED,
                           "[BuildCpuDieToHostPgEidMap] No Host PG EID found (no UDMA group with eid_count >= 8)");
  return SUCCESS;
}

// Find remote_eid for Server: standalone non-PG EID on non-mesh die (die 0).
// The UDMA device has exactly 1 EID with no PG.
Status FindServerRemoteEid(const std::vector<UrmaDevice> &urma_devices, int32_t mesh_die_id, std::string &remote_eid) {
  int32_t non_mesh_die = 1 - mesh_die_id;
  for (const auto &dev : urma_devices) {
    if (dev.eid_list.size() != 1) {
      continue;
    }
    auto info = ParseEidByte6(dev.eid_list[0]);
    if (info.die_id != non_mesh_die || info.is_pg_eid) {
      continue;
    }
    remote_eid = dev.eid_list[0];
    HIXL_LOGI("[FindServerRemoteEid] remote_eid=%s (die_id=%d, standalone non-PG)", remote_eid.c_str(), non_mesh_die);
    return SUCCESS;
  }
  HIXL_CHK_BOOL_RET_STATUS(false, FAILED, "[FindServerRemoteEid] No standalone non-PG EID found on die %d",
                           non_mesh_die);
}

// Find remote_eid for PoD: PG EID of the 2+1 group (2 physical + 1 PG = 3 EIDs)
// on the same die as the CLOS 6-port out-of-box group (non-mesh die).
Status FindPodRemoteEid(const std::vector<UrmaDevice> &urma_devices, int32_t mesh_die_id, std::string &remote_eid) {
  int32_t non_mesh_die = 1 - mesh_die_id;
  for (const auto &dev : urma_devices) {
    if (dev.eid_list.size() != kSecondElementSize + 1) {  // 3 EIDs: 2 physical + 1 PG
      continue;
    }
    std::string pg_eid;
    int32_t die_id = -1;
    for (const auto &eid : dev.eid_list) {
      auto info = ParseEidByte6(eid);
      if (info.is_pg_eid) {
        pg_eid = eid;
        die_id = info.die_id;
        break;
      }
    }
    if (pg_eid.empty() || die_id != non_mesh_die) {
      continue;
    }
    remote_eid = pg_eid;
    HIXL_LOGI("[FindPodRemoteEid] remote_eid=%s (die_id=%d, 2+1 PG group)", remote_eid.c_str(), die_id);
    return SUCCESS;
  }
  HIXL_CHK_BOOL_RET_STATUS(false, FAILED, "[FindPodRemoteEid] No 2+1 PG EID found on die %d", non_mesh_die);
}

// Find the PG EID for a UDMA group. Returns FAILED if no PG EID exists.
Status FindPgEidForGroup(const std::vector<UrmaEidEntry> &entries, std::string &pg_eid) {
  for (const auto &entry : entries) {
    std::string eid_no_colon = FormatEidFromUrma(entry.eid);
    auto info = ParseEidByte6(eid_no_colon);
    if (info.is_pg_eid) {
      pg_eid = eid_no_colon;
      return SUCCESS;
    }
  }
  HIXL_CHK_BOOL_RET_STATUS(false, FAILED, "[FindPgEidForGroup] No PG EID found in %zu entries", entries.size());
}

// Build UB dev name → PG EID map from urma_admin entries.
// Each UDMA group must contain a PG EID, otherwise FAILED.
Status BuildUbDevNameToEidMap(const std::vector<UrmaEidEntry> &all_entries,
                              std::map<std::string, std::string> &ub_name_to_eid) {
  std::map<std::string, std::vector<UrmaEidEntry>> udma_groups;
  for (const auto &entry : all_entries) {
    udma_groups[entry.udma_name].push_back(entry);
  }

  for (const auto &[name, entries] : udma_groups) {
    std::string pg_eid;
    if (FindPgEidForGroup(entries, pg_eid) != SUCCESS) {
      HIXL_LOGW("[BuildUbDevNameToEidMap] No PG EID found for UDMA group '%s', skipping", name.c_str());
      continue;
    }
    ub_name_to_eid[name] = pg_eid;
  }

  HIXL_CHK_BOOL_RET_STATUS(!ub_name_to_eid.empty(), FAILED, "[BuildUbDevNameToEidMap] No UDMA group with PG EID found");
  return SUCCESS;
}

// Generate a single RouteEntry for one NPU via DSMI + urma_admin + DCMI.
// local_eid = the eid corresponding to the UB dev name in urma_admin show.
// host_pg_eid (8-port PG) is computed separately for H2U and returned via out param
// when npu_id == phy_dev_id.
Status GenerateRouteEntryForNpu(int32_t npu_id, int32_t mesh_die_id, bool is_server,
                                const std::map<std::string, std::string> &ub_name_to_eid, RouteEntry &entry) {
  // Convert phy_dev_id to logic_id for DSMI call
  uint32_t logic_id = 0;
  HIXL_CHK_STATUS_RET(DcmiProxy::GetLogicIdFromPhyId(static_cast<uint32_t>(npu_id), &logic_id),
                      "[GenerateRouteEntryForNpu] Call api:GetLogicIdFromPhyId failed, npu_id:%d", npu_id);

  // Get UB dev name from DSMI
  std::string ub_dev_name;
  HIXL_CHK_STATUS_RET(DsmiProxy::GetUbDevName(static_cast<int32_t>(logic_id), ub_dev_name),
                      "[GenerateRouteEntryForNpu] Call api:GetUbDevName failed, npu_id=%d", npu_id);

  // Look up local_eid from urma_admin entries by UB dev name
  auto name_it = ub_name_to_eid.find(ub_dev_name);
  HIXL_CHK_BOOL_RET_STATUS(name_it != ub_name_to_eid.end(), FAILED,
                           "[GenerateRouteEntryForNpu] UB dev name '%s' not found in urma_admin show (npu_id=%d)",
                           ub_dev_name.c_str(), npu_id);

  // Get device-side EIDs and find remote_eid
  std::vector<UrmaDevice> urma_devices;
  HIXL_CHK_STATUS_RET(GetUrmaDeviceList(npu_id, urma_devices),
                      "[GenerateRouteEntryForNpu] Failed to get urma devices for npu_id=%d", npu_id);

  std::string remote_eid;
  if (is_server) {
    HIXL_CHK_STATUS_RET(FindServerRemoteEid(urma_devices, mesh_die_id, remote_eid),
                        "[GenerateRouteEntryForNpu] Failed to find remote_eid for npu_id=%d", npu_id);
  } else {
    HIXL_CHK_STATUS_RET(FindPodRemoteEid(urma_devices, mesh_die_id, remote_eid),
                        "[GenerateRouteEntryForNpu] Failed to find remote_eid for npu_id=%d", npu_id);
  }

  entry.device_id = npu_id;
  entry.local_eid = name_it->second;  // PG EID from urma_admin for this UB dev name
  entry.remote_eid = remote_eid;
  HIXL_LOGI("[GenerateRouteEntryForNpu] npu_id=%d, local_eid=%s, remote_eid=%s", npu_id, entry.local_eid.c_str(),
            entry.remote_eid.c_str());
  return SUCCESS;
}

// Compute Host 8-port PG EID for phy_dev_id using DSMI UB dev name + urma_admin map
Status ComputeHostPgEid(int32_t phy_dev_id, const std::map<std::string, std::string> &cpu_die_to_pg_eid,
                        std::string &host_pg_eid) {
  uint32_t logic_id = 0;
  HIXL_CHK_STATUS_RET(DcmiProxy::GetLogicIdFromPhyId(static_cast<uint32_t>(phy_dev_id), &logic_id),
                      "[ComputeHostPgEid] Call api:GetLogicIdFromPhyId failed, phy_dev_id:%d", phy_dev_id);

  std::string ub_dev_name;
  HIXL_CHK_STATUS_RET(DsmiProxy::GetUbDevName(static_cast<int32_t>(logic_id), ub_dev_name),
                      "[ComputeHostPgEid] Call api:GetUbDevName failed, logic_id:%d", logic_id);

  std::string cpu_die_key = ExtractCpuDieKey(ub_dev_name);
  HIXL_CHK_BOOL_RET_STATUS(!cpu_die_key.empty(), FAILED,
                           "[ComputeHostPgEid] Failed to extract cpu_die_key from ub_dev_name:%s", ub_dev_name.c_str());

  auto it = cpu_die_to_pg_eid.find(cpu_die_key);
  HIXL_CHK_BOOL_RET_STATUS(it != cpu_die_to_pg_eid.end(), FAILED,
                           "[ComputeHostPgEid] No 8-port PG EID for cpu_die_key:%s", cpu_die_key.c_str());

  host_pg_eid = it->second;
  HIXL_LOGI("[ComputeHostPgEid] host_pg_eid=%s (cpu_die_key=%s)", host_pg_eid.c_str(), cpu_die_key.c_str());
  return SUCCESS;
}

// Parse a "die/port" string (e.g. "1/7") into die_id and port.
// die_id must be 0 or 1 (same encoding as EID byte6); port is non-negative; the whole string must be consumed.
Status ParseDiePort(const std::string &port_str, int32_t &die_id, int32_t &port) {
  const size_t slash = port_str.find('/');
  HIXL_CHK_BOOL_RET_STATUS(slash != std::string::npos && slash != 0U && slash + 1 < port_str.size(), FAILED,
                           "[ParseDiePort] Invalid die/port format:%s", port_str.c_str());
  size_t parsed_die = 0;
  size_t parsed_port = 0;
  try {
    die_id = std::stoi(port_str.substr(0, slash), &parsed_die);
    port = std::stoi(port_str.substr(slash + 1), &parsed_port);
  } catch (const std::invalid_argument &) {
    HIXL_CHK_BOOL_RET_STATUS(false, FAILED, "[ParseDiePort] Failed to parse die/port as integer:%s", port_str.c_str());
  } catch (const std::out_of_range &) {
    HIXL_CHK_BOOL_RET_STATUS(false, FAILED, "[ParseDiePort] die/port out of range:%s", port_str.c_str());
  }
  const size_t port_start = slash + 1;
  constexpr int32_t kPortMin = 0;
  HIXL_CHK_BOOL_RET_STATUS(parsed_die == slash && parsed_port == port_str.size() - port_start && die_id >= 0 &&
                               die_id <= kTopoDieIdMax && port >= kPortMin,
                           FAILED, "[ParseDiePort] Invalid die/port value:%s, die_id=%d valid range [0, %d], port=%d",
                           port_str.c_str(), die_id, kTopoDieIdMax, port);
  return SUCCESS;
}

// Parse die id from the first port in the list; SUCCESS on success.
Status ResolveDieIdFromPorts(const std::vector<std::string> &ports, int32_t &die_id) {
  HIXL_CHK_BOOL_RET_STATUS(!ports.empty(), FAILED, "[ResolveDieIdFromPorts] Empty port list");
  int32_t port = -1;
  HIXL_CHK_STATUS_RET(ParseDiePort(ports.front(), die_id, port),
                      "[ResolveDieIdFromPorts] Failed to parse first port:%s", ports.front().c_str());
  return SUCCESS;
}

// Resolve the fullmesh (net_layer=0) die of an NPU from topo.
Status ResolveMeshDieIdFromTopo(const TopoData &topo_data, int32_t npu_id, int32_t &mesh_die_id) {
  for (const auto &link : topo_data.links) {
    if (link.net_layer != kTopoNetLayerMesh) {
      continue;
    }
    const std::vector<std::string> *ports = nullptr;
    if (link.local_a == npu_id) {
      ports = &link.local_a_ports;
    } else if (link.local_b == npu_id) {
      ports = &link.local_b_ports;
    } else {
      continue;
    }
    HIXL_CHK_STATUS_RET(ResolveDieIdFromPorts(*ports, mesh_die_id),
                        "[ResolveMeshDieIdFromTopo] Failed to parse mesh ports, npu_id=%d", npu_id);
    HIXL_LOGI("[ResolveMeshDieIdFromTopo] npu_id=%d, mesh_die_id=%d", npu_id, mesh_die_id);
    return SUCCESS;
  }
  HIXL_CHK_BOOL_RET_STATUS(false, FAILED, "[ResolveMeshDieIdFromTopo] No fullmesh edge found for npu_id=%d", npu_id);
}

// Resolve the CLOS die of an NPU from topo: count CLOS ports per die and take the majority.
// Covers same-die 8-port CLOS and mixed 6+2 ports on one edge.
Status ResolveClosDieIdFromTopo(const TopoData &topo_data, int32_t npu_id, int32_t &clos_die_id) {
  std::array<int32_t, kTopoDieIdMax + 1> die_port_count = {0, 0};
  bool found_clos_edge = false;
  for (const auto &link : topo_data.links) {
    if (link.net_layer != kTopoNetLayerClos || link.local_a != npu_id) {
      continue;
    }
    found_clos_edge = true;
    for (const auto &port_str : link.local_a_ports) {
      int32_t die_id = -1;
      int32_t port = -1;
      HIXL_CHK_STATUS_RET(ParseDiePort(port_str, die_id, port),
                          "[ResolveClosDieIdFromTopo] Failed to parse CLOS port:%s, npu_id=%d", port_str.c_str(),
                          npu_id);
      ++die_port_count[die_id];
    }
  }
  const int32_t die0_ports = die_port_count[0];
  const int32_t die1_ports = die_port_count[1];
  HIXL_CHK_BOOL_RET_STATUS(found_clos_edge && (die0_ports != 0 || die1_ports != 0), FAILED,
                           "[ResolveClosDieIdFromTopo] No parseable CLOS port for npu_id=%d", npu_id);
  clos_die_id = (die1_ports > die0_ports) ? 1 : 0;
  HIXL_LOGI("[ResolveClosDieIdFromTopo] npu_id=%d, clos_die_id=%d, die0_ports=%d, die1_ports=%d", npu_id, clos_die_id,
            die0_ports, die1_ports);
  return SUCCESS;
}

}  // anonymous namespace

// ============================================================================
// TopoFileFinder
// ============================================================================

TopoFileFinder::TopoFileFinder() {}

TopoFileFinder::~TopoFileFinder() {}

bool TopoFileFinder::MatchProductForm(uint32_t mainboard_id, std::string &topo_file_name) {
  // mainboard_id → default topo file name (case-when). Append mappings for new product forms.
  // Used only when the caller does not pass a topo file; an explicit topo path is unconstrained.
  switch (mainboard_id) {
    case kMainboardIdPod1:
    case kMainboardIdPod2:
    case kMainboardIdPod3:
      topo_file_name = kTopoFileAtlas950;
      return true;
    default:
      break;
  }
  if (IsProductServer(mainboard_id)) {
    topo_file_name = kTopoFileAtlas850;
    return true;
  }
  return false;
}

bool TopoFileFinder::IsProductServer(uint32_t mainboard_id) {
  return ((mainboard_id >= kMainboardIdServerMin1 && mainboard_id <= kMainboardIdServerMax1 &&
           (mainboard_id % kParityModuloBase == kOddParity)) ||
          (mainboard_id >= kMainboardIdServerMin2 && mainboard_id <= kMainboardIdServerMax2 &&
           (mainboard_id % kParityModuloBase == kEvenParity)));
}

std::string TopoFileFinder::FindTopoFile(const std::string &topo_dir, uint32_t mainboard_id) {
  std::string topo_file_name;
  if (!MatchProductForm(mainboard_id, topo_file_name)) {
    HIXL_LOGW("Unknown product form for mainboard_id=0x%x", mainboard_id);
    return "";
  }
  HIXL_LOGI("mainboard_id=0x%x, topo_file_name=%s", mainboard_id, topo_file_name.c_str());

  std::string full_path = topo_dir + "/" + topo_file_name;
  if (!IsFileExists(full_path)) {
    HIXL_LOGW("Topo file not found: %s", full_path.c_str());
    return "";
  }
  HIXL_LOGI("Matched topo file: %s", full_path.c_str());
  return full_path;
}

// ============================================================================
// ProcfsRouteHandler
// ============================================================================

ProcfsRouteHandler::ProcfsRouteHandler() : injected_proc_base_path_() {}

ProcfsRouteHandler::ProcfsRouteHandler(std::string proc_base_path)
    : injected_proc_base_path_(std::move(proc_base_path)) {}

ProcfsRouteHandler::~ProcfsRouteHandler() {}

std::string ProcfsRouteHandler::FindProcBasePath() const {
  // Injected path takes priority; empty string means default ascend_ub / asdrv_ub auto-discovery.
  if (!injected_proc_base_path_.empty()) {
    if (IsFileExists(injected_proc_base_path_ + "/" + kProcDevIdFile)) {
      return injected_proc_base_path_;
    }
    return "";
  }
  std::string dev_id_file = kProcDevIdFile;
  if (IsFileExists(std::string(kProcPathAscendUb) + "/" + dev_id_file)) {
    return kProcPathAscendUb;
  }
  if (IsFileExists(std::string(kProcPathAsdrvUb) + "/" + dev_id_file)) {
    return kProcPathAsdrvUb;
  }
  return "";
}

bool ProcfsRouteHandler::ReadFileToString(const std::string &path, std::string &content) {
  if (access(path.c_str(), F_OK) != 0) {
    HIXL_LOGW("[ReadFileToString] File access check failed: %s, errno=%d(%s)", path.c_str(), errno, strerror(errno));
    return false;
  }
  std::ifstream file(path);
  if (!file.is_open()) {
    HIXL_LOGW("[ReadFileToString] Failed to open file: %s, errno=%d(%s)", path.c_str(), errno, strerror(errno));
    return false;
  }
  std::ostringstream oss;
  oss << file.rdbuf();
  content = oss.str();
  return true;
}

bool ProcfsRouteHandler::WriteStringToFile(const std::string &path, const std::string &content) {
  int fd = open(path.c_str(), O_WRONLY);
  if (fd < 0) {
    HIXL_LOGW("[WriteStringToFile] Failed to open %s: errno=%d(%s)", path.c_str(), errno, strerror(errno));
    return false;
  }
  ssize_t written = write(fd, content.c_str(), content.size());
  if (written < 0) {
    HIXL_LOGW("[WriteStringToFile] write() failed for %s: errno=%d(%s)", path.c_str(), errno, strerror(errno));
  }
  close(fd);
  fd = -1;
  if (written != static_cast<ssize_t>(content.size())) {
    HIXL_LOGW("[WriteStringToFile] Incomplete write to %s: written=%zd, expected=%zu", path.c_str(), written,
              content.size());
    return false;
  }
  return true;
}

std::string ProcfsRouteHandler::TrimString(const std::string &s) {
  size_t start = s.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) {
    return "";
  }
  size_t end = s.find_last_not_of(" \t\r\n");
  return s.substr(start, end - start + 1);
}

bool ProcfsRouteHandler::ParseSlotIdFromLine(const std::string &line, std::string &slot_id) {
  if (line.find("dev_id=") == std::string::npos) {
    return false;
  }
  size_t pos = line.find("slot_id=");
  if (pos == std::string::npos) {
    return false;
  }
  slot_id = TrimString(line.substr(pos + std::strlen("slot_id=")));
  return true;
}

bool ProcfsRouteHandler::ParseEidFromLine(const std::string &line, const std::string &prefix, std::string &eid) {
  if (line.find(prefix) == std::string::npos) {
    return false;
  }
  size_t pos = line.find(':');
  if (pos == std::string::npos) {
    return false;
  }
  eid = TrimString(line.substr(pos + 1));
  return !eid.empty();
}

std::string ProcfsRouteHandler::FormatEidValue(const std::string &eid) {
  std::string result = eid;
  if (result.size() >= kHexPrefixLength && result[0] == '0' && (result[1] == 'x' || result[1] == 'X')) {
    result = result.substr(kHexPrefixLength);
  }
  // Strip all colons.
  result.erase(std::remove(result.begin(), result.end(), ':'), result.end());
  return result;
}

size_t ProcfsRouteHandler::SelectEidIndexByNpuId(int32_t npu_id, size_t local_count, size_t remote_count) {
  int32_t group_offset = npu_id % 8;
  size_t eid_idx = (group_offset < 4) ? 0 : 1;  // First 4 NPUs use group 0, last 4 use group 1.
  if (eid_idx >= local_count || eid_idx >= remote_count) {
    HIXL_LOGW("[ParsePairInfo] npu_id=%d: eid_idx=%zu out of range (local=%zu, remote=%zu), fallback to index 0",
              npu_id, eid_idx, local_count, remote_count);
    eid_idx = 0;
  }
  return eid_idx;
}

bool ProcfsRouteHandler::CollectEidsFromPairInfo(const std::string &pair_info_content, std::string &found_slot_id,
                                                 std::vector<std::string> &local_eids,
                                                 std::vector<std::string> &remote_eids) {
  std::istringstream iss(pair_info_content);
  std::string line;
  while (std::getline(iss, line)) {
    line = TrimString(line);
    if (line.empty()) {
      continue;
    }
    std::string slot;
    if (ParseSlotIdFromLine(line, slot)) {
      found_slot_id = slot;
    }
    std::string eid_val;
    if (ParseEidFromLine(line, "local_eid", eid_val)) {
      local_eids.push_back(eid_val);
    }
    if (ParseEidFromLine(line, "remote_eid", eid_val)) {
      remote_eids.push_back(eid_val);
    }
  }
  return (!found_slot_id.empty() && !local_eids.empty() && !remote_eids.empty());
}

bool ProcfsRouteHandler::ParsePairInfoForDevice(const std::string &pair_info_content, int32_t npu_id, int32_t &slot_id,
                                                std::string &local_eid, std::string &remote_eid) const {
  std::string found_slot_id;
  std::vector<std::string> local_eids;
  std::vector<std::string> remote_eids;

  if (!CollectEidsFromPairInfo(pair_info_content, found_slot_id, local_eids, remote_eids)) {
    HIXL_LOGW("[ParsePairInfo] npu_id=%d: failed to collect slot_id or eids", npu_id);
    return false;
  }

  HIXL_LOGD("[ParsePairInfo] npu_id=%d, slot_id=[%s], local_eids_count=%zu, remote_eids_count=%zu", npu_id,
            found_slot_id.c_str(), local_eids.size(), remote_eids.size());

  size_t eid_idx = SelectEidIndexByNpuId(npu_id, local_eids.size(), remote_eids.size());

  try {
    slot_id = std::stoi(found_slot_id);
  } catch (const std::exception &) {
    slot_id = npu_id;
  }

  local_eid = FormatEidValue(local_eids[eid_idx]);
  remote_eid = FormatEidValue(remote_eids[eid_idx]);

  return (!local_eid.empty() || !remote_eid.empty());
}

Status ProcfsRouteHandler::ProcessNpuProcfsRoute(int32_t npu_id, const std::string &dev_id_path,
                                                 const std::string &pair_info_path, RouteEntry &entry) const {
  HIXL_LOGI("[Procfs] Processing npu_id=%d", npu_id);
  // Write phyid to select the device
  std::ostringstream dev_id_ss;
  dev_id_ss << npu_id << "\n";
  HIXL_CHK_BOOL_RET_STATUS(WriteStringToFile(dev_id_path, dev_id_ss.str()), FAILED,
                           "[Procfs] Failed to write npu_id=%d to %s", npu_id, dev_id_path.c_str());

  // Short delay so the kernel can refresh
  usleep(kProcfsWriteDelayUs);

  // Read pair_info
  std::string pair_info_content;
  HIXL_CHK_BOOL_RET_STATUS(ReadFileToString(pair_info_path, pair_info_content), FAILED,
                           "[Procfs] Failed to read pair_info for npu_id=%d", npu_id);

  // Parse pair_info
  int32_t slot_id = npu_id;
  std::string local_eid;
  std::string remote_eid;
  HIXL_CHK_BOOL_RET_STATUS(ParsePairInfoForDevice(pair_info_content, npu_id, slot_id, local_eid, remote_eid), FAILED,
                           "[Procfs] Failed to parse pair_info for npu_id=%d", npu_id);

  HIXL_LOGI("[Procfs] Parsed: npu_id=%d, slot_id=%d, local_eid=[%s], remote_eid=[%s]", npu_id, slot_id,
            local_eid.c_str(), remote_eid.c_str());

  // Only generate the H2D RouteEntry
  entry.device_id = npu_id;
  entry.local_eid = local_eid;
  entry.remote_eid = remote_eid;
  return SUCCESS;
}

Status ProcfsRouteHandler::GenerateRouteData(const std::set<int32_t> &related_npu_ids, RouteData &route_data) const {
  route_data.entries.clear();

  std::string proc_base = FindProcBasePath();
  HIXL_CHK_BOOL_RET_STATUS(!proc_base.empty(), FAILED, "Neither /proc/ascend_ub nor /proc/asdrv_ub found");
  HIXL_LOGI("Using procfs base path: %s", proc_base.c_str());

  std::string dev_id_path = proc_base + "/" + kProcDevIdFile;
  std::string pair_info_path = proc_base + "/" + kProcPairInfoFile;

  for (int32_t npu_id : related_npu_ids) {
    RouteEntry entry;
    HIXL_CHK_STATUS_RET(ProcessNpuProcfsRoute(npu_id, dev_id_path, pair_info_path, entry),
                        "[Procfs] ProcessNpuProcfsRoute failed, npu_id=%d", npu_id);
    route_data.entries.push_back(entry);
    HIXL_LOGI("[Procfs] RouteEntry H2D: npu_id=%d, device_id=%d, local_eid=[%s], remote_eid=[%s]", npu_id,
              entry.device_id, entry.local_eid.c_str(), entry.remote_eid.c_str());
  }

  HIXL_CHK_BOOL_RET_STATUS(!route_data.entries.empty(), FAILED, "No route entries generated from procfs");

  HIXL_LOGI("[Procfs] Generated %zu route entries from procfs:", route_data.entries.size());
  for (size_t i = 0; i < route_data.entries.size(); ++i) {
    const auto &entry = route_data.entries[i];
    HIXL_LOGI("[Procfs]   [%zu] device_id=%d, local_eid=[%s], remote_eid=[%s]", i, entry.device_id,
              entry.local_eid.c_str(), entry.remote_eid.c_str());
  }

  return SUCCESS;
}

// ============================================================================
// End of ProcfsRouteHandler
// ============================================================================

// ============ File-parse helpers ============

bool LoadRouteKvMap(std::ifstream &file, std::map<std::string, std::string> &kv_map) {
  std::string line;
  while (std::getline(file, line)) {
    size_t eq_pos = line.find('=');
    if (eq_pos == std::string::npos) {
      continue;
    }

    std::string key = line.substr(0, eq_pos);
    std::string value = line.substr(eq_pos + 1);

    size_t key_end = key.find_last_not_of(" \t\r\n");
    if (key_end != std::string::npos) {
      key = key.substr(0, key_end + 1);
    }

    size_t val_start = value.find_first_not_of(" \t\r\n");
    size_t val_end = value.find_last_not_of(" \t\r\n");
    if (val_start != std::string::npos && val_end != std::string::npos) {
      value = value.substr(val_start, val_end - val_start + 1);
    }
    kv_map[key] = value;
  }
  return true;
}

void AddRouteEntriesForDevice(const std::map<std::string, std::string> &kv_map, int32_t device_idx, int32_t device_id,
                              RouteData &route_data) {
  std::string chan_num_key = "pair" + std::to_string(device_idx) + "_chan_num";
  int32_t chan_num = 1;
  auto chan_num_it = kv_map.find(chan_num_key);
  if (chan_num_it != kv_map.end()) {
    try {
      chan_num = std::stoi(chan_num_it->second);
    } catch (const std::exception &) {
      chan_num = 1;
    }
  }

  for (int32_t j = 0; j < chan_num; ++j) {
    std::string local_key = "pair" + std::to_string(device_idx) + "_chan" + std::to_string(j) + "_local_eid";
    std::string remote_key = "pair" + std::to_string(device_idx) + "_chan" + std::to_string(j) + "_remote_eid";

    auto local_it = kv_map.find(local_key);
    auto remote_it = kv_map.find(remote_key);
    if (local_it != kv_map.end() && remote_it != kv_map.end()) {
      RouteEntry entry;
      entry.device_id = device_id;
      entry.local_eid = local_it->second;
      entry.remote_eid = remote_it->second;
      // Strip the "0x" prefix.
      if (entry.local_eid.size() >= kHexPrefixLength && entry.local_eid[0] == '0' && entry.local_eid[1] == 'x') {
        entry.local_eid = entry.local_eid.substr(kHexPrefixLength);
      }
      if (entry.remote_eid.size() >= kHexPrefixLength && entry.remote_eid[0] == '0' && entry.remote_eid[1] == 'x') {
        entry.remote_eid = entry.remote_eid.substr(kHexPrefixLength);
      }
      route_data.entries.push_back(entry);
    }
  }
}

Status BuildRouteEntries(const std::map<std::string, std::string> &kv_map, RouteData &route_data) {
  auto it = kv_map.find("pair_device_num");
  HIXL_CHK_BOOL_RET_STATUS(it != kv_map.end(), FAILED, "Missing pair_device_num");

  int32_t pair_device_num = 0;
  try {
    pair_device_num = std::stoi(it->second);
  } catch (const std::exception &) {
    HIXL_CHK_BOOL_RET_STATUS(false, FAILED, "Invalid pair_device_num value: %s", it->second.c_str());
  }
  for (int32_t i = 0; i < pair_device_num; ++i) {
    std::string dev_id_key = "pair" + std::to_string(i) + "_dev_id";
    auto dev_it = kv_map.find(dev_id_key);
    if (dev_it == kv_map.end()) {
      continue;
    }

    int32_t device_id = 0;
    try {
      device_id = std::stoi(dev_it->second);
    } catch (const std::exception &) {
      HIXL_LOGW("Invalid dev_id value: %s", dev_it->second.c_str());
      continue;
    }
    AddRouteEntriesForDevice(kv_map, i, device_id, route_data);
  }

  HIXL_LOGI("Parsed %zu route entries", route_data.entries.size());
  for (size_t i = 0; i < route_data.entries.size(); ++i) {
    const auto &entry = route_data.entries[i];
    HIXL_LOGI("  route_entry[%zu]: device_id=%d, local_eid=[%s], remote_eid=[%s]", i, entry.device_id,
              entry.local_eid.c_str(), entry.remote_eid.c_str());
  }
  return SUCCESS;
}

// ============ DCMI wrappers ============

Status GetMainboardId(int32_t phy_dev_id, unsigned int &mainboard_id) {
  HIXL_CHK_STATUS_RET(DcmiProxy::LoadDcmi(), "[GetMainboardId] DCMI not loaded");

  uint32_t logic_id = 0;
  HIXL_CHK_STATUS_RET(DcmiProxy::GetLogicIdFromPhyId(phy_dev_id, &logic_id),
                      "[GetMainboardId] Call api:GetLogicIdFromPhyId failed, phy_dev_id=%d", phy_dev_id);
  HIXL_CHK_STATUS_RET(DcmiProxy::GetMainboardId(logic_id, &mainboard_id),
                      "[GetMainboardId] Call api:GetMainboardId failed, phy_dev_id=%d", phy_dev_id);

  return SUCCESS;
}

Status GetClosNetInstanceId(int32_t phy_dev_id, std::string &net_instance_id) {
  HIXL_CHK_STATUS_RET(DcmiProxy::LoadDcmi(), "[GetClosNetInstanceId] DCMI not loaded");

  uint32_t logic_id = 0;
  HIXL_CHK_STATUS_RET(DcmiProxy::GetLogicIdFromPhyId(phy_dev_id, &logic_id),
                      "[GetClosNetInstanceId] Call api:GetLogicIdFromPhyId failed, phy_dev_id=%d", phy_dev_id);

  DcmiSpodInfo spod_info = {};
  uint32_t buf_size = sizeof(DcmiSpodInfo);
  HIXL_CHK_STATUS_RET(
      DcmiProxy::GetDeviceInfo(logic_id, static_cast<int32_t>(DcmiMainCmd::CHIP_INF),
                               static_cast<int32_t>(DcmiChipInfoSubCmd::SPOD_INFO), &spod_info, &buf_size),
      "[GetClosNetInstanceId] Call api:GetDeviceInfo failed, phy_dev_id=%d", phy_dev_id);

  net_instance_id = std::string(kNetInstancePrefix) + std::to_string(spod_info.super_pod_id);
  HIXL_LOGI("phy_dev_id=%d, super_pod_id=%u, net_instance_id=%s", phy_dev_id, spod_info.super_pod_id,
            net_instance_id.c_str());
  return SUCCESS;
}

// ============ File parsing ============

static bool ParsePortsFromJson(const nlohmann::json &edge, const char *key, std::vector<std::string> &ports) {
  if (!edge.contains(key) || !edge[key].is_array()) {
    return false;
  }
  for (const auto &port : edge[key]) {
    if (port.is_string()) {
      ports.push_back(port.get<std::string>());
    }
  }
  return true;
}

static int32_t ParseSingleLink(const nlohmann::json &edge, TopoLink &link) {
  if (!edge.contains("net_layer")) {
    HIXL_LOGW("Missing net_layer in edge object, skipping");
    return 1;  // 1 means skip
  }
  link.net_layer = edge.value("net_layer", 0);
  link.link_type = edge.value("link_type", "");
  link.topo_type = edge.value("topo_type", "");
  link.local_a = edge.value("local_a", 0);
  link.local_b = edge.value("local_b", 0);
  link.remote_a = edge.value("remote_a", -1);
  link.remote_b = edge.value("remote_b", -1);

  ParsePortsFromJson(edge, "local_a_ports", link.local_a_ports);
  ParsePortsFromJson(edge, "local_b_ports", link.local_b_ports);
  return 0;  // 0 means success
}

static Status ParseTopoJson(const std::string &topo_path, nlohmann::json &j) {
  HIXL_CHK_BOOL_RET_STATUS(access(topo_path.c_str(), F_OK) == 0, PARAM_INVALID,
                           "Call api:access failed, topo_path:%s, errno=%d(%s)", topo_path.c_str(), errno,
                           strerror(errno));
  std::ifstream file(topo_path);
  HIXL_CHK_BOOL_RET_STATUS(file.is_open(), PARAM_INVALID, "Failed to open topo file: %s, errno=%d(%s)",
                           topo_path.c_str(), errno, strerror(errno));

  try {
    file >> j;
  } catch (const nlohmann::json::exception &e) {
    HIXL_CHK_BOOL_RET_STATUS(false, FAILED, "Failed to parse topo file JSON: %s, path:%s", e.what(), topo_path.c_str());
  }
  file.close();

  HIXL_CHK_BOOL_RET_STATUS(j.contains("edge_list") && j["edge_list"].is_array() && !j["edge_list"].empty(), FAILED,
                           "No or empty edge_list found in path:%s", topo_path.c_str());
  return SUCCESS;
}

Status ParseTopoFile(const std::string &topo_path, TopoData &topo_data) {
  topo_data.links.clear();

  nlohmann::json j;
  HIXL_CHK_STATUS_RET(ParseTopoJson(topo_path, j), "ParseTopoJson failed, topo_path=%s", topo_path.c_str());

  for (const auto &edge : j["edge_list"]) {
    TopoLink link;
    link.remote_a = -1;
    link.remote_b = -1;

    if (ParseSingleLink(edge, link) == 1) {
      continue;  // Skip this edge.
    }
    topo_data.links.push_back(link);
  }

  HIXL_LOGI("Parsed %zu links from %s", topo_data.links.size(), topo_path.c_str());
  return SUCCESS;
}

Status ParseRouteFile(const std::string &route_path, RouteData &route_data) {
  route_data.entries.clear();
  HIXL_CHK_BOOL_RET_STATUS(access(route_path.c_str(), F_OK) == 0, PARAM_INVALID,
                           "Call api:access failed, route_path:%s, errno=%d(%s)", route_path.c_str(), errno,
                           strerror(errno));
  std::ifstream file(route_path);
  HIXL_CHK_BOOL_RET_STATUS(file.is_open(), PARAM_INVALID, "Failed to open route file: %s, errno=%d(%s)",
                           route_path.c_str(), errno, strerror(errno));

  std::map<std::string, std::string> kv_map;
  HIXL_CHK_BOOL_RET_STATUS(LoadRouteKvMap(file, kv_map), FAILED, "Failed to load route kv map");
  file.close();

  HIXL_CHK_STATUS_RET(BuildRouteEntries(kv_map, route_data), "BuildRouteEntries failed, route_path=%s",
                      route_path.c_str());
  return SUCCESS;
}

// ============ Edge generation ============

bool ShouldSkipD2DLink(const TopoLink &link, std::array<size_t, 4> &skip_reason) {  // 4: skip reason categories
  if (link.net_layer != kTopoNetLayerMesh) {
    ++skip_reason[0];
    return true;
  }
  if (link.link_type != kLinkTypePeer2Peer) {
    ++skip_reason[1];
    return true;
  }
  if (link.topo_type != kTopoType1DMesh) {
    ++skip_reason[2];
    return true;
  }
  return false;
}

Status AddD2DEdgesFromLink(const D2DEdgeMatchInput &input, std::vector<EndpointConfig> &edges) {
  HIXL_CHK_BOOL_RET_STATUS(input.local_ports.size() == input.peer_ports.size(), FAILED,
                           "[AddD2DEdgesFromLink] D2D port list size mismatch, peer_id=%d, local_ports=%zu, "
                           "peer_ports=%zu",
                           input.peer_id, input.local_ports.size(), input.peer_ports.size());
  for (size_t i = 0; i < input.local_ports.size(); ++i) {
    const std::string &local_port = input.local_ports[i];
    const std::string &peer_port = input.peer_ports[i];

    auto local_eid_it = input.self_rootinfo.port_to_eid.find(local_port);
    HIXL_CHK_BOOL_RET_STATUS(local_eid_it != input.self_rootinfo.port_to_eid.end(), FAILED,
                             "[AddD2DEdgesFromLink] No EID for local port:%s", local_port.c_str());
    const std::string &comm_id = local_eid_it->second;

    auto peer_eid_it = input.peer_rootinfo.port_to_eid.find(peer_port);
    HIXL_CHK_BOOL_RET_STATUS(peer_eid_it != input.peer_rootinfo.port_to_eid.end(), FAILED,
                             "[AddD2DEdgesFromLink] No EID for peer port:%s, npu_id=%d", peer_port.c_str(),
                             input.peer_id);
    const std::string &dst_eid = peer_eid_it->second;

    EndpointConfig edge{};
    edge.protocol = kProtocolUbCtp;
    edge.comm_id = comm_id;
    edge.placement = kPlacementDevice;
    edge.dst_eid = dst_eid;
    edges.push_back(edge);
    HIXL_LOGD("D2D matched: comm_id=%s, dst_eid=%s", comm_id.c_str(), dst_eid.c_str());
  }
  return SUCCESS;
}

namespace {
struct D2DLinkAppendCtx {
  const std::map<int32_t, NpuRootInfo> &npu_rootinfos;
  const NpuRootInfo &self_rootinfo;
  int32_t phy_id;
  std::array<size_t, 4> &skip_reason;  // 4: skip reason categories
  std::vector<EndpointConfig> &edges;
};

Status AppendD2DEdgesIfPhyOnLink(const TopoLink &link, D2DLinkAppendCtx &ctx) {
  bool is_local_a_side = (link.local_a == ctx.phy_id);
  bool is_local_b_side = (link.local_b == ctx.phy_id);
  if (!is_local_a_side && !is_local_b_side) {
    ++ctx.skip_reason[3];
    return SUCCESS;
  }

  int32_t peer_id = is_local_a_side ? link.local_b : link.local_a;
  const std::vector<std::string> &local_ports = is_local_a_side ? link.local_a_ports : link.local_b_ports;
  const std::vector<std::string> &peer_ports = is_local_a_side ? link.local_b_ports : link.local_a_ports;
  HIXL_CHK_BOOL_RET_STATUS(!local_ports.empty() && !peer_ports.empty() && local_ports.size() == peer_ports.size(),
                           FAILED,
                           "[GenerateD2DEdges] Invalid D2D port lists, phy_id=%d, peer_id=%d, local_ports=%zu, "
                           "peer_ports=%zu",
                           ctx.phy_id, peer_id, local_ports.size(), peer_ports.size());

  auto peer_it = ctx.npu_rootinfos.find(peer_id);
  HIXL_CHK_BOOL_RET_STATUS(peer_it != ctx.npu_rootinfos.end(), FAILED,
                           "[GenerateD2DEdges] No rootinfo for peer npu_id=%d, phy_id=%d", peer_id, ctx.phy_id);
  HIXL_CHK_STATUS_RET(
      AddD2DEdgesFromLink({ctx.self_rootinfo, peer_it->second, peer_id, local_ports, peer_ports}, ctx.edges),
      "[GenerateD2DEdges] AddD2DEdgesFromLink failed, phy_id=%d, peer_id=%d", ctx.phy_id, peer_id);
  return SUCCESS;
}
}  // namespace

Status GenerateD2DEdges(const TopoData &topo_data, const std::map<int32_t, NpuRootInfo> &npu_rootinfos, int32_t phy_id,
                        std::vector<EndpointConfig> &edges) {
  edges.clear();

  auto self_it = npu_rootinfos.find(phy_id);
  if (self_it == npu_rootinfos.end()) {
    HIXL_CHK_BOOL_RET_STATUS(topo_data.links.empty(), FAILED, "[GenerateD2DEdges] No rootinfo for self npu_id=%d",
                             phy_id);
    return SUCCESS;
  }
  const auto &self_rootinfo = self_it->second;

  HIXL_LOGI("D2D: phy_id=%d, topo_links=%zu, self_rootinfo_size=%zu", phy_id, topo_data.links.size(),
            self_rootinfo.port_to_eid.size());

  std::array<size_t, 4> skip_reason = {0, 0, 0, 0};  // 4: skip reason categories for D2D links
  D2DLinkAppendCtx ctx{npu_rootinfos, self_rootinfo, phy_id, skip_reason, edges};
  for (const auto &link : topo_data.links) {
    if (ShouldSkipD2DLink(link, skip_reason)) {
      continue;
    }
    HIXL_CHK_STATUS_RET(AppendD2DEdgesIfPhyOnLink(link, ctx),
                        "[GenerateD2DEdges] AppendD2DEdgesIfPhyOnLink failed, phy_id=%d", phy_id);
  }

  HIXL_LOGI(
      "D2D result: matched=%zu, skip(net_layer)=%zu, skip(link_type)=%zu, "
      "skip(topo_type)=%zu, skip(phy_id)=%zu",
      edges.size(), skip_reason[0], skip_reason[1], skip_reason[2], skip_reason[3]);
  return SUCCESS;
}

Status GenerateH2DEdges(const RouteData &route_data, std::vector<EndpointConfig> &edges) {
  edges.clear();

  HIXL_LOGI("H2D: route_entries=%zu", route_data.entries.size());

  for (const auto &entry : route_data.entries) {
    EndpointConfig edge;
    edge.protocol = kProtocolUbCtp;
    edge.comm_id = entry.local_eid;
    edge.placement = kPlacementHost;
    edge.dst_eid = entry.remote_eid;
    edges.push_back(edge);
    HIXL_LOGD("H2D matched: device_id=%d, local_eid=%s, remote_eid=%s", entry.device_id, entry.local_eid.c_str(),
              entry.remote_eid.c_str());
  }

  HIXL_LOGI("H2D result: matched=%zu", edges.size());
  return SUCCESS;
}

Status GenerateD2HEdges(const RouteData &route_data, int32_t phy_dev_id, std::vector<EndpointConfig> &edges) {
  edges.clear();

  HIXL_LOGI("D2H: route_entries=%zu, phy_dev_id=%d", route_data.entries.size(), phy_dev_id);

  for (const auto &entry : route_data.entries) {
    if (entry.device_id != phy_dev_id) {
      continue;
    }
    EndpointConfig edge;
    edge.protocol = kProtocolUbCtp;
    edge.comm_id = entry.remote_eid;
    edge.placement = kPlacementDevice;
    edge.dst_eid = entry.local_eid;
    edges.push_back(edge);
    HIXL_LOGD("D2H matched: device_id=%d, remote_eid=%s, local_eid=%s", entry.device_id, entry.remote_eid.c_str(),
              entry.local_eid.c_str());
  }

  HIXL_LOGI("D2H result: matched=%zu", edges.size());
  return SUCCESS;
}

// ============ Core APIs ============

void LogEndpointList(const std::vector<EndpointConfig> &endpoint_list) {
  for (size_t i = 0; i < endpoint_list.size(); ++i) {
    const auto &ep = endpoint_list[i];
    HIXL_LOGI("  [%zu] protocol=%s, comm_id=%s, placement=%s, plane=%s, dst_eid=%s, net_instance_id=%s", i,
              ep.protocol.c_str(), ep.comm_id.c_str(), ep.placement.c_str(), ep.plane.c_str(), ep.dst_eid.c_str(),
              ep.net_instance_id.c_str());
  }
}

bool IsProductPod(uint32_t mainboard_id) {
  return (mainboard_id == kMainboardIdPod1 || mainboard_id == kMainboardIdPod2 || mainboard_id == kMainboardIdPod3);
}

std::set<int32_t> CollectRelatedNpuIds(int32_t phy_dev_id) {
  // NPUs are grouped by kNpuGroupSize; collect every NPU in phy_dev_id's group.
  int32_t group_start = static_cast<int32_t>((phy_dev_id / kNpuGroupSize) * kNpuGroupSize);
  std::set<int32_t> related_npu_ids;
  for (size_t i = 0; i < kNpuGroupSize; ++i) {
    related_npu_ids.insert(group_start + static_cast<int32_t>(i));
  }
  HIXL_LOGI("phy_dev_id=%d, group_start=%d, Related NPU IDs: %s", phy_dev_id, group_start,
            ToString(related_npu_ids).c_str());
  return related_npu_ids;
}

Status BuildNpuRootinfos(const std::set<int32_t> &related_npu_ids, const TopoData &topo_data,
                         std::map<int32_t, NpuRootInfo> &npu_rootinfos) {
  npu_rootinfos.clear();
  for (int32_t npu_id : related_npu_ids) {
    int32_t mesh_die_id = 0;
    int32_t clos_die_id = 0;
    HIXL_CHK_STATUS_RET(ResolveMeshDieIdFromTopo(topo_data, npu_id, mesh_die_id),
                        "[BuildNpuRootinfos] Failed to resolve mesh die from topo, npu_id=%d", npu_id);
    HIXL_CHK_STATUS_RET(ResolveClosDieIdFromTopo(topo_data, npu_id, clos_die_id),
                        "[BuildNpuRootinfos] Failed to resolve CLOS die from topo, npu_id=%d", npu_id);
    NpuRootInfo rootinfo;
    HIXL_CHK_STATUS_RET(BuildNpuRootInfo(npu_id, mesh_die_id, clos_die_id, rootinfo),
                        "Failed to build rootinfo for npu_id=%d", npu_id);
    npu_rootinfos[npu_id] = rootinfo;
  }
  HIXL_LOGI("Built rootinfo for %zu NPU(s)", npu_rootinfos.size());
  return SUCCESS;
}

namespace {
struct ClosPgCollectResult {
  std::string plane_pg_0_eid;
  std::string plane_pg_1_eid;
};

Status CollectClosPgEids(const std::map<int32_t, NpuRootInfo> &npu_rootinfos, int32_t phy_dev_id,
                         ClosPgCollectResult &result) {
  auto self_it = npu_rootinfos.find(phy_dev_id);
  HIXL_CHK_BOOL_RET_STATUS(self_it != npu_rootinfos.end(), FAILED, "[CollectClosPgEids] No rootinfo for phy_dev_id:%d",
                           phy_dev_id);

  // rootinfo_builder already filters: clos_pg_eids[0]=plane_pg_0, [1]=plane_pg_1.
  const auto &pg_eids = self_it->second.clos_pg_eids;
  HIXL_CHK_BOOL_RET_STATUS(!pg_eids.empty(), FAILED, "[CollectClosPgEids] No CLOS PG EID for phy_dev_id:%d",
                           phy_dev_id);
  result.plane_pg_0_eid = pg_eids[0].eid;
  if (pg_eids.size() >= kSecondElementSize) {
    result.plane_pg_1_eid = pg_eids[kPgEidSecondIndex].eid;
  }
  HIXL_LOGI("plane_pg_0_eid=%s die_id=%d, plane_pg_1_eid=%s die_id=%d", result.plane_pg_0_eid.c_str(),
            pg_eids[0].die_id, result.plane_pg_1_eid.empty() ? "(none)" : result.plane_pg_1_eid.c_str(),
            pg_eids.size() >= kSecondElementSize ? pg_eids[kPgEidSecondIndex].die_id : -1);
  return SUCCESS;
}
}  // namespace

Status GenerateD2UEdges(const std::string &plane_pg_0_eid, const std::string &plane_pg_1_eid,
                        std::vector<EndpointConfig> &d2u_edges) {
  if (!plane_pg_0_eid.empty()) {
    EndpointConfig edge;
    edge.protocol = kProtocolUbCtp;
    edge.comm_id = plane_pg_0_eid;
    edge.placement = kPlacementDevice;
    edge.plane = kPlanePg0;
    d2u_edges.push_back(edge);
  }
  if (!plane_pg_1_eid.empty()) {
    EndpointConfig edge;
    edge.protocol = kProtocolUbCtp;
    edge.comm_id = plane_pg_1_eid;
    edge.placement = kPlacementDevice;
    edge.plane = kPlanePg1;
    d2u_edges.push_back(edge);
  }
  return SUCCESS;
}

Status GenerateH2UEdges(const std::string &host_pg_eid, const std::string &plane_pg_0_eid,
                        const std::string &plane_pg_1_eid, std::vector<EndpointConfig> &h2u_edges) {
  // host_pg_eid (8-port PG) is computed in GenerateRouteDataViaDsmi and passed in
  HIXL_CHK_BOOL_RET_STATUS(!host_pg_eid.empty(), FAILED, "[H2U] host_pg_eid is empty");
  HIXL_LOGI("[H2U] Using Host PG EID: %s", host_pg_eid.c_str());
  if (!plane_pg_0_eid.empty()) {
    EndpointConfig edge;
    edge.protocol = kProtocolUbCtp;
    edge.comm_id = host_pg_eid;
    edge.placement = kPlacementHost;
    edge.plane = kPlanePg0;
    h2u_edges.push_back(edge);
  }
  if (!plane_pg_1_eid.empty()) {
    EndpointConfig edge;
    edge.protocol = kProtocolUbCtp;
    edge.comm_id = host_pg_eid;
    edge.placement = kPlacementHost;
    edge.plane = kPlanePg1;
    h2u_edges.push_back(edge);
  }
  return SUCCESS;
}

namespace {
// Collect edges that do not need route_data (D2D direct + D2U gateway); generated first.
Status CollectForwardEdges(const TopoData &topo_data, const std::map<int32_t, NpuRootInfo> &npu_rootinfos,
                           int32_t phy_dev_id, const ClosPgCollectResult &clos_pg,
                           std::vector<EndpointConfig> &all_edges) {
  if (!topo_data.links.empty() && !npu_rootinfos.empty()) {
    std::vector<EndpointConfig> edges;
    HIXL_CHK_STATUS_RET(GenerateD2DEdges(topo_data, npu_rootinfos, phy_dev_id, edges),
                        "[CollectForwardEdges] GenerateD2DEdges failed, phy_dev_id=%d", phy_dev_id);
    all_edges.insert(all_edges.end(), edges.begin(), edges.end());
  }
  if (!clos_pg.plane_pg_0_eid.empty() || !clos_pg.plane_pg_1_eid.empty()) {
    std::vector<EndpointConfig> edges;
    HIXL_CHK_STATUS_RET(GenerateD2UEdges(clos_pg.plane_pg_0_eid, clos_pg.plane_pg_1_eid, edges),
                        "[CollectForwardEdges] GenerateD2UEdges failed, phy_dev_id=%d", phy_dev_id);
    all_edges.insert(all_edges.end(), edges.begin(), edges.end());
  }
  return SUCCESS;
}

// Collect edges that depend on route_data (H2U / H2D / D2H); call only after route_data succeeds.
Status CollectRouteDependentEdges(const RouteGenResult &route_gen_result, int32_t phy_dev_id,
                                  const ClosPgCollectResult &clos_pg, std::vector<EndpointConfig> &all_edges) {
  std::vector<EndpointConfig> edges;
  HIXL_CHK_STATUS_RET(
      GenerateH2UEdges(route_gen_result.host_pg_eid, clos_pg.plane_pg_0_eid, clos_pg.plane_pg_1_eid, edges),
      "[CollectRouteDependentEdges] GenerateH2UEdges failed");
  all_edges.insert(all_edges.end(), edges.begin(), edges.end());

  if (!route_gen_result.route_data.entries.empty()) {
    edges.clear();
    HIXL_CHK_STATUS_RET(GenerateH2DEdges(route_gen_result.route_data, edges),
                        "[CollectRouteDependentEdges] GenerateH2DEdges failed");
    all_edges.insert(all_edges.end(), edges.begin(), edges.end());
    edges.clear();
    HIXL_CHK_STATUS_RET(GenerateD2HEdges(route_gen_result.route_data, phy_dev_id, edges),
                        "[CollectRouteDependentEdges] GenerateD2HEdges failed");
    all_edges.insert(all_edges.end(), edges.begin(), edges.end());
  }
  return SUCCESS;
}

struct LocalCommResBuildCtx {
  int32_t phy_dev_id;
  bool is_server;
  const TopoData &topo_data;
  const std::string &topo_path;
  RouteGenResult &route_gen_result;
  LocalCommResGenerateMode mode;
};

Status CollectLocalCommResEdges(LocalCommResBuildCtx &ctx, std::vector<EndpointConfig> &all_edges) {
  std::map<int32_t, NpuRootInfo> npu_rootinfos;
  HIXL_CHK_STATUS_RET(BuildNpuRootinfos(ctx.route_gen_result.related_npu_ids, ctx.topo_data, npu_rootinfos),
                      "[BuildLocalCommResResult] BuildNpuRootinfos failed, phy_dev_id=%d", ctx.phy_dev_id);

  ClosPgCollectResult clos_pg_result;
  HIXL_CHK_STATUS_RET(CollectClosPgEids(npu_rootinfos, ctx.phy_dev_id, clos_pg_result),
                      "[BuildLocalCommResResult] Failed to collect CLOS PG EIDs, phy_dev_id=%d", ctx.phy_dev_id);

  HIXL_CHK_STATUS_RET(CollectForwardEdges(ctx.topo_data, npu_rootinfos, ctx.phy_dev_id, clos_pg_result, all_edges),
                      "[BuildLocalCommResResult] CollectForwardEdges failed, phy_dev_id=%d", ctx.phy_dev_id);

  // Only kDeviceAndHost (caller passed ub_ctp:host) generates route_data and host edges;
  // route_data is a hard dependency of host edges, so generation failure is fatal.
  if (ctx.mode == LocalCommResGenerateMode::kDeviceAndHost) {
    HIXL_CHK_STATUS_RET(GenerateRouteDataViaDsmi(ctx.phy_dev_id, ctx.topo_path, ctx.is_server, ctx.route_gen_result),
                        "[BuildLocalCommResResult] GenerateRouteDataViaDsmi failed, phy_dev_id=%d", ctx.phy_dev_id);
    HIXL_CHK_STATUS_RET(CollectRouteDependentEdges(ctx.route_gen_result, ctx.phy_dev_id, clos_pg_result, all_edges),
                        "[BuildLocalCommResResult] CollectRouteDependentEdges failed, phy_dev_id=%d", ctx.phy_dev_id);
  }

  HIXL_CHK_BOOL_RET_STATUS(!all_edges.empty(), PARAM_INVALID,
                           "No endpoint edges generated, phy_dev_id=%d, mode=%d, topo_links=%zu", ctx.phy_dev_id,
                           static_cast<int>(ctx.mode), ctx.topo_data.links.size());
  return SUCCESS;
}

Status FillLocalCommResOutput(int32_t phy_dev_id, std::vector<EndpointConfig> &all_edges,
                              LocalCommRes &local_comm_res) {
  std::string net_instance_id;
  HIXL_CHK_STATUS_RET(GetClosNetInstanceId(phy_dev_id, net_instance_id),
                      "[BuildLocalCommResResult] GetClosNetInstanceId failed, phy_dev_id=%d", phy_dev_id);

  local_comm_res.version = "1.3";
  local_comm_res.net_instance_id = net_instance_id;
  local_comm_res.endpoint_list = std::move(all_edges);
  for (auto &ep : local_comm_res.endpoint_list) {
    ep.net_instance_id = net_instance_id;
  }

  HIXL_LOGI("GenerateLocalCommRes result: version=%s, net_instance_id=%s, endpoints=%zu",
            local_comm_res.version.c_str(), local_comm_res.net_instance_id.c_str(),
            local_comm_res.endpoint_list.size());
  LogEndpointList(local_comm_res.endpoint_list);
  return SUCCESS;
}

// Assemble LocalCommRes internally
Status BuildLocalCommResResult(LocalCommResBuildCtx &ctx, LocalCommRes &local_comm_res) {
  // Unload the DCMI handle on every exit path (idempotent)
  HIXL_DISMISSABLE_GUARD(dcmi, []() { DcmiProxy::UnloadDcmi(); });

  std::vector<EndpointConfig> all_edges;
  HIXL_CHK_STATUS_RET(CollectLocalCommResEdges(ctx, all_edges),
                      "[BuildLocalCommResResult] CollectLocalCommResEdges failed, phy_dev_id=%d", ctx.phy_dev_id);
  return FillLocalCommResOutput(ctx.phy_dev_id, all_edges, local_comm_res);
}
}  // namespace

namespace {

Status ResolveTopoPathAndParse(int32_t phy_dev_id, const std::string &topo_path, TopoData &topo_data) {
  std::string resolved_topo_path = topo_path;
  if (resolved_topo_path.empty()) {
    HIXL_CHK_STATUS_RET(ResolveDefaultLocalCommResPaths(phy_dev_id, resolved_topo_path),
                        "[GenerateRouteDataViaDsmi] ResolveDefaultLocalCommResPaths failed, phy_dev_id=%d", phy_dev_id);
  }
  HIXL_CHK_STATUS_RET(ParseTopoFile(resolved_topo_path, topo_data),
                      "[GenerateRouteDataViaDsmi] ParseTopoFile failed, topo_path=%s", resolved_topo_path.c_str());
  return SUCCESS;
}

Status LoadUrmaMapsAndHostPgEid(int32_t phy_dev_id, std::map<std::string, std::string> &ub_name_to_eid,
                                std::string &host_pg_eid) {
  std::string cmd_output;
  HIXL_CHK_STATUS_RET(DefaultUrmaAdminExec("show", cmd_output),
                      "[GenerateRouteDataViaDsmi] Call api:DefaultUrmaAdminExec failed, phy_dev_id=%d", phy_dev_id);
  HIXL_LOGD("[GenerateRouteDataViaDsmi] urma_admin show output: %s", cmd_output.c_str());

  std::vector<UrmaEidEntry> all_entries;
  HIXL_CHK_STATUS_RET(ParseUrmaAdminOutput(cmd_output, all_entries),
                      "[GenerateRouteDataViaDsmi] ParseUrmaAdminOutput failed, phy_dev_id=%d", phy_dev_id);
  HIXL_CHK_STATUS_RET(BuildUbDevNameToEidMap(all_entries, ub_name_to_eid),
                      "[GenerateRouteDataViaDsmi] BuildUbDevNameToEidMap failed, phy_dev_id=%d", phy_dev_id);
  std::map<std::string, std::string> cpu_die_to_pg_eid;
  HIXL_CHK_STATUS_RET(BuildCpuDieToHostPgEidMap(all_entries, cpu_die_to_pg_eid),
                      "[GenerateRouteDataViaDsmi] BuildCpuDieToHostPgEidMap failed, phy_dev_id=%d", phy_dev_id);
  HIXL_CHK_STATUS_RET(ComputeHostPgEid(phy_dev_id, cpu_die_to_pg_eid, host_pg_eid),
                      "[GenerateRouteDataViaDsmi] ComputeHostPgEid failed, phy_dev_id=%d", phy_dev_id);
  return SUCCESS;
}

Status GenerateRouteEntriesForRelatedNpus(const std::set<int32_t> &related_npu_ids, const TopoData &topo_data,
                                          bool is_server, const std::map<std::string, std::string> &ub_name_to_eid,
                                          std::vector<RouteEntry> &entries) {
  for (int32_t npu_id : related_npu_ids) {
    int32_t mesh_die_id = 0;
    HIXL_CHK_STATUS_RET(ResolveMeshDieIdFromTopo(topo_data, npu_id, mesh_die_id),
                        "[GenerateRouteDataViaDsmi] Failed to resolve mesh die from topo for npu_id=%d", npu_id);
    RouteEntry entry;
    HIXL_CHK_STATUS_RET(GenerateRouteEntryForNpu(npu_id, mesh_die_id, is_server, ub_name_to_eid, entry),
                        "[GenerateRouteDataViaDsmi] Failed to generate route entry for npu_id=%d", npu_id);
    entries.push_back(entry);
  }
  return SUCCESS;
}

void LogRouteGenResult(const RouteGenResult &result) {
  HIXL_LOGI("[GenerateRouteDataViaDsmi] Generated %zu route entries:", result.route_data.entries.size());
  for (size_t i = 0; i < result.route_data.entries.size(); ++i) {
    const auto &entry = result.route_data.entries[i];
    HIXL_LOGI("[GenerateRouteDataViaDsmi]   [%zu] device_id=%d, local_eid=[%s], remote_eid=[%s]", i, entry.device_id,
              entry.local_eid.c_str(), entry.remote_eid.c_str());
  }
  HIXL_LOGI("[GenerateRouteDataViaDsmi] 8-port PG host_pg_eid=%s", result.host_pg_eid.c_str());
}

}  // namespace

// Generate route data via DSMI (UB dev name) + urma_admin + DCMI (device EID).
// Replaces the old route.conf + procfs fallback.
// local_eid: PG EID from urma_admin matching the UB dev name (H2D/D2H).
// host_pg_eid: 8-port PG EID (H2U), computed from cpu_die_key.
// Mesh die of each NPU comes from topo fullmesh ports; is_server only selects host EID search.
Status GenerateRouteDataViaDsmi(int32_t phy_dev_id, const std::string &topo_path, bool is_server,
                                RouteGenResult &result) {
  TopoData topo_data;
  HIXL_CHK_STATUS_RET(ResolveTopoPathAndParse(phy_dev_id, topo_path, topo_data),
                      "[GenerateRouteDataViaDsmi] Failed to resolve/parse topo, phy_dev_id=%d", phy_dev_id);

  result.related_npu_ids = CollectRelatedNpuIds(phy_dev_id);
  result.route_data.entries.clear();
  result.host_pg_eid.clear();

  std::map<std::string, std::string> ub_name_to_eid;
  HIXL_CHK_STATUS_RET(LoadUrmaMapsAndHostPgEid(phy_dev_id, ub_name_to_eid, result.host_pg_eid),
                      "[GenerateRouteDataViaDsmi] Failed to load urma maps, phy_dev_id=%d", phy_dev_id);
  HIXL_CHK_STATUS_RET(GenerateRouteEntriesForRelatedNpus(result.related_npu_ids, topo_data, is_server, ub_name_to_eid,
                                                         result.route_data.entries),
                      "[GenerateRouteDataViaDsmi] Failed to generate route entries, phy_dev_id=%d", phy_dev_id);
  LogRouteGenResult(result);
  return SUCCESS;
}

// Parse topo and collect related NPU IDs; route_data is generated on demand in BuildLocalCommResResult.
Status ParseTopoAndCollectNpuIds(int32_t phy_dev_id, const std::string &topo_path, TopoData &topo_data,
                                 RouteGenResult &route_gen_result) {
  HIXL_CHK_STATUS_RET(ParseTopoFile(topo_path, topo_data),
                      "[ParseTopoAndCollectNpuIds] ParseTopoFile failed, phy_dev_id=%d, topo_path=%s", phy_dev_id,
                      topo_path.c_str());
  route_gen_result.related_npu_ids = CollectRelatedNpuIds(phy_dev_id);
  return SUCCESS;
}

Status ResolveDefaultLocalCommResPaths(int32_t phy_dev_id, std::string &topo_path) {
  // 1. Get mainboard_id and pick the topo file from product form.
  uint32_t mainboard_id = 0;
  HIXL_CHK_STATUS_RET(GetMainboardId(phy_dev_id, mainboard_id),
                      "[ResolveDefaultLocalCommResPaths] GetMainboardId failed, phy_dev_id=%d", phy_dev_id);
  topo_path = TopoFileFinder().FindTopoFile(kDefaultTopoDir, mainboard_id);
  HIXL_CHK_BOOL_RET_STATUS(!topo_path.empty(), PARAM_INVALID, "No topo file found for mainboard_id=0x%x in %s",
                           mainboard_id, kDefaultTopoDir);
  return SUCCESS;
}

Status GenerateLocalCommRes(int32_t phy_dev_id, LocalCommRes &local_comm_res) {
  return GenerateLocalCommRes(phy_dev_id, LocalCommResGenerateMode::kDeviceOnly, local_comm_res);
}

Status GenerateLocalCommRes(int32_t phy_dev_id, LocalCommResGenerateMode mode, LocalCommRes &local_comm_res) {
  std::string topo_path;
  HIXL_CHK_STATUS_RET(ResolveDefaultLocalCommResPaths(phy_dev_id, topo_path),
                      "[GenerateLocalCommRes] ResolveDefaultLocalCommResPaths failed, phy_dev_id=%d", phy_dev_id);
  return GenerateLocalCommRes(phy_dev_id, topo_path, mode, local_comm_res);
}

Status GenerateLocalCommRes(int32_t phy_dev_id, const std::string &topo_path, LocalCommRes &local_comm_res) {
  return GenerateLocalCommRes(phy_dev_id, topo_path, LocalCommResGenerateMode::kDeviceOnly, local_comm_res);
}

Status GenerateLocalCommRes(int32_t phy_dev_id, const std::string &topo_path, LocalCommResGenerateMode mode,
                            LocalCommRes &local_comm_res) {
  // 1. Get product form
  uint32_t mainboard_id = 0;
  HIXL_CHK_STATUS_RET(GetMainboardId(phy_dev_id, mainboard_id),
                      "[GenerateLocalCommRes] GetMainboardId failed, phy_dev_id=%d", phy_dev_id);
  // Whether this is a Server product form
  bool is_server = IsProductServer(mainboard_id);

  // 2. Parse topo and collect related NPUs
  TopoData topo_data;
  RouteGenResult route_gen_result;
  HIXL_CHK_STATUS_RET(ParseTopoAndCollectNpuIds(phy_dev_id, topo_path, topo_data, route_gen_result),
                      "[GenerateLocalCommRes] ParseTopoAndCollectNpuIds failed, phy_dev_id=%d, topo_path=%s",
                      phy_dev_id, topo_path.c_str());

  // 3. Assemble result (D2D/D2U first; route_data and host edges only in kDeviceAndHost)
  LocalCommResBuildCtx ctx{phy_dev_id, is_server, topo_data, topo_path, route_gen_result, mode};
  return BuildLocalCommResResult(ctx, local_comm_res);
}

Status TransLocalCommRes(int32_t phy_dev_id, AscendString &result) {
  std::string topo_path;
  HIXL_CHK_STATUS_RET(ResolveDefaultLocalCommResPaths(phy_dev_id, topo_path),
                      "[TransLocalCommRes] ResolveDefaultLocalCommResPaths failed");
  return TransLocalCommRes(phy_dev_id, topo_path, result);
}

Status SerializeLocalCommResJson(const LocalCommRes &local_comm_res, std::string &json_str) {
  nlohmann::json j;
  j["version"] = local_comm_res.version;
  j["net_instance_id"] = local_comm_res.net_instance_id;
  j["endpoint_list"] = nlohmann::json::array();
  for (const auto &ep : local_comm_res.endpoint_list) {
    nlohmann::json ep_json;
    ep_json["protocol"] = ep.protocol;
    ep_json["comm_id"] = ep.comm_id;
    ep_json["placement"] = ep.placement;
    if (!ep.plane.empty()) {
      ep_json["plane"] = ep.plane;
    }
    if (!ep.dst_eid.empty()) {
      ep_json["dst_eid"] = ep.dst_eid;
    }
    if (!ep.net_instance_id.empty()) {
      ep_json["net_instance_id"] = ep.net_instance_id;
    }
    j["endpoint_list"].push_back(std::move(ep_json));
  }

  constexpr int32_t kJsonIndent = 2;
  try {
    json_str = j.dump(kJsonIndent);
  } catch (const nlohmann::json::exception &e) {
    HIXL_CHK_BOOL_RET_STATUS(false, FAILED, "[SerializeLocalCommResJson] Failed to dump JSON: %s", e.what());
  }
  return SUCCESS;
}

Status TransLocalCommRes(int32_t phy_dev_id, const std::string &topo_path, AscendString &result) {
  // 1. Build LocalCommRes via GenerateLocalCommRes
  LocalCommRes local_comm_res;
  HIXL_CHK_STATUS_RET(GenerateLocalCommRes(phy_dev_id, topo_path, local_comm_res),
                      "[TransLocalCommRes] GenerateLocalCommRes failed, phy_dev_id=%d", phy_dev_id);

  // 2. Serialize as 2-space-indented JSON
  std::string json_str;
  HIXL_CHK_STATUS_RET(SerializeLocalCommResJson(local_comm_res, json_str),
                      "[TransLocalCommRes] SerializeLocalCommResJson failed");

  // 3. Return via AscendString (wraps shared_ptr<std::string>; ABI-safe across .so)
  result = AscendString(json_str.c_str());
  return SUCCESS;
}

}  // namespace hixl
