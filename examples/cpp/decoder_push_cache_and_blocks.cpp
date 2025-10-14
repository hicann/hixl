/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <numeric>
#include <cstdio>
#include <thread>
#include <iostream>
#include "acl/acl.h"
#include "llm_datadist/llm_datadist.h"

using namespace llm_datadist;
namespace {
constexpr uint16_t kDecoderListenPort = 26001;
constexpr uint16_t kDecoderClusterId = 1;
constexpr uint32_t kNumTensors = 4U;
constexpr size_t kTensorSize = 8 * 16 * sizeof(int32_t);
const std::vector<int64_t> kTensorShape = {8, 16};
constexpr size_t kTensorBlockElementNum = 16;
constexpr int32_t kWaitPromptTime = 10;
constexpr int32_t kExpectedArgCnt = 3;
constexpr uint32_t kArgIndexDeviceId = 1;
constexpr uint32_t kArgIndexLocalIp = 2;

#define CHECK_ACL(x)                                                                  \
  do {                                                                                \
    aclError __ret = x;                                                               \
    if (__ret != ACL_ERROR_NONE) {                                                    \
      std::cerr << __FILE__ << ":" << __LINE__ << " aclError:" << __ret << std::endl; \
    }                                                                                 \
  } while (0)
}  // namespace

int Initialize(LlmDataDist &llm_datadist, const std::string &device_id, const std::string &local_ip) {
  std::map<AscendString, AscendString> options;
  options[OPTION_DEVICE_ID] = device_id.c_str();
  options[OPTION_LISTEN_IP_INFO] = (std::string(local_ip) + ":" + std::to_string(kDecoderListenPort)).c_str();
  auto ret = llm_datadist.Initialize(options);
  if (ret != LLM_SUCCESS) {
    printf("[ERROR] Initialize failed, ret = %u\n", ret);
    return -1;
  }
  printf("[INFO] Initialize success\n");
  return LLM_SUCCESS;
}

int32_t CheckBuffers(const std::vector<void *> &buffers, const std::vector<uint32_t> &check_index_list) {
  for (auto buffer : buffers) {
    std::vector<int32_t> host_buffer(kTensorSize / sizeof(int32_t));
    CHECK_ACL(aclrtMemcpy(&host_buffer[0], kTensorSize, buffer, kTensorSize, ACL_MEMCPY_DEVICE_TO_HOST));
    for (auto check_index : check_index_list) {
      for (size_t i = 0U; i < kTensorBlockElementNum; ++i) {
        auto expect = check_index * kTensorBlockElementNum + i;
        if (static_cast<uint32_t>(host_buffer[expect]) != expect) {
          printf("[ERROR] Buffer check failed, index = %zu, val = %d, expect val = %zu\n", expect, host_buffer[expect],
                 expect);
          return -1;
        }
      }
    }
  }
  printf("[INFO] CheckBuffers success\n");
  return 0;
}

void Finalize(LlmDataDist &llm_datadist, int64_t cache_id, const std::vector<void *> buffers) {
  if (cache_id > 0) {
    auto ret = llm_datadist.UnregisterKvCache(cache_id);
    if (ret != 0) {
      printf("[ERROR] UnregisterKvCache failed, ret = %u\n", ret);
    } else {
      printf("[INFO] UnregisterKvCache success\n");
    }
  }
  for (auto buffer : buffers) {
    aclrtFree(buffer);
  }
  llm_datadist.Finalize();
}

int32_t RunDecoderSample(const char *device_id, const char *local_ip) {
  printf("[INFO] Decoder Sample start\n");
  // 1. 初始化
  LlmDataDist llm_datadist(kDecoderClusterId, LlmRole::kDecoder);
  if (Initialize(llm_datadist, device_id, local_ip) != 0) {
    return -1;
  }

  // 2. 注册内存地址
  CacheDesc cache_desc{};
  cache_desc.num_tensors = kNumTensors;
  cache_desc.data_type = DT_INT32;
  cache_desc.shape = kTensorShape;
  std::vector<uint64_t> tensor_addrs;
  std::vector<void *> buffers;
  for (uint32_t i = 0U; i < kNumTensors; ++i) {
    int32_t *buffer = nullptr;
    CHECK_ACL(aclrtMalloc((void **)&buffer, kTensorSize, ACL_MEM_MALLOC_HUGE_ONLY));
    tensor_addrs.emplace_back(reinterpret_cast<uint64_t>(buffer));
    buffers.emplace_back(reinterpret_cast<void *>(buffer));
  }
  int64_t cache_id = -1;
  auto ret = llm_datadist.RegisterKvCache(cache_desc, tensor_addrs, {}, cache_id);
  if (ret != LLM_SUCCESS) {
    printf("[ERROR] RegisterKvCache failed, ret = %u\n", ret);
    Finalize(llm_datadist, cache_id, buffers);
    return -1;
  }
  // 3. RegisterKvCache成功后，可以获取cache中各tensor的地址用于后续操作
  printf("[INFO] RegisterKvCache success\n");
  for (size_t i = 0U; i < tensor_addrs.size(); ++i) {
    printf("[INFO] Tensor[%zu] addr = %p\n", i, reinterpret_cast<void *>(tensor_addrs[i]));
  }

  // 4. 等待prompt写完cache，实际业务场景可通过合适方式实现通知
  std::this_thread::sleep_for(std::chrono::seconds(kWaitPromptTime));
  if (CheckBuffers(buffers, {4, 5, 6, 7}) != 0) {
    Finalize(llm_datadist, cache_id, buffers);
    return -1;
  }

  // 10. 释放cache与llmDataDist
  Finalize(llm_datadist, cache_id, buffers);
  printf("[INFO] Decoder Sample end\n");
  return 0;
}

int main(int32_t argc, char **argv) {
  if (argc != kExpectedArgCnt) {
    printf("[ERROR] expect 3 args(device_id, localHostIp), but got %d\n", argc - 1);
    return -1;
  }
  const auto device_id = argv[kArgIndexDeviceId];
  const auto local_ip = argv[kArgIndexLocalIp];
  printf("[INFO] device_id = %s, local_ip = %s\n", device_id, local_ip);
  auto ret = RunDecoderSample(device_id, local_ip);
  return ret;
}