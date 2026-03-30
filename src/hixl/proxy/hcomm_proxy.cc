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
__attribute__((weak)) HcommResult HcommEndpointCreate(const EndpointDesc *endpoint, EndpointHandle *endpoint_handle);

__attribute__((weak)) HcommResult HcommEndpointDestroy(EndpointHandle endpoint_handle);

__attribute__((weak)) HcommResult HcommMemReg(EndpointHandle endpoint_handle, const char *mem_tag, const CommMem *mem,
                                              HcommMemHandle *mem_handle);

__attribute__((weak)) HcommResult HcommMemUnreg(EndpointHandle endpoint_handle, HcommMemHandle mem_handle);

__attribute__((weak)) HcommResult HcommMemExport(EndpointHandle endpoint_handle, HcommMemHandle mem_handle,
                                                 void **memDesc, uint32_t *mem_desc_len);

__attribute__((weak)) HcommResult HcommMemImport(EndpointHandle endpoint_handle, const void *memDesc, uint32_t desc_len,
                                                 CommMem *out_mem);

__attribute__((weak)) HcommResult HcommMemUnimport(EndpointHandle endpoint_handle, const void *memDesc,
                                                   uint32_t desc_len);

__attribute__((weak)) HcommResult HcommChannelCreate(EndpointHandle endpoint_handle, CommEngine engine,
                                                     HcommChannelDesc *channel_descs, uint32_t channel_num,
                                                     ChannelHandle *channels);

__attribute__((weak)) HcommResult HcommChannelGetStatus(const ChannelHandle *channel_list, uint32_t list_num,
                                                        int32_t *statusList);

__attribute__((weak)) HcommResult HcommChannelDestroy(const ChannelHandle *channels, uint32_t channel_num);

__attribute__((weak)) HcommResult HcommThreadAlloc(CommEngine engine, uint32_t thread_num,
                                                   const uint32_t *notify_num_per_thread, ThreadHandle *threads);

__attribute__((weak)) HcommResult HcommThreadFree(const ThreadHandle *threads, uint32_t thread_num);

__attribute__((weak)) int32_t HcommWriteNbiOnThread(ThreadHandle thread, ChannelHandle channel, void *dst,
                                                    const void *src, uint64_t len);
__attribute__((weak)) int32_t HcommReadNbiOnThread(ThreadHandle thread, ChannelHandle channel, void *dst,
                                                   const void *src, uint64_t len);

__attribute__((weak)) int32_t HcommBatchModeStart(const char *batch_tag);
__attribute__((weak)) int32_t HcommBatchModeEnd(const char *batch_tag);

__attribute__((weak)) int32_t HcommReadOnThread(ThreadHandle thread, ChannelHandle channel, void *dst, const void *src,
                                                uint64_t len);
__attribute__((weak)) int32_t HcommWriteOnThread(ThreadHandle thread, ChannelHandle channel, void *dst, const void *src,
                                                 uint64_t len);
__attribute__((weak)) int32_t HcommChannelFenceOnThread(ThreadHandle thread, ChannelHandle channel);
}

namespace hixl {
HcclResult HcommProxy::MemReg(EndpointHandle endpoint_handle, const char *mem_tag, const CommMem *mem,
                              HcommMemHandle *mem_handle) {
  HIXL_CHK_BOOL_RET_STATUS(&HcommMemReg != nullptr, HCCL_E_NOT_SUPPORT,
                           "function HcommMemReg is null, maybe unsupported.");
  return static_cast<HcclResult>(HcommMemReg(endpoint_handle, mem_tag, mem, mem_handle));
}

HcclResult HcommProxy::MemUnreg(EndpointHandle endpoint_handle, void *mem_handle) {
  HIXL_CHK_BOOL_RET_STATUS(&HcommMemUnreg != nullptr, HCCL_E_NOT_SUPPORT,
                           "function HcommMemUnreg is null, maybe unsupported.");
  return static_cast<HcclResult>(HcommMemUnreg(endpoint_handle, mem_handle));
}

HcclResult HcommProxy::MemExport(EndpointHandle endpoint_handle, void *mem_handle, void **mem_desc,
                                 uint32_t *mem_desc_len) {
  HIXL_CHK_BOOL_RET_STATUS(&HcommMemExport != nullptr, HCCL_E_NOT_SUPPORT,
                           "function HcommMemExport is null, maybe unsupported.");
  return static_cast<HcclResult>(HcommMemExport(endpoint_handle, mem_handle, mem_desc, mem_desc_len));
}

HcclResult HcommProxy::EndpointCreate(const EndpointDesc *endpoint, EndpointHandle *endpoint_handle) {
  HIXL_CHK_BOOL_RET_STATUS(&HcommEndpointCreate != nullptr, HCCL_E_NOT_SUPPORT,
                           "function HcommEndpointCreate is null, maybe unsupported.");
  return static_cast<HcclResult>(HcommEndpointCreate(endpoint, endpoint_handle));
}

HcclResult HcommProxy::EndpointDestroy(EndpointHandle endpoint_handle) {
  HIXL_CHK_BOOL_RET_STATUS(&HcommEndpointDestroy != nullptr, HCCL_E_NOT_SUPPORT,
                           "function HcommEndpointDestroy is null, maybe unsupported.");
  return static_cast<HcclResult>(HcommEndpointDestroy(endpoint_handle));
}

HcclResult HcommProxy::MemImport(EndpointHandle endpoint_handle, const void *mem_desc, uint32_t desc_len,
                                 CommMem *out_mem) {
  HIXL_CHK_BOOL_RET_STATUS(&HcommMemImport != nullptr, HCCL_E_NOT_SUPPORT,
                           "function HcommMemImport is null, maybe unsupported.");
  return static_cast<HcclResult>(HcommMemImport(endpoint_handle, mem_desc, desc_len, out_mem));
}

HcclResult HcommProxy::MemUnimport(EndpointHandle endpoint_handle, const void *mem_desc, uint32_t desc_len) {
  HIXL_CHK_BOOL_RET_STATUS(&HcommMemUnimport != nullptr, HCCL_E_NOT_SUPPORT,
                           "function HcommMemUnimport is null, maybe unsupported.");
  return static_cast<HcclResult>(HcommMemUnimport(endpoint_handle, mem_desc, desc_len));
}

HcclResult HcommProxy::ChannelCreate(EndpointHandle endpoint_handle, CommEngine engine, HcommChannelDesc *channel_descs,
                                     uint32_t channel_num, ChannelHandle *channels) {
  HIXL_CHK_BOOL_RET_STATUS(&HcommChannelCreate != nullptr, HCCL_E_NOT_SUPPORT,
                           "function HcommChannelCreate is null, maybe unsupported.");
  return static_cast<HcclResult>(HcommChannelCreate(endpoint_handle, engine, channel_descs, channel_num, channels));
}

HcclResult HcommProxy::ChannelDestroy(const ChannelHandle *channels, uint32_t channel_num) {
  HIXL_CHK_BOOL_RET_STATUS(&HcommChannelDestroy != nullptr, HCCL_E_NOT_SUPPORT,
                           "function HcommChannelDestroy is null, maybe unsupported.");
  return static_cast<HcclResult>(HcommChannelDestroy(channels, channel_num));
}

HcclResult HcommProxy::ChannelGetStatus(const ChannelHandle *channel_list, uint32_t list_num, int32_t *status_list) {
  HIXL_CHK_BOOL_RET_STATUS(&HcommChannelGetStatus != nullptr, HCCL_E_NOT_SUPPORT,
                           "function HcommChannelGetStatus is null, maybe unsupported.");
  return static_cast<HcclResult>(HcommChannelGetStatus(channel_list, list_num, status_list));
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

HcclResult HcommProxy::ThreadAlloc(CommEngine engine, uint32_t thread_num, const uint32_t *notify_num_per_thread,
                                   ThreadHandle *threads) {
  HIXL_CHK_BOOL_RET_STATUS(&HcommThreadAlloc != nullptr, HCCL_E_NOT_SUPPORT,
                           "function HcommThreadAlloc is null, maybe unsupported.");
  return static_cast<HcclResult>(HcommThreadAlloc(engine, thread_num, notify_num_per_thread, threads));
}

HcclResult HcommProxy::ThreadFree(const ThreadHandle *threads, uint32_t thread_num) {
  HIXL_CHK_BOOL_RET_STATUS(&HcommThreadFree != nullptr, HCCL_E_NOT_SUPPORT,
                           "function HcommThreadFree is null, maybe unsupported.");
  return static_cast<HcclResult>(HcommThreadFree(threads, thread_num));
}

int32_t HcommProxy::BatchModeStart(const char *batch_tag) {
  HIXL_CHK_BOOL_RET_STATUS(&HcommBatchModeStart != nullptr, HCCL_E_NOT_SUPPORT,
                           "function HcommBatchModeStart is null, maybe unsupported.");
  return HcommBatchModeStart(batch_tag);
}

int32_t HcommProxy::BatchModeEnd(const char *batch_tag) {
  HIXL_CHK_BOOL_RET_STATUS(&HcommBatchModeEnd != nullptr, HCCL_E_NOT_SUPPORT,
                           "function HcommBatchModeEnd is null, maybe unsupported.");
  return HcommBatchModeEnd(batch_tag);
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
