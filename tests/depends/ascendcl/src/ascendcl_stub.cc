/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#include <string>
#include <map>
#include <queue>
#include <iostream>
#include "ascendcl_stub.h"
#include "mmpa/mmpa_api.h"

static std::string g_acl_stub_mock = "";
static char g_soc_version[50] = {0};

static int32_t g_free_stream_num = 2048;
static int32_t g_free_event_num = 2048;
static int32_t g_cnt_rtStreamSynchronize_over_flow = 0;
static int32_t g_cnt_rtStreamSynchronize_fail = 0;
static size_t reserve_mem_size_ = 200UL * 1024UL * 1024UL;

#define EVENT_LENTH 10
#define NOTIFY_LENTH 10

namespace llm {
std::string &GetAclStubMock() {
  return g_acl_stub_mock;
}

struct aclrtContextStub {
    int32_t deviceId;
};

std::shared_ptr<AclRuntimeStub> AclRuntimeStub::instance_;
std::mutex AclRuntimeStub::mutex_;
thread_local AclRuntimeStub* AclRuntimeStub::fake_instance_;
AclRuntimeStub *AclRuntimeStub::GetInstance() {
  const std::lock_guard<std::mutex> lock(mutex_);
  if(fake_instance_ != nullptr){
    return fake_instance_;
  }
  if (instance_ == nullptr) {
    instance_ = std::make_shared<AclRuntimeStub>();
  }
  return instance_.get();
}

void AclRuntimeStub::Install(AclRuntimeStub* instance){
  fake_instance_ = instance;
}

void AclRuntimeStub::UnInstall(AclRuntimeStub*){
  fake_instance_ = nullptr;
}

aclError AclRuntimeStub::aclrtRecordNotify(aclrtNotify notify, aclrtStream stream) {
  return ACL_SUCCESS;
}

aclError AclRuntimeStub::aclrtBinaryGetFunctionByEntry(aclrtBinHandle binHandle,
                                                       uint64_t funcEntry,
                                                       aclrtFuncHandle *funcHandle) {
  return ACL_SUCCESS;
}

aclError AclRuntimeStub::aclrtLaunchKernel(aclrtFuncHandle funcHandle,
                                           uint32_t blockDim,
                                           const void *argsData,
                                           size_t argsSize,
                                           aclrtStream stream) {
  return ACL_SUCCESS;
}

aclError AclRuntimeStub::aclrtStreamGetId(aclrtStream stream, int32_t *streamId) {
  return ACL_SUCCESS;
}

aclError AclRuntimeStub::aclrtWaitAndResetNotify(aclrtNotify notify, aclrtStream stream, uint32_t timeout) {
  return ACL_SUCCESS;
}

aclError AclRuntimeStub::aclrtSetDevice(int32_t deviceId) {
  return ACL_ERROR_NONE;
}

aclError AclRuntimeStub::aclrtResetDevice(int32_t deviceId) {
  return ACL_ERROR_NONE;
}

aclError AclRuntimeStub::aclrtGetDevice(int32_t *deviceId) {
  if (__FUNCTION__ == g_acl_stub_mock) {
    return ACL_ERROR_RT_INTERNAL_ERROR;
  }
  *deviceId = 0;
  return ACL_ERROR_NONE;
}

aclError AclRuntimeStub::aclrtGetThreadLastTaskId(uint32_t *taskId) {
  return ACL_SUCCESS;
}

aclError AclRuntimeStub::aclrtCreateContext(aclrtContext *context, int32_t deviceId) {
  aclrtContextStub *ctxStub = new aclrtContextStub;
  ctxStub->deviceId = deviceId;
  *context = ctxStub;
  return ACL_ERROR_NONE;
}

aclError AclRuntimeStub::aclrtDestroyContext(aclrtContext context) {
  if (context != nullptr) {
    delete (aclrtContextStub *)context;
  }
  return ACL_ERROR_NONE;
}

aclError AclRuntimeStub::aclrtSetCurrentContext(aclrtContext context) {
  const char * const kEnvRecordPath = "SET_TRANS_VAR_DATA";
  char record_path[MMPA_MAX_PATH] = {};
  (void)mmGetEnv(kEnvRecordPath, &record_path[0], static_cast<uint32_t>(MMPA_MAX_PATH));

  if (std::string(&record_path[0]).find("mock_fail") != std::string::npos) {
    return ACL_ERROR_RT_INTERNAL_ERROR;
  }
  return ACL_ERROR_NONE;
}

aclError AclRuntimeStub::aclrtGetCurrentContext(aclrtContext *context) {
  uintptr_t x = 1;
  *context = (aclrtContext *)x;
  return ACL_ERROR_NONE;
}

aclError AclRuntimeStub::aclrtCreateEvent(aclrtEvent *event) {
  if (__FUNCTION__ == g_acl_stub_mock) {
    return ACL_ERROR_RT_INTERNAL_ERROR;
  }

  if(g_free_event_num <= 0) {
    return ACL_ERROR_RT_INTERNAL_ERROR;
  }
  g_free_event_num--;
  *event = new int[EVENT_LENTH];
  return ACL_ERROR_NONE;
}

aclError AclRuntimeStub::aclrtDestroyEvent(aclrtEvent event) {
  g_free_event_num++;
  delete[](int *) event;
  return ACL_ERROR_NONE;
}

aclError AclRuntimeStub::aclrtRecordEvent(aclrtEvent event, aclrtStream stream) {
  return ACL_ERROR_NONE;
}

aclError AclRuntimeStub::aclrtQueryEventStatus(aclrtEvent event, aclrtEventRecordedStatus *status) {
  *status = ACL_EVENT_RECORDED_STATUS_COMPLETE;
  return ACL_ERROR_NONE;
}

aclError AclRuntimeStub::aclrtCreateStream(aclrtStream *stream) {
  const char * const kEnvRecordPath = "CONSTANT_FOLDING_PASS_4";
  char record_path[MMPA_MAX_PATH] = {};
  (void)mmGetEnv(kEnvRecordPath, &record_path[0], static_cast<uint32_t>(MMPA_MAX_PATH));
  if (std::string(&record_path[0]).find("mock_fail") != std::string::npos) {
    return ACL_ERROR_RT_INTERNAL_ERROR;
  }

  if(g_free_stream_num <= 0) {
    return ACL_ERROR_RT_INTERNAL_ERROR;
  }
  g_free_stream_num--;
  *stream = new uint32_t;
  return ACL_ERROR_NONE;
}

aclError AclRuntimeStub::aclrtCreateStreamWithConfig(aclrtStream *stream, uint32_t priority, uint32_t flag) {
  if(g_free_stream_num <= 0) {
    return ACL_ERROR_RT_INTERNAL_ERROR;
  }
  g_free_stream_num--;
  *stream = new uint32_t;
  return ACL_ERROR_NONE;
}

aclError AclRuntimeStub::aclrtDestroyStream(aclrtStream stream) {
  if (stream != nullptr) {
    delete (uint32_t *)stream;
  }
  g_free_stream_num++;
  return ACL_ERROR_NONE;
}

aclError AclRuntimeStub::aclrtStreamAbort(aclrtStream stream) {
  (void) stream;
  return ACL_ERROR_NONE;
}

aclError AclRuntimeStub::aclrtStreamWaitEvent(aclrtStream stream, aclrtEvent event) {
  (void) stream;
  return ACL_ERROR_NONE;
}

aclError AclRuntimeStub::aclrtSynchronizeStream(aclrtStream stream) {
  const char * const kEnvRecordPath = "CONSTANT_FOLDING_PASS_9";
  char record_path[MMPA_MAX_PATH] = {};
  (void)mmGetEnv(kEnvRecordPath, &record_path[0], static_cast<uint32_t>(MMPA_MAX_PATH));
  if (std::string(&record_path[0]).find("mock_fail") != std::string::npos) {
    return ACL_ERROR_RT_INTERNAL_ERROR;
  }

  const char * const kEnvPath = "END_OF_SEQUENCE";
  char env_path[MMPA_MAX_PATH] = {};
  (void)mmGetEnv(kEnvPath, &env_path[0], static_cast<uint32_t>(MMPA_MAX_PATH));
  if (std::string(&env_path[0]).find("end") != std::string::npos) {
    return ACL_ERROR_RT_END_OF_SEQUENCE;
  }

  const char * const kEnvOverFlowPath = "ACL_ERROR_RT_OVER_FLOW";
  char over_flow_path[MMPA_MAX_PATH] = {};
  (void)mmGetEnv(kEnvOverFlowPath, &over_flow_path[0], static_cast<uint32_t>(MMPA_MAX_PATH));
  if (std::string(&over_flow_path[0]).find("over_flow") != std::string::npos) {
    return ACL_ERROR_RT_OVER_FLOW;
  }

  const char * const kEnvPathSt = "MOCK_FAIL_ST";
  char env_path_st[MMPA_MAX_PATH] = {};
  (void)mmGetEnv(kEnvPathSt, &env_path_st[0], static_cast<uint32_t>(MMPA_MAX_PATH));
  if (std::string(&env_path_st[0]).find("mock_st_fail") != std::string::npos) {
    g_cnt_rtStreamSynchronize_fail++;
    if (g_cnt_rtStreamSynchronize_fail == 3) {
      return ACL_ERROR_RT_INTERNAL_ERROR;
    }
  }

  const char * const kEnvOverFlowPathSt = "ACL_ERROR_RT_OVER_FLOW_ST";
  char over_flow_path_st[MMPA_MAX_PATH] = {};
  (void)mmGetEnv(kEnvOverFlowPathSt, &over_flow_path_st[0], static_cast<uint32_t>(MMPA_MAX_PATH));
  if (std::string(&over_flow_path_st[0]).find("over_st_flow") != std::string::npos) {
    g_cnt_rtStreamSynchronize_over_flow++;
    if (g_cnt_rtStreamSynchronize_over_flow == 3) {
      return ACL_ERROR_RT_OVER_FLOW;
    }
  }

  return ACL_ERROR_NONE;
}

aclError AclRuntimeStub::aclrtSynchronizeStreamWithTimeout(aclrtStream stream, int32_t timeout) {
  const char * const kEnvRecordPath = "CONSTANT_FOLDING_PASS_9";
  char record_path[MMPA_MAX_PATH] = {};
  (void)mmGetEnv(kEnvRecordPath, &record_path[0], static_cast<uint32_t>(MMPA_MAX_PATH));
  if (std::string(&record_path[0]).find("mock_fail") != std::string::npos) {
    return ACL_ERROR_RT_INTERNAL_ERROR;
  }
  const char * const kEnvPath = "END_OF_SEQUENCE";
  char env_path[MMPA_MAX_PATH] = {};
  (void)mmGetEnv(kEnvPath, &env_path[0], static_cast<uint32_t>(MMPA_MAX_PATH));
  if (std::string(&env_path[0]).find("end") != std::string::npos) {
    return ACL_ERROR_RT_END_OF_SEQUENCE;
  }

  const char * const kEnvPathWithTimeout = "WITH_TIMEOUT_END_OF_SEQUENCE";
  char end_path[MMPA_MAX_PATH] = {};
  (void)mmGetEnv(kEnvPathWithTimeout, &end_path[0], static_cast<uint32_t>(MMPA_MAX_PATH));
  if (std::string(&end_path[0]).find("end") != std::string::npos) {
    return ACL_ERROR_RT_END_OF_SEQUENCE;
  }

  const char * const kTimeoutEnvPath = "TIMEOUT";
  char timeout_env_path[MMPA_MAX_PATH] = {};
  (void)mmGetEnv(kTimeoutEnvPath, &timeout_env_path[0], static_cast<uint32_t>(MMPA_MAX_PATH));
  if (std::string(&timeout_env_path[0]).find("timeout") != std::string::npos) {
    return ACL_ERROR_RT_STREAM_SYNC_TIMEOUT;
  }
  const char * const kOverflowEnvPath = "SYNCSTREAM_OVERFLOW_RET";
  char overflow_env_path[MMPA_MAX_PATH] = {};
  (void)mmGetEnv(kOverflowEnvPath, &overflow_env_path[0], static_cast<uint32_t>(MMPA_MAX_PATH));
  if (std::string(&overflow_env_path[0]).find("aicore") != std::string::npos) {
    return ACL_ERROR_RT_AICORE_OVER_FLOW;
  }
  if (std::string(&overflow_env_path[0]).find("aicpu") != std::string::npos) {
    return ACL_ERROR_RT_OVER_FLOW;
  }
  if (std::string(__FUNCTION__) == g_acl_stub_mock) {
    return ACL_ERROR_RT_INTERNAL_ERROR;
  }
  return ACL_ERROR_NONE;
}

aclError AclRuntimeStub::aclrtMalloc(void **devPtr, size_t size, aclrtMemMallocPolicy policy) {
  const char * const kEnvRecordPath = "CONSTANT_FOLDING_PASS_2";
  char record_path[MMPA_MAX_PATH] = {};
  (void)mmGetEnv(kEnvRecordPath, &record_path[0], static_cast<uint32_t>(MMPA_MAX_PATH));
  if (std::string(&record_path[0]).find("mock_fail") != std::string::npos) {
    return ACL_ERROR_RT_INTERNAL_ERROR;
  }

  const char *const kEnvHybridProfiling = "HYBRID_PROFILING_LEVEL";
  char record_path1[MMPA_MAX_PATH] = {};
  (void)mmGetEnv(kEnvHybridProfiling, &record_path1[0], static_cast<uint32_t>(MMPA_MAX_PATH));
  if (std::string(&record_path1[0]).find("1") != std::string::npos) {
    *devPtr = new uint8_t[size];
    memset_s(*devPtr, size, 0, size);
    return ACL_ERROR_NONE;
  }
  if (std::string(__FUNCTION__) == g_acl_stub_mock) {
    return ACL_ERROR_RT_INTERNAL_ERROR;
  }
  if (size == 123) {
    return ACL_ERROR_RT_INTERNAL_ERROR;
  }
  const char *const kEnvRecordPath_Huge = "MOCK_MEMCPY_HUGE";
  char record_path_Huge[MMPA_MAX_PATH] = {};
  int32_t ret = mmGetEnv(kEnvRecordPath_Huge, &record_path_Huge[0], static_cast<uint32_t>(MMPA_MAX_PATH));
  if ((ret == EN_OK) && (strlen(record_path_Huge) != 0)) {
    *devPtr = new uint8_t[size];
    memset_s(*devPtr, size, 0, size);
    return ACL_ERROR_NONE;
  }
  if (size > INT32_MAX) {
    *devPtr = new uint8_t[1024U];
    return ACL_ERROR_NONE;
  }
  *devPtr = new uint8_t[size];
  memset_s(*devPtr, size, 0, size);
  return ACL_ERROR_NONE;
}

aclError AclRuntimeStub::aclrtMallocHost(void **hostPtr, size_t size) {
  return aclrtMalloc(hostPtr, size, ACL_MEM_MALLOC_HUGE_FIRST);
}

aclError AclRuntimeStub::aclrtMemset(void *devPtr, size_t maxCount, int32_t value, size_t count) {
  if (maxCount == 321) {
    return ACL_ERROR_RT_INTERNAL_ERROR;
  }
  memset_s(devPtr, maxCount, value, count);
  return ACL_ERROR_NONE;
}

aclError AclRuntimeStub::aclrtFree(void *devPtr) {
  delete[](uint8_t *) devPtr;
  return ACL_ERROR_NONE;
}

aclError AclRuntimeStub::aclrtFreeHost(void *devPtr) {
  delete[](uint8_t *) devPtr;
  return ACL_ERROR_NONE;
}

aclError AclRuntimeStub::aclrtMemcpy(void *dst, size_t dest_max, const void *src, size_t count, aclrtMemcpyKind kind) {
  const char * const kEnvRecordPath = "CONSTANT_FOLDING_PASS";
  char record_path[MMPA_MAX_PATH] = {};
  (void)mmGetEnv(kEnvRecordPath, &record_path[0], static_cast<uint32_t>(MMPA_MAX_PATH));
  if (std::string(&record_path[0]).find("mock_fail") != std::string::npos) {
    return ACL_ERROR_RT_INTERNAL_ERROR;
  }
  const char *const kEnvRecordPath1 = "NPU_COLLECT_PATH_EXE";
  (void)mmGetEnv(kEnvRecordPath1, &record_path[0], static_cast<uint32_t>(MMPA_MAX_PATH));
  if (!std::string(&record_path[0]).empty()) {
    return ACL_ERROR_NONE;
  }

  if (__FUNCTION__ == g_acl_stub_mock) {
    return ACL_ERROR_RT_INTERNAL_ERROR;
  }

  if (dst != nullptr && src != nullptr) {
    dest_max = std::min(dest_max, reserve_mem_size_);
    memcpy_s(dst, dest_max, src, count);
  }
  return ACL_ERROR_NONE;
}

aclError AclRuntimeStub::aclrtMemcpyAsync(void *dst,
              size_t dest_max,
              const void *src,
              size_t src_count,
              aclrtMemcpyKind kind,
              aclrtStream stream) {
  const char *const kEnvRecordPath = "MOCK_MEMCPY_HUGE";
  char record_path[MMPA_MAX_PATH] = {};
  int32_t ret = mmGetEnv(kEnvRecordPath, &record_path[0], static_cast<uint32_t>(MMPA_MAX_PATH));
  if ((ret != EN_OK) || (strlen(record_path) == 0)) {
    if (dst != nullptr && src != nullptr) {
      dest_max = std::min(dest_max, reserve_mem_size_);
      memcpy_s(dst, dest_max, src, src_count);
    }
    return ACL_ERROR_NONE;
  }
  size_t offset = 0U;
  size_t remain_size = src_count;
  do {
    size_t copy_size = (remain_size > SECUREC_MEM_MAX_LEN) ? SECUREC_MEM_MAX_LEN : remain_size;
    memcpy_s((dst + offset), copy_size, (src + offset), copy_size);
    offset += copy_size;
    remain_size -= copy_size;
  } while (remain_size > 0U);
  return ACL_ERROR_NONE;
}

aclError AclRuntimeStub::aclrtMemcpyAsyncWithCondition(void *dst,
                                                      size_t destMax,
                                                      const void *src,
                                                      size_t count,
                                                      aclrtMemcpyKind kind,
                                                      aclrtStream stream) {
  return aclrtMemcpyAsync(dst, destMax, src, count, kind, stream);
}

aclError AclRuntimeStub::aclrtGetMemInfo(aclrtMemAttr attr, size_t *free_size, size_t *total) {
  *free_size = 64UL * 1024UL * 1024UL;
  *total = 128UL * 1024UL * 1024UL;
  return ACL_ERROR_NONE;
}

const char* AclRuntimeStub::aclrtGetSocName() {
  return g_soc_version;
}

aclError AclRuntimeStub::aclrtGetDeviceInfo(uint32_t deviceId, aclrtDevAttr attr, int64_t *value) {
  *value = 8;
  return ACL_ERROR_NONE;
}

aclError AclRuntimeStub::aclrtGetPhyDevIdByLogicDevId(const int32_t logicDevId, int32_t *const phyDevId) {
  *phyDevId = logicDevId;
  return ACL_ERROR_NONE;
}

aclError AclRuntimeStub::aclrtMemcpyBatch(void **dsts, size_t *destMax, void **srcs, size_t *sizes, size_t numBatches,
                          aclrtMemcpyBatchAttr *attrs, size_t *attrsIndexex, size_t numAttrs, size_t *failIndex)
{
  *failIndex = static_cast<size_t>(0);
  if (__FUNCTION__ == g_acl_stub_mock) {
    return ACL_ERROR_RT_INTERNAL_ERROR;
  }

  if (dsts != nullptr && srcs != nullptr) {
    for (size_t i = 0; i < numBatches; i++) {
      memcpy_s(dsts[i], destMax[i], srcs[i], sizes[i]);
    }
  }
  return ACL_ERROR_NONE;
}

aclError AclRuntimeStub::aclrtReserveMemAddress(void** devPtr, size_t size, size_t alignment, void *devAddr, uint64_t flags) {
  if (size < 200UL * 1024UL *1024UL) {
    *devPtr = new uint8_t[size];
    reserve_mem_size_ = size;
  } else {
    *devPtr = new uint8_t[reserve_mem_size_];
  }
  memset_s(*devPtr, reserve_mem_size_, 0, reserve_mem_size_);
  return ACL_ERROR_NONE;
}

aclError AclRuntimeStub::aclrtReleaseMemAddress(void* devPtr) {
  delete[] (uint8_t *)devPtr;
  return ACL_ERROR_NONE;
}

aclError AclRuntimeStub::aclrtMapMem(void* devPtr, size_t size, size_t offset, aclrtDrvMemHandle handle, uint64_t flags) {
  return ACL_ERROR_NONE;
}

aclError AclRuntimeStub::aclrtUnmapMem(void* devPtr) {
  return ACL_ERROR_NONE;
}

aclError AclRuntimeStub::aclrtMemRetainAllocationHandle(void *devPtr, aclrtDrvMemHandle *handle) {
  return ACL_ERROR_NONE;
}

aclError AclRuntimeStub::aclrtPointerGetAttributes(const void *ptr, aclrtPtrAttributes *attributes) {
  attributes->location.type = ACL_MEM_LOCATION_TYPE_DEVICE;
  return ACL_ERROR_NONE;
}

aclError AclRuntimeStub::aclrtMemExportToShareableHandleV2(aclrtDrvMemHandle handle, uint64_t flags,
                                                          aclrtMemSharedHandleType type, void *shareableHandle) {
  return ACL_ERROR_NONE;
}

aclError AclRuntimeStub::aclrtMemImportFromShareableHandleV2(void *shareableHandle, aclrtMemSharedHandleType type,
                                                    uint64_t flags, aclrtDrvMemHandle *handle) {
  *handle = (aclrtDrvMemHandle) new uint8_t[8];
  return ACL_ERROR_NONE;
}

aclError AclRuntimeStub::aclrtMallocPhysical(aclrtDrvMemHandle *handle, size_t size, const aclrtPhysicalMemProp *prop, uint64_t flags) {
  *handle = (aclrtDrvMemHandle) new uint8_t[8];
  return ACL_ERROR_NONE;
}

aclError AclRuntimeStub::aclrtFreePhysical(aclrtDrvMemHandle handle) {
  delete[] (uint8_t *)handle;
  return ACL_ERROR_NONE;
}
}

#ifdef __cplusplus
extern "C" {
#endif

aclError aclrtRecordNotify(aclrtNotify notify, aclrtStream stream) {
  return llm::AclRuntimeStub::GetInstance()->aclrtRecordNotify(notify, stream);
}

aclError aclrtBinaryGetFunctionByEntry(aclrtBinHandle binHandle, uint64_t funcEntry, aclrtFuncHandle *funcHandle) {
  return llm::AclRuntimeStub::GetInstance()->aclrtBinaryGetFunctionByEntry(binHandle, funcEntry, funcHandle);
}

aclError aclrtLaunchKernel(aclrtFuncHandle funcHandle, uint32_t blockDim, const void *argsData, size_t argsSize,
  aclrtStream stream) {
  return llm::AclRuntimeStub::GetInstance()->aclrtLaunchKernel(funcHandle, blockDim, argsData, argsSize, stream);
}

aclError aclrtStreamGetId(aclrtStream stream, int32_t *streamId) {
  return llm::AclRuntimeStub::GetInstance()->aclrtStreamGetId(stream, streamId);
}

aclError aclrtWaitAndResetNotify(aclrtNotify notify, aclrtStream stream, uint32_t timeout) {
  return llm::AclRuntimeStub::GetInstance()->aclrtWaitAndResetNotify(notify, stream, timeout);
}

aclError aclrtSetDevice(int32_t deviceId) {
  return llm::AclRuntimeStub::GetInstance()->aclrtSetDevice(deviceId);
}

aclError aclrtResetDevice(int32_t deviceId) {
  return llm::AclRuntimeStub::GetInstance()->aclrtResetDevice(deviceId);
}

aclError aclrtGetDevice(int32_t *deviceId) {
  return llm::AclRuntimeStub::GetInstance()->aclrtGetDevice(deviceId);
}

aclError aclrtGetThreadLastTaskId(uint32_t *taskId) {
  return llm::AclRuntimeStub::GetInstance()->aclrtGetThreadLastTaskId(taskId);
}

aclError aclrtCreateContext(aclrtContext *context, int32_t deviceId) {
  return llm::AclRuntimeStub::GetInstance()->aclrtCreateContext(context, deviceId);
}

aclError aclrtDestroyContext(aclrtContext context) {
  return llm::AclRuntimeStub::GetInstance()->aclrtDestroyContext(context);
}

aclError aclrtSetCurrentContext(aclrtContext context) {
  return llm::AclRuntimeStub::GetInstance()->aclrtSetCurrentContext(context);
}

aclError aclrtGetCurrentContext(aclrtContext *context) {
  return llm::AclRuntimeStub::GetInstance()->aclrtGetCurrentContext(context);
}

aclError aclrtCreateEvent(aclrtEvent *event) {
  return llm::AclRuntimeStub::GetInstance()->aclrtCreateEvent(event);
}

aclError aclrtDestroyEvent(aclrtEvent event) {
  return llm::AclRuntimeStub::GetInstance()->aclrtDestroyEvent(event);
}

aclError aclrtRecordEvent(aclrtEvent event, aclrtStream stream) {
  return llm::AclRuntimeStub::GetInstance()->aclrtRecordEvent(event, stream);
}

aclError aclrtQueryEventStatus(aclrtEvent event, aclrtEventRecordedStatus *status) {
  return llm::AclRuntimeStub::GetInstance()->aclrtQueryEventStatus(event, status);
}

aclError aclrtCreateStream(aclrtStream *stream) {
  return llm::AclRuntimeStub::GetInstance()->aclrtCreateStream(stream);
}

aclError aclrtCreateStreamWithConfig(aclrtStream *stream, uint32_t priority, uint32_t flag) {
  return llm::AclRuntimeStub::GetInstance()->aclrtCreateStreamWithConfig(stream, priority, flag);
}

aclError aclrtDestroyStream(aclrtStream stream) {
  return llm::AclRuntimeStub::GetInstance()->aclrtDestroyStream(stream);
}

aclError aclrtStreamAbort(aclrtStream stream) {
  return llm::AclRuntimeStub::GetInstance()->aclrtStreamAbort(stream);
}

aclError aclrtStreamWaitEvent(aclrtStream stream, aclrtEvent event) {
  return llm::AclRuntimeStub::GetInstance()->aclrtStreamWaitEvent(stream, event);
}

aclError aclrtSynchronizeStream(aclrtStream stream) {
  return llm::AclRuntimeStub::GetInstance()->aclrtSynchronizeStream(stream);
}

aclError aclrtSynchronizeStreamWithTimeout(aclrtStream stream, int32_t timeout) {
  return llm::AclRuntimeStub::GetInstance()->aclrtSynchronizeStreamWithTimeout(stream, timeout);
}

aclError aclrtMalloc(void **devPtr, size_t size, aclrtMemMallocPolicy policy) {
  return llm::AclRuntimeStub::GetInstance()->aclrtMalloc(devPtr, size, policy);
}

aclError aclrtMallocHost(void **hostPtr, size_t size) {
  return llm::AclRuntimeStub::GetInstance()->aclrtMallocHost(hostPtr, size);
}

aclError aclrtMemset(void *devPtr, size_t maxCount, int32_t value, size_t count) {
  return llm::AclRuntimeStub::GetInstance()->aclrtMemset(devPtr, maxCount, value, count);
}

aclError aclrtFree(void *devPtr) {
  return llm::AclRuntimeStub::GetInstance()->aclrtFree(devPtr);
}

aclError aclrtFreeHost(void *devPtr) {
  return llm::AclRuntimeStub::GetInstance()->aclrtFreeHost(devPtr);
}

aclError aclrtMemcpy(void *dst, size_t dest_max, const void *src, size_t count, aclrtMemcpyKind kind) {
  return llm::AclRuntimeStub::GetInstance()->aclrtMemcpy(dst, dest_max, src, count, kind);
}

aclError aclrtMemcpyAsync(void *dst,
                          size_t dest_max,
                          const void *src,
                          size_t src_count,
                          aclrtMemcpyKind kind,
                          aclrtStream stream) {
  return llm::AclRuntimeStub::GetInstance()->aclrtMemcpyAsync(dst, dest_max, src, src_count, kind, stream);
}

aclError aclrtMemcpyAsyncWithCondition(void *dst,
                                        size_t destMax,
                                        const void *src,
                                        size_t count,
                                        aclrtMemcpyKind kind,
                                        aclrtStream stream) {
  return llm::AclRuntimeStub::GetInstance()->aclrtMemcpyAsyncWithCondition(dst, destMax, src, count, kind, stream);
}

aclError aclrtGetMemInfo(aclrtMemAttr attr, size_t *free_size, size_t *total) {
  return llm::AclRuntimeStub::GetInstance()->aclrtGetMemInfo(attr, free_size, total);
}

const char* aclrtGetSocName() {
  return llm::AclRuntimeStub::GetInstance()->aclrtGetSocName();
}

aclError aclrtGetDeviceInfo(uint32_t deviceId, aclrtDevAttr attr, int64_t *value) {
  return llm::AclRuntimeStub::GetInstance()->aclrtGetDeviceInfo(deviceId, attr, value);
}

aclError aclrtGetPhyDevIdByLogicDevId(const int32_t logicDevId, int32_t *const phyDevId) {
  return llm::AclRuntimeStub::GetInstance()->aclrtGetPhyDevIdByLogicDevId(logicDevId, phyDevId);
}

aclError aclrtMemcpyBatch(void **dsts, size_t *destMax, void **srcs, size_t *sizes, size_t numBatches,
                          aclrtMemcpyBatchAttr *attrs, size_t *attrsIndexex, size_t numAttrs, size_t *failIndex)
{
  return llm::AclRuntimeStub::GetInstance()->aclrtMemcpyBatch(dsts, destMax, srcs, sizes, numBatches,
                                                              attrs, attrsIndexex, numAttrs, failIndex);
}

aclError aclrtReserveMemAddress(void** devPtr, size_t size, size_t alignment, void *devAddr, uint64_t flags) {
  return llm::AclRuntimeStub::GetInstance()->aclrtReserveMemAddress(devPtr, size, alignment, devAddr, flags);
}

aclError aclrtReleaseMemAddress(void* devPtr) {
  return llm::AclRuntimeStub::GetInstance()->aclrtReleaseMemAddress(devPtr);
}

aclError aclrtMapMem(void* devPtr, size_t size, size_t offset, aclrtDrvMemHandle handle, uint64_t flags) {
  return llm::AclRuntimeStub::GetInstance()->aclrtMapMem(devPtr, size, offset, handle, flags);
}

aclError aclrtUnmapMem(void* devPtr) {
  return llm::AclRuntimeStub::GetInstance()->aclrtUnmapMem(devPtr);
}

aclError aclrtMemRetainAllocationHandle(void *devPtr, aclrtDrvMemHandle *handle) {
  return llm::AclRuntimeStub::GetInstance()->aclrtMemRetainAllocationHandle(devPtr, handle);
}

aclError aclrtPointerGetAttributes(const void *ptr, aclrtPtrAttributes *attributes) {
  return llm::AclRuntimeStub::GetInstance()->aclrtPointerGetAttributes(ptr, attributes);
}

aclError aclrtMemExportToShareableHandleV2(aclrtDrvMemHandle handle, uint64_t flags, aclrtMemSharedHandleType type,
                                          void *shareableHandle) {
  return llm::AclRuntimeStub::GetInstance()->aclrtMemExportToShareableHandleV2(handle, flags, type, shareableHandle);  
}

aclError aclrtMemImportFromShareableHandleV2(void *shareableHandle, aclrtMemSharedHandleType type,
                                             uint64_t flags, aclrtDrvMemHandle *handle) {
  return llm::AclRuntimeStub::GetInstance()->aclrtMemImportFromShareableHandleV2(shareableHandle, type,
                                                              flags, handle);  
}

aclError aclrtMallocPhysical(aclrtDrvMemHandle *handle, size_t size, const aclrtPhysicalMemProp *prop, uint64_t flags) {
  return llm::AclRuntimeStub::GetInstance()->aclrtMallocPhysical(handle, size, prop, flags);  
}

aclError aclrtFreePhysical(aclrtDrvMemHandle handle) {
  return llm::AclRuntimeStub::GetInstance()->aclrtFreePhysical(handle);  
}

#ifdef __cplusplus
}
#endif