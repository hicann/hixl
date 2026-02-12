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
constexpr uint16_t kPromptListenPort = 26000;
constexpr uint16_t kDecoderListenPort = 26001;
constexpr uint16_t kPromptClusterId = 0;
constexpr uint16_t kDecoderClusterId = 1;
constexpr uint32_t kNumTensors = 4U;
constexpr size_t kTensorSize = 8 * 16 * sizeof(int32_t);
const std::vector<int64_t> kTensorShape = {8, 16};
constexpr size_t kTensorBlockElementNum = 16;
constexpr int32_t kWaitPromptInitTime = 5;
constexpr int32_t kWaitPromptPushTime = 30;
constexpr int32_t kExpectedArgCnt = 4;
constexpr uint32_t kArgIndexDeviceId = 1;
constexpr uint32_t kArgIndexLocalIp = 2;
constexpr uint32_t kArgIndexRemoteIp = 3;


#define CHECK_ACL(x)                                                                  \
  do {                                                                                \
    aclError __ret = x;                                                               \
    if (__ret != ACL_ERROR_NONE) {                                                    \
      std::cerr << __FILE__ << ":" << __LINE__ << " aclError:" << __ret << std::endl; \
    }                                                                                 \
  } while (0)

const char *GetRecentErrMsg() {
  const char *errmsg = aclGetRecentErrMsg();
  if (errmsg == nullptr) {
    return "no error";
  }
  return errmsg;
}
}  // namespace

int Initialize(LlmDataDist &llm_datadist, const std::string &device_id) {
  std::map<AscendString, AscendString> options;
  options[OPTION_DEVICE_ID] = device_id.c_str();
  auto ret = llm_datadist.Initialize(options);
  if (ret != LLM_SUCCESS) {
    printf("[ERROR] Initialize failed, ret = %u, errmsg: %s\n", ret, GetRecentErrMsg());
    return -1;
  }
  printf("[INFO] Initialize success\n");
  return LLM_SUCCESS;
}

int32_t SetRole(LlmDataDist &llm_datadist, LlmRole role, const char *local_ip) {
  std::map<AscendString, AscendString> options;
  options[OPTION_LISTEN_IP_INFO] = (std::string(local_ip) + ":" + std::to_string(kDecoderListenPort)).c_str();
  auto ret = llm_datadist.SetRole(role, options);
  if (ret != LLM_SUCCESS) {
    printf("[ERROR] SetRole failed, ret = %u, errmsg: %s\n", ret, GetRecentErrMsg());
    return -1;
  }
  printf("[INFO] SetRole success\n");
  return 0;
}

int Link(LlmDataDist &llm_datadist, const char *local_ip, const char *remote_ip) {
  std::vector<Status> rets;
  std::vector<ClusterInfo> clusters;
  ClusterInfo cluster_info;
  cluster_info.remote_cluster_id = 0;
  IpInfo local_ip_info;
  local_ip_info.ip = local_ip;
  local_ip_info.port = kPromptListenPort;
  cluster_info.local_ip_infos.emplace_back(std::move(local_ip_info));
  IpInfo remote_ip_info;
  remote_ip_info.ip = remote_ip;
  remote_ip_info.port = kPromptListenPort;
  cluster_info.remote_ip_infos.emplace_back(std::move(remote_ip_info));
  clusters.emplace_back(std::move(cluster_info));
  auto ret = llm_datadist.LinkLlmClusters(clusters, rets);
  if (ret != LLM_SUCCESS) {
    printf("[ERROR] LinkLlmClusters failed, ret = %u, errmsg: %s\n", ret, GetRecentErrMsg());
    return -1;
  }
  printf("[INFO] LinkLlmClusters success\n");
  return 0;
}

int Unlink(LlmDataDist &llm_datadist, const char *remote_ip) {
  std::vector<Status> rets;
  std::vector<ClusterInfo> clusters;
  ClusterInfo cluster_info;
  cluster_info.remote_cluster_id = 0;
  IpInfo remote_ip_info;
  remote_ip_info.ip = remote_ip;
  remote_ip_info.port = kPromptListenPort;
  cluster_info.remote_ip_infos.emplace_back(std::move(remote_ip_info));
  clusters.emplace_back(std::move(cluster_info));
  auto ret = llm_datadist.UnlinkLlmClusters(clusters, rets);
  if (ret != LLM_SUCCESS) {
    printf("[ERROR] UnlinkLlmClusters failed, ret = %u, errmsg: %s\n", ret, GetRecentErrMsg());
    return -1;
  }
  printf("[INFO] UnlinkLlmClusters success\n");
  return 0;
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

int32_t PullCache(LlmDataDist &llm_datadist, int64_t cache_id) {
  std::vector<uint64_t> prompt_blocks{1, 2, 3};
  std::vector<uint64_t> decoder_blocks{1, 2, 3};
  CacheIndex cache_index = {};
  cache_index.cluster_id = kPromptClusterId;
  cache_index.cache_id = 1;
  cache_index.batch_index = 0;
  // 可以使用PullKvBlock拉取多块block的数据
  Cache cache{};
  cache.cache_id = cache_id;
  auto ret = llm_datadist.PullKvBlocks(cache_index, cache, prompt_blocks, decoder_blocks);
  if (ret != LLM_SUCCESS) {
    printf("[ERROR] PullKvBlocks failed, ret = %u, errmsg: %s\n", ret, GetRecentErrMsg());
    return -1;
  }
  printf("[INFO] PullKvBlocks success\n");
  // 也可以使用PullKvCache拉取一个batch中的连续数据
  cache_index.batch_index = 0;
  ret = llm_datadist.PullKvCache(cache_index, cache, 0);
  if (ret != LLM_SUCCESS) {
    printf("[ERROR] PullKvCache failed, ret = %u, errmsg: %s\n", ret, GetRecentErrMsg());
    return -1;
  }
  printf("[INFO] PullKvCache success\n");
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
      printf("[ERROR] UnregisterKvCache failed, ret = %u, errmsg: %s\n", ret, GetRecentErrMsg());
    } else {
      printf("[INFO] UnregisterKvCache success\n");
    }
  }
  for (auto buffer : buffers) {
    aclrtFree(buffer);
  }
  llm_datadist.Finalize();
}

int32_t RegisterCache(LlmDataDist &llm_datadist, std::vector<void *> &buffers, int64_t &cache_id) {
  CacheDesc cache_desc{};
  cache_desc.num_tensors = kNumTensors;
  cache_desc.data_type = DT_INT32;
  cache_desc.shape = kTensorShape;
  std::vector<uint64_t> tensor_addrs;
  for (uint32_t i = 0U; i < kNumTensors; ++i) {
    int32_t *buffer = nullptr;
    CHECK_ACL(aclrtMalloc((void **)&buffer, kTensorSize, ACL_MEM_MALLOC_HUGE_ONLY));
    tensor_addrs.emplace_back(reinterpret_cast<uint64_t>(buffer));
    buffers.emplace_back(reinterpret_cast<void *>(buffer));
  }
  auto ret = llm_datadist.RegisterKvCache(cache_desc, tensor_addrs, {}, cache_id);
  if (ret != LLM_SUCCESS) {
    printf("[ERROR] RegisterKvCache failed, ret = %u, errmsg: %s\n", ret, GetRecentErrMsg());
    return -1;
  }
  // RegisterKvCache成功后，可以获取cache中各tensor的地址用于后续操作
  printf("[INFO] RegisterKvCache success\n");
  for (size_t i = 0U; i < tensor_addrs.size(); ++i) {
    printf("[INFO] Tensor[%zu] addr = %p\n", i, reinterpret_cast<void *>(tensor_addrs[i]));
  }
  return 0;
}

int32_t RunDecoderSample(const char *device_id, const char *local_ip, const char *remote_ip) {
  printf("[INFO] Decoder Sample start\n");
  // 1. 初始化
  LlmDataDist llm_datadist(kDecoderClusterId, LlmRole::kDecoder);
  if (Initialize(llm_datadist, device_id) != 0) {
    return -1;
  }

  // 2. 注册内存地址
  std::vector<void *> buffers;
  int64_t cache_id = -1;
  bool linked = false;
  if (RegisterCache(llm_datadist, buffers, cache_id) != 0) {
    Finalize(llm_datadist, cache_id, linked, remote_ip, buffers);
    return -1;
  }

  // 3. 等待prompt写完cache，实际业务场景可通过合适方式实现通知
  std::this_thread::sleep_for(std::chrono::seconds(kWaitPromptInitTime));

  // 4. 与prompt建链
  if (Link(llm_datadist, local_ip, remote_ip) != 0) {
    Finalize(llm_datadist, cache_id, linked, remote_ip, buffers);
    return -1;
  }
  linked = true;

  // 5. 从prompt拉取cache
  if (PullCache(llm_datadist, cache_id) != 0) {
    Finalize(llm_datadist, cache_id, linked, remote_ip, buffers);
    return -1;
  }

  if (CheckBuffers(buffers, {0, 1, 2, 3}) != 0) {
    Finalize(llm_datadist, cache_id, linked, remote_ip, buffers);
    return -1;
  }

  // 6. 解除链路
  if (Unlink(llm_datadist, remote_ip) != 0) {
    Finalize(llm_datadist, cache_id, linked, remote_ip, buffers);
    return -1;
  }
  linked = false;

  // 7. 切换角色
  if (SetRole(llm_datadist, LlmRole::kPrompt, local_ip) != 0) {
    Finalize(llm_datadist, cache_id, linked, remote_ip, buffers);
    return -1;
  }

  // 8. 等待prompt push cache，实际业务场景可通过合适方式实现通知
  std::this_thread::sleep_for(std::chrono::seconds(kWaitPromptPushTime));

  if (CheckBuffers(buffers, {4, 5, 6, 7}) != 0) {
    Finalize(llm_datadist, cache_id, linked, remote_ip, buffers);
    return -1;
  }

  // 9. 释放cache与llmDataDist
  llm_datadist.Finalize();
  printf("[INFO] Finalize success\n");
  printf("[INFO] Decoder Sample end\n");
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
  auto ret = RunDecoderSample(device_id, local_ip, remote_ip);
  return ret;
}