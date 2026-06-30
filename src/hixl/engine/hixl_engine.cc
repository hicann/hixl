/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <string>

#include "hixl_engine.h"
#include "hixl_options.h"
#include "engine/endpoint_generator.h"
#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/hixl_utils.h"
#include "common/llm_utils.h"
#include "common/scope_guard.h"
#include "profiling/prof_api_reg.h"
#include "acl/acl.h"

namespace hixl {
namespace {
constexpr int32_t kAutoConnectTimeout = 3000;
}

const std::unordered_set<std::string> HixlEngine::kSupportedOptions = {
    OPTION_RDMA_TRAFFIC_CLASS,    adxl::OPTION_RDMA_TRAFFIC_CLASS,
    OPTION_RDMA_SERVICE_LEVEL,    adxl::OPTION_RDMA_SERVICE_LEVEL,
    OPTION_LOCAL_COMM_RES,        adxl::OPTION_LOCAL_COMM_RES,
    OPTION_BUFFER_POOL,           adxl::OPTION_BUFFER_POOL,
    OPTION_AUTO_CONNECT,          adxl::OPTION_AUTO_CONNECT,
    OPTION_GLOBAL_RESOURCE_CONFIG};

bool HixlEngine::IsInitialized() const {
  return is_initialized_.load(std::memory_order::memory_order_relaxed);
}

Status HixlEngine::InitServer(std::optional<uint32_t> listen_port) {
  std::string ip;
  int32_t port = 0;
  HIXL_CHK_STATUS_RET(ParseListenInfo(local_engine_, ip, port),
                      "[HixlEngine] Failed to parse ip and port, local_engine should be in form as below: "
                      "ipv4 should be 'host_ip:host_port' or 'host_ip' "
                      "ipv6 should be '[host_ip]:host_port' or '[host_ip]' "
                      "current local_engine:%s",
                      local_engine_.c_str());
  HIXL_CHK_STATUS_RET(server_.Initialize(ip, port, endpoint_list_, listen_port),
                      "[HixlEngine] Failed to initialize HixlEngine, local_engine:%s", local_engine_.c_str());
  return SUCCESS;
}

Status HixlEngine::Initialize(const HixlOptions &options) {
  HIXL_LOGI("[HixlEngine] Initialization started, local_engine:%s", local_engine_.c_str());
  std::lock_guard<std::mutex> lock(mutex_);
  HIXL_CHK_STATUS_RET(options.CheckSupportedOptions(kSupportedOptions), "[HixlEngine] Unsupported option");
  const auto &raw = options.RawOptions();
  const auto &hixl_bp_it = raw.find(hixl::OPTION_BUFFER_POOL);
  const auto &adxl_bp_it = raw.find(adxl::OPTION_BUFFER_POOL);
  auto bp_it = (hixl_bp_it != raw.cend()) ? hixl_bp_it : adxl_bp_it;
  if (bp_it != raw.cend()) {
    HIXL_CHK_BOOL_RET_STATUS(std::string(bp_it->second.GetString()) == "0:0", PARAM_INVALID,
                             "Invalid option fields, OPTION_BUFFER_POOL for hixl engine only supports 0:0");
  }
  std::string local_comm_res;
  Status ret = EndpointGenerator::BuildEndpointList(options, local_engine_, local_comm_res, endpoint_list_);
  HIXL_CHK_STATUS_RET(ret, "[HixlEngine] Failed to build endpoint list from options");
  auto global_resource_config = options.GlobalResourceCfg();
  std::optional<uint32_t> listen_port;
  if (global_resource_config.has_value()) {
    listen_port = global_resource_config->comm_resource_config.listen_port;
    qos_ = global_resource_config->comm_resource_config.qos;
  } else {
    listen_port.reset();
    qos_.reset();
  }
  HIXL_CHK_STATUS_RET(aclrt_context_.CreateContext(), "[HixlEngine] Failed to create optional aclrt context");
  HIXL_DISMISSABLE_GUARD(ctx_fail_guard, ([this]() { aclrt_context_.DestroyContext(); }));
  {
    auto with_context = aclrt_context_.GetContextGuard();
    HIXL_CHK_STATUS_RET(InitServer(listen_port),
                        "[HixlEngine] Failed to initialize server, local_engine:%s, local_comm_res:%s",
                        local_engine_.c_str(), local_comm_res.c_str());
  }
  rdma_traffic_class_ = options.RdmaTrafficClass().value_or(kRdmaTrafficClass);
  rdma_service_level_ = options.RdmaServiceLevel().value_or(kRdmaServiceLevel);
  auto_connect_ = options.AutoConnect().value_or(false);
  HIXL_CHK_STATUS_RET(client_manager_.Initialize(auto_connect_), "[HixlEngine] Failed to initialize client manager");
  HIXL_DISMISS_GUARD(ctx_fail_guard);
  is_initialized_ = true;
  HIXL_LOGI("[HixlEngine] Initialization succeeded, local_engine:%s", local_engine_.c_str());
  return SUCCESS;
}

Status HixlEngine::RegisterMem(const MemDesc &mem, MemType type, MemHandle &mem_handle) {
  HIXL_LOGI("[HixlEngine] Registration started, type:%s, addr:%p, size:%lu", MemTypeToString(type).c_str(),
            reinterpret_cast<void *>(mem.addr), mem.len);
  auto with_context = aclrt_context_.GetContextGuard();
  HIXL_CHK_STATUS_RET(server_.RegisterMem(mem, type, mem_handle),
                      "[HixlEngine] Failed to register mem, type:%s, addr:%p, size:%lu", MemTypeToString(type).c_str(),
                      reinterpret_cast<void *>(mem.addr), mem.len);
  MemHandleInfo mem_info = {mem_handle, mem, type};
  std::lock_guard<std::mutex> lock(mutex_);
  mem_map_.emplace(mem_handle, mem_info);
  HIXL_LOGI("[HixlEngine] Registration succeeded, type:%s, addr:%p, size:%lu", MemTypeToString(type).c_str(),
            reinterpret_cast<void *>(mem.addr), mem.len);
  return SUCCESS;
}

Status HixlEngine::DeregisterMem(MemHandle mem_handle) {
  HIXL_LOGI("[HixlEngine] Deregistration started, mem_handle: %p", mem_handle);
  auto with_context = aclrt_context_.GetContextGuard();
  std::lock_guard<std::mutex> lock(mutex_);
  const auto &it = mem_map_.find(mem_handle);
  if (it == mem_map_.end()) {
    HIXL_LOGW("[HixlEngine] handle:%p is not registered", mem_handle);
    return SUCCESS;
  }
  HIXL_CHK_BOOL_RET_STATUS(
      client_manager_.IsEmpty(), FAILED,
      "[HixlEngine] Failed to deregister mem. All clients must be disconnected before deregistration, "
      "mem_handle: %p, local_engine: %s",
      mem_handle, local_engine_.c_str());
  HIXL_CHK_STATUS_RET(server_.DeregisterMem(mem_handle),
                      "[HixlEngine] Failed to deregister mem, mem_handle: %p, local_engine: %s", mem_handle,
                      local_engine_.c_str());
  mem_map_.erase(it);
  HIXL_LOGI("[HixlEngine] Deregistration succeeded, mem_handle: %p", mem_handle);
  return SUCCESS;
}

Status HixlEngine::Connect(const AscendString &remote_engine, int32_t timeout_in_millis) {
  HIXL_CHK_BOOL_RET_STATUS(strcmp(local_engine_.c_str(), remote_engine.GetString()) != 0, PARAM_INVALID,
                           "[HixlEngine] Do not support connection with self, please check remote engine. "
                           "local_engine:%s, remote_engine:%s",
                           local_engine_.c_str(), remote_engine.GetString());
  HIXL_LOGI("[HixlEngine] Connection started, local_engine:%s, remote_engine:%s", local_engine_.c_str(),
            remote_engine.GetString());
  auto with_context = aclrt_context_.GetContextGuard();
  ClientConfig config{};
  std::vector<MemHandleInfo> mem_info_list;
  BuildClientConfig(remote_engine, config, mem_info_list, timeout_in_millis);
  config.is_lazy = false;
  ClientPtr client_ptr = nullptr;
  Status ret = client_manager_.GetOrCreateClient(config, mem_info_list, client_ptr);
  HIXL_CHK_BOOL_RET_STATUS(ret == SUCCESS || ret == ALREADY_CONNECTED, ret,
                           "[HixlEngine] Failed to create client, local_engine:%s, remote_engine:%s, timeout:%d ms",
                           local_engine_.c_str(), remote_engine.GetString(), timeout_in_millis);
  HIXL_CHK_STATUS_RET(client_ptr->Connect(timeout_in_millis),
                      "[HixlEngine] Failed to connect client, local_engine:%s, remote_engine:%s, timeout:%d ms",
                      local_engine_.c_str(), remote_engine.GetString(), timeout_in_millis);
  HIXL_LOGI("[HixlEngine] Connection succeeded, local_engine:%s, remote_engine:%s", local_engine_.c_str(),
            remote_engine.GetString());
  return SUCCESS;
}

Status HixlEngine::Disconnect(const AscendString &remote_engine, int32_t timeout_in_millis) {
  HIXL_LOGI("[HixlEngine] Disconnection started, local_engine:%s, remote_engine:%s, timeout:%d ms",
            local_engine_.c_str(), remote_engine.GetString(), timeout_in_millis);
  auto with_context = aclrt_context_.GetContextGuard();
  Status ret = client_manager_.DestroyClient(remote_engine.GetString());
  HIXL_CHK_STATUS_RET(ret, "[HixlEngine] Failed to disconnect, local_engine:%s, remote_engine:%s, timeout:%d ms",
                      local_engine_.c_str(), remote_engine.GetString(), timeout_in_millis);
  HIXL_LOGI("[HixlEngine] Disconnection succeeded, local_engine:%s, remote_engine:%s, timeout:%d ms",
            local_engine_.c_str(), remote_engine.GetString(), timeout_in_millis);
  return SUCCESS;
}

void HixlEngine::Disconnect() {
  HIXL_LOGI("[HixlEngine] Disconnection with all clients started, local_engine:%s", local_engine_.c_str());
  auto with_context = aclrt_context_.GetContextGuard();
  Status ret = client_manager_.Finalize();
  if (ret != SUCCESS) {
    HIXL_LOGE(FAILED, "[HixlEngine] Failed to disconnect with all clients, local_engine:%s", local_engine_.c_str());
  } else {
    HIXL_LOGI("[HixlEngine] Disconnection with all clients succeeded, local_engine:%s", local_engine_.c_str());
  }
}

Status HixlEngine::TransferSync(const AscendString &remote_engine, TransferOp operation,
                                const std::vector<TransferOpDesc> &op_descs, int32_t timeout_in_millis) {
  HIXL_LOGI("[HixlEngine] Synchronous transmission started, local_engine:%s, remote_engine:%s, timeout:%d ms",
            local_engine_.c_str(), remote_engine.GetString(), timeout_in_millis);
  HIXL_CHK_STATUS_RET(AutoConnect(remote_engine, timeout_in_millis),
                      "[HixlEngine] Failed to auto connect before TransferSync, local_engine:%s, remote_engine:%s",
                      local_engine_.c_str(), remote_engine.GetString());
  ClientPtr client_ptr = client_manager_.GetClient(remote_engine.GetString());
  HIXL_CHK_BOOL_RET_STATUS(client_ptr != nullptr, NOT_CONNECTED,
                           "[HixlEngine] Failed to get client through remote engine, please check connection. "
                           "local_engine:%s, remote_engine:%s",
                           local_engine_.c_str(), remote_engine.GetString());
  HixlProfType type = (operation == READ ? HixlProfType::HixlOpBatchRead : HixlProfType::HixlOpBatchWrite);
  HIXL_API_PROFILING(type);
  Status ret = client_ptr->TransferSync(op_descs, operation, timeout_in_millis);
  if (ret != SUCCESS) {
    HIXL_LOGE(ret, "[HixlEngine] Failed to TransferSync, local_engine:%s, remote_engine:%s, timeout:%d ms",
              local_engine_.c_str(), remote_engine.GetString(), timeout_in_millis);
    HIXL_CHK_STATUS_RET(AutoDisconnect(remote_engine.GetString(), timeout_in_millis),
                        "[HixlEngine] Failed to disconnect on error.");
    return ret;
  }
  HIXL_LOGI("[HixlEngine] Synchronous transmission succeeded, local_engine:%s, remote_engine:%s, timeout:%d ms",
            local_engine_.c_str(), remote_engine.GetString(), timeout_in_millis);
  return SUCCESS;
}

Status HixlEngine::TransferAsync(const AscendString &remote_engine, TransferOp operation,
                                 const std::vector<TransferOpDesc> &op_descs, const TransferArgs &optional_args,
                                 TransferReq &req) {
  HIXL_LOGI("[HixlEngine] Asynchronous transmission started, local_engine:%s, remote_engine:%s", local_engine_.c_str(),
            remote_engine.GetString());
  HIXL_CHK_STATUS_RET(AutoConnect(remote_engine, kAutoConnectTimeout),
                      "[HixlEngine] Failed to auto connect before TransferAsync, local_engine:%s, remote_engine:%s",
                      local_engine_.c_str(), remote_engine.GetString());
  ClientPtr client_ptr = client_manager_.GetClient(remote_engine.GetString());
  HIXL_CHK_BOOL_RET_STATUS(
      client_ptr != nullptr, NOT_CONNECTED,
      "[HixlEngine] Failed to get client through remote engine, please check connection. remote_engine:%s",
      remote_engine.GetString());
  Status trans_status = client_ptr->TransferAsync(op_descs, operation, optional_args, req);
  if (trans_status != SUCCESS) {
    HIXL_LOGE(trans_status, "[HixlEngine] Failed to TransferAsync, local_engine:%s, remote_engine:%s",
              local_engine_.c_str(), remote_engine.GetString());
    HIXL_CHK_STATUS_RET(AutoDisconnect(remote_engine, kAutoConnectTimeout),
                        "[HixlEngine] Failed to disconnect on error.");
    return trans_status;
  }
  client_manager_.RegisterTransferReq(req, client_ptr, optional_args.user_data);
  HIXL_LOGI("[HixlEngine] Asynchronous transmission succeeded, local_engine:%s, remote_engine:%s",
            local_engine_.c_str(), remote_engine.GetString());
  return SUCCESS;
}

Status HixlEngine::GetTransferStatus(const TransferReq &req, TransferStatus &status) {
  ClientPtr client = client_manager_.GetClientByReq(req);
  if (client == nullptr) {
    status = TransferStatus::FAILED;
    HIXL_LOGE(PARAM_INVALID, "[HixlEngine] Request not found, request has been completed or does not exist, req: %p",
              req);
    return PARAM_INVALID;
  }
  Status ret = client->GetTransferStatus(req, status);
  if (ret != SUCCESS) {
    HIXL_LOGE(ret, "[HixlEngine] Failed to get status through client, local_engine:%s, req:%p, status:%d",
              local_engine_.c_str(), req, static_cast<int32_t>(status));
    client_manager_.EraseTransferReq(req);
    HIXL_CHK_STATUS_RET(AutoDisconnect(AscendString(client->GetRemoteEngine().c_str()), kAutoConnectTimeout),
                        "[HixlEngine] Failed to disconnect on error.");
    return ret;
  }
  if (status != TransferStatus::WAITING) {
    client_manager_.EraseTransferReq(req);
  }
  return SUCCESS;
}

Status HixlEngine::GetTransferStatus(const GetTransferStatusArgs &args, std::vector<TransferResult> &results) {
  results.clear();
  if (args.max_query_count == 0) {
    return SUCCESS;
  }
  auto reqs = client_manager_.GetOrderedReqs(0);
  results.reserve(reqs.size());
  for (const auto &it : reqs) {
    TransferReq req = it.req;
    ClientPtr client = it.client;
    if (client == nullptr) {
      client_manager_.EraseTransferReq(req);
      continue;
    }
    TransferStatus status = TransferStatus::FAILED;
    Status ret = client->GetTransferStatus(req, status);
    status = (ret == SUCCESS) ? status : TransferStatus::FAILED;
    if (ret != SUCCESS) {
      HIXL_LOGE(ret, "[HixlEngine] Failed to get status through client, local_engine:%s, req:%p, status:%d",
                local_engine_.c_str(), req, static_cast<int32_t>(status));
      (void)AutoDisconnect(AscendString(client->GetRemoteEngine().c_str()), kAutoConnectTimeout);
    }
    if (status != TransferStatus::WAITING) {
      client_manager_.EraseTransferReq(req);
    }
    if (args.skip_waiting && status == TransferStatus::WAITING) {
      continue;
    }
    results.emplace_back(TransferResult{req, it.user_data, status});
    if (results.size() >= static_cast<size_t>(args.max_query_count)) {
      break;
    }
  }
  return SUCCESS;
}

void HixlEngine::Finalize() {
  HIXL_LOGI("[HixlEngine] Finalization started");
  std::lock_guard<std::mutex> lock(mutex_);
  {
    auto with_context = aclrt_context_.GetContextGuard();
    server_.Finalize();
    client_manager_.Finalize();
    mem_map_.clear();
  }
  aclrt_context_.DestroyContext();
  is_initialized_ = false;
  HIXL_LOGI("[HixlEngine] Finalization succeeded");
}

Status HixlEngine::SendNotify(const AscendString &remote_engine, const NotifyDesc &notify, int32_t timeout_in_millis) {
  HIXL_LOGI("[HixlEngine] SendNotify started, local_engine:%s, remote_engine:%s, timeout:%d ms", local_engine_.c_str(),
            remote_engine.GetString(), timeout_in_millis);
  auto with_context = aclrt_context_.GetContextGuard();
  ClientPtr client_ptr = client_manager_.GetClient(remote_engine.GetString());
  HIXL_CHK_BOOL_RET_STATUS(client_ptr != nullptr, NOT_CONNECTED,
                           "[HixlEngine] Failed to get client, remote_engine:%s is not connected",
                           remote_engine.GetString());

  HIXL_CHK_STATUS_RET(client_ptr->SendNotify(notify, timeout_in_millis),
                      "[HixlEngine] Failed to SendNotify, local_engine:%s, remote_engine:%s", local_engine_.c_str(),
                      remote_engine.GetString());
  HIXL_LOGI("[HixlEngine] SendNotify succeeded, local_engine:%s, remote_engine:%s", local_engine_.c_str(),
            remote_engine.GetString());
  return SUCCESS;
}

Status HixlEngine::GetNotifies(std::vector<NotifyDesc> &notifies) {
  HIXL_LOGI("[HixlEngine] GetNotifies started, local_engine:%s", local_engine_.c_str());
  auto with_context = aclrt_context_.GetContextGuard();
  HIXL_CHK_STATUS_RET(server_.GetNotifies(notifies), "[HixlEngine] Failed to get notifies from server, local_engine:%s",
                      local_engine_.c_str());
  HIXL_LOGI("[HixlEngine] GetNotifies succeeded, local_engine:%s, count:%zu", local_engine_.c_str(), notifies.size());
  return SUCCESS;
}

Status HixlEngine::RegisterCallbackProcessor(int32_t msg_type, CallbackProcessor processor) {
  auto with_context = aclrt_context_.GetContextGuard();
  HIXL_CHK_STATUS_RET(server_.RegisterCallbackProcessor(msg_type, processor),
                      "[HixlEngine] Failed to register msg callback, msg type:%d", msg_type);
  return SUCCESS;
}

Status HixlEngine::AutoConnect(const AscendString &remote_engine, int32_t timeout_in_millis) {
  if (!auto_connect_) {
    return SUCCESS;
  }
  HIXL_CHK_BOOL_RET_STATUS(strcmp(local_engine_.c_str(), remote_engine.GetString()) != 0, PARAM_INVALID,
                           "[HixlEngine] Do not support connection with self, please check remote engine. "
                           "local_engine:%s, remote_engine:%s",
                           local_engine_.c_str(), remote_engine.GetString());
  // 快速路径：无锁检查（client_manager_.GetClient 内部有锁）
  ClientPtr client_ptr = client_manager_.GetClient(remote_engine.GetString());
  if (client_ptr != nullptr) {
    return SUCCESS;
  }

  ClientConfig config{};
  std::vector<MemHandleInfo> mem_info_list;
  BuildClientConfig(remote_engine, config, mem_info_list, timeout_in_millis);

  HIXL_LOGI("[HixlEngine] Auto connect started, local_engine:%s, remote_engine:%s, timeout:%d ms.",
            local_engine_.c_str(), remote_engine.GetString(), timeout_in_millis);
  auto with_context = aclrt_context_.GetContextGuard();
  Status ret = client_manager_.GetOrCreateClient(config, mem_info_list, client_ptr);
  if (ret == ALREADY_CONNECTED) {
    return SUCCESS;
  }
  HIXL_CHK_STATUS_RET(ret, "[HixlEngine] Failed to auto connect, local_engine:%s, remote_engine:%s, timeout:%d ms",
                      local_engine_.c_str(), remote_engine.GetString(), timeout_in_millis);
  HIXL_CHK_STATUS_RET(
      client_ptr->Connect(timeout_in_millis),
      "[HixlEngine] Failed to connect client in auto connect, local_engine:%s, remote_engine:%s, timeout:%d ms",
      local_engine_.c_str(), remote_engine.GetString(), timeout_in_millis);
  return SUCCESS;
}

void HixlEngine::BuildClientConfig(const AscendString &remote_engine, ClientConfig &config,
                                   std::vector<MemHandleInfo> &mem_info_list, int32_t timeout_in_millis) {
  config.endpoint_list = endpoint_list_;
  config.remote_engine = remote_engine.GetString();
  config.rdma_tc = rdma_traffic_class_;
  config.rdma_sl = rdma_service_level_;
  config.timeout_ms = static_cast<uint32_t>(timeout_in_millis);
  config.qos = qos_;
  config.is_lazy = auto_connect_;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto &pair : mem_map_) {
      mem_info_list.push_back(pair.second);
    }
  }
}

Status HixlEngine::AutoDisconnect(const AscendString &remote_engine, int32_t timeout_in_millis) {
  if (auto_connect_) {
    HIXL_CHK_STATUS_RET(Disconnect(remote_engine, timeout_in_millis),
                        "[HixlEngine] Failed to disconnect on error, remote_engine:%s, timeout:%d ms",
                        remote_engine.GetString(), timeout_in_millis);
  }
  return SUCCESS;
}
}  // namespace hixl
