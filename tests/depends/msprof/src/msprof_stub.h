/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef AIR_TESTS_DEPENDS_MMPA_SRC_MSPROF_STUB_H_
#define AIR_TESTS_DEPENDS_MMPA_SRC_MSPROF_STUB_H_

#include <stdint.h>

#define PROF_CTRL_SWITCH 1

#ifdef __cplusplus
extern "C" {
#endif

// 函数指针类型定义
typedef int32_t (*ProfCommandHandle)(uint32_t, void*, uint32_t);

ProfCommandHandle GetHixlProfCallback(void);

void SetMsprofRegTypeInfoRet(int32_t ret);
void SetMsprofRegisterCallbackRet(int32_t ret);

#ifdef __cplusplus
}
#endif

#endif // AIR_TESTS_DEPENDS_MMPA_SRC_MSPROF_STUB_H_
