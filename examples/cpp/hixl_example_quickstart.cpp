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
#include <map>
#include <string>
#include <vector>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "acl/acl.h"
#include "hixl/hixl.h"

using namespace hixl;

namespace {
constexpr int32_t kDeviceClient = 0;
constexpr int32_t kDeviceServer = 2;
constexpr const char *kClientEngine = "127.0.0.1:16000";
constexpr const char *kServerEngine = "127.0.0.1:16001";
constexpr int32_t kSocketPort = 17001;
constexpr int32_t kTimeoutMs = 5000;
constexpr size_t kBufSize = 1024 * 1024;
constexpr uint8_t kFillValue = 0x5A;

const char *GetRecentErrMsg() {
  const char *errmsg = aclGetRecentErrMsg();
  return (errmsg == nullptr) ? "no error" : errmsg;
}

void HixlExitOnFailure(Status status, const char *message) {
  if (status == SUCCESS) {
    return;
  }
  printf("[ERROR] %s: errmsg=%s\n", message, GetRecentErrMsg());
  exit(EXIT_FAILURE);
}

void HixlExitOnFailure(bool condition, const char *message) {
  if (condition) {
    return;
  }
  printf("[ERROR] %s\n", message);
  exit(EXIT_FAILURE);
}

#define ACL_EXIT_ON_FAILURE(expr)                                  \
  do {                                                             \
    if ((expr) != ACL_ERROR_NONE) {                                \
      printf("[ERROR] ACL failed at %s:%d\n", __FILE__, __LINE__); \
      exit(EXIT_FAILURE);                                          \
    }                                                              \
  } while (0)

struct EngineCtx {
  Hixl engine;
  int32_t device = 0;
  void *buf = nullptr;
  MemHandle handle = nullptr;
  int fd = -1;
  MemDesc desc{};
  TransferOpDesc op{};  // Server 不使用
};

void InitEngine(EngineCtx &ctx, const char *local) {
  ACL_EXIT_ON_FAILURE(aclrtSetDevice(ctx.device));
  std::map<AscendString, AscendString> opts;
  opts[OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"comm_resource_config.protocol_desc": ["hccs:device"]})";
  HixlExitOnFailure(ctx.engine.Initialize(local, opts), "Initialize");
}

void ExchangeAddr(bool is_client, void *local_buf, uintptr_t &remote_addr, int &fd) {
  if (is_client) {
    fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(kSocketPort);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    HixlExitOnFailure(connect(fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) == 0,
                      "socket connect failed");
    recv(fd, &remote_addr, sizeof(remote_addr), 0);
    printf("[INFO] Got remote addr: 0x%lx\n", remote_addr);
  } else {
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(kSocketPort);
    bind(lfd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr));
    listen(lfd, 1);
    printf("[INFO] Server waiting on port %d...\n", kSocketPort);
    fd = accept(lfd, nullptr, nullptr);
    close(lfd);
    uintptr_t local_addr = reinterpret_cast<uintptr_t>(local_buf);
    send(fd, &local_addr, sizeof(local_addr), 0);
    printf("[INFO] Sent local addr: %p\n", local_buf);
  }
}

void VerifyData(void *buf) {
  std::vector<uint8_t> host(kBufSize);
  ACL_EXIT_ON_FAILURE(aclrtMemcpy(host.data(), kBufSize, buf, kBufSize, ACL_MEMCPY_DEVICE_TO_HOST));
  std::vector<uint8_t> expected(kBufSize, kFillValue);
  bool ok = (memcmp(host.data(), expected.data(), kBufSize) == 0);
  HixlExitOnFailure(ok, "Verify failed");
  printf("[INFO] Verify success\n");
}

void Finalize(EngineCtx &ctx) {
  if (ctx.fd >= 0) {
    close(ctx.fd);
    ctx.fd = -1;
  }
  if (ctx.handle != nullptr) {
    ctx.engine.DeregisterMem(ctx.handle);
    ctx.handle = nullptr;
    printf("[INFO] DeregisterMem done\n");
  }
  if (ctx.buf != nullptr) {
    aclrtFree(ctx.buf);
    ctx.buf = nullptr;
  }
  ctx.engine.Finalize();
  printf("[INFO] Finalize done\n");
  aclrtResetDevice(ctx.device);
}

// 申请本地 device 内存、与 server 交换地址，并生成 TransferOpDesc（不含 RegisterMem）。
void PrepareClientMemAndOp(EngineCtx &ctx) {
  ACL_EXIT_ON_FAILURE(aclrtMalloc(&ctx.buf, kBufSize, ACL_MEM_MALLOC_HUGE_ONLY));
  ctx.desc.addr = reinterpret_cast<uintptr_t>(ctx.buf);
  ctx.desc.len = kBufSize;

  uintptr_t remote_addr = 0;
  ExchangeAddr(true, ctx.buf, remote_addr, ctx.fd);

  ctx.op.local_addr = ctx.desc.addr;
  ctx.op.remote_addr = remote_addr;
  ctx.op.len = kBufSize;
}

void RunClient() {
  EngineCtx ctx;
  ctx.device = kDeviceClient;

  InitEngine(ctx, kClientEngine);
  PrepareClientMemAndOp(ctx);
  HixlExitOnFailure(ctx.engine.RegisterMem(ctx.desc, MEM_DEVICE, ctx.handle), "RegisterMem");
  printf("[INFO] RegisterMem success, buf=%p\n", ctx.buf);

  HixlExitOnFailure(ctx.engine.Connect(kServerEngine, kTimeoutMs), "Connect");
  HixlExitOnFailure(ctx.engine.TransferSync(kServerEngine, READ, {ctx.op}, kTimeoutMs), "TransferSync");
  printf("[INFO] TransferSync READ completed\n");
  VerifyData(ctx.buf);

  HixlExitOnFailure(send(ctx.fd, "d", 1, 0) == 1, "send done signal failed");
  HixlExitOnFailure(ctx.engine.Disconnect(kServerEngine, kTimeoutMs), "Disconnect");
  printf("[INFO] Disconnect done\n");

  Finalize(ctx);
}

void RunServer() {
  EngineCtx ctx;
  ctx.device = kDeviceServer;

  InitEngine(ctx, kServerEngine);

  // 先申请、填充并注册本地内存，再经 socket 交换地址，避免未注册地址被 client 提前使用。
  ACL_EXIT_ON_FAILURE(aclrtMalloc(&ctx.buf, kBufSize, ACL_MEM_MALLOC_HUGE_ONLY));
  ctx.desc.addr = reinterpret_cast<uintptr_t>(ctx.buf);
  ctx.desc.len = kBufSize;
  std::vector<uint8_t> fill(kBufSize, kFillValue);
  ACL_EXIT_ON_FAILURE(aclrtMemcpy(ctx.buf, kBufSize, fill.data(), kBufSize, ACL_MEMCPY_HOST_TO_DEVICE));

  HixlExitOnFailure(ctx.engine.RegisterMem(ctx.desc, MEM_DEVICE, ctx.handle), "RegisterMem");
  printf("[INFO] RegisterMem success, buf=%p\n", ctx.buf);

  uintptr_t remote_addr = 0;
  ExchangeAddr(false, ctx.buf, remote_addr, ctx.fd);

  char dummy = 0;
  HixlExitOnFailure(recv(ctx.fd, &dummy, 1, 0) == 1, "recv done signal failed");
  printf("[INFO] Server got done signal\n");

  Finalize(ctx);
}
}  // namespace

int main(int32_t argc, char **argv) {
  bool is_client = false;
  bool has_role = false;
  for (int32_t i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--role=client") {
      is_client = true;
      has_role = true;
    } else if (arg == "--role=server") {
      is_client = false;
      has_role = true;
    } else {
      printf("Usage: %s --role=client|server\n", argv[0]);
      return -1;
    }
  }
  if (!has_role) {
    printf("Usage: %s --role=client|server\n", argv[0]);
    return -1;
  }
  printf("[INFO] Running as %s\n", is_client ? "client" : "server");
  if (is_client) {
    RunClient();
  } else {
    RunServer();
  }
  return 0;
}
