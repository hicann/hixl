/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <cstring>
#include <cstdint>
#include "hixl/hixl_types.h"
#include "common/hixl_checker.h"
#include "hixl_mem_store.h"

namespace hixl {
Status HixlMemStore::RecordMemory(bool is_server, const void *addr, size_t size) {
  std::lock_guard<std::mutex> lock(mutex_);
  MemoryRegion new_region(addr, size);
  if (is_server) {  // server侧内存注册
    auto it = server_regions_.find(addr);
    if (it != server_regions_.end()) {
      return SUCCESS;  // 内存已注册，此时不做处理，直接返回。
    }
    server_regions_[addr] = new_region;
  } else {  // client侧内存注册
    auto it = client_regions_.find(addr);
    if (it != client_regions_.end()) {
      return PARAM_INVALID;  // 内存已注册
    }
    client_regions_[addr] = new_region;
  }
  return SUCCESS;
}

Status HixlMemStore::UnrecordMemory(bool is_server, const void *addr) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (is_server) {
    auto it = server_regions_.find(addr);
    if (it == server_regions_.end()) {
      HIXL_LOGE(PARAM_INVALID,
                "The memory has not been registered and therefore cannot be deleted. Memory information: buf_addr:%p",
                addr);
      return PARAM_INVALID;  // 内存尚未注册，无法注销
    }
    server_regions_.erase(addr);
  } else {
    auto it = client_regions_.find(addr);
    if (it == client_regions_.end()) {
      HIXL_LOGE(PARAM_INVALID,
                "The memory has not been registered and therefore cannot be deleted. Memory information: buf_addr:%p",
                addr);
      return PARAM_INVALID;
    }
    client_regions_.erase(addr);
  }
  return SUCCESS;
}

bool HixlMemStore::CheckMemoryForRegister(bool is_server, const void *check_addr, size_t check_size) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto &regions = is_server ? server_regions_ : client_regions_;
  HIXL_CHECK_NOTNULL(check_addr);
  if (check_size == size_t{0}) {
    return true;
  }  // 地址大小为0，无效，视为不允许注册
  if (regions.empty()) {
    return false;
  }  // regions为空，没有已注册，允许注册

  uintptr_t s = reinterpret_cast<uintptr_t>(check_addr);
  uintptr_t e = s + check_size;  // 半开区间 [s, e)

  auto overlaps = [s, e](const MemoryRegion &r) {
    auto rs = reinterpret_cast<uintptr_t>(r.addr);
    auto re = rs + r.size;
    return ((rs <= s) && (s <= re)) || ((rs <= e) && (e <= re));
  };

  auto it = regions.lower_bound(check_addr);
  if (it != regions.end() && overlaps(it->second)) {
    HIXL_LOGE(PARAM_INVALID,
              "%s memory registration failed; the parameters overlap with the already registered memory. "
              "Overlapping memory information: buf_addr:%p, buf_len:%u",
              is_server ? "Server" : "Client", it->second.addr, it->second.size);
    return true;  // 与后一个起点>=s的区间重叠
  }
  if (it != regions.begin()) {
    const auto &prev = std::prev(it)->second;  // 与前一个区间可能重叠
    if (overlaps(prev)) {
      HIXL_LOGE(PARAM_INVALID,
                "%s memory registration failed; the parameters overlap with the already registered memory. "
                "Overlapping memory information: buf_addr:%p, buf_len:%u",
                is_server ? "Server" : "Client", it->second.addr, it->second.size);
      return true;
    }
  }
  return false;  // 与相邻区域都不重叠，允许注册
}

bool HixlMemStore::CheckMemoryForAccess(bool is_server, const void *check_addr, size_t check_size) {
  const auto &regions = is_server ? server_regions_ : client_regions_;
  HIXL_CHECK_NOTNULL(check_addr);
  if (check_size == size_t{0}) {
    return false;
  }
  if (regions.empty()) {
    return false;  // 无注册，访问不允许
  }

  uintptr_t s = reinterpret_cast<uintptr_t>(check_addr);
  uintptr_t e = s + check_size;  // [s, e)

  auto it = regions.lower_bound(check_addr);
  auto contains = [s, e](const MemoryRegion &r) {
    auto rs = reinterpret_cast<uintptr_t>(r.addr);
    auto re = rs + r.size;
    return (s >= rs) && (e <= re);
  };

  if (it != regions.end() && contains(it->second)) {
    return true;
  }
  if (it != regions.begin()) {
    const auto &prev = std::prev(it)->second;
    if (contains(prev)) {
      return true;
    }
  }
  return false;
}


Status HixlMemStore::ValidateMemoryAccess(const void *server_addr, size_t mem_size, const void *client_addr) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (server_addr == nullptr || client_addr == nullptr || mem_size == size_t{0}) {
    return PARAM_INVALID;
  }
  bool server_valid = CheckMemoryForAccess(true, server_addr, mem_size);
  // 验证Server端内存访问t
  if (!server_valid) {
    HIXL_LOGE(PARAM_INVALID,
              "Server memory verification failed; the memory has not been registered yet. memory information: "
              "server_addr:%p, buf_len:%u",
              server_addr, mem_size);
    return PARAM_INVALID;
  }
  // 验证Client端内存访问
  bool client_valid = CheckMemoryForAccess(false, client_addr, mem_size);
  if (!client_valid) {
    HIXL_LOGE(PARAM_INVALID,
              "Client memory verification failed; the memory has not been registered yet. memory information: "
              "client_addr:%p, buf_len:%u",
              client_addr, mem_size);
    return PARAM_INVALID;
  }
  return SUCCESS;
}
}  // namespace hixl