/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <cstring>
#include <string>
#include <thread>
#include "hcomm/hcomm_res_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

HcclResult HcommMemReg(EndpointHandle endPointHandle, const char *memTag, HcommMem mem, void **memHandle) {
  static int32_t mem_num_stub = 1;
  (void)endPointHandle;
  (void)memTag;
  (void)mem;
  *memHandle = reinterpret_cast<void *>(mem_num_stub++);
  return HCCL_SUCCESS;
}

HcclResult HcommMemUnreg(EndpointHandle endPointHandle, void *memHandle) {
  (void)endPointHandle;
  (void)memHandle;
  return HCCL_SUCCESS;
}

HcclResult HcommMemExport(EndpointHandle endPointHandle, void *memHandle, void **memDesc, uint32_t *memDescLen) {
  (void)endPointHandle;
  (void)memHandle;
  static std::string desc = "test_desc2";
  *memDesc = const_cast<char *>(desc.c_str());
  *memDescLen = desc.size();
  return HCCL_SUCCESS;
}

HcclResult HcommEndpointCreate(const EndpointDesc *endPoint, EndpointHandle *endPointHandle) {
  (void)endPoint;
  static int32_t ep_num_stub = 1;
  *endPointHandle = reinterpret_cast<void *>(ep_num_stub++);
  return HCCL_SUCCESS;
}

HcclResult HcommEndpointDestroy(EndpointHandle endPointHandle) {
  (void)endPointHandle;
  return HCCL_SUCCESS;
}

HcclResult HcommMemImport(EndpointHandle endpointHandle, const void *memDesc, uint32_t descLen, HcommMem *outMem) {
  (void)endpointHandle;
  if (memDesc == nullptr || outMem == nullptr || descLen == 0) {
    return HCCL_E_INTERNAL;
  }

  if (descLen == 4 && std::memcmp(memDesc, "FAIL", 4) == 0) {
    return HCCL_E_INTERNAL;
  }

  outMem->addr = const_cast<void *>(memDesc);
  outMem->size = descLen;
  return HCCL_SUCCESS;
}

HcclResult HcommMemUnimport(EndpointHandle endpointHandle, const void *memDesc, uint32_t descLen) {
  (void)endpointHandle;
  (void)memDesc;
  (void)descLen;
  return HCCL_SUCCESS;
}

HcclResult HcommChannelCreate(EndpointHandle endPointHandle, CommEngine engine, HcommChannelDesc *channelDescs,
    uint32_t channelNum, ChannelHandle *channels) {
  (void)endPointHandle;
  (void)engine;
  (void)channelDescs;
  (void)channelNum;
  static int32_t chn_num_stub = 1;
  *channels = static_cast<ChannelHandle>(chn_num_stub++);
  return HCCL_SUCCESS;
}

HcclResult HcommChannelDestroy(const ChannelHandle *channels, uint32_t channelNum) {
  (void)channels;
  (void)channelNum;
  return HCCL_SUCCESS;
}

HcclResult HcommChannelGetStatus(const ChannelHandle *channelList, uint32_t listNum, int32_t *statusList) {
  (void)channelList;
  (void)listNum;
  (void)statusList;
  return HCCL_SUCCESS;
}

int32_t HcommChannelFence(ChannelHandle channel) {
  (void)channel;
  return HCCL_SUCCESS;
}

int32_t HcommWriteNbi(ChannelHandle channel, void *dst, void *src, uint64_t len) {
  (void)channel;
  if (dst == nullptr || src == nullptr || len == 0) {
    return HCCL_E_PARA;
  }
  // 模拟写操作耗时 10ms
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  memcpy_s(dst, len, src, len);
  return HCCL_SUCCESS;
}

int32_t HcommReadNbi(ChannelHandle channel, void *dst, void *src, uint64_t len) {
  (void)channel;
  if (dst == nullptr || src == nullptr || len == 0) {
    return HCCL_E_PARA;
  }
  // 模拟读操作耗时 10ms
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  memcpy_s(dst, len, src, len);
  return HCCL_SUCCESS;
}

#ifdef __cplusplus
}
#endif
