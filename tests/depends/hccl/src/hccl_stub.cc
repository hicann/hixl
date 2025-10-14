/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.

 * The code snippet comes from Huawei's open-source Mindspore project.
 * Copyright 2019-2020 Huawei Technologies Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 */

#include "hccl/hcom.h"
#include "hccl/hcom_executor.h"

HcclResult hcom_all_gather(const char *tag, void *input_count_ptr, void *output_ptr, u64 input_count,
                           HcclDataType data_type, const char *group, rtStream_t stream) {
  return HCCL_SUCCESS;
}

HcclResult hcom_broadcast(const char *tag, void *ptr, u64 count, HcclDataType data_type, u32 root,
                          const char *group, rtStream_t stream) {
  return HCCL_SUCCESS;
}

HcclResult hcom_all_reduce(const char *tag, void *input_ptr, void *output_ptr, u64 count, HcclDataType data_type,
                           HcclReduceOp op, const char *group, rtStream_t stream) {
  return HCCL_SUCCESS;
}

HcclResult hcom_get_split_strategy(const char *group, const struct model_feature *feature, u32 max_segment_num,
                                   u32 *segment_num, u32 *segment_idx) {
  return HCCL_SUCCESS;
}

HcclResult hcom_reduce_scatter(const char *tag, void *input_ptr, void *output_ptr, u64 count,
                               HcclDataType data_type, HcclReduceOp op, const char *group, rtStream_t stream) {
  return HCCL_SUCCESS;
}

HcclResult HcomExecEnqueueAllToAllV(HcomAllToAllVParams params, std::function<void(HcclResult status)> callback) {
  return HCCL_SUCCESS;
}

HcclResult HcomExecEnqueueAllToAllVC(HcomAllToAllVCParams params, std::function<void(HcclResult status)> callback) {
  return HCCL_SUCCESS;
}

HcclResult HcomExecEnqueueGatherAllToAllV(HcomGatherAllToAllVParams params,
std::function<void(HcclResult status)> callback) {
  return HCCL_SUCCESS;
}

HcclResult HcomExecEnqueueRemoteOperation(HcomRemoteOperation opInfo, std::function<void(HcclResult status)> callback) {
  (void)callback(HCCL_SUCCESS);
  return HCCL_SUCCESS;
}

