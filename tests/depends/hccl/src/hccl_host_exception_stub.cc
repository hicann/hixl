/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hccl_host_exception_stub.h"

extern "C" {
// ---- Exception callback stub ----
static HcommExceptionCallback g_registered_exception_callback = nullptr;
static void *g_registered_exception_user_data = nullptr;
static int32_t g_register_exception_ret = 0;
static int32_t g_unregister_exception_ret = 0;
// Invoke the callback during registration to exercise reentrant initialization.
static bool g_register_exception_reentrant_enabled = false;
static uint32_t g_register_exception_reentrant_device_id = 0U;
static uint64_t g_register_exception_reentrant_thread = 0U;

int32_t HcommExceptionRegisterCallback(HcommExceptionCallback cb, void *user_data) {
  if (cb != nullptr && g_register_exception_ret == 0) {
    g_registered_exception_callback = cb;
    g_registered_exception_user_data = user_data;
    if (g_register_exception_reentrant_enabled) {
      HcommExceptionInfoLite info{};
      info.header.magicWord = HCOMM_EXCEPTION_LITE_MAGIC_WORD;
      info.header.version = HCOMM_EXCEPTION_LITE_VERSION;
      info.deviceId = g_register_exception_reentrant_device_id;
      info.thread = g_register_exception_reentrant_thread;
      cb(&info, user_data);
    }
  }
  return g_register_exception_ret;
}

int32_t HcommExceptionUnregisterCallback(HcommExceptionCallback cb) {
  if (g_unregister_exception_ret == 0 && g_registered_exception_callback == cb) {
    g_registered_exception_callback = nullptr;
    g_registered_exception_user_data = nullptr;
  }
  return g_unregister_exception_ret;
}

void SetRegisterHostExceptionResult(int32_t ret) {
  g_register_exception_ret = ret;
}

void SetUnregisterHostExceptionResult(int32_t ret) {
  g_unregister_exception_ret = ret;
}

void SetRegisterHostExceptionReentrantInfo(uint32_t device_id, uint64_t thread) {
  g_register_exception_reentrant_enabled = true;
  g_register_exception_reentrant_device_id = device_id;
  g_register_exception_reentrant_thread = thread;
}

void ClearRegisterHostExceptionReentrantInfo() {
  g_register_exception_reentrant_enabled = false;
  g_register_exception_reentrant_device_id = 0U;
  g_register_exception_reentrant_thread = 0U;
}

HcommExceptionCallback GetRegisteredHostExceptionCallback() {
  return g_registered_exception_callback;
}

void *GetRegisteredHostExceptionUserData() {
  return g_registered_exception_user_data;
}

void ResetHostExceptionCallbackStub() {
  g_registered_exception_callback = nullptr;
  g_registered_exception_user_data = nullptr;
  g_register_exception_ret = 0;
  g_unregister_exception_ret = 0;
  ClearRegisterHostExceptionReentrantInfo();
}
}
