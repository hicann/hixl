/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef AIR_TESTS_DEPENDS_LLM_DATADIST_SRC_HCCL_STUB_H_
#define AIR_TESTS_DEPENDS_LLM_DATADIST_SRC_HCCL_STUB_H_

#include "runtime/rt.h"
#include "hccl/hccl_mem_comm.h"

HcclResult HcclExchangeMemDesc(HcclComm comm, uint32_t remoteRank, HcclMemDescs *local, int timeout,
                               HcclMemDescs *remote, uint32_t *actualNum);

HcclResult HcclCommInitClusterInfoMemConfig(const char *cluster, uint32_t rank, HcclCommConfig *config, HcclComm *comm);

HcclResult HcclCommDestroy(HcclComm comm);

HcclResult HcclRemapRegistedMemory(HcclComm *comm, HcclMem *memInfoArray, uint64_t commSize, uint64_t arraySize);
HcclResult HcclRegisterGlobalMem(HcclMem *mem, void **memHandle);

HcclResult HcclDeregisterGlobalMem(void *memHandle);

HcclResult HcclCommBindMem(HcclComm comm, void *memHandle);

HcclResult HcclCommUnbindMem(HcclComm comm, void *memHandle);

HcclResult HcclCommPrepare(HcclComm comm, HcclPrepareConfig *prepareConfig, int32_t timeout);

HcclResult HcclBatchPut(HcclComm comm, uint32_t remoteRank, HcclOneSideOpDesc *desc, uint32_t descNum,
                        rtStream_t stream);

HcclResult HcclBatchGet(HcclComm comm, uint32_t remoteRank, HcclOneSideOpDesc *desc, uint32_t descNum,
                        rtStream_t stream);

#endif  // AIR_TESTS_DEPENDS_LLM_DATADIST_SRC_HCCL_STUB_H_
