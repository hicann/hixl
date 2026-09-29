/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_PROFILING_PROF_REPORTER_H_
#define CANN_HIXL_SRC_HIXL_PROFILING_PROF_REPORTER_H_

#include <memory>

#include "prof_types.h"

namespace hixl {
class ProfStart;
using ProfStartPtr = std::shared_ptr<ProfStart>;

ProfStartPtr GetProfStart(const HixlProfType prof_type);

class HixlProfilingReporter {
 public:
  explicit HixlProfilingReporter(const HixlProfType api_id);
  explicit HixlProfilingReporter(ProfStartPtr prof_start);
  ~HixlProfilingReporter() noexcept;

  HixlProfilingReporter(const HixlProfilingReporter &) = delete;
  HixlProfilingReporter &operator=(const HixlProfilingReporter &) = delete;

 private:
  ProfStartPtr prof_start_;
};
}  // namespace hixl

#define HIXL_API_PROFILING(api_id) const hixl::HixlProfilingReporter profilingReporter(api_id)

#define HIXL_API_PROFILING_WITH_PROF_START(profStart) const hixl::HixlProfilingReporter profilingReporter((profStart))
#endif  // CANN_HIXL_SRC_HIXL_PROFILING_PROF_REPORTER_H_
