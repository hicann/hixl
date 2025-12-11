/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.

 * The code snippet comes from Huawei's open-source Mindspore project.
 * Copyright 2021 Huawei Technologies Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 */

#ifndef __INC_LLT_RUNTIME_STUB_H
#define __INC_LLT_RUNTIME_STUB_H

#include <vector>
#include <memory>
#include <mutex>
#include "mmpa/mmpa_api.h"
#include "runtime/rt.h"
#include "rts_stub.h"

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

  virtual rtError_t rtKernelLaunchEx(void *args, uint32_t args_size, uint32_t flags, rtStream_t stream) {
    return RT_ERROR_NONE;
  }

  virtual rtError_t rtStreamSwitchEx(void *ptr, rtCondition_t condition, void *value_ptr, rtStream_t true_stream,
                                     rtStream_t stream, rtSwitchDataType_t data_type);

  virtual rtError_t rtKernelLaunch(const void *stub_func,
                                   uint32_t block_dim,
                                   void *args,
                                   uint32_t args_size,
                                   rtSmDesc_t *sm_desc,
                                   rtStream_t stream) {
    return RT_ERROR_NONE;
  }
  virtual rtError_t rtKernelLaunchWithFlag(const void *stubFunc, uint32_t blockDim, rtArgsEx_t *argsInfo,
                                   rtSmDesc_t *smDesc, rtStream_t stream, uint32_t flag) {
    return RT_ERROR_NONE;
  }
  virtual rtError_t rtKernelLaunchWithFlagV2(const void *stubFunc, uint32_t blockDim, rtArgsEx_t *argsInfo,
                                             rtSmDesc_t *smDesc, rtStream_t stm, uint32_t flags,
                                             const rtTaskCfgInfo_t *cfgInfo) {
    return RT_ERROR_NONE;
  }

  virtual rtError_t rtVectorCoreKernelLaunch(const void *stubFunc, uint32_t blockDim, rtArgsEx_t *argsInfo,
                                             rtSmDesc_t *smDesc, rtStream_t stm, uint32_t flags,
                                             const rtTaskCfgInfo_t *cfgInfo) {
    return RT_ERROR_NONE;
  }

  virtual rtError_t rtAicpuKernelLaunchWithFlag(const rtKernelLaunchNames_t *launchNames, uint32_t blockDim,
                                                  const rtArgsEx_t *args, rtSmDesc_t *smDesc, rtStream_t stream,
                                                  uint32_t flags) {
    return RT_ERROR_NONE;
  }

  virtual rtError_t rtAicpuKernelLaunchExWithArgs(uint32_t kernelType, const char *opName, uint32_t blockDim,
                                                  const rtAicpuArgsEx_t *argsInfo, rtSmDesc_t *smDesc,
                                                  rtStream_t stream, uint32_t flags) {
    return RT_ERROR_NONE;
  }

  virtual rtError_t rtKernelGetAddrAndPrefCntV2(void *handle, const uint64_t tilingKey, const void *const stubFunc,
                                                const uint32_t flag, rtKernelDetailInfo_t *kernelInfo) {
    kernelInfo->functionInfoNum = 1;
    kernelInfo->functionInfo[0].pcAddr = (void *)(0x1245);
    kernelInfo->functionInfo[0].prefetchCnt = 1;
    return RT_ERROR_NONE;
  }

  virtual rtError_t rtKernelLaunchWithHandle(void *handle, uint64_t devFunc, uint32_t blockDim, rtArgsEx_t *args,
                                     rtSmDesc_t *smDesc, rtStream_t stream, const void *kernelInfo) {
    return RT_ERROR_NONE;
  }

  virtual rtError_t rtKernelLaunchWithHandleV2(void *hdl, const uint64_t tilingKey, uint32_t blockDim,
                                               rtArgsEx_t *argsInfo, rtSmDesc_t *smDesc, rtStream_t stm,
                                               const rtTaskCfgInfo_t *cfgInfo) {
    return RT_ERROR_NONE;
  }

  virtual rtError_t rtVectorCoreKernelLaunchWithHandle(void *hdl, const uint64_t tilingKey, uint32_t blockDim,
                                                       rtArgsEx_t *argsInfo, rtSmDesc_t *smDesc, rtStream_t stm,
                                                       const rtTaskCfgInfo_t *cfgInfo) {
    return RT_ERROR_NONE;
  }

  virtual rtError_t rtGetIsHeterogenous(int32_t *heterogeneous) {
    return RT_ERROR_NONE;
  }

  virtual rtError_t rtMemGrpQuery(rtMemGrpQueryInput_t * const input, rtMemGrpQueryOutput_t *output)
  {
    return RT_ERROR_NONE;
  }

  virtual rtError_t rtGetDeviceCount(int32_t *count) {
    *count = 1;
    return RT_ERROR_NONE;
  }

  virtual rtError_t rtGetDeviceInfo(uint32_t device_id, int32_t module_type, int32_t info_type, int64_t *val) {
    *val = 8;
    return RT_ERROR_NONE;
  }

  virtual rtError_t rtGetFunctionByName(const char *stub_name, void **stub_func) {
    *(char **)stub_func = (char *)("func");
    return RT_ERROR_NONE;
  }

  virtual rtError_t rtRegisterAllKernel(const rtDevBinary_t *bin, void **handle) {
    *handle = (void*)0x12345678;
    return RT_ERROR_NONE;
  }
  virtual rtError_t rtDevBinaryRegister(const rtDevBinary_t *bin, void **handle){
    return RT_ERROR_NONE;
  }
  virtual rtError_t rtStreamSynchronizeWithTimeout(rtStream_t stm, int32_t timeout);

  virtual rtError_t rtStreamSynchronize(rtStream_t stm) {
    return RT_ERROR_NONE;
  }

  virtual rtError_t rtMemcpy(void *dst, uint64_t dest_max, const void *src, uint64_t count, rtMemcpyKind_t kind);

  virtual rtError_t rtMemcpyEx(void *dst, uint64_t dest_max, const void *src, uint64_t count, rtMemcpyKind_t kind);

  virtual rtError_t rtMemcpyAsync(void *dst, uint64_t dest_max, const void *src, uint64_t count, rtMemcpyKind_t kind,
                                  rtStream_t stream);

  virtual rtError_t rtMemcpyAsyncWithoutCheckKind(void *dst, uint64_t dest_max, const void *src, uint64_t count,
                                                  rtMemcpyKind_t kind, rtStream_t stream);

  virtual rtError_t rtMemcpyAsyncWithCfgV2(void *dst, uint64_t dest_max, const void *src, uint64_t count,
                                           rtMemcpyKind_t kind, rtStream_t stm, const rtTaskCfgInfo_t *cfgInfo);

  virtual rtError_t rtMemcpyAsyncPtr(void *memcpyAddrInfo, uint64_t destMax, uint64_t count,
                                     rtMemcpyKind_t kind, rtStream_t stream, uint32_t qosCfg);

  virtual rtError_t rtsMemcpyBatch(void **dsts, void **srcs, size_t *sizes, size_t count, rtMemcpyBatchAttr *attrs,
    size_t *attrs_idxs, size_t num_attrs, size_t *fail_idx);

  virtual rtError_t rtMalloc(void **dev_ptr, uint64_t size, rtMemType_t type, uint16_t moduleId);

  virtual rtError_t rtFree(void *dev_ptr);

  virtual rtError_t rtEschedWaitEvent(int32_t device_id,
                                      uint32_t group_id,
                                      uint32_t thread_id,
                                      int32_t timeout,
                                      rtEschedEventSummary_t *event);

  virtual rtError_t rtRegTaskFailCallbackByModule(const char *moduleName,
                                                  rtTaskFailCallback callback);

  virtual rtError_t rtMemQueueDeQueue(int32_t device, uint32_t qid, void **mbuf);

  virtual rtError_t rtMemQueuePeek(int32_t device, uint32_t qid, size_t *bufLen, int32_t timeout);

  virtual rtError_t rtMemQueueEnQueueBuff(int32_t device, uint32_t qid, rtMemQueueBuff_t *inBuf, int32_t timeout);
  virtual rtError_t rtMemQueueDeQueueBuff(int32_t device, uint32_t qid, rtMemQueueBuff_t *outBuf, int32_t timeout);

  virtual rtError_t rtMbufGetBuffAddr(rtMbufPtr_t mbuf, void **databuf);

  virtual rtError_t rtMbufGetBuffSize(rtMbufPtr_t mbuf, uint64_t *size);

  virtual rtError_t rtMbufGetPrivInfo(rtMbufPtr_t mbuf, void **priv, uint64_t *size);

  virtual rtError_t rtMbufCopyBufRef(rtMbufPtr_t mbuf, rtMbufPtr_t *ref_mbuf);

  virtual rtError_t rtMemQueueEnQueue(int32_t dev_id, uint32_t qid, void *mem_buf);

  virtual rtError_t rtGeneralCtrl(uintptr_t *ctrl, uint32_t num, uint32_t type);

  virtual rtError_t rtMemGetInfoEx(rtMemInfoType_t memInfoType, size_t *free, size_t *total);

  virtual rtError_t rtMemGrpCacheAlloc(const char *name,
                                       int32_t devId,
                                       const rtMemGrpCacheAllocPara *para);

  virtual rtError_t rtBuffAlloc(uint64_t size, void **buff);
  virtual rtError_t rtMbufAlloc(rtMbufPtr_t *mbuf, uint64_t size);
  virtual rtError_t rtMbufFree(rtMbufPtr_t mbuf);
  virtual rtError_t rtGetSocVersion(char *version, const uint32_t maxLen);
  virtual rtError_t rtDeviceReset(int32_t device);
  virtual rtError_t rtModelCheckCompatibility(const char_t *OmSoCVersion, const char_t *OMArchVersion);

  virtual rtError_t rtLaunchSqeUpdateTask(uint32_t streamId, uint32_t taskId, void *src, uint64_t cnt,
                                          rtStream_t stm) {
    return RT_ERROR_NONE;
  }
  virtual rtError_t rtSetExceptionExtInfo(const rtArgsSizeInfo_t *const sizeInfo) {
    return RT_ERROR_NONE;
  }
  virtual rtError_t rtSetTaskTag(const char *taskTag);
  virtual rtError_t rtBuffConfirm(void *buff, const uint64_t size) {
    return RT_ERROR_NONE;
  }
  virtual rtError_t rtModelCreate(rtModel_t *model, uint32_t flag);
  virtual rtError_t rtModelBindStream(rtModel_t model, rtStream_t stream, uint32_t flag);
  virtual rtError_t rtModelUnbindStream(rtModel_t model, rtStream_t stream);
  virtual rtError_t rtModelGetTaskId(void *handle, uint32_t *task_id, uint32_t *stream_id);

  virtual rtError_t rtModelExecute(rtModel_t model, rtStream_t stream, uint32_t flag){
    return RT_ERROR_NONE;
  }

  virtual rtError_t rtModelExecuteSync(rtModel_t model, rtStream_t stream, uint32_t flag, int32_t timeout){
    return RT_ERROR_NONE;
  }

  virtual rtError_t rtEventRecord(rtEvent_t event, rtStream_t stream);
  virtual rtError_t rtStreamWaitEvent(rtStream_t stream, rtEvent_t event);
  virtual rtError_t rtStreamWaitEventWithTimeout(rtStream_t stream, rtEvent_t event, uint32_t timeout);

  virtual rtError_t rtStreamCreate(rtStream_t *stream, int32_t priority);
  virtual rtError_t rtStreamCreateWithFlags(rtStream_t *stream, int32_t priority, uint32_t flags);
  virtual rtError_t rtGetAvailStreamNum(const uint32_t streamType, uint32_t * const streamCount);
  virtual rtError_t rtStreamDestroyForce(rtStream_t stream);
  virtual rtError_t rtStreamDestroy(rtStream_t stream);
  virtual rtError_t rtStreamSetMode(rtStream_t stm, const uint64_t stmMode);
  virtual rtError_t rtEventCreateWithFlag(rtEvent_t *event, uint32_t flag) {
    return RT_ERROR_NONE;
  }
  virtual rtError_t rtEventDestroy(rtEvent_t event) {
    return RT_ERROR_NONE;
  }

  virtual rtError_t rtReserveMemAddress(void** devPtr, size_t size, size_t alignment, void *devAddr, uint64_t flags);

  virtual rtError_t rtReleaseMemAddress(void* devPtr) {
    delete[] (uint8_t *)devPtr;
    return RT_ERROR_NONE;
  }

  virtual rtError_t rtMallocPhysical(rtDrvMemHandle* handle, size_t size, rtDrvMemProp_t* prop, uint64_t flags);

  virtual rtError_t rtFreePhysical(rtDrvMemHandle handle) {
    delete[] (uint8_t *)handle;
    return RT_ERROR_NONE;
  }

  virtual rtError_t rtMapMem(void* devPtr, size_t size, size_t offset, rtDrvMemHandle handle, uint64_t flags) {
    return RT_ERROR_NONE;
  }

  virtual rtError_t rtUnmapMem(void* devPtr) {
    return RT_ERROR_NONE;
  }

  virtual rtError_t rtCtxCreate(rtContext_t *ctx, uint32_t flags, int32_t device) {
    return RT_ERROR_NONE;
  }
  virtual rtError_t rtSupportModelStreamReuse(bool *bSupport) {
    (void)bSupport;
    return RT_ERROR_NONE;
  }

  virtual rtError_t rtCtxGetCurrentDefaultStream(rtStream_t* stm);

  virtual rtError_t rtDatadumpInfoLoad(const void *dump_info, uint32_t length) {
    return RT_ERROR_NONE;
  }

  virtual rtError_t rtEventQueryStatus(rtEvent_t evt, rtEventStatus_t *status) {
    *status = RT_EVENT_RECORDED;
    return RT_ERROR_NONE;
  }

  virtual rtError_t rtStreamTaskClean(rtStream_t stm) {
    (void)stm;
    return RT_ERROR_NONE;
  }
  virtual rtError_t rtsBinaryLoadFromFile(const char * const binPath, const rtLoadBinaryConfig_t *const optionalCfg,
                                          rtBinHandle *binHandle) {
    rtBinHandle tmp_binHandle = nullptr;
    *binHandle = &tmp_binHandle;                         
    return RT_ERROR_NONE;
  }
  virtual rtError_t rtsFuncGetByName(const rtBinHandle binHandle, const char *kernelName,
                                     rtFuncHandle *funcHandle) {
    rtFuncHandle tmp_funcHandle = nullptr;
    *funcHandle = &tmp_funcHandle;  
    return RT_ERROR_NONE;
  }
  virtual rtError_t rtsLaunchCpuKernel(const rtFuncHandle funcHandle, const uint32_t blockDim, rtStream_t st,
                                       const rtKernelLaunchCfg_t *cfg, rtCpuKernelArgs_t *argsInfo) {
    return RT_ERROR_NONE;
  }
  virtual rtError_t rtGetDevice(int32_t *deviceId);
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

extern std::string g_runtime_stub_mock;
extern std::string g_runtime_stub_mock_v2;
#define RTS_STUB_RETURN_EXTERN(FUNC, TYPE) extern std::vector<TYPE> g_Stub_##FUNC##_RETURN;
#define RTS_STUB_OUTBOUND_EXTERN(FUNC, TYPE, NAME) extern std::vector<TYPE> g_Stub_##FUNC##_OUT_##NAME;

RTS_STUB_RETURN_EXTERN(rtGetDevice, rtError_t);
RTS_STUB_OUTBOUND_EXTERN(rtGetDevice, int32_t, device)

RTS_STUB_RETURN_EXTERN(rtGetDeviceCapability, rtError_t);
RTS_STUB_OUTBOUND_EXTERN(rtGetDeviceCapability, int32_t, value);

RTS_STUB_RETURN_EXTERN(rtGetRtCapability, rtError_t);
RTS_STUB_OUTBOUND_EXTERN(rtGetRtCapability, int32_t, value);

RTS_STUB_RETURN_EXTERN(rtGetTsMemType, uint32_t);

RTS_STUB_RETURN_EXTERN(rtStreamWaitEvent, rtError_t);

RTS_STUB_RETURN_EXTERN(rtStreamWaitEventWithTimeout, rtError_t);

RTS_STUB_RETURN_EXTERN(rtEventReset, rtError_t);

RTS_STUB_RETURN_EXTERN(rtEventRecord, rtError_t);

RTS_STUB_RETURN_EXTERN(rtEventQueryStatus, rtError_t);

RTS_STUB_RETURN_EXTERN(rtEventCreate, rtError_t);
RTS_STUB_OUTBOUND_EXTERN(rtEventCreate, rtEvent_t, event);

RTS_STUB_RETURN_EXTERN(rtGetEventID, rtError_t);
RTS_STUB_OUTBOUND_EXTERN(rtEventCreate, uint32_t, event_id);

RTS_STUB_RETURN_EXTERN(rtNotifyCreate, rtError_t);
RTS_STUB_OUTBOUND_EXTERN(rtNotifyCreate, rtNotify_t , notify);

RTS_STUB_RETURN_EXTERN(rtNotifyWait, rtError_t);

RTS_STUB_RETURN_EXTERN(rtGetNotifyID, rtError_t);
RTS_STUB_OUTBOUND_EXTERN(rtNotifyCreate, uint32_t, notify_id);

RTS_STUB_RETURN_EXTERN(rtQueryFunctionRegistered, rtError_t);

RTS_STUB_RETURN_EXTERN(rtGetAicpuDeploy, rtError_t);
RTS_STUB_OUTBOUND_EXTERN(rtGetAicpuDeploy, rtAicpuDeployType_t, value);

RTS_STUB_RETURN_EXTERN(rtProfilerTraceEx, rtError_t);

RTS_STUB_RETURN_EXTERN(rtNpuGetFloatStatus, rtError_t);

RTS_STUB_RETURN_EXTERN(rtNpuClearFloatStatus, rtError_t);

RTS_STUB_RETURN_EXTERN(rtMalloc, rtError_t);
RTS_STUB_RETURN_EXTERN(rtMemcpy, rtError_t);
RTS_STUB_RETURN_EXTERN(rtsMemcpyBatch, rtError_t);
RTS_STUB_RETURN_EXTERN(rtDatadumpInfoLoad, rtError_t);

RTS_STUB_RETURN_EXTERN(rtSetDeviceV2, rtError_t);
RTS_STUB_RETURN_EXTERN(rtSetDie, rtError_t);
RTS_STUB_RETURN_EXTERN(rtDeviceReset, rtError_t);
RTS_STUB_RETURN_EXTERN(rtGetDeviceInfo, rtError_t);

#ifdef __cplusplus
}
#endif
#endif // __INC_LLT_RUNTIME_STUB_H
