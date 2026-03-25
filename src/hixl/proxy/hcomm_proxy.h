/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_PROXY_HCOMM_PROXY_H_
#define CANN_HIXL_SRC_PROXY_HCOMM_PROXY_H_

#include "hcomm/hcomm_res_defs.h"
#include "hccl/hccl_types.h"

namespace hixl {

class HcommProxy {
 public:
  static HcclResult MemReg(EndpointHandle endpointHandle, const char *memTag, const CommMem *mem,
                           HcommMemHandle *memHandle);
  static HcclResult MemUnreg(EndpointHandle endpointHandle, HcommMemHandle memHandle);
  static HcclResult MemExport(EndpointHandle endpointHandle, HcommMemHandle memHandle, void **memDesc,
                              uint32_t *memDescLen);
  static HcclResult EndpointCreate(const EndpointDesc *endpoint, EndpointHandle *endpointHandle);
  static HcclResult EndpointDestroy(EndpointHandle endPointHandle);
  static HcclResult MemImport(EndpointHandle endpointHandle, const void *memDesc, uint32_t descLen, CommMem *outMem);
  static HcclResult MemUnimport(EndpointHandle endpointHandle, const void *memDesc, uint32_t descLen);
  static HcclResult ChannelCreate(EndpointHandle endpointHandle, CommEngine engine, HcommChannelDesc *channelDescs,
                                  uint32_t channelNum, ChannelHandle *channels);
  static HcclResult ChannelDestroy(const ChannelHandle *channels, uint32_t channelNum);
  static HcclResult ChannelGetStatus(const ChannelHandle *channelList, uint32_t listNum, int32_t *statusList);
  static HcclResult ThreadAlloc(CommEngine engine, uint32_t threadNum, const uint32_t *notifyNumPerThread,
                                ThreadHandle *threads);
  static HcclResult ThreadFree(const ThreadHandle *threads, uint32_t threadNum);

  static int32_t WriteNbiOnThread(ThreadHandle thread, ChannelHandle channel, void *dst, const void *src, uint64_t len);
  static int32_t ReadNbiOnThread(ThreadHandle thread, ChannelHandle channel, void *dst, const void *src, uint64_t len);
  static int32_t BatchModeStart(const char *batchTag);
  static int32_t BatchModeEnd(const char *batchTag);
  static int32_t ReadOnThread(ThreadHandle thread, ChannelHandle channel, void *dst, const void *src, uint64_t len);
  static int32_t WriteOnThread(ThreadHandle thread, ChannelHandle channel, void *dst, const void *src, uint64_t len);
  static int32_t ChannelFenceOnThread(ThreadHandle thread, ChannelHandle channel);
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_PROXY_HCOMM_PROXY_H_
