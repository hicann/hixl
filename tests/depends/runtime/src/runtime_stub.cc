/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <string>
#include <map>
#include <queue>
#include "securec.h"
#include "mmpa/mmpa_api.h"
#include "acl/acl_rt.h"
#include "runtime_stub.h"
#include "acl/acl.h"
#include "runtime/base.h"
#include "runtime/rt_preload_task.h"
#include "rt_error_codes.h"
#include <iostream>
#include <runtime/runtime/dev.h>

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

rtError_t RuntimeStub::rtGetDevResAddress(const rtDevResInfo *resInfo, rtDevResAddrInfo *addrInfo) {
  (void)resInfo;
  static uint64_t g_dummy_dev_mem = 0x88888888ULL;
  if (addrInfo != nullptr) {
    if (addrInfo->resAddress != nullptr) {
      *static_cast<uint64_t*>(addrInfo->resAddress) = g_dummy_dev_mem;
    }
    if (addrInfo->len != nullptr) {
      *(addrInfo->len) = static_cast<uint32_t>(sizeof(g_dummy_dev_mem));
    }
  }
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
}

rtError_t rtGetDevResAddress(const rtDevResInfo *resInfo, rtDevResAddrInfo *addrInfo) {
  return llm::RuntimeStub::GetInstance()->rtGetDevResAddress(resInfo, addrInfo);
}

#ifdef __cplusplus
}
#endif
