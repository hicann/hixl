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

// 用于测试重试逻辑的全局计数器
// 当计数器 >= 10 时，传输任务返回 HCCL_RETRY_REQUIRED (20)，触发重试
// 当执行 HcommChannelFenceOnThread 时，计数器重置为 0
static uint32_t g_transfer_retry_counter = 0;

// 控制失败模式的全局变量
// 设置后下一次调用会返回指定的错误码（既不是成功也不是重试所需）
static int32_t g_next_nbi_failure_ret = 0;    // 下一次NBI传输的返回值
static int32_t g_next_fence_failure_ret = 0;  // 下一次Fence的返回值

// 辅助函数：执行 NBI 传输并处理重试逻辑
static int32_t DoNbiTransferWithRetry(void *dst, const void *src, uint64_t len) {
  if (dst == nullptr || src == nullptr || len == 0) {
    return HCCL_E_PARA;
  }
  // 模拟传输操作耗时 1ms
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
  memcpy_s(dst, len, src, len);

  // 重试测试：前10次正常返回成功，第11次开始返回需要重试的错误码
  g_transfer_retry_counter++;
  if (g_transfer_retry_counter >= 10) {
    return 20;  // HCCL_RETRY_REQUIRED
  }
  return HCCL_SUCCESS;
}

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

// NBI传输公共辅助函数（处理失败注入）
static int32_t DoNbiTransferWithFailureInjection(void *dst, const void *src, uint64_t len) {
  if (g_next_nbi_failure_ret != 0) {
    int32_t ret = g_next_nbi_failure_ret;
    g_next_nbi_failure_ret = 0;
    return ret;
  }
  return DoNbiTransferWithRetry(dst, src, len);
}

int32_t HcommWriteNbiOnThread(ThreadHandle thread, ChannelHandle channel, void *dst, void *src, uint64_t len) {
  (void)thread;
  (void)channel;
  return DoNbiTransferWithFailureInjection(dst, src, len);
}

int32_t HcommReadNbiOnThread(ThreadHandle thread, ChannelHandle channel, void *dst, void *src, uint64_t len) {
  (void)thread;
  (void)channel;
  return DoNbiTransferWithFailureInjection(dst, src, len);
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

// 基本传输公共辅助函数
static int32_t DoBasicTransfer(void *dst, const void *src, uint64_t len) {
  if (dst == nullptr || src == nullptr || len == 0) {
    return HCCL_E_PARA;
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
  memcpy_s(dst, len, src, len);
  return HCCL_SUCCESS;
}

int32_t HcommReadOnThread(ThreadHandle thread, ChannelHandle channel, void *dst, const void *src, uint64_t len) {
  (void)thread;
  (void)channel;
  return DoBasicTransfer(dst, src, len);
}

int32_t HcommWriteOnThread(ThreadHandle thread, ChannelHandle channel, void *dst, const void *src, uint64_t len) {
  (void)thread;
  (void)channel;
  return DoBasicTransfer(dst, src, len);
}

int32_t HcommChannelFenceOnThread(ThreadHandle thread, ChannelHandle channel) {
  (void)thread;
  (void)channel;
  // 检查是否设置了Fence失败模式
  if (g_next_fence_failure_ret != 0) {
    int32_t ret = g_next_fence_failure_ret;
    g_next_fence_failure_ret = 0;
    return ret;
  }
  // 重置传输计数器，允许传输任务重新开始计数
  g_transfer_retry_counter = 0;
  return HCCL_SUCCESS;
}

// 设置下一次NBI传输返回指定错误码
void SetNextNbiFailure(int32_t ret) {
  g_next_nbi_failure_ret = ret;
}

// 设置下一次Fence返回指定错误码
void SetNextFenceFailure(int32_t ret) {
  g_next_fence_failure_ret = ret;
}

// 重置传输计数器
void ResetTransferCounter() {
  g_transfer_retry_counter = 0;
}

#ifdef __cplusplus
}
#endif
