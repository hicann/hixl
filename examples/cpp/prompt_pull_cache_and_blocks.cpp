/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <numeric>
#include <cstdio>
#include <thread>
#include <iostream>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include "acl/acl.h"
#include "llm_datadist/llm_datadist.h"

using namespace llm_datadist;
namespace {
constexpr uint16_t kPromptListenPort = 26000;
constexpr uint16_t kPromptControlPort = 26002;
constexpr uint16_t kPromptClusterId = 0;
constexpr uint32_t kNumTensors = 4U;
constexpr size_t kTensorSize = 8 * 16 * sizeof(int32_t);
const std::vector<int64_t> kTensorShape = {8, 16};
constexpr int32_t kControlTimeoutSec = 60;
constexpr int32_t kSocketBacklog = 1;
constexpr char kUnlinkDoneMessage = '1';
constexpr int32_t kExpectedArgCnt = 4;
constexpr uint32_t kArgIndexDeviceId = 1;
constexpr uint32_t kArgIndexLocalIp = 2;
constexpr uint32_t kArgIndexLocalCommRes = 3;

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

void CloseSocket(int32_t fd) {
  if (fd >= 0) {
    (void)close(fd);
  }
}

int32_t CreateControlServer(const char *local_ip, uint16_t port) {
  int32_t listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd < 0) {
    printf("[ERROR] Create control socket failed\n");
    return -1;
  }
  int32_t reuse = 1;
  (void)setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (inet_pton(AF_INET, local_ip, &addr.sin_addr) != 1) {
    printf("[ERROR] Parse control ip failed, ip = %s\n", local_ip);
    CloseSocket(listen_fd);
    return -1;
  }
  if (bind(listen_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
    printf("[ERROR] Bind control socket failed, ip = %s, port = %u\n", local_ip, static_cast<unsigned int>(port));
    CloseSocket(listen_fd);
    return -1;
  }
  if (listen(listen_fd, kSocketBacklog) != 0) {
    printf("[ERROR] Listen control socket failed, ip = %s, port = %u\n", local_ip, static_cast<unsigned int>(port));
    CloseSocket(listen_fd);
    return -1;
  }
  return listen_fd;
}

int32_t WaitUnlinkDone(const char *local_ip) {
  int32_t listen_fd = CreateControlServer(local_ip, kPromptControlPort);
  if (listen_fd < 0) {
    return -1;
  }
  fd_set read_fds;
  FD_ZERO(&read_fds);
  FD_SET(listen_fd, &read_fds);
  timeval timeout{};
  timeout.tv_sec = kControlTimeoutSec;
  int32_t ret = select(listen_fd + 1, &read_fds, nullptr, nullptr, &timeout);
  if (ret <= 0) {
    printf("[ERROR] Wait decoder unlink done %s\n", ret == 0 ? "timeout" : "failed");
    CloseSocket(listen_fd);
    return -1;
  }
  int32_t conn_fd = accept(listen_fd, nullptr, nullptr);
  if (conn_fd < 0) {
    printf("[ERROR] Accept control socket failed\n");
    CloseSocket(listen_fd);
    return -1;
  }
  char message = 0;
  auto nread = recv(conn_fd, &message, sizeof(message), 0);
  CloseSocket(conn_fd);
  CloseSocket(listen_fd);
  if (nread != static_cast<ssize_t>(sizeof(message)) || message != kUnlinkDoneMessage) {
    printf("[ERROR] Receive decoder unlink done failed\n");
    return -1;
  }
  printf("[INFO] Wait decoder unlink done success\n");
  return 0;
}
}  // namespace

int Initialize(LlmDataDist &llm_datadist, const std::string &device_id, const std::string &local_ip,
               const std::string &local_comm_res) {
  std::map<AscendString, AscendString> options;
  options[OPTION_DEVICE_ID] = device_id.c_str();
  options[OPTION_LISTEN_IP_INFO] = (local_ip + ":" + std::to_string(kPromptListenPort)).c_str();
  if (!local_comm_res.empty()) {
    options[OPTION_TRANSFER_BACKEND] = "hixl";
    options[OPTION_LOCAL_COMM_RES] = local_comm_res.c_str();
  }
  auto ret = llm_datadist.Initialize(options);
  if (ret != LLM_SUCCESS) {
    printf("[ERROR] Initialize failed, ret = %u, errmsg: %s\n", ret, GetRecentErrMsg());
    return -1;
  }
  printf("[INFO] Initialize success\n");
  return LLM_SUCCESS;
}

void Finalize(LlmDataDist &llm_datadist, int64_t cache_id, const std::vector<void *> buffers) {
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

int32_t RunPromptSample(const char *device_id, const char *local_ip, const std::string &local_comm_res) {
  printf("[INFO] Prompt Sample start\n");
  // 1. 初始化
  LlmDataDist llm_datadist(kPromptClusterId, LlmRole::kPrompt);
  if (Initialize(llm_datadist, device_id, local_ip, local_comm_res) != 0) {
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
  auto ret = llm_datadist.RegisterKvCache(cache_desc, tensor_addrs, {}, cache_id);
  if (ret != LLM_SUCCESS) {
    printf("[ERROR] RegisterKvCache failed, ret = %u, errmsg: %s\n", ret, GetRecentErrMsg());
    Finalize(llm_datadist, cache_id, buffers);
    return -1;
  }
  // 3. RegisterKvCache成功后，可以获取cache中各tensor的地址用于后续操作
  printf("[INFO] RegisterKvCache success\n");
  for (size_t i = 0U; i < tensor_addrs.size(); ++i) {
    printf("[INFO] Tensor[%zu] addr = %p\n", i, reinterpret_cast<void *>(tensor_addrs[i]));
  }

  // 4. 等待decoder完成UnlinkLlmClusters后再释放本端cache
  if (WaitUnlinkDone(local_ip) != 0) {
    Finalize(llm_datadist, cache_id, buffers);
    return -1;
  }

  // 5. 释放Cache与llmDataDist
  Finalize(llm_datadist, cache_id, buffers);
  printf("[INFO] Prompt Sample end\n");
  return 0;
}

int main(int32_t argc, char **argv) {
  if (argc < kExpectedArgCnt - 1 || argc > kExpectedArgCnt) {
    printf("[ERROR] expect at least 3 args(device_id, localHostIp, [local_comm_res]), but got %d\n", argc - 1);
    return -1;
  }
  const auto device_id = argv[kArgIndexDeviceId];
  const auto local_ip = argv[kArgIndexLocalIp];
  printf("[INFO] device_id = %s, local_ip = %s\n", device_id, local_ip);
  std::string local_comm_res;
  if (argc == kExpectedArgCnt) {
    local_comm_res = argv[kArgIndexLocalCommRes];
    printf("[INFO] local_comm_res = %s\n", local_comm_res.c_str());
  }
  auto ret = RunPromptSample(device_id, local_ip, local_comm_res);
  return ret;
}
