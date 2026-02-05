/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HCOMM_COMPAT_H_
#define CANN_HIXL_SRC_HCOMM_COMPAT_H_

#include "hcomm/hcomm_res_defs.h"
#include "common/hixl_log.h"

#ifdef __cplusplus
extern "C" {
#endif

__attribute__((weak)) HcclResult HcommMemReg(EndpointHandle endPointHandle, const char *memTag, HcommMem mem, void **memHandle);
__attribute__((weak)) HcclResult HcommMemUnreg(EndpointHandle endPointHandle, void *memHandle);
__attribute__((weak)) HcclResult HcommMemExport(EndpointHandle endPointHandle, void *memHandle, void **memDesc, uint32_t *memDescLen);
__attribute__((weak)) HcclResult HcommEndpointCreate(const EndpointDesc *endPoint, EndpointHandle *endPointHandle);
__attribute__((weak)) HcclResult HcommEndpointDestroy(EndpointHandle endPointHandle);
__attribute__((weak)) HcclResult HcommMemImport(EndpointHandle endpointHandle, const void *memDesc, uint32_t descLen, HcommMem *outMem);
__attribute__((weak)) HcclResult HcommMemUnimport(EndpointHandle endpointHandle, const void *memDesc, uint32_t descLen);
__attribute__((weak)) HcclResult HcommChannelCreate(EndpointHandle endPointHandle, CommEngine engine, HcommChannelDesc *channelDescs,
    uint32_t channelNum, ChannelHandle *channels);
__attribute__((weak)) HcclResult HcommChannelDestroy(const ChannelHandle *channels, uint32_t channelNum);
__attribute__((weak)) HcclResult HcommChannelGetStatus(const ChannelHandle *channelList, uint32_t listNum, int32_t *statusList);

#ifdef __cplusplus
}
#endif

#endif  // CANN_HIXL_SRC_HCOMM_COMPAT_H_
