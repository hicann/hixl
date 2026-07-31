/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_OPS_HIXL_KERNEL_FABRIC_MEM_RTSQ_QUERY_H_
#define CANN_HIXL_SRC_OPS_HIXL_KERNEL_FABRIC_MEM_RTSQ_QUERY_H_

#include <cstdint>

#include "ascend_hal_error.h"
#include "hal_pkg/trs_pkg.h"

namespace hixl {
namespace fabric_mem_rtsq {

// halSqCqQueryInfo::value[7] is the driver query-flag field, but not every
// CANN version exports a macro for this ABI slot.
constexpr uint32_t kRtsqQueryInfoFlagIndex = 7U;
static_assert(kRtsqQueryInfoFlagIndex < sizeof(halSqCqQueryInfo{}.value) / sizeof(uint32_t),
              "halSqCqQueryInfo does not expose the query-flag ABI slot.");

// Used by the FabricMem AICPU kernel. Callers must provide the halSqCqQuery
// symbol (weak-linked stubs are fine in UT). query_flag is forwarded to the driver's query flag slot.
inline bool QueryRtsqValues(uint32_t device_id, uint32_t sq_id, drvSqCqPropType_t property, uint32_t &value_low,
                            uint32_t &value_high, drvError_t (*query_fn)(uint32_t, struct halSqCqQueryInfo *) = nullptr,
                            uint32_t query_flag = 0U) {
  if (query_fn == nullptr) {
    return false;
  }
  halSqCqQueryInfo query{};
  query.type = DRV_NORMAL_TYPE;
  query.tsId = 0U;
  query.sqId = sq_id;
  query.cqId = 0U;
  query.prop = property;
  query.value[kRtsqQueryInfoFlagIndex] = query_flag;
  if (query_fn(device_id, &query) != DRV_ERROR_NONE) {
    return false;
  }
  value_low = query.value[0U];
  value_high = query.value[1U];
  return true;
}

inline bool QueryRtsqValue(uint32_t device_id, uint32_t sq_id, drvSqCqPropType_t property, uint32_t &value,
                           drvError_t (*query_fn)(uint32_t, struct halSqCqQueryInfo *) = nullptr,
                           uint32_t query_flag = 0U) {
  uint32_t unused_high = 0U;
  return QueryRtsqValues(device_id, sq_id, property, value, unused_high, query_fn, query_flag);
}

}  // namespace fabric_mem_rtsq
}  // namespace hixl

#endif  // CANN_HIXL_SRC_OPS_HIXL_KERNEL_FABRIC_MEM_RTSQ_QUERY_H_
