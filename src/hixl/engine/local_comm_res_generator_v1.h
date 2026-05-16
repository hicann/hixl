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

// ============ Topology 数据结构 ============

// ============ Topology 数据结构 ============

/**
 * @brief Topology 链路结构
 */
struct TopoLink {
  int32_t net_layer = 0;                       // 网络层级 (0: Mesh, 1: CLOS)
  std::string link_type;                       // 连接类型: PEER2PEER, PEER2NET
  std::string topo_type;                       // 拓扑类型: 1DMESH, CLOS
  int32_t local_a = 0;                         // 本地节点 A
  int32_t local_b = 0;                         // 本地节点 B
  int32_t remote_a = -1;                       // 远端节点 A
  int32_t remote_b = -1;                       // 远端节点 B
  std::vector<std::string> local_a_ports;      // 本地 A 端口列表（串口标识，如 "die_id/port"）
  std::vector<std::string> local_b_ports;      // 本地 B 端口列表
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
 * @param [in] topo_data topology 数据
 * @param [in] npu_rootinfos 所有相关 NPU 的 rootinfo 映射
 * @param [in] clos_plane_ids CLOS 层 plane_id 列表
 * @param [in] phy_id 当前 NPU 物理 ID
 * @param [out] edges 生成的边列表
 * @return 成功: SUCCESS, 失败: 其它错误码
 */
int32_t GenerateD2UEdges(const TopoData &topo_data, const std::map<int32_t, NpuRootInfo> &npu_rootinfos,
                         const std::vector<std::string> &clos_plane_ids, int32_t phy_id,
                         std::vector<EndpointConfig> &edges);

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
 * @param [in] route_data route 数据
 * @param [in] clos_plane_ids CLOS 层 plane_id 列表
 * @param [in] phy_id 当前 NPU 物理 ID
 * @param [out] edges 生成的边列表
 * @return 成功: SUCCESS, 失败: 其它错误码
 */
int32_t GenerateH2UEdges(const RouteData &route_data, const std::vector<std::string> &clos_plane_ids, int32_t phy_id,
                         std::vector<EndpointConfig> &edges);

/**
 * @brief 根据 topo 数据获取某个 NPU 的 CLOS PG EID 对应的 port 数量
 * @param [in] topo_data topology 数据
 * @param [in] phy_id NPU 物理 ID
 * @param [in] clos_pg_eid CLOS PG EID
 * @return port 数量，失败返回 -1
 */
int32_t GetClosPgPortCount(const TopoData &topo_data, int32_t phy_id, const std::string &clos_pg_eid);

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_ENGINE_HIXL_LOCAL_COMM_RES_TOOL_H
