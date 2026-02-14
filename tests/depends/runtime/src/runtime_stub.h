/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef __INC_LLT_RUNTIME_STUB_H
#define __INC_LLT_RUNTIME_STUB_H

#include <vector>
#include <memory>
#include <mutex>
#include "mmpa/mmpa_api.h"
#include "acl/acl.h"

#include <runtime/runtime/base.h>
#include <runtime/runtime/dev.h>

#ifdef __cplusplus
extern "C" {
#endif
// is_mock_new_way is 1 means new way, 0 old way(default);
// for some old ge testcases
void SetMockRtGetDeviceWay(int32_t is_mock_new_way);
#ifdef __cplusplus
}
#endif

namespace llm {
class RuntimeStub {
 public:
  virtual ~RuntimeStub() = default;

  static RuntimeStub* GetInstance();

  static void SetInstance(const std::shared_ptr<RuntimeStub> &instance) {
    instance_ = instance;
  }

  static void Install(RuntimeStub*);
  static void UnInstall(RuntimeStub*);

  static void Reset() {
    SetMockRtGetDeviceWay(0);
    instance_.reset();
  }

//  virtual void LaunchTaskToStream(TaskTypeOnStream task_type, rtStream_t stream) {};


  virtual rtError_t rtGetDevResAddress(const rtDevResInfo *resInfo, rtDevResAddrInfo *addrInfo);

 private:
  static std::mutex mutex_;
  static std::shared_ptr<RuntimeStub> instance_;
  static thread_local RuntimeStub *fake_instance_;
  size_t reserve_mem_size_ = 200UL * 1024UL * 1024UL;
  std::mutex mtx_;
  std::vector<rtStream_t> model_bind_streams_;
  std::vector<rtStream_t> model_unbind_streams_;
  size_t input_mem_copy_batch_count_{0UL};
};

class EnvGuard {
public:
  EnvGuard(const char *key, const char *value) : key_(key) {
    mmSetEnv(key, value, 1);
  }
  ~EnvGuard() {
    unsetenv(key_.c_str());
  }
private:
  const std::string key_;
};
}  // namespace llm

#ifdef __cplusplus
extern "C" {
#endif
void rtStubTearDown();

#define RTS_STUB_SETUP()    \
do {                        \
  rtStubTearDown();         \
} while (0)

#define RTS_STUB_TEARDOWN() \
do {                        \
  rtStubTearDown();         \
} while (0)

#define RTS_STUB_RETURN_VALUE(FUNC, TYPE, VALUE)                          \
do {                                                                      \
  g_Stub_##FUNC##_RETURN.emplace(g_Stub_##FUNC##_RETURN.begin(), VALUE);  \
} while (0)

#define RTS_STUB_OUTBOUND_VALUE(FUNC, TYPE, NAME, VALUE)                          \
do {                                                                              \
  g_Stub_##FUNC##_OUT_##NAME.emplace(g_Stub_##FUNC##_OUT_##NAME.begin(), VALUE);  \
} while (0)

#define RTS_STUB_RETURN_EXTERN(FUNC, TYPE) extern std::vector<TYPE> g_Stub_##FUNC##_RETURN;
#define RTS_STUB_OUTBOUND_EXTERN(FUNC, TYPE, NAME) extern std::vector<TYPE> g_Stub_##FUNC##_OUT_##NAME;


#ifdef __cplusplus
}
#endif
#endif // __INC_LLT_RUNTIME_STUB_H
