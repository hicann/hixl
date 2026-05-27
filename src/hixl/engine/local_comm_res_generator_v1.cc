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
 * @brief LocalCommRes 生成工具实现文件
 *
 * 本文件负责：
 * - DCMI 接口封装（GetUBEntityList, GetMainboardId）
 * - 文件解析（ParseTopoFile, ParseRouteFile）
 * - 边生成（GenerateD2DEdges, GenerateH2DEdges, GenerateD2HEdges）
 * - 核心流程（GenerateLocalCommRes）
 *
 * RootInfo 构建功能已拆分到 rootinfo_builder 模块
 */

#include "local_comm_res_generator_v1.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <array>
#include <cstring>
#include <set>
#include <dirent.h>
#include <sys/stat.h>
#include "common/hixl_log.h"
#include "common/hixl_utils.h"
#include <nlohmann/json.hpp>

namespace hixl {

// ============ 文件内私有常量与辅助定义 ============

namespace {

// 产品形态常量
constexpr uint32_t kMainboardIdPod1 = 0x3;
constexpr uint32_t kMainboardIdPod2 = 0x5;
constexpr uint32_t kMainboardIdPod3 = 0x7;
constexpr uint32_t kMainboardIdServerMin1 = 0x21;
constexpr uint32_t kMainboardIdServerMax1 = 0x2B;
constexpr uint32_t kMainboardIdServerMin2 = 0x40;
constexpr uint32_t kMainboardIdServerMax2 = 0x46;

// Topology 常量
constexpr const char *kLinkTypePeer2Peer = "PEER2PEER";
constexpr const char *kTopoType1DMesh = "1DMESH";

// Plane 常量
constexpr const char *kPlanePg0 = "plane_pg_0";
constexpr const char *kPlanePg1 = "plane_pg_1";

// 网络实例前缀
constexpr const char *kNetInstancePrefix = "superpod_";

// 默认路径常量
constexpr const char *kDefaultTopoDir = "/etc/";
constexpr const char *kDefaultTopoSuffix = "noroce.json";
constexpr const char *kDefaultRoutePath = "/lib/route.conf";

// 文件路径校验辅助函数
inline bool IsFileExists(const std::string &path) {
  struct stat buffer;
  return (stat(path.c_str(), &buffer) == 0);
}

// 魔法数字常量
constexpr size_t kHexPrefixLength = 2;        // "0x" 前缀长度
constexpr size_t kNpuGroupSize = 8;          // NPU 分组大小
constexpr size_t kPgEidSecondIndex = 1;      // PG EID 第二索引
constexpr size_t kSecondElementSize = 2;     // 第二元素 size 检查
constexpr uint32_t kOddParity = 1;           // 奇校验
constexpr uint32_t kEvenParity = 0;          // 偶校验

// DCMI 主命令和子命令
enum class DcmiMainCmd {
  CHIP_INF = 12,
};

enum class DcmiChipInfoSubCmd {
  SPOD_INFO = 1,
};

// D2D 边匹配输入参数封装（入参过多，合并为结构体）
struct D2DEdgeMatchInput {
  const NpuRootInfo &self_rootinfo;
  const NpuRootInfo &peer_rootinfo;
  int32_t peer_id;
  const std::vector<std::string> &local_ports;
  const std::vector<std::string> &peer_ports;
};

// 边收集输入参数封装（入参过多，合并为结构体）
struct EdgeCollectInput {
  const TopoData &topo_data;
  const RouteData &route_data;
  const std::map<int32_t, NpuRootInfo> &npu_rootinfos;
  int32_t phy_dev_id;
  const std::string &plane_pg_0_eid;
  const std::string &plane_pg_1_eid;
};

// 在 /etc/ 目录下模糊匹配以 noroce.json 结尾的文件，返回修改时间最新的一个
std::string FindLatestTopoFile() {
  DIR *dir = opendir(kDefaultTopoDir);
  if (dir == nullptr) {
    HIXL_LOGW("Failed to open /etc/ for topo file scan");
    return "";
  }

  std::string latest_file;
  time_t latest_mtime = 0;
  struct dirent *entry = nullptr;
  while ((entry = readdir(dir)) != nullptr) {
    std::string name(entry->d_name);
    if (name.size() < std::strlen(kDefaultTopoSuffix)) {
      continue;
    }
    if (name.compare(name.size() - std::strlen(kDefaultTopoSuffix), std::strlen(kDefaultTopoSuffix),
                     kDefaultTopoSuffix) != 0) {
      continue;
    }

    std::string full_path = std::string(kDefaultTopoDir) + name;
    struct stat st;
    if (stat(full_path.c_str(), &st) == 0) {
      if (st.st_mtime > latest_mtime) {
        latest_mtime = st.st_mtime;
        latest_file = full_path;
      }
    }
  }
  closedir(dir);
  return latest_file;
}

}  // anonymous namespace

// ============ 文件解析辅助函数 ============

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
    } catch (...) {
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
      // 去掉 "0x" 前缀
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

int32_t BuildRouteEntries(const std::map<std::string, std::string> &kv_map, RouteData &route_data) {
  auto it = kv_map.find("pair_device_num");
  if (it == kv_map.end()) {
    HIXL_LOGE(FAILED, "Missing pair_device_num");
    return FAILED;
  }

  int32_t pair_device_num = 0;
  try {
    pair_device_num = std::stoi(it->second);
  } catch (...) {
    HIXL_LOGE(FAILED, "Invalid pair_device_num value: %s", it->second.c_str());
    return FAILED;
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
    } catch (...) {
      HIXL_LOGW("Invalid dev_id value: %s", dev_it->second.c_str());
      continue;
    }
    AddRouteEntriesForDevice(kv_map, i, device_id, route_data);
  }

  HIXL_LOGI("Parsed %zu route entries", route_data.entries.size());
  return SUCCESS;
}

// ============ DCMI 接口封装实现 ============

int32_t GetMainboardId(int32_t phy_dev_id, unsigned int &mainboard_id) {
  if (DcmiProxy::LoadDcmi() != 0) {
    HIXL_LOGE(FAILED, "DCMI not loaded");
    return FAILED;
  }

  uint32_t logic_id = 0;
  if (DcmiProxy::GetLogicIdFromPhyId(phy_dev_id, &logic_id) != 0) {
    HIXL_LOGE(FAILED, "Failed to get logic id from phy id: %d", phy_dev_id);
    return FAILED;
  }

  int32_t ret = DcmiProxy::GetMainboardId(logic_id, &mainboard_id);
  if (ret != 0) {
    HIXL_LOGE(FAILED, "Failed to get mainboard id, ret=%d", ret);
    return FAILED;
  }

  return SUCCESS;
}

int32_t GetClosNetInstanceId(int32_t phy_dev_id, std::string &net_instance_id) {
  if (DcmiProxy::LoadDcmi() != 0) {
    HIXL_LOGE(FAILED, "DCMI not loaded");
    return FAILED;
  }

  uint32_t logic_id = 0;
  if (DcmiProxy::GetLogicIdFromPhyId(phy_dev_id, &logic_id) != 0) {
    HIXL_LOGE(FAILED, "Failed to get logic id from phy id: %d", phy_dev_id);
    return FAILED;
  }

  DcmiSpodInfo spod_info = {};
  uint32_t buf_size = sizeof(DcmiSpodInfo);
  int32_t ret = DcmiProxy::GetDeviceInfo(logic_id, static_cast<int32_t>(DcmiMainCmd::CHIP_INF),
                                         static_cast<int32_t>(DcmiChipInfoSubCmd::SPOD_INFO), &spod_info, &buf_size);
  if (ret != 0) {
    HIXL_LOGE(FAILED, "Failed to get device info, ret=%d", ret);
    return FAILED;
  }

  net_instance_id = std::string(kNetInstancePrefix) + std::to_string(spod_info.super_pod_id);
  HIXL_LOGI("phy_dev_id=%d, super_pod_id=%u, net_instance_id=%s", phy_dev_id, spod_info.super_pod_id,
            net_instance_id.c_str());
  return SUCCESS;
}

// ============ 文件解析实现 ============

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
    return 1;  // 1 表示跳过
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
  return 0;  // 0 表示成功
}

static int32_t ParseTopoJson(const std::string &topo_path, nlohmann::json &j) {
  if (!IsFileExists(topo_path)) {
    HIXL_LOGE(PARAM_INVALID, "Topo file does not exist: %s", topo_path.c_str());
    return PARAM_INVALID;
  }
  std::ifstream file(topo_path);
  if (!file.is_open()) {
    HIXL_LOGE(PARAM_INVALID, "Failed to open topo file: %s", topo_path.c_str());
    return PARAM_INVALID;
  }

  try {
    file >> j;
  } catch (const nlohmann::json::exception &e) {
    HIXL_LOGE(FAILED, "Failed to parse topo file JSON: %s", e.what());
    return FAILED;
  }
  file.close();

  if (!j.contains("edge_list") || !j["edge_list"].is_array() || j["edge_list"].empty()) {
    HIXL_LOGE(FAILED, "No or empty edge_list found in: %s", topo_path.c_str());
    return FAILED;
  }
  return SUCCESS;
}

int32_t ParseTopoFile(const std::string &topo_path, TopoData &topo_data) {
  topo_data.links.clear();

  nlohmann::json j;
  int32_t ret = ParseTopoJson(topo_path, j);
  if (ret != SUCCESS) {
    return ret;
  }

  for (const auto &edge : j["edge_list"]) {
    TopoLink link;
    link.remote_a = -1;
    link.remote_b = -1;

    if (ParseSingleLink(edge, link) == 1) {
      continue;  // 跳过该 edge
    }
    topo_data.links.push_back(link);
  }

  HIXL_LOGI("Parsed %zu links from %s", topo_data.links.size(), topo_path.c_str());
  return SUCCESS;
}

int32_t ParseRouteFile(const std::string &route_path, RouteData &route_data) {
  route_data.entries.clear();
  if (!IsFileExists(route_path)) {
    HIXL_LOGE(PARAM_INVALID, "Route file does not exist: %s", route_path.c_str());
    return PARAM_INVALID;
  }
  std::ifstream file(route_path);
  if (!file.is_open()) {
    HIXL_LOGE(PARAM_INVALID, "Failed to open route file: %s", route_path.c_str());
    return PARAM_INVALID;
  }

  std::map<std::string, std::string> kv_map;
  if (!LoadRouteKvMap(file, kv_map)) {
    HIXL_LOGE(FAILED, "Failed to load route kv map");
    return FAILED;
  }
  file.close();

  return BuildRouteEntries(kv_map, route_data);
}

// ============ 边生成实现 ============

bool ShouldSkipD2DLink(const TopoLink &link, std::array<size_t, 4> &skip_reason) {
  if (link.net_layer != 0) {
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

void AddD2DEdgesFromLink(const D2DEdgeMatchInput &input, std::vector<EndpointConfig> &edges,
                         size_t &no_port_match_local, size_t &no_port_match_peer) {
  for (size_t i = 0; i < input.local_ports.size() && i < input.peer_ports.size(); ++i) {
    const std::string &local_port = input.local_ports[i];
    const std::string &peer_port = input.peer_ports[i];

    auto local_eid_it = input.self_rootinfo.port_to_eid.find(local_port);
    if (local_eid_it == input.self_rootinfo.port_to_eid.end()) {
      ++no_port_match_local;
      HIXL_LOGD("No EID for local port '%s'", local_port.c_str());
      continue;
    }
    const std::string &comm_id = local_eid_it->second;

    auto peer_eid_it = input.peer_rootinfo.port_to_eid.find(peer_port);
    if (peer_eid_it == input.peer_rootinfo.port_to_eid.end()) {
      ++no_port_match_peer;
      HIXL_LOGD("No EID for peer port '%s' on npu_id=%d", peer_port.c_str(), input.peer_id);
      continue;
    }
    const std::string &dst_eid = peer_eid_it->second;

    EndpointConfig edge{};
    edge.protocol = kProtocolUbCtp;
    edge.comm_id = comm_id;
    edge.placement = kPlacementDevice;
    edge.dst_eid = dst_eid;
    edges.push_back(edge);
    HIXL_LOGD("D2D matched: comm_id=%s, dst_eid=%s", comm_id.c_str(), dst_eid.c_str());
  }
}

int32_t GenerateD2DEdges(const TopoData &topo_data, const std::map<int32_t, NpuRootInfo> &npu_rootinfos, int32_t phy_id,
                         std::vector<EndpointConfig> &edges) {
  edges.clear();

  auto self_it = npu_rootinfos.find(phy_id);
  if (self_it == npu_rootinfos.end()) {
    HIXL_LOGW("No rootinfo for self npu_id=%d", phy_id);
    return SUCCESS;
  }
  const auto &self_rootinfo = self_it->second;

  HIXL_LOGI("D2D: phy_id=%d, topo_links=%zu, self_rootinfo_size=%zu", phy_id, topo_data.links.size(),
            self_rootinfo.port_to_eid.size());

  std::array<size_t, 4> skip_reason = {0, 0, 0, 0};
  size_t no_rootinfo_peer = 0;
  size_t no_port_match_local = 0;
  size_t no_port_match_peer = 0;

  for (const auto &link : topo_data.links) {
    if (ShouldSkipD2DLink(link, skip_reason)) {
      continue;
    }

    bool is_local_a_side = (link.local_a == phy_id);
    bool is_local_b_side = (link.local_b == phy_id);
    if (!is_local_a_side && !is_local_b_side) {
      ++skip_reason[3];
      continue;
    }

    int32_t peer_id = is_local_a_side ? link.local_b : link.local_a;
    const std::vector<std::string> &local_ports = is_local_a_side ? link.local_a_ports : link.local_b_ports;
    const std::vector<std::string> &peer_ports = is_local_a_side ? link.local_b_ports : link.local_a_ports;

    if (local_ports.empty() || peer_ports.empty()) {
      HIXL_LOGD("D2D skip (empty ports)");
      continue;
    }

    auto peer_it = npu_rootinfos.find(peer_id);
    if (peer_it == npu_rootinfos.end()) {
      ++no_rootinfo_peer;
      HIXL_LOGD("D2D No rootinfo for peer npu_id=%d", peer_id);
      continue;
    }
    const auto &peer_rootinfo = peer_it->second;

    AddD2DEdgesFromLink({self_rootinfo, peer_rootinfo, peer_id, local_ports, peer_ports}, edges, no_port_match_local,
                        no_port_match_peer);
  }

  HIXL_LOGI(
      "D2D result: matched=%zu, skip(net_layer)=%zu, skip(link_type)=%zu, "
      "skip(topo_type)=%zu, skip(phy_id)=%zu, no_port_match_local=%zu, "
      "no_port_match_peer=%zu, no_rootinfo_peer=%zu",
      edges.size(), skip_reason[0], skip_reason[1], skip_reason[2], skip_reason[3], no_port_match_local,
      no_port_match_peer, no_rootinfo_peer);
  return SUCCESS;
}

int32_t GenerateH2DEdges(const RouteData &route_data, std::vector<EndpointConfig> &edges) {
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

int32_t GenerateD2HEdges(const RouteData &route_data, int32_t phy_dev_id, std::vector<EndpointConfig> &edges) {
  edges.clear();

  HIXL_LOGI("D2H: route_entries=%zu, phy_dev_id=%d", route_data.entries.size(), phy_dev_id);

  for (const auto &entry : route_data.entries) {
    if (entry.device_id != (phy_dev_id % 8)) {
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

// ============ 核心接口实现 ============

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

bool IsProductServer(uint32_t mainboard_id) {
  return (
      (mainboard_id >= kMainboardIdServerMin1 && mainboard_id <= kMainboardIdServerMax1 && (mainboard_id % 2 == kOddParity)) ||
      (mainboard_id >= kMainboardIdServerMin2 && mainboard_id <= kMainboardIdServerMax2 && (mainboard_id % 2 == kEvenParity)));
}

std::set<int32_t> CollectRelatedNpuIds(int32_t phy_dev_id) {
  // NPU 按 kNpuGroupSize 个一组划分，找出 phy_dev_id 所在组的所有 NPU
  int32_t group_start = static_cast<int32_t>((phy_dev_id / kNpuGroupSize) * kNpuGroupSize);
  std::set<int32_t> related_npu_ids;
  for (size_t i = 0; i < kNpuGroupSize; ++i) {
    related_npu_ids.insert(group_start + static_cast<int32_t>(i));
  }
  HIXL_LOGI("phy_dev_id=%d, group_start=%d, Related NPU IDs: %s", phy_dev_id, group_start,
            ToString(related_npu_ids).c_str());
  return related_npu_ids;
}

int32_t BuildNpuRootinfos(const std::set<int32_t> &related_npu_ids, bool is_server,
                          std::map<int32_t, NpuRootInfo> &npu_rootinfos) {
  npu_rootinfos.clear();
  for (int32_t npu_id : related_npu_ids) {
    NpuRootInfo rootinfo;
    int32_t build_ret = BuildNpuRootInfo(npu_id, is_server, rootinfo);
    if (build_ret != SUCCESS) {
      HIXL_LOGE(FAILED, "Failed to build rootinfo for npu_id=%d, ret=%d", npu_id, build_ret);
      return build_ret;
    }
    npu_rootinfos[npu_id] = rootinfo;
  }
  HIXL_LOGI("Built rootinfo for %zu NPU(s)", npu_rootinfos.size());
  return SUCCESS;
}

void CollectClosPgEids(const std::map<int32_t, NpuRootInfo> &npu_rootinfos, int32_t phy_dev_id, bool is_server,
                       std::string &plane_pg_0_eid, std::string &plane_pg_1_eid, int32_t &mesh_die_id) {
  mesh_die_id = GetMeshDieId(phy_dev_id, is_server);
  auto self_it = npu_rootinfos.find(phy_dev_id);
  if (self_it == npu_rootinfos.end()) {
    return;
  }

  // rootinfo_builder 已按正确逻辑过滤：clos_pg_eids[0]=plane_pg_0, [1]=plane_pg_1
  const auto &pg_eids = self_it->second.clos_pg_eids;
  if (!pg_eids.empty()) {
    plane_pg_0_eid = pg_eids[0].eid;
  }
  if (pg_eids.size() >= kSecondElementSize) {
    plane_pg_1_eid = pg_eids[kPgEidSecondIndex].eid;
  }
  HIXL_LOGI("Mesh die_id=%d", mesh_die_id);
  HIXL_LOGI("plane_pg_0_eid=%s", plane_pg_0_eid.empty() ? "(none)" : plane_pg_0_eid.c_str());
  HIXL_LOGI("plane_pg_1_eid=%s", plane_pg_1_eid.empty() ? "(none)" : plane_pg_1_eid.c_str());
}

void GenerateD2UEdges(const std::string &plane_pg_0_eid, const std::string &plane_pg_1_eid,
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
}

void GenerateH2UEdges(const std::string &plane_pg_0_eid, const std::string &plane_pg_1_eid,
                      std::vector<EndpointConfig> &h2u_edges) {
  if (!plane_pg_0_eid.empty()) {
    EndpointConfig edge;
    edge.protocol = kProtocolUbCtp;
    edge.comm_id = plane_pg_0_eid;
    edge.placement = kPlacementHost;
    edge.plane = kPlanePg0;
    h2u_edges.push_back(edge);
  }
  if (!plane_pg_1_eid.empty()) {
    EndpointConfig edge;
    edge.protocol = kProtocolUbCtp;
    edge.comm_id = plane_pg_1_eid;
    edge.placement = kPlacementHost;
    edge.plane = kPlanePg1;
    h2u_edges.push_back(edge);
  }
}

void CollectAllEdges(const EdgeCollectInput &input, std::vector<EndpointConfig> &all_edges) {
  if (!input.topo_data.links.empty() && !input.npu_rootinfos.empty()) {
    std::vector<EndpointConfig> edges;
    GenerateD2DEdges(input.topo_data, input.npu_rootinfos, input.phy_dev_id, edges);
    all_edges.insert(all_edges.end(), edges.begin(), edges.end());
  }
  if (!input.plane_pg_0_eid.empty() || !input.plane_pg_1_eid.empty()) {
    std::vector<EndpointConfig> edges;
    GenerateD2UEdges(input.plane_pg_0_eid, input.plane_pg_1_eid, edges);
    all_edges.insert(all_edges.end(), edges.begin(), edges.end());
    edges.clear();
    GenerateH2UEdges(input.plane_pg_0_eid, input.plane_pg_1_eid, edges);
    all_edges.insert(all_edges.end(), edges.begin(), edges.end());
  }
  if (!input.route_data.entries.empty()) {
    std::vector<EndpointConfig> edges;
    GenerateH2DEdges(input.route_data, edges);
    all_edges.insert(all_edges.end(), edges.begin(), edges.end());
    edges.clear();
    GenerateD2HEdges(input.route_data, input.phy_dev_id, edges);
    all_edges.insert(all_edges.end(), edges.begin(), edges.end());
  }
}

int32_t GenerateLocalCommRes(int32_t phy_dev_id, LocalCommRes &local_comm_res) {
  // 1. 使用默认路径
  std::string topo_path = FindLatestTopoFile();
  if (topo_path.empty()) {
    HIXL_LOGE(PARAM_INVALID, "No topo file found in /etc/");
    return PARAM_INVALID;
  }
  std::string route_path = kDefaultRoutePath;
  return GenerateLocalCommRes(phy_dev_id, topo_path, route_path, local_comm_res);
}

int32_t GenerateLocalCommRes(int32_t phy_dev_id, const std::string &topo_path, const std::string &route_path,
                             LocalCommRes &local_comm_res) {
  // 1. 获取产品信息
  uint32_t mainboard_id = 0;
  int32_t ret = GetMainboardId(phy_dev_id, mainboard_id);
  if (ret != SUCCESS) {
    return ret;
  }
  bool is_server = IsProductServer(mainboard_id);
  // 3. 解析文件
  TopoData topo_data;
  ret = ParseTopoFile(topo_path, topo_data);
  if (ret != SUCCESS) {
    return ret;
  }
  RouteData route_data;
  ret = ParseRouteFile(route_path, route_data);
  if (ret != SUCCESS) {
    return ret;
  }

  // 4. 构建 NpuRootInfo
  std::set<int32_t> related_npu_ids = CollectRelatedNpuIds(phy_dev_id);
  std::map<int32_t, NpuRootInfo> npu_rootinfos;
  ret = BuildNpuRootinfos(related_npu_ids, is_server, npu_rootinfos);
  if (ret != SUCCESS) {
    return ret;
  }

  std::string plane_pg_0_eid, plane_pg_1_eid;
  int32_t mesh_die_id = 0;
  CollectClosPgEids(npu_rootinfos, phy_dev_id, is_server, plane_pg_0_eid, plane_pg_1_eid, mesh_die_id);

  // 5. 生成所有边
  std::vector<EndpointConfig> all_edges;
  CollectAllEdges({topo_data, route_data, npu_rootinfos, phy_dev_id, plane_pg_0_eid, plane_pg_1_eid}, all_edges);
  if (all_edges.empty()) {
    return PARAM_INVALID;
  }

  // 6. 获取 net_instance_id 并组装结果
  std::string net_instance_id;
  ret = GetClosNetInstanceId(phy_dev_id, net_instance_id);
  if (ret != SUCCESS) {
    return ret;
  }

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

  DcmiProxy::UnloadDcmi();
  return SUCCESS;
}

}  // namespace hixl
