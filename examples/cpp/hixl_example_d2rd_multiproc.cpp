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
#include <string>
#include <vector>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "acl/acl.h"
#include "hixl/hixl.h"

using namespace hixl;
namespace {
constexpr size_t kBufSize = 8 * 1024 * 1024;
constexpr size_t kBlockSize = 16 * 1024;
constexpr size_t kBlockCount = kBufSize / kBlockSize;
constexpr int32_t kDefaultDeviceClient = 0;
constexpr int32_t kDefaultDeviceServer = 2;
constexpr int32_t kVersionLegacy = 0;
constexpr const char *kDefaultClientEngine = "127.0.0.1:16000";
constexpr const char *kDefaultServerEngine = "127.0.0.1:16001";
constexpr int32_t kConnectTimeout = 5000;
constexpr int32_t kTransferTimeout = 30000;
constexpr int32_t kSocketPortOffset = 1000;
constexpr int32_t kSocketBacklog = 1;
constexpr int32_t kSocketRetryCount = 10;
constexpr int32_t kSocketRetryIntervalUs = 500000;
constexpr uint8_t kFillValue = 0xAA;
static const std::vector<std::string> protocolList = {"hccs:device", "roce:device",   "roce:host",   "uboe:device",
                                                      "ubg:device",  "ub_ctp:device", "ub_tp:device"};

#define CHECK_ACL(x)                                                                     \
  do {                                                                                   \
    aclError __status = x;                                                               \
    if (__status != ACL_ERROR_NONE) {                                                    \
      std::cerr << __FILE__ << ":" << __LINE__ << " aclError:" << __status << std::endl; \
    }                                                                                    \
  } while (0)

struct EngineCtx {
  Hixl engine;
  int32_t device_id = 0;
  std::string local_engine;
  std::string remote_engine;
  bool is_client = false;
  bool initialized = false;
  bool connected = false;
  void *dev_buf = nullptr;
  MemHandle dev_handle = nullptr;
  uintptr_t remote_addr = 0;
  int listen_fd = -1;
  int conn_fd = -1;
};

const char *GetRecentErrMsg() {
  const char *err_msg = aclGetRecentErrMsg();
  return (err_msg == nullptr) ? "no error" : err_msg;
}

int32_t ParsePort(const std::string &engine) {
  auto pos = engine.find(':');
  if (pos == std::string::npos) {
    return -1;
  }
  return std::stoi(engine.substr(pos + 1));
}

void ParseProtoCSV(const std::string &val, std::vector<std::string> &protocols) {
  size_t offset = 0;
  size_t delimiter = val.find(',');
  while (delimiter != std::string::npos) {
    protocols.push_back(val.substr(offset, delimiter - offset));
    offset = delimiter + 1;
    delimiter = val.find(',', offset);
  }
  protocols.push_back(val.substr(offset));
}

int32_t ValidateProtoName(const std::string &proto) {
  for (const auto &proto_item : protocolList) {
    if (proto_item == proto) {
      return 0;
    }
  }
  printf("[ERROR] Invalid protocol: %s\n", proto.c_str());
  printf("Supported:");
  for (const auto &proto_item : protocolList) {
    printf(" %s", proto_item.c_str());
  }
  printf("\n");
  return -1;
}

int32_t ParseSingleArg(const std::string &arg, std::string &role, int32_t &device_id, std::string &local_engine,
                       std::string &remote_engine, std::vector<std::string> &protocols, int32_t &version,
                       const char *prog_name) {
  if (arg.find("--role=") == 0) {
    role = arg.substr(7);
  } else if (arg.find("--device=") == 0) {
    device_id = std::stoi(arg.substr(9));
  } else if (arg.find("--local-engine=") == 0) {
    local_engine = arg.substr(15);
  } else if (arg.find("--remote-engine=") == 0) {
    remote_engine = arg.substr(16);
  } else if (arg.find("--protocol=") == 0) {
    ParseProtoCSV(arg.substr(11), protocols);
  } else if (arg.find("--version=") == 0) {
    version = std::stoi(arg.substr(10));
  } else {
    printf("[ERROR] Unknown argument: %s\n", arg.c_str());
    printf(
        "Usage: %s --role=client|server --protocol=<type>[,...] "
        "[--device=<id>] [--local-engine=<ip:port>] [--remote-engine=<ip:port>] "
        "[--version=0|1]\n",
        prog_name);
    return -1;
  }
  return 0;
}

int32_t ValidateArgs(const std::string &role, const std::vector<std::string> &protocols, int32_t version,
                     EngineCtx &ctx) {
  if (role != "client" && role != "server") {
    printf("[ERROR] --role is required and must be 'client' or 'server'\n");
    return -1;
  }
  if (protocols.empty()) {
    printf("[ERROR] --protocol is required\n");
    return -1;
  }
  ctx.is_client = (role == "client");
  for (const auto &p : protocols) {
    if (ValidateProtoName(p) != 0) {
      return -1;
    }
  }
  if (version == kVersionLegacy &&
      (protocols.size() != 1 || (protocols[0] != "roce:device" && protocols[0] != "hccs:device"))) {
    printf("[ERROR] version 0 only supports roce:device and hccs:device\n");
    return -1;
  }
  if (ParsePort(ctx.local_engine) < 0 || ParsePort(ctx.remote_engine) < 0) {
    printf("[ERROR] Invalid engine address format, expected ip:port\n");
    return -1;
  }
  return 0;
}

int32_t ParseArgs(int32_t argc, char **argv, EngineCtx &ctx, std::vector<std::string> &protocols, int32_t &version) {
  std::string role;
  int32_t device_id = -1;
  std::string local_engine;
  std::string remote_engine;

  // Parse all command line arguments
  for (int32_t i = 1; i < argc; ++i) {
    if (ParseSingleArg(argv[i], role, device_id, local_engine, remote_engine, protocols, version, argv[0]) != 0) {
      return -1;
    }
  }

  // Set context based on role
  ctx.is_client = (role == "client");
  ctx.device_id = (device_id >= 0) ? device_id : (ctx.is_client ? kDefaultDeviceClient : kDefaultDeviceServer);
  ctx.local_engine =
      local_engine.empty() ? (ctx.is_client ? kDefaultClientEngine : kDefaultServerEngine) : local_engine;
  ctx.remote_engine =
      remote_engine.empty() ? (ctx.is_client ? kDefaultServerEngine : kDefaultClientEngine) : remote_engine;

  // Validate all parameters
  if (ValidateArgs(role, protocols, version, ctx) != 0) {
    return -1;
  }

  // Display final configuration
  printf("[INFO] ParseArgs: role=%s, device=%d\n", role.c_str(), ctx.device_id);
  printf("[INFO]   local=%s, remote=%s\n", ctx.local_engine.c_str(), ctx.remote_engine.c_str());
  printf("[INFO]   version=%d\n", version);
  for (const auto &p : protocols) {
    printf("[INFO]   protocol: %s\n", p.c_str());
  }
  return 0;
}

int32_t PrepareLegacyOpts(const std::vector<std::string> &protocols, std::map<AscendString, AscendString> &options) {
  printf("[INFO] Using legacy flow (version=0)\n");
  std::string local_comm_res = "{\"version\": \"1.2\"}";
  options[OPTION_LOCAL_COMM_RES] = local_comm_res.c_str();
  options[OPTION_BUFFER_POOL] = "0:0";
  if (protocols[0] == "roce:device") {
    setenv("HCCL_INTRA_ROCE_ENABLE", "1", 1);
  }
  return 0;
}

int32_t PrepareV2Opts(const std::vector<std::string> &protocols, std::map<AscendString, AscendString> &options) {
  std::string desc_array;
  for (size_t i = 0; i < protocols.size(); ++i) {
    if (i > 0) {
      desc_array += ",";
    }
    desc_array += "\"" + protocols[i] + "\"";
  }
  std::string resource_config = "{\"comm_resource_config.protocol_desc\": [" + desc_array + "]}";
  options[OPTION_GLOBAL_RESOURCE_CONFIG] = resource_config.c_str();
  return 0;
}

int32_t InitEngine(EngineCtx &ctx, const std::vector<std::string> &protocols, int32_t version) {
  CHECK_ACL(aclrtSetDevice(ctx.device_id));
  std::map<AscendString, AscendString> options;
  if (version == kVersionLegacy) {
    PrepareLegacyOpts(protocols, options);
  } else {
    PrepareV2Opts(protocols, options);
  }
  auto ret = ctx.engine.Initialize(ctx.local_engine.c_str(), options);
  if (ret != SUCCESS) {
    printf("[ERROR] Initialize failed, ret=%u, errmsg:%s\n", ret, GetRecentErrMsg());
    return -1;
  }
  ctx.initialized = true;
  uint8_t *dev_ptr = nullptr;
  CHECK_ACL(aclrtMalloc(reinterpret_cast<void **>(&dev_ptr), kBufSize, ACL_MEM_MALLOC_HUGE_ONLY));
  ctx.dev_buf = dev_ptr;
  MemDesc desc{};
  desc.addr = reinterpret_cast<uintptr_t>(ctx.dev_buf);
  desc.len = kBufSize;
  ret = ctx.engine.RegisterMem(desc, MEM_DEVICE, ctx.dev_handle);
  if (ret != SUCCESS) {
    printf("[ERROR] RegisterMem failed, ret=%u, errmsg:%s\n", ret, GetRecentErrMsg());
    return -1;
  }
  if (!ctx.is_client) {
    std::vector<uint8_t> fill_data(kBufSize, kFillValue);
    CHECK_ACL(aclrtMemcpy(ctx.dev_buf, kBufSize, fill_data.data(), kBufSize, ACL_MEMCPY_HOST_TO_DEVICE));
  }
  printf("[INFO] InitEngine success, dev:%p\n", ctx.dev_buf);
  return 0;
}

int32_t GetRemoteAddr(EngineCtx &ctx) {
  int port = ParsePort(ctx.remote_engine) + kSocketPortOffset;
  std::string ip = ctx.remote_engine.substr(0, ctx.remote_engine.find(':'));
  int fd = -1;
  for (int32_t retry = 0; retry < kSocketRetryCount; ++retry) {
    fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
    if (connect(fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) == 0) {
      break;
    }
    close(fd);
    fd = -1;
    if (retry + 1 < kSocketRetryCount) {
      printf("[INFO] Connect retry %d/%d...\n", retry + 1, kSocketRetryCount);
      usleep(kSocketRetryIntervalUs);
    }
  }
  if (fd < 0) {
    printf("[ERROR] Failed to connect to server on port %d\n", port);
    return -1;
  }
  ssize_t received = 0;
  while (received < static_cast<ssize_t>(sizeof(ctx.remote_addr))) {
    ssize_t n = recv(fd, reinterpret_cast<char *>(&ctx.remote_addr) + received, sizeof(ctx.remote_addr) - received, 0);
    if (n <= 0) {
      printf("[ERROR] recv failed\n");
      close(fd);
      return -1;
    }
    received += n;
  }
  ctx.conn_fd = fd;
  printf("[INFO] GetRemoteAddr success, remote_addr:%p\n", reinterpret_cast<void *>(ctx.remote_addr));
  return 0;
}

int32_t Connect(EngineCtx &ctx) {
  auto ret = ctx.engine.Connect(ctx.remote_engine.c_str(), kConnectTimeout);
  if (ret != SUCCESS) {
    printf("[ERROR] Connect failed, ret=%u, errmsg:%s\n", ret, GetRecentErrMsg());
    return -1;
  }
  ctx.connected = true;
  printf("[INFO] Connect success\n");
  return 0;
}

int32_t Transfer(EngineCtx &ctx) {
  std::vector<TransferOpDesc> descs;
  descs.reserve(kBlockCount);
  for (size_t i = 0; i < kBlockCount; ++i) {
    TransferOpDesc desc{};
    desc.local_addr = reinterpret_cast<uintptr_t>(ctx.dev_buf) + i * kBlockSize;
    desc.remote_addr = ctx.remote_addr + i * kBlockSize;
    desc.len = kBlockSize;
    descs.push_back(desc);
  }
  auto ret = ctx.engine.TransferSync(ctx.remote_engine.c_str(), READ, descs, kTransferTimeout);
  if (ret != SUCCESS) {
    printf("[ERROR] TransferSync READ failed, ret=%u, errmsg:%s\n", ret, GetRecentErrMsg());
    return -1;
  }
  printf("[INFO] TransferSync READ completed\n");
  return 0;
}

int32_t Verify(EngineCtx &ctx) {
  void *host_tmp = nullptr;
  CHECK_ACL(aclrtMallocHost(&host_tmp, kBufSize));
  CHECK_ACL(aclrtMemcpy(host_tmp, kBufSize, ctx.dev_buf, kBufSize, ACL_MEMCPY_DEVICE_TO_HOST));
  std::vector<uint8_t> expected(kBufSize, kFillValue);
  if (std::memcmp(host_tmp, expected.data(), kBufSize) != 0) {
    printf("[ERROR] Verify failed, data mismatch\n");
    CHECK_ACL(aclrtFreeHost(host_tmp));
    return -1;
  }
  CHECK_ACL(aclrtFreeHost(host_tmp));
  printf("[INFO] Verify success\n");
  return 0;
}

int32_t SetupListenSocket(EngineCtx &ctx, int port) {
  ctx.listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (ctx.listen_fd < 0) {
    printf("[ERROR] socket() failed\n");
    return -1;
  }
  int opt = 1;
  setsockopt(ctx.listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
  struct sockaddr_in addr {};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(static_cast<uint16_t>(port));
  if (bind(ctx.listen_fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0) {
    printf("[ERROR] bind port %d failed\n", port);
    close(ctx.listen_fd);
    ctx.listen_fd = -1;
    return -1;
  }
  if (listen(ctx.listen_fd, kSocketBacklog) < 0) {
    printf("[ERROR] listen failed\n");
    close(ctx.listen_fd);
    ctx.listen_fd = -1;
    return -1;
  }
  return 0;
}

int32_t AcceptAndSendAddr(EngineCtx &ctx) {
  ctx.conn_fd = accept(ctx.listen_fd, nullptr, nullptr);
  if (ctx.conn_fd < 0) {
    printf("[ERROR] accept failed\n");
    return -1;
  }
  uintptr_t buf_addr = reinterpret_cast<uintptr_t>(ctx.dev_buf);
  ssize_t sent = 0;
  while (sent < static_cast<ssize_t>(sizeof(buf_addr))) {
    ssize_t n = send(ctx.conn_fd, reinterpret_cast<const char *>(&buf_addr) + sent, sizeof(buf_addr) - sent, 0);
    if (n <= 0) {
      printf("[ERROR] send failed\n");
      close(ctx.conn_fd);
      ctx.conn_fd = -1;
      return -1;
    }
    sent += n;
  }
  printf("[INFO] Sent buffer addr %p to client, waiting for client to finish...\n", reinterpret_cast<void *>(buf_addr));
  return 0;
}

void WaitForClientDisconnect(EngineCtx &ctx) {
  char dummy;
  ssize_t recv_ret = 1;
  while (recv_ret > 0) {
    recv_ret = recv(ctx.conn_fd, &dummy, 1, 0);
  }
  printf("[INFO] Client disconnected, server exiting\n");
}

int32_t ServerSendAddr(EngineCtx &ctx) {
  int port = ParsePort(ctx.local_engine) + kSocketPortOffset;
  if (SetupListenSocket(ctx, port) != 0) {
    return -1;
  }
  printf("[INFO] Waiting for client on port %d...\n", port);
  if (AcceptAndSendAddr(ctx) != 0) {
    return -1;
  }
  WaitForClientDisconnect(ctx);
  return 0;
}

void Finalize(EngineCtx &ctx) {
  if (ctx.connected) {
    auto ret = ctx.engine.Disconnect(ctx.remote_engine.c_str(), kConnectTimeout);
    if (ret != SUCCESS) {
      printf("[ERROR] Disconnect failed, ret=%u, errmsg:%s\n", ret, GetRecentErrMsg());
    } else {
      printf("[INFO] Disconnect success\n");
    }
  }
  if (ctx.conn_fd >= 0) {
    close(ctx.conn_fd);
  }
  if (ctx.listen_fd >= 0) {
    close(ctx.listen_fd);
  }
  if (ctx.dev_handle != nullptr) {
    ctx.engine.DeregisterMem(ctx.dev_handle);
    printf("[INFO] DeregisterMem success\n");
  }
  if (ctx.dev_buf != nullptr) {
    CHECK_ACL(aclrtFree(ctx.dev_buf));
    printf("[INFO] Free memory success\n");
  }
  if (ctx.initialized) {
    ctx.engine.Finalize();
    printf("[INFO] Finalize success\n");
  }
  CHECK_ACL(aclrtResetDevice(ctx.device_id));
}
int32_t Run(EngineCtx &ctx, const std::vector<std::string> &protocols, int32_t version) {
  if (InitEngine(ctx, protocols, version) != 0) {
    return -1;
  }
  if (ctx.is_client) {
    if (GetRemoteAddr(ctx) != 0) {
      return -1;
    }
    if (Connect(ctx) != 0) {
      return -1;
    }
    if (Transfer(ctx) != 0) {
      return -1;
    }
    if (Verify(ctx) != 0) {
      return -1;
    }
  } else {
    if (ServerSendAddr(ctx) != 0) {
      return -1;
    }
  }
  return 0;
}
}  // namespace

int main(int32_t argc, char **argv) {
  EngineCtx ctx;
  std::vector<std::string> protocols;
  int32_t version = 1;
  if (ParseArgs(argc, argv, ctx, protocols, version) != 0) {
    return -1;
  }
  int32_t ret = Run(ctx, protocols, version);
  Finalize(ctx);
  return ret;
}
