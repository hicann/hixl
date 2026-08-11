/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "sys_api_wrap.h"

#include <climits>
#include <cstring>
#include <dlfcn.h>
#include <string>
#include <unistd.h>

#include "securec.h"

extern "C" {
// __real_* are aliases synthesized by the linker --wrap option (see CMake --wrap
// flags); no system header declares them, so extern declarations are unavoidable.
// This is test-only infrastructure and never ships in production builds.
extern void *__real_dlopen(const char *, int);       // NOLINT(G.EXP.05-CPP)
extern void *__real_dlsym(void *, const char *);     // NOLINT(G.EXP.05-CPP)
extern int __real_dlclose(void *);                   // NOLINT(G.EXP.05-CPP)
extern char *__real_dlerror(void);                   // NOLINT(G.EXP.05-CPP)
extern int __real_access(const char *, int);         // NOLINT(G.EXP.05-CPP)
extern char *__real_realpath(const char *, char *);  // NOLINT(G.EXP.05-CPP)
}

namespace hixl_test {

namespace {
// Always-on hook that lets TransferPool/load_kernel succeed for the device-kernel
// json path in unit tests (the file does not exist on disk), matching the legacy
// mmpa_stub default behaviour. Every other path and all dlopen/dlsym/dlclose calls
// pass through to the real system call.
class DefaultKernelJsonHook : public SysApiHooks {
 public:
  int32_t RealPath(const char *path, char *real_path, int32_t real_path_len) override {
    if (path != nullptr && real_path != nullptr && real_path_len > 0 &&
        std::string(path).find("libcann_hixl_kernel.json") != std::string::npos) {
      size_t cap = static_cast<size_t>(real_path_len);
      errno_t sret = strncpy_s(real_path, cap, path, cap - 1U);
      if (sret != EOK) {
        return kSysApiPassThrough;
      }
      return 0;
    }
    return kSysApiPassThrough;
  }

  int32_t Access(const char *path_name) override {
    if (path_name != nullptr && std::string(path_name).find("libcann_hixl_kernel.json") != std::string::npos) {
      return 0;
    }
    return kSysApiPassThrough;
  }
};

SysApiHooks &DefaultHook() {
  static DefaultKernelJsonHook default_hook;
  return default_hook;
}

// Holds the test-installed hook alive; empty means "default active".
std::shared_ptr<SysApiHooks> &InstalledHolder() {
  static std::shared_ptr<SysApiHooks> holder;
  return holder;
}

SysApiHooks *&ActiveRef() {
  static SysApiHooks *active = &DefaultHook();
  return active;
}
}  // namespace

SysApiHooks *ActiveSysApiHooks() {
  return ActiveRef();
}

void InstallSysApiHooks(std::shared_ptr<SysApiHooks> hooks) {
  InstalledHolder() = std::move(hooks);
  ActiveRef() = (InstalledHolder() != nullptr) ? InstalledHolder().get() : &DefaultHook();
}

// Self-healing reset: unconditionally restore the default hook (matching the
// legacy MmpaStub::Reset semantics). This guarantees a test can never leak a
// mock hook to later tests, regardless of how many installs it performed.
void ResetSysApiHooks() {
  InstalledHolder().reset();
  ActiveRef() = &DefaultHook();
}

void *RealDlOpen(const char *file_name, int mode) {
  return __real_dlopen(file_name, mode);
}
void *RealDlSym(void *handle, const char *func_name) {
  return __real_dlsym(handle, func_name);
}
int RealDlClose(void *handle) {
  return __real_dlclose(handle);
}

}  // namespace hixl_test

extern "C" {

void *__wrap_dlopen(const char *name, int mode) {
  auto *hooks = hixl_test::ActiveSysApiHooks();
  if (hooks != nullptr) {
    void *ret = hooks->DlOpen(name, static_cast<int32_t>(mode));
    if (!hixl_test::SysApiHooks::IsPassThrough(ret)) {
      return ret;
    }
  }
  return __real_dlopen(name, mode);
}

void *__wrap_dlsym(void *handle, const char *name) {
  auto *hooks = hixl_test::ActiveSysApiHooks();
  if (hooks != nullptr) {
    void *ret = hooks->DlSym(handle, name);
    if (!hixl_test::SysApiHooks::IsPassThrough(ret)) {
      return ret;
    }
  }
  return __real_dlsym(handle, name);
}

int __wrap_dlclose(void *handle) {
  auto *hooks = hixl_test::ActiveSysApiHooks();
  if (hooks != nullptr) {
    int32_t ret = hooks->DlClose(handle);
    if (!hixl_test::SysApiHooks::IsPassThrough(ret)) {
      return static_cast<int>(ret);
    }
  }
  return __real_dlclose(handle);
}

char *__wrap_dlerror(void) {
  char *real = __real_dlerror();
  if (real != nullptr) {
    return real;
  }
  // When a hook simulates a dlopen/dlsym failure, no real dl error is pending;
  // return a non-null placeholder so callers can safely format it with %s.
  if (hixl_test::ActiveSysApiHooks() != nullptr) {
    return const_cast<char *>("mock dlerror");
  }
  return nullptr;
}

int __wrap_access(const char *path, int mode) {
  auto *hooks = hixl_test::ActiveSysApiHooks();
  if (hooks != nullptr) {
    int32_t ret = hooks->Access(path);
    if (!hixl_test::SysApiHooks::IsPassThrough(ret)) {
      return (ret == 0) ? 0 : -1;
    }
  }
  return __real_access(path, mode);
}

char *__wrap_realpath(const char *path, char *resolved) {
  auto *hooks = hixl_test::ActiveSysApiHooks();
  if (hooks != nullptr) {
    int32_t ret = hooks->RealPath(path, resolved, PATH_MAX);
    if (!hixl_test::SysApiHooks::IsPassThrough(ret)) {
      return (ret == 0) ? resolved : nullptr;
    }
  }
  return __real_realpath(path, resolved);
}
}  // extern "C"
