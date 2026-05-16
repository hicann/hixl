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

#ifndef CANN_HIXL_SRC_HIXL_PROXY_HCOMM_DCM_PROXY_H
#define CANN_HIXL_SRC_HIXL_PROXY_HCOMM_DCM_PROXY_H

#include <cstdint>

namespace hixl {

// ============ DCMI 相关数据结构 ============

const int32_t DCMI_URMA_EID_SIZE = 16;
const int32_t MAX_EID_PER_UE = 32;

/**
 * @brief DCMI URMA EID 结构
 */
typedef union dcmi_urma_eid {
    unsigned char raw[DCMI_URMA_EID_SIZE];
    struct {
        unsigned long subnet_prefix;
        unsigned long interface_id;
    } in6;
} dcmi_urma_eid_t;

/**
 * @brief DCMI URMA EID 信息结构
 */
typedef struct dcmi_urma_eid_info {
    dcmi_urma_eid_t eid;
    unsigned int eid_index;
} dcmi_urma_eid_info_t;

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

// ============ DCMI 函数指针类型（内部实现使用） ============

typedef int (*dcmi_init_func)();
typedef int (*dcmi_get_urma_device_cnt_func)(int npu_id, unsigned int* dev_cnt);
typedef int (*dcmi_get_eid_list_func)(int npu_id, int urma_dev_index,
                                       dcmi_urma_eid_info_t* eid_list, int* eid_cnt);
typedef int (*dcmi_get_mainboard_id_func)(int npu_id, unsigned int* mainboard_id);
typedef int (*dcmi_get_logicid_from_phyid_func)(unsigned int phy_id, unsigned int* logic_id);
typedef int (*dcmi_get_device_info_func)(int npu_id, int main_cmd,
                                          unsigned int sub_cmd, void* buf, unsigned int* size);

// ============ DCMI 高层代理接口 ============

int32_t DcmiGetLogicIdFromPhyId(unsigned int phy_id, unsigned int *logic_id);
int32_t DcmiGetUrmaDeviceCnt(unsigned int logic_id, unsigned int *dev_cnt);
int32_t DcmiGetEidList(unsigned int logic_id, int urma_dev_index,
                       dcmi_urma_eid_info_t *eid_list, int *eid_cnt);
int32_t DcmiGetMainboardId(unsigned int logic_id, unsigned int *mainboard_id);
int32_t DcmiGetDeviceInfo(unsigned int logic_id, int main_cmd,
                          unsigned int sub_cmd, void *buf, unsigned int *size);

/**
 * @brief 加载 DCMI 动态库并初始化
 * @return 成功: 0, 失败: -1
 */
int LoadDcmi();

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_PROXY_HCOMM_DCM_PROXY_H
