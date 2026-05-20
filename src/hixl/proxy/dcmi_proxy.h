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
 * @file dcmi_proxy.h
 * @brief DCMI 接口代理模块
 *
 * 封装 DCMI 接口的动态加载（通过 dlopen），对外暴露高层代理函数接口。
 */

#ifndef CANN_HIXL_SRC_HIXL_PROXY_DCMI_PROXY_H
#define CANN_HIXL_SRC_HIXL_PROXY_DCMI_PROXY_H

#include <cstdint>

namespace hixl {

// ============ DCMI 相关数据结构 ============

constexpr int32_t kDcmiUrmaEidSize = 16;
constexpr int32_t kMaxEidPerUe = 32;

/**
 * @brief DCMI URMA EID 结构
 */
union DcmiUrmaEid {
    unsigned char raw[kDcmiUrmaEidSize];
    struct {
        unsigned long subnet_prefix;
        unsigned long interface_id;
    } in6;
};

/**
 * @brief DCMI URMA EID 信息结构
 */
struct DcmiUrmaEidInfo {
    DcmiUrmaEid eid;
    unsigned int eid_index;
};

// ============ DCMI SPOD 信息结构 ============

/**
 * @brief DCMI SPOD 信息结构体
 */
struct DcmiSpodInfo {
  unsigned int sdid;
  unsigned int super_pod_size;
  unsigned int super_pod_id;
  unsigned int server_index;
  unsigned int chassis_id;
  unsigned int super_pod_type;
  unsigned int reserve[6];
};

// ============ DCMI 代理接口 ============

/**
 * @brief DCMI 接口代理类
 *
 * 通过 dlopen 动态加载 libdcmi.so，封装 DCMI 接口调用。
 * 使用 static 方法，不允许实例化。
 */
class DcmiProxy {
public:
    DcmiProxy() = delete;

    /**
     * @brief 加载 DCMI 动态库并初始化
     * @return 成功: 0, 失败: -1
     */
    static int LoadDcmi();

    /**
     * @brief 根据物理 ID 获取逻辑 ID
     * @param phy_id 物理设备 ID
     * @param logic_id 逻辑设备 ID（输出）
     * @return 成功: 0, 失败: -1
     */
    static int32_t GetLogicIdFromPhyId(unsigned int phy_id, unsigned int *logic_id);

    /**
     * @brief 获取 URMA 设备数量
     * @param logic_id 逻辑设备 ID
     * @param dev_cnt 设备数量（输出）
     * @return 成功: 0, 失败: -1
     */
    static int32_t GetUrmaDeviceCnt(unsigned int logic_id, unsigned int *dev_cnt);

    /**
     * @brief 获取 EID 列表
     * @param logic_id 逻辑设备 ID
     * @param urma_dev_index URMA 设备索引
     * @param eid_list EID 列表（输出）
     * @param eid_cnt EID 数量（输入/输出）
     * @return 成功: 0, 失败: -1
     */
    static int32_t GetEidList(unsigned int logic_id, int urma_dev_index,
                               DcmiUrmaEidInfo *eid_list, int *eid_cnt);

    /**
     * @brief 获取主板 ID
     * @param logic_id 逻辑设备 ID
     * @param mainboard_id 主板 ID（输出）
     * @return 成功: 0, 失败: -1
     */
    static int32_t GetMainboardId(unsigned int logic_id, unsigned int *mainboard_id);

    /**
     * @brief 获取设备信息
     * @param logic_id 逻辑设备 ID
     * @param main_cmd 主命令
     * @param sub_cmd 子命令
     * @param buf 缓冲区（输出）
     * @param size 缓冲区大小（输入/输出）
     * @return 成功: 0, 失败: -1
     */
    static int32_t GetDeviceInfo(unsigned int logic_id, int main_cmd,
                                 unsigned int sub_cmd, void *buf, unsigned int *size);
    static void UnloadDcmi();
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_PROXY_DCMI_PROXY_H
