/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "server_runner.h"

#include <cstdio>
#include <map>
#include <vector>

#include "acl/acl.h"
#include "fabric_mem/fabric_mem_transfer_service.h"

using hixl::AscendString;
using hixl::FabricMemTransferService;
using hixl::MemDesc;
using hixl::MemType;
using hixl::SUCCESS;
using hixl::Hixl;

namespace {

const char *RecentErrMsg() {
  const char *errmsg = aclGetRecentErrMsg();
  if (errmsg == nullptr) {
    return "no error";
  }
  return errmsg;
}

int32_t InitializeHixl(const std::string &local_engine, const hixl_benchmark::BenchmarkConfig &cfg, Hixl *hixl) {
  const std::map<AscendString, AscendString> init_options =
      hixl_benchmark::BenchmarkConfigParser::BuildInitializeOptions(cfg);
  const auto ret = hixl->Initialize(AscendString(local_engine.c_str()), init_options);
  if (ret != SUCCESS) {
    std::printf("[ERROR] Initialize failed, ret = %u, errmsg: %s\n", ret, RecentErrMsg());
    return -1;
  }
  std::printf("[INFO] Initialize success local_engine=%s\n", local_engine.c_str());
  return 0;
}

void DeregisterMemHandles(Hixl &hixl_engine, const std::vector<hixl::MemHandle> &handles) {
  for (const auto &handle : handles) {
    if (handle == nullptr) {
      continue;
    }
    const auto ret = hixl_engine.DeregisterMem(handle);
    if (ret != 0) {
      std::printf("[ERROR] DeregisterMem failed, ret = %u, errmsg: %s\n", ret, RecentErrMsg());
    } else {
      std::printf("[INFO] DeregisterMem success\n");
    }
  }
}

void FreeHostBuffers(const std::vector<void *> &buffers, const std::string &transport) {
  for (const auto &buffer : buffers) {
    if (buffer == nullptr) {
      continue;
    }
    if (transport == "fabric_mem") {
      (void)FabricMemTransferService::FreeMem(buffer);
    } else {
      (void)aclrtFreeHost(buffer);
    }
  }
}

void FreeDeviceBuffers(const std::vector<void *> &buffers, const std::string &transport) {
  for (const auto &buffer : buffers) {
    if (buffer == nullptr) {
      continue;
    }
    if (transport == "fabric_mem") {
      (void)FabricMemTransferService::FreeMem(buffer);
    } else {
      (void)aclrtFree(buffer);
    }
  }
}

void ReleaseHixlResources(Hixl &hixl_engine, bool need_register, bool is_host,
                          const std::vector<hixl::MemHandle> &handles,
                          const std::vector<void *> &buffers,
                          const std::string &transport = "") {
  if (need_register) {
    DeregisterMemHandles(hixl_engine, handles);
  }
  using FreeBuffersFn = void (*)(const std::vector<void *> &, const std::string &);
  const FreeBuffersFn free_buffers = is_host ? FreeHostBuffers : FreeDeviceBuffers;
  free_buffers(buffers, transport);
  hixl_engine.Finalize();
}

}  // namespace

namespace hixl_benchmark {

ServerRunner::~ServerRunner() { Shutdown(); }

void ServerRunner::ReleaseServerResources() {
  tcp_session_.reset();

  if (hixl_initialized_) {
    if (buffer_allocated_) {
      if (need_register_ && mem_registered_) {
        ReleaseHixlResources(hixl_, true, is_host_, {mem_handle_}, {buffer_}, cfg_.transport);
      } else {
        ReleaseHixlResources(hixl_, false, is_host_, {}, {buffer_}, cfg_.transport);
      }
    } else {
      if (need_register_ && mem_registered_) {
        ReleaseHixlResources(hixl_, true, is_host_, {mem_handle_}, {}, cfg_.transport);
      } else {
        ReleaseHixlResources(hixl_, false, is_host_, {}, {}, cfg_.transport);
      }
    }
    hixl_initialized_ = false;
    mem_registered_ = false;
    buffer_allocated_ = false;
    buffer_ = nullptr;
    mem_handle_ = nullptr;
    return;
  }
  if (buffer_allocated_) {
    if (is_host_) {
      if (cfg_.transport == "fabric_mem") {
        (void)FabricMemTransferService::FreeMem(buffer_);
      } else {
        (void)aclrtFreeHost(buffer_);
      }
    } else if (cfg_.transport == "fabric_mem") {
      (void)FabricMemTransferService::FreeMem(buffer_);
    } else {
      (void)aclrtFree(buffer_);
    }
    buffer_allocated_ = false;
    buffer_ = nullptr;
  }
}

bool ServerRunner::Init() {
  device_id_ = cfg_.expanded_device_ids[0];
  if (aclrtSetDevice(device_id_) != ACL_ERROR_NONE) {
    return false;
  }
  device_bound_ = true;
  return true;
}

void ServerRunner::Shutdown() {
  ReleaseServerResources();
  if (device_bound_) {
    (void)aclrtResetDevice(device_id_);
    device_bound_ = false;
  }
}

bool ServerRunner::AllocServerBufferForRun() {
  is_host_ = (cfg_.target_memory_type == "host");
  const size_t alloc_size = static_cast<size_t>(cfg_.buffer_size);
  if (is_host_ && cfg_.transport == "fabric_mem") {
    auto status = FabricMemTransferService::MallocMem(MemType::MEM_HOST, alloc_size, &buffer_);
    if (status != SUCCESS) {
      std::printf("[ERROR] server fabric_mem alloc failed status=%d\n", static_cast<int>(status));
      return false;
    }
  } else if (is_host_) {
    aclError ar_alloc = aclrtMallocHost(&buffer_, alloc_size);
    if (ar_alloc != ACL_ERROR_NONE) {
      std::printf("[ERROR] server alloc host failed acl=%d\n", static_cast<int>(ar_alloc));
      return false;
    }
  } else if (cfg_.transport == "fabric_mem") {
    auto status = FabricMemTransferService::MallocMem(MemType::MEM_DEVICE, alloc_size, &buffer_);
    if (status != SUCCESS) {
      std::printf("[ERROR] server fabric_mem device alloc failed status=%d\n", static_cast<int>(status));
      return false;
    }
  } else {
    aclError ar_alloc = aclrtMalloc(&buffer_, alloc_size, ACL_MEM_MALLOC_HUGE_ONLY);
    if (ar_alloc != ACL_ERROR_NONE) {
      std::printf("[ERROR] server alloc device failed acl=%d\n", static_cast<int>(ar_alloc));
      return false;
    }
  }
  buffer_allocated_ = true;
  return true;
}

bool ServerRunner::InitHixlAndRegisterMem() {
  const std::string &local = cfg_.expanded_local_engines[0];
  if (InitializeHixl(local, cfg_, &hixl_) != 0) {
    return false;
  }
  hixl_initialized_ = true;

  const std::uintptr_t addr = reinterpret_cast<std::uintptr_t>(buffer_);
  const auto mem_type = is_host_ ? MemType::MEM_HOST : MemType::MEM_DEVICE;
  need_register_ = true;
  MemDesc desc{};
  desc.addr = addr;
  desc.len = static_cast<size_t>(cfg_.buffer_size);
  const auto ret = hixl_.RegisterMem(desc, mem_type, mem_handle_);
  if (ret != SUCCESS) {
    std::printf("[ERROR] RegisterMem failed, ret = %u, errmsg: %s\n", ret, RecentErrMsg());
    return false;
  }
  mem_registered_ = true;
  std::printf("[INFO] RegisterMem success, addr:%p\n", buffer_);
  return true;
}

int ServerRunner::CompleteTcpHandshake(std::uintptr_t addr) {
  tcp_session_.emplace(cfg_.tcp_port, cfg_.tcp_accept_wait_sec, cfg_.tcp_client_count);
  if (!tcp_session_->WaitAndSendAddr(addr)) {
    tcp_session_.reset();
    return -1;
  }

  std::printf("[INFO] Wait transfer begin (N=%zu)\n", tcp_session_->ConnectedPeerCount());
  if (!tcp_session_->WaitAllNotify()) {
    tcp_session_.reset();
    return -1;
  }
  std::printf("[INFO] Wait transfer end\n");

  tcp_session_.reset();
  return 0;
}

int ServerRunner::Run() {
  std::printf("[INFO] server start\n");

  if (!AllocServerBufferForRun()) {
    return -1;
  }
  if (!InitHixlAndRegisterMem()) {
    return -1;
  }

  const std::uintptr_t addr = reinterpret_cast<std::uintptr_t>(buffer_);
  if (CompleteTcpHandshake(addr) != 0) {
    return -1;
  }

  std::printf("[INFO] Server Sample end\n");
  return 0;
}

}  // namespace hixl_benchmark
