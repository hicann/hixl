/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HIXL_TESTS_DEPENDS_SYS_API_SYS_API_WRAP_H_
#define HIXL_TESTS_DEPENDS_SYS_API_SYS_API_WRAP_H_

#include <cstdint>
#include <memory>

namespace hixl_test {

// Sentinel value used by the default hook implementations to signal
// "not handled -> delegate to the real system call". The --wrap shims call the
// real symbol when a hook returns this sentinel. -2147483647 is never a real
// mmAccess/mmRealPath/mmDlclose return value.
constexpr int32_t kSysApiPassThrough = -2147483647;

// Returns the pointer sentinel shared by DlOpen/DlSym passthrough. Real
// dlopen/dlsym never return this address.
inline void *SysApiPassSentinelPtr() {
  static char sentinel;
  return &sentinel;
}

// Real-symbol helpers for hooks that need to delegate to the underlying system
// call with a different argument (e.g. redirect libra.so to libdl.so). They are
// backed by the linker __real_* aliases produced by --wrap.
void *RealDlOpen(const char *file_name, int mode);
void *RealDlSym(void *handle, const char *func_name);
int RealDlClose(void *handle);

// Test-facing hook interface. The virtual signatures mirror the corresponding
// mmpa_stub virtuals (DlOpen/DlSym/DlClose/Access/RealPath) so existing mock
// subclasses can switch base class with no method-body changes.
//
// Return-value convention (same as mmpa_stub):
//   - DlOpen/DlSym : return a handle (may be nullptr to simulate failure);
//                    return the sentinel to pass through to the real call.
//   - DlClose      : return mmDlclose-style value (0 on success);
//                    return kSysApiPassThrough to pass through.
//   - Access       : mmAccess-style, 0 on success, non-zero on failure;
//                    return kSysApiPassThrough to pass through.
//   - RealPath     : write into real_path and return 0 on success, non-zero on
//                    failure; return kSysApiPassThrough to pass through.
class SysApiHooks {
 public:
  virtual ~SysApiHooks() = default;

  virtual void *DlOpen(const char *file_name, int32_t mode) {
    (void)file_name;
    (void)mode;
    return SysApiPassSentinelPtr();
  }
  virtual void *DlSym(void *handle, const char *func_name) {
    (void)handle;
    (void)func_name;
    return SysApiPassSentinelPtr();
  }
  virtual int32_t DlClose(void *handle) {
    (void)handle;
    return kSysApiPassThrough;
  }
  virtual int32_t Access(const char *path_name) {
    (void)path_name;
    return kSysApiPassThrough;
  }
  virtual int32_t RealPath(const char *path, char *real_path, int32_t real_path_len) {
    (void)path;
    (void)real_path;
    (void)real_path_len;
    return kSysApiPassThrough;
  }

  static bool IsPassThrough(void *p) {
    return p == SysApiPassSentinelPtr();
  }
  static bool IsPassThrough(int32_t v) {
    return v == kSysApiPassThrough;
  }
};

SysApiHooks *ActiveSysApiHooks();
// Install makes `hooks` the active hook (replaces any previously installed one).
void InstallSysApiHooks(std::shared_ptr<SysApiHooks> hooks);
// Self-healing reset: unconditionally restores the default (kernel-json) hook so
// a test can never leak its mock to subsequent tests, regardless of how many
// installs it performed. Mirrors the legacy MmpaStub::Reset semantics.
void ResetSysApiHooks();

// RAII guard that installs a hook for the current scope and restores the
// default hook on exit. Safe for sequential (non-nested) scopes.
class ScopedSysApiMock {
 public:
  explicit ScopedSysApiMock(std::shared_ptr<SysApiHooks> hooks) {
    InstallSysApiHooks(std::move(hooks));
  }
  ~ScopedSysApiMock() {
    ResetSysApiHooks();
  }
  ScopedSysApiMock(const ScopedSysApiMock &) = delete;
  ScopedSysApiMock &operator=(const ScopedSysApiMock &) = delete;
};

}  // namespace hixl_test

#endif  // HIXL_TESTS_DEPENDS_SYS_API_SYS_API_WRAP_H_
