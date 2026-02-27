/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#ifndef CANN_HIXL_SRC_HIXL_OPS_HIXL_KERNEL_HIXL_BATCH_TRANSFER_H_
#define CANN_HIXL_SRC_HIXL_OPS_HIXL_KERNEL_HIXL_BATCH_TRANSFER_H_

#include "cs/hcomm_compat.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 批量读取server侧的内存内容
 */
struct HixlOneSideOpParam {
  ThreadHandle thread;        ///< [in] 线程句柄
  ChannelHandle channel;      ///< [in] 通道句柄
  uint32_t list_num;          ///< [in] 本次传输任务的数目
  void **dst_buf_addr_list;   ///< [in] 记录了本次传输任务中每组的目标侧内存地址
  void **src_buf_addr_list;   ///< [in] 记录了本次传输任务中每组的源侧内存地址
  uint64_t *len_list;         ///< [in] 记录了本次传输任务中每组任务的内存块大小
  uint64_t remote_flag_addr;  ///< [in] 记录了本次传输任务中remote_flag的内存地址
  uint64_t local_flag_addr;   ///< [in] 记录了本次传输任务中local_flag的内存地址
  uint32_t flag_size;         ///< [in] 记录了本次传输任务中flag的内存大小
};

uint32_t HixlBatchPut(HixlOneSideOpParam *param);

uint32_t HixlBatchGet(HixlOneSideOpParam *param);
#ifdef __cplusplus
}
#endif

#endif
