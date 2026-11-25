/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "cs/host_roce/host_roce_endpoint.h"
#include <arpa/inet.h>
#include <atomic>
#include <cerrno>
#include <cinttypes>
#include <exception>
#include <iterator>
#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/hixl_utils.h"
#include "cs/ubmem/acl_compat.h"

namespace hixl {
namespace {
constexpr uint32_t kMemDescWireMagic = 0x48524345U;
constexpr uint32_t kMemDescWireVersion = 1U;
constexpr uint32_t kMemDescWireKind = 2U;
constexpr size_t kMemDescWireBytes = 44U;
constexpr uint32_t kUint32Bytes = 4U;
constexpr uint32_t kBitsPerByte = 8U;
constexpr uint32_t kHighWordShift = 32U;
constexpr uint32_t kMaxWireMemType = 1U;

void AppendBeU32(std::vector<uint8_t> &out, uint32_t value) {
  for (int i = static_cast<int>(kUint32Bytes) - 1; i >= 0; --i) {
    out.push_back(static_cast<uint8_t>(value >> (static_cast<uint32_t>(i) * kBitsPerByte)));
  }
}
void AppendBeU64(std::vector<uint8_t> &out, uint64_t value) {
  AppendBeU32(out, static_cast<uint32_t>(value >> kHighWordShift));
  AppendBeU32(out, static_cast<uint32_t>(value));
}
uint32_t ReadBeU32(const uint8_t *&cursor) {
  uint32_t value = 0U;
  for (uint32_t i = 0; i < kUint32Bytes; ++i) {
    value = (value << kBitsPerByte) | *cursor++;
  }
  return value;
}
uint64_t ReadBeU64(const uint8_t *&cursor) {
  const uint64_t hi = ReadBeU32(cursor);
  return (hi << kHighWordShift) | ReadBeU32(cursor);
}

std::atomic<uint64_t> g_registration_id{1U};
uint64_t NextRegistrationId() {
  uint64_t id = g_registration_id.load(std::memory_order_relaxed);
  while (id != UINT64_MAX) {
    if (g_registration_id.compare_exchange_weak(id, id + 1U, std::memory_order_relaxed)) {
      return id;
    }
  }
  return 0U;
}
Status NumericEndpointIp(const CommAddr &address, std::string &ip) {
  const bool ipv4 = address.type == COMM_ADDR_TYPE_IP_V4;
  const auto family = ipv4 ? AF_INET : AF_INET6;
  const void *raw = ipv4 ? static_cast<const void *>(&address.addr) : static_cast<const void *>(&address.addr6);
  char value[INET6_ADDRSTRLEN]{};
  const auto *converted = inet_ntop(family, raw, value, sizeof(value));
  const auto error = errno;
  HIXL_CHK_BOOL_RET_STATUS(converted != nullptr, PARAM_INVALID, "[HostRoce] inet_ntop failed, family=%d, errno=%d",
                           family, error);
  ip = value;
  return SUCCESS;
}
}  // namespace
bool HostRoceContains(uint64_t base, uint64_t size, uint64_t address, uint64_t length) {
  return size <= UINT64_MAX - base && address >= base && address - base <= size && length <= size - (address - base);
}
std::vector<uint8_t> EncodeHostRoceMem(const HostRoceMemDesc &value) {
  std::vector<uint8_t> out;
  out.reserve(kMemDescWireBytes);
  AppendBeU32(out, kMemDescWireMagic);
  AppendBeU32(out, kMemDescWireVersion);
  AppendBeU32(out, kMemDescWireKind);
  AppendBeU64(out, value.address);
  AppendBeU64(out, value.size);
  AppendBeU64(out, value.registration);
  AppendBeU32(out, value.rkey);
  AppendBeU32(out, value.type);
  return out;
}
Status DecodeHostRoceMem(const void *data, size_t size, HostRoceMemDesc &value) {
  if (data == nullptr || size != kMemDescWireBytes) {
    return PARAM_INVALID;
  }
  const auto *cursor = static_cast<const uint8_t *>(data);
  if (ReadBeU32(cursor) != kMemDescWireMagic || ReadBeU32(cursor) != kMemDescWireVersion ||
      ReadBeU32(cursor) != kMemDescWireKind) {
    return PARAM_INVALID;
  }
  HostRoceMemDesc out;
  out.address = ReadBeU64(cursor);
  out.size = ReadBeU64(cursor);
  out.registration = ReadBeU64(cursor);
  out.rkey = ReadBeU32(cursor);
  out.type = ReadBeU32(cursor);
  if (out.address == 0U || out.size == 0U || out.registration == 0U || out.type > kMaxWireMemType ||
      !HostRoceContains(out.address, out.size, out.address, out.size)) {
    return PARAM_INVALID;
  }
  value = out;
  return SUCCESS;
}
HcclResult HostRoceResult(Status status) {
  if (status == SUCCESS) {
    return HCCL_SUCCESS;
  }
  if (status == PARAM_INVALID) {
    return HCCL_E_PARA;
  }
  if (status == UNSUPPORTED) {
    return HCCL_E_NOT_SUPPORT;
  }
  if (status == TIMEOUT) {
    return HCCL_E_TIMEOUT;
  }
  if (status == RESOURCE_EXHAUSTED) {
    return HCCL_E_AGAIN;
  }
  return HCCL_E_INTERNAL;
}
HcclResult HostRoceEndpoint::EndpointCreate(EndpointHandle &handle) {
  const auto &ep = GetEndpoint();
  if (!IsHostRoce(ep) || (ep.commAddr.type != COMM_ADDR_TYPE_IP_V4 && ep.commAddr.type != COMM_ADDR_TYPE_IP_V6)) {
    HIXL_LOGE(PARAM_INVALID, "[HostRoce] Endpoint requires a Host RoCE IPv4/IPv6 address");
    return HCCL_E_PARA;
  }
  std::string ip;
  auto ret = NumericEndpointIp(ep.commAddr, ip);
  if (ret != SUCCESS) {
    return HostRoceResult(ret);
  }
  auto verbs = GetHostRoceVerbs();
  std::shared_ptr<HostRoceDevice> device;
  ret = verbs->Open(ip, device);
  if (ret != SUCCESS) {
    return HostRoceResult(ret);
  }
  {
    std::lock_guard<std::mutex> lock(memory_mutex_);
    verbs_ = std::move(verbs);
    device_ = std::move(device);
  }
  handle = static_cast<Endpoint *>(this);
  return HCCL_SUCCESS;
}
HcclResult HostRoceEndpoint::EndpointDestroy() {
  std::lock_guard<std::mutex> lock(memory_mutex_);
  local_address_index_.clear();
  remote_address_index_.clear();
  remote_mems_.clear();
  local_mems_.clear();
  device_.reset();
  verbs_.reset();
  return HCCL_SUCCESS;
}
Status HostRoceEndpoint::RegistrationRange(const CommMem &mem, uint64_t &base, uint64_t &size,
                                           uint32_t &memory_device) const {
  base = reinterpret_cast<uint64_t>(mem.addr);
  size = mem.size;
  memory_device = UINT32_MAX;
  if (mem.type != COMM_MEM_TYPE_DEVICE || !device_->resolve_device_memory) {
    return SUCCESS;
  }
  aclrtPtrAttributes attr{};
  HIXL_CHK_ACL_RET(aclrtPointerGetAttributes(mem.addr, &attr),
                   "[HostRoce] aclrtPointerGetAttributes failed, size=%" PRIu64 " bytes", mem.size);
  if (attr.location.type != ACL_MEM_LOCATION_TYPE_DEVICE) {
    HIXL_LOGI("[HostRoce] Skip device range resolution for non-device pointer, location_type=%d, size=%" PRIu64
              " bytes",
              static_cast<int32_t>(attr.location.type), mem.size);
    return SUCCESS;
  }
  memory_device = attr.location.id;
  HIXL_CHK_BOOL_RET_STATUS(aclrtMemGetAddressRange != nullptr, UNSUPPORTED,
                           "[HostRoce] aclrtMemGetAddressRange is unavailable, size=%" PRIu64 " bytes", mem.size);
  void *allocation = nullptr;
  size_t bytes = 0;
  HIXL_CHK_ACL_RET(aclrtMemGetAddressRange(mem.addr, &allocation, &bytes),
                   "[HostRoce] aclrtMemGetAddressRange failed, size=%" PRIu64 " bytes", mem.size);
  const auto start = reinterpret_cast<uint64_t>(allocation);
  HIXL_CHK_BOOL_RET_STATUS(bytes != 0U && HostRoceContains(start, bytes, base, 1U), PARAM_INVALID,
                           "[HostRoce] Invalid Device allocation range, bytes=%zu, size=%" PRIu64 " bytes", bytes,
                           mem.size);
  // VMM windows may span several physical mappings; keep the validated user span in that case.
  if (HostRoceContains(start, bytes, base, size)) {
    base = start;
    size = bytes;
  }
  return SUCCESS;
}
Status HostRoceEndpoint::Register(const CommMem &mem, MemHandle &handle) {
  const auto address = reinterpret_cast<uint64_t>(mem.addr);
  HIXL_CHK_BOOL_RET_STATUS(
      device_ != nullptr && address != 0 && mem.size != 0 && HostRoceContains(address, mem.size, address, mem.size) &&
          (mem.type == COMM_MEM_TYPE_HOST || mem.type == COMM_MEM_TYPE_DEVICE),
      PARAM_INVALID, "[HostRoce] Invalid registration range/type, size=%" PRIu64 " bytes", mem.size);
  const uint64_t id = NextRegistrationId();
  HIXL_CHK_BOOL_RET_STATUS(id != 0U, RESOURCE_EXHAUSTED, "[HostRoce] Registration identifier exhausted");
  uint64_t base = 0U;
  uint64_t size = 0U;
  uint32_t memory_device = UINT32_MAX;
  HIXL_CHK_STATUS_RET(RegistrationRange(mem, base, size, memory_device));
  const bool is_device_memory =
      mem.type == COMM_MEM_TYPE_DEVICE && (!device_->resolve_device_memory || memory_device != UINT32_MAX);
  auto local = std::make_shared<HostRoceLocalMemory>();
  local->memory = mem;
  {
    std::lock_guard<std::mutex> lock(device_->memory_mutex);
    const auto key = std::make_tuple(base, size, mem.type == COMM_MEM_TYPE_DEVICE);
    const auto cached_it = device_->registrations.find(key);
    if (cached_it != device_->registrations.end()) {
      local->mr = cached_it->second.lock();
    }
    if (local->mr == nullptr) {
      HIXL_CHK_STATUS_RET(verbs_->Register(device_, base, size, is_device_memory, memory_device, local->mr));
      device_->registrations[key] = local->mr;
    }
  }
  const HostRoceMemDesc wire{address, mem.size, id, local->mr->rkey, static_cast<uint32_t>(mem.type)};
  local->exported = EncodeHostRoceMem(wire);
  handle = local.get();
  local_mems_.emplace(handle, std::move(local));
  RebuildLocalIndex();
  return SUCCESS;
}
HcclResult HostRoceEndpoint::MemReg(const char *tag, const CommMem *mem, HcommMemHandle *handle) {
  (void)tag;
  if (mem == nullptr || handle == nullptr) {
    return HCCL_E_PARA;
  }
  std::lock_guard<std::mutex> lock(memory_mutex_);
  return HostRoceResult(Register(*mem, *handle));
}
HcclResult HostRoceEndpoint::MemUnreg(HcommMemHandle handle) {
  std::lock_guard<std::mutex> lock(memory_mutex_);
  const auto it = local_mems_.find(handle);
  if (it == local_mems_.end()) {
    return HCCL_E_PARA;
  }
  if (it->second.use_count() != 1) {
    HIXL_LOGE(RESOURCE_EXHAUSTED, "[HostRoce] MR is referenced by an outstanding transfer");
    return HCCL_E_AGAIN;
  }
  std::lock_guard<std::mutex> registration_lock(device_->memory_mutex);
  if (it->second->mr.use_count() == 1) {
    const auto mr_key =
        std::make_tuple(it->second->mr->base, it->second->mr->size, it->second->memory.type == COMM_MEM_TYPE_DEVICE);
    const auto ret = verbs_->Deregister(*it->second->mr);
    if (ret != SUCCESS) {
      return HostRoceResult(ret);
    }
    const auto cached_it = device_->registrations.find(mr_key);
    if (cached_it != device_->registrations.end() && cached_it->second.lock() == it->second->mr) {
      device_->registrations.erase(cached_it);
    }
  }
  local_mems_.erase(it);
  RebuildLocalIndex();
  return HCCL_SUCCESS;
}
HcclResult HostRoceEndpoint::MemExport(HcommMemHandle handle, void **desc, uint32_t *size) {
  if (desc == nullptr || size == nullptr) {
    return HCCL_E_PARA;
  }
  std::lock_guard<std::mutex> lock(memory_mutex_);
  const auto it = local_mems_.find(handle);
  if (it == local_mems_.end()) {
    return HCCL_E_PARA;
  }
  *desc = it->second->exported.data();
  *size = static_cast<uint32_t>(it->second->exported.size());
  return HCCL_SUCCESS;
}
HcclResult HostRoceEndpoint::MemImport(const void *desc, uint32_t size, CommMem *out) {
  if (out == nullptr) {
    return HCCL_E_PARA;
  }
  HostRoceMemDesc wire;
  const auto ret = DecodeHostRoceMem(desc, size, wire);
  if (ret != SUCCESS) {
    return HostRoceResult(ret);
  }
  std::lock_guard<std::mutex> lock(memory_mutex_);
  const auto key = MakeRemoteKey(wire);
  auto &entry = remote_mems_[key];
  if (entry.refs == UINT32_MAX) {
    return HCCL_E_PARA;
  }
  const bool first_import = entry.refs == 0;
  entry.desc = wire;
  ++entry.refs;
  if (first_import) {
    RebuildRemoteIndex();
  }
  out->addr = reinterpret_cast<void *>(wire.address);
  out->size = wire.size;
  out->type = static_cast<CommMemType>(wire.type);
  return HCCL_SUCCESS;
}
HcclResult HostRoceEndpoint::MemUnimport(const void *desc, uint32_t size) {
  HostRoceMemDesc wire;
  const auto ret = DecodeHostRoceMem(desc, size, wire);
  if (ret != SUCCESS) {
    return HostRoceResult(ret);
  }
  std::lock_guard<std::mutex> lock(memory_mutex_);
  const auto it = remote_mems_.find(MakeRemoteKey(wire));
  if (it == remote_mems_.end()) {
    return HCCL_E_PARA;
  }
  if (--it->second.refs == 0) {
    remote_mems_.erase(it);
    RebuildRemoteIndex();
  }
  return HCCL_SUCCESS;
}
void HostRoceEndpoint::RebuildLocalIndex() {
  local_address_index_.clear();
  for (const auto &entry : local_mems_) {
    const auto &memory = entry.second;
    auto &candidate = local_address_index_[reinterpret_cast<uint64_t>(memory->memory.addr)];
    const auto current = candidate.lock();
    if (current == nullptr || memory->memory.size > current->memory.size) {
      candidate = memory;
    }
  }
  std::shared_ptr<HostRoceLocalMemory> longest;
  uint64_t max_end = 0;
  for (auto &entry : local_address_index_) {
    const auto memory = entry.second.lock();
    // Index the exported logical span, which can be smaller than the allocation registered with verbs.
    const auto end = entry.first + memory->memory.size;
    if (end > max_end) {
      max_end = end;
      longest = memory;
    }
    entry.second = longest;
  }
}
void HostRoceEndpoint::RebuildRemoteIndex() {
  remote_address_index_.clear();
  for (const auto &entry : remote_mems_) {
    const auto &memory = entry.second;
    auto &candidate = remote_address_index_[memory.desc.address];
    if (candidate == nullptr || memory.desc.size > candidate->desc.size) {
      candidate = &memory;
    }
  }
  const Remote *longest = nullptr;
  uint64_t max_end = 0;
  for (auto &entry : remote_address_index_) {
    const auto end = entry.first + entry.second->desc.size;
    if (end > max_end) {
      max_end = end;
      longest = entry.second;
    }
    entry.second = longest;
  }
}
Status HostRoceEndpoint::FindLocal(uint64_t address, uint64_t size,
                                   std::shared_ptr<HostRoceLocalMemory> &memory) const {
  std::lock_guard<std::mutex> lock(memory_mutex_);
  const auto it = local_address_index_.upper_bound(address);
  if (it != local_address_index_.begin()) {
    auto candidate = std::prev(it)->second.lock();
    if (candidate != nullptr &&
        HostRoceContains(reinterpret_cast<uint64_t>(candidate->memory.addr), candidate->memory.size, address, size)) {
      memory = std::move(candidate);
      return SUCCESS;
    }
  }
  HIXL_LOGE(PARAM_INVALID, "[HostRoce] Local range is not registered, size=%" PRIu64 " bytes", size);
  return PARAM_INVALID;
}
Status HostRoceEndpoint::FindRemote(uint64_t address, uint64_t size, HostRoceMemDesc &memory) const {
  std::lock_guard<std::mutex> lock(memory_mutex_);
  const auto it = remote_address_index_.upper_bound(address);
  if (it != remote_address_index_.begin()) {
    const auto &candidate = std::prev(it)->second->desc;
    if (HostRoceContains(candidate.address, candidate.size, address, size)) {
      memory = candidate;
      return SUCCESS;
    }
  }
  HIXL_LOGE(PARAM_INVALID, "[HostRoce] Remote range is not imported, size=%" PRIu64 " bytes", size);
  return PARAM_INVALID;
}
Status HostRoceEndpoint::ValidatePeerEndpoint(const EndpointDesc &remote_endpoint) const {
  HIXL_CHK_BOOL_RET_STATUS(IsHostRoce(remote_endpoint), UNSUPPORTED,
                           "[HostRoce] Both native endpoints must use roce:host");
  return SUCCESS;
}
HostRoceEndpoint::HostRoceEndpoint(const EndpointDesc &endpoint)
    : Endpoint(endpoint, false,
               EndpointCapabilities{BackendKind::kHixl, /*uses_channel_handshake=*/true,
                                    /*supports_configurable_transfer=*/true, TransFlagKind::kDeviceOnly,
                                    /*requires_device_trans_flag=*/true, /*min_retry_interval=*/0U,
                                    /*max_retry_interval=*/31U}) {}

HostRoceEndpoint::~HostRoceEndpoint() {
  try {
    (void)Finalize();
  } catch (const std::exception &e) {
    HIXL_LOGE(FAILED, "[HostRoce] Endpoint Finalize caught exception:%s", e.what());
  }
}
}  // namespace hixl
