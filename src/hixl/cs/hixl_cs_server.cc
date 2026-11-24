/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hixl_cs_server.h"
#include "common/tls/tls_manager.h"
#include <algorithm>
#include <atomic>
#include <cinttypes>
#include <memory>
#include <sys/epoll.h>
#include <unistd.h>
#include "nlohmann/json.hpp"
#include "common/hixl_checker.h"
#include "common/ctrl_msg.h"
#include "common/scope_guard.h"
#include "common/ctrl_msg_plugin.h"
#include "securec.h"
#include "transfer_pool.h"
#include "common/hixl_inner_types.h"

static inline void to_json(nlohmann::json &j, const CommMem &m) {
  j = nlohmann::json{};
  j["type"] = m.type;
  j["addr"] = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(m.addr));
  j["size"] = m.size;
}

namespace hixl {
namespace {
constexpr uint32_t kDefaultTransferPoolSize = 128U;  // 与 HixlCSClient 侧设备池大小一致
constexpr int32_t kMaxEventsNum = 128;               // epoll_wait并发处理事件数量，减少epoll系统调用
constexpr int32_t kEpollWaitTimeInMillis = 100;      // epoll_wait等待超时时间
constexpr uint32_t kMsPerSecond = 1000U;
// server端进程级channel索引计数器，用于两端构建一致的channelName
std::atomic<uint64_t> g_next_server_channel_index{0UL};

bool IsUbEndpoint(CommProtocol protocol) {
  return protocol == COMM_PROTOCOL_UBC_TP || protocol == COMM_PROTOCOL_UBC_CTP;
}

bool IsValidMemType(CommMemType type) {
  return type == COMM_MEM_TYPE_HOST || type == COMM_MEM_TYPE_DEVICE;
}

bool IsDeviceEndpointDesc(const EndpointDesc &endpoint) {
  return endpoint.loc.locType == ENDPOINT_LOC_TYPE_DEVICE;
}

bool IsUbCtpEndpoint(const EndpointDesc &endpoint) {
  return endpoint.protocol == COMM_PROTOCOL_UBC_CTP;
}

bool IsDeviceUbCtpEndpoint(const EndpointDesc &endpoint) {
  return IsDeviceEndpointDesc(endpoint) && IsUbCtpEndpoint(endpoint);
}

bool IsDeviceUboeEndpoint(const EndpointDesc &endpoint) {
  return IsDeviceEndpointDesc(endpoint) && endpoint.protocol == COMM_PROTOCOL_UBOE;
}

bool IsDeviceUbgEndpoint(const EndpointDesc &endpoint) {
  return IsDeviceEndpointDesc(endpoint) && endpoint.protocol == COMM_PROTOCOL_UBG;
}

bool AreUbCtpEndpointsAllDevice(const EndpointDesc *endpoint_list, uint32_t list_num) {
  for (uint32_t i = 0U; i < list_num; ++i) {
    if (IsUbCtpEndpoint(endpoint_list[i]) && !IsDeviceEndpointDesc(endpoint_list[i])) {
      return false;
    }
  }
  return true;
}

bool NeedServerHostVaMapping(const EndpointDesc &endpoint, bool ub_ctp_endpoints_all_device) {
  return IsDeviceUboeEndpoint(endpoint) || IsDeviceUbgEndpoint(endpoint) ||
         (ub_ctp_endpoints_all_device && IsDeviceUbCtpEndpoint(endpoint));
}

bool ShouldRegisterEndpointForMem(const EndpointPtr &endpoint, CommMemType mem_type) {
  const auto &endpoint_desc = endpoint->GetEndpoint();
  if (!IsUbEndpoint(endpoint_desc.protocol)) {
    return true;
  }
  if (mem_type == COMM_MEM_TYPE_HOST) {
    return endpoint_desc.loc.locType == ENDPOINT_LOC_TYPE_HOST || endpoint->NeedHostVaMapping();
  }
  if (mem_type == COMM_MEM_TYPE_DEVICE) {
    return endpoint_desc.loc.locType == ENDPOINT_LOC_TYPE_DEVICE;
  }
  return false;
}

// 回滚本次 RegisterMem 中已成功注册的 endpoint: 逆序注销, 单点注销失败仅记录错误并继续
void RollbackRegisteredMem(const EndpointStore &endpoint_store, const std::vector<EndpointMemInfo> &ep_mem_infos) {
  for (auto it = ep_mem_infos.crbegin(); it != ep_mem_infos.crend(); ++it) {
    auto endpoint = endpoint_store.GetEndpoint(it->endpoint_handle);
    if (endpoint == nullptr) {
      continue;
    }
    auto ret = endpoint->DeregisterMem(it->mem_handle);
    if (ret != SUCCESS) {
      HIXL_LOGE(ret, "Failed to rollback registered mem, mem_handle:%p.", it->mem_handle);
    }
  }
}
}  // namespace

std::unique_ptr<hixl::TemporaryRtContext> HixlCSServer::GetContextGuard() const {
  if (device_id_ >= 0) {
    auto *pool = TransferPool::GetInstance(device_id_);
    if (pool == nullptr) {
      return nullptr;
    }
    aclrtContext ctx = pool->GetContext();
    if (ctx != nullptr) {
      return MakeUnique<hixl::TemporaryRtContext>(ctx);
    }
  }
  if (dev_flag_context_ != nullptr) {
    return MakeUnique<hixl::TemporaryRtContext>(dev_flag_context_);
  }
  return nullptr;  // 不切换 context
}

Status HixlCSServer::InitTransFinishedFlag() {
  std::vector<EndpointPtr> host_endpoints;
  std::vector<EndpointPtr> device_endpoints;
  bool needs_transfer_pool = false;
  bool has_non_hccs_device_ep = false;
  for (auto handle : endpoint_store_.GetAllEndpointHandles()) {
    auto endpoint = endpoint_store_.GetEndpoint(handle);
    if (endpoint == nullptr) {
      continue;
    }
    const auto &desc = endpoint->GetEndpoint();
    const TransFlagKind flag_kind = endpoint->Capabilities().builtin_trans_flag;
    if (flag_kind == TransFlagKind::kHost) {
      host_endpoints.emplace_back(std::move(endpoint));
    } else if (flag_kind == TransFlagKind::kDeviceOnly || flag_kind == TransFlagKind::kDeviceWithPool) {
      if (flag_kind == TransFlagKind::kDeviceWithPool) {
        needs_transfer_pool = true;
        if (desc.protocol != COMM_PROTOCOL_HCCS) {
          has_non_hccs_device_ep = true;
        }
      }
      device_endpoints.emplace_back(std::move(endpoint));
    }
  }
  if (!host_endpoints.empty()) {
    HIXL_CHK_STATUS_RET(RegisterHostTransFinishedFlag(host_endpoints), "Failed to reg HOST trans finished flag");
  }
  if (!device_endpoints.empty()) {
    HIXL_CHK_STATUS_RET(RegisterDeviceTransFinishedFlag(device_endpoints, needs_transfer_pool, has_non_hccs_device_ep),
                        "Failed to reg DEVICE trans finished flag");
  }
  return SUCCESS;
}

Status HixlCSServer::RegisterHostTransFinishedFlag(const std::vector<EndpointPtr> &host_endpoints) {
  void *host_flag = nullptr;
  int64_t page_size = sysconf(_SC_PAGESIZE);
  HIXL_CHK_BOOL_RET_STATUS(page_size > 0, FAILED, "sysconf(_SC_PAGESIZE) failed, errno=%d, errmsg=%s", errno,
                           strerror(errno));
  size_t align_size = static_cast<size_t>(page_size);
  // Register host mem addr need aligned by page size.
  int ret = posix_memalign(&host_flag, align_size, sizeof(int64_t));
  HIXL_CHK_BOOL_RET_STATUS(ret == 0 && host_flag != nullptr, FAILED,
                           "HOST trans finished flag posix_memalign failed, ret:%d, page_size=%zu, alloc size=%zu.",
                           ret, align_size, sizeof(int64_t));
  HIXL_DISMISSABLE_GUARD(host_flag_guard, ([host_flag]() { free(host_flag); }));
  *static_cast<int64_t *>(host_flag) = 1;
  CommMem mem{};
  mem.type = COMM_MEM_TYPE_HOST;
  mem.addr = host_flag;
  mem.size = sizeof(int64_t);
  for (const auto &endpoint : host_endpoints) {
    MemHandle ep_mem_handle = nullptr;
    HIXL_CHK_STATUS_RET(endpoint->RegisterMem(kBuiltinHostTransFlagName, mem, ep_mem_handle),
                        "Failed to reg HOST trans finished flag");
  }
  host_trans_flag_ = host_flag;
  HIXL_DISMISS_GUARD(host_flag_guard);
  return SUCCESS;
}

Status HixlCSServer::RegisterDeviceTransFinishedFlag(const std::vector<EndpointPtr> &device_endpoints,
                                                     bool needs_transfer_pool, bool resolve_notify_addr) {
  if (!needs_transfer_pool) {
    return RegisterDeviceFlagMemory(device_endpoints);
  }
  int32_t dev_id = 0;
  HIXL_CHK_ACL_RET(aclrtGetDevice(&dev_id), "Failed to aclrtGetDevice for CS server TransferPool");
  hixl::TemporaryRtContext with_context(nullptr);
  auto *pool = TransferPool::GetInstance(dev_id);
  HIXL_CHECK_NOTNULL(pool);
  HIXL_CHK_STATUS_RET(pool->Initialize(global_config_.MaxActiveChannels().value_or(kDefaultTransferPoolSize)),
                      "Failed to init TransferPool for CS server, dev_id:%d", dev_id);
  HIXL_DISMISSABLE_GUARD(pool_rollback, ([dev_id]() {
                           auto *p = TransferPool::GetInstance(dev_id);
                           if (p != nullptr) {
                             p->Finalize();
                           }
                         }));
  if (resolve_notify_addr) {
    HIXL_CHK_STATUS_RET(pool->ResolveNotifyAddr(), "Failed to resolve TransferPool notify address for CS server");
  }
  HIXL_CHK_STATUS_RET(RegisterDeviceFlagMemory(device_endpoints), "Failed to reg DEVICE trans finished flag");
  HIXL_DISMISS_GUARD(pool_rollback);
  device_id_ = dev_id;
  return SUCCESS;
}

Status HixlCSServer::RegisterDeviceFlagMemory(const std::vector<EndpointPtr> &endpoints) {
  void *dev_flag = nullptr;
  HIXL_CHK_ACL_RET(
      aclrtMalloc(&dev_flag, sizeof(int64_t),
                  static_cast<aclrtMemMallocPolicy>(ACL_MEM_TYPE_HIGH_BAND_WIDTH | ACL_MEM_MALLOC_HUGE_ONLY)));
  HIXL_DISMISSABLE_GUARD(dev_flag_guard, ([&dev_flag]() { FreeDeviceMem(dev_flag); }));
  const int64_t val = 1;
  HIXL_CHK_ACL_RET(aclrtMemcpy(dev_flag, sizeof(int64_t), &val, sizeof(int64_t), ACL_MEMCPY_HOST_TO_DEVICE));
  CommMem mem{};
  mem.type = COMM_MEM_TYPE_DEVICE;
  mem.addr = dev_flag;
  mem.size = sizeof(int64_t);
  for (const auto &endpoint : endpoints) {
    MemHandle ep_mem_handle = nullptr;
    HIXL_CHK_STATUS_RET(endpoint->RegisterMem(kBuiltinDeviceTransFlagName, mem, ep_mem_handle),
                        "Failed to reg DEVICE trans finished flag");
  }
  const aclError ctx_ret = aclrtGetCurrentContext(&dev_flag_context_);
  if (ctx_ret != ACL_SUCCESS) {
    HIXL_LOGW("[HixlServer] aclrtGetCurrentContext for device flag failed, ret=%d", static_cast<int32_t>(ctx_ret));
  }
  dev_trans_flag_ = dev_flag;
  HIXL_DISMISS_GUARD(dev_flag_guard);
  return SUCCESS;
}

HixlCSServer::HixlCSServer(const char *ip, uint32_t port, const GlobalConfig &global_config)
    : ip_(ip), port_(port), global_config_(global_config) {
  if (TlsManager::GetInstance().IsInitialized()) {
    (void)TlsManager::GetInstance().GetTlsEnabled(tls_enabled_);
  }
}

Status HixlCSServer::Initialize(const EndpointDesc *endpoint_list, uint32_t list_num) {
  HIXL_CHECK_NOTNULL(endpoint_list);
  HIXL_CHK_BOOL_RET_STATUS(list_num > 0, PARAM_INVALID, "endpoint list num:%u is invalid, must > 0", list_num);
  if (TlsManager::GetInstance().IsInitialized()) {
    (void)TlsManager::GetInstance().GetTlsEnabled(tls_enabled_);
  }
  const bool ub_ctp_endpoints_all_device = AreUbCtpEndpointsAllDevice(endpoint_list, list_num);
  HIXL_DISMISSABLE_GUARD(init_rollback, ([this]() { (void)Finalize(); }));
  bool all_endpoints_support_configurable_transfer = true;
  for (uint32_t i = 0U; i < list_num; ++i) {
    EndpointHandle handle = nullptr;
    HIXL_CHK_STATUS_RET(endpoint_store_.CreateEndpoint(
                            endpoint_list[i], handle,
                            NeedServerHostVaMapping(endpoint_list[i], ub_ctp_endpoints_all_device), global_config_),
                        "Failed to create endpoint, index:%u, %s", i, EndpointToString(endpoint_list[i]).c_str());
    const auto ep = endpoint_store_.GetEndpoint(handle);
    if (ep == nullptr || !ep->Capabilities().supports_configurable_transfer) {
      all_endpoints_support_configurable_transfer = false;
    }
  }
  configurable_transfer_supported_ =
      all_endpoints_support_configurable_transfer || IsHcommConfigurableTransferSupported();
  HIXL_CHK_STATUS_RET(RegisterMsgProcessors(), "Failed to register server message processors");
  listener_connection_ = MakeShared<CtrlMsgPlugin>();
  HIXL_CHECK_NOTNULL(listener_connection_);
  HIXL_CHK_STATUS_RET(listener_connection_->Initialize(), "Failed to initialize listener TLS");
  HIXL_CHK_STATUS_RET(msg_handler_.Initialize(), "Failed to initialize msg handler");
  HIXL_CHK_STATUS_RET(InitTransFinishedFlag(), "Failed to init trans finished flag");
  HIXL_DISMISS_GUARD(init_rollback);
  HIXL_EVENT("[HixlServer] init success, endpoint_list_num:%u", list_num);
  return SUCCESS;
}

Status HixlCSServer::RegisterMsgProcessors() {
  HIXL_CHK_STATUS_RET(RegProc(CtrlMsgType::kMatchEndpointReq,
                              [this](const CtrlMsgPluginPtr &connection, const char *msg, uint64_t msg_len) -> Status {
                                return this->MatchEndpointMsg(connection, msg, msg_len);
                              }),
                      "Failed to register processor, msg type:%d",
                      static_cast<int32_t>(CtrlMsgType::kMatchEndpointReq));
  HIXL_CHK_STATUS_RET(RegProc(CtrlMsgType::kCreateChannelReq,
                              [this](const CtrlMsgPluginPtr &connection, const char *msg, uint64_t msg_len) -> Status {
                                return this->CreateChannel(connection, msg, msg_len);
                              }),
                      "Failed to register processor, msg type:%d",
                      static_cast<int32_t>(CtrlMsgType::kCreateChannelReq));
  HIXL_CHK_STATUS_RET(RegProc(CtrlMsgType::kChannelHandshakeReq,
                              [this](const CtrlMsgPluginPtr &connection, const char *msg, uint64_t msg_len) -> Status {
                                return this->HandleChannelHandshake(connection, msg, msg_len);
                              }),
                      "Failed to register processor, msg type:%d",
                      static_cast<int32_t>(CtrlMsgType::kChannelHandshakeReq));
  HIXL_CHK_STATUS_RET(RegProc(CtrlMsgType::kGetRemoteMemReq,
                              [this](const CtrlMsgPluginPtr &connection, const char *msg, uint64_t msg_len) -> Status {
                                return this->ExportMem(connection, msg, msg_len);
                              }),
                      "Failed to register processor, msg type:%d", static_cast<int32_t>(CtrlMsgType::kGetRemoteMemReq));
  HIXL_CHK_STATUS_RET(RegProc(CtrlMsgType::kDestroyChannelReq,
                              [this](const CtrlMsgPluginPtr &connection, const char *msg, uint64_t msg_len) -> Status {
                                const Status close_ret = connection->Close();
                                const Status destroy_ret = this->DestroyChannel(connection, msg, msg_len);
                                HIXL_CHK_STATUS_RET(close_ret, "Failed to close disconnected client");
                                HIXL_CHK_STATUS_RET(destroy_ret, "Failed to destroy disconnected client channel");
                                std::lock_guard<std::mutex> lock(client_mutex_);
                                disconnected_clients_.erase(connection);
                                return SUCCESS;
                              }),
                      "Failed to register processor, msg type:%d",
                      static_cast<int32_t>(CtrlMsgType::kDestroyChannelReq));
  return SUCCESS;
}

Status HixlCSServer::DestroyChannel(const CtrlMsgPluginPtr &connection, const char *msg, uint64_t msg_len) {
  (void)msg;
  (void)msg_len;
  EndpointChannelInfo info{};
  bool found = false;
  {
    // 临界区内仅做 find + erase，销毁放到锁外：HcommChannelDestroy 为毫秒级驱动操作，
    // 持锁销毁会在断连风暴下串行化所有客户端的 channel 销毁与建链插入。
    std::unique_lock<std::mutex> lock(chn_mutex_);
    chn_cv_.wait(lock, [this, &connection]() { return channel_handshakes_in_flight_.count(connection) == 0U; });
    auto it = channels_.find(connection);
    if (it != channels_.end()) {
      info = it->second;
      channels_.erase(it);
      found = true;
    }
    if (channel_handshake_deadlines_.erase(connection) > 0U) {
      pending_channel_handshakes_.store(channel_handshake_deadlines_.size(), std::memory_order_release);
    }
  }
  if (!found) {
    return SUCCESS;
  }
  auto ep = endpoint_store_.GetEndpoint(info.endpoint_handle);
  if (ep == nullptr) {
    HIXL_LOGW("Endpoint is not found when destroying channel, connection:%s, endpoint_handle:%p, channel_handle:0x%lx",
              connection->Desc().c_str(), info.endpoint_handle, info.channel_handle);
    return SUCCESS;
  }
  // Finalization and cleanup workers may need to switch to the endpoint's runtime context.
  auto ctx_guard = GetContextGuard();
  (void)ctx_guard;
  HIXL_CHK_STATUS_RET(ep->DestroyChannel(info.channel_handle),
                      "Failed to destroy channel, connection:%s, endpoint_handle:%p, channel_handle:0x%lx",
                      connection->Desc().c_str(), info.endpoint_handle, info.channel_handle);
  return SUCCESS;
}

void HixlCSServer::FreeDeviceMem(void *&ptr) {
  if (ptr == nullptr) {
    return;
  }
  auto rt_ret = aclrtFree(ptr);
  if (rt_ret != ACL_SUCCESS) {
    HIXL_LOGE(FAILED, "Failed to free device memory, ret:%d", rt_ret);
  }
  ptr = nullptr;
}

Status HixlCSServer::Finalize() {
  HIXL_LOGI("[HixlServer] finalize begin");
  Status ret = SUCCESS;
  // ctx_guard 在 TransferPool::Finalize() 之前必须析构，否则 Finalize 会销毁 rts_context_
  // 而 ctx_guard 析构时需要恢复到之前的 context，如果之前 context 和 rts_context_ 相同会报错
  {
    auto ctx_guard = GetContextGuard();
    (void)ctx_guard;
    if (listener_running_) {
      listener_running_ = false;
      if (listener_.joinable()) {
        listener_.join();
      }
    }
    ShutdownAllClients();
    msg_handler_.Finalize();
    CloseAllClients();
    ret = endpoint_store_.Finalize();
    HIXL_CHK_STATUS(ret, "Failed to finalize endpoint store.");
    if (host_trans_flag_ != nullptr) {
      free(host_trans_flag_);
      host_trans_flag_ = nullptr;
    }
    FreeDeviceMem(dev_trans_flag_);
  }
  dev_flag_context_ = nullptr;
  // ctx_guard 已析构，现在可以安全销毁 TransferPool
  if (device_id_ >= 0) {
    auto *pool = TransferPool::GetInstance(device_id_);
    if (pool != nullptr) {
      pool->Finalize();
    }
    device_id_ = -1;
  }

  if (listener_connection_ != nullptr) {
    (void)listener_connection_->Close();
    listener_connection_.reset();
  }

  if (epoll_fd_ != -1) {
    (void)close(epoll_fd_);
    epoll_fd_ = -1;
  }
  // Each endpoint owns a virtual-memory-pool reference; EndpointStore teardown releases the final one.
  HIXL_EVENT("[HixlServer] finalize end, ret:%u", ret);
  return ret;
}

Status HixlCSServer::RegisterMem(const char *mem_tag, const CommMem *mem, MemHandle *mem_handle) {
  auto ctx_guard = GetContextGuard();
  (void)ctx_guard;
  HIXL_EVENT("[HixlServer] register mem start, addr:%p, size:%lu bytes, type:%d", mem->addr, mem->size,
             static_cast<int32_t>(mem->type));
  auto all_handles = endpoint_store_.GetAllEndpointHandles();
  HIXL_CHK_BOOL_RET_STATUS(all_handles.size() > 0, PARAM_INVALID, "no endpoint is available");
  bool has_ub_endpoint = false;
  for (auto handle : all_handles) {
    auto endpoint = endpoint_store_.GetEndpoint(handle);
    HIXL_CHECK_NOTNULL(endpoint);
    has_ub_endpoint = has_ub_endpoint || IsUbEndpoint(endpoint->GetEndpoint().protocol);
  }
  HIXL_CHK_BOOL_RET_STATUS(!has_ub_endpoint || IsValidMemType(mem->type), PARAM_INVALID, "invalid mem type:%d",
                           static_cast<int32_t>(mem->type));

  std::vector<EndpointMemInfo> ep_mem_infos;
  HIXL_DISMISSABLE_GUARD(rollback_guard,
                         ([this, &ep_mem_infos]() { RollbackRegisteredMem(endpoint_store_, ep_mem_infos); }));
  for (auto handle : all_handles) {
    auto endpoint = endpoint_store_.GetEndpoint(handle);
    HIXL_CHECK_NOTNULL(endpoint);
    if (!ShouldRegisterEndpointForMem(endpoint, mem->type)) {
      continue;
    }
    MemHandle ep_mem_handle = nullptr;
    HIXL_CHK_STATUS_RET(endpoint->RegisterMem(mem_tag, *mem, ep_mem_handle), "Failed to register mem.");
    EndpointMemInfo ep_mem_info{};
    ep_mem_info.endpoint_handle = handle;
    ep_mem_info.mem_handle = ep_mem_handle;
    ep_mem_infos.emplace_back(ep_mem_info);
  }
  HIXL_CHK_BOOL_RET_STATUS(!ep_mem_infos.empty(), PARAM_INVALID, "no endpoint matches mem type:%d for register mem",
                           static_cast<int32_t>(mem->type));
  *mem_handle = ep_mem_infos[0].mem_handle;
  HIXL_EVENT("[HixlServer] register mem success, addr:%p, size:%lu bytes, type:%d, handle:%p", mem->addr, mem->size,
             static_cast<int32_t>(mem->type), *mem_handle);
  HIXL_DISMISS_GUARD(rollback_guard);
  std::lock_guard<std::mutex> lock(reg_mutex_);
  reg_mems_[ep_mem_infos[0].mem_handle] = std::move(ep_mem_infos);
  return SUCCESS;
}

Status HixlCSServer::DeregisterMem(MemHandle mem_handle) {
  auto ctx_guard = GetContextGuard();
  (void)ctx_guard;
  HIXL_EVENT("[HixlServer] deregister mem start, handle:%p", mem_handle);
  std::lock_guard<std::mutex> lock(reg_mutex_);
  auto it = reg_mems_.find(mem_handle);
  HIXL_CHK_BOOL_RET_STATUS(it != reg_mems_.cend(), PARAM_INVALID, "mem_handle:%p is not registered", mem_handle);
  for (const auto &ep_mem_info : it->second) {
    auto endpoint = endpoint_store_.GetEndpoint(ep_mem_info.endpoint_handle);
    HIXL_CHECK_NOTNULL(endpoint);
    HIXL_CHK_STATUS_RET(endpoint->DeregisterMem(ep_mem_info.mem_handle), "Failed to deregister mem, handle:%p.",
                        ep_mem_info.mem_handle);
  }
  reg_mems_.erase(it);
  HIXL_EVENT("[HixlServer] deregister mem success, handle:%p", mem_handle);
  return SUCCESS;
}

Status HixlCSServer::SendCreateChannelResp(const CtrlMsgPluginPtr &connection, const CreateChannelResp &resp) {
  CtrlMsgHeader header{};
  header.magic = kMagicNumber;
  header.body_size = static_cast<uint64_t>(sizeof(CtrlMsgType) + sizeof(CreateChannelResp));
  CtrlMsgType msg_type = CtrlMsgType::kCreateChannelResp;
  HIXL_CHK_STATUS_RET(connection->Send(&header, static_cast<uint64_t>(sizeof(header))));
  HIXL_CHK_STATUS_RET(connection->Send(&msg_type, static_cast<uint64_t>(sizeof(msg_type))));
  HIXL_CHK_STATUS_RET(connection->Send(&resp, static_cast<uint64_t>(sizeof(resp))));
  return SUCCESS;
}

Status HixlCSServer::SendChannelHandshakeResp(const CtrlMsgPluginPtr &connection, Status result, bool done,
                                              const std::vector<uint8_t> &payload) {
  HIXL_CHK_BOOL_RET_STATUS(payload.size() <= kMaxChannelHandshakePayloadBytes, PARAM_INVALID,
                           "ChannelHandshakeResp payload too large, size=%zu", payload.size());
  ChannelHandshakeHeader handshake_hdr{};
  handshake_hdr.result = result;
  handshake_hdr.done = done ? 1U : 0U;
  handshake_hdr.payload_len = static_cast<uint32_t>(payload.size());
  CtrlMsgHeader header{};
  header.magic = kMagicNumber;
  header.body_size = static_cast<uint64_t>(sizeof(CtrlMsgType) + sizeof(ChannelHandshakeHeader) + payload.size());
  const CtrlMsgType msg_type = CtrlMsgType::kChannelHandshakeResp;
  const size_t total_size = sizeof(header) + sizeof(msg_type) + sizeof(handshake_hdr) + payload.size();
  std::vector<uint8_t> frame(total_size);
  size_t offset = 0U;
  HIXL_CHK_BOOL_RET_STATUS(memcpy_s(frame.data() + offset, total_size - offset, &header, sizeof(header)) == EOK, FAILED,
                           "memcpy_s CtrlMsgHeader failed");
  offset += sizeof(header);
  HIXL_CHK_BOOL_RET_STATUS(memcpy_s(frame.data() + offset, total_size - offset, &msg_type, sizeof(msg_type)) == EOK,
                           FAILED, "memcpy_s CtrlMsgType failed");
  offset += sizeof(msg_type);
  HIXL_CHK_BOOL_RET_STATUS(
      memcpy_s(frame.data() + offset, total_size - offset, &handshake_hdr, sizeof(handshake_hdr)) == EOK, FAILED,
      "memcpy_s ChannelHandshakeHeader failed");
  offset += sizeof(handshake_hdr);
  if (!payload.empty()) {
    HIXL_CHK_BOOL_RET_STATUS(
        memcpy_s(frame.data() + offset, total_size - offset, payload.data(), payload.size()) == EOK, FAILED,
        "memcpy_s ChannelHandshakeResp payload failed");
  }
  HIXL_CHK_STATUS_RET(connection->Send(frame.data(), static_cast<uint64_t>(frame.size())));
  return SUCCESS;
}

Status HixlCSServer::SendMatchEndpointResp(const CtrlMsgPluginPtr &connection, const MatchEndpointResp &resp) {
  CtrlMsgHeader header{};
  header.magic = kMagicNumber;
  header.body_size = static_cast<uint64_t>(sizeof(CtrlMsgType) + sizeof(MatchEndpointResp));
  CtrlMsgType msg_type = CtrlMsgType::kMatchEndpointResp;
  HIXL_CHK_STATUS_RET(connection->Send(&header, static_cast<uint64_t>(sizeof(header))));
  HIXL_CHK_STATUS_RET(connection->Send(&msg_type, static_cast<uint64_t>(sizeof(msg_type))));
  HIXL_CHK_STATUS_RET(connection->Send(&resp, static_cast<uint64_t>(sizeof(resp))));
  return SUCCESS;
}

Status HixlCSServer::MatchEndpointMsg(const CtrlMsgPluginPtr &connection, const char *msg, uint64_t msg_len) const {
  HIXL_DISMISSABLE_GUARD(failed, ([connection, this]() {
                           MatchEndpointResp resp{};
                           resp.result = FAILED;
                           HIXL_CHK_STATUS(SendMatchEndpointResp(connection, resp));
                         }));
  HIXL_CHECK_NOTNULL(msg);
  HIXL_CHK_BOOL_RET_STATUS(msg_len == sizeof(MatchEndpointReq), PARAM_INVALID,
                           "invalid msg len:%lu of match endpoint, must = %zu", msg_len, sizeof(MatchEndpointReq));
  const auto &req = *reinterpret_cast<const MatchEndpointReq *>(msg);
  EndpointHandle handle = nullptr;
  auto ep = endpoint_store_.MatchEndpoint(req.dst, handle);
  MatchEndpointResp resp{};
  if (ep == nullptr) {
    resp.result = PARAM_INVALID;
    HIXL_CHK_STATUS_RET(SendMatchEndpointResp(connection, resp), "Failed to send match endpoint resp");
    HIXL_DISMISS_GUARD(failed);
    return SUCCESS;
  }
  uint32_t listen_port = 0U;
  if (global_config_.ListenPort().has_value()) {
    listen_port = global_config_.ListenPort().value();
    ep->SetPort(listen_port);
  } else {
    const Status ret = ep->GetListenPort(listen_port);
    if (ret == SUCCESS) {
      ep->SetPort(listen_port);
    }
    HIXL_CHK_BOOL_RET_STATUS(ret == SUCCESS || ret == UNSUPPORTED, FAILED,
                             "Call api:EndpointGetListenPort failed, ret:%u, ep_handle:%p", static_cast<uint32_t>(ret),
                             handle);
  }
  resp.result = SUCCESS;
  resp.dst_ep_handle = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(handle));
  resp.port = ep->GetPort();
  resp.channel_index = g_next_server_channel_index.fetch_add(1U, std::memory_order_relaxed);
  HIXL_CHK_STATUS_RET(SendMatchEndpointResp(connection, resp), "Failed to send match endpoint resp");
  HIXL_DISMISS_GUARD(failed);
  HIXL_LOGI("SendMatchEndpointResp success, channel_index=%" PRIu64, resp.channel_index);
  return SUCCESS;
}

Status HixlCSServer::CreateChannel(const CtrlMsgPluginPtr &connection, const char *msg, uint64_t msg_len) {
  HIXL_DISMISSABLE_GUARD(failed, ([connection, this]() {
                           CreateChannelResp resp{};
                           resp.result = FAILED;
                           HIXL_LOGI("SendCreateChannelResp start");
                           HIXL_CHK_STATUS(SendCreateChannelResp(connection, resp));
                         }));
  HIXL_CHECK_NOTNULL(msg);
  HIXL_CHK_BOOL_RET_STATUS(msg_len == sizeof(CreateChannelReq), PARAM_INVALID,
                           "invalid msg len:%lu of create channel, must = %zu", msg_len, sizeof(CreateChannelReq));
  const auto &req = *reinterpret_cast<const CreateChannelReq *>(msg);
  HIXL_CHK_BOOL_RET_STATUS(req.qos == kQosUnset || req.qos <= kQosMax, PARAM_INVALID,
                           "invalid req qos:%u of create channel, must in range= [%u, %u] or unset", req.qos, kQosMin,
                           kQosMax);
  EndpointHandle handle = reinterpret_cast<EndpointHandle>(static_cast<uintptr_t>(req.dst_ep_handle));
  auto ep = endpoint_store_.GetEndpoint(handle);
  HIXL_CHECK_NOTNULL(ep);
  ChannelHandle channel_handle = 0UL;
  ChannelDesc channel_desc{};
  channel_desc.remote_endpoint = req.src;
  channel_desc.tc = req.tc;
  channel_desc.sl = req.sl;
  channel_desc.retry_cnt = req.retry_cnt;
  channel_desc.retry_interval = req.retry_interval;
  channel_desc.channel_type = ChannelType::kServer;
  channel_desc.channel_index = req.channel_index;
  channel_desc.qos = req.qos;
  channel_desc.configurable_transfer_supported =
      ep->Capabilities().supports_configurable_transfer || configurable_transfer_supported_;
  const bool uses_handshake = ep->Capabilities().uses_channel_handshake;
  if (uses_handshake) {
    std::lock_guard<std::mutex> lock(chn_mutex_);
    HIXL_CHK_BOOL_RET_STATUS(channels_.find(connection) == channels_.end() &&
                                 channel_handshake_deadlines_.find(connection) == channel_handshake_deadlines_.end(),
                             ALREADY_CONNECTED,
                             "Control connection already has a channel or pending handshake, connection:%s",
                             connection->Desc().c_str());
    HIXL_CHK_BOOL_RET_STATUS(channel_handshake_deadlines_.size() < kMaxPendingHandshakes, RESOURCE_EXHAUSTED,
                             "Pending channel handshake limit reached, limit:%zu, connection:%s", kMaxPendingHandshakes,
                             connection->Desc().c_str());
    channel_handshake_deadlines_[connection] =
        std::chrono::steady_clock::now() + std::chrono::seconds(kChannelHandshakeTimeoutSec);
    pending_channel_handshakes_.store(channel_handshake_deadlines_.size(), std::memory_order_release);
  }
  HIXL_DISMISSABLE_GUARD(reserve_guard, ([this, &connection, uses_handshake]() {
                           if (!uses_handshake) {
                             return;
                           }
                           std::lock_guard<std::mutex> lock(chn_mutex_);
                           if (channel_handshake_deadlines_.erase(connection) > 0U) {
                             pending_channel_handshakes_.store(channel_handshake_deadlines_.size(),
                                                               std::memory_order_release);
                           }
                         }));
  auto ctx_guard = GetContextGuard();
  (void)ctx_guard;
  HIXL_CHK_STATUS_RET(ep->CreateChannel(channel_desc, channel_handle, req.timeout_ms), "Failed to create channel");
  const TrackChannelResult tracked = TrackChannelIfConnected(connection, ep, channel_handle);
  if (tracked != TrackChannelResult::kTracked) {
    // 仅在 client 已断开时跳过错误回包；若活跃连接因重复或超限被拒，保留 failed guard 向对端回错。
    if (tracked == TrackChannelResult::kDisconnected) {
      HIXL_DISMISS_GUARD(failed);
    }
    HIXL_LOGE(
        FAILED,
        "client connection:%s disconnected or rejected during channel creation, reason=%u, discard channel:%" PRIu64,
        connection->Desc().c_str(), static_cast<uint32_t>(tracked), channel_handle);
    HIXL_CHK_STATUS_RET(ep->DestroyChannel(channel_handle), "Failed to destroy channel after client disconnect");
    return FAILED;
  }
  HIXL_DISMISS_GUARD(reserve_guard);
  HIXL_DISMISS_GUARD(failed);
  CreateChannelResp resp{};
  resp.result = SUCCESS;
  HIXL_CHK_STATUS_RET(SendCreateChannelResp(connection, resp), "Failed to send create channel resp");
  HIXL_LOGI("CreateChannel success, channel_index=%" PRIu64, req.channel_index);
  return SUCCESS;
}

Status HixlCSServer::HandleChannelHandshake(const CtrlMsgPluginPtr &connection, const char *msg, uint64_t msg_len) {
  std::vector<uint8_t> out_payload;
  bool done = false;
  bool concurrent_in_flight = false;

  auto run = [&]() -> Status {
    HIXL_CHECK_NOTNULL(msg);
    HIXL_CHK_BOOL_RET_STATUS(msg_len >= sizeof(ChannelHandshakeHeader), PARAM_INVALID,
                             "ChannelHandshakeReq too short, msg_len=%" PRIu64, msg_len);
    ChannelHandshakeHeader hdr{};
    const errno_t rc = memcpy_s(&hdr, sizeof(hdr), msg, sizeof(hdr));
    HIXL_CHK_BOOL_RET_STATUS(rc == EOK, FAILED, "memcpy_s ChannelHandshakeHeader failed, rc=%d",
                             static_cast<int32_t>(rc));
    HIXL_CHK_BOOL_RET_STATUS(hdr.payload_len <= kMaxChannelHandshakePayloadBytes &&
                                 sizeof(ChannelHandshakeHeader) + static_cast<uint64_t>(hdr.payload_len) == msg_len,
                             PARAM_INVALID, "ChannelHandshakeReq payload_len mismatch, header=%u, msg_len=%" PRIu64,
                             hdr.payload_len, msg_len);
    const auto *payload_begin = reinterpret_cast<const uint8_t *>(msg + sizeof(hdr));
    const std::vector<uint8_t> in_payload(payload_begin, payload_begin + hdr.payload_len);

    EndpointHandle ep_handle = nullptr;
    ChannelHandle channel_handle = 0UL;
    {
      std::lock_guard<std::mutex> lock(chn_mutex_);
      if (channel_handshakes_in_flight_.count(connection) != 0U) {
        concurrent_in_flight = true;
        HIXL_LOGE(ALREADY_CONNECTED, "Control connection already advancing a channel handshake");
        return ALREADY_CONNECTED;
      }
      const auto chn_it = channels_.find(connection);
      HIXL_CHK_BOOL_RET_STATUS(chn_it != channels_.end(), PARAM_INVALID,
                               "ChannelHandshakeReq received before CreateChannel");
      HIXL_CHK_BOOL_RET_STATUS(channel_handshake_deadlines_.count(connection) != 0U, PARAM_INVALID,
                               "Control connection has no active channel handshake");
      ep_handle = chn_it->second.endpoint_handle;
      channel_handle = chn_it->second.channel_handle;
      channel_handshakes_in_flight_.insert(connection);
    }
    ScopeGuard in_flight_guard([this, &connection]() {
      {
        std::lock_guard<std::mutex> lock(chn_mutex_);
        channel_handshakes_in_flight_.erase(connection);
      }
      chn_cv_.notify_all();
    });

    const auto ep = endpoint_store_.GetEndpoint(ep_handle);
    HIXL_CHK_BOOL_RET_STATUS(ep != nullptr && ep->Capabilities().uses_channel_handshake, PARAM_INVALID,
                             "ChannelHandshakeReq targets an invalid endpoint");
    const auto channel = ep->GetChannel(channel_handle);
    HIXL_CHECK_NOTNULL(channel);
    auto ctx_guard = GetContextGuard();
    (void)ctx_guard;
    HIXL_CHK_STATUS_RET(channel->StepServerHandshake(in_payload, out_payload, done));
    if (done) {
      std::lock_guard<std::mutex> lock(chn_mutex_);
      if (channel_handshake_deadlines_.erase(connection) > 0U) {
        pending_channel_handshakes_.store(channel_handshake_deadlines_.size(), std::memory_order_release);
      }
    }
    return SUCCESS;
  };

  const Status result = run();
  if (concurrent_in_flight) {
    // 另一个 worker 正在同连接上推进握手，避免并发 Send 导致帧交错，直接清理连接。
    CleanupClient(connection);
    return result;
  }
  const std::vector<uint8_t> empty_payload;
  const Status sent = SendChannelHandshakeResp(connection, result, result == SUCCESS && done,
                                               result == SUCCESS ? out_payload : empty_payload);
  if (result != SUCCESS || sent != SUCCESS) {
    CleanupClient(connection);
  }
  return sent == SUCCESS ? result : sent;
}

HixlCSServer::TrackChannelResult HixlCSServer::TrackChannelIfConnected(const CtrlMsgPluginPtr &connection,
                                                                       const EndpointPtr &endpoint,
                                                                       ChannelHandle channel) {
  // Serialize insertion with disconnect removal; lock order: client_mutex_ -> chn_mutex_.
  std::lock_guard<std::mutex> client_lock(client_mutex_);
  const auto it = clients_.find(connection->Fd());
  if (it == clients_.end() || it->second->Connection() != connection) {
    return TrackChannelResult::kDisconnected;
  }
  std::lock_guard<std::mutex> chn_lock(chn_mutex_);
  if (endpoint->Capabilities().uses_channel_handshake) {
    if (channels_.find(connection) != channels_.end() ||
        channel_handshake_deadlines_.find(connection) == channel_handshake_deadlines_.end()) {
      return TrackChannelResult::kRejected;
    }
    channel_handshake_deadlines_[connection] =
        std::chrono::steady_clock::now() + std::chrono::seconds(kChannelHandshakeTimeoutSec);
    pending_channel_handshakes_.store(channel_handshake_deadlines_.size(), std::memory_order_release);
  }
  channels_[connection] = {endpoint->GetHandle(), channel};
  return TrackChannelResult::kTracked;
}

static inline void to_json(nlohmann::json &j, const HixlMemDesc &m) {
  j = nlohmann::json{};
  j["mem"] = m.mem;
  j["tag"] = m.tag;
  j["export_desc"] = nlohmann::json::array();
  if (m.export_desc != nullptr && m.export_len > 0) {
    const uint8_t *data_ptr = static_cast<const uint8_t *>(m.export_desc);
    for (size_t i = 0; i < m.export_len; ++i) {
      j["export_desc"].push_back(static_cast<int>(data_ptr[i]));
    }
  }
  if (m.registered_dev_mem != nullptr) {
    j["registered_dev_mem"] = reinterpret_cast<uintptr_t>(m.registered_dev_mem);
  }
}

static inline void to_json(nlohmann::json &j, const GetRemoteMemResp &r) {
  j = nlohmann::json{};
  j["result"] = r.result;
  j["mem_descs"] = r.mem_descs;
}

template <typename T>
Status HixlCSServer::Serialize(const T &msg, std::string &msg_str) {
  try {
    nlohmann::json j = msg;
    msg_str = j.dump();
  } catch (const nlohmann::json::exception &e) {
    HIXL_LOGE(PARAM_INVALID, "Failed to dump msg to str, exception:%s", e.what());
    return PARAM_INVALID;
  }
  return SUCCESS;
}

Status HixlCSServer::SendRemoteMemResp(const CtrlMsgPluginPtr &connection, const GetRemoteMemResp &resp) {
  CtrlMsgHeader header{};
  header.magic = kMagicNumber;
  std::string msg_str;
  HIXL_CHK_STATUS_RET(Serialize(resp, msg_str), "Failed to serialize msg");
  // Do not log msg_str content: it carries memory addresses and RDMA export descriptors.
  HIXL_LOGD("remote mem serialize success, connection:%s, size:%zu bytes", connection->Desc().c_str(), msg_str.size());
  header.body_size = static_cast<uint64_t>(sizeof(CtrlMsgType) + msg_str.size());
  CtrlMsgType msg_type = CtrlMsgType::kGetRemoteMemResp;
  HIXL_CHK_STATUS_RET(connection->Send(&header, static_cast<uint64_t>(sizeof(header))));
  HIXL_CHK_STATUS_RET(connection->Send(&msg_type, static_cast<uint64_t>(sizeof(msg_type))));
  HIXL_CHK_STATUS_RET(connection->Send(msg_str.c_str(), static_cast<uint64_t>(msg_str.size())));
  return SUCCESS;
}

Status HixlCSServer::ExportMem(const CtrlMsgPluginPtr &connection, const char *msg, uint64_t msg_len) const {
  HIXL_DISMISSABLE_GUARD(failed, ([connection, this]() {
                           GetRemoteMemResp resp{};
                           resp.result = FAILED;
                           HIXL_CHK_STATUS(SendRemoteMemResp(connection, resp));
                         }));
  HIXL_CHECK_NOTNULL(msg);
  HIXL_CHK_BOOL_RET_STATUS(msg_len == sizeof(GetRemoteMemReq), PARAM_INVALID, "invalid msg len:%lu of get remote mem",
                           msg_len);
  const auto req = reinterpret_cast<const GetRemoteMemReq *>(msg);
  EndpointHandle handle = reinterpret_cast<EndpointHandle>(static_cast<uintptr_t>(req->dst_ep_handle));
  HIXL_CHECK_NOTNULL(handle);
  auto ep = endpoint_store_.GetEndpoint(handle);
  HIXL_CHECK_NOTNULL(ep);
  GetRemoteMemResp resp{};
  HIXL_CHK_STATUS_RET(ep->ExportMem(resp.mem_descs), "Failed to export mem");
  resp.result = SUCCESS;
  HIXL_DISMISS_GUARD(failed);
  HIXL_CHK_STATUS_RET(SendRemoteMemResp(connection, resp), "Failed to send remote mem resp.");
  return SUCCESS;
}

Status HixlCSServer::Listen(uint32_t backlog) {
  auto ctx_guard = GetContextGuard();
  (void)ctx_guard;
  HIXL_CHECK_NOTNULL(listener_connection_);
  HIXL_CHK_BOOL_RET_STATUS(listener_connection_->Fd() == -1 && !listener_.joinable(), FAILED,
                           "Server is already listening, ip:%s, port:%u", ip_.c_str(), port_);
  HIXL_CHK_STATUS_RET(listener_connection_->Listen(ip_, port_, backlog), "Failed to listen on %s:%u", ip_.c_str(),
                      port_);
  HIXL_DISMISSABLE_GUARD(close_connection, ([this]() { (void)listener_connection_->Close(); }));
  HIXL_CHK_STATUS_RET(listener_connection_->AddToEpoll(epoll_fd_), "Failed to register listener with epoll");
  HIXL_DISMISS_GUARD(close_connection);
  HIXL_EVENT("[HixlServer] start to listen on %s:%u", ip_.c_str(), port_);
  listener_running_ = true;
  listener_ = std::thread([this]() {
    while (listener_running_) {
      (void)DoWait();
    }
  });
  return SUCCESS;
}

Status HixlCSServer::RegProc(CtrlMsgType msg_type, MsgProcessor proc) {
  auto ctx_guard = GetContextGuard();
  (void)ctx_guard;
  auto processor = [this, proc](const CtrlMsgPluginPtr &connection, const char *msg, uint64_t msg_len) {
    const Status ret = proc(connection, msg, msg_len);
    if (ret != SUCCESS && !connection->IsUsable()) {
      CleanupClient(connection);
    }
    return ret;
  };
  HIXL_CHK_STATUS_RET(msg_handler_.RegisterMsgProcessor(msg_type, processor),
                      "Failed to reg msg processor, msg type:%d", static_cast<int32_t>(msg_type));
  return SUCCESS;
}

Status HixlCSServer::ProClientMsg(const CtrlMsgPluginPtr &connection, std::shared_ptr<MsgReceiver> receiver) {
  std::vector<CtrlMsgPtr> msgs;
  const Status ret = receiver->IRecv(msgs);
  for (auto msg : msgs) {
    msg_handler_.SubmitMsg(connection, msg);
  }
  return ret;
}

void HixlCSServer::CloseClientSocket(const CtrlMsgPluginPtr &connection) const {
  (void)epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, connection->Fd(), nullptr);
  (void)connection->Close();
}

void HixlCSServer::ShutdownAllClients() {
  std::vector<CtrlMsgPluginPtr> connections;
  {
    std::lock_guard<std::mutex> lock(client_mutex_);
    for (const auto &entry : clients_) {
      connections.push_back(entry.second->Connection());
    }
    connections.insert(connections.end(), disconnected_clients_.begin(), disconnected_clients_.end());
  }
  for (const auto &entry : pending_handshakes_) {
    connections.push_back(entry.second.connection);
  }
  for (const auto &connection : connections) {
    HIXL_CHK_STATUS(connection->ShutdownSocket(), "Failed to interrupt client during server finalization");
  }
}

void HixlCSServer::CloseAllClients() {
  for (const auto &entry : pending_handshakes_) {
    CloseClientSocket(entry.second.connection);
  }
  pending_handshakes_.clear();
  std::map<int32_t, std::shared_ptr<MsgReceiver>> clients;
  std::set<CtrlMsgPluginPtr> disconnected;
  {
    std::lock_guard<std::mutex> lock(client_mutex_);
    clients.swap(clients_);
    disconnected.swap(disconnected_clients_);
  }
  for (const auto &entry : clients) {
    auto connection = entry.second->Connection();
    CloseClientSocket(connection);
  }
  for (const auto &connection : disconnected) {
    CloseClientSocket(connection);
  }
  {
    std::lock_guard<std::mutex> lock(chn_mutex_);
    channels_.clear();
    channel_handshake_deadlines_.clear();
    channel_handshakes_in_flight_.clear();
    pending_channel_handshakes_.store(0U, std::memory_order_release);
  }
  chn_cv_.notify_all();
  HIXL_EVENT("[HixlServer] closed remaining client sockets, count:%zu", clients.size() + disconnected.size());
}

void HixlCSServer::CleanupClient(const CtrlMsgPluginPtr &connection) {
  auto msg = MakeShared<CtrlMsg>();
  if (msg == nullptr) {
    HIXL_LOGE(FAILED, "Failed to allocate disconnected client cleanup message");
    return;
  }
  msg->msg_type = CtrlMsgType::kDestroyChannelReq;
  const int32_t fd = connection->Fd();
  {
    std::lock_guard<std::mutex> lock(client_mutex_);
    const auto it = clients_.find(fd);
    if (it == clients_.end() || it->second->Connection() != connection) {
      return;
    }
    disconnected_clients_.insert(connection);
    clients_.erase(it);
  }
  (void)epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
  HIXL_CHK_STATUS(connection->ShutdownSocket(), "Failed to interrupt disconnected client, fd:%d", fd);
  msg_handler_.SubmitMsg(connection, msg);
  HIXL_EVENT("[HixlServer] client disconnected, fd:%d cleanup scheduled", fd);
}

bool HixlCSServer::ProcessPendingMsg() {
  if (!tls_enabled_) {
    return false;
  }
  std::vector<std::shared_ptr<MsgReceiver>> receivers;
  {
    std::lock_guard<std::mutex> lock(client_mutex_);
    receivers.reserve(clients_.size());
    for (const auto &entry : clients_) {
      receivers.push_back(entry.second);
    }
  }
  bool processed = false;
  for (const auto &receiver : receivers) {
    auto connection = receiver->Connection();
    if (!connection->HasPending()) {
      continue;
    }
    processed = true;
    if (ProClientMsg(connection, receiver) != SUCCESS) {
      CleanupClient(connection);
    }
  }
  return processed;
}

Status HixlCSServer::UpdateClientEvents(int32_t fd, uint32_t events) const {
  epoll_event event{};
  event.events = events | EPOLLRDHUP;
  event.data.fd = fd;
  const int ret = epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &event);
  const int saved_errno = errno;
  HIXL_CHK_BOOL_RET_STATUS(ret == 0, FAILED,
                           "Call api:epoll_ctl failed, ret:%d, fd:%d, events:0x%x, errno:%d, error_msg:%s", ret, fd,
                           event.events, saved_errno, strerror(saved_errno));
  return SUCCESS;
}

Status HixlCSServer::AdvanceHandshake(int32_t fd) {
  const auto it = pending_handshakes_.find(fd);
  HIXL_CHK_BOOL_RET_STATUS(it != pending_handshakes_.end(), FAILED, "Missing pending TLS connection, fd:%d", fd);
  auto connection = it->second.connection;
  HIXL_DISMISSABLE_GUARD(rollback, ([this, connection, fd]() {
                           pending_handshakes_.erase(fd);
                           CloseClientSocket(connection);
                         }));
  HIXL_CHK_BOOL_RET_STATUS(std::chrono::steady_clock::now() < it->second.deadline, TIMEOUT,
                           "Server TLS handshake timed out, fd:%d, timeout:%d ms", fd,
                           CtrlMsgPlugin::kServerHandshakeTimeoutMs);
  TlsHandshakeState state;
  HIXL_CHK_STATUS_RET(connection->HandshakeServerStep(state), "Failed to advance server TLS handshake, fd:%d", fd);
  const uint32_t events = state == TlsHandshakeState::kWantWrite ? EPOLLOUT : EPOLLIN;
  HIXL_CHK_STATUS_RET(UpdateClientEvents(fd, events), "Failed to update control connection events, fd:%d", fd);
  if (state == TlsHandshakeState::kComplete) {
    auto receiver = MakeShared<MsgReceiver>(connection);
    HIXL_CHECK_NOTNULL(receiver);
    {
      std::lock_guard<std::mutex> lock(client_mutex_);
      const bool inserted = clients_.emplace(fd, std::move(receiver)).second;
      HIXL_CHK_BOOL_RET_STATUS(inserted, FAILED,
                               "Failed to publish established control connection, duplicate client fd:%d", fd);
    }
    pending_handshakes_.erase(it);
  }
  HIXL_DISMISS_GUARD(rollback);
  return SUCCESS;
}

void HixlCSServer::ExpireHandshakes() {
  const auto now = std::chrono::steady_clock::now();
  for (auto it = pending_handshakes_.begin(); it != pending_handshakes_.end();) {
    if (it->second.deadline > now) {
      ++it;
      continue;
    }
    HIXL_LOGW("Server TLS handshake timed out, fd:%d, timeout:%d ms", it->first,
              CtrlMsgPlugin::kServerHandshakeTimeoutMs);
    CloseClientSocket(it->second.connection);
    it = pending_handshakes_.erase(it);
  }
  if (pending_channel_handshakes_.load(std::memory_order_acquire) == 0U) {
    return;
  }
  std::vector<CtrlMsgPluginPtr> expired_channels;
  {
    std::lock_guard<std::mutex> lock(chn_mutex_);
    for (auto it = channel_handshake_deadlines_.begin(); it != channel_handshake_deadlines_.end();) {
      if (it->second > now || channel_handshakes_in_flight_.count(it->first) != 0U) {
        ++it;
        continue;
      }
      expired_channels.push_back(it->first);
      it = channel_handshake_deadlines_.erase(it);
    }
    pending_channel_handshakes_.store(channel_handshake_deadlines_.size(), std::memory_order_release);
  }
  for (const auto &connection : expired_channels) {
    HIXL_LOGW("[HixlServer] Incomplete channel handshake timed out, timeout=%u ms",
              kChannelHandshakeTimeoutSec * kMsPerSecond);
    CleanupClient(connection);
  }
}

Status HixlCSServer::AcceptClient() {
  CtrlMsgPluginPtr connection;
  HIXL_CHK_STATUS_RET(listener_connection_->Accept(connection), "Failed to accept connection");
  if (connection == nullptr) {
    return SUCCESS;
  }
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(CtrlMsgPlugin::kServerHandshakeTimeoutMs);
  HIXL_DISMISSABLE_GUARD(rollback, ([this, connection]() { CloseClientSocket(connection); }));
  HIXL_CHK_BOOL_RET_STATUS(!tls_enabled_ || pending_handshakes_.size() < kMaxPendingHandshakes, RESOURCE_EXHAUSTED,
                           "Pending TLS handshake limit reached, limit:%zu", kMaxPendingHandshakes);
  HIXL_CHK_STATUS_RET(connection->SetTcpNoDelay(), "Failed to set TCP_NODELAY");
  HIXL_CHK_STATUS_RET(connection->SetTcpKeepAlive(), "Failed to set TCP keepalive");
  HIXL_CHK_STATUS_RET(connection->SetSendTimeout(CtrlMsgPlugin::kDefaultCtrlTimeoutMs),
                      "Failed to set SO_SNDTIMEO, fd:%d, timeout:%u ms", connection->Fd(),
                      CtrlMsgPlugin::kDefaultCtrlTimeoutMs);
  HIXL_CHK_STATUS_RET(connection->AddToEpoll(epoll_fd_), "Failed to register connection with epoll");
  const int32_t fd = connection->Fd();
  pending_handshakes_.emplace(fd, PendingHandshake{connection, deadline});
  HIXL_CHK_STATUS_RET(AdvanceHandshake(fd), "Failed to initialize server TLS handshake, fd:%d", fd);
  HIXL_DISMISS_GUARD(rollback);
  return SUCCESS;
}

void HixlCSServer::HandleClientEvent(int32_t fd, uint32_t events) {
  const auto pending = pending_handshakes_.find(fd);
  if (pending != pending_handshakes_.end()) {
    if ((events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0U) {
      CloseClientSocket(pending->second.connection);
      pending_handshakes_.erase(pending);
    } else {
      HIXL_CHK_STATUS(AdvanceHandshake(fd), "Failed to advance server TLS handshake, fd:%d", fd);
    }
    return;
  }
  std::shared_ptr<MsgReceiver> receiver;
  {
    std::lock_guard<std::mutex> lock(client_mutex_);
    const auto it = clients_.find(fd);
    if (it != clients_.end()) {
      receiver = it->second;
    }
  }
  if (receiver != nullptr) {
    auto connection = receiver->Connection();
    if ((events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0U ||
        ((events & EPOLLIN) != 0U && ProClientMsg(connection, receiver) != SUCCESS)) {
      HIXL_EVENT("[HixlServer] detected client disconnect event, fd:%d, events:%u", fd, events);
      CleanupClient(connection);
    }
  }
}

Status HixlCSServer::DoWait() {
  const bool processed_pending = ProcessPendingMsg();
  ExpireHandshakes();
  struct epoll_event events[kMaxEventsNum];
  const int32_t timeout_ms = processed_pending ? 0 : kEpollWaitTimeInMillis;
  const int count = epoll_wait(epoll_fd_, events, kMaxEventsNum, timeout_ms);
  bool accept_ready = false;
  for (int i = 0; i < count; ++i) {
    if (events[i].data.fd == listener_connection_->Fd()) {
      accept_ready = true;
    } else {
      HandleClientEvent(events[i].data.fd, events[i].events);
    }
  }
  ExpireHandshakes();
  // Accept last so a closed fd cannot be reused before this event batch has been consumed.
  if (accept_ready) {
    HIXL_CHK_STATUS_RET(AcceptClient(), "Failed to accept client");
  }
  return SUCCESS;
}
}  // namespace hixl
