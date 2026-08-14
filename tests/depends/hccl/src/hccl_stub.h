/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef AIR_TESTS_DEPENDS_HCCL_SRC_HCCL_STUB_H_
#define AIR_TESTS_DEPENDS_HCCL_SRC_HCCL_STUB_H_

#include <cstdint>
#include "hcomm/hcomm_exception.h"
#include "hcomm/hcomm_res_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

void SetNextNbiFailure(int32_t ret);
void SetNextFenceFailure(int32_t ret);
void SetNextEndpointDestroyFailure(int32_t ret);
void SetNextBatchModeStartFailure(int32_t ret);
void SetNextBatchModeEndFailure(int32_t ret);
void SetListenPortResult(int32_t ret);
void SetChannelGetStatusPendingCount(uint32_t count);
uint32_t GetChannelGetStatusCallCount();
void SetChannelGetStatusFailValue(int32_t status);
uint32_t GetNbiCallCount();
uint32_t GetFenceCallCount();
void ResetTransferCounter();
void ResetMemRegRecord();
uint32_t GetMemRegRecordCount();
int32_t GetMemRegRecordType(uint32_t index);
void ResetChannelCreateRecord();
bool GetLastChannelCreateDesc(HcommChannelDesc *desc);

void SetRegisterExceptionResult(int32_t ret);
void SetUnregisterExceptionResult(int32_t ret);
HcommExceptionCallback GetRegisteredExceptionCallback();
void *GetRegisteredExceptionUserData();
void ResetExceptionCallbackStub();

#ifdef __cplusplus
}
#endif

#endif
