/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HIXL_SRC_HIXL_COMMON_STATISTIC_UTILS_H_
#define HIXL_SRC_HIXL_COMMON_STATISTIC_UTILS_H_

#include <cstdint>
#include <string>

namespace hixl {
namespace statistic {
constexpr uint64_t kResetTimes = 100000UL;
constexpr uint32_t kStatisticTimerPeriodMs = 80U * 1000U;
constexpr double kBytesPerGb = 1024.0 * 1024.0 * 1024.0;
constexpr uint64_t kKiloByte = 1024UL;
constexpr double kMicrosPerSecond = 1000.0 * 1000.0;
constexpr char kClientStatisticPrefix[] = "client:";
constexpr char kServerStatisticPrefix[] = "server:";

inline double GetBandwidthGbps(uint64_t total_bytes, uint64_t total_cost) {
  if (total_bytes == 0U || total_cost == 0U) {
    return 0.0;
  }
  return static_cast<double>(total_bytes) * kMicrosPerSecond / static_cast<double>(total_cost) / kBytesPerGb;
}

inline uint64_t GetAvgBytesPerOpDesc(uint64_t total_bytes, uint64_t total_op_desc_count) {
  if (total_bytes == 0U || total_op_desc_count == 0U) {
    return 0U;
  }
  return total_bytes / total_op_desc_count;
}

inline uint64_t ToKBytes(uint64_t bytes) {
  return bytes / kKiloByte;
}

inline std::string GetStatisticChannelId(const std::string &channel_id, bool is_client) {
  return (is_client ? kClientStatisticPrefix : kServerStatisticPrefix) + channel_id;
}
}  // namespace statistic
}  // namespace hixl

#endif  // HIXL_SRC_HIXL_COMMON_STATISTIC_UTILS_H_
