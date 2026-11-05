/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "ibverbs_stub.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace {

std::mutex g_ibv_mu;
struct NicStub {
  std::string name;
  std::vector<std::array<uint8_t, 16>> gids;
};
std::vector<NicStub> g_nics;

}  // namespace

struct ibv_device {
  char name[64];
};

struct ibv_context {
  ibv_device *device;
};

union ibv_gid {
  uint8_t raw[16];
};

static std::vector<ibv_device> g_devices;

extern "C" {

void IbverbsStubReset(void) {
  std::lock_guard<std::mutex> lock(g_ibv_mu);
  g_nics.clear();
  g_devices.clear();
}

void IbverbsStubAddIpv4Device(const char *name, uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
  if (name == nullptr || name[0] == '\0') {
    return;
  }
  std::lock_guard<std::mutex> lock(g_ibv_mu);
  NicStub nic;
  nic.name = name;
  std::array<uint8_t, 16> gid{};
  gid[10] = 0xffU;
  gid[11] = 0xffU;
  gid[12] = a;
  gid[13] = b;
  gid[14] = c;
  gid[15] = d;
  nic.gids.push_back(gid);
  g_nics.push_back(std::move(nic));
}

void IbverbsStubAddNameOnly(const char *name) {
  if (name == nullptr || name[0] == '\0') {
    return;
  }
  std::lock_guard<std::mutex> lock(g_ibv_mu);
  NicStub nic;
  nic.name = name;
  g_nics.push_back(std::move(nic));
}

struct ibv_device **ibv_get_device_list(int *num_devices) {
  std::lock_guard<std::mutex> lock(g_ibv_mu);
  g_devices.clear();
  g_devices.resize(g_nics.size());
  for (size_t i = 0; i < g_nics.size(); ++i) {
    (void)std::snprintf(g_devices[i].name, sizeof(g_devices[i].name), "%s", g_nics[i].name.c_str());
  }
  if (num_devices != nullptr) {
    *num_devices = static_cast<int>(g_nics.size());
  }
  if (g_nics.empty()) {
    return nullptr;
  }
  auto **list = static_cast<ibv_device **>(std::malloc((g_nics.size() + 1U) * sizeof(ibv_device *)));
  if (list == nullptr) {
    return nullptr;
  }
  for (size_t i = 0; i < g_nics.size(); ++i) {
    list[i] = &g_devices[i];
  }
  list[g_nics.size()] = nullptr;
  return list;
}

void ibv_free_device_list(struct ibv_device **list) {
  std::free(list);
}

const char *ibv_get_device_name(struct ibv_device *device) {
  if (device == nullptr) {
    return nullptr;
  }
  return device->name;
}

struct ibv_context *ibv_open_device(struct ibv_device *device) {
  if (device == nullptr) {
    return nullptr;
  }
  auto *ctx = static_cast<ibv_context *>(std::malloc(sizeof(ibv_context)));
  if (ctx == nullptr) {
    return nullptr;
  }
  ctx->device = device;
  return ctx;
}

int ibv_close_device(struct ibv_context *context) {
  std::free(context);
  return 0;
}

int ibv_query_gid(struct ibv_context *context, uint8_t port_num, int index, union ibv_gid *gid) {
  (void)port_num;
  if (context == nullptr || context->device == nullptr || gid == nullptr || index < 0) {
    return -1;
  }
  std::lock_guard<std::mutex> lock(g_ibv_mu);
  for (const auto &nic : g_nics) {
    if (nic.name != context->device->name) {
      continue;
    }
    if (static_cast<size_t>(index) >= nic.gids.size()) {
      return -1;
    }
    (void)std::memcpy(gid->raw, nic.gids[static_cast<size_t>(index)].data(), sizeof(gid->raw));
    return 0;
  }
  return -1;
}

}  // extern "C"
