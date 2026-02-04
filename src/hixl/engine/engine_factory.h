/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HIXL_SRC_HIXL_ENGINE_ENGINE_FACTORY_H_
#define HIXL_SRC_HIXL_ENGINE_ENGINE_FACTORY_H_

#include <map>
#include "engine.h"
#include "adxl_engine.h"

namespace hixl {
class EngineFactory {
 public:
  static std::unique_ptr<Engine> CreateEngine(const std::string local_engine,
                                              const std::map<AscendString, AscendString> &options);
};
}  // namespace hixl

#endif  // HIXL_SRC_HIXL_ENGINE_ENGINE_FACTORY_H_