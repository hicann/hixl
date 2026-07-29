/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#include "aicpu_stub.h"
#include "aicpu/aicpu_schedule/aicpu_context.h"

namespace {
struct AicpuContextMock {
  uint32_t device_id = 0U;
  uint32_t ts_id = 0U;
  int32_t host_pid = 1000;
  uint32_t vf_id = 0U;
  int32_t ret = 0;
};

AicpuContextMock g_aicpu_context_mock;

void ResetMock() {
  g_aicpu_context_mock = AicpuContextMock{};
}
}  // namespace

namespace aicpu_test {
void SetAicpuGetContextResult(uint32_t device_id, uint32_t ts_id, int32_t host_pid, uint32_t vf_id, int32_t ret) {
  g_aicpu_context_mock.device_id = device_id;
  g_aicpu_context_mock.ts_id = ts_id;
  g_aicpu_context_mock.host_pid = host_pid;
  g_aicpu_context_mock.vf_id = vf_id;
  g_aicpu_context_mock.ret = ret;
}

void ResetAicpuGetContextResult() {
  ResetMock();
}
}  // namespace aicpu_test

namespace aicpu {
status_t aicpuGetContext(aicpuContext_t *ctx) {
  if (ctx == nullptr) {
    return AICPU_ERROR_FAILED;
  }
  ctx->deviceId = g_aicpu_context_mock.device_id;
  ctx->tsId = g_aicpu_context_mock.ts_id;
  ctx->hostPid = g_aicpu_context_mock.host_pid;
  ctx->vfId = g_aicpu_context_mock.vf_id;
  return static_cast<status_t>(g_aicpu_context_mock.ret);
}
}  // namespace aicpu
