/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_COMMON_SOC_TYPE_H_
#define CANN_HIXL_SRC_HIXL_COMMON_SOC_TYPE_H_

#include "hixl_checker.h"

namespace hixl {

// SoC category derived from the device SoC name prefix. The single source of truth for
// SoC-dependent behaviour; callers must not match SoC names by themselves.
enum class SocType { kGen1, kGen2, kDefault };

// Utility that resolves the device SoC category.
class SocTypeUtil {
 public:
  SocTypeUtil() = delete;

  // Reads the device SoC name (aclrtGetSocName), logs the name and category, and returns the category.
  // Callers must treat a non-SUCCESS return as fatal. Unknown names map to SocType::kDefault.
  static Status GetSocType(SocType &soc_type);
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_COMMON_SOC_TYPE_H_
