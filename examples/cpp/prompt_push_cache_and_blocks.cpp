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
constexpr uint16_t kPromptClusterId = 0;
constexpr uint32_t kNumTensors = 4U;
constexpr size_t kTensorSize = 8 * 16 * sizeof(int32_t);
const std::vector<int64_t> kTensorShape = {8, 16};
constexpr int32_t kWaitTime = 5;
constexpr int32_t kExpectedArgCnt = 4;
constexpr uint32_t kArgIndexDeviceId = 1;
constexpr uint32_t kArgIndexLocalIp = 2;
constexpr uint32_t kArgIndexRemoteIp = 3;
constexpr uint32_t kPushBatchIndex = 4;
constexpr uint8_t kPushTensorNumPerLayer = 4;

#define CHECK_ACL(x)                                                                  \
  do {                                                                                \
    aclError __ret = x;                                                               \
    if (__ret != ACL_ERROR_NONE) {                                                    \
      std::cerr << __FILE__ << ":" << __LINE__ << " aclError:" << __ret << std::endl; \
    }                                                                                 \
  } while (0)
}  // namespace

int Initialize(LlmDataDist &llm_datadist, const std::string &device_id) {
  std::map<AscendString, AscendString> options;
  options[OPTION_DEVICE_ID] = device_id.c_str();
  auto ret = llm_datadist.Initialize(options);
  if (ret != LLM_SUCCESS) {
    printf("[ERROR] Initialize failed, ret = %u\n", ret);
    return -1;
  }
  printf("[INFO] Initialize success\n");
  return LLM_SUCCESS;
}

int Link(LlmDataDist &llm_datadist, const char *local_ip, const char *remote_ip) {
  std::vector<Status> rets;
  std::vector<ClusterInfo> clusters;
  ClusterInfo cluster_info;
  cluster_info.remote_cluster_id = 1;
  IpInfo local_ip_info;
  local_ip_info.ip = local_ip;
  local_ip_info.port = kDecoderListenPort;
  cluster_info.local_ip_infos.emplace_back(std::move(local_ip_info));
  IpInfo remote_ip_info;
  remote_ip_info.ip = remote_ip;
  remote_ip_info.port = kDecoderListenPort;
  cluster_info.remote_ip_infos.emplace_back(std::move(remote_ip_info));
  clusters.emplace_back(std::move(cluster_info));
  auto ret = llm_datadist.LinkLlmClusters(clusters, rets);
  if (ret != LLM_SUCCESS) {
    printf("[ERROR] LinkLlmClusters failed, ret = %u\n", ret);
    return -1;
  }
  printf("[INFO] LinkLlmClusters success\n");
  return 0;
}

int Unlink(LlmDataDist &llm_datadist, const char *remote_ip) {
  std::vector<Status> rets;
  std::vector<ClusterInfo> clusters;
  ClusterInfo cluster_info;
  cluster_info.remote_cluster_id = 1;
  IpInfo remote_ip_info;
  remote_ip_info.ip = remote_ip;
  remote_ip_info.port = kDecoderListenPort;
  cluster_info.remote_ip_infos.emplace_back(std::move(remote_ip_info));
  clusters.emplace_back(std::move(cluster_info));
  auto ret = llm_datadist.UnlinkLlmClusters(clusters, rets);
  if (ret != LLM_SUCCESS) {
    printf("[ERROR] UnlinkLlmClusters failed, ret = %u\n", ret);
    return -1;
  }
  printf("[INFO] UnlinkLlmClusters success\n");
  return 0;
}

int32_t PushCache(LlmDataDist &llm_datadist, int64_t cache_id) {
  std::vector<uint64_t> prompt_blocks{5, 6, 7};
  std::vector<uint64_t> decoder_blocks{5, 6, 7};
  // 可以使用PushKvBlock推送多块block的数据
  Cache cache{};
  cache.cache_id = cache_id;
  auto ret = LLM_SUCCESS;
  CacheIndex cache_index = {};
  cache_index.cluster_id = 1;
  cache_index.cache_id = 1;
  for (uint32_t i = 0U; i < kNumTensors; ++i) {
    KvCacheExtParam param{};
    param.src_layer_range = std::pair<int32_t, int32_t>(i, i);
    param.dst_layer_range = std::pair<int32_t, int32_t>(i, i);
    param.tensor_num_per_layer = 1;
    ret = llm_datadist.PushKvBlocks(cache, cache_index, prompt_blocks, decoder_blocks, param);
    if (ret != LLM_SUCCESS) {
      printf("[ERROR] PushKvBlocks failed, ret = %u\n", ret);
      return -1;
    }
  }
  printf("[INFO] PushKvBlocks success\n");

  // 也可以使用PushKvCache推送一个batch中的连续数据
  CacheIndex cache_index2 = {};
  cache_index2.cluster_id = 1;
  cache_index2.cache_id = 1;
  cache_index2.batch_index = kPushBatchIndex;
  KvCacheExtParam param2{};
  param2.src_layer_range = std::pair<int32_t, int32_t>(0, 0);
  param2.dst_layer_range = std::pair<int32_t, int32_t>(0, 0);
  param2.tensor_num_per_layer = kPushTensorNumPerLayer;
  ret = llm_datadist.PushKvCache(cache, cache_index2, kPushBatchIndex, -1, param2);
  if (ret != LLM_SUCCESS) {
    printf("[ERROR] PushKvCache failed, ret = %u\n", ret);
    return -1;
  }
  printf("[INFO] PushKvCache success\n");
  return 0;
}

void Finalize(LlmDataDist &llm_datadist, int64_t cache_id, bool linked, const char *remote_ip,
              const std::vector<void *> buffers) {
  if (linked) {
    auto ret = Unlink(llm_datadist, remote_ip);
    if (ret != 0) {
      printf("[ERROR] Unlink failed, ret = %d\n", ret);
    } else {
      printf("[INFO] Unlink success\n");
    }
  }
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

int32_t RunPromptSample(const char *device_id, const char *local_ip, const char *remote_ip) {
  printf("[INFO] Prompt Sample start\n");
  // 1. 初始化
  LlmDataDist llm_datadist(kPromptClusterId, LlmRole::kPrompt);
  if (Initialize(llm_datadist, device_id) != 0) {
    printf("[ERROR] Initialize LlmDataDist failed\n");
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

    // init device buffer
    std::vector<int32_t> host_buffer(kTensorSize / sizeof(int32_t));
    std::iota(host_buffer.begin(), host_buffer.end(), 0);
    CHECK_ACL(aclrtMemcpy(buffer, kTensorSize, &host_buffer[0], kTensorSize, ACL_MEMCPY_HOST_TO_DEVICE));

    tensor_addrs.emplace_back(reinterpret_cast<uint64_t>(buffer));
    buffers.emplace_back(reinterpret_cast<void *>(buffer));
  }
  int64_t cache_id = -1;
  bool linked = false;
  auto ret = llm_datadist.RegisterKvCache(cache_desc, tensor_addrs, {}, cache_id);
  if (ret != LLM_SUCCESS) {
    printf("[ERROR] RegisterKvCache failed, ret = %u\n", ret);
    Finalize(llm_datadist, cache_id, linked, remote_ip, buffers);
    return -1;
  }
  // 3. RegisterKvCache成功后，可以获取cache中各tensor的地址用于后续操作
  printf("[INFO] RegisterKvCache success\n");
  for (size_t i = 0U; i < tensor_addrs.size(); ++i) {
    printf("[INFO] Tensor[%zu] addr = %p\n", i, reinterpret_cast<void *>(tensor_addrs[i]));
  }

  // 等待decoder注册完成
  std::this_thread::sleep_for(std::chrono::seconds(kWaitTime));

  // 4. 与decoder建链
  if (Link(llm_datadist, local_ip, remote_ip) != 0) {
    Finalize(llm_datadist, cache_id, linked, remote_ip, buffers);
    return -1;
  }
  linked = true;

  // 5. 向decoder push cache
  if (PushCache(llm_datadist, cache_id) != 0) {
    Finalize(llm_datadist, cache_id, linked, remote_ip, buffers);
    return -1;
  }

  // 6. 释放Cache与llmDataDist
  Finalize(llm_datadist, cache_id, linked, remote_ip, buffers);
  printf("[INFO] Prompt Sample end\n");
  return 0;
}

int main(int32_t argc, char **argv) {
  if (argc != kExpectedArgCnt) {
    printf("[ERROR] expect 3 args(device_id, localHostIp, remoteHostIp), but got %d\n", argc - 1);
    return -1;
  }
  const auto device_id = argv[kArgIndexDeviceId];
  const auto local_ip = argv[kArgIndexLocalIp];
  const auto remote_ip = argv[kArgIndexRemoteIp];
  printf("[INFO] device_id = %s, local_ip = %s, remote_ip = %s\n", device_id, local_ip, remote_ip);
  auto ret = RunPromptSample(device_id, local_ip, remote_ip);
  return ret;
}