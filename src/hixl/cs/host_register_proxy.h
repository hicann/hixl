/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_CS_HOST_REGISTER_PROXY_H_
#define CANN_HIXL_SRC_HIXL_CS_HOST_REGISTER_PROXY_H_

#include <cstdint>
#include <memory>
#include <mutex>
#include <map>
#include "acl/acl_rt.h"
#include "hixl/hixl_types.h"

namespace hixl {

struct HostMemInfo {
  void *host_addr = nullptr;
  uint64_t size = 0;
  void *device_addr = nullptr;
  int32_t ref_cnt = 0;

  HostMemInfo() = default;
  HostMemInfo(void *host, uint64_t host_size, void *device) : host_addr(host), size(host_size), device_addr(device) {}
};

class HostRegisterProxy {
 public:
  ~HostRegisterProxy();

  static Status RegisterByDev(uint32_t dev_phy_id, void *host_addr, uint64_t size, void *&device_addr);

  static Status UnregisterByDev(uint32_t dev_phy_id, void *host_addr);

  /**
   * 获取host addr注册的device地址。
   * 仅支持获取直接注册的，偏移后的以及远端的不支持获取。
   * @param dev_phy_id device physical id
   * @param host_addr host内存地址
   * @param device_addr 调用HostRegister映射后的device地址。
   * @return SUCCESS成功，其他失败。
   */
  static Status GetRegisteredDeviceAddrByDev(uint32_t dev_phy_id, void *host_addr, void *&device_addr);

  HostRegisterProxy(const HostRegisterProxy &) = delete;
  HostRegisterProxy &operator=(const HostRegisterProxy &) = delete;

 private:
  static std::shared_ptr<HostRegisterProxy> GetOrCreateInstance(uint32_t dev_phy_id);
  static std::shared_ptr<HostRegisterProxy> GetInstance(uint32_t dev_phy_id);
  explicit HostRegisterProxy(int32_t dev_phy_id);
  Status Initialize();
  Status Register(void *host_addr, uint64_t size, void *&device_addr);
  Status Unregister(void *host_addr);
  Status GetRegisteredDeviceAddr(void *host_addr, void *&device_addr) const;

  std::map<void *, HostMemInfo> registered_mems_;
  mutable std::mutex mutex_;
  int32_t dev_phy_id_;
  aclrtContext context_ = nullptr;
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_CS_HOST_REGISTER_PROXY_H_
