/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <thread>
#include <string>
#include <vector>
#include "acl/acl.h"
#include "hixl/hixl.h"

using namespace hixl;
namespace {
constexpr size_t kBufferSize = 8 * 1024 * 1024;
constexpr size_t kChunkSize = 16 * 1024;
constexpr size_t kChunkCount = kBufferSize / kChunkSize;
constexpr int32_t kDefaultDevA = 0;
constexpr int32_t kDefaultDevB = 2;
constexpr int32_t kVersionLegacy = 0;
constexpr const char *kEngineA = "127.0.0.1:16000";
constexpr const char *kEngineB = "127.0.0.1:16001";
constexpr uint8_t kFillA = 0xAA;
constexpr uint8_t kFillB = 0xBB;
constexpr int32_t kConnTimeout = 5000;
constexpr int32_t kMaxPollCount = 100000;
static const std::vector<std::string> kSupportedProtocols = {"roce:device", "roce:host",     "uboe:device",
                                                             "ubg:device",  "ub_ctp:device", "ub_tp:device",
                                                             "ub_ctp:host", "ub_tp:host"};

#define CHECK_ACL(x)                                                                       \
  do {                                                                                     \
    aclError __err_code = x;                                                               \
    if (__err_code != ACL_ERROR_NONE) {                                                    \
      std::cerr << __FILE__ << ":" << __LINE__ << " aclError:" << __err_code << std::endl; \
    }                                                                                      \
  } while (0)

struct EngineCtx {
  Hixl engine;
  int32_t device_id = 0;
  const char *name = nullptr;
  bool initialized = false;
  bool connected = false;
  void *dev_buf = nullptr;
  void *host_buf = nullptr;
  MemHandle dev_handle = nullptr;
  MemHandle host_handle = nullptr;
};

const char *GetRecentErrMsg() {
  const char *errmsg = aclGetRecentErrMsg();
  return (errmsg == nullptr) ? "no error" : errmsg;
}

void TokenizeProtocols(const std::string &val, std::vector<std::string> &protocols) {
  size_t start = 0;
  size_t pos = val.find(',');
  while (pos != std::string::npos) {
    protocols.push_back(val.substr(start, pos - start));
    start = pos + 1;
    pos = val.find(',', start);
  }
  protocols.push_back(val.substr(start));
}

int32_t VerifyProtocolSupport(const std::string &proto) {
  for (const auto &item : kSupportedProtocols) {
    if (item == proto) {
      return 0;
    }
  }
  printf("[ERROR] Invalid protocol: %s\n", proto.c_str());
  printf("Supported:");
  for (const auto &item : kSupportedProtocols) {
    printf(" %s", item.c_str());
  }
  printf("\n");
  return -1;
}

int32_t ParseArgs(int32_t argc, char **argv, int32_t &device_a, int32_t &device_b, std::vector<std::string> &protocols,
                  int32_t &version) {
  for (int32_t i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg.find("--device=") == 0) {
      std::string device_str = arg.substr(9);
      auto comma_pos = device_str.find(',');
      if (comma_pos == std::string::npos) {
        printf("[ERROR] Invalid --device format, expected id1,id2\n");
        return -1;
      }
      device_a = std::stoi(device_str.substr(0, comma_pos));
      device_b = std::stoi(device_str.substr(comma_pos + 1));
    } else if (arg.find("--protocol=") == 0) {
      TokenizeProtocols(arg.substr(11), protocols);
    } else if (arg.find("--version=") == 0) {
      version = std::stoi(arg.substr(10));
    } else {
      printf("[ERROR] Unknown argument: %s\n", arg.c_str());
      printf("Usage: %s --protocol=<type>[,...] [--device=id1,id2] [--version=0|1]\n", argv[0]);
      return -1;
    }
  }

  if (protocols.empty()) {
    printf("[ERROR] --protocol is required\n");
    return -1;
  }

  for (const auto &proto : protocols) {
    if (VerifyProtocolSupport(proto) != 0) {
      return -1;
    }
  }

  bool is_legacy = (version == kVersionLegacy);
  bool single_roce = (protocols.size() == 1 && protocols[0] == "roce:device");
  if (is_legacy && !single_roce) {
    printf("[ERROR] version 0 only supports roce:device\n");
    return -1;
  }

  printf("[INFO] ParseArgs success: device_a=%d, device_b=%d, version=%d\n", device_a, device_b, version);
  for (const auto &proto : protocols) {
    printf("[INFO]   protocol: %s\n", proto.c_str());
  }
  return 0;
}

int32_t BuildLegacyConfig(EngineCtx &ctx, const std::vector<std::string> &protocols,
                          std::map<AscendString, AscendString> &options) {
  printf("[INFO] %s using legacy flow (version=0)\n", ctx.name);
  std::string eng_name(ctx.name);
  auto sep = eng_name.find(':');
  uint32_t listen_port = std::stoi(eng_name.substr(sep + 1));
  std::string lcomm = "{\"version\": \"1.2\"}";
  options[OPTION_LOCAL_COMM_RES] = lcomm.c_str();
  std::string res_cfg = "{\"comm_resource_config.listen_port\": " + std::to_string(listen_port) + "}";
  options[OPTION_GLOBAL_RESOURCE_CONFIG] = res_cfg.c_str();
  if (protocols[0] == "roce:device") {
    options[OPTION_BUFFER_POOL] = "0:0";
    setenv("HCCL_INTRA_ROCE_ENABLE", "1", 1);
  }
  return 0;
}

int32_t BuildV2Config(const std::vector<std::string> &protocols, std::map<AscendString, AscendString> &options) {
  std::string proto_list;
  bool first = true;
  for (const auto &proto : protocols) {
    if (!first) {
      proto_list += ",";
    }
    proto_list += "\"" + proto + "\"";
    first = false;
  }
  std::string res_config = "{\"comm_resource_config.protocol_desc\": [" + proto_list + "]}";
  options[OPTION_GLOBAL_RESOURCE_CONFIG] = res_config.c_str();
  return 0;
}

int32_t InitEngine(EngineCtx &ctx, const std::vector<std::string> &protocols, int32_t version, uint8_t fill_value) {
  CHECK_ACL(aclrtSetDevice(ctx.device_id));
  std::map<AscendString, AscendString> options;
  if (version == kVersionLegacy) {
    BuildLegacyConfig(ctx, protocols, options);
  } else {
    BuildV2Config(protocols, options);
  }
  auto ret = ctx.engine.Initialize(ctx.name, options);
  if (ret != SUCCESS) {
    printf("[ERROR] Initialize %s failed, ret=%u, errmsg:%s\n", ctx.name, ret, GetRecentErrMsg());
    return -1;
  }
  ctx.initialized = true;
  printf("[INFO] InitEngine %s success\n", ctx.name);
  uint8_t *dev_ptr = nullptr;
  CHECK_ACL(aclrtMalloc(reinterpret_cast<void **>(&dev_ptr), kBufferSize, ACL_MEM_MALLOC_HUGE_ONLY));
  ctx.dev_buf = dev_ptr;
  CHECK_ACL(aclrtMallocHost(&ctx.host_buf, kBufferSize));
  std::fill(static_cast<uint8_t *>(ctx.host_buf), static_cast<uint8_t *>(ctx.host_buf) + kBufferSize, 0);
  MemDesc desc{};
  desc.addr = reinterpret_cast<uintptr_t>(ctx.dev_buf);
  desc.len = kBufferSize;
  ret = ctx.engine.RegisterMem(desc, MEM_DEVICE, ctx.dev_handle);
  if (ret != SUCCESS) {
    printf("[ERROR] %s RegisterMem device failed, ret=%u, errmsg:%s\n", ctx.name, ret, GetRecentErrMsg());
    return -1;
  }
  desc.addr = reinterpret_cast<uintptr_t>(ctx.host_buf);
  ret = ctx.engine.RegisterMem(desc, MEM_HOST, ctx.host_handle);
  if (ret != SUCCESS) {
    printf("[ERROR] %s RegisterMem host failed, ret=%u, errmsg:%s\n", ctx.name, ret, GetRecentErrMsg());
    return -1;
  }
  std::fill(static_cast<uint8_t *>(ctx.host_buf), static_cast<uint8_t *>(ctx.host_buf) + kBufferSize, fill_value);
  CHECK_ACL(aclrtMemcpy(ctx.dev_buf, kBufferSize, ctx.host_buf, kBufferSize, ACL_MEMCPY_HOST_TO_DEVICE));
  printf("[INFO] %s InitEngine success, dev:%p, host:%p\n", ctx.name, ctx.dev_buf, ctx.host_buf);
  return 0;
}

int32_t Connect(EngineCtx &ctx_a, EngineCtx &ctx_b) {
  auto ret = ctx_a.engine.ConnectAsync(ctx_b.name, kConnTimeout);
  if (ret != SUCCESS) {
    printf("[ERROR] ConnectAsync %s->%s failed, ret=%u, errmsg:%s\n", ctx_a.name, ctx_b.name, ret, GetRecentErrMsg());
    return -1;
  }
  ret = ctx_b.engine.ConnectAsync(ctx_a.name, kConnTimeout);
  if (ret != SUCCESS) {
    printf("[ERROR] ConnectAsync %s->%s failed, ret=%u, errmsg:%s\n", ctx_b.name, ctx_a.name, ret, GetRecentErrMsg());
    return -1;
  }
  AsyncConnectStatus status_a = AsyncConnectStatus::NOT_CONNECT;
  AsyncConnectStatus status_b = AsyncConnectStatus::NOT_CONNECT;
  while (status_a != AsyncConnectStatus::CONNECTED || status_b != AsyncConnectStatus::CONNECTED) {
    ctx_a.engine.GetAsyncConnectStatus(ctx_b.name, status_a);
    ctx_b.engine.GetAsyncConnectStatus(ctx_a.name, status_b);
    if (status_a == AsyncConnectStatus::CONNECT_FAILED || status_b == AsyncConnectStatus::CONNECT_FAILED) {
      printf("[ERROR] Connect failed, status_a=%d, status_b=%d\n", static_cast<int>(status_a),
             static_cast<int>(status_b));
      return -1;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  ctx_a.connected = true;
  ctx_b.connected = true;
  printf("[INFO] Connect success\n");
  return 0;
}

void BuildTransferDescs(std::vector<TransferOpDesc> &descs, EngineCtx &ctx_a, EngineCtx &ctx_b) {
  descs.reserve(kChunkCount);
  for (size_t i = 0; i < kChunkCount; ++i) {
    TransferOpDesc desc{};
    desc.local_addr = reinterpret_cast<uintptr_t>(ctx_a.dev_buf) + i * kChunkSize;
    desc.remote_addr = reinterpret_cast<uintptr_t>(ctx_b.host_buf) + i * kChunkSize;
    desc.len = kChunkSize;
    descs.push_back(desc);
  }
}

int32_t StartTransfers(EngineCtx &ctx_a, EngineCtx &ctx_b, std::vector<TransferOpDesc> &descs, TransferReq &req_a,
                       TransferReq &req_b) {
  TransferArgs args{};
  req_a = nullptr;
  auto ret = ctx_a.engine.TransferAsync(ctx_b.name, WRITE, descs, args, req_a);
  if (ret != SUCCESS) {
    printf("[ERROR] TransferAsync %s->%s failed, ret=%u, errmsg:%s\n", ctx_a.name, ctx_b.name, ret, GetRecentErrMsg());
    return -1;
  }

  // Update descriptors for B->A transfer
  for (size_t i = 0; i < kChunkCount; ++i) {
    descs[i].local_addr = reinterpret_cast<uintptr_t>(ctx_b.dev_buf) + i * kChunkSize;
    descs[i].remote_addr = reinterpret_cast<uintptr_t>(ctx_a.host_buf) + i * kChunkSize;
  }

  req_b = nullptr;
  ret = ctx_b.engine.TransferAsync(ctx_a.name, WRITE, descs, args, req_b);
  if (ret != SUCCESS) {
    printf("[ERROR] TransferAsync %s->%s failed, ret=%u, errmsg:%s\n", ctx_b.name, ctx_a.name, ret, GetRecentErrMsg());
    return -1;
  }
  return 0;
}

int32_t WaitTransfers(EngineCtx &ctx_a, EngineCtx &ctx_b, TransferReq req_a, TransferReq req_b) {
  TransferStatus st_a = TransferStatus::WAITING;
  TransferStatus st_b = TransferStatus::WAITING;
  int32_t poll_count = 0;
  while (st_a == TransferStatus::WAITING || st_b == TransferStatus::WAITING) {
    if (st_a == TransferStatus::WAITING) {
      ctx_a.engine.GetTransferStatus(req_a, st_a);
    }
    if (st_b == TransferStatus::WAITING) {
      ctx_b.engine.GetTransferStatus(req_b, st_b);
    }
    if (++poll_count > kMaxPollCount) {
      printf("[ERROR] Transfer poll timeout\n");
      return -1;
    }
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }
  if (st_a != TransferStatus::COMPLETED) {
    printf("[ERROR] Transfer %s->%s failed, status=%d\n", ctx_a.name, ctx_b.name, static_cast<int>(st_a));
    return -1;
  }
  if (st_b != TransferStatus::COMPLETED) {
    printf("[ERROR] Transfer %s->%s failed, status=%d\n", ctx_b.name, ctx_a.name, static_cast<int>(st_b));
    return -1;
  }
  printf("[INFO] Transfer completed\n");
  return 0;
}

int32_t Transfer(EngineCtx &ctx_a, EngineCtx &ctx_b) {
  std::vector<TransferOpDesc> descs;
  BuildTransferDescs(descs, ctx_a, ctx_b);
  TransferReq req_a = nullptr;
  TransferReq req_b = nullptr;
  if (StartTransfers(ctx_a, ctx_b, descs, req_a, req_b) != 0) {
    return -1;
  }
  return WaitTransfers(ctx_a, ctx_b, req_a, req_b);
}

int32_t Verify(EngineCtx &ctx_a, EngineCtx &ctx_b) {
  std::vector<uint8_t> expected_a(kBufferSize, kFillA);
  std::vector<uint8_t> expected_b(kBufferSize, kFillB);
  if (std::memcmp(ctx_b.host_buf, expected_a.data(), kBufferSize) != 0) {
    printf("[ERROR] Verify %s host failed, expected 0xAA\n", ctx_b.name);
    return -1;
  }
  if (std::memcmp(ctx_a.host_buf, expected_b.data(), kBufferSize) != 0) {
    printf("[ERROR] Verify %s host failed, expected 0xBB\n", ctx_a.name);
    return -1;
  }
  printf("[INFO] Verify success\n");
  return 0;
}

void DisconnectBoth(EngineCtx &ctx_a, EngineCtx &ctx_b) {
  if (ctx_a.connected) {
    auto ret = ctx_a.engine.DisconnectAsync(ctx_b.name, kConnTimeout);
    if (ret != SUCCESS) {
      printf("[ERROR] DisconnectAsync %s->%s failed, ret=%u, errmsg:%s\n", ctx_a.name, ctx_b.name, ret,
             GetRecentErrMsg());
    }
  }
  if (ctx_b.connected) {
    auto ret = ctx_b.engine.DisconnectAsync(ctx_a.name, kConnTimeout);
    if (ret != SUCCESS) {
      printf("[ERROR] DisconnectAsync %s->%s failed, ret=%u, errmsg:%s\n", ctx_b.name, ctx_a.name, ret,
             GetRecentErrMsg());
    }
  }
  if (ctx_a.connected || ctx_b.connected) {
    AsyncConnectStatus st_a = AsyncConnectStatus::DISCONNECT_PENDING;
    AsyncConnectStatus st_b = AsyncConnectStatus::DISCONNECT_PENDING;
    while (st_a != AsyncConnectStatus::NOT_CONNECT || st_b != AsyncConnectStatus::NOT_CONNECT) {
      ctx_a.engine.GetAsyncConnectStatus(ctx_b.name, st_a);
      ctx_b.engine.GetAsyncConnectStatus(ctx_a.name, st_b);
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    printf("[INFO] Disconnect success\n");
  }
}

void CleanupResources(EngineCtx &ctx) {
  if (ctx.dev_handle != nullptr) {
    ctx.engine.DeregisterMem(ctx.dev_handle);
  }
  if (ctx.host_handle != nullptr) {
    ctx.engine.DeregisterMem(ctx.host_handle);
  }
  if (ctx.dev_buf != nullptr) {
    CHECK_ACL(aclrtFree(ctx.dev_buf));
  }
  if (ctx.host_buf != nullptr) {
    CHECK_ACL(aclrtFreeHost(ctx.host_buf));
  }
}

void ShutdownEngines(EngineCtx &ctx_a, EngineCtx &ctx_b) {
  if (ctx_a.initialized) {
    ctx_a.engine.Finalize();
  }
  if (ctx_b.initialized) {
    ctx_b.engine.Finalize();
  }
}

void RestoreDevices(EngineCtx &ctx_a, EngineCtx &ctx_b) {
  CHECK_ACL(aclrtResetDevice(ctx_a.device_id));
  CHECK_ACL(aclrtResetDevice(ctx_b.device_id));
}

void Finalize(EngineCtx &ctx_a, EngineCtx &ctx_b) {
  DisconnectBoth(ctx_a, ctx_b);
  CleanupResources(ctx_a);
  CleanupResources(ctx_b);
  ShutdownEngines(ctx_a, ctx_b);
  RestoreDevices(ctx_a, ctx_b);
}

int32_t Run(EngineCtx &ctx_a, EngineCtx &ctx_b, const std::vector<std::string> &protocols, int32_t version) {
  if (InitEngine(ctx_a, protocols, version, kFillA) != 0) {
    return -1;
  }
  if (InitEngine(ctx_b, protocols, version, kFillB) != 0) {
    return -1;
  }
  if (Connect(ctx_a, ctx_b) != 0) {
    return -1;
  }
  if (Transfer(ctx_a, ctx_b) != 0) {
    return -1;
  }
  return Verify(ctx_a, ctx_b);
}
}  // namespace

int main(int32_t argc, char **argv) {
  std::vector<std::string> protocols;
  int32_t version = 1;
  int32_t device_a = kDefaultDevA;
  int32_t device_b = kDefaultDevB;

  if (ParseArgs(argc, argv, device_a, device_b, protocols, version) != 0) {
    return -1;
  }

  EngineCtx ctx_a;
  EngineCtx ctx_b;

  ctx_a.name = kEngineA;
  ctx_a.device_id = device_a;
  ctx_b.name = kEngineB;
  ctx_b.device_id = device_b;

  int32_t ret = Run(ctx_a, ctx_b, protocols, version);
  Finalize(ctx_a, ctx_b);
  return ret;
}
