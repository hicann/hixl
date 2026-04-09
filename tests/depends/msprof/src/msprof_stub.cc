/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <stdint.h>
#include <stdarg.h>
#include "msprof_stub.h"

#ifdef __cplusplus
extern "C" {
#endif

// 保存回调函数指针（测试用）
ProfCommandHandle g_hixl_prof_callback = nullptr;

// 桩返回值控制
int32_t g_msprof_reg_type_info_ret = 0;
int32_t g_msprof_register_cb_ret = 0;

// 【公开接口】获取回调
ProfCommandHandle GetHixlProfCallback(void) {
    return g_hixl_prof_callback;
}

// 【公开接口】设置桩返回值
void SetMsprofRegTypeInfoRet(int32_t ret) {
    g_msprof_reg_type_info_ret = ret;
}
void SetMsprofRegisterCallbackRet(int32_t ret) {
    g_msprof_register_cb_ret = ret;
}

// ================== 桩实现 ==================
int32_t MsprofRegTypeInfo(uint32_t moduleId, uint32_t typeId, const char* name) {
    return g_msprof_reg_type_info_ret;
}

int32_t MsprofRegisterCallback(uint32_t moduleId, ProfCommandHandle cb) {
    g_hixl_prof_callback = cb; // 保存指针
    return g_msprof_register_cb_ret;
}

uint64_t MsprofSysCycleTime(void) {
    return 123456; // 固定返回
}

int32_t MsprofReportApi(bool flag, void* api) {
    return 0;
}

int32_t MsprofFinalize(void) {
    return 0;
}

#ifdef __cplusplus
}
#endif

