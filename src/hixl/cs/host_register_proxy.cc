/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "host_register_proxy.h"
#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/hixl_utils.h"

namespace hixl {

namespace {
std::map<int32_t, std::shared_ptr<HostRegisterProxy>> g_proxy_instances;
std::mutex g_proxy_mutex;
}  // namespace

std::shared_ptr<HostRegisterProxy> HostRegisterProxy::GetOrCreateInstance(uint32_t dev_phy_id) {
  auto get_proxy = GetInstance(dev_phy_id);
  if (get_proxy != nullptr) {
    return get_proxy;
  }
  if (dev_phy_id > INT32_MAX) {
    HIXL_LOGE(PARAM_INVALID, "dev_phy_id=%u is invalid.", dev_phy_id);
    return nullptr;
  }
  auto dev_phy_id_int = static_cast<int32_t>(dev_phy_id);
  std::lock_guard<std::mutex> lock(g_proxy_mutex);
  auto &proxy = g_proxy_instances[dev_phy_id_int];
  if (proxy != nullptr) {
    return proxy;
  }
  proxy.reset(new (std::nothrow) HostRegisterProxy(dev_phy_id_int));
  if (proxy != nullptr) {
    Status init_ret = proxy->Initialize();
    if (init_ret != SUCCESS) {
      HIXL_LOGE(init_ret, "Failed to initialize host register proxy, dev_phy_id=%u.", dev_phy_id);
      proxy.reset();
      return nullptr;
    }
  }
  return proxy;
}

std::shared_ptr<HostRegisterProxy> HostRegisterProxy::GetInstance(uint32_t dev_phy_id) {
  if (dev_phy_id > INT32_MAX) {
    HIXL_LOGE(PARAM_INVALID, "dev_phy_id=%u is invalid.", dev_phy_id);
    return nullptr;
  }

  auto dev_phy_id_int = static_cast<int32_t>(dev_phy_id);
  std::lock_guard<std::mutex> lock(g_proxy_mutex);
  auto it = g_proxy_instances.find(dev_phy_id_int);
  if (it != g_proxy_instances.end()) {
    return it->second;
  }
  return nullptr;
}

HostRegisterProxy::HostRegisterProxy(int32_t dev_phy_id) : dev_phy_id_(dev_phy_id) {}

Status HostRegisterProxy::Initialize() {
  int32_t device_id = dev_phy_id_;
  HIXL_CHK_ACL_RET(aclrtGetLogicDevIdByPhyDevId(dev_phy_id_, &device_id));
  HIXL_CHK_ACL_RET(aclrtCreateContext(&context_, device_id));
  return SUCCESS;
}

HostRegisterProxy::~HostRegisterProxy() {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto &pair : registered_mems_) {
    void *host_addr = pair.first;
    HIXL_LOGI("host_addr has not been unregistered. host_addr=%p, ref_cnt=%d.", host_addr, pair.second.ref_cnt);
    const aclError ret = aclrtHostUnregister(host_addr);
    if (ret != ACL_ERROR_NONE) {
      HIXL_LOGE(ret, "Failed to unregister host memory in destructor. host_addr=%p", host_addr);
    }
  }
  registered_mems_.clear();

  if (context_ != nullptr) {
    HIXL_CHK_ACL(aclrtDestroyContext(context_));
    context_ = nullptr;
  }
}

Status HostRegisterProxy::RegisterByDev(uint32_t dev_phy_id, void *host_addr, uint64_t size, void *&device_addr) {
  auto proxy = GetOrCreateInstance(dev_phy_id);
  HIXL_CHECK_NOTNULL(proxy);
  return proxy->Register(host_addr, size, device_addr);
}

Status HostRegisterProxy::UnregisterByDev(uint32_t dev_phy_id, void *host_addr) {
  auto proxy = GetOrCreateInstance(dev_phy_id);
  HIXL_CHECK_NOTNULL(proxy);
  return proxy->Unregister(host_addr);
}

Status HostRegisterProxy::GetRegisteredDeviceAddrByDev(uint32_t dev_phy_id, void *host_addr, void *&device_addr) {
  auto proxy = GetInstance(dev_phy_id);
  HIXL_CHECK_NOTNULL(proxy);
  return proxy->GetRegisteredDeviceAddr(host_addr, device_addr);
}

Status HostRegisterProxy::GetRegisteredDeviceAddr(void *host_addr, void *&device_addr) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = registered_mems_.find(host_addr);
  if (it != registered_mems_.end()) {
    device_addr = it->second.device_addr;
    return SUCCESS;
  }
  return FAILED;
}

Status HostRegisterProxy::Register(void *host_addr, uint64_t size, void *&device_addr) {
  HIXL_CHECK_NOTNULL(host_addr);

  std::lock_guard<std::mutex> lock(mutex_);
  auto it = registered_mems_.find(host_addr);
  if (it != registered_mems_.end()) {
    if (it->second.size != size) {
      HIXL_LOGE(PARAM_INVALID,
                "Host memory already registered with different size. host_addr=%p, cached_size=%lu, request_size=%lu",
                host_addr, it->second.size, size);
      return PARAM_INVALID;
    }
    it->second.ref_cnt++;
    device_addr = it->second.device_addr;
    HIXL_LOGI("Host memory already registered, host_addr=%p, device_addr=%p, size=%lu, ref_cnt=%d.", host_addr,
              device_addr, size, it->second.ref_cnt);
    return SUCCESS;
  }
  void *dev_ptr = nullptr;
  {
    TemporaryRtContext context_guard(context_);
    HIXL_CHK_ACL_RET(aclrtHostRegister(host_addr, size, ACL_HOST_REGISTER_MAPPED, &dev_ptr));
  }
  HostMemInfo info(host_addr, size, dev_ptr);
  info.ref_cnt++;
  registered_mems_[host_addr] = info;

  device_addr = dev_ptr;
  HIXL_LOGI("Host memory registered successfully. host_addr=%p, device_addr=%p, size%lu", host_addr, dev_ptr, size);
  return SUCCESS;
}

Status HostRegisterProxy::Unregister(void *host_addr) {
  HIXL_CHECK_NOTNULL(host_addr);

  std::lock_guard<std::mutex> lock(mutex_);
  auto it = registered_mems_.find(host_addr);
  if (it == registered_mems_.end()) {
    HIXL_LOGI("Host memory not registered, returning success. host_addr=%p.", host_addr);
    return SUCCESS;
  }
  auto &mem_info = it->second;
  mem_info.ref_cnt--;
  if (mem_info.ref_cnt > 0) {
    HIXL_LOGI("Host mem has reference, no need call aclrtHostUnregister. host_addr=%p, ref_cnt=%d.", host_addr,
              mem_info.ref_cnt);
    return SUCCESS;
  }
  {
    TemporaryRtContext context_guard(context_);
    HIXL_CHK_ACL_RET(aclrtHostUnregister(host_addr));
  }

  registered_mems_.erase(it);
  HIXL_LOGI("Host memory unregistered successfully. host_addr=%p", host_addr);
  return SUCCESS;
}

}  // namespace hixl
