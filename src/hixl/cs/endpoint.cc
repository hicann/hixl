/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "endpoint.h"

#include <cinttypes>
#include <string>
#include <vector>

#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/hixl_utils.h"
#include "common/scope_guard.h"
#include "channel_factory.h"
#include "cs/ubmem/ubmem_endpoint.h"
#include "hcomm_endpoint.h"
#include "cs/host_roce/host_roce_endpoint.h"
#include "host_register_proxy.h"

namespace hixl {
namespace {
bool IsDefaultHostVaMappingEnabled(const EndpointDesc &endpoint) {
  return endpoint.loc.locType == ENDPOINT_LOC_TYPE_DEVICE &&
         (endpoint.protocol == COMM_PROTOCOL_UBOE || endpoint.protocol == COMM_PROTOCOL_UBG ||
          endpoint.protocol == COMM_PROTOCOL_UBC_CTP);
}

bool IsHostVaMappingEnabledForPair(const EndpointDesc &local_endpoint, const EndpointDesc &remote_endpoint) {
  if (!IsDefaultHostVaMappingEnabled(local_endpoint)) {
    return false;
  }
  return local_endpoint.protocol != COMM_PROTOCOL_UBC_CTP || remote_endpoint.loc.locType == ENDPOINT_LOC_TYPE_DEVICE;
}

EndpointCapabilities DefaultEndpointCapabilities(const EndpointDesc &endpoint) {
  EndpointCapabilities caps{};
  caps.backend = IsUbMemProtocol(endpoint.protocol) ? BackendKind::kHixl : BackendKind::kDefault;
  if (endpoint.loc.locType == ENDPOINT_LOC_TYPE_DEVICE && IsUbMemProtocol(endpoint.protocol)) {
    caps.builtin_trans_flag = TransFlagKind::kNone;
  } else if (endpoint.loc.locType == ENDPOINT_LOC_TYPE_HOST) {
    caps.builtin_trans_flag = TransFlagKind::kHost;
  } else if (endpoint.loc.locType == ENDPOINT_LOC_TYPE_DEVICE) {
    caps.builtin_trans_flag = TransFlagKind::kDeviceWithPool;
  }
  if (IsHostRoce(endpoint)) {
    caps.min_retry_interval = 0U;
    caps.max_retry_interval = UINT32_MAX;
  }
  return caps;
}
}  // namespace

EndpointPtr Endpoint::Create(const EndpointDesc &endpoint, const GlobalConfig &global_config) {
  if (IsHostRoce(endpoint) && global_config.RoceHixlBackend()) {
    return MakeShared<HostRoceEndpoint>(endpoint);
  }
  if (IsUbMemProtocol(endpoint.protocol)) {
    return MakeShared<UbMemEndpoint>(endpoint, global_config);
  }
  return MakeShared<HcommEndpoint>(endpoint);
}

EndpointPtr Endpoint::Create(const EndpointDesc &local_endpoint, const EndpointDesc &remote_endpoint,
                             const GlobalConfig &global_config) {
  if (IsHostRoce(local_endpoint) && global_config.RoceHixlBackend()) {
    return MakeShared<HostRoceEndpoint>(local_endpoint);
  }
  if (IsUbMemProtocol(local_endpoint.protocol)) {
    return MakeShared<UbMemEndpoint>(local_endpoint, remote_endpoint, global_config);
  }
  return MakeShared<HcommEndpoint>(local_endpoint, remote_endpoint);
}

EndpointPtr Endpoint::Create(const EndpointDesc &endpoint, bool need_host_va_mapping,
                             const GlobalConfig &global_config) {
  if (IsHostRoce(endpoint) && global_config.RoceHixlBackend()) {
    return MakeShared<HostRoceEndpoint>(endpoint);
  }
  if (IsUbMemProtocol(endpoint.protocol)) {
    return MakeShared<UbMemEndpoint>(endpoint, need_host_va_mapping, global_config);
  }
  return MakeShared<HcommEndpoint>(endpoint, need_host_va_mapping);
}

Endpoint::Endpoint(const EndpointDesc &endpoint)
    : endpoint_(endpoint),
      need_host_va_mapping_(IsDefaultHostVaMappingEnabled(endpoint)),
      caps_(DefaultEndpointCapabilities(endpoint)) {}

Endpoint::Endpoint(const EndpointDesc &local_endpoint, const EndpointDesc &remote_endpoint)
    : endpoint_(local_endpoint),
      need_host_va_mapping_(IsHostVaMappingEnabledForPair(local_endpoint, remote_endpoint)),
      caps_(DefaultEndpointCapabilities(local_endpoint)) {}

Endpoint::Endpoint(const EndpointDesc &endpoint, bool need_host_va_mapping)
    : endpoint_(endpoint), need_host_va_mapping_(need_host_va_mapping), caps_(DefaultEndpointCapabilities(endpoint)) {}

Endpoint::Endpoint(const EndpointDesc &endpoint, bool need_host_va_mapping, const EndpointCapabilities &caps)
    : endpoint_(endpoint), need_host_va_mapping_(need_host_va_mapping), caps_(caps) {}

Status Endpoint::Initialize() {
  std::lock_guard<std::mutex> lock(mutex_);
  HIXL_LOGI("EndpointCreate start, %s", EndpointToString(endpoint_).c_str());
  HIXL_CHK_HCCL_RET(EndpointCreate(handle_),
                    "EndpointCreate failed, endpoint=[%s]. "
                    "Please check whether the endpoint address is valid and available in the current environment.",
                    EndpointToString(endpoint_).c_str());
  HIXL_LOGI("EndpointCreate success, handle_:%p", handle_);
  return SUCCESS;
}

Status Endpoint::Finalize() {
  std::vector<ChannelPtr> channels;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    channels.reserve(channels_.size());
    for (const auto &it : channels_) {
      channels.emplace_back(it.second);
    }
    channels_.clear();
  }
  Status ret = SUCCESS;
  // Channel::Destroy may call back into the endpoint (memory deregistration), so it must run without mutex_.
  for (const auto &channel : channels) {
    const Status chn_ret = channel->Destroy();
    if (chn_ret != SUCCESS && ret == SUCCESS) {
      ret = chn_ret;
      HIXL_LOGE(chn_ret, "Destroy channel failed, ret: %d", chn_ret);
    }
  }
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto &it : reg_mems_) {
    if (handle_ != nullptr) {
      const HcclResult hccl_ret = MemUnreg(it.first);
      if (hccl_ret != HCCL_SUCCESS && ret == SUCCESS) {
        ret = ConvertHcommErrorToStatus(hccl_ret);
        HIXL_REPORT_ERR_MSG("E19999", "Call api:MemUnreg failed, ret:%d, ep_handle:%p, mem_handle:%p", hccl_ret,
                            handle_, it.first);
        HIXL_LOGE(ret, "Call api:MemUnreg failed, ret:%d, ep_handle:%p, mem_handle:%p", hccl_ret, handle_, it.first);
      }
    }
    if (it.second.registered_dev_mem != nullptr) {
      (void)HostRegisterProxy::UnregisterByDev(endpoint_.loc.device.devPhyId, it.second.mem.addr);
    }
  }
  reg_mems_.clear();
  if (handle_ != nullptr) {
    const HcclResult hccl_ret = EndpointDestroy();
    if (hccl_ret != HCCL_SUCCESS && ret == SUCCESS) {
      ret = ConvertHcommErrorToStatus(hccl_ret);
      HIXL_REPORT_ERR_MSG("E19999", "Call api:EndpointDestroy failed, ret:%d, ep_handle:%p", hccl_ret, handle_);
      HIXL_LOGE(ret, "Call api:EndpointDestroy failed, ret:%d, ep_handle:%p", hccl_ret, handle_);
    }
  }
  handle_ = nullptr;
  return ret;
}

EndpointHandle Endpoint::GetHandle() const {
  return handle_;
}

const EndpointDesc &Endpoint::GetEndpoint() const {
  return endpoint_;
}

bool Endpoint::NeedHostVaMapping() const {
  return need_host_va_mapping_;
}

CommEngine Endpoint::GetEngine() const {
  if (endpoint_.loc.locType == EndpointLocType::ENDPOINT_LOC_TYPE_HOST) {
    return CommEngine::COMM_ENGINE_CPU;
  }
  if (endpoint_.loc.locType == EndpointLocType::ENDPOINT_LOC_TYPE_DEVICE) {
    return CommEngine::COMM_ENGINE_AICPU;
  }
  return CommEngine::COMM_ENGINE_RESERVED;
}

Status Endpoint::RegisterMem(const char *mem_tag, const CommMem &mem, MemHandle &mem_handle) {
  std::lock_guard<std::mutex> lock(mutex_);
  HIXL_CHK_BOOL_RET_STATUS(handle_ != nullptr, FAILED, "[endpoint] RegisterMem called before Initialize");
  const CommMem *reg_mem = &mem;
  CommMem mapped_mem{};
  void *registered_dev_mem = nullptr;
  ScopeGuard reg_guard([this, &mem, &registered_dev_mem]() {
    if (registered_dev_mem != nullptr) {
      (void)HostRegisterProxy::UnregisterByDev(endpoint_.loc.device.devPhyId, mem.addr);
    }
  });
  if (mem.type == COMM_MEM_TYPE_HOST && NeedHostVaMapping()) {
    HIXL_CHK_STATUS_RET(
        HostRegisterProxy::RegisterByDev(endpoint_.loc.device.devPhyId, mem.addr, mem.size, registered_dev_mem),
        "Register mem failed, as host mem register failed, host addr=%p, size=%lu, devPhyId=%d.", mem.addr, mem.size,
        endpoint_.loc.device.devPhyId);
    mapped_mem.type = COMM_MEM_TYPE_DEVICE;
    mapped_mem.addr = registered_dev_mem;
    mapped_mem.size = mem.size;
    reg_mem = &mapped_mem;
  }
  const HcclResult hccl_ret = MemReg(mem_tag, reg_mem, &mem_handle);
  HIXL_CHK_BOOL_RET_STATUS(hccl_ret == HCCL_SUCCESS || hccl_ret == HCCL_E_AGAIN, ConvertHcommErrorToStatus(hccl_ret),
                           "Call api:MemReg failed, ret:%d, ep_handle:%p, addr:%p, size:%lu bytes", hccl_ret, handle_,
                           mem.addr, mem.size);
  reg_guard.Dismiss();
  HixlMemDesc desc{};
  if (mem_tag != nullptr) {
    desc.tag = mem_tag;
  }
  desc.mem = mem;
  desc.registered_dev_mem = registered_dev_mem;
  reg_mems_[mem_handle] = desc;
  HIXL_LOGI("MemReg success, ep_handle=%p, mem_handle=%p, addr=%p, size=%lu", handle_, mem_handle, mem.addr, mem.size);
  return SUCCESS;
}

Status Endpoint::DeregisterMem(MemHandle mem_handle) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = reg_mems_.find(mem_handle);
  if (it == reg_mems_.end()) {
    HIXL_LOGW("mem handle:%p is not registered, please use the handle generated by register mem.", mem_handle);
    return SUCCESS;
  }
  HIXL_CHK_BOOL_RET_STATUS(handle_ != nullptr, FAILED, "[endpoint] DeregisterMem called before Initialize");
  HIXL_CHK_HCCL_RET(MemUnreg(mem_handle));
  if (it->second.registered_dev_mem != nullptr) {
    HIXL_CHK_STATUS_RET(HostRegisterProxy::UnregisterByDev(endpoint_.loc.device.devPhyId, it->second.mem.addr),
                        "Deregister mem failed, as host mem unregister failed, host addr=%p, devPhyId=%d.",
                        it->second.mem.addr, endpoint_.loc.device.devPhyId);
  }
  reg_mems_.erase(it);
  return SUCCESS;
}

Status Endpoint::ExportMem(std::vector<HixlMemDesc> &mem_descs) {
  std::lock_guard<std::mutex> lock(mutex_);
  HIXL_CHK_BOOL_RET_STATUS(handle_ != nullptr, FAILED, "[endpoint] ExportMem called before Initialize");
  for (auto &it : reg_mems_) {
    if (it.second.export_desc == nullptr) {
      HIXL_CHK_HCCL_RET(MemExport(it.first, &it.second.export_desc, &it.second.export_len));
    }
    mem_descs.emplace_back(it.second);
  }
  return SUCCESS;
}

Status Endpoint::CreateChannel(const ChannelDesc &channel_desc, ChannelHandle &channel_handle, uint32_t timeout_ms) {
  HIXL_CHK_BOOL_RET_STATUS(handle_ != nullptr, FAILED, "[channel] CreateChannel called before Initialize");
  ChannelPtr channel = ChannelFactory::Create(*this);
  HIXL_CHECK_NOTNULL(channel);
  HIXL_CHK_STATUS_RET(channel->Connect(channel_desc, timeout_ms),
                      "[Channel] Create failed, local=[%s], remote=[%s], type=%d, index=%" PRIu64,
                      EndpointToString(endpoint_).c_str(), EndpointToString(channel_desc.remote_endpoint).c_str(),
                      static_cast<int32_t>(channel_desc.channel_type), channel_desc.channel_index);
  const ChannelHandle h = channel->GetHandle();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    channels_[h] = channel;
    channel_handle = h;
  }
  HIXL_LOGI("[Channel] Create success, handle=%" PRIu64 ", local=[%s], remote=[%s], type=%d, index=%" PRIu64, h,
            EndpointToString(endpoint_).c_str(), EndpointToString(channel_desc.remote_endpoint).c_str(),
            static_cast<int32_t>(channel_desc.channel_type), channel_desc.channel_index);
  return SUCCESS;
}

ChannelPtr Endpoint::GetChannel(ChannelHandle handle) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = channels_.find(handle);
  return it == channels_.end() ? nullptr : it->second;
}

Status Endpoint::DestroyChannel(ChannelHandle channel_handle) {
  ChannelPtr channel;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = channels_.find(channel_handle);
    HIXL_CHK_BOOL_RET_STATUS(it != channels_.end(), PARAM_INVALID,
                             "DestroyChannel failed, channel not found, handle=%lu", channel_handle);
    channel = it->second;
    channels_.erase(it);
  }
  // Channel::Destroy may callback into the endpoint (memory deregistration), so run it without mutex_.
  const Status ret = channel->Destroy();
  if (ret != SUCCESS) {
    // Keep the channel so Finalize can retry the teardown instead of dropping a half-destroyed transport.
    std::lock_guard<std::mutex> lock(mutex_);
    channels_[channel_handle] = channel;
    HIXL_LOGE(ret, "Channel::Destroy failed, handle=%lu", channel_handle);
    return ret;
  }
  HIXL_LOGI("Endpoint::DestroyChannel success, handle=%lu", channel_handle);
  return SUCCESS;
}

Status Endpoint::ImportMem(const void *mem_desc, uint32_t desc_len, CommMem &out_buf) {
  std::lock_guard<std::mutex> lock(mutex_);
  HIXL_CHECK_NOTNULL(mem_desc);
  HIXL_CHK_BOOL_RET_STATUS(handle_ != nullptr, FAILED, "[endpoint] ImportMem called before Initialize");
  HIXL_CHK_HCCL_RET(MemImport(mem_desc, desc_len, &out_buf));
  return SUCCESS;
}

Status Endpoint::UnimportMem(const void *mem_desc, uint32_t desc_len) {
  std::lock_guard<std::mutex> lock(mutex_);
  HIXL_CHK_BOOL_RET_STATUS(mem_desc != nullptr && desc_len > 0U, PARAM_INVALID, "[endpoint] invalid mem_desc");
  HIXL_CHK_BOOL_RET_STATUS(handle_ != nullptr, FAILED, "[endpoint] UnimportMem called before Initialize");
  HIXL_CHK_HCCL_RET(MemUnimport(mem_desc, desc_len));
  return SUCCESS;
}

Status Endpoint::GetListenPort(uint32_t &port) const {
  std::lock_guard<std::mutex> lock(mutex_);
  HIXL_CHK_BOOL_RET_STATUS(handle_ != nullptr, FAILED, "[endpoint] GetListenPort called before Initialize");
  const HcclResult hccl_ret = EndpointGetListenPort(port);
  // No listen socket. One warning is enough; do not log ERROR or report E19999.
  if (hccl_ret == HCCL_E_NOT_SUPPORT) {
    HIXL_LOGW("EndpointGetListenPort is not supported.");
    return UNSUPPORTED;
  }
  HIXL_CHK_BOOL_RET_STATUS(hccl_ret == HCCL_SUCCESS, ConvertHcommErrorToStatus(hccl_ret),
                           "Call hccl api:EndpointGetListenPort(port) failed, ret:%d", static_cast<int32_t>(hccl_ret));
  return SUCCESS;
}

Status Endpoint::GetMemDesc(MemHandle mem_handle, HixlMemDesc &desc) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = reg_mems_.find(mem_handle);
  if (it != reg_mems_.end()) {
    desc = it->second;
    return SUCCESS;
  }
  return PARAM_INVALID;
}

void Endpoint::SetPort(uint32_t port) {
  port_ = port;
}

uint32_t Endpoint::GetPort() const {
  return port_;
}

}  // namespace hixl
