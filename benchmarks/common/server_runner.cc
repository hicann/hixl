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

using hixl::AscendString;
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

void ReleaseHixlResources(Hixl &hixl_engine, bool need_register, bool is_host,
                          const std::vector<hixl::MemHandle> &handles,
                          const std::vector<void *> &buffers) {
  if (need_register) {
    for (const auto &handle : handles) {
      if (handle == nullptr) {
        continue;
      }
      auto ret = hixl_engine.DeregisterMem(handle);
      if (ret != 0) {
        std::printf("[ERROR] DeregisterMem failed, ret = %u, errmsg: %s\n", ret, RecentErrMsg());
      } else {
        std::printf("[INFO] DeregisterMem success\n");
      }
    }
  }
  if (is_host) {
    for (const auto &buffer : buffers) {
      if (buffer != nullptr) {
        (void)aclrtFreeHost(buffer);
      }
    }
  } else {
    for (const auto &buffer : buffers) {
      if (buffer != nullptr) {
        (void)aclrtFree(buffer);
      }
    }
  }
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
        ReleaseHixlResources(hixl_, true, is_host_, {mem_handle_}, {buffer_});
      } else {
        ReleaseHixlResources(hixl_, false, is_host_, {}, {buffer_});
      }
    } else {
      if (need_register_ && mem_registered_) {
        ReleaseHixlResources(hixl_, true, is_host_, {mem_handle_}, {});
      } else {
        ReleaseHixlResources(hixl_, false, is_host_, {}, {});
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
      (void)aclrtFreeHost(buffer_);
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
  is_host_ = (cfg_.transfer_mode == "d2h" || cfg_.transfer_mode == "h2h");
  const size_t alloc_size = static_cast<size_t>(cfg_.total_size);
  aclError ar_alloc = ACL_ERROR_NONE;
  if (is_host_) {
    ar_alloc = aclrtMallocHost(&buffer_, alloc_size);
  } else {
    ar_alloc = aclrtMalloc(&buffer_, alloc_size, ACL_MEM_MALLOC_HUGE_ONLY);
  }
  if (ar_alloc != ACL_ERROR_NONE) {
    std::printf("[ERROR] server alloc failed acl=%d\n", static_cast<int>(ar_alloc));
    return false;
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
  need_register_ = !(cfg_.use_buffer_pool && cfg_.transfer_mode == "d2h");
  if (!need_register_) {
    return true;
  }
  MemDesc desc{};
  desc.addr = addr;
  desc.len = static_cast<size_t>(cfg_.total_size);
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
