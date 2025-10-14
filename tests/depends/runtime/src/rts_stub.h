/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#ifndef AIR_CXX_RUNTIME_V2_RTS_STUB_H_
#define AIR_CXX_RUNTIME_V2_RTS_STUB_H_

#include <stdlib.h>

using rtEngineType = enum { RT_ENGINE_TYPE_AIC = 0, RT_ENGINE_TYPE_AIV };

/**
 * @ingroup rts_kernel
 * @brief kernel launch option config type
 */
using rtLaunchKernelAttrId = enum {
  RT_LAUNCH_KERNEL_ATTR_SCHEM_MODE = 1,
  RT_LAUNCH_KERNEL_ATTR_LOCAL_MEM_SIZE,
  // vector core使能使用
  RT_LAUNCH_KERNEL_ATTR_ENGINE_TYPE,
  // vector core使能使用
  RT_LAUNCH_KERNEL_ATTR_BLOCKDIM_OFFSET,
  RT_LAUNCH_KERNEL_ATTR_BLOCK_TASK_PREFETCH,
  RT_LAUNCH_KERNEL_ATTR_DATA_DUMP,
  RT_LAUNCH_KERNEL_ATTR_TIMEOUT,
  RT_LAUNCH_KERNEL_ATTR_MAX
};

using rtLaunchKernelAttrVal_t = union {
  uint8_t schemMode;
  uint32_t localMemorySize;
  rtEngineType engineType;
  uint32_t blockDimOffset;
  uint8_t isBlockTaskPrefetch;  // 任务下发时判断是否sqe后续需要刷新标记（tiling key依赖下沉场景）0:disable 1:enable
  uint8_t isDataDump;           // 0:disable 1:enable
  uint16_t timeout;
  uint32_t rsv[4];
};

/**
 * @ingroup rts_kernel
 * @brief kernel launch option config struct
 */
using rtLaunchKernelAttr_t = struct {
  rtLaunchKernelAttrId id;
  rtLaunchKernelAttrVal_t value;
};

using rtKernelLaunchCfg_t = struct {
  rtLaunchKernelAttr_t *attrs;
  size_t numAttrs;
};

using rtLoadBinaryOption = enum {
  RT_LOAD_BINARY_OPT_LAZY_LOAD = 1,
  RT_LOAD_BINARY_OPT_MAGIC = 2,
  RT_LOAD_BINARY_OPT_CPU_KERNEL_MODE = 3,
  RT_LOAD_BINARY_OPT_MAX
};

using rtLoadBinaryOptionValue_t = union {
  uint32_t isLazyLoad;
  uint32_t magic;
  int32_t cpuKernelMode;  // 0 ：仅需要加载json，1 ：加载cpu so & json，2: LoadFromData
  uint32_t rsv[4];
};

using rtLoadBinaryOption_t = struct {
  rtLoadBinaryOption optionId;
  rtLoadBinaryOptionValue_t value;
};

using rtLoadBinaryConfig_t = struct {
  rtLoadBinaryOption_t *options;
  size_t numOpt;
};
#endif  // AIR_CXX_RUNTIME_V2_RTS_STUB_H_