/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "engine_factory.h"
#include "adxl/adxl_inner_engine.h"
#include "hixl/hixl_types.h"
#include "adxl/adxl_types.h"

namespace hixl {
std::unique_ptr<Engine> EngineFactory::CreateEngine(const std::string local_engine,
                                                    const std::map<AscendString, AscendString> &options) {
  (void)options;
  return std::make_unique<AdxlEngine>(AscendString(local_engine.c_str()));
}
}  // namespace hixl