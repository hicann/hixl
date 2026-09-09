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
 * @file local_comm_res_tool.h
 * @brief LocalCommRes generator header
 *
 * Generates local communication resource info during HIXL initialization.
 * Supports Mesh and CLOS layer EID/port collection.
 *
 * DCMI APIs are loaded from libdcmi.so via dlopen to avoid a direct dependency on hal.h.
 */

#ifndef CANN_HIXL_SRC_HIXL_ENGINE_HIXL_LOCAL_COMM_RES_TOOL_H
#define CANN_HIXL_SRC_HIXL_ENGINE_HIXL_LOCAL_COMM_RES_TOOL_H

#include <string>
#include <vector>
#include <map>
#include <set>

// Data structures from rootinfo_builder
#include "rootinfo_builder_generator_v1.h"
// EndpointConfig definition
#include "common/hixl_inner_types.h"
// AscendString is aliased via hixl_types.h, included indirectly through hixl_inner_types.h (hixl::AscendString)

namespace hixl {

enum class LocalCommResGenerateMode {
  kDeviceOnly,
  kDeviceAndHost,
};

// ============ Endpoint config ============
// EndpointConfig is provided by hixl_inner_types.h

/**
 * @brief LocalCommRes structure (replaces JSON output)
 */
struct LocalCommRes {
  std::string version;                        // Version string, default "1.3"
  std::string net_instance_id;                // Network instance ID
  std::string server_id;                      // Server ID (host UB only; empty when unused)
  std::vector<EndpointConfig> endpoint_list;  // Endpoint list
};

// ============ Topology data ============

/**
 * @brief Topology link
 */
struct TopoLink {
  int32_t net_layer = 0;                   // Network layer (0: Mesh, 1: CLOS)
  std::string link_type;                   // Link type: PEER2PEER, PEER2NET
  std::string topo_type;                   // Topology type: 1DMESH, CLOS
  int32_t local_a = 0;                     // Local node A
  int32_t local_b = 0;                     // Local node B
  int32_t remote_a = -1;                   // Remote node A
  int32_t remote_b = -1;                   // Remote node B
  std::vector<std::string> local_a_ports;  // Local A ports (serial-port ids such as "die_id/port")
  std::vector<std::string> local_b_ports;  // Local B ports
};

struct TopoData {
  std::vector<TopoLink> links;
};

// ============ Route data ============

/**
 * @brief Route entry
 */
struct RouteEntry {
  int32_t device_id = 0;   // Device ID
  std::string local_eid;   // Local EID
  std::string remote_eid;  // Remote EID
};

struct RouteData {
  std::vector<RouteEntry> entries;
};

/**
 * @brief Result of route data generation
 * Holds route_data and the host_pg_eid produced in the same pass (8-port PG EID for H2U).
 */
struct RouteGenResult {
  RouteData route_data;
  std::set<int32_t> related_npu_ids;
  std::string host_pg_eid;                               // 8-port PG EID for H2U
  std::map<std::string, std::string> cpu_die_to_pg_eid;  // Host 8-port PG map (cpu_die_key -> PG EID)
};

// ============ Core APIs ============

/**
 * @brief Generate route_data and host_pg_eid via DSMI + urma_admin + DCMI
 *
 * Parses the topo file internally. Mesh die of each NPU is taken from fullmesh ports;
 * NPU set is the ACL-visible phy ids; is_server only selects the host EID search strategy.
 * When topo_path is empty, the default topo path is resolved from phy_dev_id first.
 *
 * @param [in] phy_dev_id Physical device ID
 * @param [in] topo_path Topology JSON path; empty string means use the default topo
 * @param [in] is_server Whether the product form is Server (host EID search strategy)
 * @param [out] result route_data + related_npu_ids + host_pg_eid
 * @return SUCCESS on success, other error codes on failure
 */
Status GenerateRouteDataViaDsmi(int32_t phy_dev_id, const std::string &topo_path, bool is_server,
                                RouteGenResult &result);

/**
 * @brief Generate LocalCommRes (production API, default paths)
 * @param [in] phy_dev_id Physical device ID from aclrtGetPhyDevIdByUserDevId
 * @param [in] mode Device-only or device+host generation
 * @param [out] local_comm_res Output LocalCommRes
 * @return SUCCESS on success, other error codes on failure
 */
Status GenerateLocalCommRes(int32_t phy_dev_id, LocalCommResGenerateMode mode, LocalCommRes &local_comm_res);

/**
 * @brief Generate LocalCommRes (test overload that injects a topo path)
 * @param [in] phy_dev_id Physical device ID
 * @param [in] topo_path Topology file path
 * @param [in] mode Device-only or device+host generation
 * @param [out] local_comm_res Output LocalCommRes
 * @return SUCCESS on success, other error codes on failure
 *
 * route_data is generated via DSMI + urma_admin + DCMI when mode is kDeviceAndHost.
 */
Status GenerateLocalCommRes(int32_t phy_dev_id, const std::string &topo_path, LocalCommResGenerateMode mode,
                            LocalCommRes &local_comm_res);

Status GenerateLocalCommRes(int32_t phy_dev_id, LocalCommResGenerateMode mode, const std::string &user_local_comm_res,
                            LocalCommRes &local_comm_res);

/**
 * @brief Generate LocalCommRes and honor a user-provided top-level server_id when present
 * @param [in] user_local_comm_res User OPTION_LOCAL_COMM_RES JSON; empty means not provided
 */
Status GenerateLocalCommRes(int32_t phy_dev_id, const std::string &topo_path, LocalCommResGenerateMode mode,
                            const std::string &user_local_comm_res, LocalCommRes &local_comm_res);

/**
 * @brief Internal helper: resolve the default topo path from mainboard_id
 *
 * Shared by GenerateLocalCommRes default-path overloads and 2-arg TransLocalCommRes.
 * **Internal use only**; callers must pass a valid phy_dev_id.
 *
 * @param [in]  phy_dev_id Physical device ID
 * @param [out] topo_path  Full path of the matched topo file in the default topo directory
 * @return SUCCESS on success; GetMainboardId error or PARAM_INVALID on failure
 */
Status ResolveDefaultLocalCommResPaths(int32_t phy_dev_id, std::string &topo_path);

/**
 * @brief Generate LocalCommRes as a JSON string (used by the lcrgen tool)
 *
 * Runs GenerateLocalCommRes (default-path overload), serializes LocalCommRes as
 * 2-space-indented JSON, and returns it via AscendString. **All std::string
 * handling stays inside libcann_hixl.so**; the caller only holds AscendString
 * (which wraps shared_ptr<std::string>), so the .so boundary stays ABI-safe.
 *
 * @param [in] phy_dev_id Physical device ID
 * @param [out] result JSON string on success; undefined on failure
 * @return SUCCESS on success, other error codes on failure
 */
Status TransLocalCommRes(int32_t phy_dev_id, AscendString &result);

/**
 * @brief Generate LocalCommRes as a JSON string (test overload that injects a topo path)
 *
 * @param [in] phy_dev_id Physical device ID
 * @param [in] topo_path Topology file path
 * @param [out] result JSON string on success; undefined on failure
 * @return SUCCESS on success, other error codes on failure
 */
Status TransLocalCommRes(int32_t phy_dev_id, const std::string &topo_path, AscendString &result);

/**
 * @brief Serialize LocalCommRes to a 2-space-indented JSON string
 *
 * Shared by external tools such as hixl_tool and internal serialization.
 *
 * @param [in] local_comm_res LocalCommRes to serialize
 * @param [out] json_str Serialized JSON string
 * @return SUCCESS on success, other error codes on failure
 */
Status SerializeLocalCommResJson(const LocalCommRes &local_comm_res, std::string &json_str);

// ============ DCMI wrappers ============

/**
 * @brief Get mainboard ID
 * @param [in] phy_dev_id Physical device ID
 * @param [out] mainboard_id Mainboard ID
 * @return SUCCESS on success, other error codes on failure
 */
Status GetMainboardId(int32_t phy_dev_id, uint32_t &mainboard_id);

/**
 * @brief Get CLOS-layer net_instance_id
 * @param [in] phy_dev_id Physical device ID
 * @param [out] net_instance_id Output net_instance_id, format "superpod_{super_pod_id}"
 * @return SUCCESS on success, other error codes on failure
 */
Status GetClosNetInstanceId(int32_t phy_dev_id, std::string &net_instance_id);

// ============ File parsing ============

/**
 * @brief Parse a topology file
 * @param [in] topo_path Topology file path
 * @param [out] topo_data Parsed topology data
 * @return SUCCESS on success, other error codes on failure
 */
Status ParseTopoFile(const std::string &topo_path, TopoData &topo_data);

// ============ Edge generation ============

/**
 * @brief Generate D2D direct edges (Device to Device)
 * @param [in] topo_data Topology data
 * @param [in] npu_rootinfos Rootinfo map of related NPUs
 * @param [in] phy_id Current NPU physical ID
 * @param [out] edges Generated edges
 * @return SUCCESS on success, other error codes on failure
 */
Status GenerateD2DEdges(const TopoData &topo_data, const std::map<int32_t, NpuRootInfo> &npu_rootinfos, int32_t phy_id,
                        std::vector<EndpointConfig> &edges);

/**
 * @brief Generate D2U indirect edges (Device to UB Gateway)
 * @param [in] plane_pg_0_eid plane_pg_0 EID
 * @param [in] plane_pg_1_eid plane_pg_1 EID
 * @param [out] d2u_edges Generated edges
 * @return SUCCESS on success, other error codes on failure
 */
Status GenerateD2UEdges(const std::string &plane_pg_0_eid, const std::string &plane_pg_1_eid,
                        std::vector<EndpointConfig> &d2u_edges);

/**
 * @brief Generate H2D direct edges (Host to Device)
 * @param [in] route_data Route data
 * @param [out] edges Generated edges
 * @return SUCCESS on success, other error codes on failure
 */
Status GenerateH2DEdges(const RouteData &route_data, std::vector<EndpointConfig> &edges);

/**
 * @brief Generate D2H direct edges (Device to Host)
 * @param [in] route_data Route data
 * @param [in] phy_dev_id Current NPU physical ID; only matching device_id entries are used
 * @param [out] edges Generated edges
 * @return SUCCESS on success, other error codes on failure
 */
Status GenerateD2HEdges(const RouteData &route_data, int32_t phy_dev_id, std::vector<EndpointConfig> &edges);

/**
 * @brief Generate H2U indirect edges (Host to UB Gateway)
 * @param [in] host_pg_eid Host 8-port PG EID (from GenerateRouteDataViaDsmi)
 * @param [in] plane_pg_0_eid plane_pg_0 EID
 * @param [in] plane_pg_1_eid plane_pg_1 EID
 * @param [out] h2u_edges Generated edges
 * @return SUCCESS on success, other error codes on failure
 */
Status GenerateH2UEdges(const std::string &host_pg_eid, const std::string &plane_pg_0_eid,
                        const std::string &plane_pg_1_eid, std::vector<EndpointConfig> &h2u_edges);

// ============ TopoFileFinder ============

/**
 * @brief Topology file finder
 * Finds a matching topo file in a directory based on product form (mainboard_id)
 */
class TopoFileFinder {
 public:
  TopoFileFinder();
  ~TopoFileFinder();

  /**
   * @brief Find a matching topo file by mainboard_id
   * @param [in] topo_dir Topology file directory
   * @param [in] mainboard_id Mainboard ID
   * @return Matched topo file path, or empty string on failure
   */
  static std::string FindTopoFile(const std::string &topo_dir, uint32_t mainboard_id);

  /**
   * @brief Whether mainboard_id is a Server product form
   */
  static bool IsProductServer(uint32_t mainboard_id);

 private:
  static bool MatchProductForm(uint32_t mainboard_id, std::string &topo_file_name);
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_ENGINE_HIXL_LOCAL_COMM_RES_TOOL_H
