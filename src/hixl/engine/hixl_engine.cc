/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <set>
#include <regex>
#include "nlohmann/json.hpp"
#include "hixl_engine.h"
#include "common/hixl_log.h"
#include "common/hixl_checker.h"
#include "common/hixl_utils.h"
#include "common/llm_utils.h"
#include "common/scope_guard.h"
#include "adxl/adxl_types.h"

namespace hixl {

void from_json(const nlohmann::json &j, hixl::EndpointConfig &ep) {
  j.at("protocol").get_to(ep.protocol);
  j.at("comm_id").get_to(ep.comm_id);
  j.at("placement").get_to(ep.placement);
  if (j.contains("plane")) {
    j.at("plane").get_to(ep.plane);
  }
  if (j.contains("dst_eid")) {
    j.at("dst_eid").get_to(ep.dst_eid);
  }
  if (j.contains("_net_instance_id")) {
    j.at("_net_instance_id").get_to(ep.net_instance_id);
  }
}

bool HixlEngine::IsInitialized() const {
  return is_initialized_.load(std::memory_order::memory_order_relaxed);
}

Status HixlEngine::Initialize(const std::map<AscendString, AscendString> &options) {
  HIXL_LOGI("[HixlEngine] Initialization started, local_engine:%s", local_engine_.c_str());
  std::lock_guard<std::mutex> lock(mutex_);
  std::string local_comm_res;
  auto it = options.find(adxl::OPTION_LOCAL_COMM_RES);
  if (it != options.cend()) {
    local_comm_res = it->second.GetString();
  }
  HIXL_CHK_STATUS_RET(ParseEndPoint(local_comm_res, endpoint_list_), 
                      "[HixlEngine] Failed to parse endpoint from local_comm_res, local_comm_res:%s",
                      local_comm_res.c_str());
  std::string ip;
  int32_t port = 0;
  HIXL_CHK_STATUS_RET(ParseListenInfo(local_engine_, ip, port), 
                      "[HixlEngine] Failed to parse ip and port, local_engine should be in form as below:\n"
                      "ipv4: host_ip:host_port or host_ip\n"
                      "ipv6: [host_ip]:host_port or [host_ip]\n"
                      "current local_engine:%s",
                      local_engine_.c_str());
  HIXL_CHK_STATUS_RET(server_.Initialize(ip, port, endpoint_list_), 
                      "[HixlEngine] Failed to initialize HixlEngine, local_engine:%s, local_comm_res:%s", 
                      local_engine_.c_str(), local_comm_res.c_str());
  is_initialized_ = true;
  HIXL_LOGI("[HixlEngine] Initialization succeeded, local_engine:%s", local_engine_.c_str());
  return SUCCESS;
}

Status HixlEngine::RegisterMem(const MemDesc &mem, MemType type, MemHandle &mem_handle) {
  HIXL_LOGI("[HixlEngine] Registration started, type:%s, addr:%p, size:%lu", 
            MemTypeToString(type).c_str(), reinterpret_cast<void *>(mem.addr), mem.len);
  HIXL_CHK_STATUS_RET(server_.RegisterMem(mem, type, mem_handle), 
                      "[HixlEngine] Failed to register mem, type:%s, addr:%p, size:%lu",
                      MemTypeToString(type).c_str(), reinterpret_cast<void *>(mem.addr), mem.len);
  MemInfo mem_info = {mem_handle, mem, type};
  std::lock_guard<std::mutex> lock(mutex_);
  mem_map_.emplace(mem_handle, mem_info);
  HIXL_LOGI("[HixlEngine] Registration succeeded, type:%s, addr:%p, size:%lu", 
            MemTypeToString(type).c_str(), reinterpret_cast<void *>(mem.addr), mem.len);
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
  HIXL_CHK_STATUS_RET(server_.DeregisterMem(mem_handle), 
                      "[HixlEngine] Failed to deregister mem, mem_handle: %p, local_engine: %s", 
                      mem_handle, local_engine_.c_str());
  mem_map_.erase(it);
  HIXL_LOGI("[HixlEngine] Deregistration succeeded, mem_handle: %p", mem_handle);
  return SUCCESS;
}

Status HixlEngine::Connect(const AscendString &remote_engine, int32_t timeout_in_millis) {
  HIXL_LOGI("[HixlEngine] Connection started, local_engine:%s, remote_engine:%s", 
            local_engine_.c_str(), remote_engine.GetString());
  ClientPtr client_ptr = nullptr;
  client_ptr = client_manager_.GetClient(remote_engine.GetString());
  if (client_ptr != nullptr) {
    HIXL_LOGE(ALREADY_CONNECTED, 
              "[HixlEngine] remote engine:%s is already connected to local_engine:%s", 
              remote_engine.GetString(), local_engine_.c_str());
    return ALREADY_CONNECTED;
  }
  HIXL_CHK_STATUS_RET(client_manager_.CreateClient(endpoint_list_, remote_engine.GetString(), client_ptr),
                      "[HixlEngine] Failed to create HixlClient, local_engine: %s, remote engine: %s",
                      local_engine_.c_str(), remote_engine.GetString());
  HIXL_CHECK_NOTNULL(client_ptr, 
                     "[HixlEngine] Created client is null, please check your parameters! local_engine:%s, remote_engine:%s",
                     local_engine_.c_str(), remote_engine.GetString());
  std::vector<MemInfo> mem_info_list;
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto &pair : mem_map_) {
    mem_info_list.push_back(pair.second);
  }
  HIXL_DISMISSABLE_GUARD(rollback, ([this, &remote_engine]() { client_manager_.DestroyClient(remote_engine.GetString()); }));
  HIXL_CHK_STATUS_RET(client_ptr->SetLocalMemInfo(mem_info_list), 
                      "[HixlEngine] Failed to set local memory info, local_engine:%s",
                      local_engine_.c_str());
  HIXL_CHK_STATUS_RET(client_ptr->Connect(timeout_in_millis), 
                      "[HixlEngine] Failed to connect, local_engine:%s, remote_engine:%s, timeout:%d ms",
                      local_engine_.c_str(), remote_engine.GetString(), timeout_in_millis);
  HIXL_DISMISS_GUARD(rollback);
  HIXL_LOGI("[HixlEngine] Connection succeeded, local_engine:%s, remote_engine:%s", 
            local_engine_.c_str(), remote_engine.GetString());
  return SUCCESS;
}

Status HixlEngine::Disconnect(const AscendString &remote_engine, int32_t timeout_in_millis) {
  HIXL_LOGI("[HixlEngine] Disconnection started, local_engine:%s, remote_engine:%s, timeout:%d ms",
            local_engine_.c_str(), remote_engine.GetString());
  (void)timeout_in_millis;
  HIXL_CHK_STATUS_RET(client_manager_.DestroyClient(remote_engine.GetString()),
                      "[HixlEngine] Failed to disconnect, local_engine:%s, remote_engine:%s, timeout:%d ms", 
                      local_engine_.c_str(), remote_engine.GetString(), timeout_in_millis);
  HIXL_LOGI("[HixlEngine] Disconnection succeeded, local_engine:%s, remote_engine:%s, timeout:%d ms",
            local_engine_.c_str(), remote_engine.GetString());
  return SUCCESS;
}

void HixlEngine::Disconnect() {
  HIXL_LOGI("[HixlEngine] Disconnection with all clients started, local_engine:%s",
              local_engine_.c_str());
  Status ret = client_manager_.Finalize();
  if (ret != SUCCESS) {
    HIXL_LOGE(FAILED, "[HixlEngine] Failed to disconnect with all clients, local_engine:%s", 
              local_engine_.c_str());
  } else {
    HIXL_LOGI("[HixlEngine] Disconnection with all clients succeeded, local_engine:%s",
              local_engine_.c_str());
  }
}

Status HixlEngine::TransferSync(const AscendString &remote_engine, TransferOp operation,
                                const std::vector<TransferOpDesc> &op_descs, int32_t timeout_in_millis) {
  HIXL_LOGI("[HixlEngine] Synchronous transmission started, local_engine:%s, remote_engine:%s, timeout:%d ms",
            local_engine_.c_str(), remote_engine.GetString(), timeout_in_millis);
  ClientPtr client_ptr = client_manager_.GetClient(remote_engine.GetString());
  HIXL_CHECK_NOTNULL(client_ptr, 
                     "[HixlEngine] Failed to get client through remote engine, local_engine:%s, remote_engine:%s",
                     local_engine_.c_str(), remote_engine.GetString());
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
  HIXL_LOGI("[HixlEngine] Asynchronous transmission strated, local_engine:%s, remote_engine:%s",
            local_engine_.c_str(), remote_engine.GetString());
  (void)optional_args;
  ClientPtr client_ptr = client_manager_.GetClient(remote_engine.GetString());
  HIXL_CHECK_NOTNULL(client_ptr, 
                     "[HixlEngine] Failed to get client through remote engine, remote_engine:%s",
                     remote_engine.GetString());
  HIXL_CHK_STATUS_RET(client_ptr->TransferAsync(op_descs, operation, req), 
                      "[HixlEngine] Failed to TransferAsync, local_engine:%s, remote_engine:%s",
                      local_engine_.c_str(), remote_engine.GetString());
  auto id = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(req));
  std::lock_guard<std::mutex> lock(mutex_);
  req2client_.emplace(id, remote_engine);
  HIXL_LOGI("[HixlEngine] Asynchronous transmission succeeded, local_engine:%s, remote_engine:%s",
            local_engine_.c_str(), remote_engine.GetString());
  return SUCCESS;
}

Status HixlEngine::GetTransferStatus(const TransferReq &req, TransferStatus &status) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto id = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(req));
  auto it = req2client_.find(id);
  if (it == req2client_.cend()) {
    status = TransferStatus::FAILED;
    HIXL_LOGE(PARAM_INVALID, "[HixlEngine] Request not found, request has been completed or does not exist, req: %p", req);
    return PARAM_INVALID;
  }
  auto remote_engine = it->second;
  auto client = client_manager_.GetClient(remote_engine.GetString());
  HIXL_CHECK_NOTNULL(client, 
                     "[HixlEngine] Failed to get client through remote engine, local_engine:%s, remote_engine:%s", 
                     local_engine_.c_str(), remote_engine.GetString());
  HIXL_CHK_STATUS_RET(client->GetTransferStatus(req, status), 
                      "[HixlEngine] Failed to get status through client, req:%p, status:%d", 
                      req, static_cast<int>(status));
  if (status != TransferStatus::WAITING) {
    req2client_.erase(it);
  }
  return SUCCESS;
}

void HixlEngine::Finalize() {
  HIXL_LOGI("[HixlEngine] Finalization started");
  std::lock_guard<std::mutex> lock(mutex_);
  server_.Finalize();
  client_manager_.Finalize();
  mem_map_.clear();
  req2client_.clear();
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
                      "[HixlEngine] Failed to register msg callback, msg type:%d", 
                      msg_type);
  return SUCCESS;
}

Status HixlEngine::ParseEndPoint(const std::string &local_comm_res, std::vector<EndpointConfig> &endpoint_list) {
  try {
    auto config = nlohmann::json::parse(local_comm_res);
    std::string net_id = config["net_instance_id"];
    for (auto &item : config["endpoint_list"]) {
      item["_net_instance_id"] = net_id;
    }
    config["endpoint_list"].get_to(endpoint_list);
    HIXL_CHK_BOOL_RET_STATUS(!endpoint_list.empty(), PARAM_INVALID, 
                             "[HixlEngine] endpoint_list is empty, please check options, local_comm_res:%s", 
                             local_comm_res.c_str());
  } catch (const nlohmann::json::exception &e) {
    HIXL_LOGE(PARAM_INVALID, 
              "[HixlEngine] Failed to parse local_comm_res, exception:%s, local_comm_res:%s",
              e.what(), local_comm_res);
    return PARAM_INVALID;
  }
  return SUCCESS;
}
}  // namespace hixl