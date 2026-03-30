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
  static HcclResult MemReg(EndpointHandle endpoint_handle, const char *mem_tag, const CommMem *mem,
                           HcommMemHandle *mem_handle);
  static HcclResult MemUnreg(EndpointHandle endpoint_handle, HcommMemHandle mem_handle);
  static HcclResult MemExport(EndpointHandle endpoint_handle, HcommMemHandle mem_handle, void **mem_desc,
                              uint32_t *mem_desc_len);
  static HcclResult EndpointCreate(const EndpointDesc *endpoint, EndpointHandle *endpoint_handle);
  static HcclResult EndpointDestroy(EndpointHandle endpoint_handle);
  static HcclResult MemImport(EndpointHandle endpoint_handle, const void *mem_desc, uint32_t desc_len, CommMem *out_mem);
  static HcclResult MemUnimport(EndpointHandle endpoint_handle, const void *mem_desc, uint32_t desc_len);
  static HcclResult ChannelCreate(EndpointHandle endpoint_handle, CommEngine engine, HcommChannelDesc *channel_descs,
                                  uint32_t channel_num, ChannelHandle *channels);
  static HcclResult ChannelDestroy(const ChannelHandle *channels, uint32_t channel_num);
  static HcclResult ChannelGetStatus(const ChannelHandle *channel_list, uint32_t list_num, int32_t *status_list);
  static HcclResult ThreadAlloc(CommEngine engine, uint32_t thread_num, const uint32_t *notify_num_per_thread,
                                ThreadHandle *threads);
  static HcclResult ThreadFree(const ThreadHandle *threads, uint32_t thread_num);

  static int32_t WriteNbiOnThread(ThreadHandle thread, ChannelHandle channel, void *dst, const void *src, uint64_t len);
  static int32_t ReadNbiOnThread(ThreadHandle thread, ChannelHandle channel, void *dst, const void *src, uint64_t len);
  static int32_t BatchModeStart(const char *batch_tag);
  static int32_t BatchModeEnd(const char *batch_tag);
  static int32_t ReadOnThread(ThreadHandle thread, ChannelHandle channel, void *dst, const void *src, uint64_t len);
  static int32_t WriteOnThread(ThreadHandle thread, ChannelHandle channel, void *dst, const void *src, uint64_t len);
  static int32_t ChannelFenceOnThread(ThreadHandle thread, ChannelHandle channel);
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_PROXY_HCOMM_PROXY_H_
