/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm_proxy.h"
#include "common/hixl_checker.h"

extern "C" {
__attribute__((weak)) HcclResult HcommMemReg(EndpointHandle endPointHandle, const char *memTag, HcommMem mem,
                                              void **memHandle);
__attribute__((weak)) HcclResult HcommMemUnreg(EndpointHandle endPointHandle, void *memHandle);
__attribute__((weak)) HcclResult HcommMemExport(EndpointHandle endPointHandle, void *memHandle, void **memDesc,
                                                 uint32_t *memDescLen);
__attribute__((weak)) HcclResult HcommEndpointCreate(const EndpointDesc *endPoint, EndpointHandle *endPointHandle);
__attribute__((weak)) HcclResult HcommEndpointDestroy(EndpointHandle endPointHandle);
__attribute__((weak)) HcclResult HcommMemImport(EndpointHandle endpointHandle, const void *memDesc, uint32_t descLen,
                                                 HcommMem *outMem);
__attribute__((weak)) HcclResult HcommMemUnimport(EndpointHandle endpointHandle, const void *memDesc, uint32_t descLen);
__attribute__((weak)) HcclResult HcommChannelCreate(EndpointHandle endPointHandle, CommEngine engine,
                                                    HcommChannelDesc *channelDescs, uint32_t channelNum,
                                                    ChannelHandle *channels);
__attribute__((weak)) HcclResult HcommChannelDestroy(const ChannelHandle *channels, uint32_t channelNum);
__attribute__((weak)) HcclResult HcommChannelGetStatus(const ChannelHandle *channelList, uint32_t listNum,
                                                       int32_t *statusList);
__attribute__((weak)) int32_t HcommWriteNbiOnThread(ThreadHandle thread, ChannelHandle channel, void *dst,
                                                   const void *src, uint64_t len);
__attribute__((weak)) int32_t HcommReadNbiOnThread(ThreadHandle thread, ChannelHandle channel, void *dst,
                                                  const void *src, uint64_t len);
__attribute__((weak)) HcclResult HcommThreadAlloc(CommEngine engine, uint32_t threadNum, uint32_t notifyNumPerThread,
                                                  ThreadHandle *threadHandle);
__attribute__((weak)) HcclResult HcommThreadFree(const ThreadHandle *threads, uint32_t threadNum);
__attribute__((weak)) int32_t HcommBatchModeStart(const char *batchTag);
__attribute__((weak)) int32_t HcommBatchModeEnd(const char *batchTag);
__attribute__((weak)) int32_t HcommReadOnThread(ThreadHandle thread, ChannelHandle channel, void *dst,
                                                const void *src, uint64_t len);
__attribute__((weak)) int32_t HcommWriteOnThread(ThreadHandle thread, ChannelHandle channel, void *dst,
                                                 const void *src, uint64_t len);
__attribute__((weak)) int32_t HcommChannelFenceOnThread(ThreadHandle thread, ChannelHandle channel);
}

namespace hixl {

HcclResult HcommProxy::MemReg(EndpointHandle endPointHandle, const char *memTag, HcommMem mem, void **memHandle) {
  HIXL_CHK_BOOL_RET_STATUS(&HcommMemReg != nullptr, HCCL_E_NOT_SUPPORT,
                           "function HcommMemReg is null, maybe unsupported.");
  return HcommMemReg(endPointHandle, memTag, mem, memHandle);
}

HcclResult HcommProxy::MemUnreg(EndpointHandle endPointHandle, void *memHandle) {
  HIXL_CHK_BOOL_RET_STATUS(&HcommMemUnreg != nullptr, HCCL_E_NOT_SUPPORT,
                           "function HcommMemUnreg is null, maybe unsupported.");
  return HcommMemUnreg(endPointHandle, memHandle);
}

HcclResult HcommProxy::MemExport(EndpointHandle endPointHandle, void *memHandle, void **memDesc, uint32_t *memDescLen) {
  HIXL_CHK_BOOL_RET_STATUS(&HcommMemExport != nullptr, HCCL_E_NOT_SUPPORT,
                           "function HcommMemExport is null, maybe unsupported.");
  return HcommMemExport(endPointHandle, memHandle, memDesc, memDescLen);
}

HcclResult HcommProxy::EndpointCreate(const EndpointDesc *endPoint, EndpointHandle *endPointHandle) {
  HIXL_CHK_BOOL_RET_STATUS(&HcommEndpointCreate != nullptr, HCCL_E_NOT_SUPPORT,
                           "function HcommEndpointCreate is null, maybe unsupported.");
  return HcommEndpointCreate(endPoint, endPointHandle);
}

HcclResult HcommProxy::EndpointDestroy(EndpointHandle endPointHandle) {
  HIXL_CHK_BOOL_RET_STATUS(&HcommEndpointDestroy != nullptr, HCCL_E_NOT_SUPPORT,
                           "function HcommEndpointDestroy is null, maybe unsupported.");
  return HcommEndpointDestroy(endPointHandle);
}

HcclResult HcommProxy::MemImport(EndpointHandle endpointHandle, const void *memDesc, uint32_t descLen,
                                  HcommMem *outMem) {
  HIXL_CHK_BOOL_RET_STATUS(&HcommMemImport != nullptr, HCCL_E_NOT_SUPPORT,
                           "function HcommMemImport is null, maybe unsupported.");
  return HcommMemImport(endpointHandle, memDesc, descLen, outMem);
}

HcclResult HcommProxy::MemUnimport(EndpointHandle endpointHandle, const void *memDesc, uint32_t descLen) {
  HIXL_CHK_BOOL_RET_STATUS(&HcommMemUnimport != nullptr, HCCL_E_NOT_SUPPORT,
                           "function HcommMemUnimport is null, maybe unsupported.");
  return HcommMemUnimport(endpointHandle, memDesc, descLen);
}

HcclResult HcommProxy::ChannelCreate(EndpointHandle endPointHandle, CommEngine engine, HcommChannelDesc *channelDescs,
                                      uint32_t channelNum, ChannelHandle *channels) {
  HIXL_CHK_BOOL_RET_STATUS(&HcommChannelCreate != nullptr, HCCL_E_NOT_SUPPORT,
                           "function HcommChannelCreate is null, maybe unsupported.");
  return HcommChannelCreate(endPointHandle, engine, channelDescs, channelNum, channels);
}

HcclResult HcommProxy::ChannelDestroy(const ChannelHandle *channels, uint32_t channelNum) {
  HIXL_CHK_BOOL_RET_STATUS(&HcommChannelDestroy != nullptr, HCCL_E_NOT_SUPPORT,
                           "function HcommChannelDestroy is null, maybe unsupported.");
  return HcommChannelDestroy(channels, channelNum);
}

HcclResult HcommProxy::ChannelGetStatus(const ChannelHandle *channelList, uint32_t listNum, int32_t *statusList) {
  HIXL_CHK_BOOL_RET_STATUS(&HcommChannelGetStatus != nullptr, HCCL_E_NOT_SUPPORT,
                           "function HcommChannelGetStatus is null, maybe unsupported.");
  return HcommChannelGetStatus(channelList, listNum, statusList);
}

int32_t HcommProxy::WriteNbiOnThread(ThreadHandle thread, ChannelHandle channel, void *dst, const void *src,
                                      uint64_t len) {
  HIXL_CHK_BOOL_RET_STATUS(&HcommWriteNbiOnThread != nullptr, HCCL_E_NOT_SUPPORT,
                           "function HcommWriteNbiOnThread is null, maybe unsupported.");
  return HcommWriteNbiOnThread(thread, channel, dst, src, len);
}

int32_t HcommProxy::ReadNbiOnThread(ThreadHandle thread, ChannelHandle channel, void *dst, const void *src,
                                    uint64_t len) {
  HIXL_CHK_BOOL_RET_STATUS(&HcommReadNbiOnThread != nullptr, HCCL_E_NOT_SUPPORT,
                           "function HcommReadNbiOnThread is null, maybe unsupported.");
  return HcommReadNbiOnThread(thread, channel, dst, src, len);
}

HcclResult HcommProxy::ThreadAlloc(CommEngine engine, uint32_t threadNum, uint32_t notifyNumPerThread,
                                    ThreadHandle *threadHandle) {
  HIXL_CHK_BOOL_RET_STATUS(&HcommThreadAlloc != nullptr, HCCL_E_NOT_SUPPORT,
                           "function HcommThreadAlloc is null, maybe unsupported.");
  return HcommThreadAlloc(engine, threadNum, notifyNumPerThread, threadHandle);
}

HcclResult HcommProxy::ThreadFree(const ThreadHandle *threads, uint32_t threadNum) {
  HIXL_CHK_BOOL_RET_STATUS(&HcommThreadFree != nullptr, HCCL_E_NOT_SUPPORT,
                           "function HcommThreadFree is null, maybe unsupported.");
  return HcommThreadFree(threads, threadNum);
}

int32_t HcommProxy::BatchModeStart(const char *batchTag) {
  HIXL_CHK_BOOL_RET_STATUS(&HcommBatchModeStart != nullptr, HCCL_E_NOT_SUPPORT,
                           "function HcommBatchModeStart is null, maybe unsupported.");
  return HcommBatchModeStart(batchTag);
}

int32_t HcommProxy::BatchModeEnd(const char *batchTag) {
  HIXL_CHK_BOOL_RET_STATUS(&HcommBatchModeEnd != nullptr, HCCL_E_NOT_SUPPORT,
                           "function HcommBatchModeEnd is null, maybe unsupported.");
  return HcommBatchModeEnd(batchTag);
}

int32_t HcommProxy::ReadOnThread(ThreadHandle thread, ChannelHandle channel, void *dst, const void *src, uint64_t len) {
  HIXL_CHK_BOOL_RET_STATUS(&HcommReadOnThread != nullptr, HCCL_E_NOT_SUPPORT,
                           "function HcommReadOnThread is null, maybe unsupported.");
  return HcommReadOnThread(thread, channel, dst, src, len);
}

int32_t HcommProxy::WriteOnThread(ThreadHandle thread, ChannelHandle channel, void *dst, const void *src,
                                  uint64_t len) {
  HIXL_CHK_BOOL_RET_STATUS(&HcommWriteOnThread != nullptr, HCCL_E_NOT_SUPPORT,
                           "function HcommWriteOnThread is null, maybe unsupported.");
  return HcommWriteOnThread(thread, channel, dst, src, len);
}

int32_t HcommProxy::ChannelFenceOnThread(ThreadHandle thread, ChannelHandle channel) {
  HIXL_CHK_BOOL_RET_STATUS(&HcommChannelFenceOnThread != nullptr, HCCL_E_NOT_SUPPORT,
                           "function HcommChannelFenceOnThread is null, maybe unsupported.");
  return HcommChannelFenceOnThread(thread, channel);
}

}  // namespace hixl
