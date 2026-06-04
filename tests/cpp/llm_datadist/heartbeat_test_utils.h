/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HIXL_TESTS_CPP_LLM_DATADIST_HEARTBEAT_TEST_UTILS_H_
#define HIXL_TESTS_CPP_LLM_DATADIST_HEARTBEAT_TEST_UTILS_H_

#include <cstdint>

#include "adxl/channel_manager.h"

namespace llm::test {
constexpr int32_t kFastHeartbeatWaitTimeInMillis = 10;
constexpr int64_t kFastHeartbeatTimeoutInMillis = 50;
constexpr int32_t kDefaultHeartbeatWaitTimeInMillis = 10000;
constexpr int64_t kDefaultHeartbeatTimeoutInMillis = 120000;
constexpr int32_t kWaitHeartbeatProcessTimeInMillis = 60;
constexpr int32_t kWaitAutoClearChannelTimeInMillis = 100;

inline void ResetHeartbeatConfig() {
  adxl::ChannelManager::SetHeartbeatWaitTime(kDefaultHeartbeatWaitTimeInMillis);
  adxl::CommChannel::SetHeartbeatTimeout(kDefaultHeartbeatTimeoutInMillis);
}
}  // namespace llm::test

#endif  // HIXL_TESTS_CPP_LLM_DATADIST_HEARTBEAT_TEST_UTILS_H_
