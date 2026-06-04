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

const std::unordered_set<std::string> HixlEngine::kSupportedOptions = {
    OPTION_RDMA_TRAFFIC_CLASS, adxl::OPTION_RDMA_TRAFFIC_CLASS,
    OPTION_RDMA_SERVICE_LEVEL, adxl::OPTION_RDMA_SERVICE_LEVEL,
    OPTION_LOCAL_COMM_RES, adxl::OPTION_LOCAL_COMM_RES,
    OPTION_BUFFER_POOL, adxl::OPTION_BUFFER_POOL,
    OPTION_GLOBAL_RESOURCE_CONFIG};

bool HixlEngine::IsInitialized() const {
  return is_initialized_.load(std::memory_order::memory_order_relaxed);
}

Status HixlEngine::InitServer() {
  std::string ip;
  int32_t port = 0;
  HIXL_CHK_STATUS_RET(ParseListenInfo(local_engine_, ip, port),
                      "[HixlEngine] Failed to parse ip and port, local_engine should be in form as below: "
                      "ipv4 should be 'host_ip:host_port' or 'host_ip' "
                      "ipv6 should be '[host_ip]:host_port' or '[host_ip]' "
                      "current local_engine:%s",
                      local_engine_.c_str());
  HIXL_CHK_STATUS_RET(server_.Initialize(ip, port, endpoint_list_),
                      "[HixlEngine] Failed to initialize HixlEngine, local_engine:%s",
                      local_engine_.c_str());
  return SUCCESS;
}

Status HixlEngine::Initialize(const HixlOptions &options) {
  HIXL_LOGI("[HixlEngine] Initialization started, local_engine:%s", local_engine_.c_str());
  std::lock_guard<std::mutex> lock(mutex_);
  HIXL_CHK_STATUS_RET(options.CheckSupportedOptions(kSupportedOptions),
                      "[HixlEngine] Unsupported option");
  const auto &raw = options.RawOptions();
  const auto &hixl_bp_it = raw.find(hixl::OPTION_BUFFER_POOL);
  const auto &adxl_bp_it = raw.find(adxl::OPTION_BUFFER_POOL);
  auto bp_it = (hixl_bp_it != raw.cend()) ? hixl_bp_it : adxl_bp_it;
  if (bp_it != raw.cend()) {
    HIXL_CHK_BOOL_RET_STATUS(std::string(bp_it->second.GetString()) == "0:0", PARAM_INVALID,
                             "Invalid option fields, OPTION_BUFFER_POOL for hixl engine only supports 0:0");
  }
  std::string local_comm_res;
  Status ret = EndpointGenerator::BuildEndpointList(
      options, local_engine_, local_comm_res, endpoint_list_);
  HIXL_CHK_STATUS_RET(ret, "[HixlEngine] Failed to build endpoint list from options");
  HIXL_CHK_STATUS_RET(InitServer(),
                      "[HixlEngine] Failed to initialize server, local_engine:%s, local_comm_res:%s",
                      local_engine_.c_str(), local_comm_res.c_str());
  rdma_traffic_class_ = options.RdmaTrafficClass().value_or(kRdmaTrafficClass);
  rdma_service_level_ = options.RdmaServiceLevel().value_or(kRdmaServiceLevel);
  is_initialized_ = true;
  HIXL_LOGI("[HixlEngine] Initialization succeeded, local_engine:%s", local_engine_.c_str());
  return SUCCESS;
}

Status HixlEngine::RegisterMem(const MemDesc &mem, MemType type, MemHandle &mem_handle) {
  HIXL_LOGI("[HixlEngine] Registration started, type:%s, addr:%p, size:%lu", MemTypeToString(type).c_str(),
            reinterpret_cast<void *>(mem.addr), mem.len);
  HIXL_CHK_STATUS_RET(server_.RegisterMem(mem, type, mem_handle),
                      "[HixlEngine] Failed to register mem, type:%s, addr:%p, size:%lu", MemTypeToString(type).c_str(),
                      reinterpret_cast<void *>(mem.addr), mem.len);
  MemInfo mem_info = {mem_handle, mem, type};
  std::lock_guard<std::mutex> lock(mutex_);
  mem_map_.emplace(mem_handle, mem_info);
  HIXL_LOGI("[HixlEngine] Registration succeeded, type:%s, addr:%p, size:%lu", MemTypeToString(type).c_str(),
            reinterpret_cast<void *>(mem.addr), mem.len);
  return SUCCESS;
}

Status HixlEngine::DeregisterMem(MemHandle mem_handle) {
  HIXL_LOGI("[HixlEngine] Deregistration started, mem_handle: %p", mem_handle);
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
  ClientPtr client_ptr = nullptr;
  client_ptr = client_manager_.GetClient(remote_engine.GetString());
  if (client_ptr != nullptr) {
    HIXL_LOGE(ALREADY_CONNECTED, "[HixlEngine] remote_engine:%s is already connected to local_engine:%s",
              remote_engine.GetString(), local_engine_.c_str());
    return ALREADY_CONNECTED;
  }
  ClientConfig config{};
  config.endpoint_list = endpoint_list_;
  config.remote_engine = remote_engine.GetString();
  config.rdma_tc = rdma_traffic_class_;
  config.rdma_sl = rdma_service_level_;
  config.timeout_ms = static_cast<uint32_t>(timeout_in_millis);
  HIXL_CHK_STATUS_RET(client_manager_.CreateClient(config, client_ptr),
                      "[HixlEngine] Failed to create HixlClient, local_engine: %s, remote_engine: %s",
                      local_engine_.c_str(), remote_engine.GetString());
  HIXL_CHECK_NOTNULL(
      client_ptr,
      "[HixlEngine] Created client is null, please check your parameters! local_engine:%s, remote_engine:%s",
      local_engine_.c_str(), remote_engine.GetString());
  std::vector<MemInfo> mem_info_list;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto &pair : mem_map_) {
      mem_info_list.push_back(pair.second);
    }
  }
  HIXL_DISMISSABLE_GUARD(rollback,
                         ([this, &remote_engine]() { client_manager_.DestroyClient(remote_engine.GetString()); }));
  HIXL_CHK_STATUS_RET(client_ptr->SetLocalMemInfo(mem_info_list),
                      "[HixlEngine] Failed to set local memory info, local_engine:%s", local_engine_.c_str());
  HIXL_CHK_STATUS_RET(client_ptr->Connect(timeout_in_millis),
                      "[HixlEngine] Failed to connect, local_engine:%s, remote_engine:%s, timeout:%d ms",
                      local_engine_.c_str(), remote_engine.GetString(), timeout_in_millis);
  HIXL_DISMISS_GUARD(rollback);
  HIXL_LOGI("[HixlEngine] Connection succeeded, local_engine:%s, remote_engine:%s", local_engine_.c_str(),
            remote_engine.GetString());
  return SUCCESS;
}

Status HixlEngine::Disconnect(const AscendString &remote_engine, int32_t timeout_in_millis) {
  HIXL_LOGI("[HixlEngine] Disconnection started, local_engine:%s, remote_engine:%s, timeout:%d ms",
            local_engine_.c_str(), remote_engine.GetString(), timeout_in_millis);
  HIXL_CHK_STATUS_RET(client_manager_.DestroyClient(remote_engine.GetString()),
                      "[HixlEngine] Failed to disconnect, local_engine:%s, remote_engine:%s, timeout:%d ms",
                      local_engine_.c_str(), remote_engine.GetString(), timeout_in_millis);
  HIXL_LOGI("[HixlEngine] Disconnection succeeded, local_engine:%s, remote_engine:%s, timeout:%d ms",
            local_engine_.c_str(), remote_engine.GetString(), timeout_in_millis);
  return SUCCESS;
}

void HixlEngine::Disconnect() {
  HIXL_LOGI("[HixlEngine] Disconnection with all clients started, local_engine:%s", local_engine_.c_str());
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
  ClientPtr client_ptr = client_manager_.GetClient(remote_engine.GetString());
  HIXL_CHK_BOOL_RET_STATUS(client_ptr != nullptr, NOT_CONNECTED,
                           "[HixlEngine] Failed to get client through remote engine, please check connection. "
                           "local_engine:%s, remote_engine:%s",
                           local_engine_.c_str(), remote_engine.GetString());
  HixlProfType type = (operation == READ ? HixlProfType::HixlOpBatchRead : HixlProfType::HixlOpBatchWrite);
  HIXL_API_PROFILING(type);
  HIXL_CHK_STATUS_RET(client_ptr->TransferSync(op_descs, operation, timeout_in_millis),
                      "[HixlEngine] Failed to TransferSync, local_engine:%s, remote_engine:%s, timeout:%d ms",
                      local_engine_.c_str(), remote_engine.GetString(), timeout_in_millis);
  HIXL_LOGI("[HixlEngine] Synchronous transmission succeeded, local_engine:%s, remote_engine:%s, timeout:%d ms",
            local_engine_.c_str(), remote_engine.GetString(), timeout_in_millis);
  return SUCCESS;
}

Status HixlEngine::TransferAsync(const AscendString &remote_engine, TransferOp operation,
                                 const std::vector<TransferOpDesc> &op_descs, const TransferArgs &optional_args,
                                 TransferReq &req) {
  HIXL_LOGI("[HixlEngine] Asynchronous transmission started, local_engine:%s, remote_engine:%s", local_engine_.c_str(),
            remote_engine.GetString());
  ClientPtr client_ptr = client_manager_.GetClient(remote_engine.GetString());
  HIXL_CHK_BOOL_RET_STATUS(
      client_ptr != nullptr, NOT_CONNECTED,
      "[HixlEngine] Failed to get client through remote engine, please check connection. remote_engine:%s",
      remote_engine.GetString());
  HIXL_CHK_STATUS_RET(client_ptr->TransferAsync(op_descs, operation, req),
                      "[HixlEngine] Failed to TransferAsync, local_engine:%s, remote_engine:%s", local_engine_.c_str(),
                      remote_engine.GetString());
  auto id = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(req));
  uint64_t start_time = 0;
  start_time = HixlProfilingReporter::GetSysCycleTime();
  TransferInfo transfer_info = {start_time, operation, remote_engine, optional_args.user_data};
  std::lock_guard<std::mutex> lock(req_map_mutex_);
  req_map_.emplace(id, transfer_info);
  HIXL_LOGI("[HixlEngine] Asynchronous transmission succeeded, local_engine:%s, remote_engine:%s",
            local_engine_.c_str(), remote_engine.GetString());
  return SUCCESS;
}

Status HixlEngine::GetTransferStatusInner(const TransferReq &req, TransferStatus &status) {
  auto id = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(req));
  auto it = req_map_.find(id);
  if (it == req_map_.cend()) {
    status = TransferStatus::FAILED;
    HIXL_LOGE(PARAM_INVALID, "[HixlEngine] Request not found, request has been completed or does not exist, req: %p",
              req);
    return PARAM_INVALID;
  }
  auto remote_engine = it->second.remote_engine;
  auto client = client_manager_.GetClient(remote_engine.GetString());
  HIXL_CHECK_NOTNULL(client,
                     "[HixlEngine] Failed to get client through remote engine, local_engine:%s, remote_engine:%s",
                     local_engine_.c_str(), remote_engine.GetString());
  HIXL_CHK_STATUS_RET(client->GetTransferStatus(req, status), 
                      "[HixlEngine] Failed to get status through client, req:%p, status:%d", 
                      req, static_cast<int>(status));
  if (status == TransferStatus::COMPLETED) {
    auto op_type = it->second.op_type;
    auto start_time = it->second.start_time;
    HixlProfType type = (op_type == READ ? HixlProfType::HixlOpBatchRead : HixlProfType::HixlOpBatchWrite);
    HIXL_API_PROFILING_WITH_TIME(type, start_time);
    req_map_.erase(it);
  }
  return SUCCESS;
}

Status HixlEngine::GetTransferStatus(const TransferReq &req, TransferStatus &status) {
  std::lock_guard<std::mutex> lock(req_map_mutex_);
  return GetTransferStatusInner(req, status);
}

Status HixlEngine::GetTransferStatus(const GetTransferStatusArgs &args, std::vector<TransferResult> &results) {
  std::vector<std::pair<TransferReq, void *>> reqs;

  std::lock_guard<std::mutex> lock(req_map_mutex_);
  reqs.reserve(req_map_.size());
  for (const auto &it : req_map_) {
    TransferReq req = reinterpret_cast<TransferReq>(static_cast<uintptr_t>(it.first));
    reqs.emplace_back(req, it.second.user_data);
  }

  results.clear();
  results.reserve(std::min(reqs.size(), static_cast<size_t>(args.max_query_count)));
  for (const auto &it : reqs) {
    TransferStatus status;
    Status ret = GetTransferStatusInner(it.first, status);
    status = (ret == SUCCESS) ? status : TransferStatus::FAILED;
    if (args.skip_waiting && status == TransferStatus::WAITING) {
      continue;
    }
    results.emplace_back(TransferResult{it.first, it.second, status});
    if (results.size() >= results.capacity()) {
      break;
    }
  }
  return SUCCESS;
}

void HixlEngine::Finalize() {
  HIXL_LOGI("[HixlEngine] Finalization started");
  std::lock_guard<std::mutex> lock(mutex_);
  server_.Finalize();
  client_manager_.Finalize();
  mem_map_.clear();
  std::lock_guard<std::mutex> req_map_lock(req_map_mutex_);
  req_map_.clear();
  is_initialized_ = false;
  HIXL_LOGI("[HixlEngine] Finalization succeeded");
}

Status HixlEngine::SendNotify(const AscendString &remote_engine, const NotifyDesc &notify, int32_t timeout_in_millis) {
  (void)remote_engine;
  (void)notify;
  (void)timeout_in_millis;
  HIXL_LOGE(UNSUPPORTED, "[HixlEngine] Method SendNotify is not supported by HixlEngine yet");
  return UNSUPPORTED;
}

Status HixlEngine::GetNotifies(std::vector<NotifyDesc> &notifies) {
  (void)notifies;
  HIXL_LOGE(UNSUPPORTED, "[HixlEngine] Method GetNotifies is not supported by HixlEngine yet");
  return UNSUPPORTED;
}

Status HixlEngine::RegisterCallbackProcessor(int32_t msg_type, CallbackProcessor processor) {
  HIXL_CHK_STATUS_RET(server_.RegisterCallbackProcessor(msg_type, processor),
                      "[HixlEngine] Failed to register msg callback, msg type:%d", msg_type);
  return SUCCESS;
}

}  // namespace hixl
