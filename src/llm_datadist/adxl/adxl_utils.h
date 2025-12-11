/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_ADXL_UTILS_H
#define CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_ADXL_UTILS_H

#include "hccl/hccl_types.h"
#include "runtime/rt.h"
#include "adxl/adxl_types.h"

namespace adxl {
Status HcclError2AdxlStatus(HcclResult ret);
Status AclError2AdxlStatus(rtError_t ret);
Status LLMError2AdxlStatus(ge::Status ret);
bool NeedErrorLog(Status status);
}  // namespace adxl
#endif  // CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_ADXL_UTILS_H
