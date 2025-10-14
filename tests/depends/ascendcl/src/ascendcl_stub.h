/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef INC_EXTERNAL_ACL_ACL_RT_STUB_H_
#define INC_EXTERNAL_ACL_ACL_RT_STUB_H_

#include <stdint.h>
#include <stddef.h>
#include <vector>
#include <memory>
#include <mutex>
#include "mmpa/mmpa_api.h"
#include "acl/acl.h"

namespace llm {
class AclRuntimeStub {
public:
  virtual ~AclRuntimeStub() = default;

  static AclRuntimeStub* GetInstance();

  static void SetInstance(const std::shared_ptr<AclRuntimeStub> &instance) {
    instance_ = instance;
  }

  static void Install(AclRuntimeStub*);
  static void UnInstall(AclRuntimeStub*);

  static void Reset() {
    instance_.reset();
  }

  virtual aclError aclrtRecordNotify(aclrtNotify notify, aclrtStream stream);
  virtual aclError aclrtBinaryGetFunctionByEntry(aclrtBinHandle binHandle,
                                                 uint64_t funcEntry,
                                                 aclrtFuncHandle *funcHandle);
  virtual aclError aclrtLaunchKernel(aclrtFuncHandle funcHandle,
                                     uint32_t blockDim,
                                     const void *argsData,
                                     size_t argsSize,
                                     aclrtStream stream);
  virtual aclError aclrtStreamGetId(aclrtStream stream, int32_t *streamId);
  virtual aclError aclrtWaitAndResetNotify(aclrtNotify notify, aclrtStream stream, uint32_t timeout);
  virtual aclError aclrtSetDevice(int32_t deviceId);
  virtual aclError aclrtResetDevice(int32_t deviceId);
  virtual aclError aclrtGetDevice(int32_t *deviceId);
  virtual aclError aclrtGetThreadLastTaskId(uint32_t *taskId);
  virtual aclError aclrtSetCurrentContext(aclrtContext context);
  virtual aclError aclrtGetCurrentContext(aclrtContext *context);
  virtual aclError aclrtCreateEvent(aclrtEvent *event);
  virtual aclError aclrtDestroyEvent(aclrtEvent event);
  virtual aclError aclrtRecordEvent(aclrtEvent event, aclrtStream stream);
  virtual aclError aclrtQueryEventStatus(aclrtEvent event, aclrtEventRecordedStatus *status);
  virtual aclError aclrtCreateStream(aclrtStream *stream);
  virtual aclError aclrtCreateStreamWithConfig(aclrtStream *stream, uint32_t priority, uint32_t flag);
  virtual aclError aclrtDestroyStream(aclrtStream stream);
  virtual aclError aclrtStreamAbort(aclrtStream stream);
  virtual aclError aclrtSynchronizeStream(aclrtStream stream);
  virtual aclError aclrtSynchronizeStreamWithTimeout(aclrtStream stream, int32_t timeout);
  virtual aclError aclrtMalloc(void **devPtr, size_t size, aclrtMemMallocPolicy policy);
  virtual aclError aclrtMallocHost(void **hostPtr, size_t size);
  virtual aclError aclrtMemset(void *devPtr, size_t maxCount, int32_t value, size_t count);
  virtual aclError aclrtFree(void *devPtr);
  virtual aclError aclrtFreeHost(void *devPtr);
  virtual aclError aclrtMemcpy(void *dst, size_t dest_max, const void *src, size_t count, aclrtMemcpyKind kind);
  virtual aclError aclrtMemcpyAsync(void *dst,
                    size_t dest_max,
                    const void *src,
                    size_t src_count,
                    aclrtMemcpyKind kind,
                    aclrtStream stream);
  virtual aclError aclrtMemcpyAsyncWithCondition(void *dst,
                          size_t destMax,
                          const void *src,
                          size_t count,
                          aclrtMemcpyKind kind,
                          aclrtStream stream);
  virtual aclError aclrtGetMemInfo(aclrtMemAttr attr, size_t *free_size, size_t *total);
  virtual aclError aclrtGetSocVersion(char *version, const uint32_t maxLen);
  virtual aclError aclrtGetDeviceInfo(uint32_t deviceId, aclrtDevAttr attr, int64_t *value);
  virtual aclError aclrtGetDevicePhyIdByIndex(uint32_t devIndex, uint32_t *phyId);

private:
  static std::mutex mutex_;
  static std::shared_ptr<AclRuntimeStub> instance_;
  static thread_local AclRuntimeStub *fake_instance_;
  size_t reserve_mem_size_ = 200UL * 1024UL * 1024UL;
  std::mutex mtx_;
  std::vector<aclrtStream> model_bind_streams_;
  std::vector<aclrtStream> model_unbind_streams_;
  size_t input_mem_copy_batch_count_{0UL};
};
}

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
}
#endif

#endif // INC_EXTERNAL_ACL_ACL_RT_STUB_H_
