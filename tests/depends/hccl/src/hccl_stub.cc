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
#include "hccl/hccl_types.h"
#include "hcomm/hcomm_res_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

HcommResult HcommMemReg(EndpointHandle endPointHandle, const char *memTag, const CommMem *mem,
                        HcommMemHandle *memHandle) {
  static int32_t mem_num_stub = 1;
  (void)endPointHandle;
  (void)memTag;
  (void)mem;
  *memHandle = reinterpret_cast<void *>(static_cast<uintptr_t>(mem_num_stub++));
  return static_cast<HcommResult>(HCCL_SUCCESS);
}

HcommResult HcommMemUnreg(EndpointHandle endPointHandle, HcommMemHandle memHandle) {
  (void)endPointHandle;
  (void)memHandle;
  return static_cast<HcommResult>(HCCL_SUCCESS);
}

HcommResult HcommMemExport(EndpointHandle endPointHandle, HcommMemHandle memHandle, void **memDesc,
                           uint32_t *memDescLen) {
  (void)endPointHandle;
  (void)memHandle;
  static std::string desc = "test_desc2";
  *memDesc = const_cast<char *>(desc.c_str());
  *memDescLen = desc.size();
  return static_cast<HcommResult>(HCCL_SUCCESS);
}

HcommResult HcommEndpointCreate(const EndpointDesc *endPoint, EndpointHandle *endPointHandle) {
  (void)endPoint;
  static int32_t ep_num_stub = 1;
  *endPointHandle = reinterpret_cast<void *>(ep_num_stub++);
  return static_cast<HcommResult>(HCCL_SUCCESS);
}

HcommResult HcommEndpointDestroy(EndpointHandle endPointHandle) {
  (void)endPointHandle;
  return static_cast<HcommResult>(HCCL_SUCCESS);
}

HcommResult HcommMemImport(EndpointHandle endpointHandle, const void *memDesc, uint32_t descLen, CommMem *outMem) {
  (void)endpointHandle;
  if (memDesc == nullptr || outMem == nullptr || descLen == 0) {
    return HCCL_E_INTERNAL;
  }

  if (descLen == 4 && std::memcmp(memDesc, "FAIL", 4) == 0) {
    return HCCL_E_INTERNAL;
  }

  outMem->addr = const_cast<void *>(memDesc);
  outMem->size = descLen;
  return static_cast<HcommResult>(HCCL_SUCCESS);
}

HcommResult HcommMemUnimport(EndpointHandle endpointHandle, const void *memDesc, uint32_t descLen) {
  (void)endpointHandle;
  (void)memDesc;
  (void)descLen;
  return static_cast<HcommResult>(HCCL_SUCCESS);
}

HcommResult HcommChannelCreate(EndpointHandle endPointHandle, CommEngine engine, HcommChannelDesc *channelDescs,
    uint32_t channelNum, ChannelHandle *channels) {
  (void)endPointHandle;
  (void)engine;
  (void)channelDescs;
  (void)channelNum;
  static int32_t chn_num_stub = 1;
  *channels = static_cast<ChannelHandle>(chn_num_stub++);
  return static_cast<HcommResult>(HCCL_SUCCESS);
}

HcommResult HcommChannelDestroy(const ChannelHandle *channels, uint32_t channelNum) {
  (void)channels;
  (void)channelNum;
  return static_cast<HcommResult>(HCCL_SUCCESS);
}

HcommResult HcommChannelGetStatus(const ChannelHandle *channelList, uint32_t listNum, int32_t *statusList) {
  (void)channelList;
  (void)listNum;
  (void)statusList;
  return static_cast<HcommResult>(HCCL_SUCCESS);
}


int32_t HcommWriteNbiOnThread(ThreadHandle thread, ChannelHandle channel, void *dst, void *src, uint64_t len) {
  (void)thread;
  (void)channel;
  if (dst == nullptr || src == nullptr || len == 0) {
    return HCCL_E_PARA;
  }
  // 模拟写操作耗时 1ms
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
  memcpy_s(dst, len, src, len);
  return HCCL_SUCCESS;
}

int32_t HcommReadNbiOnThread(ThreadHandle thread, ChannelHandle channel, void *dst, void *src, uint64_t len) {
  (void)thread;
  (void)channel;
  if (dst == nullptr || src == nullptr || len == 0) {
    return HCCL_E_PARA;
  }
  // 模拟读操作耗时 1ms
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
  memcpy_s(dst, len, src, len);
  return HCCL_SUCCESS;
}

HcommResult HcommThreadAlloc(CommEngine engine, uint32_t threadNum, const uint32_t *notifyNumPerThread,
                             ThreadHandle *threads) {
  (void)engine;
  (void)notifyNumPerThread;
  if (threads == nullptr) {
    return static_cast<HcommResult>(HCCL_E_PARA);
  }
  for (uint32_t i = 0; i < threadNum; ++i) {
    threads[i] = static_cast<ThreadHandle>(999U + i);
  }
  return static_cast<HcommResult>(HCCL_SUCCESS);
}

HcommResult HcommThreadFree(const ThreadHandle *threads, uint32_t threadNum) {
  (void)threads;
  (void)threadNum;
  return static_cast<HcommResult>(HCCL_SUCCESS);
}

int32_t HcommBatchModeStart(const char *batchTag) {
  (void)batchTag;
  return HCCL_SUCCESS;
}

int32_t HcommBatchModeEnd(const char *batchTag) {
  (void)batchTag;
  return HCCL_SUCCESS;
}

int32_t HcommReadOnThread(ThreadHandle thread, ChannelHandle channel, void *dst, const void *src, uint64_t len) {
  (void)thread;
  (void)channel;
  if (dst == nullptr || src == nullptr || len == 0) {
    return HCCL_E_PARA;
  }
  // 模拟读操作耗时 1ms
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
  memcpy_s(dst, len, src, len);
  return HCCL_SUCCESS;
}

int32_t HcommWriteOnThread(ThreadHandle thread, ChannelHandle channel, void *dst, const void *src, uint64_t len) {
  (void)thread;
  (void)channel;
  if (dst == nullptr || src == nullptr || len == 0) {
    return HCCL_E_PARA;
  }
  // 模拟写操作耗时 1ms
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
  memcpy_s(dst, len, src, len);
  return HCCL_SUCCESS;
}

int32_t HcommChannelFenceOnThread(ThreadHandle thread, ChannelHandle channel) {
  (void)thread;
  (void)channel;
  return HCCL_SUCCESS;
}
#ifdef __cplusplus
}
#endif
