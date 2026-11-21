/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HIXL_HCCL_HOST_EXCEPTION_STUB_H_
#define HIXL_HCCL_HOST_EXCEPTION_STUB_H_

#include "hcomm/hcomm_exception.h"

extern "C" {
void SetRegisterHostExceptionResult(int32_t ret);
void SetUnregisterHostExceptionResult(int32_t ret);
void SetRegisterHostExceptionReentrantInfo(uint32_t device_id, uint64_t thread);
void ClearRegisterHostExceptionReentrantInfo();
HcommExceptionCallback GetRegisteredHostExceptionCallback();
void *GetRegisteredHostExceptionUserData();
void ResetHostExceptionCallbackStub();
}
#endif  // HIXL_HCCL_HOST_EXCEPTION_STUB_H_
