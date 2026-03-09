/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_ACL_COMPAT_H_
#define CANN_HIXL_SRC_ACL_COMPAT_H_

#include "acl/acl.h"

#ifdef __cplusplus
extern "C" {
#endif

__attribute__((weak)) aclError aclrtReserveMemAddressNoUCMemory(void **virPtr, size_t size, size_t alignment,
                                                                void *expectPtr, uint64_t flags);

#ifdef __cplusplus
}
#endif

#endif  // CANN_HIXL_SRC_ACL_COMPAT_H_
