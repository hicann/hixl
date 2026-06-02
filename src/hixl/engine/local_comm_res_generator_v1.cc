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
#include <memory>
#include <sstream>
#include <algorithm>
#include <array>
#include <cstring>
#include <cerrno>
#include <cstdio>
#include <set>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include "common/hixl_log.h"
#include "common/hixl_utils.h"
#include <nlohmann/json.hpp>
#include "mmpa/mmpa_api.h"

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
constexpr const char *kDefaultTopoDir = "/usr/local/Ascend/driver/topo/950/";
constexpr const char *kDefaultRoutePath = "/lib/route.conf";

// 文件路径校验辅助函数
inline bool IsFileExists(const std::string &path) {
  struct stat buffer;
  return (stat(path.c_str(), &buffer) == 0);
}

// 魔法数字常量
constexpr size_t kHexPrefixLength = 2;    // "0x" 前缀长度
constexpr size_t kNpuGroupSize = 8;       // NPU 分组大小
constexpr size_t kJsonExtLen = 5;         // ".json" 长度
constexpr size_t kPgEidSecondIndex = 1;   // PG EID 第二索引
constexpr size_t kSecondElementSize = 2;  // 第二元素 size 检查
constexpr uint32_t kOddParity = 1;        // 奇校验
constexpr uint32_t kEvenParity = 0;       // 偶校验

// 产品形态判断函数
inline bool IsProductServer(uint32_t mainboard_id) {
  return ((mainboard_id >= kMainboardIdServerMin1 && mainboard_id <= kMainboardIdServerMax1 &&
           (mainboard_id % 2 == kOddParity)) ||
          (mainboard_id >= kMainboardIdServerMin2 && mainboard_id <= kMainboardIdServerMax2 &&
           (mainboard_id % 2 == kEvenParity)));
}

// topo 文件名前缀
constexpr const char *kTopoPrefixAtlas950 = "atlas_950_";
constexpr const char *kTopoPrefixAtlas850 = "atlas_850_";

// Procfs 路径常量
constexpr const char *kProcPathAscendUb = "/proc/ascend_ub";
constexpr const char *kProcPathAsdrvUb = "/proc/asdrv_ub";

// urma_admin 命令路径常量
constexpr const char *kUrmaAdminPath = "/usr/local/sbin/urma_admin";
constexpr const char *kProcDevIdFile = "dev_id";
constexpr const char *kProcPairInfoFile = "pair_info";

// Procfs 延时常量（微秒）
constexpr useconds_t kProcfsWriteDelayUs = 100000;  // 100ms

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

// TopoFileFinder 类相关函数已移至 namespace hixl 作用域

// 从格式化 EID（带冒号）中去掉冒号
std::string FormatEidFromUrma(const std::string &eid_with_colons) {
  std::string result;
  for (char c : eid_with_colons) {
    if (c != ':') {
      result += c;
    }
  }
  return result;
}

// urma_admin show 输出中的单条 EID 记录
struct UrmaEidEntry {
  std::string udma_name;  // "udma3"
  int eid_index = 0;      // eid0, eid1, ...
  std::string eid;        // 带冒号的原始 EID
};

// 获取 urma_admin 工具路径（与 hccn_tool 相同的查找逻辑）
std::string GetUrmaAdminPath() {
  // 先检查绝对路径是否存在
  if (mmAccess(kUrmaAdminPath) == EN_OK) {
    return kUrmaAdminPath;
  }
  // 绝对路径不存在，用 command -v 在 PATH 中查找
  std::string check_cmd = "command -v urma_admin > /dev/null 2>&1";
  if (system(check_cmd.c_str()) != 0) {
    HIXL_LOGW("[GetUrmaAdminPath] urma_admin not found in default path or PATH");
    return "";
  }
  return "urma_admin";  // 相对路径，从 PATH 查找
}

// urma_admin 命令执行函数
int32_t DefaultUrmaAdminExec(const std::string &cmd, std::string &output) {
  auto urma_path = GetUrmaAdminPath();
  HIXL_LOGI("[DefaultUrmaAdminExec] resolved urma_admin path: '%s'", urma_path.c_str());
  if (urma_path.empty()) {
    HIXL_LOGW("[DefaultUrmaAdminExec] urma_admin not found");
    return FAILED;
  }
  std::string full_cmd = urma_path + " " + cmd;
  HIXL_LOGI("[DefaultUrmaAdminExec] exec cmd: '%s'", full_cmd.c_str());
  FILE *raw_pipe = popen(full_cmd.c_str(), "r");
  if (raw_pipe == nullptr) {
    HIXL_LOGW("[DefaultUrmaAdminExec] Failed to execute: %s, errno=%d(%s)", full_cmd.c_str(), errno, strerror(errno));
    return FAILED;
  }
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

// 解析 urma_admin show 输出，提取所有 EID 条目
int32_t ParseUrmaAdminOutput(const std::string &cmd_output, std::vector<UrmaEidEntry> &all_entries) {
  HIXL_LOGI("[ParseUrmaAdminOutput] urma_admin output length: %zu bytes", cmd_output.length());
  if (cmd_output.empty()) {
    HIXL_LOGE(FAILED, "[ParseUrmaAdminOutput] urma_admin output is empty");
    return FAILED;
  }
  // 打印前 500 字符用于调试
  std::string output_preview = cmd_output.length() > 500 ? cmd_output.substr(0, 500) + "..." : cmd_output;
  HIXL_LOGI("[ParseUrmaAdminOutput] urma_admin output preview:\n%s", output_preview.c_str());

  std::istringstream stream(cmd_output);
  std::string line;
  while (std::getline(stream, line)) {
    // 跳过表头和分隔线
    if ((line.find("num") != std::string::npos && line.find("ubep_dev") != std::string::npos) ||
        line.find("---") != std::string::npos || line.find("eid") == std::string::npos) {
      continue;
    }

    // 解析格式: "0 udma3 UB eid1 0000:0000:003f:0600:0010:0000:df00:1001 ACTIVE"
    std::istringstream iss(line);
    std::string num_str, udma_name, tp_type, eid_name, eid_value, link_status;
    iss >> num_str >> udma_name >> tp_type >> eid_name >> eid_value >> link_status;

    if (udma_name.empty() || eid_name.empty() || eid_value.empty()) {
      continue;
    }

    // 提取 eid_index: "eid1" → 1
    int eid_index = 0;
    try {
      eid_index = std::stoi(eid_name.substr(3));
    } catch (const std::exception &) {
      continue;
    }

    UrmaEidEntry entry;
    entry.udma_name = udma_name;
    entry.eid_index = eid_index;
    entry.eid = eid_value;
    all_entries.push_back(entry);
  }

  if (all_entries.empty()) {
    HIXL_LOGE(FAILED, "[ParseUrmaAdminOutput] No entries parsed from urma_admin show output");
    return FAILED;
  }
  HIXL_LOGI("[ParseUrmaAdminOutput] Successfully parsed %zu entries", all_entries.size());
  return SUCCESS;
}

// 建立 die_id → Host PG EID 映射
int32_t BuildDieToHostPgEidMap(const std::vector<UrmaEidEntry> &all_entries,
                               std::map<int32_t, std::string> &die_to_host_pg_eid) {
  // 按 udma_name 分组
  std::map<std::string, std::vector<UrmaEidEntry>> udma_groups;
  for (const auto &entry : all_entries) {
    udma_groups[entry.udma_name].push_back(entry);
  }

  // 找到 eid_count >= 8 的 UDMA 组（Host 侧）
  for (const auto &[name, entries] : udma_groups) {
    if (entries.size() < kNpuGroupSize) {
      continue;
    }
    HIXL_LOGI("[GetHostPgEid] Host UDMA group: %s (eid_count=%zu)", name.c_str(), entries.size());
    for (const auto &entry : entries) {
      std::string eid_no_colon = FormatEidFromUrma(entry.eid);
      auto info = ParseEidByte6(eid_no_colon);
      HIXL_LOGI("[GetHostPgEid]   %s eid%d: %s, die_id=%d, is_pg=%s", name.c_str(), entry.eid_index, entry.eid.c_str(),
                info.die_id, info.is_pg_eid ? "true" : "false");
      // 只保存 PG EID（高 nibble 为 0x3 或 0x7）
      if (info.is_pg_eid) {
        die_to_host_pg_eid[info.die_id] = entry.eid;
      }
    }
  }

  if (die_to_host_pg_eid.empty()) {
    HIXL_LOGE(FAILED, "[GetHostPgEid] No Host PG EID found (no UDMA group with eid_count >= 8)");
    return FAILED;
  }
  return SUCCESS;
}

// 从 route_data 获取 CPU EID
int32_t GetCpuEidFromRouteData(int32_t phy_dev_id, const RouteData &route_data, std::string &cpu_eid) {
  uint32_t logic_id = 0;
  if (DcmiProxy::GetLogicIdFromPhyId(phy_dev_id, &logic_id) != 0) {
    HIXL_LOGE(FAILED, "[GetHostPgEid] Failed to get logic id from phy id: %d", phy_dev_id);
    return FAILED;
  }
  int32_t target_device_id = static_cast<int32_t>(logic_id);
  HIXL_LOGI("[GetHostPgEid] phy_dev_id=%d, logic_id=%u, target_device_id=%d", phy_dev_id, logic_id, target_device_id);

  for (const auto &entry : route_data.entries) {
    if (entry.device_id == target_device_id && !entry.local_eid.empty()) {
      cpu_eid = entry.local_eid;
      break;
    }
  }

  if (cpu_eid.empty()) {
    HIXL_LOGE(FAILED, "[GetHostPgEid] No local_eid found for device_id=%d (phy_dev_id=%d)", target_device_id,
              phy_dev_id);
    return FAILED;
  }
  return SUCCESS;
}

// 执行 urma_admin show，解析输出，根据 route_data 选择 Host PG EID
int32_t GetHostPgEid(int32_t phy_dev_id, const RouteData &route_data, std::string &host_pg_eid) {
  host_pg_eid.clear();

  // 1. 执行 urma_admin show
  std::string cmd_output;
  int32_t ret = DefaultUrmaAdminExec("show", cmd_output);
  HIXL_LOGI("[GetHostPgEid] the outcoming of urma_admin show are : %s", cmd_output.c_str());
  if (ret != SUCCESS) {
    HIXL_LOGE(FAILED, "[GetHostPgEid] Failed to execute urma_admin show");
    return FAILED;
  }

  // 2. 解析 urma_admin show 输出
  std::vector<UrmaEidEntry> all_entries;
  ret = ParseUrmaAdminOutput(cmd_output, all_entries);
  if (ret != SUCCESS) {
    return ret;
  }
  HIXL_LOGI("[GetHostPgEid] Parsed %zu entries from urma_admin show", all_entries.size());

  // 3. 建立 die_id → PG EID 映射
  std::map<int32_t, std::string> die_to_host_pg_eid;
  ret = BuildDieToHostPgEidMap(all_entries, die_to_host_pg_eid);
  if (ret != SUCCESS) {
    return ret;
  }

  // 4. 从 route_data 获取 CPU EID
  std::string cpu_eid;
  ret = GetCpuEidFromRouteData(phy_dev_id, route_data, cpu_eid);
  if (ret != SUCCESS) {
    return ret;
  }

  // 5. 用 ParseEidByte6 从 CPU EID 提取 die_id，选择同 die 的 Host PG EID
  auto cpu_info = ParseEidByte6(cpu_eid);
  if (cpu_info.byte6 == 0) {
    HIXL_LOGE(FAILED, "[GetHostPgEid] Failed to parse CPU EID: %s", cpu_eid.c_str());
    return FAILED;
  }
  int32_t cpu_die_id = cpu_info.die_id;
  HIXL_LOGI("[GetHostPgEid] CPU EID: %s, die_id=%d", cpu_eid.c_str(), cpu_die_id);

  auto it = die_to_host_pg_eid.find(cpu_die_id);
  if (it == die_to_host_pg_eid.end()) {
    HIXL_LOGE(FAILED, "[GetHostPgEid] No Host PG EID for die_id=%d", cpu_die_id);
    return FAILED;
  }

  host_pg_eid = FormatEidFromUrma(it->second);
  HIXL_LOGI("[GetHostPgEid] Selected Host PG EID: %s (die_id=%d)", host_pg_eid.c_str(), cpu_die_id);
  return SUCCESS;
}

}  // anonymous namespace

// ============================================================================
// TopoFileFinder 类实现
// ============================================================================

TopoFileFinder::TopoFileFinder() {}

TopoFileFinder::~TopoFileFinder() {}

bool TopoFileFinder::MatchProductForm(uint32_t mainboard_id, std::string &topo_prefix) {
  if (mainboard_id == kMainboardIdPod1 || mainboard_id == kMainboardIdPod2 || mainboard_id == kMainboardIdPod3) {
    topo_prefix = kTopoPrefixAtlas950;
    return true;
  }
  if (IsProductServer(mainboard_id)) {
    topo_prefix = kTopoPrefixAtlas850;
    return true;
  }
  return false;
}

bool TopoFileFinder::IsProductServer(uint32_t mainboard_id) {
  return ((mainboard_id >= kMainboardIdServerMin1 && mainboard_id <= kMainboardIdServerMax1 &&
           (mainboard_id % 2 == kOddParity)) ||
          (mainboard_id >= kMainboardIdServerMin2 && mainboard_id <= kMainboardIdServerMax2 &&
           (mainboard_id % 2 == kEvenParity)));
}

std::string TopoFileFinder::FindTopoFile(const std::string &topo_dir, uint32_t mainboard_id) {
  std::string topo_prefix;
  if (!MatchProductForm(mainboard_id, topo_prefix)) {
    HIXL_LOGW("Unknown product form for mainboard_id=0x%x", mainboard_id);
    return "";
  }
  HIXL_LOGI("mainboard_id=0x%x, topo_prefix=%s", mainboard_id, topo_prefix.c_str());

  DIR *dir = opendir(topo_dir.c_str());
  if (dir == nullptr) {
    HIXL_LOGW("Failed to open topo dir: %s", topo_dir.c_str());
    return "";
  }

  std::string matched_file;
  struct dirent *entry = nullptr;
  while ((entry = readdir(dir)) != nullptr) {
    std::string name(entry->d_name);
    // 必须以 .json 结尾
    if (name.size() < kJsonExtLen || name.compare(name.size() - kJsonExtLen, kJsonExtLen, ".json") != 0) {
      continue;
    }
    // 必须匹配产品形态前缀
    if (name.compare(0, topo_prefix.size(), topo_prefix) != 0) {
      continue;
    }
    matched_file = topo_dir + name;
    break;  // 取第一个匹配的文件
  }
  closedir(dir);

  if (matched_file.empty()) {
    HIXL_LOGW("No topo file matching '%s*.json' in %s", topo_prefix.c_str(), topo_dir.c_str());
  } else {
    HIXL_LOGI("Matched topo file: %s", matched_file.c_str());
  }
  return matched_file;
}

// ============================================================================
// ProcfsRouteHandler 类实现
// ============================================================================

ProcfsRouteHandler::ProcfsRouteHandler() : injected_proc_base_path_() {}

ProcfsRouteHandler::ProcfsRouteHandler(std::string proc_base_path)
    : injected_proc_base_path_(std::move(proc_base_path)) {}

ProcfsRouteHandler::~ProcfsRouteHandler() {}

std::string ProcfsRouteHandler::FindProcBasePath() {
  // 显式注入的路径优先；空字符串表示走默认 ascend_ub / asdrv_ub 自动发现
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
  if (mmAccess(path.c_str()) != EN_OK) {
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
  // 去掉 0x 前缀
  if (result.size() >= 2 && result[0] == '0' && (result[1] == 'x' || result[1] == 'X')) {
    result = result.substr(2);
  }
  // 去掉所有冒号
  result.erase(std::remove(result.begin(), result.end(), ':'), result.end());
  return result;
}

size_t ProcfsRouteHandler::SelectEidIndexByNpuId(int32_t npu_id, size_t local_count, size_t remote_count) {
  int32_t group_offset = npu_id % 8;
  size_t eid_idx = (group_offset < 4) ? 0 : 1;  // 前4个用第一组，后4个用第二组
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
                                                std::string &local_eid, std::string &remote_eid) {
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

int32_t ProcfsRouteHandler::ProcessNpuProcfsRoute(int32_t npu_id, const std::string &dev_id_path,
                                                  const std::string &pair_info_path, RouteEntry &entry) {
  int32_t device_id = npu_id % kNpuGroupSize;
  HIXL_LOGI("[Procfs] Processing npu_id=%d, device_id=%d", npu_id, device_id);

  // 写入 device_id（组内相对 ID）选择设备
  std::ostringstream dev_id_ss;
  dev_id_ss << device_id << "\n";
  if (!WriteStringToFile(dev_id_path, dev_id_ss.str())) {
    HIXL_LOGW("[Procfs] Failed to write device_id=%d to %s", device_id, dev_id_path.c_str());
    return FAILED;
  }

  // 短暂延时确保内核更新
  usleep(kProcfsWriteDelayUs);

  // 读取 pair_info
  std::string pair_info_content;
  if (!ReadFileToString(pair_info_path, pair_info_content)) {
    HIXL_LOGW("[Procfs] Failed to read pair_info for npu_id=%d", npu_id);
    return FAILED;
  }

  // 解析 pair_info
  int32_t slot_id = npu_id;
  std::string local_eid;
  std::string remote_eid;
  if (!ParsePairInfoForDevice(pair_info_content, npu_id, slot_id, local_eid, remote_eid)) {
    HIXL_LOGW("[Procfs] Failed to parse pair_info for npu_id=%d", npu_id);
    return FAILED;
  }

  HIXL_LOGI("[Procfs] Parsed: npu_id=%d, slot_id=%d, local_eid=[%s], remote_eid=[%s]", npu_id, slot_id,
            local_eid.c_str(), remote_eid.c_str());

  // 只生成 H2D 方向的 RouteEntry
  entry.device_id = device_id;
  entry.local_eid = local_eid;
  entry.remote_eid = remote_eid;
  return SUCCESS;
}

int32_t ProcfsRouteHandler::GenerateRouteData(const std::set<int32_t> &related_npu_ids, RouteData &route_data) {
  route_data.entries.clear();

  std::string proc_base = FindProcBasePath();
  if (proc_base.empty()) {
    HIXL_LOGE(FAILED, "Neither /proc/ascend_ub nor /proc/asdrv_ub found");
    return FAILED;
  }
  HIXL_LOGI("Using procfs base path: %s", proc_base.c_str());

  std::string dev_id_path = proc_base + "/" + kProcDevIdFile;
  std::string pair_info_path = proc_base + "/" + kProcPairInfoFile;

  for (int32_t npu_id : related_npu_ids) {
    RouteEntry entry;
    int32_t ret = ProcessNpuProcfsRoute(npu_id, dev_id_path, pair_info_path, entry);
    if (ret == SUCCESS) {
      route_data.entries.push_back(entry);
      HIXL_LOGI("[Procfs] RouteEntry H2D: npu_id=%d, device_id=%d, local_eid=[%s], remote_eid=[%s]", npu_id,
                entry.device_id, entry.local_eid.c_str(), entry.remote_eid.c_str());
    }
  }

  if (route_data.entries.empty()) {
    HIXL_LOGE(FAILED, "No route entries generated from procfs");
    return FAILED;
  }

  HIXL_LOGI("[Procfs] Generated %zu route entries from procfs:", route_data.entries.size());
  for (size_t i = 0; i < route_data.entries.size(); ++i) {
    const auto &entry = route_data.entries[i];
    HIXL_LOGI("[Procfs]   [%zu] device_id=%d, local_eid=[%s], remote_eid=[%s]", i, entry.device_id,
              entry.local_eid.c_str(), entry.remote_eid.c_str());
  }

  return SUCCESS;
}

// ============================================================================
// ProcfsRouteHandler 类实现结束
// ============================================================================

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
  } catch (const std::exception &) {
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
    } catch (const std::exception &) {
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
  if (mmAccess(topo_path.c_str()) != EN_OK) {
    HIXL_LOGE(PARAM_INVALID, "Topo file access failed: %s, errno=%d(%s)", topo_path.c_str(), errno, strerror(errno));
    return PARAM_INVALID;
  }
  std::ifstream file(topo_path);
  if (!file.is_open()) {
    HIXL_LOGE(PARAM_INVALID, "Failed to open topo file: %s, errno=%d(%s)", topo_path.c_str(), errno, strerror(errno));
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
  if (mmAccess(route_path.c_str()) != EN_OK) {
    HIXL_LOGE(PARAM_INVALID, "Route file access failed: %s, errno=%d(%s)", route_path.c_str(), errno, strerror(errno));
    return PARAM_INVALID;
  }
  std::ifstream file(route_path);
  if (!file.is_open()) {
    HIXL_LOGE(PARAM_INVALID, "Failed to open route file: %s, errno=%d(%s)", route_path.c_str(), errno, strerror(errno));
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

int32_t GenerateH2UEdges(int32_t phy_dev_id, const RouteData &route_data, const std::string &plane_pg_0_eid,
                         const std::string &plane_pg_1_eid, std::vector<EndpointConfig> &h2u_edges) {
  // 获取 Host PG EID（通过 urma_admin show + route_data die_id 选择）
  std::string host_pg_eid;
  int32_t ret = GetHostPgEid(phy_dev_id, route_data, host_pg_eid);
  if (ret != SUCCESS) {
    HIXL_LOGE(ret, "[H2U] Failed to get Host PG EID");
    return ret;
  }
  HIXL_LOGI("[H2U] Using Host PG EID as comm_id: %s", host_pg_eid.c_str());
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

int32_t CollectAllEdges(const EdgeCollectInput &input, std::vector<EndpointConfig> &all_edges) {
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
    int32_t ret =
        GenerateH2UEdges(input.phy_dev_id, input.route_data, input.plane_pg_0_eid, input.plane_pg_1_eid, edges);
    if (ret != SUCCESS) {
      return ret;
    }
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
  return SUCCESS;
}

// 组装 LocalCommRes 结果的内部函数
int32_t BuildLocalCommResResult(int32_t phy_dev_id, bool is_server, const TopoData &topo_data,
                                const RouteData &route_data, const std::set<int32_t> &related_npu_ids,
                                LocalCommRes &local_comm_res) {
  // 构建 NpuRootInfo
  std::map<int32_t, NpuRootInfo> npu_rootinfos;
  int32_t ret = BuildNpuRootinfos(related_npu_ids, is_server, npu_rootinfos);
  if (ret != SUCCESS) {
    return ret;
  }

  // 收集 CLOS PG EIDs
  std::string plane_pg_0_eid, plane_pg_1_eid;
  int32_t mesh_die_id = 0;
  CollectClosPgEids(npu_rootinfos, phy_dev_id, is_server, plane_pg_0_eid, plane_pg_1_eid, mesh_die_id);

  // 生成所有边
  std::vector<EndpointConfig> all_edges;
  ret = CollectAllEdges({topo_data, route_data, npu_rootinfos, phy_dev_id, plane_pg_0_eid, plane_pg_1_eid}, all_edges);
  if (ret != SUCCESS) {
    return ret;
  }
  if (all_edges.empty()) {
    return PARAM_INVALID;
  }

  // 获取 net_instance_id 并组装结果
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

// 解析 route 文件，失败时回退到 procfs
int32_t ParseRouteWithProcfsFallback(int32_t phy_dev_id, const std::string &route_path, RouteData &route_data,
                                     std::set<int32_t> &related_npu_ids) {
  related_npu_ids = CollectRelatedNpuIds(phy_dev_id);
  int32_t ret = ParseRouteFile(route_path, route_data);
  if (ret != SUCCESS) {
    HIXL_LOGW("ParseRouteFile failed (path=%s), trying procfs fallback", route_path.c_str());
    ProcfsRouteHandler handler;
    ret = handler.GenerateRouteData(related_npu_ids, route_data);
    if (ret != SUCCESS) {
      HIXL_LOGE(FAILED, "Both route.conf and procfs fallback failed");
      return ret;
    }
  }
  return SUCCESS;
}

// 解析 topo 和 route 文件，公共逻辑提取
int32_t ParseTopoAndRouteFiles(int32_t phy_dev_id, const std::string &topo_path, const std::string &route_path,
                               TopoData &topo_data, RouteData &route_data, std::set<int32_t> &related_npu_ids) {
  int32_t ret = ParseTopoFile(topo_path, topo_data);
  if (ret != SUCCESS) {
    return ret;
  }
  ret = ParseRouteWithProcfsFallback(phy_dev_id, route_path, route_data, related_npu_ids);
  if (ret != SUCCESS) {
    return ret;
  }
  return SUCCESS;
}

int32_t GenerateLocalCommRes(int32_t phy_dev_id, LocalCommRes &local_comm_res) {
  // 1. 获取 mainboard_id，根据产品形态选择 topo 文件
  uint32_t mainboard_id = 0;
  int32_t ret = GetMainboardId(phy_dev_id, mainboard_id);
  if (ret != SUCCESS) {
    return ret;
  }
  std::string topo_path = TopoFileFinder().FindTopoFile(kDefaultTopoDir, mainboard_id);
  if (topo_path.empty()) {
    HIXL_LOGE(PARAM_INVALID, "No topo file found for mainboard_id=0x%x in %s", mainboard_id, kDefaultTopoDir);
    return PARAM_INVALID;
  }
  std::string route_path = kDefaultRoutePath;

  // 2. 调用有参版本
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
  // 判断是否为 Server 产品形态
  bool is_server = IsProductServer(mainboard_id);

  // 2. 解析文件
  TopoData topo_data;
  RouteData route_data;
  std::set<int32_t> related_npu_ids;
  ret = ParseTopoAndRouteFiles(phy_dev_id, topo_path, route_path, topo_data, route_data, related_npu_ids);
  if (ret != SUCCESS) {
    return ret;
  }

  // 3. 组装结果
  return BuildLocalCommResResult(phy_dev_id, is_server, topo_data, route_data, related_npu_ids, local_comm_res);
}

void ConvertLocalCommResToView(const LocalCommRes &src, LocalCommResView &dst) {
  // std::string 字段仅在 .so 内部使用，不跨 .so 边界传递；外层通过 AscendString
  // （内部封装 shared_ptr<std::string>）持有，跨 .so 边界 ABI 安全。
  dst.version = AscendString(src.version.c_str());
  dst.net_instance_id = AscendString(src.net_instance_id.c_str());
  dst.endpoint_list.clear();
  dst.endpoint_list.reserve(src.endpoint_list.size());
  for (const auto &ep : src.endpoint_list) {
    EndpointConfigView ep_view;
    ep_view.protocol = AscendString(ep.protocol.c_str());
    ep_view.comm_id = AscendString(ep.comm_id.c_str());
    ep_view.placement = AscendString(ep.placement.c_str());
    ep_view.plane = AscendString(ep.plane.c_str());
    ep_view.dst_eid = AscendString(ep.dst_eid.c_str());
    ep_view.net_instance_id = AscendString(ep.net_instance_id.c_str());
    ep_view.device_info = ep.device_info;
    dst.endpoint_list.push_back(std::move(ep_view));
  }
}

int32_t GenerateLocalCommResJson(int32_t phy_dev_id, LocalCommResView &view) {
  // 1. 调用 GenerateLocalCommRes 组装 LocalCommRes 结构体
  LocalCommRes local_comm_res;
  int32_t ret = GenerateLocalCommRes(phy_dev_id, local_comm_res);
  if (ret != SUCCESS) {
    HIXL_LOGE(ret, "[GenerateLocalCommResJson] GenerateLocalCommRes failed, ret=%d", ret);
    return ret;
  }

  // 2. 把 std::string 字段全部转成 AscendString 填充到 view
  ConvertLocalCommResToView(local_comm_res, view);
  return SUCCESS;
}

}  // namespace hixl
