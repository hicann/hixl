/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "depends/hccl/src/hccl_stub.h"
#include "hcomm_proxy.h"

namespace hixl {
namespace {
class DeviceExceptionProxyForTest {
 public:
  static int32_t RegisterExceptionCallback(hixl_test::DeviceExceptionCallback cb, void *user_data) {
    return HcommDeviceExceptionRegisterCallbackStub(cb, user_data);
  }

  static int32_t UnregisterExceptionCallback(hixl_test::DeviceExceptionCallback cb) {
    return HcommDeviceExceptionUnregisterCallbackStub(cb);
  }
};
}  // namespace
}  // namespace hixl

// Bind the Device handler to its own stub to keep Host and Device callback registrations separate.
#define HcommProxy DeviceExceptionProxyForTest
#include "../../../../src/ops/hixl_kernel/task_exception_handler.cc"
#undef HcommProxy
