/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "ascend_hal_stub.h"

#include <cstdint>

static int32_t g_host_register_ret = 0;
static int32_t g_host_unregister_ret = 0;
static int32_t g_mem_reg_ub_segment_ret = 0;
static int32_t g_mem_unreg_ub_segment_ret = 0;

#ifdef __cplusplus
extern "C" {
#endif

int32_t halHostRegister(void *src_ptr, uint64_t size, uint32_t flag, uint32_t devid, void **dst_ptr) {
  (void)size;
  (void)flag;
  (void)devid;
  if (dst_ptr == nullptr) {
    return -1;
  }
  if (g_host_register_ret != 0) {
    return g_host_register_ret;
  }
  // Peer VA: return src so the mapped pointer remains a valid host-accessible address in UT.
  *dst_ptr = src_ptr;
  return 0;
}

int32_t halHostUnregister(void *ptr, uint32_t devid) {
  (void)ptr;
  (void)devid;
  return g_host_unregister_ret;
}

int32_t halMemRegUbSegment(uint32_t devid, uint64_t va, uint64_t size) {
  (void)devid;
  (void)va;
  (void)size;
  return g_mem_reg_ub_segment_ret;
}

int32_t halMemUnRegUbSegment(uint32_t devid, uint64_t va, uint64_t size) {
  (void)devid;
  (void)va;
  (void)size;
  return g_mem_unreg_ub_segment_ret;
}

void AscendHalStubSetHostRegisterRet(int32_t ret) {
  g_host_register_ret = ret;
}

void AscendHalStubSetHostUnregisterRet(int32_t ret) {
  g_host_unregister_ret = ret;
}

void AscendHalStubSetMemRegUbSegmentRet(int32_t ret) {
  g_mem_reg_ub_segment_ret = ret;
}

void AscendHalStubSetMemUnRegUbSegmentRet(int32_t ret) {
  g_mem_unreg_ub_segment_ret = ret;
}

void AscendHalStubReset(void) {
  g_host_register_ret = 0;
  g_host_unregister_ret = 0;
  g_mem_reg_ub_segment_ret = 0;
  g_mem_unreg_ub_segment_ret = 0;
}

#ifdef __cplusplus
}
#endif
