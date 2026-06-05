/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HIXL_TESTS_CPP_ADXL_ADXL_TEST_HELPERS_H
#define HIXL_TESTS_CPP_ADXL_ADXL_TEST_HELPERS_H

#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "adxl/adxl_engine.h"
#include "../common/async_transfer_test_helpers.h"

namespace adxl {
namespace test_helpers {

inline void RegisterDeviceBufferMem(AdxlEngine &engine, const std::vector<int8_t> &buffer, MemHandle &handle) {
  adxl::MemDesc mem{};
  mem.addr = reinterpret_cast<uintptr_t>(buffer.data());
  mem.len = buffer.size();
  EXPECT_EQ(engine.RegisterMem(mem, MEM_DEVICE, handle), SUCCESS);
}

inline void WaitForAllAsyncTransfers(AdxlEngine &engine, TransferReq *req_list, int req_count,
                                     int poll_interval_ms = 10, int max_wait_sec = 5) {
  hixl::test_helpers::WaitForAllAsyncTransfers<AdxlEngine, TransferStatus>(engine, req_list, req_count,
                                                                           poll_interval_ms, max_wait_sec);
}

}  // namespace test_helpers
}  // namespace adxl

#endif  // HIXL_TESTS_CPP_ADXL_ADXL_TEST_HELPERS_H
