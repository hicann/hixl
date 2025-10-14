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
#include "securec.h"
#include "mmpa/mmpa_api.h"
#include "runtime_stub.h"
#include "runtime/rt.h"
#include "runtime/base.h"
#include "runtime/rt_preload_task.h"
#include "external/runtime/rt_error_codes.h"
#include <iostream>

extern std::string g_runtime_stub_mock;
extern std::string g_runtime_stub_mock_v2;
std::string g_runtime_stub_mock = "";
std::string g_runtime_stub_mock_v2 = "";
static const int32_t END_OF_SEQUENCE = 507005;

static int32_t g_free_stream_num = 2048;
static int32_t g_free_event_num = 2048;
static int32_t g_cnt_rtStreamSynchronize_over_flow = 0;
static int32_t g_cnt_rtStreamSynchronize_fail = 0;
static uint32_t g_rt_model_id = 0;

#define ADD_STUB_RETURN_VALUE(FUNC, TYPE) std::vector<TYPE> g_Stub_##FUNC##_RETURN

#define GET_STUB_RETURN_VALUE(FUNC, TYPE, DEFAULT) ({   \
  TYPE result;                                          \
  if (!g_Stub_##FUNC##_RETURN.empty()) {                \
    result = g_Stub_##FUNC##_RETURN.back();             \
    g_Stub_##FUNC##_RETURN.pop_back();                  \
  } else {                                              \
    result = DEFAULT;                                   \
  }                                                     \
  result;                                               \
})

#define DEL_STUB_RETURN_VALUE(FUNC, TYPE)           \
do {                                                \
  extern std::vector<TYPE> g_Stub_##FUNC##_RETURN;  \
  g_Stub_##FUNC##_RETURN.clear();                   \
} while (0)


#define ADD_STUB_OUTBOUND_VALUE(FUNC, TYPE, NAME) std::vector<TYPE> g_Stub_##FUNC##_OUT_##NAME

#define GET_STUB_OUTBOUND_VALUE(FUNC, TYPE, NAME, DEFAULT) ({ \
  TYPE value;                                                 \
  if (!g_Stub_##FUNC##_OUT_##NAME.empty()) {                  \
    value = g_Stub_##FUNC##_OUT_##NAME.back();                \
    g_Stub_##FUNC##_OUT_##NAME.pop_back();                    \
  } else {                                                    \
    value = DEFAULT;                                          \
  }                                                           \
  value;                                                      \
})

#define DEL_STUB_OUTBOUND_VALUE(FUNC, TYPE, NAME)       \
do {                                                    \
  extern std::vector<TYPE> g_Stub_##FUNC##_OUT_##NAME;  \
  g_Stub_##FUNC##_OUT_##NAME.clear();                   \
} while (0)


namespace llm {
namespace {
struct MbufStub {
  explicit MbufStub(uint64_t size) {
    length = size;
    if (size > 0) {
      buffer = new uint8_t[size];
    }
    head.resize(1024, 0);
  }
  ~MbufStub() {
    delete []buffer;
  }
  std::vector<uint8_t> head;
  uint8_t *buffer = nullptr;
  uint64_t length = 0;
};

std::mutex mock_mbufs_mu_;
std::map<void *, std::shared_ptr<MbufStub>> mock_mbufs_;
std::map<int32_t, std::map<uint32_t, std::queue<void *>>> mem_queues_;
}  // namespace

std::shared_ptr<RuntimeStub> RuntimeStub::instance_;
std::mutex RuntimeStub::mutex_;
thread_local RuntimeStub* RuntimeStub::fake_instance_;
RuntimeStub *RuntimeStub::GetInstance() {
  const std::lock_guard<std::mutex> lock(mutex_);
  if(fake_instance_ != nullptr){
    return fake_instance_;
  }
  if (instance_ == nullptr) {
    instance_ = std::make_shared<RuntimeStub>();
  }
  return instance_.get();
}

void RuntimeStub::Install(RuntimeStub* instance){
  fake_instance_ = instance;
}

void RuntimeStub::UnInstall(RuntimeStub*){
  fake_instance_ = nullptr;
}

rtError_t RuntimeStub::rtStreamSynchronizeWithTimeout(rtStream_t stm, int32_t timeout) {
  const char * const kEnvRecordPath = "CONSTANT_FOLDING_PASS_9";
  char record_path[MMPA_MAX_PATH] = {};
  (void)mmGetEnv(kEnvRecordPath, &record_path[0], static_cast<uint32_t>(MMPA_MAX_PATH));
  if (std::string(&record_path[0]).find("mock_fail") != std::string::npos) {
    return -1;
  }
  const char * const kEnvPath = "END_OF_SEQUENCE";
  char env_path[MMPA_MAX_PATH] = {};
  (void)mmGetEnv(kEnvPath, &env_path[0], static_cast<uint32_t>(MMPA_MAX_PATH));
  if (std::string(&env_path[0]).find("end") != std::string::npos) {
    return END_OF_SEQUENCE;
  }

  const char * const kEnvPathWithTimeout = "WITH_TIMEOUT_END_OF_SEQUENCE";
  char end_path[MMPA_MAX_PATH] = {};
  (void)mmGetEnv(kEnvPathWithTimeout, &end_path[0], static_cast<uint32_t>(MMPA_MAX_PATH));
  if (std::string(&end_path[0]).find("end") != std::string::npos) {
    return END_OF_SEQUENCE;
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
  if (std::string(__FUNCTION__) == g_runtime_stub_mock) {
    return -1;
  }
  return RT_ERROR_NONE;
}

rtError_t RuntimeStub::rtMemcpy(void *dst, uint64_t dest_max, const void *src, uint64_t count, rtMemcpyKind_t kind) {
  const char * const kEnvRecordPath = "CONSTANT_FOLDING_PASS";
  char record_path[MMPA_MAX_PATH] = {};
  (void)mmGetEnv(kEnvRecordPath, &record_path[0], static_cast<uint32_t>(MMPA_MAX_PATH));
  if (std::string(&record_path[0]).find("mock_fail") != std::string::npos) {
    return -1;
  }
  const char *const kEnvRecordPath1 = "NPU_COLLECT_PATH_EXE";
  (void)mmGetEnv(kEnvRecordPath1, &record_path[0], static_cast<uint32_t>(MMPA_MAX_PATH));
  if (!std::string(&record_path[0]).empty()) {
    return RT_ERROR_NONE;
  }

  if (__FUNCTION__ == g_runtime_stub_mock) {
    return -1;
  }

  if (dst != nullptr && src != nullptr) {
    dest_max = std::min(dest_max, reserve_mem_size_);
    memcpy_s(dst, dest_max, src, count);
  }
  return RT_ERROR_NONE;
}

rtError_t RuntimeStub::rtMemcpyEx(void *dst, uint64_t dest_max, const void *src, uint64_t count, rtMemcpyKind_t kind) {
  return this->rtMemcpy(dst, dest_max, src, count, kind);
}

rtError_t RuntimeStub::rtMemcpyAsync(void *dst, uint64_t dest_max, const void *src, uint64_t count, rtMemcpyKind_t kind,
                                     rtStream_t stream) {
  const char *const kEnvRecordPath = "MOCK_MEMCPY_HUGE";
  char record_path[MMPA_MAX_PATH] = {};
  int32_t ret = mmGetEnv(kEnvRecordPath, &record_path[0], static_cast<uint32_t>(MMPA_MAX_PATH));
  if ((ret != EN_OK) || (strlen(record_path) == 0)) {
    if (dst != nullptr && src != nullptr) {
      dest_max = std::min(dest_max, reserve_mem_size_);
      memcpy_s(dst, dest_max, src, count);
    }
    return RT_ERROR_NONE;
  }
  size_t offset = 0U;
  size_t remain_size = count;
  do {
    size_t copy_size = (remain_size > SECUREC_MEM_MAX_LEN) ? SECUREC_MEM_MAX_LEN : remain_size;
    memcpy_s((dst + offset), copy_size, (src + offset), copy_size);
    offset += copy_size;
    remain_size -= copy_size;
  } while (remain_size > 0U);
  return RT_ERROR_NONE;
}

rtError_t RuntimeStub::rtMemcpyAsyncWithoutCheckKind(void *dst, uint64_t dest_max, const void *src, uint64_t count,
                                                     rtMemcpyKind_t kind, rtStream_t stream) {
  return this->rtMemcpyAsync(dst, dest_max, src, count, kind, stream);
}

rtError_t RuntimeStub::rtMemcpyAsyncWithCfgV2(void *dst, uint64_t dest_max, const void *src, uint64_t count,
                                              rtMemcpyKind_t kind, rtStream_t stm, const rtTaskCfgInfo_t *cfgInfo) {
  if (dst != nullptr && src != nullptr) {
    dest_max = std::min(dest_max, reserve_mem_size_);
    memcpy_s(dst, dest_max, src, count);
  }
  return RT_ERROR_NONE;
}

rtError_t RuntimeStub::rtMemcpyAsyncPtr(void *memcpyAddrInfo, uint64_t destMax, uint64_t count,
                                        rtMemcpyKind_t kind, rtStream_t stream, uint32_t qosCfg) {
  return RT_ERROR_NONE;
}

rtError_t RuntimeStub::rtsMemcpyBatch(void **dsts, void **srcs, size_t *sizes, size_t count,
    rtMemcpyBatchAttr *attrs, size_t *attrs_idxs, size_t num_attrs, size_t *fail_idx) {
  auto stub_return_value = GET_STUB_RETURN_VALUE(rtsMemcpyBatch, rtError_t, RT_ERROR_NONE);
  // 调用成功才计入统计次数
  if (stub_return_value == RT_ERROR_NONE) {
    input_mem_copy_batch_count_ = num_attrs;
  }
  return stub_return_value;
}

rtError_t RuntimeStub::rtStreamSwitchEx(void *ptr, rtCondition_t condition, void *value_ptr, rtStream_t true_stream,
                                        rtStream_t stream, rtSwitchDataType_t data_type) {
  return RT_ERROR_NONE;
}

rtError_t RuntimeStub::rtMalloc(void **dev_ptr, uint64_t size, rtMemType_t type, uint16_t moduleId) {
  const char * const kEnvRecordPath = "CONSTANT_FOLDING_PASS_2";
  char record_path[MMPA_MAX_PATH] = {};
  (void)mmGetEnv(kEnvRecordPath, &record_path[0], static_cast<uint32_t>(MMPA_MAX_PATH));
  if (std::string(&record_path[0]).find("mock_fail") != std::string::npos) {
    return -1;
  }

  const char *const kEnvHybridProfiling = "HYBRID_PROFILING_LEVEL";
  char record_path1[MMPA_MAX_PATH] = {};
  (void)mmGetEnv(kEnvHybridProfiling, &record_path1[0], static_cast<uint32_t>(MMPA_MAX_PATH));
  if (std::string(&record_path1[0]).find("1") != std::string::npos) {
    *dev_ptr = new uint8_t[size];
    memset_s(*dev_ptr, size, 0, size);
    return RT_ERROR_NONE;
  }
  if (std::string(__FUNCTION__) == g_runtime_stub_mock) {
    return -1;
  }

  if (size == 123) {
    return -1;
  }
  const char *const kEnvRecordPath_Huge = "MOCK_MEMCPY_HUGE";
  char record_path_Huge[MMPA_MAX_PATH] = {};
  int32_t ret = mmGetEnv(kEnvRecordPath_Huge, &record_path_Huge[0], static_cast<uint32_t>(MMPA_MAX_PATH));
  if ((ret == EN_OK) && (strlen(record_path_Huge) != 0)) {
    *dev_ptr = new uint8_t[size];
    memset_s(*dev_ptr, size, 0, size);
    return RT_ERROR_NONE;
  }
  if (size > INT32_MAX) {
    *dev_ptr = new uint8_t[1024U];
    return RT_ERROR_NONE;
  }
  *dev_ptr = new uint8_t[size];
  memset_s(*dev_ptr, size, 0, size);
  return RT_ERROR_NONE;
}

rtError_t RuntimeStub::rtFree(void *dev_ptr) {
  delete[](uint8_t *) dev_ptr;
  return RT_ERROR_NONE;
}

rtError_t RuntimeStub::rtEschedWaitEvent(int32_t device_id,
                                         uint32_t group_id,
                                         uint32_t thread_id,
                                         int32_t timeout,
                                         rtEschedEventSummary_t *event) {
  return RT_ERROR_NONE;
}

rtError_t RuntimeStub::rtMemGetInfoEx(rtMemInfoType_t memInfoType, size_t *free, size_t *total) {
  *free = 64UL * 1024UL * 1024UL;
  *total = 128UL * 1024UL * 1024UL;
  return RT_ERROR_NONE;
}

rtError_t RuntimeStub::rtMemGrpCacheAlloc(const char *name,
                                          int32_t devId,
                                          const rtMemGrpCacheAllocPara *para) {
  return RT_ERROR_NONE;
}

rtError_t RuntimeStub::rtRegTaskFailCallbackByModule(const char *moduleName,
                                                  rtTaskFailCallback callback) {
  return  RT_ERROR_NONE;
}

rtError_t RuntimeStub::rtMemQueueDeQueue(int32_t device, uint32_t qid, void **mbuf) {
  if (llm::mem_queues_[device][qid].empty()) {
    return 1;
  }
  *mbuf = llm::mem_queues_[device][qid].back();
  llm::mem_queues_[device][qid].pop();
  return 0;
}

rtError_t RuntimeStub::rtMemQueuePeek(int32_t device, uint32_t qid, size_t *bufLen, int32_t timeout) {
    return 0;
};
rtError_t RuntimeStub::rtMemQueueEnQueueBuff(int32_t device, uint32_t qid, rtMemQueueBuff_t *inBuf, int32_t timeout) {
    return 0;
};
rtError_t RuntimeStub::rtMemQueueDeQueueBuff(int32_t device, uint32_t qid, rtMemQueueBuff_t *outBuf, int32_t timeout) {
    return 0;
};

rtError_t RuntimeStub::rtMbufGetBuffAddr(rtMbufPtr_t mbuf, void **databuf) {
  *databuf = reinterpret_cast<llm::MbufStub *>(mbuf)->buffer;
  return 0;
}

rtError_t RuntimeStub::rtMbufGetBuffSize(rtMbufPtr_t mbuf, uint64_t *size) {
  *size = reinterpret_cast<llm::MbufStub *>(mbuf)->length;
  return 0;
}

rtError_t RuntimeStub::rtMbufGetPrivInfo(rtMbufPtr_t mbuf, void **priv, uint64_t *size) {
  if (priv != nullptr && mbuf != nullptr) {
    *priv = reinterpret_cast<llm::MbufStub *>(mbuf)->head.data();
    *size = 512;
  }
  return 0;
}

rtError_t RuntimeStub::rtMbufCopyBufRef(rtMbufPtr_t mbuf, rtMbufPtr_t *ref_mbuf) {
  *ref_mbuf = mbuf;
  return 0;
}

rtError_t RuntimeStub::rtMemQueueEnQueue(int32_t dev_id, uint32_t qid, void *mem_buf) {
  llm::mem_queues_[dev_id][qid].push(mem_buf);
  return 0;
}

rtError_t RuntimeStub::rtGeneralCtrl(uintptr_t *ctrl, uint32_t num, uint32_t type) {
  if (std::string(__FUNCTION__) == g_runtime_stub_mock) {
    return -1;
  }
  return RT_ERROR_NONE;
}

rtError_t RuntimeStub::rtBuffAlloc(uint64_t size, void **buff) {
  return RT_ERROR_NONE;
}

rtError_t RuntimeStub::rtMbufAlloc(rtMbufPtr_t *mbuf, uint64_t size) {
  if (mbuf != nullptr) {
    auto mock_mbuf = std::make_shared<llm::MbufStub>(size);
    *mbuf = mock_mbuf.get();
    std::lock_guard<std::mutex> lk(mock_mbufs_mu_);
    mock_mbufs_[*mbuf] = mock_mbuf;
  }
  return 0;
}

rtError_t RuntimeStub::rtMbufFree(rtMbufPtr_t mbuf) {
  if (mbuf != nullptr) {
    std::lock_guard<std::mutex> lk(mock_mbufs_mu_);
    auto it = mock_mbufs_.find(mbuf);
    if (it != mock_mbufs_.end()) {
      mock_mbufs_.erase(it);
    }
  }
  return RT_ERROR_NONE;
}

rtError_t RuntimeStub::rtGetSocVersion(char *version, const uint32_t maxLen) {
  (void)strcpy_s(version, maxLen, "Ascend910");
  return RT_ERROR_NONE;
}

rtError_t RuntimeStub::rtDeviceReset(int32_t device) {
  return RT_ERROR_NONE;
}

rtError_t RuntimeStub::rtModelCheckCompatibility(const char_t *OmSoCVersion, const char_t *OMArchVersion) {
  return RT_ERROR_NONE;
}
rtError_t RuntimeStub::rtSetTaskTag(const char *taskTag) {
  return RT_ERROR_NONE;
}

rtError_t RuntimeStub::rtModelCreate(rtModel_t *model, uint32_t flag) {
  return RT_ERROR_NONE;
}

rtError_t RuntimeStub::rtModelBindStream(rtModel_t model, rtStream_t stream, uint32_t flag) {
  const std::lock_guard<std::mutex> lock(mtx_);
  model_bind_streams_.emplace_back(stream);
  return RT_ERROR_NONE;
}

rtError_t RuntimeStub::rtModelUnbindStream(rtModel_t model, rtStream_t stream) {
  const std::lock_guard<std::mutex> lock(mtx_);
  model_unbind_streams_.emplace_back(stream);
  return RT_ERROR_NONE;
}

rtError_t RuntimeStub::rtModelGetTaskId(void *handle, uint32_t *task_id, uint32_t *stream_id) {
  return RT_ERROR_NONE;
}

rtError_t RuntimeStub::rtStreamWaitEvent(rtStream_t stream, rtEvent_t event) {
  return GET_STUB_RETURN_VALUE(rtStreamWaitEvent, rtError_t, RT_ERROR_NONE);
}

rtError_t RuntimeStub::rtEventRecord(rtEvent_t event, rtStream_t stream) {
  return GET_STUB_RETURN_VALUE(rtEventRecord, rtError_t, RT_ERROR_NONE);
}

rtError_t RuntimeStub::rtStreamWaitEventWithTimeout(rtStream_t stream, rtEvent_t event, uint32_t timeout) {
  return GET_STUB_RETURN_VALUE(rtStreamWaitEventWithTimeout, rtError_t, RT_ERROR_NONE);
}
rtError_t RuntimeStub::rtStreamCreate(rtStream_t *stream, int32_t priority) {
  const char * const kEnvRecordPath = "CONSTANT_FOLDING_PASS_4";
  char record_path[MMPA_MAX_PATH] = {};
  (void)mmGetEnv(kEnvRecordPath, &record_path[0], static_cast<uint32_t>(MMPA_MAX_PATH));
  if (std::string(&record_path[0]).find("mock_fail") != std::string::npos) {
    return -1;
  }

  if(g_free_stream_num <= 0) {
    return -1;
  }
  g_free_stream_num--;
  *stream = new uint32_t;
  return RT_ERROR_NONE;
}
rtError_t RuntimeStub::rtStreamCreateWithFlags(rtStream_t *stream, int32_t priority, uint32_t flags) {
  if(g_free_stream_num <= 0) {
    return -1;
  }
  g_free_stream_num--;
  *stream = new uint32_t;
  return RT_ERROR_NONE;
}

rtError_t RuntimeStub::rtStreamDestroy(rtStream_t stream) {
  if (stream != nullptr) {
    delete (uint32_t *)stream;
  }
  g_free_stream_num++;
  return RT_ERROR_NONE;
}

rtError_t RuntimeStub::rtStreamSetMode(rtStream_t stm, const uint64_t stmMode) {
  (void) stm;
  (void) stmMode;
  return RT_ERROR_NONE;
}

rtError_t RuntimeStub::rtStreamDestroyForce(rtStream_t stream) {
  if (stream != nullptr) {
    delete (uint32_t *)stream;
  }
  g_free_stream_num++;
  return RT_ERROR_NONE;
}

rtError_t RuntimeStub::rtGetAvailStreamNum(uint32_t streamType, uint32_t *const streamCount) {
  const char *const kEnvRecordPath = "MOCK_AVAIL_STREAM_NUM";
  char record_path[8] = {};
  int32_t ret = mmGetEnv(kEnvRecordPath, &record_path[0], static_cast<uint32_t>(8));
  if ((ret != EN_OK) || (strlen(record_path) == 0)) {
    *streamCount = g_free_stream_num;
    return RT_ERROR_NONE;
  }
  try {
    *streamCount = std::stoi(std::string(record_path));
    return RT_ERROR_NONE;
  } catch (...) {
    return 1; // SOME ERROR
  }
  *streamCount = g_free_stream_num;
  return RT_ERROR_NONE;
}

rtError_t RuntimeStub::rtCtxGetCurrentDefaultStream(rtStream_t *stream) {
  if (__FUNCTION__ == g_runtime_stub_mock) {
    return -1;
  }
  uintptr_t x = 1;
  *stream = (rtStream_t *)x;
  return  RT_ERROR_NONE;
}

rtError_t RuntimeStub::rtMallocPhysical(rtDrvMemHandle* handle, size_t size, rtDrvMemProp_t* prop, uint64_t flags) {
  if (std::string(__FUNCTION__) == g_runtime_stub_mock) {
    return -1;
  }
  *handle = (rtDrvMemHandle) new uint8_t[8];
  return RT_ERROR_NONE;
}

rtError_t RuntimeStub::rtReserveMemAddress(void** devPtr, size_t size, size_t alignment, void *devAddr, uint64_t flags) {
  if (size < 200UL * 1024UL *1024UL) {
    *devPtr = new uint8_t[size];
    reserve_mem_size_ = size;
  } else {
    *devPtr = new uint8_t[reserve_mem_size_];
  }
  memset_s(*devPtr, reserve_mem_size_, 0, reserve_mem_size_);
  return RT_ERROR_NONE;
}

rtError_t RuntimeStub::rtGetDevice(int32_t *deviceId) {
  if (__FUNCTION__ == g_runtime_stub_mock) {
    return -1;
  }
  *deviceId = 0;
  return RT_ERROR_NONE;
}

} // namespace llm

#ifdef __cplusplus
extern "C" {
#endif
static int32_t rtGetDevice_is_mock_new_way = 0;
void SetMockRtGetDeviceWay(int32_t is_mock_new_way) {
  rtGetDevice_is_mock_new_way = is_mock_new_way;
}

#define EVENT_LENTH 10
#define NOTIFY_LENTH 10

void rtStubTearDown() {
  SetMockRtGetDeviceWay(0);
  DEL_STUB_RETURN_VALUE(rtGetDevice, rtError_t);
  DEL_STUB_RETURN_VALUE(rtGetDeviceCapability, rtError_t);
  DEL_STUB_RETURN_VALUE(rtStreamWaitEvent, rtError_t);
  DEL_STUB_RETURN_VALUE(rtStreamWaitEventWithTimeout, rtError_t);
  DEL_STUB_RETURN_VALUE(rtEventReset, rtError_t);
  DEL_STUB_RETURN_VALUE(rtEventRecord, rtError_t);
  DEL_STUB_RETURN_VALUE(rtEventCreate, rtError_t);
  DEL_STUB_RETURN_VALUE(rtGetEventID, rtError_t);

  DEL_STUB_RETURN_VALUE(rtNotifyCreate, rtError_t);
  DEL_STUB_RETURN_VALUE(rtNotifyWait, rtError_t);
  DEL_STUB_RETURN_VALUE(rtGetNotifyID, rtError_t);
  DEL_STUB_RETURN_VALUE(rtQueryFunctionRegistered, rtError_t);

  DEL_STUB_RETURN_VALUE(rtMalloc, rtError_t);
  DEL_STUB_RETURN_VALUE(rtMemcpy, rtError_t);
  DEL_STUB_RETURN_VALUE(rtsMemcpyBatch, rtError_t);
  DEL_STUB_RETURN_VALUE(rtDatadumpInfoLoad, rtError_t);

  DEL_STUB_RETURN_VALUE(rtSetDie, rtError_t);
  DEL_STUB_RETURN_VALUE(rtSetDeviceV2, rtError_t);
  DEL_STUB_RETURN_VALUE(rtDeviceReset, rtError_t);
  DEL_STUB_RETURN_VALUE(rtGetDeviceInfo, rtError_t);
}

ADD_STUB_RETURN_VALUE(rtStreamWaitEvent, rtError_t);
ADD_STUB_RETURN_VALUE(rtEventRecord, rtError_t);

ADD_STUB_RETURN_VALUE(rtGetDevice, rtError_t);
ADD_STUB_RETURN_VALUE(rtGetDeviceInfo, rtError_t);
rtError_t rtGetDevice(int32_t *device) {
  if (rtGetDevice_is_mock_new_way == 0) {
    if (__FUNCTION__ == g_runtime_stub_mock) {
      return -1;
    }
    *device = 0;
    return GET_STUB_RETURN_VALUE(rtGetDevice, rtError_t, RT_ERROR_NONE);
  }

  return llm::RuntimeStub::GetInstance()->rtGetDevice(device);
}

ADD_STUB_RETURN_VALUE(rtGetDeviceCapability, rtError_t);
ADD_STUB_OUTBOUND_VALUE(rtGetDeviceCapability, int32_t, value);
rtError_t rtGetDeviceCapability(int32_t device, int32_t moduleType, int32_t featureType, int32_t *value) {
  *value = GET_STUB_OUTBOUND_VALUE(rtGetDeviceCapability, int32_t, value, RT_AICPU_BLOCKING_OP_SUPPORT);
  return GET_STUB_RETURN_VALUE(rtGetDeviceCapability, rtError_t, RT_ERROR_NONE);
}


rtError_t rtStreamWaitEvent(rtStream_t stream, rtEvent_t event) {
  return llm::RuntimeStub::GetInstance()->rtStreamWaitEvent(stream, event);
}

ADD_STUB_RETURN_VALUE(rtStreamWaitEventWithTimeout, rtError_t);
rtError_t rtStreamWaitEventWithTimeout(rtStream_t stream, rtEvent_t event, uint32_t timeout) {
  return llm::RuntimeStub::GetInstance()->rtStreamWaitEventWithTimeout(stream, event, timeout);
}

ADD_STUB_RETURN_VALUE(rtEventReset, rtError_t);
rtError_t rtEventReset(rtEvent_t event, rtStream_t stream) {
  return GET_STUB_RETURN_VALUE(rtEventReset, rtError_t, RT_ERROR_NONE);
}

RTS_API rtError_t rtEventQueryStatus(rtEvent_t evt, rtEventStatus_t *status) {
  return llm::RuntimeStub::GetInstance()->rtEventQueryStatus(evt, status);
}

ADD_STUB_RETURN_VALUE(rtEventCreate, rtError_t);
rtError_t rtEventCreate(rtEvent_t *event) {
  if (__FUNCTION__ == g_runtime_stub_mock) {
    return -1;
  }

  if(g_free_event_num <= 0) {
    return -1;
  }
  g_free_event_num--;
  *event = new int[EVENT_LENTH];
  return GET_STUB_RETURN_VALUE(rtEventCreate, rtError_t, RT_ERROR_NONE);
}

ADD_STUB_RETURN_VALUE(rtGetEventID, rtError_t);
rtError_t rtGetEventID(rtEvent_t event, uint32_t *event_id) {
  if (__FUNCTION__ == g_runtime_stub_mock) {
    return -1;
  }
  *event_id = 0;
  return GET_STUB_RETURN_VALUE(rtEventCreate, rtError_t, RT_ERROR_NONE);
}

ADD_STUB_RETURN_VALUE(rtGetNotifyID, rtError_t);
rtError_t rtGetNotifyID(rtNotify_t notify, uint32_t *notify_id) {
  if (__FUNCTION__ == g_runtime_stub_mock) {
    return -1;
  }
  *notify_id = 0;
  return GET_STUB_RETURN_VALUE(rtNotifyCreate, rtError_t, RT_ERROR_NONE);
}

ADD_STUB_RETURN_VALUE(rtQueryFunctionRegistered, rtError_t);
rtError_t rtQueryFunctionRegistered(const char *stub_name) {
  return GET_STUB_RETURN_VALUE(rtQueryFunctionRegistered, rtError_t, RT_ERROR_NONE);
}

rtError_t rtCtxSetCurrent(rtContext_t ctx)
{
  const char * const kEnvRecordPath = "SET_TRANS_VAR_DATA";
  char record_path[MMPA_MAX_PATH] = {};
  (void)mmGetEnv(kEnvRecordPath, &record_path[0], static_cast<uint32_t>(MMPA_MAX_PATH));

  if (std::string(&record_path[0]).find("mock_fail") != std::string::npos) {
    return -1;
  }
  return RT_ERROR_NONE;
}

rtError_t rtGetStreamId(rtStream_t stream, int32_t *stream_id) {
  *stream_id = 0;
  return RT_ERROR_NONE;
}

rtError_t rtCtxGetCurrent(rtContext_t *ctx) {
  if (__FUNCTION__ == g_runtime_stub_mock) {
    return -1;
  }
  uintptr_t x = 1;
  *ctx = (rtContext_t *)x;
  return RT_ERROR_NONE;
}

rtError_t rtCtxSetDryRun(rtContext_t ctx, rtDryRunFlag_t enable, uint32_t flag) { return RT_ERROR_NONE; }

rtError_t rtEventGetTimeStamp(uint64_t *time, rtEvent_t event) {
  *time = 12345;
  return RT_ERROR_NONE;
}

rtError_t rtEventCreateWithFlag(rtEvent_t *event, uint32_t flag) {
  return rtEventCreate(event);
}

rtError_t rtEventCreateExWithFlag(rtEvent_t *event, uint32_t flag) {
  return rtEventCreate(event);
}

rtError_t rtSupportModelStreamReuse(bool *bSupport) {
  if (std::string(__FUNCTION__) == g_runtime_stub_mock) {
    *bSupport = true;
  }
  return llm::RuntimeStub::GetInstance()->rtSupportModelStreamReuse(bSupport);
}

rtError_t rtNotifyCreateWithFlag(int32_t deviceId, rtNotify_t *notify, uint32_t flag) {
  return rtNotifyCreate(deviceId, notify);
}

rtError_t rtEventRecord(rtEvent_t event, rtStream_t stream) {
  return llm::RuntimeStub::GetInstance()->rtEventRecord(event, stream);
}

rtError_t rtEventSynchronize(rtEvent_t event) { return RT_ERROR_NONE; }

rtError_t rtEventSynchronizeWithTimeout(rtEvent_t evt, const int32_t timeout) { return RT_ERROR_NONE; }

rtError_t rtEventDestroy(rtEvent_t event) {
  g_free_event_num++;
  delete[](int *) event;
  return RT_ERROR_NONE;
}

rtError_t rtEventDestroySync(rtEvent_t event) {
  g_free_event_num++;
  delete[](int *) event;
  return RT_ERROR_NONE;
}

ADD_STUB_RETURN_VALUE(rtNotifyCreate, rtError_t);
rtError_t rtNotifyCreate(int32_t deviceId, rtNotify_t *notify) {
  if (__FUNCTION__ == g_runtime_stub_mock) {
    return -1;
  }
  *notify = new int[NOTIFY_LENTH];
  return GET_STUB_RETURN_VALUE(rtNotifyCreate, rtError_t, RT_ERROR_NONE);
}

ADD_STUB_RETURN_VALUE(rtNotifyWait, rtError_t);
rtError_t rtNotifyWait(rtNotify_t notify, rtStream_t stm) {
  return GET_STUB_RETURN_VALUE(rtNotifyWait, rtError_t, RT_ERROR_NONE);
}

rtError_t rtNotifyRecord(rtNotify_t notify, rtStream_t stream) {
  return RT_ERROR_NONE;
}

rtError_t rtNotifyDestroy(rtNotify_t notify)
{
  if (notify != nullptr) {
    delete[](int *) notify;
    notify = nullptr;
  }
  return RT_ERROR_NONE;
}

rtError_t rtMemset(void *dev_ptr, uint64_t dest_max, uint32_t value, uint64_t count) {
  if (dest_max == 321) {
    return -1;
  }
  return RT_ERROR_NONE;
}

rtError_t rtMemsetAsync(void *dev_ptr, uint64_t dest_max, uint32_t value, uint64_t count, rtStream_t stream) {
  return RT_ERROR_NONE;
}

ADD_STUB_RETURN_VALUE(rtMalloc, rtError_t);
rtError_t rtMalloc(void **dev_ptr, uint64_t size, rtMemType_t type, uint16_t moduleId) {
  return GET_STUB_RETURN_VALUE(rtMalloc, rtError_t,
    llm::RuntimeStub::GetInstance()->rtMalloc(dev_ptr, size, type, moduleId));
}

rtError_t rtFree(void *dev_ptr) {
  return llm::RuntimeStub::GetInstance()->rtFree(dev_ptr);
}

rtError_t rtMallocHost(void **host_ptr, uint64_t size, uint16_t moduleId) {
  *host_ptr = new uint8_t[size];
  return RT_ERROR_NONE;
}

rtError_t rtFreeHost(void *host_ptr) {
  delete[](uint8_t *) host_ptr;
  return RT_ERROR_NONE;
}

rtError_t rtStreamCreate(rtStream_t *stream, int32_t priority) {
  return llm::RuntimeStub::GetInstance()->rtStreamCreate(stream, priority);
}

rtError_t rtStreamDestroy(rtStream_t stream) {
  return llm::RuntimeStub::GetInstance()->rtStreamDestroy(stream);
}

rtError_t rtStreamDestroyForce(rtStream_t stream) {
  return llm::RuntimeStub::GetInstance()->rtStreamDestroyForce(stream);
}

rtError_t rtModelAbort(rtModel_t model) { return RT_ERROR_NONE; }

ADD_STUB_RETURN_VALUE(rtSetDie, rtError_t);
rtError_t rtSetDie(int32_t die) {
  return GET_STUB_RETURN_VALUE(rtSetDie, rtError_t, RT_ERROR_NONE);
}

rtError_t rtSetDevice(int32_t device) { return RT_ERROR_NONE; }

ADD_STUB_RETURN_VALUE(rtSetDeviceV2, rtError_t);
rtError_t rtSetDeviceV2(int32_t device, rtDeviceMode deviceMode) {
  return GET_STUB_RETURN_VALUE(rtSetDeviceV2, rtError_t, RT_ERROR_NONE);
}

rtError_t rtGetDeviceIndexByPhyId(uint32_t phyId, uint32_t *devIndex) {
  return RT_ERROR_NONE;
}
rtError_t rtGetDevicePhyIdByIndex(uint32_t devIndex, uint32_t *phyId) {
  *phyId = devIndex;
  return RT_ERROR_NONE;
}

rtError_t rtStreamSynchronize(rtStream_t stream) {
  const char * const kEnvRecordPath = "CONSTANT_FOLDING_PASS_9";
  char record_path[MMPA_MAX_PATH] = {};
  (void)mmGetEnv(kEnvRecordPath, &record_path[0], static_cast<uint32_t>(MMPA_MAX_PATH));
  if (std::string(&record_path[0]).find("mock_fail") != std::string::npos) {
    return -1;
  }

  const char * const kEnvPath = "END_OF_SEQUENCE";
  char env_path[MMPA_MAX_PATH] = {};
  (void)mmGetEnv(kEnvPath, &env_path[0], static_cast<uint32_t>(MMPA_MAX_PATH));
  if (std::string(&env_path[0]).find("end") != std::string::npos) {
    return END_OF_SEQUENCE;
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
      return -1;
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

  return RT_ERROR_NONE;
}

rtError_t rtStreamSynchronizeWithTimeout(rtStream_t stm, int32_t timeout) {
  return llm::RuntimeStub::GetInstance()->rtStreamSynchronizeWithTimeout(stm, timeout);
}

ADD_STUB_RETURN_VALUE(rtMemcpy, rtError_t);
rtError_t rtMemcpy(void *dst, uint64_t dest_max, const void *src, uint64_t count, rtMemcpyKind_t kind) {
  return GET_STUB_RETURN_VALUE(rtMemcpy, rtError_t,
    llm::RuntimeStub::GetInstance()->rtMemcpy(dst, dest_max, src, count, kind));
}

rtError_t rtCmoAddrTaskLaunch(void *cmoAddrInfo, uint64_t destMax, rtCmoOpCode_t cmoOpCode,
                              rtStream_t stm, uint32_t flag) {
  return RT_ERROR_NONE;
}

rtError_t rtMemcpyEx(void *dst, uint64_t dest_max, const void *src, uint64_t count, rtMemcpyKind_t kind) {
  return rtMemcpy(dst, dest_max, src, count, kind);
}

rtError_t rtMemQueueQueryInfo(int32_t dev_id, uint32_t qid, rtMemQueueInfo_t *que_info) {
  return RT_ERROR_NONE;
}

rtError_t rtMemcpyAsync(void *dst, uint64_t dest_max, const void *src, uint64_t count, rtMemcpyKind_t kind,
                        rtStream_t stream) {
  if (std::string(__FUNCTION__) == g_runtime_stub_mock) {
    return -1;
  }
  return llm::RuntimeStub::GetInstance()->rtMemcpyAsync(dst, dest_max, src, count, kind, stream);
}

rtError_t rtMemcpyAsyncWithoutCheckKind(void *dst, uint64_t dest_max, const void *src, uint64_t count,
                                        rtMemcpyKind_t kind, rtStream_t stream) {
  return rtMemcpyAsync(dst, dest_max, src, count, kind, stream);
}

rtError_t rtMemcpyAsyncWithCfgV2(void *dst, uint64_t dest_max, const void *src, uint64_t count,
                                 rtMemcpyKind_t kind, rtStream_t stm, const rtTaskCfgInfo_t *cfgInfo) {
  if (std::string(__FUNCTION__) == g_runtime_stub_mock) {
    return -1;
  }
  return llm::RuntimeStub::GetInstance()->rtMemcpyAsyncWithCfgV2(dst, dest_max, src, count, kind, stm, cfgInfo);
}

rtError_t rtMemcpyAsyncPtr(void *memcpyAddrInfo, uint64_t destMax, uint64_t count,
                           rtMemcpyKind_t kind, rtStream_t stream, uint32_t qosCfg) {
  if (std::string(__FUNCTION__) == g_runtime_stub_mock) {
    return -1;
  }
  return llm::RuntimeStub::GetInstance()->rtMemcpyAsyncPtr(memcpyAddrInfo, destMax, count, kind, stream, qosCfg);
}

ADD_STUB_RETURN_VALUE(rtsMemcpyBatch, rtError_t);
rtError_t rtsMemcpyBatch(void **dsts, void **srcs, size_t *sizes, size_t count,
    rtMemcpyBatchAttr *attrs, size_t *attrsIdxs, size_t numAttrs, size_t *failIdx) {
  if (std::string(__FUNCTION__) == g_runtime_stub_mock) {
    return -1;
  }
  return llm::RuntimeStub::GetInstance()->rtsMemcpyBatch(dsts, srcs, sizes, count, attrs, attrsIdxs, numAttrs, failIdx);
}

rtError_t rtMemcpyHostTask(void *dst, uint64_t dest_max, const void *src, uint64_t count, rtMemcpyKind_t kind,
                        rtStream_t stream) {
  if (std::string(__FUNCTION__) == g_runtime_stub_mock) {
    return -1;
  }
  return llm::RuntimeStub::GetInstance()->rtMemcpyAsync(dst, dest_max, src, count, kind, stream);
}

rtError_t rtSetTSDevice(uint32_t tsId) {
  return RT_ERROR_NONE;
}

rtError_t rtGetDeviceCount(int32_t *count) {
  return llm::RuntimeStub::GetInstance()->rtGetDeviceCount(count);
}

rtError_t rtDeviceGetBareTgid(uint32_t *pid) {
  *pid = getpid();
  return RT_ERROR_NONE;
}

rtError_t rtBindHostPid(rtBindHostpidInfo info) {
  return RT_ERROR_NONE;
}

rtError_t rtGetDeviceInfo(uint32_t device_id, int32_t module_type, int32_t info_type, int64_t *val) {
  *val = 8;
  return GET_STUB_RETURN_VALUE(rtGetDeviceInfo, rtError_t, RT_ERROR_NONE);
}

ADD_STUB_RETURN_VALUE(rtDeviceReset, rtError_t);
rtError_t rtDeviceReset(int32_t device) {
  return GET_STUB_RETURN_VALUE(rtDeviceReset, rtError_t, RT_ERROR_NONE);
}

rtError_t rtGetVisibleDeviceIdByLogicDeviceId(const int32_t logicDeviceId, int32_t * const visibleDeviceId) {
  return RT_ERROR_NONE;
}

rtError_t rtEventElapsedTime(float *time, rtEvent_t start, rtEvent_t end) {
  *time = 10.0f;
  return RT_ERROR_NONE;
}

rtError_t rtFunctionRegister(void *bin_handle, const void *stub_func, const char *stub_name, const void *dev_func,
                             uint32_t func_mode) {
  if (reinterpret_cast<uintptr_t>(bin_handle) == 99) {
    return -1;
  }
  if (stub_name != nullptr && stub_name[0] == 'Z') {
    return -1;
  }
  return RT_ERROR_NONE;
}

rtError_t rtDevBinaryRegister(const rtDevBinary_t *bin, void **handle) {
  return llm::RuntimeStub::GetInstance()->rtDevBinaryRegister(bin, handle);
}

rtError_t rtRegisterAllKernel(const rtDevBinary_t *bin, void **handle) {
  return llm::RuntimeStub::GetInstance()->rtRegisterAllKernel(bin, handle);
}

rtError_t rtKernelConfigTransArg(const void *ptr, uint64_t size, uint32_t flag, void **arg) { return RT_ERROR_NONE; }
rtError_t rtKernelLaunchWithHandle(void *handle, const uint64_t tilingkey, uint32_t blockDim, rtArgsEx_t *args,
                                   rtSmDesc_t *smDesc, rtStream_t stream, const void *kernelInfo) {
  if (blockDim == 99) {
    return -1;
  }
  return llm::RuntimeStub::GetInstance()->rtKernelLaunchWithHandle(handle, tilingkey, blockDim, args, smDesc, stream, kernelInfo);
}

rtError_t rtKernelLaunchWithHandleV2(void *hdl, const uint64_t tilingKey, uint32_t blockDim, rtArgsEx_t *argsInfo,
                                     rtSmDesc_t *smDesc, rtStream_t stm, const rtTaskCfgInfo_t *cfgInfo)  {
  if (blockDim == 99) {
    return -1;
  }
  return llm::RuntimeStub::GetInstance()->rtKernelLaunchWithHandleV2(
          hdl, tilingKey, blockDim, argsInfo, smDesc, stm, cfgInfo);
}

rtError_t rtVectorCoreKernelLaunchWithHandle(void *hdl, const uint64_t tilingKey, uint32_t blockDim,
                                             rtArgsEx_t *argsInfo, rtSmDesc_t *smDesc, rtStream_t stm,
                                             const rtTaskCfgInfo_t *cfgInfo) {
  if (blockDim == 99) {
    return -1;
  }
  return llm::RuntimeStub::GetInstance()->rtVectorCoreKernelLaunchWithHandle(hdl, tilingKey, blockDim, argsInfo, smDesc,
                                                                            stm, cfgInfo);
}

rtError_t rtKernelLaunch(const void *stub_func, uint32_t block_dim, void *args, uint32_t args_size, rtSmDesc_t *sm_desc,
                         rtStream_t stream) {
  if (block_dim == 99) {
    return -1;
  }
  return llm::RuntimeStub::GetInstance()->rtKernelLaunch(stub_func, block_dim, args, args_size, sm_desc, stream);
}

rtError_t rtKernelLaunchWithFlag(const void *stubFunc, uint32_t blockDim, rtArgsEx_t *argsInfo,
                                 rtSmDesc_t *smDesc, rtStream_t stream, uint32_t flag) {
  return llm::RuntimeStub::GetInstance()->rtKernelLaunchWithFlag(stubFunc, blockDim, argsInfo, smDesc, stream, flag);
}

rtError_t rtKernelLaunchWithFlagV2(const void *stubFunc, uint32_t blockDim, rtArgsEx_t *argsInfo,
                                   rtSmDesc_t *smDesc, rtStream_t stm, uint32_t flags,
                                   const rtTaskCfgInfo_t *cfgInfo) {
  return llm::RuntimeStub::GetInstance()->rtKernelLaunchWithFlagV2(
          stubFunc, blockDim, argsInfo, smDesc, stm, flags, cfgInfo);
}

rtError_t rtVectorCoreKernelLaunch(const void *stubFunc, uint32_t blockDim, rtArgsEx_t *argsInfo, rtSmDesc_t *smDesc,
                                   rtStream_t stm, uint32_t flags, const rtTaskCfgInfo_t *cfgInfo) {
  return llm::RuntimeStub::GetInstance()->rtVectorCoreKernelLaunch(stubFunc, blockDim, argsInfo, smDesc, stm, flags,
                                                                  cfgInfo);
}

rtError_t rtSetupArgument(const void *arg, uint32_t size, uint32_t offset) { return RT_ERROR_NONE; }
rtError_t rtLaunch(const void *stub_func) { return RT_ERROR_NONE; }
rtError_t rtDevBinaryUnRegister(void *handle) { return RT_ERROR_NONE; }
rtError_t rtConfigureCall(uint32_t num_blocks, rtSmDesc_t *sm_desc, rtStream_t stream) { return RT_ERROR_NONE; }

rtError_t rtSetProfDir(char *prof_dir) { return RT_ERROR_NONE; }

rtError_t rtSetProfDirEx(const char *profDir, const char *address, const char *jobCtx) { return RT_ERROR_NONE; }

rtError_t rtAiCoreMemorySizes(rtAiCoreMemorySize_t *aicore_memory_size) { return RT_ERROR_NONE; }

rtError_t rtSetKernelReportCallback(rtKernelReportCallback callback) {
  rtKernelInfo rt_kernel_info = {0};
  rt_kernel_info.arg_size = 12;
  rt_kernel_info.task_offset = 100;
  rt_kernel_info.arg = (void *)100;
  rt_kernel_info.module_addr = (void *)100;
  rt_kernel_info.module_size = 100;

  rtStream_t stream = nullptr;
  callback(stream, &rt_kernel_info);
  return RT_ERROR_NONE;
}

rtError_t rtMemAdvise(void *ptr, uint64_t size, uint32_t advise) { return RT_ERROR_NONE; }

/// @ingroup rt_kernel
/// @brief start fusion kernels.
/// @param [in] stream   stream for fusion kernels
/// @return RT_ERROR_NONE for ok, errno for failed
rtError_t rtKernelFusionStart(rtStream_t stream) { return RT_ERROR_NONE; }

/// @ingroup rt_kernel
/// @brief end fusion kernels.
/// @param [in] stream   stream for fusion kernels
/// @return RT_ERROR_NONE for ok, errno for failed
rtError_t rtKernelFusionEnd(rtStream_t stream) { return RT_ERROR_NONE; }
rtError_t rtMemGetInfo(size_t *free, size_t *total) {
  *free = 64UL * 1024UL * 1024UL;
  *total = 128UL * 1024UL * 1024UL;
  return RT_ERROR_NONE;
}

rtError_t rtMemGetInfoEx(rtMemInfoType_t memInfoType, size_t *free, size_t *total) {
  *free = 64UL * 1024UL * 1024UL;
  *total = 128UL * 1024UL * 1024UL;
  return llm::RuntimeStub::GetInstance()->rtMemGetInfoEx(memInfoType, free, total);
}

rtError_t rtMemAllocManaged(void **ptr, uint64_t size, uint32_t flag, uint16_t moduleId) {
  *ptr = malloc(size);
  return RT_ERROR_NONE;
}

rtError_t rtMemFreeManaged(void *ptr) {
  free(ptr);
  return RT_ERROR_NONE;
}

rtError_t rtMetadataRegister(void *handle, const char *meta_data) {
  if (reinterpret_cast<uintptr_t>(handle) == 99) {
    return -1;
  }
  return RT_ERROR_NONE;
}
rtError_t rtSetTaskGenCallback(rtTaskGenCallback callback) { return RT_ERROR_NONE; }

rtError_t rtModelCreate(rtModel_t *model, uint32_t flag) {
  const char * const kEnvRecordPath = "CONSTANT_FOLDING_PASS_3";
  char record_path[MMPA_MAX_PATH] = {};
  (void)mmGetEnv(kEnvRecordPath, &record_path[0], static_cast<uint32_t>(MMPA_MAX_PATH));

  if (std::string(&record_path[0]).find("mock_fail") != std::string::npos) {
    return -1;
  }

  return llm::RuntimeStub::GetInstance()->rtModelCreate(model, flag);
}

rtError_t rtSetModelName(rtModel_t model, const char_t *mdlName) {
    return RT_ERROR_NONE;
}

rtError_t rtModelDestroy(rtModel_t model) {
  uint32_t *stub = static_cast<uint32_t *>(model);
  delete stub;
  return RT_ERROR_NONE;
}

rtError_t rtModelBindStream(rtModel_t model, rtStream_t stream, uint32_t flag) {
  const char * const kEnvRecordPath = "CONSTANT_FOLDING_PASS_5";
  char record_path[MMPA_MAX_PATH] = {};
  (void)mmGetEnv(kEnvRecordPath, &record_path[0], static_cast<uint32_t>(MMPA_MAX_PATH));

  if (std::string(&record_path[0]).find("mock_fail") != std::string::npos) {
    return -1;
  }

  return llm::RuntimeStub::GetInstance()->rtModelBindStream(model, stream, flag);
}

rtError_t rtModelUnbindStream(rtModel_t model, rtStream_t stream) {
  return llm::RuntimeStub::GetInstance()->rtModelUnbindStream(model, stream);
}
rtError_t rtModelExecute(rtModel_t model, rtStream_t stream, uint32_t flag) {
  const char * const kEnvRecordPath = "CONSTANT_FOLDING_PASS_8";
  char record_path[MMPA_MAX_PATH] = {};
  (void)mmGetEnv(kEnvRecordPath, &record_path[0], static_cast<uint32_t>(MMPA_MAX_PATH));

  if (std::string(&record_path[0]).find("mock_fail") != std::string::npos) {
    return -1;
  }
  return llm::RuntimeStub::GetInstance()->rtModelExecute(model, stream, flag);
}

rtError_t rtModelExecuteSync(rtModel_t model, rtStream_t stream, uint32_t flag, int32_t timeout) {
  const char * const kEnvRecordPath = "CONSTANT_FOLDING_PASS_8";
  char record_path[MMPA_MAX_PATH] = {};
  (void)mmGetEnv(kEnvRecordPath, &record_path[0], static_cast<uint32_t>(MMPA_MAX_PATH));

  if (std::string(&record_path[0]).find("mock_fail") != std::string::npos) {
    return -1;
  }
  return llm::RuntimeStub::GetInstance()->rtModelExecuteSync(model, stream, flag, timeout);
}

rtError_t rtGetFunctionByName(const char *stub_name, void **stub_func) {
  return llm::RuntimeStub::GetInstance()->rtGetFunctionByName(stub_name, stub_func);
}
rtError_t rtGetAddrByFun(const void *stubFunc, void **addr) {
  *(char **)addr = (char *)("dev_func");
  return RT_ERROR_NONE;
}

rtError_t rtCtxCreate(rtContext_t *ctx, uint32_t flags, int32_t device) {
  return llm::RuntimeStub::GetInstance()->rtCtxCreate(ctx, flags, device);
}
rtError_t rtCtxCreateV2(rtContext_t *ctx,
                        uint32_t flags,
                        int32_t device,
                        rtDeviceMode deviceMode) { return RT_ERROR_NONE; }

rtError_t rtKernelLaunchEx(void *args, uint32_t args_size, uint32_t flags, rtStream_t stream_) {
  const char * const kEnvRecordPath = "CONSTANT_FOLDING_PASS_6";
  char record_path[MMPA_MAX_PATH] = {};
  (void)mmGetEnv(kEnvRecordPath, &record_path[0], static_cast<uint32_t>(MMPA_MAX_PATH));

  if (std::string(&record_path[0]).find("mock_fail") != std::string::npos) {
    return -1;
  }

  return llm::RuntimeStub::GetInstance()->rtKernelLaunchEx(args, args_size, flags, stream_);
}

rtError_t rtSetExceptionExtInfo(const rtArgsSizeInfo_t *const sizeInfo) {
  return llm::RuntimeStub::GetInstance()->rtSetExceptionExtInfo(sizeInfo);
}

rtError_t rtModelGetTaskId(void *handle, uint32_t *task_id, uint32_t *stream_id) {
  if (std::string(__FUNCTION__) == g_runtime_stub_mock) {
    return -1;
  }
  return llm::RuntimeStub::GetInstance()->rtModelGetTaskId(handle, task_id, stream_id);
}

rtError_t rtEndGraph(rtModel_t model, rtStream_t stream) { return RT_ERROR_NONE; }
rtError_t rtEndGraphEx(rtModel_t model, rtStream_t stream, uint32_t flags)
{
  return RT_ERROR_NONE;
}
rtError_t rtProfilerStop(uint64_t profConfig, int32_t numsDev, uint32_t *deviceList) {
  return RT_ERROR_NONE;
}

rtError_t rtUnsetDvfsProfile() { return RT_ERROR_NONE; }

rtError_t rtCtxDestroy(rtContext_t ctx) { return RT_ERROR_NONE; }

rtError_t rtProfilerInit(const char *prof_dir, const char *address, const char *job_ctx) { return RT_ERROR_NONE; }

rtError_t rtProfilerStart(uint64_t profConfig, int32_t numsDev, uint32_t *deviceList) {
  return RT_ERROR_NONE;
}

rtError_t rtLabelCreate(rtLabel_t *label) {
  *label = new uint64_t;
  return RT_ERROR_NONE;
}

rtError_t rtLabelCreateEx(rtLabel_t *label, rtStream_t stream) {
  *label = new uint64_t;
  return RT_ERROR_NONE;
}

rtError_t rtLabelCreateV2(rtLabel_t *label, rtModel_t model) {
  *label = new uint64_t;
  return RT_ERROR_NONE;
}

rtError_t rtLabelCreateExV2(rtLabel_t *label, rtModel_t model, rtStream_t stream) {
  *label = new uint64_t;
  return RT_ERROR_NONE;
}

rtError_t rtLabelListCpy(rtLabel_t *label, uint32_t labelNumber, void *dst, uint32_t dstMax) {
  return RT_ERROR_NONE;
}

rtError_t rtLabelDestroy(rtLabel_t label) {
  uint64_t *stub = static_cast<uint64_t *>(label);
  delete stub;
  return RT_ERROR_NONE;
}

rtError_t rtLabelSet(rtLabel_t label, rtStream_t stream) { return RT_ERROR_NONE; }

rtError_t rtLabelSwitch(void *ptr, rtCondition_t condition, uint32_t value, rtLabel_t true_label, rtStream_t stream) {
  return RT_ERROR_NONE;
}

rtError_t rtLabelSwitchByIndex(void *ptr, uint32_t max, void *labelInfoPtr, rtStream_t stream) {
  return RT_ERROR_NONE;
}

rtError_t rtLabelGoto(rtLabel_t label, rtStream_t stream) { return RT_ERROR_NONE; }

rtError_t rtLabelGotoEx(rtLabel_t label, rtStream_t stream) {
  return RT_ERROR_NONE;
}


rtError_t rtInvalidCache(void *base, size_t len) {
  return RT_ERROR_NONE;
}

rtError_t rtModelLoadComplete(rtModel_t model) {
  const char * const kEnvRecordPath = "CONSTANT_FOLDING_PASS_7";
  char record_path[MMPA_MAX_PATH] = {};
  (void)mmGetEnv(kEnvRecordPath, &record_path[0], static_cast<uint32_t>(MMPA_MAX_PATH));

  if (std::string(&record_path[0]).find("mock_fail") != std::string::npos) {
    return -1;
  }

  return RT_ERROR_NONE;
}

rtError_t rtStreamCreateWithFlags(rtStream_t *stream, int32_t priority, uint32_t flags) {
  return llm::RuntimeStub::GetInstance()->rtStreamCreateWithFlags(stream, priority,flags);
}

rtError_t rtStreamSetMode(rtStream_t stm, const uint64_t stmMode) {
  return llm::RuntimeStub::GetInstance()->rtStreamSetMode(stm, stmMode);
}

rtError_t rtFlushCache(void *base, size_t len) {
  return RT_ERROR_NONE;
}

rtError_t rtProfilerTrace(uint64_t id, bool notify, uint32_t flags, rtStream_t stream_) { return RT_ERROR_NONE; }

ADD_STUB_RETURN_VALUE(rtProfilerTraceEx, rtError_t);
rtError_t rtProfilerTraceEx(uint64_t id, uint64_t modelId, uint16_t tagId, rtStream_t stream) {
  const char *const kEnvRecordPath = "CONSTANT_FOLDING_PASS";
  char record_path[MMPA_MAX_PATH] = {};
  (void)mmGetEnv(kEnvRecordPath, &record_path[0], static_cast<uint32_t>(MMPA_MAX_PATH));
  if (std::string(&record_path[0]).find("mock_fail") != std::string::npos) {
    return -1;
  }
  return GET_STUB_RETURN_VALUE(rtProfilerTraceEx, rtError_t, RT_ERROR_NONE);
}

rtError_t rtMemSetRC(const void *dev_ptr, uint64_t size, uint32_t read_count) { return RT_ERROR_NONE; }

rtError_t rtStreamSwitch(void *ptr, rtCondition_t condition, int64_t value, rtStream_t true_stream, rtStream_t stream) {
  return RT_ERROR_NONE;
}

rtError_t rtStreamSwitchN(void *ptr, uint32_t size, void *valuePtr, rtStream_t *trueStreamPtr, uint32_t elementSize,
                          rtStream_t stream, rtSwitchDataType_t dataType) {
  return RT_ERROR_NONE;
}

rtError_t rtStreamSwitchEx(void *ptr, rtCondition_t condition, void *value_ptr, rtStream_t true_stream,
                           rtStream_t stream, rtSwitchDataType_t data_type) {
  return llm::RuntimeStub::GetInstance()->rtStreamSwitchEx(ptr, condition, value_ptr, true_stream, stream, data_type);
}

rtError_t rtStreamActive(rtStream_t active_stream, rtStream_t stream) { return RT_ERROR_NONE; }

ADD_STUB_RETURN_VALUE(rtDatadumpInfoLoad, rtError_t);
rtError_t rtDatadumpInfoLoad(const void *dump_info, uint32_t length) {
  return GET_STUB_RETURN_VALUE(rtDatadumpInfoLoad, rtError_t,
    llm::RuntimeStub::GetInstance()->rtDatadumpInfoLoad(dump_info, length));
}

rtError_t rtAicpuInfoLoad(const void *aicpuInfo, uint32_t length) {
  return RT_ERROR_NONE;
}

rtError_t rtModelTaskUpdate(rtStream_t desStm, uint32_t desTaskId, rtStream_t sinkStm,
                            rtMdlTaskUpdateInfo_t *para) {
  return RT_ERROR_NONE;
}

rtError_t rtNopTask(rtStream_t stm) {
  return RT_ERROR_NONE;
}

rtError_t rtModelSetExtId(rtModel_t model, uint32_t extId)
{
  return RT_ERROR_NONE;
}

rtError_t rtModelGetId(rtModel_t model, uint32_t *modelId)
{
  if (g_runtime_stub_mock == "rtSupportModelStreamReuse") {
    *modelId = g_rt_model_id++;
  }
  return RT_ERROR_NONE;
}

rtError_t rtModelBindQueue(rtModel_t model, uint32_t queueId, rtModelQueueFlag_t flag)
{
  return RT_ERROR_NONE;
}

rtError_t rtSetSocVersion(const char *version)
{
  return RT_ERROR_NONE;
}

rtError_t rtGetSocVersion(char *version, const uint32_t maxLen)
{
  return llm::RuntimeStub::GetInstance()->rtGetSocVersion(version, maxLen);
}

rtError_t rtGetAiCoreCount(uint32_t *aiCoreCnt)
{
  return RT_ERROR_NONE;
}

rtError_t rtGetAiCpuCount(uint32_t *aiCpuCnt)
{
  return RT_ERROR_NONE;
}

RTS_API rtError_t rtSetOpWaitTimeOut(uint32_t timeout)
{
  return RT_ERROR_NONE;
}

RTS_API rtError_t rtSetOpExecuteTimeOut(uint32_t timeout)
{
  return RT_ERROR_NONE;
}

RTS_API rtError_t rtSetDeviceSatMode(rtFloatOverflowMode_t mode)
{
  return RT_ERROR_NONE;
}

rtError_t rtSetTaskFailCallback(rtTaskFailCallback callback)
{
  return RT_ERROR_NONE;
}

rtError_t rtMallocHostSharedMemory(rtMallocHostSharedMemoryIn *in,
		                               rtMallocHostSharedMemoryOut *out)
{
  out->ptr = new uint8_t[in->size];
  out->devPtr = new uint8_t[in->size];
  return RT_ERROR_NONE;
}

rtError_t rtFreeHostSharedMemory(rtFreeHostSharedMemoryIn *in)
{
  delete[] (uint8_t*)in->ptr;
  delete[] (uint8_t*)in->devPtr;
  return RT_ERROR_NONE;
}

ADD_STUB_RETURN_VALUE(rtGetAicpuDeploy, rtError_t);
ADD_STUB_OUTBOUND_VALUE(rtGetAicpuDeploy, rtAicpuDeployType_t, value);
rtError_t rtGetAicpuDeploy(rtAicpuDeployType_t *deplyType)
{
  *deplyType = GET_STUB_OUTBOUND_VALUE(rtGetAicpuDeploy, rtAicpuDeployType_t, value, AICPU_DEPLOY_CROSS_PROCESS);
  return GET_STUB_RETURN_VALUE(rtGetAicpuDeploy, rtError_t, RT_ERROR_NONE);
}

rtError_t rtDebugRegister(rtModel_t model, uint32_t flag, const void *addr, uint32_t *streamId, uint32_t *taskId)
{
  return RT_ERROR_NONE;
}

rtError_t rtDebugUnRegister(rtModel_t model)
{
  return RT_ERROR_NONE;
}

rtError_t rtDumpAddrSet(rtModel_t model, void *addr, uint32_t dumpSize, uint32_t flag)
{
  if (__FUNCTION__ == g_runtime_stub_mock) {
    return -1;
  }
  return RT_ERROR_NONE;
}

rtError_t rtSetCtxINFMode(bool mode)
{
  return RT_ERROR_NONE;
}

rtError_t rtGetRtCapability(rtFeatureType_t featureType, int32_t featureInfo, int64_t *value)
{
  const char * const kEnvRecordPath = "SET_CAPA_VALUE";
  char record_path[MMPA_MAX_PATH] = {};
  (void)mmGetEnv(kEnvRecordPath, &record_path[0], static_cast<uint32_t>(MMPA_MAX_PATH));

  if (std::string(&record_path[0]).find("mock_fail") != std::string::npos) {
    *value = 1;
  }

  return RT_ERROR_NONE;
}

uint32_t rtGetTsMemType(rtMemRequestFeature_t featureType, uint32_t memSize) {
  const char * const kEnvRecordPath = "RT_MEMORY_HBM";
  char record_path[MMPA_MAX_PATH] = {};
  (void)mmGetEnv(kEnvRecordPath, &record_path[0], static_cast<uint32_t>(MMPA_MAX_PATH));

  if (std::string(&record_path[0]).find("mock_fail") != std::string::npos) {
    return RT_MEMORY_HBM;
  }

  return RT_MEMORY_TS;
}

rtError_t rtGetMaxStreamAndTask(uint32_t streamType, uint32_t *maxStrCount, uint32_t *maxTaskCount)
{
  if (std::string(__FUNCTION__) == g_runtime_stub_mock) {
    return -1;
  }
  *maxStrCount = 1024;
  *maxTaskCount = 1024;
  if (streamType == RT_HUGE_STREAM) {
    *maxStrCount = 8192;
    *maxTaskCount = 8192;
  }
  return RT_ERROR_NONE;
}

rtError_t rtModelExit(rtModel_t model, rtStream_t stream)
{
 return RT_ERROR_NONE;
}

rtError_t rtGetTaskIdAndStreamID(uint32_t *taskId, uint32_t *streamId)
{
 if (*taskId == 999 || *streamId == 999) {
  return -1;
 }
 return RT_ERROR_NONE;
}

rtError_t rtDebugRegisterForStream(rtStream_t stream, uint32_t flag, const void *addr, uint32_t *streamId, uint32_t *taskId) {
  return RT_ERROR_NONE;
}

rtError_t rtDebugUnRegisterForStream(rtStream_t stream) {
  return RT_ERROR_NONE;
}

rtError_t rtFftsTaskLaunch(rtFftsTaskInfo_t *fftsTaskInfo, rtStream_t stream) {
  return RT_ERROR_NONE;
}

rtError_t rtStarsTaskLaunchWithFlag(const void *taskSqe, uint32_t sqeLen, rtStream_t stm, uint32_t flag){
  return RT_ERROR_NONE;
}

rtError_t rtKernelGetAddrAndPrefCnt(void *handle, const uint64_t tilingKey, const void *const stubFunc,
                                    const uint32_t flag, void **addr, uint32_t *prefetchCnt) {
  return RT_ERROR_NONE;
}

rtError_t rtKernelGetAddrAndPrefCntV2(void *handle, const uint64_t tilingKey, const void *const stubFunc,
                                      const uint32_t flag, rtKernelDetailInfo_t *kernelInfo) {
  return llm::RuntimeStub::GetInstance()->rtKernelGetAddrAndPrefCntV2(handle, tilingKey, stubFunc, flag, kernelInfo);
}

rtError_t rtFftsPlusTaskLaunch(rtFftsPlusTaskInfo_t *fftsPlusTaskInfo, rtStream_t stream) {
  return RT_ERROR_NONE;
}

rtError_t rtStarsTaskLaunch(const void *address, uint32_t len, rtStream_t stm) {
  return RT_ERROR_NONE;
}

rtError_t rtGeneralCtrl(uintptr_t *ctrl, uint32_t num, uint32_t type) {
  return llm::RuntimeStub::GetInstance()->rtGeneralCtrl(ctrl, num, type);
}
rtError_t rtKernelLaunchFwk(const char *opName, void *args, uint32_t argSize, uint32_t flags, rtStream_t rtStream) {
  return RT_ERROR_NONE;
}

rtError_t rtAicpuKernelLaunchWithFlag(const rtKernelLaunchNames_t *launchNames, uint32_t blockDim,
                                         const rtArgsEx_t *args, rtSmDesc_t *smDesc, rtStream_t stream,
                                         uint32_t flags) {
  return llm::RuntimeStub::GetInstance()->rtAicpuKernelLaunchWithFlag(launchNames, blockDim, args, smDesc, stream,
                                                                     flags);
}

rtError_t rtAicpuKernelLaunchEx(uint32_t kernelType, const rtKernelLaunchNames_t *launchNames,
                                uint32_t blockDim, const rtArgsEx_t *argsInfo, rtSmDesc_t *smDesc,
                                rtStream_t stream, uint32_t flags) {
  return RT_ERROR_NONE;
}

rtError_t rtAicpuKernelLaunchExWithArgs(uint32_t kernelType, const char *opName, uint32_t blockDim,
                                        const rtAicpuArgsEx_t *argsInfo, rtSmDesc_t *smDesc,
                                        rtStream_t stream, uint32_t flags) {
  return llm::RuntimeStub::GetInstance()->rtAicpuKernelLaunchExWithArgs(kernelType, opName, blockDim, argsInfo,
                                                                       smDesc, stream, flags);
}

rtError_t rtSetDeviceIdByGeModelIdx(uint32_t modelIdx, uint32_t deviceId) {
  return RT_ERROR_NONE;
}

rtError_t rtUnsetDeviceIdByGeModelIdx(uint32_t modelIdx, uint32_t deviceId) {
  return RT_ERROR_NONE;
}

rtError_t rtProfRegisterCtrlCallback(uint32_t logId, rtProfCtrlHandle callback) {
  return RT_ERROR_NONE;
}

rtError_t rtFftsPlusTaskLaunchWithFlag(rtFftsPlusTaskInfo_t *fftsPlusTaskInfo, rtStream_t stream, uint32_t flag) {
  return RT_ERROR_NONE;
}

rtError_t rtFftsTaskLaunchWithFlag(rtFftsTaskInfo_t *fftsTaskInfo, rtStream_t stream, uint32_t flag) {
  return RT_ERROR_NONE;
}

rtError_t rtGetC2cCtrlAddr(uint64_t *addr, uint32_t *len) {
  return RT_ERROR_NONE;
}

rtError_t rtGetDevMsg(rtGetDevMsgType_t getMsgType, rtGetMsgCallback callback) {
  const char * const kEnvRecordPath = "NPU_COLLECT_PATH";
  char record_path[MMPA_MAX_PATH] = {};
  (void)mmGetEnv(kEnvRecordPath, &record_path[0], static_cast<uint32_t>(MMPA_MAX_PATH));

  if (std::string(&record_path[0]).find("mock_fail") != std::string::npos) {
    return -1;
  }

  const char *snapshot = "snapshot";
  callback(snapshot, strlen(snapshot));
  return RT_ERROR_NONE;
}

rtError_t rtSetTaskTag(const char *taskTag) {
  return llm::RuntimeStub::GetInstance()->rtSetTaskTag(taskTag);
}

rtError_t rtSetAicpuAttr(const char *key, const char *val) {
  return RT_ERROR_NONE;
}

rtError_t rtRegTaskFailCallbackByModule(const char *moduleName, rtTaskFailCallback callback) {
  return llm::RuntimeStub::GetInstance()->rtRegTaskFailCallbackByModule(moduleName, callback);
}

rtError_t rtGetIsHeterogenous(int32_t *heterogeneous) {
  return llm::RuntimeStub::GetInstance()->rtGetIsHeterogenous(heterogeneous);
}

rtError_t rtMemGrpQuery(rtMemGrpQueryInput_t * const input, rtMemGrpQueryOutput_t *output)
{
  return llm::RuntimeStub::GetInstance()->rtMemGrpQuery(input, output);
}

rtError_t rtMemQueueGetQidByName(int32_t device, const char *name, uint32_t *qId) {
  return RT_ERROR_NONE;
}

rtError_t rtMemQueueSet(int32_t devId, rtMemQueueSetCmdType, const rtMemQueueSetInputPara *input) {
  return 0;
}

rtError_t rtMemQueueInit(int32_t devId) {
  return 0;
}

rtError_t rtMemQueueGrant(int32_t devId, uint32_t qid, int32_t pid, rtMemQueueShareAttr_t *attr) {
  return 0;
}

rtError_t rtMemQueueCreate(int32_t device, const rtMemQueueAttr_t *queAttr, uint32_t *qid) {
  *qid = llm::mem_queues_[device].size();
  llm::mem_queues_[device][*qid] = std::queue<void *>{};
  return 0;
}

rtError_t rtMemQueueDestroy(int32_t device, uint32_t qid) {
  llm::mem_queues_[device].erase(qid);
  return 0;
}

rtError_t rtMemQueueEnQueueBuff(int32_t device, uint32_t qid, rtMemQueueBuff_t *inBuf, int32_t timeout) {
  return llm::RuntimeStub::GetInstance()->rtMemQueueEnQueueBuff(device, qid, inBuf, timeout);
}

rtError_t rtMemQueueDeQueueBuff(int32_t device, uint32_t qid, rtMemQueueBuff_t *outBuf, int32_t timeout) {
  return llm::RuntimeStub::GetInstance()->rtMemQueueDeQueueBuff(device, qid, outBuf, timeout);
}

rtError_t rtMemQueueEnQueue(int32_t device, uint32_t qid, void *mbuf) {
  return llm::RuntimeStub::GetInstance()->rtMemQueueEnQueue(device, qid, mbuf);
}

rtError_t rtMemQueueDeQueue(int32_t device, uint32_t qid, void **mbuf) {
  return llm::RuntimeStub::GetInstance()->rtMemQueueDeQueue(device, qid, mbuf);
}

rtError_t rtMemQueuePeek(int32_t device, uint32_t qid, size_t *bufLen, int32_t timeout) {
  return llm::RuntimeStub::GetInstance()->rtMemQueuePeek(device, qid, bufLen, timeout);
}

rtError_t rtMbufInit(rtMemBuffCfg_t *cfg) {
  return 0;
}

rtError_t rtMemGrpCreate(const char *name, const rtMemGrpConfig_t *cfg) {
  return 0;
}

rtError_t rtMemGrpAddProc(const char *name, int32_t pid, const rtMemGrpShareAttr_t *attr) {
  return 0;
}

rtError_t rtMemGrpAttach(const char *name, int32_t timeout) {
  return 0;
}

rtError_t rtMbufUnBuild(rtMbufPtr_t mbuf, void **buff, uint64_t * const size) {
  return 0;
}

rtError_t rtMbufBuild(void *buff, uint64_t size, rtMbufPtr_t *mbuf) {
  return 0;
}

rtError_t rtBuffGetInfo(rtBuffGetCmdType type, const void *const in, uint32_t in_len, void *const out, uint32_t *const out_len) {
  return 0;
}

rtError_t rtBuffGet(const rtMbufPtr_t mbufPtr, void *buff, const uint64_t size){
  return 0;
}

rtError_t rtBuffPut(const rtMbufPtr_t mbufPtr, void *buff){
  return 0;
}

rtError_t rtBuffAlloc(uint64_t size, void **buff) {
  return llm::RuntimeStub::GetInstance()->rtBuffAlloc(size, buff);
}

rtError_t rtBuffFree(void *buff) {
  return 0;
}

rtError_t rtBuffConfirm(void *buff, const uint64_t size){
  return llm::RuntimeStub::GetInstance()->rtBuffConfirm(buff, size);
}

rtError_t rtMbufAlloc(rtMbufPtr_t *mbuf, uint64_t size) {
  return llm::RuntimeStub::GetInstance()->rtMbufAlloc(mbuf, size);
}

rtError_t rtMbufCopyBufRef(rtMbufPtr_t mbuf, rtMbufPtr_t *ref_mbuf) {
  return llm::RuntimeStub::GetInstance()->rtMbufCopyBufRef(mbuf, ref_mbuf);
}

rtError_t rtMbufFree(rtMbufPtr_t mbuf) {
  return llm::RuntimeStub::GetInstance()->rtMbufFree(mbuf);
}

rtError_t rtMbufSetDataLen(rtMbufPtr_t mbuf, uint64_t len) {
  return 0;
}

rtError_t rtMbufGetBuffAddr(rtMbufPtr_t mbuf, void **databuf) {
  return llm::RuntimeStub::GetInstance()->rtMbufGetBuffAddr(mbuf, databuf);
}

rtError_t rtMbufGetDataLen(rtMbufPtr_t memBuf, uint64_t *len) {
    return llm::RuntimeStub::GetInstance()->rtMbufGetBuffSize(memBuf, len);
}

rtError_t rtMbufGetBuffSize(rtMbufPtr_t mbuf, uint64_t *size) {
  return llm::RuntimeStub::GetInstance()->rtMbufGetBuffSize(mbuf, size);
}

rtError_t rtMbufGetPrivInfo(rtMbufPtr_t mbuf, void **priv, uint64_t *size) {
  return llm::RuntimeStub::GetInstance()->rtMbufGetPrivInfo(mbuf, priv, size);
}

rtError_t rtMemQueueAttach(int32_t devId, uint32_t qid, int32_t timeout) {
  return 0;
}

rtError_t rtEschedSubmitEventSync(int32_t devId, rtEschedEventSummary_t *event, rtEschedEventReply_t *ack) {
  return 0;
}

rtError_t rtEschedSubmitEvent(int32_t devId, rtEschedEventSummary_t *event) {
  return 0;
}

rtError_t rtCmoTaskLaunch(rtCmoTaskInfo_t *taskInfo, rtStream_t stm, uint32_t flag) {
  return RT_ERROR_NONE;
}

rtError_t rtBarrierTaskLaunch(rtBarrierTaskInfo_t *taskInfo, rtStream_t stm, uint32_t flag) {
  return RT_ERROR_NONE;
}

rtError_t rtEschedAttachDevice(int32_t device) {
  return 0;
}

rtError_t rtEschedCreateGrp(int32_t devId, uint32_t grpId, rtGroupType_t type) {
  return 0;
}

rtError_t rtEschedWaitEvent(int32_t devId,
                            uint32_t grpId,
                            uint32_t threadId,
                            int32_t timeout,
                            rtEschedEventSummary_t *event) {
  return llm::RuntimeStub::GetInstance()->rtEschedWaitEvent(devId, grpId, threadId, timeout, event);
}

rtError_t rtMemGrpCacheAlloc(const char *name,
                             int32_t devId,
                             const rtMemGrpCacheAllocPara *para) {
  return llm::RuntimeStub::GetInstance()->rtMemGrpCacheAlloc(name, devId, para);
}

rtError_t rtEschedSubscribeEvent(int32_t devId,
                                 uint32_t grpId,
                                 uint32_t threadId,
                                 uint64_t eventBitmap) {
  return 0;
}

rtError_t rtEschedQueryInfo(const uint32_t devId,
                            const rtEschedQueryType type,
                            rtEschedInputInfo *input,
                            rtEschedOutputInfo *output) {
  return 0;
}

rtError_t rtQueueSubscribe(int32_t devId, uint32_t qid, uint32_t groupId, int32_t type) {
  return 0;
}

rtError_t rtQueueSubF2NFEvent(int32_t devId, uint32_t qid, uint32_t groupId) {
  return 0;
}

ADD_STUB_RETURN_VALUE(rtNpuGetFloatStatus, rtError_t);
rtError_t rtNpuGetFloatStatus(void *outputAddr, uint64_t outputSize, uint32_t checkMode, rtStream_t stm) {
  return RT_ERROR_NONE;
}

ADD_STUB_RETURN_VALUE(rtNpuClearFloatStatus, rtError_t);
rtError_t rtNpuClearFloatStatus(uint32_t checkMode, rtStream_t stm) {
  return RT_ERROR_NONE;
}

rtError_t rtCtxGetDevice(int32_t *device) {
  *device = 0;
  return RT_ERROR_NONE;
}

rtError_t rtGetPriCtxByDeviceId(int32_t device, rtContext_t *ctx) {
  if (__FUNCTION__ == g_runtime_stub_mock_v2) {
    return -1;
  }
  uintptr_t x = 1;
  *ctx = (rtContext_t *)x;
  return RT_ERROR_NONE;
}

rtError_t rtGetAvailStreamNum(const uint32_t streamType, uint32_t * const streamCount) {
  return llm::RuntimeStub::GetInstance()->rtGetAvailStreamNum(streamType, streamCount);
}

rtError_t rtGetAvailEventNum(uint32_t * const eventCount) {
  *eventCount = (uint32_t)g_free_event_num;
  return RT_ERROR_NONE;
}

rtError_t rtCtxGetOverflowAddr(void **overflowAddr) {
  *overflowAddr = (void *)0x1;
  return RT_ERROR_NONE;
}

rtError_t rtGetStreamBufferLen(const bool isHuge, uint32_t * const bufferLen) {
  (void)isHuge;
  *bufferLen = 1U;
  return 0;
}

rtError_t rtGetTaskBufferLen(const rtTaskBuffType_t type, uint32_t * const bufferLen) {
  (void)type;
  *bufferLen = 1U;
  return 0;
}

rtError_t rtTaskBuild(const rtTaskInput_t * const taskInput, uint32_t* taskLen) {
  (void)taskInput;
  *taskLen = 1U;
  return 0;
}

RTS_API rtError_t rtGetKernelBin(const char_t * const binFileName, char_t **const buffer, uint32_t *length) {
  (void)binFileName;
  std::vector<uint8_t> buff(64, 'A');
  *buffer = (char_t *)buff.data();
  *length = buff.size();
  return 0;
}

RTS_API rtError_t rtFreeKernelBin(char_t * const buffer) {
  (void)buffer;
  return 0;
}

RTS_API rtError_t rtGetElfOffset(void * const elfData, const uint32_t elfLen, uint32_t* offset) {
  (void)elfData;
  (void)elfLen;
  (void)offset;
  return 0;
}

RTS_API rtError_t rtSetStreamSqLock(rtStream_t stm) {
  (void)stm;
  return 0;
}

RTS_API rtError_t rtSetStreamSqUnlock(rtStream_t stm) {
  (void)stm;
  return 0;
}

RTS_API rtError_t rtNeedDevVA2PA(bool *need) {
  *need = true;
  return 0;
}

RTS_API rtError_t rtDevVA2PA(uint64_t devAddr, uint64_t len, rtStream_t stm, bool isAsync) {
  (void)devAddr;
  (void)len;
  (void)stm;
  (void)isAsync;
  return 0;
}

rtError_t rtModelCheckCompatibility(const char_t *OmSoCVersion, const char_t *OMArchVersion) {
  if (std::string(__FUNCTION__) == g_runtime_stub_mock) {
    return -1;
  }
  return llm::RuntimeStub::GetInstance()->rtModelCheckCompatibility(OmSoCVersion, OMArchVersion);
}
rtError_t rtLaunchSqeUpdateTask(uint32_t streamId, uint32_t taskId, void *src, uint64_t cnt,
                                rtStream_t stm) {
  return llm::RuntimeStub::GetInstance()->rtLaunchSqeUpdateTask(streamId, taskId, src, cnt, stm);
}

rtError_t rtReserveMemAddress(void** devPtr, size_t size, size_t alignment, void *devAddr, uint64_t flags) {
  return llm::RuntimeStub::GetInstance()->rtReserveMemAddress(devPtr, size, alignment, devAddr, flags);
}

rtError_t rtReleaseMemAddress(void* devPtr) {
  return llm::RuntimeStub::GetInstance()->rtReleaseMemAddress(devPtr);
}

rtError_t rtMallocPhysical(rtDrvMemHandle* handle, size_t size, rtDrvMemProp_t* prop, uint64_t flags) {
  return llm::RuntimeStub::GetInstance()->rtMallocPhysical(handle, size, prop, flags);
}

rtError_t rtFreePhysical(rtDrvMemHandle handle) {
  return llm::RuntimeStub::GetInstance()->rtFreePhysical(handle);
}

rtError_t rtMapMem(void* devPtr, size_t size, size_t offset, rtDrvMemHandle handle, uint64_t flags) {
  return llm::RuntimeStub::GetInstance()->rtMapMem(devPtr, size, offset, handle, flags);
}

rtError_t rtUnmapMem(void* devPtr) {
  return llm::RuntimeStub::GetInstance()->rtUnmapMem(devPtr);
}

rtError_t rtCtxGetCurrentDefaultStream(rtStream_t* stm) {
  return llm::RuntimeStub::GetInstance()->rtCtxGetCurrentDefaultStream(stm);
}

rtError_t rtStreamAbort(rtStream_t stm) {
  (void) stm;
  return RT_ERROR_NONE;
}

rtError_t rtsValueWrite(const void * const devAddr, const uint64_t value, const uint32_t flag, rtStream_t stm) {
  return RT_ERROR_NONE;
}

rtError_t rtsValueWait(const void * const devAddr, const uint64_t value, const uint32_t flag, rtStream_t stm) {
  return RT_ERROR_NONE;
}

rtError_t rtStreamTaskClean(rtStream_t stm) {
  return llm::RuntimeStub::GetInstance()->rtStreamTaskClean(stm);
}

rtError_t rtsBinaryLoadFromFile(const char * const binPath, const rtLoadBinaryConfig_t *const optionalCfg,
                                rtBinHandle *binHandle)
{
  return llm::RuntimeStub::GetInstance()->rtsBinaryLoadFromFile(binPath, optionalCfg, binHandle);
}
rtError_t rtsFuncGetByName(const rtBinHandle binHandle, const char *kernelName,
                           rtFuncHandle *funcHandle)
{
  return llm::RuntimeStub::GetInstance()->rtsFuncGetByName(binHandle, kernelName, funcHandle);
}
rtError_t rtsLaunchCpuKernel(const rtFuncHandle funcHandle, const uint32_t blockDim, rtStream_t st,
                             const rtKernelLaunchCfg_t *cfg, rtCpuKernelArgs_t *argsInfo)
{
  return llm::RuntimeStub::GetInstance()->rtsLaunchCpuKernel(funcHandle, blockDim, st, cfg, argsInfo);
}

#ifdef __cplusplus
}
#endif
