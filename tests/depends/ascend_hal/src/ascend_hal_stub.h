/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_TESTS_DEPENDS_ASCEND_HAL_SRC_ASCEND_HAL_STUB_H_
#define CANN_HIXL_TESTS_DEPENDS_ASCEND_HAL_SRC_ASCEND_HAL_STUB_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void AscendHalStubSetHostRegisterRet(int32_t ret);
void AscendHalStubSetHostUnregisterRet(int32_t ret);
void AscendHalStubReset(void);

#ifdef __cplusplus
}
#endif

#endif  // CANN_HIXL_TESTS_DEPENDS_ASCEND_HAL_SRC_ASCEND_HAL_STUB_H_
