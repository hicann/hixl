/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "ascend_hal_proxy.h"

#include <dlfcn.h>
#include <mutex>

#include "common/hixl_checker.h"
#include "common/hixl_log.h"

namespace hixl {
namespace {

constexpr const char *kLibAscendHalSo = "libascend_hal.so";
constexpr int32_t kDrvErrorNone = 0;

using HalHostRegisterFn = int32_t (*)(void *src_ptr, uint64_t size, uint32_t flag, uint32_t devid, void **dst_ptr);
using HalHostUnregisterFn = int32_t (*)(void *ptr, uint32_t devid);

struct LibAscendHalLoader {
  void *handle = nullptr;
  HalHostRegisterFn host_register = nullptr;
  HalHostUnregisterFn host_unregister = nullptr;
  std::mutex mu;

  void Reset() {
    std::lock_guard<std::mutex> lock(mu);
    if (handle != nullptr) {
      HIXL_LOGI("[AscendHalProxy] LibAscendHalLoader reset, dlclose %s", kLibAscendHalSo);
      (void)dlclose(handle);
    }
    handle = nullptr;
    host_register = nullptr;
    host_unregister = nullptr;
  }

  ~LibAscendHalLoader() {
    Reset();
  }
};

LibAscendHalLoader &LibAscendHal() {
  static LibAscendHalLoader inst;
  return inst;
}

Status EnsureLibAscendHalLoaded() {
  LibAscendHalLoader &ldr = LibAscendHal();
  std::lock_guard<std::mutex> lock(ldr.mu);
  if (ldr.handle != nullptr) {
    return SUCCESS;
  }

  const int32_t dl_mode = RTLD_NOW;
  void *hal_handle = dlopen(kLibAscendHalSo, dl_mode);
  if (hal_handle == nullptr) {
    const char *err = dlerror();
    HIXL_LOGE(FAILED, "[AscendHalProxy] dlopen %s failed: %s", kLibAscendHalSo, err != nullptr ? err : "unknown error");
    return FAILED;
  }

  auto *register_fn = reinterpret_cast<HalHostRegisterFn>(dlsym(hal_handle, "halHostRegister"));
  auto *unregister_fn = reinterpret_cast<HalHostUnregisterFn>(dlsym(hal_handle, "halHostUnregister"));
  if ((register_fn == nullptr) || (unregister_fn == nullptr)) {
    const char *err = dlerror();
    HIXL_LOGE(FAILED, "[AscendHalProxy] dlsym halHostRegister/halHostUnregister failed: %s",
              err != nullptr ? err : "unknown error");
    (void)dlclose(hal_handle);
    return FAILED;
  }

  ldr.handle = hal_handle;
  ldr.host_register = register_fn;
  ldr.host_unregister = unregister_fn;
  return SUCCESS;
}

}  // namespace

#ifdef HIXL_ENABLE_TEST_HOOKS
void AscendHalProxy::ResetForTest() {
  LibAscendHal().Reset();
}
#endif

Status AscendHalProxy::HostRegister(void *src, uint64_t size, uint32_t flag, uint32_t logic_dev_id, void **dst) {
  HIXL_CHECK_NOTNULL(src);
  HIXL_CHECK_NOTNULL(dst);
  HIXL_CHK_STATUS_RET(EnsureLibAscendHalLoaded(), "[AscendHalProxy] EnsureLibAscendHalLoaded failed");

  LibAscendHalLoader &ldr = LibAscendHal();
  std::lock_guard<std::mutex> lock(ldr.mu);
  const int32_t ret = ldr.host_register(src, size, flag, logic_dev_id, dst);
  HIXL_CHK_BOOL_RET_STATUS(ret == kDrvErrorNone, FAILED,
                           "[AscendHalProxy] halHostRegister failed, ret=%d, flag=%u, logic_dev_id=%u, size=%lu", ret,
                           flag, logic_dev_id, size);
  return SUCCESS;
}

Status AscendHalProxy::HostUnregister(void *src, uint32_t logic_dev_id) {
  HIXL_CHECK_NOTNULL(src);
  HIXL_CHK_STATUS_RET(EnsureLibAscendHalLoaded(), "[AscendHalProxy] EnsureLibAscendHalLoaded failed");

  LibAscendHalLoader &ldr = LibAscendHal();
  std::lock_guard<std::mutex> lock(ldr.mu);
  const int32_t ret = ldr.host_unregister(src, logic_dev_id);
  HIXL_CHK_BOOL_RET_STATUS(ret == kDrvErrorNone, FAILED,
                           "[AscendHalProxy] halHostUnregister failed, ret=%d, logic_dev_id=%u", ret, logic_dev_id);
  return SUCCESS;
}

}  // namespace hixl
