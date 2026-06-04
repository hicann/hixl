/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_FABRIC_MEM_FABRIC_MEM_CONFIG_H_
#define CANN_HIXL_SRC_HIXL_FABRIC_MEM_FABRIC_MEM_CONFIG_H_

#include <cstddef>

#include "hixl/hixl_types.h"

namespace hixl {
struct FabricMemConfig {
  bool enabled = false;
  bool auto_connect = false;
  bool has_capacity_tb = false;
  bool has_start_address_tb = false;
  size_t capacity_tb = 0;
  size_t start_address_tb = 0;
  size_t task_stream_num = 4U;
  size_t max_stream_num = 512U;
};
}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_FABRIC_MEM_FABRIC_MEM_CONFIG_H_
