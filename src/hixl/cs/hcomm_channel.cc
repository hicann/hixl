/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm_channel.h"

#include <chrono>
#include <cinttypes>
#include <thread>

#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/hixl_utils.h"
#include "common/scope_guard.h"
#include "endpoint.h"
#include "host_register_proxy.h"
#include "hixl_mem_store.h"
#include "proxy/hcomm_proxy.h"

namespace hixl {
namespace {
constexpr uint32_t kSingleChannel = 1U;
constexpr uint32_t kRoceQueueNum = 1U;  // ROCE QP数量默认值
constexpr int32_t kChannelConnectedStatus = 0;
constexpr int32_t kChannelConnectingStatus = 1;
constexpr int32_t kChannelUnknownStatus = -1;
constexpr auto kChannelStatusPollInterval = std::chrono::microseconds(1);

bool IsUrmaProtocol(CommProtocol protocol) {
  return protocol == COMM_PROTOCOL_UBC_CTP || protocol == COMM_PROTOCOL_UBOE || protocol == COMM_PROTOCOL_UBG;
}

void InitQueueDepth(const EndpointDesc &endpoint, const ChannelDesc &channel_desc, HcommChannelDesc &ch_desc) {
  if (!channel_desc.configurable_transfer_supported) {
    return;
  }
  const bool is_client = channel_desc.channel_type == ChannelType::kClient;
  const uint32_t data_depth =
      is_client ? CalculateTransportQueueDepth(channel_desc.max_transfer_count_per_batch) : kMinTransportQueueDepth;
  if (endpoint.protocol == COMM_PROTOCOL_ROCE) {
    ch_desc.roceAttr.sqDepth = data_depth;
    ch_desc.roceAttr.scqDepth = data_depth;
    HIXL_LOGI("[channel] RoCE queue depth set, role=%d, sq=%u, scq=%u", static_cast<int32_t>(channel_desc.channel_type),
              ch_desc.roceAttr.sqDepth, ch_desc.roceAttr.scqDepth);
  } else if (IsUrmaProtocol(endpoint.protocol)) {
    ch_desc.ubAttr.sqDepth = data_depth;
    ch_desc.ubAttr.scqDepth = data_depth;
    HIXL_LOGI("[channel] URMA queue depth set, protocol=%d, role=%d, sq=%u, scq=%u",
              static_cast<int32_t>(endpoint.protocol), static_cast<int32_t>(channel_desc.channel_type),
              ch_desc.ubAttr.sqDepth, ch_desc.ubAttr.scqDepth);
  }
}

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
}  // namespace

HcommChannel::HcommChannel(Endpoint &local_endpoint) : local_endpoint_(local_endpoint) {}

Status HcommChannel::Initialize(const ChannelContext &ctx) {
  return InitContext(ctx);
}

Status HcommChannel::DoConnect(const ChannelDesc &channel_desc, uint32_t timeout_ms) {
  const EndpointHandle ep_handle = local_endpoint_.GetHandle();
  HIXL_CHK_BOOL_RET_STATUS(ep_handle != nullptr, PARAM_INVALID, "[Channel] Connect called before endpoint Initialize");
  const CommEngine engine = local_endpoint_.GetEngine();
  HIXL_CHK_BOOL_RET_STATUS(engine != CommEngine::COMM_ENGINE_RESERVED, PARAM_INVALID,
                           "[Channel] invalid endpoint location=%d",
                           static_cast<int32_t>(local_endpoint_.GetEndpoint().loc.locType));
  HcommChannelDesc ch_desc{};
  std::string channel_name;
  HIXL_CHK_STATUS_RET(BuildChannelDesc(channel_desc, ch_desc, channel_name), "[Channel] BuildChannelDesc failed");
  HIXL_CHK_HCCL_RET(HcommProxy::ChannelCreate(ep_handle, engine, &ch_desc, kSingleChannel, &channel_handle_),
                    "[Channel] ChannelCreate failed, local=[%s], remote=[%s], type=%d, index=%" PRIu64,
                    EndpointToString(local_endpoint_.GetEndpoint()).c_str(),
                    EndpointToString(channel_desc.remote_endpoint).c_str(),
                    static_cast<int32_t>(channel_desc.channel_type), channel_desc.channel_index);
  const Status ret = WaitChannelConnected(timeout_ms);
  if (ret != SUCCESS) {
    const ChannelHandle handle = channel_handle_;
    const HcclResult destroy_ret = HcommProxy::ChannelDestroy(&handle, kSingleChannel);
    if (destroy_ret != HCCL_SUCCESS) {
      HIXL_LOGW("[Channel] Destroy channel after failed connect failed, handle=%" PRIu64 ", ret=0x%X", handle,
                static_cast<uint32_t>(destroy_ret));
    }
    channel_handle_ = 0UL;
    return ret;
  }
  HIXL_LOGI("[Channel] Connect success, handle=%" PRIu64, channel_handle_);
  return SUCCESS;
}

Status HcommChannel::Disconnect() {
  if (channel_handle_ == 0UL) {
    return SUCCESS;
  }
  const ChannelHandle handle = channel_handle_;
  HIXL_CHK_HCCL_RET(HcommProxy::ChannelDestroy(&handle, kSingleChannel),
                    "[Channel] ChannelDestroy failed, handle=%" PRIu64, handle);
  channel_handle_ = 0UL;
  HIXL_LOGI("[Channel] Disconnect success, handle=%" PRIu64, handle);
  return SUCCESS;
}

Status HcommChannel::Destroy() {
  const Status ret = Disconnect();
  if (ret == SUCCESS) {
    initialized_ = false;
  }
  return ret;
}

Status HcommChannel::RegMem(const char *mem_tag, const CommMem *mem, MemHandle *mem_handle) {
  HIXL_CHK_STATUS_RET(EnsureInitialized("RegMem"), "[Channel] EnsureInitialized failed");
  HIXL_CHECK_NOTNULL(mem_handle);
  *mem_handle = nullptr;
  HIXL_CHECK_NOTNULL(mem);
  HIXL_CHK_BOOL_RET_STATUS(mem->addr != nullptr && mem->size > 0U, PARAM_INVALID,
                           "[Channel] Invalid memory registration, mem_addr:%p, mem_size:%lu bytes", mem->addr,
                           mem->size);
  auto check_result = ctx_.mem_store->CheckMemoryForRegister(false, mem->addr, mem->size);
  if (check_result) {
    HIXL_LOGE(PARAM_INVALID,
              "[Channel] Memory registration failed. This memory may overlap with the already recorded memory. "
              "Please check Mem, mem_addr:%p, mem_size:%lu bytes.",
              mem->addr, mem->size);
    return PARAM_INVALID;
  }
  MemHandle ep_mem_handle = nullptr;
  HIXL_CHK_STATUS_RET(local_endpoint_.RegisterMem(mem_tag, *mem, ep_mem_handle),
                      "[Channel] Failed to register client endpoint mem.");
  *mem_handle = ep_mem_handle;
  HIXL_DISMISSABLE_GUARD(mem_rollback, ([this, ep_mem_handle, mem_handle]() {
                           const Status rollback_status = local_endpoint_.DeregisterMem(ep_mem_handle);
                           if (rollback_status != SUCCESS) {
                             HIXL_LOGE(rollback_status,
                                       "[Channel] Failed to rollback client endpoint mem registration, "
                                       "mem_handle:%p, ret:%u",
                                       ep_mem_handle, static_cast<uint32_t>(rollback_status));
                           }
                           *mem_handle = nullptr;
                         }));
  bool is_host_mem = mem->type == COMM_MEM_TYPE_HOST;
  void *register_dev_addr = nullptr;
  const auto &ep_desc = local_endpoint_.GetEndpoint();
  if (is_host_mem && local_endpoint_.NeedHostVaMapping()) {
    HIXL_CHK_STATUS_RET(
        HostRegisterProxy::GetRegisteredDeviceAddrByDev(ep_desc.loc.device.devPhyId, mem->addr, register_dev_addr),
        "Failed to get registered device addr, devPhyId=%d, addr=%p", ep_desc.loc.device.devPhyId, mem->addr);
  }
  HIXL_CHK_STATUS_RET(ctx_.mem_store->RecordMemory(false, mem->addr, mem->size, is_host_mem, register_dev_addr),
                      "[Channel] Client record memory failed, mem_addr:%p, mem_size:%lu bytes", mem->addr, mem->size);
  HIXL_DISMISS_GUARD(mem_rollback);
  HIXL_LOGI("[Channel] Memory register success. ");
  return SUCCESS;
}

Status HcommChannel::UnregMem(MemHandle mem_handle) {
  HIXL_CHK_STATUS_RET(EnsureInitialized("UnregMem"), "[Channel] EnsureInitialized failed");
  HIXL_CHECK_NOTNULL(mem_handle);
  HixlMemDesc desc;
  Status query_status = local_endpoint_.GetMemDesc(mem_handle, desc);
  HIXL_CHK_BOOL_RET_STATUS(query_status == SUCCESS, PARAM_INVALID, "[Channel] GetMemDesc failed, mem_handle:%p, ret:%u",
                           mem_handle, static_cast<uint32_t>(query_status));
  Status result = local_endpoint_.DeregisterMem(mem_handle);
  HIXL_CHK_BOOL_RET_STATUS(result == SUCCESS, PARAM_INVALID,
                           "[Channel] Failed to deregister client endpoint mem, mem_handle:%p, addr:%p, ret:%u",
                           mem_handle, desc.mem.addr, static_cast<uint32_t>(result));
  HIXL_CHK_STATUS_RET(ctx_.mem_store->UnrecordMemory(false, desc.mem.addr),
                      "[Channel] Client unrecord memory failed, mem_addr:%p", desc.mem.addr);
  HIXL_EVENT("[Channel] deregister mem success, handle:%p, addr:%p, size:%lu bytes", mem_handle, desc.mem.addr,
             desc.mem.size);
  return SUCCESS;
}

Status HcommChannel::SetRemoteMemInfo(const CommMem &trans_flag) {
  HIXL_CHK_STATUS_RET(EnsureInitialized("SetRemoteMemInfo"), "[Channel] EnsureInitialized failed");
  remote_trans_flag_ = trans_flag;
  return SUCCESS;
}

Status HcommChannel::BuildChannelDesc(const ChannelDesc &channel_desc, HcommChannelDesc &ch_desc,
                                      std::string &channel_name) const {
  const EndpointDesc &endpoint = local_endpoint_.GetEndpoint();
  HIXL_CHK_HCCL_RET(static_cast<HcclResult>(HcommChannelDescInit(&ch_desc, 1)));
  ch_desc.role = static_cast<HcommSocketRole>(channel_desc.channel_type);
  ch_desc.remoteEndpoint = channel_desc.remote_endpoint;
  ch_desc.notifyNum = 1U;
  ch_desc.exchangeAllMems = true;
  if (endpoint.protocol == CommProtocol::COMM_PROTOCOL_ROCE) {
    ch_desc.roceAttr.tc = static_cast<uint32_t>(channel_desc.tc);
    ch_desc.roceAttr.sl = static_cast<uint32_t>(channel_desc.sl);
    ch_desc.roceAttr.retryCnt = channel_desc.retry_cnt;
    ch_desc.roceAttr.retryInterval = channel_desc.retry_interval;
    ch_desc.roceAttr.queueNum = kRoceQueueNum;
    HIXL_LOGI("[channel] ROCE attributes set, tc=%u, sl=%u, retryCnt=%u, retryInterval=%u, queueNum=%u",
              ch_desc.roceAttr.tc, ch_desc.roceAttr.sl, ch_desc.roceAttr.retryCnt, ch_desc.roceAttr.retryInterval,
              ch_desc.roceAttr.queueNum);
  }
  InitQueueDepth(endpoint, channel_desc, ch_desc);
  ch_desc.port = local_endpoint_.GetPort();
  if (channel_desc.qos != kQosUnset) {
    ch_desc.qos = static_cast<uint32_t>(channel_desc.qos);
    HIXL_LOGI("[channel] attributes set, qos=%u", ch_desc.qos);
  } else {
    HIXL_LOGI("[channel] attributes set, qos unset");
  }
  HIXL_CHK_STATUS_RET(BuildChannelName(endpoint, channel_desc, local_endpoint_.GetPort(), channel_name));
  ch_desc.channelName = channel_name.c_str();
  HIXL_LOGI("[channel] channelName=%s", ch_desc.channelName);
  return SUCCESS;
}

Status HcommChannel::WaitChannelConnected(uint32_t timeout_ms) const {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (true) {
    const ChannelHandle handle = GetHandle();
    int32_t status = kChannelUnknownStatus;
    HIXL_CHK_HCCL_RET(HcommProxy::ChannelGetStatus(&handle, kSingleChannel, &status));
    if (status == kChannelConnectedStatus) {
      return SUCCESS;
    }
    HIXL_CHK_BOOL_RET_STATUS(status == kChannelConnectingStatus, FAILED,
                             "Wait channel connected failed, handle:%lu, status:%d", handle, status);
    HIXL_CHK_BOOL_RET_STATUS(std::chrono::steady_clock::now() < deadline, TIMEOUT,
                             "Wait channel connected timed out, handle:%lu, status:%d, timeout:%u ms", handle, status,
                             timeout_ms);
    std::this_thread::sleep_for(kChannelStatusPollInterval);
  }
}

}  // namespace hixl
