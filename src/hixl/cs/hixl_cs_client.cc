/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hixl_cs_client.h"
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <securec.h>
#include "acl/acl.h"
#include "nlohmann/json.hpp"
#include "hixl/hixl_types.h"
#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/hixl_utils.h"
#include "common/llm_utils.h"
#include "common/scope_guard.h"
#include "common/tls/tls_manager.h"
#include "common/ctrl_msg_plugin.h"
#include "channel_factory.h"
#include "conn_msg_handler.h"
#include "mem_msg_handler.h"
#include "transfer_pool.h"

namespace hixl {
namespace {
constexpr uint32_t kDefaultTransferPoolSize = 128U;
constexpr uint32_t kMinRdmaRetryCnt = 1U;
constexpr uint32_t kMaxRdmaRetryCnt = 7U;

Status ParseEnvUint32(const char *name, uint32_t &out, bool &present) {
  present = false;
  const char *value = std::getenv(name);
  if (value == nullptr || *value == '\0') {
    return SUCCESS;
  }
  present = true;
  char *end = nullptr;
  errno = 0;
  unsigned long parsed = std::strtoul(value, &end, 10);
  if (errno != 0 || end == value || *end != '\0' || parsed > std::numeric_limits<uint32_t>::max()) {
    HIXL_LOGE(PARAM_INVALID, "[HixlClient] %s is invalid, value=%s", name, value);
    return PARAM_INVALID;
  }
  out = static_cast<uint32_t>(parsed);
  return SUCCESS;
}

void FreeExportDesc(std::vector<hixl::HixlMemDesc> &desc_list) {
  for (auto &d : desc_list) {
    if (d.export_desc != nullptr && d.export_len > 0U) {
      std::free(d.export_desc);
      d.export_desc = nullptr;
      d.export_len = 0U;
    }
  }
  desc_list.clear();
}

hixl::Status ValidateExportDescList(const std::vector<hixl::HixlMemDesc> &desc_list) {
  for (const auto &d : desc_list) {
    if (d.export_desc == nullptr || d.export_len == 0U) {
      HIXL_LOGE(hixl::PARAM_INVALID,
                "[HixlClient] ValidateExportDescList failed! Invalid export_desc at "
                "ptr=%p, len=%u, total_count=%zu",
                d.export_desc, d.export_len, desc_list.size());
      return hixl::PARAM_INVALID;
    }
  }
  return hixl::SUCCESS;
}

hixl::Status AppendTagStorage(std::vector<std::vector<char>> &storage, const std::string &tag) {
  std::vector<char> buf(tag.size() + 1U, '\0');
  if (!tag.empty()) {
    errno_t rc = memcpy_s(buf.data(), buf.size(), tag.data(), tag.size());
    HIXL_CHK_BOOL_RET_STATUS(
        rc == EOK, hixl::FAILED,
        "[HixlClient] Call api:memcpy_s failed, ret:%d, tag:%s, src_size:%zu bytes, dst_size:%zu bytes",
        static_cast<int32_t>(rc), tag.c_str(), tag.size(), buf.size());
  }
  storage.emplace_back(std::move(buf));
  HIXL_LOGD("[HixlClient] AppendTagStorage success. tag: '%s', current_storage_size: %zu", tag.c_str(), storage.size());
  return hixl::SUCCESS;
}

void BuildTagPtrs(std::vector<std::vector<char>> &storage, std::vector<char *> &ptrs) {
  ptrs.clear();
  ptrs.reserve(storage.size());
  for (auto &s : storage) {
    ptrs.emplace_back(s.empty() ? nullptr : s.data());
  }
}

void CloseImportedBufs(Endpoint &endpoint, const std::vector<hixl::HixlMemDesc> &bufs) {
  if (endpoint.GetHandle() == nullptr) {
    return;
  }
  for (const auto &b : bufs) {
    if (!b.is_imported) {
      continue;
    }
    const hixl::Status ret = endpoint.UnimportMem(b.export_desc, b.export_len);
    if (ret != hixl::SUCCESS) {
      HIXL_REPORT_ERR_MSG("E19999", "Call api:MemUnimport failed, ret:%u, addr:%p, size:%" PRIu64 " bytes",
                          static_cast<uint32_t>(ret), b.mem.addr, b.mem.size);
      HIXL_LOGW("[HixlClient] Call api:MemUnimport failed, ret:%u, addr:%p, size:%" PRIu64 " bytes",
                static_cast<uint32_t>(ret), b.mem.addr, b.mem.size);
    }
  }
}

void UnrecordAddrs(hixl::HixlMemStore &store, std::vector<void *> &addrs) {
  for (auto *addr : addrs) {
    if (addr == nullptr) {
      continue;
    }
    const hixl::Status ret = store.UnrecordMemory(true, addr);
    if (ret != hixl::SUCCESS) {
      HIXL_LOGW("[HixlClient] UnrecordMemory failed. addr=%p ret=%u", addr, static_cast<uint32_t>(ret));
    }
  }
  addrs.clear();
}

hixl::Status ImportOneDesc(hixl::ImportCtx &ctx, uint32_t idx, hixl::HixlMemDesc &desc) {
  CommMem buf{};
  hixl::Status ret = ctx.ep->ImportMem(desc.export_desc, desc.export_len, buf);
  const char *safe_tag = desc.tag.empty() ? "<empty>" : desc.tag.c_str();
  if (ret != hixl::SUCCESS) {
    HIXL_LOGE(ret, "[HixlClient] MemImport failed, idx=%u, tag=%s", idx, safe_tag);
    return ret;
  }
  ctx.imported.emplace_back(buf);
  desc.is_imported = true;
  CommMem mem{};
  mem.type = desc.mem.type;
  mem.addr = desc.mem.addr;
  mem.size = desc.mem.size;
  bool is_host_mem = desc.mem.type == COMM_MEM_TYPE_HOST;
  HIXL_LOGI("[HixlClient] ImportOneDesc desc.tag=%s mem.addr=%p", safe_tag, mem.addr);
  ctx.mems.emplace_back(mem);
  if (!desc.tag.empty()) {
    ctx.tag_mem_map[desc.tag] = mem;
  }
  HIXL_LOGD("[HixlClient] Imported mem[%u]: tag='%s', addr=%p, size=%llu", idx, safe_tag, mem.addr, mem.size);
  ret = ctx.store->RecordMemory(true, mem.addr, static_cast<size_t>(mem.size), is_host_mem, desc.registered_dev_mem);
  if (ret == hixl::SUCCESS) {
    ctx.recorded_addrs.emplace_back(mem.addr);
  } else {
    HIXL_LOGE(ret,
              "[HixlClient] RecordMemory(server) failed! This memory may have been registered. idx=%u, tag=%s, "
              "addr=%p, size=%llu",
              idx, safe_tag, mem.addr, mem.size);
    return ret;
  }
  return AppendTagStorage(ctx.tag_storage, desc.tag);
}

hixl::Status ImportAllDescs(hixl::ImportCtx &ctx, std::vector<hixl::HixlMemDesc> &desc_list) {
  for (uint32_t i = 0; i < ctx.num; ++i) {
    hixl::Status ret = ImportOneDesc(ctx, i, desc_list[i]);
    if (ret != hixl::SUCCESS) {
      return ret;
    }
  }
  return hixl::SUCCESS;
}

bool IsDeviceEndpoint(const EndpointDesc &ep) {
  return (ep.loc.locType == ENDPOINT_LOC_TYPE_DEVICE);
}

}  // namespace

HixlCSClient::HixlCSClient() : mem_store_() {}

HixlCSClient::~HixlCSClient() {
  (void)Destroy();
}

Status HixlCSClient::InitBaseClient(const HixlClientDesc *client_desc) {
  connection_ = MakeShared<CtrlMsgPlugin>();
  HIXL_CHECK_NOTNULL(connection_);
  HIXL_CHK_STATUS_RET(connection_->Initialize(), "Failed to initialize control connection");
  server_ip_ = client_desc->server_ip;
  server_port_ = client_desc->server_port;
  const EndpointDesc &local_ep = local_endpoint_->GetEndpoint();
  tc_ = client_desc->tc;
  sl_ = client_desc->sl;
  remote_endpoint_ = *(client_desc->remote_endpoint);
  HIXL_LOGD("[HixlClient] CtrlMsgPlugin initialized");
  auto ctx_guard = GetContextGuard();
  (void)ctx_guard;
  HIXL_CHK_STATUS_RET(local_endpoint_->Initialize(),
                      "[HixlClient] Failed to initialize src endpoint. "
                      "Check Config: [Loc:%d, protocol:%s, AddrVal:0x%x]",
                      local_ep.loc.locType, ProtocolToString(local_ep.protocol).c_str(), local_ep.commAddr.id);
  HIXL_LOGI("[HixlClient] local_endpoint initialized. ep_handle=%p", local_endpoint_->GetHandle());
  return SUCCESS;
}

Status HixlCSClient::InitDeviceResource(const EndpointDesc &ep) {
  if (!IsDeviceEndpoint(ep)) {
    device_id_ = -1;
    return SUCCESS;
  }
  HIXL_CHK_ACL_RET(aclrtGetDevice(&device_id_), "[HixlClient] aclrtGetDevice failed");
  HIXL_LOGI("[HixlClient] device_id=%d", device_id_);
  // 创建context会切换当前context，因此需要在析构时恢复原用户context
  hixl::TemporaryRtContext with_context(nullptr);
  auto *pool = TransferPool::GetInstance(device_id_);
  HIXL_CHECK_NOTNULL(pool);
  HIXL_CHK_STATUS_RET(pool->Initialize(global_config_.MaxActiveChannels().value_or(kDefaultTransferPoolSize)),
                      "[HixlClient] TransferPool Initialize failed. devId=%d", device_id_);
  return SUCCESS;
}

Status HixlCSClient::ParseGlobalConfig(const HixlClientDesc *client_desc, const HixlClientConfig *config) {
  HIXL_CHECK_NOTNULL(client_desc->server_ip);
  HIXL_CHECK_NOTNULL(client_desc->local_endpoint);
  HIXL_CHECK_NOTNULL(client_desc->remote_endpoint);
  HIXL_CHECK_NOTNULL(config);
  global_config_ = GlobalConfig{};
  const bool is_host_roce = IsHostRoce(*client_desc->local_endpoint);
  HIXL_CHK_STATUS_RET(GlobalConfig::Parse(config->global_resource_config, global_config_,
                                          GlobalConfig::ParseTarget::kClient, !is_host_roce),
                      "[HixlClient] Failed to parse global_resource_config");
  auto endpoint = Endpoint::Create(*(client_desc->local_endpoint), *(client_desc->remote_endpoint), global_config_);
  HIXL_CHECK_NOTNULL(endpoint);
  HIXL_CHK_STATUS_RET(endpoint->ValidatePeerEndpoint(*(client_desc->remote_endpoint)),
                      "[HixlClient] Incompatible remote endpoint");
  const bool endpoint_configurable = endpoint->Capabilities().supports_configurable_transfer;
  HIXL_CHK_BOOL_RET_STATUS(
      endpoint_configurable || !global_config_.HasMaxTransferCountPerBatch() || IsHcommConfigurableTransferSupported(),
      PARAM_INVALID, "[HixlClient] Configurable transfer requires Hcomm >= 9.2.0");
  configurable_transfer_supported_ =
      endpoint_configurable || global_config_.HasMaxTransferCountPerBatch() || IsHcommConfigurableTransferSupported();
  if (configurable_transfer_supported_ && is_host_roce) {
    global_config_.SetDefaultMaxTransferCountPerBatch(kDefaultHostRoceMaxTransferCountPerBatch);
  }
  local_endpoint_ = std::move(endpoint);
  return SUCCESS;
}

Status HixlCSClient::Create(const HixlClientDesc *client_desc, const HixlClientConfig *config) {
  std::lock_guard<std::mutex> lock(mutex_);
  HIXL_CHK_BOOL_RET_STATUS(!tls_initialized_, FAILED, "[HixlClient] TLS is already initialized");
  HIXL_CHK_STATUS_RET(ParseGlobalConfig(client_desc, config), "[HixlClient] Invalid client configuration");
  HIXL_DISMISSABLE_GUARD(uninit_endpoint_guard, ([this]() { local_endpoint_.reset(); }));
  HIXL_CHK_STATUS_RET(TlsManager::GetInstance().Initialize(global_config_.TlsCfg()),
                      "[HixlClient] Failed to initialize TLS from global_resource_config");
  HIXL_DISMISSABLE_GUARD(tls_guard, ([this]() {
                           CloseSocket();
                           TlsManager::GetInstance().Finalize();
                         }));
  HIXL_EVENT(
      "[HixlClient] Create begin. Server=%s:%u. "
      "SrcEndpoint[Loc:%d, protocol:%s, commAddr.Type:%d, commAddr.id:0x%x], "
      "DstEndpoint[Loc:%d, protocol:%s, commAddr.Type:%d, commAddr.id:0x%x]",
      client_desc->server_ip, client_desc->server_port, client_desc->local_endpoint->loc.locType,
      ProtocolToString(client_desc->local_endpoint->protocol).c_str(), client_desc->local_endpoint->commAddr.type,
      client_desc->local_endpoint->commAddr.id, client_desc->remote_endpoint->loc.locType,
      ProtocolToString(client_desc->remote_endpoint->protocol).c_str(), client_desc->remote_endpoint->commAddr.type,
      client_desc->remote_endpoint->commAddr.id);
  HIXL_CHK_STATUS_RET(InitDeviceResource(*(client_desc->local_endpoint)), "[HixlClient] InitDeviceResource failed");
  HIXL_DISMISSABLE_GUARD(pool_rollback, ([this]() {
                           if (device_id_ >= 0) {
                             auto *pool = TransferPool::GetInstance(device_id_);
                             if (pool != nullptr) {
                               pool->Finalize();
                             }
                             device_id_ = -1;
                           }
                         }));
  HIXL_CHK_STATUS_RET(InitBaseClient(client_desc), "[HixlClient] InitBaseClient failed");
  HIXL_DISMISS_GUARD(uninit_endpoint_guard);
  const Status retry_cfg_ret = InitRdmaRetryConfig();
  if (retry_cfg_ret != SUCCESS) {
    HIXL_LOGE(retry_cfg_ret, "[HixlClient] InitRdmaRetryConfig failed");
    FinalizeEndpointAfterCreateFailure();
    return retry_cfg_ret;
  }
  client_channel_ = ChannelFactory::Create(*local_endpoint_);
  if (client_channel_ == nullptr) {
    HIXL_LOGE(PARAM_INVALID, "[HixlClient] Create channel failed, unsupported local endpoint protocol=%d",
              static_cast<int32_t>(local_endpoint_->GetEndpoint().protocol));
    FinalizeEndpointAfterCreateFailure();
    return PARAM_INVALID;
  }
  ChannelContext channel_ctx{};
  channel_ctx.local_endpoint = local_endpoint_.get();
  channel_ctx.device_id = device_id_;
  channel_ctx.global_config = &global_config_;
  channel_ctx.mem_store = &mem_store_;
  channel_ctx.configurable_transfer_supported = configurable_transfer_supported_;
  const Status channel_init_ret = client_channel_->Initialize(channel_ctx);
  if (channel_init_ret != SUCCESS) {
    (void)client_channel_->Destroy();
    client_channel_.reset();
    HIXL_LOGE(channel_init_ret, "[HixlClient] channel Initialize failed");
    FinalizeEndpointAfterCreateFailure();
    return channel_init_ret;
  }
  HIXL_DISMISS_GUARD(pool_rollback);
  HIXL_DISMISS_GUARD(tls_guard);
  tls_initialized_ = true;
  HIXL_EVENT("[HixlClient] Create success. server=%s:%u, channel_handle=%" PRIu64, server_ip_.c_str(), server_port_,
             client_channel_->GetHandle());
  return SUCCESS;
}

// 注册client的endpoint的内存信息到内存注册表中。
// mem是一个结构体，其中记录了内存类型、地址和大小。
Status HixlCSClient::RegMem(const char *mem_tag, const CommMem *mem, MemHandle *mem_handle) {
  std::lock_guard<std::mutex> lock(mutex_);
  HIXL_CHECK_NOTNULL(client_channel_);
  auto ctx_guard = GetContextGuard();
  (void)ctx_guard;
  return client_channel_->RegMem(mem_tag, mem, mem_handle);
}

Status HixlCSClient::BatchPutSync(const HixlOneSideOpDesc *desc_list, uint32_t list_num, uint32_t timeout_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  HIXL_CHECK_NOTNULL(client_channel_);
  auto ctx_guard = GetContextGuard();
  (void)ctx_guard;
  const Status ret = client_channel_->BatchPutSync(desc_list, list_num, timeout_ms);
  HIXL_CHK_STATUS_RET(ret, "[HixlClient] BatchPutSync failed, list_num=%u, local=[%s], remote=[%s]", list_num,
                      EndpointToString(local_endpoint_->GetEndpoint()).c_str(),
                      EndpointToString(remote_endpoint_).c_str());
  HIXL_LOGI("[HixlClient] BatchPutSync success, list_num=%u", list_num);
  return SUCCESS;
}

Status HixlCSClient::BatchGetSync(const HixlOneSideOpDesc *desc_list, uint32_t list_num, uint32_t timeout_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  HIXL_CHECK_NOTNULL(client_channel_);
  auto ctx_guard = GetContextGuard();
  (void)ctx_guard;
  const Status ret = client_channel_->BatchGetSync(desc_list, list_num, timeout_ms);
  HIXL_CHK_STATUS_RET(ret, "[HixlClient] BatchGetSync failed, list_num=%u, local=[%s], remote=[%s]", list_num,
                      EndpointToString(local_endpoint_->GetEndpoint()).c_str(),
                      EndpointToString(remote_endpoint_).c_str());
  HIXL_LOGI("[HixlClient] BatchGetSync success, list_num=%u", list_num);
  return SUCCESS;
}

Status HixlCSClient::BatchPutAsync(const HixlOneSideOpDesc *desc_list, uint32_t list_num, void **query_handle) {
  std::lock_guard<std::mutex> lock(mutex_);
  HIXL_CHECK_NOTNULL(client_channel_);
  auto ctx_guard = GetContextGuard();
  (void)ctx_guard;
  const Status ret = client_channel_->BatchPutAsync(desc_list, list_num, query_handle);
  HIXL_CHK_STATUS_RET(ret, "[HixlClient] BatchPutAsync failed, list_num=%u, local=[%s], remote=[%s]", list_num,
                      EndpointToString(local_endpoint_->GetEndpoint()).c_str(),
                      EndpointToString(remote_endpoint_).c_str());
  HIXL_LOGI("[HixlClient] BatchPutAsync success, list_num=%u", list_num);
  return SUCCESS;
}

Status HixlCSClient::BatchGetAsync(const HixlOneSideOpDesc *desc_list, uint32_t list_num, void **query_handle) {
  std::lock_guard<std::mutex> lock(mutex_);
  HIXL_CHECK_NOTNULL(client_channel_);
  auto ctx_guard = GetContextGuard();
  (void)ctx_guard;
  const Status ret = client_channel_->BatchGetAsync(desc_list, list_num, query_handle);
  HIXL_CHK_STATUS_RET(ret, "[HixlClient] BatchGetAsync failed, list_num=%u, local=[%s], remote=[%s]", list_num,
                      EndpointToString(local_endpoint_->GetEndpoint()).c_str(),
                      EndpointToString(remote_endpoint_).c_str());
  HIXL_LOGI("[HixlClient] BatchGetAsync success, list_num=%u", list_num);
  return SUCCESS;
}

// 通过已经建立好的channel，检查批量读写的状态。
Status HixlCSClient::QueryCompleteStatus(void *query_handle, HixlCompleteStatus *status) {
  std::lock_guard<std::mutex> lock(mutex_);
  HIXL_CHECK_NOTNULL(client_channel_);
  auto ctx_guard = GetContextGuard();
  (void)ctx_guard;
  return client_channel_->QueryCompleteStatus(query_handle, status);
}

// 注销client的endpoint的内存信息。
Status HixlCSClient::UnregMem(MemHandle mem_handle) {
  std::lock_guard<std::mutex> lock(mutex_);
  HIXL_CHECK_NOTNULL(mem_handle);
  HIXL_CHECK_NOTNULL(client_channel_);
  auto ctx_guard = GetContextGuard();
  (void)ctx_guard;
  return client_channel_->UnregMem(mem_handle);
}

std::unique_ptr<hixl::TemporaryRtContext> HixlCSClient::GetContextGuard() const {
  if (device_id_ >= 0) {
    auto *pool = TransferPool::GetInstance(device_id_);
    if (pool == nullptr) {
      return nullptr;
    }
    auto ctx = pool->GetContext();
    if (ctx != nullptr) {
      return MakeUnique<hixl::TemporaryRtContext>(ctx);
    }
  }
  return nullptr;  // 不切换 context
}

Status HixlCSClient::Connect(uint32_t timeout_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto ctx_guard = GetContextGuard();
  (void)ctx_guard;
  HIXL_CHECK_NOTNULL(local_endpoint_);
  HIXL_CHK_BOOL_RET_STATUS(remote_endpoint_.protocol != COMM_PROTOCOL_RESERVED, PARAM_INVALID,
                           "[HixlClient] Connect called but remote_endpoint is not set in Create");
  if (is_connected_) {
    HIXL_LOGW("[HixlClient] Already connected. connection=%s, Target=%s:%u", connection_->Desc().c_str(),
              server_ip_.c_str(), server_port_);
    return ALREADY_CONNECTED;
  }
  if (connection_ == nullptr) {
    connection_ = MakeShared<CtrlMsgPlugin>();
    HIXL_CHECK_NOTNULL(connection_);
    HIXL_CHK_STATUS_RET(connection_->Initialize(), "Failed to initialize control connection");
  }
  HIXL_EVENT("[HixlClient] Connect start. Target=%s:%u, timeout=%u ms", server_ip_.c_str(), server_port_, timeout_ms);
  const Status sock_ret = connection_->Connect(server_ip_, server_port_, timeout_ms);
  if (sock_ret != SUCCESS) {
    // CtrlMsgPlugin::Connect 失败时内部已关闭 fd，此处仅清除残留句柄，避免误判为已连接
    connection_.reset();
    HIXL_CHK_STATUS_RET(sock_ret, "[HixlClient] Connect socket to %s:%u failed", server_ip_.c_str(), server_port_);
  }
  HIXL_LOGI("[HixlClient] Socket connected (TCP ready). connection=%s", connection_->Desc().c_str());
  HIXL_DISMISSABLE_GUARD(close_socket, [this] { CloseSocket(); });
  HIXL_CHK_STATUS_RET(ExchangeEndpointAndCreateChannel(timeout_ms),
                      "[HixlClient] Exchange endpoint info failed. connection=%s, Target=%s:%u",
                      connection_->Desc().c_str(), server_ip_.c_str(), server_port_);
  HIXL_DISMISS_GUARD(close_socket);
  is_connected_ = true;
  HIXL_EVENT("[HixlClient] Connect success. target=%s:%u, connection=%s, remote_ep_handle=%" PRIu64 ", ch=%" PRIu64,
             server_ip_.c_str(), server_port_, connection_->Desc().c_str(), remote_endpoint_handle_,
             client_channel_->GetHandle());
  return SUCCESS;
}

void HixlCSClient::CloseSocket() {
  if (connection_ != nullptr) {
    HIXL_LOGI("[HixlClient] Closing socket. connection=%s", connection_->Desc().c_str());
    (void)connection_->Close();
    connection_.reset();
  }
}

void HixlCSClient::FinalizeEndpointAfterCreateFailure() {
  if (local_endpoint_ == nullptr) {
    return;
  }
  const Status ret = local_endpoint_->Finalize();
  if (ret != SUCCESS) {
    HIXL_LOGW("[HixlClient] Finalize endpoint after Create failure failed. ep_handle=%p, ret=%u",
              local_endpoint_->GetHandle(), static_cast<uint32_t>(ret));
  }
  local_endpoint_.reset();
}

Status HixlCSClient::ExchangeEndpointAndCreateChannel(uint32_t timeout_ms) {
  const EndpointDesc &src_ep = local_endpoint_->GetEndpoint();
  HIXL_LOGD(
      "[HixlClient] MatchEndpoint then CreateChannel. connection: %s, timeout: %u ms, "
      "Src[protocol:%s, type:%d, id:%u], Dst[protocol:%s, type:%d, id:%u]",
      connection_->Desc().c_str(), timeout_ms, ProtocolToString(src_ep.protocol).c_str(),
      static_cast<int32_t>(src_ep.commAddr.type), src_ep.commAddr.id,
      ProtocolToString(remote_endpoint_.protocol).c_str(), static_cast<int32_t>(remote_endpoint_.commAddr.type),
      remote_endpoint_.commAddr.id);
  HIXL_CHK_STATUS_RET(ConnMsgHandler::SendMatchEndpointRequest(connection_, remote_endpoint_),
                      "[HixlClient] SendMatchEndpointRequest failed. connection=%s", connection_->Desc().c_str());
  uint32_t remote_listen_port = 0;
  uint64_t channel_index = 0UL;
  HIXL_CHK_STATUS_RET(ConnMsgHandler::RecvMatchEndpointResponse(connection_, remote_endpoint_handle_,
                                                                remote_listen_port, channel_index, timeout_ms),
                      "[HixlClient] RecvMatchEndpointResponse failed. connection=%s, timeout=%u ms",
                      connection_->Desc().c_str(), timeout_ms);
  local_endpoint_->SetPort(remote_listen_port);
  CommMem *prefetch_mems = nullptr;
  char **prefetch_tags = nullptr;
  uint32_t prefetch_num = 0U;
  HIXL_LOGI("[HixlClient] Connect: prefetch remote mem before CreateChannel");
  HIXL_CHK_STATUS_RET(GetRemoteMemImpl(timeout_ms, &prefetch_mems, &prefetch_tags, &prefetch_num),
                      "[HixlClient] Connect prefetch GetRemoteMem/Import failed. connection=%s, timeout=%u ms",
                      connection_->Desc().c_str(), timeout_ms);
  const uint8_t qos = global_config_.Qos().value_or(kQosUnset);
  ChannelDesc channel_desc{remote_endpoint_,
                           tc_,
                           sl_,
                           retry_cnt_,
                           retry_interval_,
                           ChannelType::kClient,
                           channel_index,
                           qos,
                           global_config_.MaxTransferCountPerBatch(),
                           configurable_transfer_supported_,
                           remote_endpoint_handle_};
  HIXL_CHK_STATUS_RET(client_channel_->Connect(channel_desc, timeout_ms, connection_),
                      "[HixlClient] channel Connect failed. Dst[id:0x%x]", remote_endpoint_.commAddr.id);
  HIXL_LOGI("[HixlClient] Connect: remote endpoint handle = %" PRIu64, remote_endpoint_handle_);
  HIXL_LOGI("[HixlClient] Channel Ready. channel_handle=%" PRIu64, client_channel_->GetHandle());
  return SUCCESS;
}

Status HixlCSClient::InitRdmaRetryConfig() {
  retry_cnt_ = kRdmaRetryCntDefault;
  retry_interval_ = kRdmaRetryIntervalDefault;

  if (global_config_.RoceRetryCnt().has_value()) {
    retry_cnt_ = *global_config_.RoceRetryCnt();
    HIXL_LOGI("[HixlClient] use roce_config.retry_cnt=%u", retry_cnt_);
  } else {
    uint32_t env_retry_cnt = UINT32_MAX;
    bool has_env_retry_cnt = false;
    HIXL_CHK_STATUS_RET(ParseEnvUint32("HCCL_RDMA_RETRY_CNT", env_retry_cnt, has_env_retry_cnt),
                        "[HixlClient] Parse HCCL_RDMA_RETRY_CNT failed");
    if (has_env_retry_cnt) {
      HIXL_CHK_BOOL_RET_STATUS(env_retry_cnt >= kMinRdmaRetryCnt && env_retry_cnt <= kMaxRdmaRetryCnt, PARAM_INVALID,
                               "[HixlClient] HCCL_RDMA_RETRY_CNT=%u is invalid, valid range=[%u, %u]", env_retry_cnt,
                               kMinRdmaRetryCnt, kMaxRdmaRetryCnt);
      retry_cnt_ = env_retry_cnt;
      HIXL_LOGI("[HixlClient] use HCCL_RDMA_RETRY_CNT=%u", retry_cnt_);
    } else {
      HIXL_LOGI("[HixlClient] use default rdma retry cnt=%u", retry_cnt_);
    }
  }

  if (global_config_.RoceTimeout().has_value()) {
    retry_interval_ = *global_config_.RoceTimeout();
    HIXL_LOGI("[HixlClient] use roce_config.timeout=%u", retry_interval_);
  } else {
    uint32_t env_retry_interval = UINT32_MAX;
    bool has_env_retry_interval = false;
    HIXL_CHK_STATUS_RET(ParseEnvUint32("HCCL_RDMA_TIMEOUT", env_retry_interval, has_env_retry_interval),
                        "[HixlClient] Parse HCCL_RDMA_TIMEOUT failed");
    if (has_env_retry_interval) {
      const auto &caps = local_endpoint_->Capabilities();
      HIXL_CHK_BOOL_RET_STATUS(
          env_retry_interval >= caps.min_retry_interval && env_retry_interval <= caps.max_retry_interval, PARAM_INVALID,
          "[HixlClient] HCCL_RDMA_TIMEOUT=%u is invalid, valid range=[%u, %u]", env_retry_interval,
          caps.min_retry_interval, caps.max_retry_interval);
      retry_interval_ = env_retry_interval;
      HIXL_LOGI("[HixlClient] use HCCL_RDMA_TIMEOUT=%u", retry_interval_);
    } else {
      HIXL_LOGI("[HixlClient] use default rdma retry interval=%u", retry_interval_);
    }
  }
  return SUCCESS;
}

Status HixlCSClient::GetRemoteMemImpl(uint32_t timeout_ms, CommMem **remote_mem_list, char ***mem_tag_list,
                                      uint32_t *list_num) {
  HIXL_CHECK_NOTNULL(local_endpoint_);
  HIXL_CHK_BOOL_RET_STATUS(connection_ != nullptr, FAILED,
                           "[HixlClient] Cannot get remote memory: control connection is missing");
  HIXL_CHK_STATUS_RET(MemMsgHandler::SendGetRemoteMemRequest(connection_, remote_endpoint_handle_, timeout_ms),
                      "[HixlClient] SendGetRemoteMemRequest failed. connection=%s, remote_ep_handle=%" PRIu64,
                      connection_->Desc().c_str(), remote_endpoint_handle_);
  std::vector<HixlMemDesc> mem_descs;
  HIXL_CHK_STATUS_RET(MemMsgHandler::RecvGetRemoteMemResponse(connection_, mem_descs, timeout_ms),
                      "[HixlClient] RecvGetRemoteMemResponse failed. connection=%s, timeout=%u ms",
                      connection_->Desc().c_str(), timeout_ms);
  HIXL_LOGD("[HixlClient] Recv remote mem descs success. Count=%zu", mem_descs.size());
  HIXL_CHK_STATUS_RET(ImportRemoteMem(mem_descs, remote_mem_list, mem_tag_list, list_num),
                      "[HixlClient] ImportRemoteMem failed. desc_count=%zu", mem_descs.size());
  return SUCCESS;
}

Status HixlCSClient::GetRemoteMem(CommMem **remote_mem_list, char ***mem_tag_list, uint32_t *list_num,
                                  uint32_t timeout_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto ctx_guard = GetContextGuard();
  (void)ctx_guard;
  HIXL_EVENT("[HixlClient] GetRemoteMem begin. remote_ep_handle=%" PRIu64 ", timeout=%u ms", remote_endpoint_handle_,
             timeout_ms);
  HIXL_CHECK_NOTNULL(remote_mem_list);
  HIXL_CHECK_NOTNULL(mem_tag_list);
  HIXL_CHECK_NOTNULL(list_num);
  *remote_mem_list = nullptr;
  *mem_tag_list = nullptr;
  HIXL_CHECK_NOTNULL(local_endpoint_);
  HIXL_CHK_STATUS_RET(GetRemoteMemImpl(timeout_ms, remote_mem_list, mem_tag_list, list_num),
                      "[HixlClient] GetRemoteMemImpl failed");
  HIXL_EVENT("[HixlClient] GetRemoteMem success. connection=%s, remote_ep_handle=%" PRIu64 ", imported=%u",
             connection_->Desc().c_str(), remote_endpoint_handle_, *list_num);
  return SUCCESS;
}

void HixlCSClient::FillOutputParams(ImportCtx &ctx, CommMem **remote_mem_list, char ***mem_tag_list,
                                    uint32_t *list_num) {
  imported_remote_bufs_ = std::move(ctx.imported);
  recorded_remote_addrs_ = std::move(ctx.recorded_addrs);
  tag_mem_descs_ = std::move(ctx.tag_mem_map);
  remote_mems_out_ = std::move(ctx.mems);
  remote_tag_storage_.clear();
  remote_tag_ptrs_.clear();
  remote_tag_storage_ = std::move(ctx.tag_storage);
  BuildTagPtrs(remote_tag_storage_, remote_tag_ptrs_);
  *mem_tag_list = remote_tag_ptrs_.empty() ? nullptr : remote_tag_ptrs_.data();
  *remote_mem_list = remote_mems_out_.empty() ? nullptr : remote_mems_out_.data();
  *list_num = static_cast<uint32_t>(remote_mems_out_.size());
}

Status HixlCSClient::ImportRemoteMem(std::vector<HixlMemDesc> &desc_list, CommMem **remote_mem_list,
                                     char ***mem_tag_list, uint32_t *list_num) {
  HIXL_DISMISSABLE_GUARD(free_export_desc, [&desc_list]() { FreeExportDesc(desc_list); });
  *list_num = static_cast<uint32_t>(desc_list.size());
  HIXL_CHK_STATUS_RET(ClearRemoteMemInfo(), "[HixlClient] ClearRemoteMemInfo before ImportRemoteMem failed");
  if (*list_num == 0U) {
    HIXL_CHK_BOOL_RET_STATUS(local_endpoint_ == nullptr || !local_endpoint_->Capabilities().requires_device_trans_flag,
                             PARAM_INVALID, "[HixlClient] Endpoint requires the peer HBM read-after-write flag");
    HIXL_LOGI("[HixlClient] Remote mem list is empty, nothing to import.");
    return SUCCESS;
  }
  HIXL_CHK_STATUS_RET(ValidateExportDescList(desc_list), "[HixlClient] ValidateExportDescList failed");
  HIXL_CHECK_NOTNULL(local_endpoint_);
  EndpointHandle ep_handle = local_endpoint_->GetHandle();
  HIXL_CHECK_NOTNULL(ep_handle, "[HixlClient] ImportRemoteMem: endpoint handle is null");
  ImportCtx ctx;
  ctx.ep = local_endpoint_.get();
  ctx.ep_handle = ep_handle;
  ctx.store = &mem_store_;
  ctx.num = *list_num;
  ctx.imported.reserve(ctx.num);
  ctx.recorded_addrs.reserve(ctx.num);
  ctx.mems.reserve(ctx.num);
  ctx.tag_storage.reserve(ctx.num);
  Status ret = ImportAllDescs(ctx, desc_list);
  auto rollback_import = [&]() {
    HIXL_LOGW("[HixlClient] RollbackImport triggered. Cleaning up %zu imported bufs.", desc_list.size());
    CloseImportedBufs(*local_endpoint_, desc_list);
    // Descriptors imported before the failing one were already recorded in the mem store; drop those
    // records too, otherwise a later transfer passes validation and then fails during translation.
    UnrecordAddrs(*ctx.store, ctx.recorded_addrs);
  };
  if (ret != SUCCESS) {
    rollback_import();
    return ret;
  }
  // 提前把对端注册的完成标志交给 channel（device 侧在此解析 remote flag），避免传输时引入耗时。
  const bool require_device_flag = local_endpoint_->Capabilities().requires_device_trans_flag;
  const bool remote_is_host = remote_endpoint_.loc.locType == ENDPOINT_LOC_TYPE_HOST && !require_device_flag;
  const char *trans_flag_tag = remote_is_host ? kBuiltinHostTransFlagName : kBuiltinDeviceTransFlagName;
  CommMem trans_flag{};
  const auto trans_flag_it = ctx.tag_mem_map.find(trans_flag_tag);
  if (trans_flag_it != ctx.tag_mem_map.end()) {
    trans_flag = trans_flag_it->second;
  }
  if (require_device_flag && (trans_flag.addr == nullptr || trans_flag.type != COMM_MEM_TYPE_DEVICE)) {
    HIXL_LOGE(PARAM_INVALID, "[HixlClient] Endpoint requires the peer HBM read-after-write flag");
    rollback_import();
    return PARAM_INVALID;
  }
  ret = client_channel_->SetRemoteMemInfo(trans_flag);
  if (ret != SUCCESS) {
    HIXL_LOGE(ret, "[HixlClient] SetRemoteMemInfo failed");
    rollback_import();
    return ret;
  }
  desc_list_ = std::move(desc_list);
  FillOutputParams(ctx, remote_mem_list, mem_tag_list, list_num);
  HIXL_DISMISS_GUARD(free_export_desc);
  return SUCCESS;
}

Status HixlCSClient::ClearRemoteMemInfo() {
  const size_t buf_cnt = imported_remote_bufs_.size();
  const size_t addr_cnt = recorded_remote_addrs_.size();
  if (buf_cnt > 0U || addr_cnt > 0U) {
    HIXL_LOGI("[HixlClient] Cleaning up remote mem info. Bufs=%zu, Addrs=%zu", buf_cnt, addr_cnt);
  }
  if (!desc_list_.empty()) {
    if (local_endpoint_ != nullptr) {
      CloseImportedBufs(*local_endpoint_, desc_list_);
    } else {
      HIXL_LOGW("[HixlClient] ClearRemoteMemInfo: endpoint null, skip MemUnimport for %zu bufs", desc_list_.size());
    }
    for (auto &desc : desc_list_) {
      if (desc.export_desc != nullptr) {
        std::free(desc.export_desc);
        desc.export_desc = nullptr;
      }
    }
    desc_list_.clear();
  }
  if (!recorded_remote_addrs_.empty()) {
    UnrecordAddrs(mem_store_, recorded_remote_addrs_);
  }
  tag_mem_descs_.clear();
  remote_mems_out_.clear();
  remote_tag_ptrs_.clear();
  remote_tag_storage_.clear();
  // Drop the completion flag cached by the channel together with the remote memory it lives in. The channel
  // is already gone in Destroy(), where client_channel_ is null, so this reset is best-effort.
  if (client_channel_ != nullptr) {
    const Status reset_ret = client_channel_->SetRemoteMemInfo(CommMem{});
    if (reset_ret != SUCCESS) {
      HIXL_LOGW("[HixlClient] Reset channel remote trans flag failed. ret=%u", static_cast<uint32_t>(reset_ret));
    }
  }
  return SUCCESS;
}

Status HixlCSClient::Destroy() {
  std::lock_guard<std::mutex> lock(mutex_);
  Status first_error = SUCCESS;
  {
    auto ctx_guard = GetContextGuard();
    (void)ctx_guard;
    HIXL_EVENT("[HixlClient] Destroy start. imported_bufs=%zu, recorded_addrs=%zu", imported_remote_bufs_.size(),
               recorded_remote_addrs_.size());
    if (client_channel_ != nullptr) {
      const Status channel_ret = client_channel_->Destroy();
      if (channel_ret != SUCCESS) {
        HIXL_LOGW("[HixlClient] Destroy channel failed. ret=%u", static_cast<uint32_t>(channel_ret));
        first_error = (first_error == SUCCESS) ? channel_ret : first_error;
      }
      client_channel_.reset();
    }
    Status ret = ClearRemoteMemInfo();
    if (ret != SUCCESS) {
      HIXL_LOGW("[HixlClient] ClearRemoteMemInfo failed. ret=%u", static_cast<uint32_t>(ret));
      first_error = (first_error == SUCCESS) ? ret : first_error;
    }
    CloseSocket();
    is_connected_ = false;
    if (local_endpoint_ != nullptr) {
      ret = local_endpoint_->Finalize();
      if (ret != SUCCESS) {
        HIXL_LOGW("[HixlClient] Finalize endpoint failed in Destroy. ep_handle=%p, ret=%u",
                  local_endpoint_->GetHandle(), static_cast<uint32_t>(ret));
        first_error = (first_error == SUCCESS) ? ret : first_error;
      }
      local_endpoint_.reset();
    }
  }
  // ctx_guard 已析构，TransferPool 放到最后销毁
  if (device_id_ >= 0) {
    auto *pool = TransferPool::GetInstance(device_id_);
    if (pool != nullptr) {
      pool->Finalize();
    }
    device_id_ = -1;
  }
  if (tls_initialized_) {
    tls_initialized_ = false;
    TlsManager::GetInstance().Finalize();
  }
  HIXL_EVENT("[HixlClient] Destroy done. first_error=%u", static_cast<uint32_t>(first_error));
  return first_error;
}
}  // namespace hixl
