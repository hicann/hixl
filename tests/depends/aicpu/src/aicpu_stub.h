/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#ifndef TEST_AICPU_STUB_H_
#define TEST_AICPU_STUB_H_

#include <cstdint>

namespace aicpu_test {
void SetAicpuGetContextResult(uint32_t device_id, uint32_t ts_id, int32_t host_pid, uint32_t vf_id, int32_t ret);
void ResetAicpuGetContextResult();
}  // namespace aicpu_test

#endif  // TEST_AICPU_STUB_H_
