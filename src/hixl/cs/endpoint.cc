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

#include "host_register_proxy.h"
#include "common/hixl_utils.h"
#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/scope_guard.h"
#include "proxy/hcomm_proxy.h"

namespace hixl {
namespace {
constexpr uint32_t kRoceQueueNum = 1U;  // ROCE QP数量默认值

Status BuildChannelName(const EndpointDesc &endpoint, const ChannelDesc &channel_desc, uint32_t port,
                        std::string &channel_name) {
  // channelName作为两端channel业务匹配标识，两端需一致：
  // client_ep + server_ep + server_port + server_channel_index(进程级自增)
  const EndpointDesc &client_ep =
      (channel_desc.channel_type == ChannelType::kClient) ? endpoint : channel_desc.remote_endpoint;
  const EndpointDesc &server_ep =
      (channel_desc.channel_type == ChannelType::kClient) ? channel_desc.remote_endpoint : endpoint;
  channel_name = FormatCommAddr(client_ep.commAddr) + "_" + FormatCommAddr(server_ep.commAddr) + "_" +
                 std::to_string(port) + "_" + std::to_string(channel_desc.channel_index);
  HIXL_CHK_BOOL_RET_STATUS(channel_name.length() <= HCOMM_CHANNEL_NAME_MAX_LEN, PARAM_INVALID,
                           "[channel] channelName length=%zu exceeds max=%u, channelName=%s", channel_name.length(),
                           HCOMM_CHANNEL_NAME_MAX_LEN, channel_name.c_str());
  return SUCCESS;
}

Status InitChannelDesc(const EndpointDesc &endpoint, const ChannelDesc &channel_desc, uint32_t port,
                       HcommChannelDesc &ch_desc, std::string &channel_name) {
  HIXL_CHK_HCCL_RET(static_cast<HcclResult>(HcommChannelDescInit(&ch_desc, 1)));
  ch_desc.role = static_cast<HcommSocketRole>(channel_desc.channel_type);
  ch_desc.remoteEndpoint = channel_desc.remote_endpoint;
  ch_desc.notifyNum = 1U;
  ch_desc.exchangeAllMems = true;
  if (endpoint.protocol == CommProtocol::COMM_PROTOCOL_ROCE) {
    ch_desc.roceAttr.tc = static_cast<uint32_t>(channel_desc.tc);
    ch_desc.roceAttr.sl = static_cast<uint32_t>(channel_desc.sl);
    ch_desc.roceAttr.queueNum = kRoceQueueNum;
    HIXL_LOGI("[channel] ROCE attributes set, tc=%u, sl=%u, queueNum=%u", ch_desc.roceAttr.tc, ch_desc.roceAttr.sl,
              ch_desc.roceAttr.queueNum);
  }
  ch_desc.port = port;
  // need add qos at here when HcommChannelDesc exist qos
  ch_desc.qos = static_cast<uint32_t>(channel_desc.qos);
  HIXL_LOGI("[channel] attributes set, qos=%u", ch_desc.qos);
  HIXL_CHK_STATUS_RET(BuildChannelName(endpoint, channel_desc, port, channel_name));
  ch_desc.channelName = channel_name.c_str();
  HIXL_LOGI("[channel] channelName=%s", ch_desc.channelName);
  return SUCCESS;
}
}  // namespace

Status Endpoint::Initialize() {
  std::lock_guard<std::mutex> lock(mutex_);
  HIXL_LOGI("HcommEndpointCreate start, %s", EndpointToString(endpoint_).c_str());
  HIXL_CHK_HCCL_RET(HcommProxy::EndpointCreate(&endpoint_, &handle_));
  HIXL_LOGI("HcommEndpointCreate success, handle_:%p", handle_);
  return SUCCESS;
}

Status Endpoint::Finalize() {
  std::lock_guard<std::mutex> lock(mutex_);
  Status ret = SUCCESS;
  for (const auto &it : channels_) {
    auto chn_ret = it.second->Destroy();
    if (chn_ret != SUCCESS) {
      ret = chn_ret;
      HIXL_LOGE(chn_ret, "Destroy channel failed, ret: %d", chn_ret);
    }
  }

  for (const auto &it : reg_mems_) {
    auto mem_handle = it.first;
    auto hccl_ret = HcommProxy::MemUnreg(handle_, mem_handle);
    if (hccl_ret != HCCL_SUCCESS) {
      ret = hixl::HcclError2Status(hccl_ret);
      HIXL_LOGE(ret, "HcommMemUnreg failed, ret: %d", hccl_ret);
    }

    if (it.second.registered_dev_mem != nullptr) {
      (void)HostRegisterProxy::UnregisterByDev(endpoint_.loc.device.devPhyId, it.second.mem.addr);
    }
  }
  reg_mems_.clear();
  auto hccl_ret = HcommProxy::EndpointDestroy(handle_);
  if (hccl_ret != HCCL_SUCCESS) {
    ret = hixl::HcclError2Status(hccl_ret);
    HIXL_LOGE(ret, "HcommEndpointDestroy failed, ret: %d", hccl_ret);
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

Status Endpoint::RegisterMem(const char *mem_tag, const CommMem &mem, MemHandle &mem_handle) {
  std::lock_guard<std::mutex> lock(mutex_);
  Status ret = SUCCESS;
  HcclResult hccl_ret = HCCL_SUCCESS;
  void *registered_dev_mem = nullptr;
  ScopeGuard reg_guard([this, &mem, &registered_dev_mem]() {
    if (registered_dev_mem != nullptr) {
      (void)HostRegisterProxy::UnregisterByDev(endpoint_.loc.device.devPhyId, mem.addr);
    }
  });
  if (mem.type == COMM_MEM_TYPE_HOST && IsHostRegisterMappedProtocol(endpoint_.protocol)) {
    HIXL_CHK_STATUS_RET(
        HostRegisterProxy::RegisterByDev(endpoint_.loc.device.devPhyId, mem.addr, mem.size, registered_dev_mem),
        "Register mem failed, as host mem register failed, host addr=%p, size=%lu, devPhyId=%d.", mem.addr, mem.size,
        endpoint_.loc.device.devPhyId);

    CommMem reg_mem{};
    reg_mem.type = COMM_MEM_TYPE_DEVICE;
    reg_mem.addr = registered_dev_mem;
    reg_mem.size = mem.size;
    HIXL_LOGI("HcommMemReg start (host-mapped), ep_handle=%p, host_addr=%p, dev_addr=%p, size=%lu", handle_, mem.addr,
              registered_dev_mem, mem.size);
    hccl_ret = HcommProxy::MemReg(handle_, mem_tag, &reg_mem, &mem_handle);
  } else {
    hccl_ret = HcommProxy::MemReg(handle_, mem_tag, &mem, &mem_handle);
  }
  if (hccl_ret != HCCL_SUCCESS && hccl_ret != HCCL_E_AGAIN) {
    ret = hixl::HcclError2Status(hccl_ret);
    HIXL_LOGE(ret, "HcommMemReg failed, hccl_ret %d", hccl_ret);
    return ret;
  }
  reg_guard.Dismiss();
  HixlMemDesc desc{};
  if (mem_tag != nullptr) {
    desc.tag = mem_tag;
  }
  desc.mem = mem;
  desc.registered_dev_mem = registered_dev_mem;
  reg_mems_[mem_handle] = desc;
  HIXL_LOGI("HcommMemReg success, ep_handle=%p, mem_handle=%p, addr=%p, size=%lu", handle_, mem_handle, mem.addr,
            mem.size);
  return ret;
}

Status Endpoint::DeregisterMem(MemHandle mem_handle) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = reg_mems_.find(mem_handle);
  if (it == reg_mems_.end()) {
    HIXL_LOGW("mem handle:%p is not registered, please use the handle generated by register mem.", mem_handle);
    return SUCCESS;
  }
  HIXL_CHK_HCCL_RET(HcommProxy::MemUnreg(handle_, mem_handle));
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
  for (auto &it : reg_mems_) {
    auto mem_handle = it.first;
    auto &mem = it.second;
    if (mem.export_desc == nullptr) {
      HIXL_CHK_HCCL_RET(HcommProxy::MemExport(handle_, mem_handle, &mem.export_desc, &mem.export_len));
    }
    mem_descs.emplace_back(mem);
  }
  return SUCCESS;
}

Status Endpoint::CreateChannel(const ChannelDesc &channel_desc, ChannelHandle &channel_handle) {
  HIXL_CHK_BOOL_RET_STATUS(handle_ != nullptr, FAILED, "[channel] CreateChannel called before Initialize");
  CommEngine engine = CommEngine::COMM_ENGINE_RESERVED;
  if (endpoint_.loc.locType == EndpointLocType::ENDPOINT_LOC_TYPE_HOST) {
    engine = CommEngine::COMM_ENGINE_CPU;
  } else if (endpoint_.loc.locType == EndpointLocType::ENDPOINT_LOC_TYPE_DEVICE) {
    engine = CommEngine::COMM_ENGINE_AICPU;
  } else {
    HIXL_LOGE(PARAM_INVALID, "[channel] invalid endpoint location=%d", static_cast<int32_t>(endpoint_.loc.locType));
    return PARAM_INVALID;
  }
  HcommChannelDesc ch_desc{};
  std::string channel_name;
  HIXL_CHK_STATUS_RET(InitChannelDesc(endpoint_, channel_desc, port_, ch_desc, channel_name));

  ChannelPtr channel = MakeShared<Channel>();
  HIXL_CHECK_NOTNULL(channel);

  Status ret = channel->Create(handle_, ch_desc, engine);
  HIXL_CHK_STATUS_RET(ret, "[Channel] Create failed, local=[%s], remote=[%s], type=%d, index=%" PRIu64,
                      EndpointToString(endpoint_).c_str(), EndpointToString(channel_desc.remote_endpoint).c_str(),
                      static_cast<int32_t>(channel_desc.channel_type), channel_desc.channel_index);
  ChannelHandle h = channel->GetChannelHandle();
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

Status Endpoint::DestroyChannel(ChannelHandle channel_handle) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = channels_.find(channel_handle);
  HIXL_CHK_BOOL_RET_STATUS(it != channels_.end(), PARAM_INVALID, "DestroyChannel failed, channel not found, handle=%lu",
                           channel_handle);

  Status ret = it->second->Destroy();
  HIXL_CHK_STATUS_RET(ret, "Channel::Destroy failed, handle=%lu", channel_handle);

  channels_.erase(it);
  HIXL_LOGI("Endpoint::DestroyChannel success, handle=%lu", channel_handle);
  return SUCCESS;
}

Status Endpoint::MemImport(const void *mem_desc, uint32_t desc_len, CommMem &out_buf) {
  std::lock_guard<std::mutex> lock(mutex_);
  HIXL_CHECK_NOTNULL(handle_);
  HIXL_CHECK_NOTNULL(mem_desc);

  HIXL_CHK_HCCL_RET(HcommProxy::MemImport(handle_, mem_desc, desc_len, &out_buf));
  return SUCCESS;
}

Status Endpoint::GetMemDesc(MemHandle mem_handle, HixlMemDesc &desc) {
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
