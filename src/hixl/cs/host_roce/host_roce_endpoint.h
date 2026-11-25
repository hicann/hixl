/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_CS_HOST_ROCE_ENDPOINT_H_
#define CANN_HIXL_SRC_HIXL_CS_HOST_ROCE_ENDPOINT_H_
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <tuple>
#include <vector>
#include "cs/endpoint.h"
#include "cs/host_roce/host_roce_verbs.h"
namespace hixl {
struct HostRoceMemDesc {
  uint64_t address{0};
  uint64_t size{0};
  uint64_t registration{0};
  uint32_t rkey{0};
  uint32_t type{0};
};
bool HostRoceContains(uint64_t base, uint64_t size, uint64_t address, uint64_t length);
std::vector<uint8_t> EncodeHostRoceMem(const HostRoceMemDesc &value);
Status DecodeHostRoceMem(const void *data, size_t size, HostRoceMemDesc &value);
HcclResult HostRoceResult(Status status);
struct HostRoceLocalMemory {
  CommMem memory{};
  std::shared_ptr<HostRoceMr> mr;
  std::vector<uint8_t> exported;
};
class HostRoceEndpoint final : public Endpoint {
 public:
  explicit HostRoceEndpoint(const EndpointDesc &endpoint);
  HostRoceEndpoint(const HostRoceEndpoint &) = delete;
  HostRoceEndpoint &operator=(const HostRoceEndpoint &) = delete;
  HostRoceEndpoint(HostRoceEndpoint &&) = delete;
  HostRoceEndpoint &operator=(HostRoceEndpoint &&) = delete;
  ~HostRoceEndpoint() override;
  std::shared_ptr<HostRoceDevice> Device() const {
    std::lock_guard<std::mutex> lock(memory_mutex_);
    return device_;
  }
  std::shared_ptr<HostRoceVerbs> Verbs() const {
    std::lock_guard<std::mutex> lock(memory_mutex_);
    return verbs_;
  }
  Status FindLocal(uint64_t address, uint64_t size, std::shared_ptr<HostRoceLocalMemory> &memory) const;
  Status FindRemote(uint64_t address, uint64_t size, HostRoceMemDesc &memory) const;
  Status ValidatePeerEndpoint(const EndpointDesc &remote_endpoint) const override;

 protected:
  HcclResult EndpointCreate(EndpointHandle &handle) override;
  HcclResult EndpointDestroy() override;
  HcclResult EndpointGetListenPort(uint32_t &port) const override {
    port = 0;
    return HCCL_SUCCESS;
  }
  HcclResult MemReg(const char *tag, const CommMem *mem, HcommMemHandle *handle) override;
  HcclResult MemUnreg(HcommMemHandle handle) override;
  HcclResult MemExport(HcommMemHandle handle, void **desc, uint32_t *size) override;
  HcclResult MemImport(const void *desc, uint32_t size, CommMem *out) override;
  HcclResult MemUnimport(const void *desc, uint32_t size) override;

 private:
  Status Register(const CommMem &mem, MemHandle &handle);
  Status RegistrationRange(const CommMem &mem, uint64_t &base, uint64_t &size, uint32_t &memory_device) const;
  void RebuildLocalIndex();
  void RebuildRemoteIndex();
  using RemoteKey = std::tuple<uint64_t, uint64_t, uint64_t, uint32_t, uint32_t>;
  struct Remote {
    HostRoceMemDesc desc;
    uint32_t refs{0};
  };
  static RemoteKey MakeRemoteKey(const HostRoceMemDesc &desc) {
    return {desc.registration, desc.address, desc.size, desc.rkey, desc.type};
  }
  mutable std::mutex memory_mutex_;
  std::map<MemHandle, std::shared_ptr<HostRoceLocalMemory>> local_mems_;
  std::map<RemoteKey, Remote> remote_mems_;
  // Each address stores the candidate with the greatest end among all starts up to that address.
  // Weak references keep the index from pinning a registration that no outstanding request owns.
  std::map<uint64_t, std::weak_ptr<HostRoceLocalMemory>> local_address_index_;
  std::map<uint64_t, const Remote *> remote_address_index_;
  std::shared_ptr<HostRoceVerbs> verbs_;
  std::shared_ptr<HostRoceDevice> device_;
};
}  // namespace hixl
#endif
