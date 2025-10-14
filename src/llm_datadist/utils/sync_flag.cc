/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */


#include "utils/sync_flag.h"
#include "common/def_types.h"

namespace llm {

SyncFlag::SyncFlag(void *flag) : flag_(PtrToPtr<void, uint8_t>(flag)) {}

int32_t SyncFlag::Wait(const std::chrono::steady_clock::time_point *end_time_point) const {
  uint8_t ret = 0;
  while (ret == 0) {
    ret = *flag_;
    if (end_time_point != nullptr && std::chrono::steady_clock::now() >= *end_time_point) {
      // 打印时间点
      LLMLOGE(ge::LLM_WAIT_PROC_TIMEOUT, "Wait flag timed out");
      break;
    }
  }
  *flag_ = 0;  // reset
  return ret;
}

ge::Status SyncFlag::Check() const {
  auto end_time_point = std::chrono::steady_clock::now() + std::chrono::milliseconds(1);
  while (true) {
    if (*flag_ == 1U) {
      break;
    }
    if (std::chrono::steady_clock::now() >= end_time_point) {
      return ge::FAILED;
    }
  }
  *flag_ = static_cast<uint8_t>(0U);  // reset
  return ge::SUCCESS;
}
}  // namespace llm