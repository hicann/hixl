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
 * @file root_info_builder.h
 * @brief RootInfo 构建模块
 *
 * 提供根据 NPU ID 构建 RootInfo 的功能，包括：
 * - 调用 DCMI 接口获取 URMA 设备信息
 * - 根据产品形态构建 port 到 EID 的映射
 */

#ifndef CANN_HIXL_SRC_HIXL_ENGINE_HIXL_ROOTINFO_BUILDER_H
#define CANN_HIXL_SRC_HIXL_ENGINE_HIXL_ROOTINFO_BUILDER_H

#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include "hixl/hixl_types.h"
#include "proxy/dcmi_proxy.h"

namespace hixl {

// ============ 错误码定义 ============
// 使用 hixl_types.h 中定义的全局状态码：
//   SUCCESS              — 成功
//   PARAM_INVALID        — 参数无效（文件未找到、JSON 解析失败等）
//   FAILED               — 内部失败（DCMI 接口失败、无 EID 等）

// ============ URMA Device 数据结构 ============

/**
 * @brief URMA Device 结构
 * 每个 URMA Device 包含多个 EID，die_id 从第一个 EID 获取
 */
struct UrmaDevice {
  std::string name;                   // 设备名称，如 "udma0"
  std::vector<std::string> eid_list;  // 该设备下的所有 EID 列表
};

// ============ RootInfo 数据结构 ============

/**
 * @brief CLOS PG EID 信息
 */
struct ClosPgEidInfo {
  std::string eid;  // CLOS PG EID
  int32_t die_id;   // 该 PG EID 对应的 die_id
};

/**
 * @brief NPU 串口到 EID 的映射信息
 * key: 串口标识 "die_id/port"
 * value: 对应的 EID 字符串
 */
struct NpuRootInfo {
  std::map<std::string, std::string> port_to_eid;  // Mesh 层串口到 EID 映射
  std::vector<ClosPgEidInfo> clos_pg_eids;         // CLOS 层 PG EID 列表（可能有多个）
};

/**
 * @brief EID 第6字节解析结果
 */
struct EidByte6Info {
  uint8_t byte6;        // 原始第6字节值
  uint8_t high_nibble;  // 高4位
  uint8_t low_nibble;   // 低4位
  uint8_t die_id;       // die_id (0 或 1)
  bool is_pg_eid;       // 是否为 PG EID (串口组)
  int32_t port;         // port 值 (0-15)
};

// ============ EID 解析辅助函数 ============

/**
 * @brief 解析 EID 的第6字节
 * @param [in] eid 字符串格式 EID
 * @return EidByte6Info 解析结果
 */
EidByte6Info ParseEidByte6(const std::string &eid);

// ============ RootInfo 构建接口 ============

/**
 * @brief 根据 NPU ID 构建 RootInfo
 * @param [in] npu_id NPU ID
 * @param [in] is_server 是否为 Server 产品形态
 * @param [out] root_info 输出的 RootInfo 结构
 * @return 成功: SUCCESS, 失败: 其它错误码
 *
 * 该函数内部调用 DCMI 接口获取 URMA 设备信息，
 * 并根据产品形态构建 port 到 EID 的映射。
 */
int32_t BuildNpuRootInfo(int32_t npu_id, bool is_server, NpuRootInfo &root_info);

/**
 * @brief 获取 URMA Device 列表
 * @param [in] npu_id NPU ID
 * @param [out] urma_devices URMA Device 列表
 * @return 成功: SUCCESS, 失败: 其它错误码
 */
int32_t GetUrmaDeviceList(int32_t npu_id, std::vector<UrmaDevice> &urma_devices);

/**
 * @brief 确定 Mesh 层的 die_id
 * @param npu_id NPU ID
 * @param is_server 是否为 Server 产品形态
 * @return Mesh 层所在的 die_id
 *
 * Server: Mesh 在 1die
 * Pod: 根据 npu_id % 8 判断，0-3 在 0die，4-7 在 1die
 */
int32_t GetMeshDieId(int32_t npu_id, bool is_server);

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_ENGINE_HIXL_ROOTINFO_BUILDER_H
