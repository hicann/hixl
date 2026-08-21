/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HIXL_SRC_LLM_DATADIST_COMM_ADAPTER_COMM_TYPES_H_
#define HIXL_SRC_LLM_DATADIST_COMM_ADAPTER_COMM_TYPES_H_
#include "hccl/base.h"
#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus
const u32 HCCL_MEM_DESC_LENGTH = 504;

typedef struct {
  u32 localRank;
  u32 remoteRank;
  char desc[HCCL_MEM_DESC_LENGTH];
} HcclMemDesc;

typedef struct {
  HcclMemDesc *array;
  u32 arrayLength;
} HcclMemDescs;

typedef struct {
  void *localAddr;
  void *remoteAddr;
  u64 count;
  HcclDataType dataType;
} HcclOneSideOpDesc;

typedef enum tagHcclTopoType {
  HCCL_TOPO_FULLMESH,
  HCCL_TOPO_NUM,
} HcclTopoType;

typedef struct {
  HcclTopoType topoType;
  uint64_t rsvd0;
  uint64_t rsvd1;
  uint64_t rsvd2;
} HcclPrepareConfig;
#ifdef __cplusplus
}
#endif  // __cplusplus
#endif
