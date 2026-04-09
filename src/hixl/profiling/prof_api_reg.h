/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_COMMON_PROF_API_REG_H_
#define CANN_HIXL_SRC_HIXL_COMMON_PROF_API_REG_H_

#include <cstdint>
#include "acl/acl_base.h"
#include "aprof_pub.h"
#include "hixl/hixl_types.h"
#include "common/hixl_inner_types.h"

namespace hixl {
constexpr uint32_t kHixlProfTypeStartOffset = 0x009000U;
enum class HixlProfType {
  // start with 0x049000U
  kProfTypeStart = MSPROF_REPORT_ACL_OTHERS_BASE_TYPE + kHixlProfTypeStartOffset,
  HixlOpBatchRead,
  HixlOpBatchWrite,
  kProfTypeEnd
};

class HixlProfilingReporter {
 public:
  explicit HixlProfilingReporter(const HixlProfType api_id);
  HixlProfilingReporter(const HixlProfType api_id, uint64_t start_time);
  static uint64_t GetSysCycleTime();
  ~HixlProfilingReporter() noexcept;

 private:
  uint64_t start_time_ = 0UL;
  const HixlProfType hixl_api_;
};
} // namespace hixl

#define HIXL_API_PROFILING(api_id) \
  const hixl::HixlProfilingReporter profilingReporter(api_id)

#define HIXL_API_PROFILING_WITH_TIME(apiId, startTime) \
  const hixl::HixlProfilingReporter profilingReporter(apiId, startTime)
#endif