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

#include <cinttypes>
#include <dlfcn.h>
#include <mutex>

#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/scope_guard.h"

namespace hixl {
namespace {

constexpr const char *kLibAscendHalSo = "libascend_hal.so";
constexpr int32_t kDrvErrorNone = 0;

using HalHostRegisterFn = int32_t (*)(void *src_ptr, uint64_t size, uint32_t flag, uint32_t devid, void **dst_ptr);
using HalHostUnregisterFn = int32_t (*)(void *ptr, uint32_t devid);
using HalMemRegUbSegmentFn = int32_t (*)(uint32_t devid, uint64_t va, uint64_t size);
using HalMemUnRegUbSegmentFn = int32_t (*)(uint32_t devid, uint64_t va, uint64_t size);

struct LibAscendHalLoader {
  LibAscendHalLoader() = default;
  LibAscendHalLoader(const LibAscendHalLoader &) = delete;
  LibAscendHalLoader &operator=(const LibAscendHalLoader &) = delete;
  LibAscendHalLoader(LibAscendHalLoader &&) = delete;
  LibAscendHalLoader &operator=(LibAscendHalLoader &&) = delete;
  void *handle = nullptr;
  HalHostRegisterFn host_register = nullptr;
  HalHostUnregisterFn host_unregister = nullptr;
  HalMemRegUbSegmentFn mem_reg_ub_segment = nullptr;
  HalMemUnRegUbSegmentFn mem_unreg_ub_segment = nullptr;
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
    mem_reg_ub_segment = nullptr;
    mem_unreg_ub_segment = nullptr;
  }

  ~LibAscendHalLoader() {
    Reset();
  }
};

LibAscendHalLoader &LibAscendHal() {
  static LibAscendHalLoader inst;
  return inst;
}

Status OpenLibAscendHalLocked(LibAscendHalLoader &ldr, void *&hal_handle, bool &opened_here) {
  opened_here = false;
  if (ldr.handle != nullptr) {
    hal_handle = ldr.handle;
    return SUCCESS;
  }

  const int32_t dl_mode = RTLD_NOW;
  hal_handle = dlopen(kLibAscendHalSo, dl_mode);
  const char *open_error = (hal_handle == nullptr) ? dlerror() : "";
  HIXL_CHK_BOOL_RET_STATUS(hal_handle != nullptr, FAILED, "[AscendHalProxy] Call api:dlopen failed, lib:%s, msg:%s",
                           kLibAscendHalSo, open_error == nullptr ? "unknown error" : open_error);
  opened_here = true;
  return SUCCESS;
}

Status EnsureHostRegisterLoadedLocked(LibAscendHalLoader &ldr) {
  if (ldr.host_register != nullptr && ldr.host_unregister != nullptr) {
    return SUCCESS;
  }

  void *hal_handle = nullptr;
  bool opened_here = false;
  HIXL_CHK_STATUS_RET(OpenLibAscendHalLocked(ldr, hal_handle, opened_here));
  HIXL_DISMISSABLE_GUARD(handle_guard, ([hal_handle, opened_here]() {
                           if (opened_here) {
                             (void)dlclose(hal_handle);
                           }
                         }));

  auto *register_fn = reinterpret_cast<HalHostRegisterFn>(dlsym(hal_handle, "halHostRegister"));
  auto *unregister_fn = reinterpret_cast<HalHostUnregisterFn>(dlsym(hal_handle, "halHostUnregister"));
  const bool symbols_loaded = register_fn != nullptr && unregister_fn != nullptr;
  const char *symbol_error = symbols_loaded ? "" : dlerror();
  HIXL_CHK_BOOL_RET_STATUS(symbols_loaded, FAILED,
                           "[AscendHalProxy] Call api:dlsym failed, symbols:halHostRegister/halHostUnregister, msg:%s",
                           symbol_error == nullptr ? "unknown error" : symbol_error);

  ldr.handle = hal_handle;
  ldr.host_register = register_fn;
  ldr.host_unregister = unregister_fn;
  HIXL_DISMISS_GUARD(handle_guard);
  return SUCCESS;
}

Status EnsureUbSegmentLoadedLocked(LibAscendHalLoader &ldr) {
  if (ldr.mem_reg_ub_segment != nullptr && ldr.mem_unreg_ub_segment != nullptr) {
    return SUCCESS;
  }

  void *hal_handle = nullptr;
  bool opened_here = false;
  HIXL_CHK_STATUS_RET(OpenLibAscendHalLocked(ldr, hal_handle, opened_here));
  HIXL_DISMISSABLE_GUARD(handle_guard, ([hal_handle, opened_here]() {
                           if (opened_here) {
                             (void)dlclose(hal_handle);
                           }
                         }));

  auto *reg_fn = reinterpret_cast<HalMemRegUbSegmentFn>(dlsym(hal_handle, "halMemRegUbSegment"));
  auto *unreg_fn = reinterpret_cast<HalMemUnRegUbSegmentFn>(dlsym(hal_handle, "halMemUnRegUbSegment"));
  const bool symbols_loaded = reg_fn != nullptr && unreg_fn != nullptr;
  const char *symbol_error = symbols_loaded ? "" : dlerror();
  HIXL_CHK_BOOL_RET_STATUS(
      symbols_loaded, UNSUPPORTED,
      "[AscendHalProxy] Call api:dlsym failed, symbols:halMemRegUbSegment/halMemUnRegUbSegment, msg:%s",
      symbol_error == nullptr ? "unknown error" : symbol_error);

  ldr.handle = hal_handle;
  ldr.mem_reg_ub_segment = reg_fn;
  ldr.mem_unreg_ub_segment = unreg_fn;
  HIXL_DISMISS_GUARD(handle_guard);
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

  LibAscendHalLoader &ldr = LibAscendHal();
  std::lock_guard<std::mutex> lock(ldr.mu);
  HIXL_CHK_STATUS_RET(EnsureHostRegisterLoadedLocked(ldr), "[AscendHalProxy] EnsureHostRegisterLoadedLocked failed");
  const int32_t ret = ldr.host_register(src, size, flag, logic_dev_id, dst);
  HIXL_CHK_BOOL_RET_STATUS(ret == kDrvErrorNone, FAILED,
                           "[AscendHalProxy] halHostRegister failed, ret=%d, flag=%u, logic_dev_id=%u, size=%" PRIu64,
                           ret, flag, logic_dev_id, size);
  return SUCCESS;
}

Status AscendHalProxy::HostUnregister(void *src, uint32_t logic_dev_id) {
  HIXL_CHECK_NOTNULL(src);

  LibAscendHalLoader &ldr = LibAscendHal();
  std::lock_guard<std::mutex> lock(ldr.mu);
  HIXL_CHK_STATUS_RET(EnsureHostRegisterLoadedLocked(ldr), "[AscendHalProxy] EnsureHostRegisterLoadedLocked failed");
  const int32_t ret = ldr.host_unregister(src, logic_dev_id);
  HIXL_CHK_BOOL_RET_STATUS(ret == kDrvErrorNone, FAILED,
                           "[AscendHalProxy] halHostUnregister failed, ret=%d, logic_dev_id=%u", ret, logic_dev_id);
  return SUCCESS;
}

Status AscendHalProxy::MemRegUbSegment(uint32_t logic_dev_id, uint64_t va, uint64_t size) {
  HIXL_CHK_BOOL_RET_STATUS(va != 0U && size != 0U, PARAM_INVALID,
                           "[AscendHalProxy] Invalid UB segment range, logic_dev_id=%u, size=%" PRIu64, logic_dev_id,
                           size);

  LibAscendHalLoader &ldr = LibAscendHal();
  std::lock_guard<std::mutex> lock(ldr.mu);
  HIXL_CHK_STATUS_RET(EnsureUbSegmentLoadedLocked(ldr), "[AscendHalProxy] EnsureUbSegmentLoadedLocked failed");
  const int32_t ret = ldr.mem_reg_ub_segment(logic_dev_id, va, size);
  HIXL_CHK_BOOL_RET_STATUS(
      ret == kDrvErrorNone, FAILED,
      "[AscendHalProxy] Call api:halMemRegUbSegment failed, ret=%d, logic_dev_id=%u, size=%" PRIu64, ret, logic_dev_id,
      size);
  return SUCCESS;
}

Status AscendHalProxy::MemUnRegUbSegment(uint32_t logic_dev_id, uint64_t va, uint64_t size) {
  HIXL_CHK_BOOL_RET_STATUS(va != 0U && size != 0U, PARAM_INVALID,
                           "[AscendHalProxy] Invalid UB segment range, logic_dev_id=%u, size=%" PRIu64, logic_dev_id,
                           size);

  LibAscendHalLoader &ldr = LibAscendHal();
  std::lock_guard<std::mutex> lock(ldr.mu);
  HIXL_CHK_STATUS_RET(EnsureUbSegmentLoadedLocked(ldr), "[AscendHalProxy] EnsureUbSegmentLoadedLocked failed");
  const int32_t ret = ldr.mem_unreg_ub_segment(logic_dev_id, va, size);
  HIXL_CHK_BOOL_RET_STATUS(
      ret == kDrvErrorNone, FAILED,
      "[AscendHalProxy] Call api:halMemUnRegUbSegment failed, ret=%d, logic_dev_id=%u, size=%" PRIu64, ret,
      logic_dev_id, size);
  return SUCCESS;
}

}  // namespace hixl
