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
 * @brief LocalCommRes 生成工具头文件
 *
 * 本工具用于在 HIXL 初始化阶段自动生成本地通信资源信息。
 * 支持 Mesh 层和 CLOS 层 EID 和 Port 的获取。
 *
 * DCMI 接口通过 dlopen 方式动态加载 libdcmi.so，避免直接依赖 hal.h
 */

#ifndef CANN_HIXL_SRC_HIXL_ENGINE_HIXL_LOCAL_COMM_RES_TOOL_H
#define CANN_HIXL_SRC_HIXL_ENGINE_HIXL_LOCAL_COMM_RES_TOOL_H

#include <string>
#include <vector>
#include <map>

// 引入 rootinfo_builder 的数据结构
#include "rootinfo_builder_generator_v1.h"
// 引入 EndpointConfig 定义
#include "common/hixl_inner_types.h"
// AscendString 通过 hixl_inner_types.h 间接包含的 hixl_types.h 提供别名（hixl::AscendString）

namespace hixl {

// ============ 端点配置结构 ============
// EndpointConfig 已通过 hixl_inner_types.h 引入

/**
 * @brief LocalCommRes 结构体（替代 JSON 输出）
 */
struct LocalCommRes {
  std::string version;                        // 版本号，默认 "1.3"
  std::string net_instance_id;                // 网络实例 ID
  std::vector<EndpointConfig> endpoint_list;  // 端点列表
};

/**
 * @brief 端点配置视图，所有字符串字段均为 AscendString（ABI 安全）
 */
struct EndpointConfigView {
  AscendString protocol;
  AscendString comm_id;
  AscendString placement;
  AscendString plane;
  AscendString dst_eid;
  AscendString net_instance_id;
  DeviceInfoConfig device_info{};
};

/**
 * @brief LocalCommRes 视图，所有字符串字段均为 AscendString（ABI 安全）
 */
struct LocalCommResView {
  AscendString version;                           // 版本号，默认 "1.3"
  AscendString net_instance_id;                   // 网络实例 ID
  std::vector<EndpointConfigView> endpoint_list;  // 端点列表
};

// ============ Topology 数据结构 ============

/**
 * @brief Topology 链路结构
 */
struct TopoLink {
  int32_t net_layer = 0;                   // 网络层级 (0: Mesh, 1: CLOS)
  std::string link_type;                   // 连接类型: PEER2PEER, PEER2NET
  std::string topo_type;                   // 拓扑类型: 1DMESH, CLOS
  int32_t local_a = 0;                     // 本地节点 A
  int32_t local_b = 0;                     // 本地节点 B
  int32_t remote_a = -1;                   // 远端节点 A
  int32_t remote_b = -1;                   // 远端节点 B
  std::vector<std::string> local_a_ports;  // 本地 A 端口列表（串口标识，如 "die_id/port"）
  std::vector<std::string> local_b_ports;  // 本地 B 端口列表
};

struct TopoData {
  std::vector<TopoLink> links;
};

// ============ Route 数据结构 ============

/**
 * @brief Route 条目结构
 */
struct RouteEntry {
  int32_t device_id = 0;   // 设备 ID
  std::string local_eid;   // 本地 EID
  std::string remote_eid;  // 远端 EID
};

struct RouteData {
  std::vector<RouteEntry> entries;
};

// ============ 核心接口 ============

/**
 * @brief 生成 LocalCommRes 结构体（生产接口，使用默认路径）
 * @param [in] phy_dev_id 物理设备 ID，通过 aclrtGetPhyDevIdByLogicDevId 获取
 * @param [out] local_comm_res 输出的 LocalCommRes 结构体
 * @return 成功: SUCCESS, 失败: 其它错误码
 */
int32_t GenerateLocalCommRes(int32_t phy_dev_id, LocalCommRes &local_comm_res);

/**
 * @brief 生成 LocalCommRes 结构体（测试用重载，允许注入路径）
 * @param [in] phy_dev_id 物理设备 ID
 * @param [in] topo_path topology 文件路径
 * @param [in] route_path route.conf 文件路径
 * @param [out] local_comm_res 输出的 LocalCommRes 结构体
 * @return 成功: SUCCESS, 失败: 其它错误码
 */
int32_t GenerateLocalCommRes(int32_t phy_dev_id, const std::string &topo_path, const std::string &route_path,
                             LocalCommRes &local_comm_res);

/**
 * @brief 把 LocalCommRes（std::string 字段）转成 LocalCommResView（AscendString 字段）
 *
 * 纯数据转换，**不调用任何 DCMI/urma_admin/procfs 接口**，
 * 仅依赖 LocalCommRes 输入。提供给 GenerateLocalCommResJson 调用，
 * 也方便单元测试覆盖 std::string → AscendString 的转换逻辑。
 *
 * @param [in]  src  源 LocalCommRes（std::string 字段）
 * @param [out] dst  目标 LocalCommResView（AscendString 字段），会被整体覆盖
 */
void ConvertLocalCommResToView(const LocalCommRes &src, LocalCommResView &dst);

/**
 * @brief 生成 LocalCommRes 的视图数据（lcrgen 工具使用）
 *
 * 内部完成 GenerateLocalCommRes，并通过 ConvertLocalCommResToView 把
 * LocalCommRes 的 std::string 字段全部转成 AscendString 填充到 LocalCommResView 中。
 * **所有 std::string → AscendString 转换都在 libcann_hixl.so 内部完成**，
 * 避免 std::string 跨 .so 边界传递。JSON 序列化由 lcrgen 工具本地完成。
 *
 * @param [in] phy_dev_id 物理设备 ID
 * @param [out] view 成功时填入 LocalCommResView；失败时状态未定义
 * @return 成功: SUCCESS, 失败: 其它错误码
 */
int32_t GenerateLocalCommResJson(int32_t phy_dev_id, LocalCommResView &view);

// ============ DCMI 接口封装 ============

/**
 * @brief 获取主板 ID
 * @param [in] phy_dev_id 物理设备 ID
 * @param [out] mainboard_id 主板 ID
 * @return 成功: SUCCESS, 失败: 其它错误码
 */
int32_t GetMainboardId(int32_t phy_dev_id, uint32_t &mainboard_id);

/**
 * @brief 获取 CLOS 层 net_instance_id
 * @param [in] phy_dev_id 物理设备 ID
 * @param [out] net_instance_id 输出的 net_instance_id，格式为 "superpod_{super_pod_id}"
 * @return 成功: SUCCESS, 失败: 其它错误码
 */
int32_t GetClosNetInstanceId(int32_t phy_dev_id, std::string &net_instance_id);

// ============ 文件解析接口 ============

/**
 * @brief 解析 topology 文件
 * @param [in] topo_path topology 文件路径
 * @param [out] topo_data 解析后的 topology 数据
 * @return 成功: SUCCESS, 失败: 其它错误码
 */
int32_t ParseTopoFile(const std::string &topo_path, TopoData &topo_data);

/**
 * @brief 解析 route.conf 文件
 * @param [in] route_path route.conf 文件路径
 * @param [out] route_data 解析后的 route 数据
 * @return 成功: SUCCESS, 失败: 其它错误码
 */
int32_t ParseRouteFile(const std::string &route_path, RouteData &route_data);

// ============ 边生成接口 ============

/**
 * @brief 生成 D2D 直连边（Device to Device）
 * @param [in] topo_data topology 数据
 * @param [in] npu_rootinfos 所有相关 NPU 的 rootinfo 映射
 * @param [in] phy_id 当前 NPU 物理 ID
 * @param [out] edges 生成的边列表
 * @return 成功: SUCCESS, 失败: 其它错误码
 */
int32_t GenerateD2DEdges(const TopoData &topo_data, const std::map<int32_t, NpuRootInfo> &npu_rootinfos, int32_t phy_id,
                         std::vector<EndpointConfig> &edges);

/**
 * @brief 生成 D2U 非直连边（Device to UB Gateway）
 * @param [in] plane_pg_0_eid plane_pg_0 的 EID
 * @param [in] plane_pg_1_eid plane_pg_1 的 EID
 * @param [out] d2u_edges 生成的边列表
 */
void GenerateD2UEdges(const std::string &plane_pg_0_eid, const std::string &plane_pg_1_eid,
                      std::vector<EndpointConfig> &d2u_edges);

/**
 * @brief 生成 H2D 直连边（Host to Device）
 * @param [in] route_data route 数据
 * @param [out] edges 生成的边列表
 * @return 成功: SUCCESS, 失败: 其它错误码
 */
int32_t GenerateH2DEdges(const RouteData &route_data, std::vector<EndpointConfig> &edges);

/**
 * @brief 生成 D2H 直连边（Device to Host）
 * @param [in] route_data route 数据
 * @param [in] phy_dev_id 当前 NPU 物理 ID，只取 device_id 匹配的条目
 * @param [out] edges 生成的边列表
 * @return 成功: SUCCESS, 失败: 其它错误码
 */
int32_t GenerateD2HEdges(const RouteData &route_data, int32_t phy_dev_id, std::vector<EndpointConfig> &edges);

/**
 * @brief 生成 H2U 非直连边（Host to UB Gateway）
 * @param [in] phy_dev_id 当前 NPU 物理 ID
 * @param [in] route_data route 数据
 * @param [in] plane_pg_0_eid plane_pg_0 的 EID
 * @param [in] plane_pg_1_eid plane_pg_1 的 EID
 * @param [out] h2u_edges 生成的边列表
 * @return 成功: SUCCESS, 失败: 其它错误码
 */
int32_t GenerateH2UEdges(int32_t phy_dev_id, const RouteData &route_data, const std::string &plane_pg_0_eid,
                         const std::string &plane_pg_1_eid, std::vector<EndpointConfig> &h2u_edges);

/**
 * @brief 根据 topo 数据获取某个 NPU 的 CLOS PG EID 对应的 port 数量
 * @param [in] topo_data topology 数据
 * @param [in] phy_id NPU 物理 ID
 * @param [in] clos_pg_eid CLOS PG EID
 * @return port 数量，失败返回 -1
 */
int32_t GetClosPgPortCount(const TopoData &topo_data, int32_t phy_id, const std::string &clos_pg_eid);

// ============ ProcfsRouteHandler 类 ============

/**
 * @brief Procfs 路由处理器（用于 route.conf 不存在时的 fallback）
 * 通过读写 /proc/ascend_ub 或 /proc/asdrv_ub 获取路由信息
 */
class ProcfsRouteHandler {
 public:
  ProcfsRouteHandler();
  /**
   * @brief 构造时显式指定 proc 根目录
   * @param [in] proc_base_path 注入的 proc 根目录；空字符串表示走默认双路径自动发现
   */
  explicit ProcfsRouteHandler(std::string proc_base_path);
  ~ProcfsRouteHandler();

  // 通过 procfs 生成路由数据
  int32_t GenerateRouteData(const std::set<int32_t> &related_npu_ids, RouteData &route_data);

 private:
  // 私有辅助方法
  std::string FindProcBasePath();
  bool ReadFileToString(const std::string &path, std::string &content);
  bool WriteStringToFile(const std::string &path, const std::string &content);
  std::string TrimString(const std::string &s);
  bool ParseSlotIdFromLine(const std::string &line, std::string &slot_id);
  bool ParseEidFromLine(const std::string &line, const std::string &prefix, std::string &eid);
  std::string FormatEidValue(const std::string &eid);
  size_t SelectEidIndexByNpuId(int32_t npu_id, size_t local_count, size_t remote_count);
  bool CollectEidsFromPairInfo(const std::string &pair_info_content, std::string &found_slot_id,
                               std::vector<std::string> &local_eids, std::vector<std::string> &remote_eids);
  bool ParsePairInfoForDevice(const std::string &pair_info_content, int32_t npu_id, int32_t &slot_id,
                              std::string &local_eid, std::string &remote_eid);
  int32_t ProcessNpuProcfsRoute(int32_t npu_id, const std::string &dev_id_path, const std::string &pair_info_path,
                                RouteEntry &entry);

  // 显式注入的 proc 根目录；空字符串表示走默认 ascend_ub / asdrv_ub 自动发现
  std::string injected_proc_base_path_;
};

// ============ TopoFileFinder 类 ============

/**
 * @brief Topo 文件查找器
 * 根据产品形态（mainboard_id）在指定目录下查找匹配的 topo 文件
 */
class TopoFileFinder {
 public:
  TopoFileFinder();
  ~TopoFileFinder();

  /**
   * @brief 根据 mainboard_id 查找匹配的 topo 文件
   * @param [in] topo_dir topo 文件目录
   * @param [in] mainboard_id 主板 ID
   * @return 匹配的 topo 文件路径，失败返回空字符串
   */
  std::string FindTopoFile(const std::string &topo_dir, uint32_t mainboard_id);

 private:
  bool IsProductServer(uint32_t mainboard_id);
  bool MatchProductForm(uint32_t mainboard_id, std::string &topo_prefix);
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_ENGINE_HIXL_LOCAL_COMM_RES_TOOL_H
