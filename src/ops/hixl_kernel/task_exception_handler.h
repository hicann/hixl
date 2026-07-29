/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#ifndef CANN_HIXL_SRC_OPS_HIXL_KERNEL_TASK_EXCEPTION_HANDLER_H_
#define CANN_HIXL_SRC_OPS_HIXL_KERNEL_TASK_EXCEPTION_HANDLER_H_

#include <atomic>

namespace hixl {

class TaskExceptionHandler {
 public:
  static TaskExceptionHandler &Instance();

  void EnableExceptionCallback();

  void DisableExceptionCallback();

 private:
  TaskExceptionHandler() = default;

  // 调用方保证 Enable/Disable 不会并发，atomic 仅用于跨线程可见性
  std::atomic<bool> callback_registered_{false};
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_OPS_HIXL_KERNEL_TASK_EXCEPTION_HANDLER_H_
