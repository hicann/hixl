/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "fabric_mem/fabric_mem_control.h"

#include <poll.h>
#include <utility>
#include <unistd.h>

#include "common/ctrl_msg_plugin.h"
#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/hixl_utils.h"
#include "common/scope_guard.h"
#include "nlohmann/json.hpp"
#include "securec.h"

namespace hixl {
namespace {

constexpr uint32_t kBacklog = 256U;
constexpr int32_t kPollTimeoutMs = 100;
constexpr int32_t kRecvTimeoutMultiplier = 10;
constexpr size_t kShareHandleDataSize = sizeof(aclrtMemFabricHandle{}.data);
constexpr int32_t kShareHandleArrayCheckErrCode = 401;
constexpr uint32_t kFabricMemMagic = 0xA4B3C2D1;
constexpr size_t kMaxMsgLen = 1ULL << 20;

void CheckShareHandleArray(const nlohmann::json &share_array) {
  if (!share_array.is_array() || share_array.size() != kShareHandleDataSize) {
    throw nlohmann::json::out_of_range::create(kShareHandleArrayCheckErrCode,
                                               "share_handle size must be " + std::to_string(kShareHandleDataSize),
                                               &share_array);
  }
}

void to_json(nlohmann::json &j, const ShareHandleInfo &info) {
  auto share_array = nlohmann::json::array();
  for (size_t i = 0; i < kShareHandleDataSize; ++i) {
    share_array.push_back(info.share_handle.data[i]);
  }
  j = nlohmann::json{{"va_addr", info.va_addr}, {"len", info.len}, {"share_handle", share_array}};
}

void from_json(const nlohmann::json &j, ShareHandleInfo &info) {
  j.at("va_addr").get_to(info.va_addr);
  j.at("len").get_to(info.len);
  const auto &share_array = j.at("share_handle");
  CheckShareHandleArray(share_array);
  for (size_t i = 0; i < kShareHandleDataSize; ++i) {
    info.share_handle.data[i] = share_array.at(i).get<uint8_t>();
  }
}

// Wire format compatible with MsgHandlerPlugin (old protocol):
// | magic (4B) | length (8B) | type (4B) | payload (N B) |
// where length = sizeof(type) + payload.size().
Status SendFabricMemMsg(int32_t fd, int32_t msg_type, const std::string &payload) {
  const uint64_t length = static_cast<uint64_t>(sizeof(msg_type)) + payload.size();
  HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Send(fd, &kFabricMemMagic, sizeof(kFabricMemMagic)),
                      "Send fabric mem magic failed.");
  HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Send(fd, &length, sizeof(length)), "Send fabric mem length failed.");
  HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Send(fd, &msg_type, sizeof(msg_type)), "Send fabric mem msg type failed.");
  if (!payload.empty()) {
    HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Send(fd, payload.data(), payload.size()),
                        "Send fabric mem msg body failed.");
  }
  return SUCCESS;
}

Status RecvFabricMemMsg(int32_t fd, int32_t timeout_ms, int32_t &msg_type, std::string &payload) {
  uint32_t magic = 0U;
  HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Recv(fd, &magic, sizeof(magic), static_cast<uint32_t>(timeout_ms)),
                      "Recv fabric mem magic failed.");
  HIXL_CHK_BOOL_RET_STATUS(magic == kFabricMemMagic, PARAM_INVALID, "Invalid fabric mem magic:0x%x.", magic);

  uint64_t length = 0ULL;
  HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Recv(fd, &length, sizeof(length), static_cast<uint32_t>(timeout_ms)),
                      "Recv fabric mem length failed.");
  HIXL_CHK_BOOL_RET_STATUS(length <= kMaxMsgLen && length >= sizeof(msg_type), PARAM_INVALID,
                           "Invalid fabric mem msg length:%lu.", length);

  HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Recv(fd, &msg_type, sizeof(msg_type), static_cast<uint32_t>(timeout_ms)),
                      "Recv fabric mem msg type failed.");

  const size_t data_len = static_cast<size_t>(length) - sizeof(msg_type);
  payload.resize(data_len);
  if (data_len > 0U) {
    HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Recv(fd, payload.data(), data_len, static_cast<uint32_t>(timeout_ms)),
                        "Recv fabric mem msg body failed.");
  }
  return SUCCESS;
}

// Common helper: parse remote engine address, initialize plugin, and establish TCP connection.
// Returns the connected fd via conn_fd; caller is responsible for closing it.
Status ConnectToEngine(const std::string &remote_engine, int32_t timeout_ms, int32_t &conn_fd) {
  std::string remote_ip;
  int32_t remote_port = -1;
  HIXL_CHK_STATUS_RET(ParseListenInfo(remote_engine, remote_ip, remote_port),
                      "Parse remote fabric mem engine failed.");
  HIXL_CHK_BOOL_RET_STATUS(remote_port > 0, PARAM_INVALID,
                           "Remote fabric mem engine port must be greater than zero.");
  CtrlMsgPlugin::Initialize();
  HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Connect(remote_ip, static_cast<uint32_t>(remote_port), conn_fd, timeout_ms),
                      "Connect remote fabric mem engine failed:%s.", remote_engine.c_str());
  return SUCCESS;
}

Status ParseShareHandlesFromResponse(const std::string &payload, std::vector<ShareHandleInfo> &share_handles) {
  nlohmann::json json;
  try {
    json = nlohmann::json::parse(payload);
  } catch (const nlohmann::json::exception &e) {
    HIXL_LOGE(PARAM_INVALID, "Parse connect response failed:%s", e.what());
    return PARAM_INVALID;
  }
  share_handles.clear();
  if (!json.contains("share_handles") || !json["share_handles"].is_array()) {
    return SUCCESS;
  }
  for (const auto &item : json["share_handles"]) {
    ShareHandleInfo info{};
    from_json(item, info);
    share_handles.emplace_back(std::move(info));
  }
  return SUCCESS;
}
}  // namespace

FabricMemControlServer::~FabricMemControlServer() {
  Stop();
}

Status FabricMemControlServer::Start(const std::string &listen_info, FabricMemShareHandleProvider provider) {
  HIXL_CHK_BOOL_RET_STATUS(static_cast<bool>(provider), PARAM_INVALID,
                           "FabricMemControlServer provider cannot be empty.");
  std::string ip;
  int32_t port = 0;
  HIXL_CHK_STATUS_RET(ParseListenInfo(listen_info, ip, port), "Parse fabric mem listen info failed:%s",
                      listen_info.c_str());
  auto state = state_;
  if (port <= 0) {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->provider = std::move(provider);
    HIXL_LOGI("FabricMemControlServer does not listen because port:%d.", port);
    return SUCCESS;
  }
  std::lock_guard<std::mutex> lock(state->mutex);
  if (state->running.load(std::memory_order_acquire)) {
    return SUCCESS;
  }
  CtrlMsgPlugin::Initialize();
  HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Listen(ip, static_cast<uint32_t>(port), kBacklog, state->listen_fd),
                      "Fabric mem control listen failed, ip:%s, port:%d.", ip.c_str(), port);
  state->provider = std::move(provider);
  state->running.store(true, std::memory_order_release);
  worker_ = std::thread(&FabricMemControlServer::Run, state);
  HIXL_EVENT("FabricMemControlServer started, listen:%s:%d.", ip.c_str(), port);
  return SUCCESS;
}

void FabricMemControlServer::Stop() {
  auto state = state_;
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    if (!state->running.load(std::memory_order_acquire) && state->listen_fd < 0) {
      return;
    }
    state->running.store(false, std::memory_order_release);
    if (state->listen_fd >= 0) {
      (void)close(state->listen_fd);
      state->listen_fd = -1;
    }
    for (const int32_t fd : state->keepalive_fds) {
      (void)close(fd);
    }
    state->keepalive_fds.clear();
  }
  if (worker_.joinable()) {
    worker_.join();
  }
}

void FabricMemControlServer::Run(std::shared_ptr<State> state) {
  while (state->running.load(std::memory_order_acquire)) {
    int32_t listen_fd = -1;
    {
      std::lock_guard<std::mutex> lock(state->mutex);
      listen_fd = state->listen_fd;
    }
    if (listen_fd < 0) {
      break;
    }
    pollfd pfd{};
    pfd.fd = listen_fd;
    pfd.events = POLLIN;
    const int32_t ret = poll(&pfd, 1, kPollTimeoutMs);
    if (ret <= 0) {
      continue;
    }
    int32_t conn_fd = -1;
    if (CtrlMsgPlugin::Accept(listen_fd, conn_fd) != SUCCESS || conn_fd < 0) {
      continue;
    }
    HIXL_MAKE_GUARD(close_conn, ([conn_fd, state]() {
      std::lock_guard<std::mutex> lock(state->mutex);
      if (state->keepalive_fds.count(conn_fd) > 0U) {
        return;
      }
      (void)close(conn_fd);
    }));
    (void)CtrlMsgPlugin::SetTcpNoDelay(conn_fd);
    (void)CtrlMsgPlugin::SetTcpKeepAlive(conn_fd);
    (void)HandleConnection(state, conn_fd);
  }
}

Status FabricMemControlServer::HandleConnection(const std::shared_ptr<State> &state, int32_t fd) {
  int32_t msg_type = 0;
  std::string payload;
  HIXL_CHK_STATUS_RET(RecvFabricMemMsg(fd, kPollTimeoutMs * kRecvTimeoutMultiplier, msg_type, payload),
                      "Recv fabric mem request failed.");
  switch (msg_type) {
    case FabricMemMsgType::kConnect:
      return HandleConnectRequest(state, fd);
    case FabricMemMsgType::kSendNotifyReq:
      return HandleSendNotify(state, payload);
    case FabricMemMsgType::kGetNotifiesReq:
      return HandleGetNotifies(state, fd);
    case FabricMemMsgType::kDisconnect:
      return HandleDisconnectRequest(state, fd);
    default:
      HIXL_LOGE(PARAM_INVALID, "Unexpected fabric mem request type:%d.", msg_type);
      return PARAM_INVALID;
  }
}

Status FabricMemControlServer::HandleConnectRequest(const std::shared_ptr<State> &state, int32_t fd) {
  std::vector<ShareHandleInfo> share_handles;
  FabricMemShareHandleProvider provider;
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    provider = state->provider;
  }
  HIXL_CHK_BOOL_RET_STATUS(static_cast<bool>(provider), FAILED, "FabricMemControlServer provider is empty.");
  Status result = provider(share_handles);

  // Build response with share_handles in ChannelConnectInfo format
  nlohmann::json response;
  response["channel_id"] = "";
  response["comm_res"] = "";
  response["timeout"] = 0;
  response["addrs"] = nlohmann::json::array();
  auto share_array = nlohmann::json::array();
  for (const auto &handle : share_handles) {
    nlohmann::json item;
    to_json(item, handle);
    share_array.push_back(std::move(item));
  }
  response["share_handles"] = std::move(share_array);

  HIXL_CHK_STATUS_RET(SendFabricMemMsg(fd, FabricMemMsgType::kConnect, response.dump()),
                      "Send connect response failed.");

  // Send kStatus
  nlohmann::json status;
  status["error_code"] = static_cast<uint32_t>(result);
  status["error_message"] = "";
  HIXL_CHK_STATUS_RET(SendFabricMemMsg(fd, FabricMemMsgType::kStatus, status.dump()),
                      "Send status response failed.");

  // Register fd for keepalive
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->keepalive_fds.insert(fd);
  }
  HIXL_EVENT("FabricMemControlServer handled connect request, shared %zu handles.", share_handles.size());
  return result;
}

Status FabricMemControlServer::HandleSendNotify(const std::shared_ptr<State> &state, const std::string &payload) {
  try {
    auto json = nlohmann::json::parse(payload);
    NotifyDesc notify;
    notify.name = AscendString(json.at("name").get<std::string>().c_str());
    notify.notify_msg = AscendString(json.at("notify_msg").get<std::string>().c_str());
    std::lock_guard<std::mutex> lock(state->notify_mutex);
    state->notify_queue.emplace_back(std::move(notify));
    return SUCCESS;
  } catch (const nlohmann::json::exception &e) {
    HIXL_LOGE(PARAM_INVALID, "Parse fabric mem notify failed:%s", e.what());
    return PARAM_INVALID;
  }
}

Status FabricMemControlServer::HandleGetNotifies(const std::shared_ptr<State> &state, int32_t fd) {
  std::vector<NotifyDesc> notifies;
  {
    std::lock_guard<std::mutex> lock(state->notify_mutex);
    notifies.swap(state->notify_queue);
  }
  nlohmann::json response;
  response["result"] = SUCCESS;
  auto notify_array = nlohmann::json::array();
  for (const auto &notify : notifies) {
    nlohmann::json item;
    item["name"] = notify.name.GetString();
    item["notify_msg"] = notify.notify_msg.GetString();
    notify_array.push_back(std::move(item));
  }
  response["notifies"] = std::move(notify_array);
  HIXL_CHK_STATUS_RET(SendFabricMemMsg(fd, FabricMemMsgType::kGetNotifiesResp, response.dump()),
                      "Send get notifies response failed.");
  return SUCCESS;
}

Status FabricMemControlServer::DequeueNotifies(std::vector<NotifyDesc> &notifies) {
  std::lock_guard<std::mutex> lock(state_->notify_mutex);
  notifies.swap(state_->notify_queue);
  return SUCCESS;
}

Status FabricMemControlServer::HandleDisconnectRequest(const std::shared_ptr<State> &state, int32_t fd) {
  (void)state;
  HIXL_LOGI("FabricMemControlServer received disconnect request on fd:%d.", fd);

  nlohmann::json status;
  status["error_code"] = static_cast<uint32_t>(SUCCESS);
  status["error_message"] = "";
  HIXL_CHK_STATUS_RET(SendFabricMemMsg(fd, FabricMemMsgType::kStatus, status.dump()),
                      "Send disconnect status failed.");

  {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->keepalive_fds.erase(fd);
  }
  (void)close(fd);
  return SUCCESS;
}

Status FabricMemControlClient::Fetch(const std::string &remote_engine, int32_t timeout_ms,
                                     std::vector<ShareHandleInfo> &share_handles, int32_t &conn_fd) {
  conn_fd = -1;
  HIXL_CHK_STATUS_RET(ConnectToEngine(remote_engine, timeout_ms, conn_fd),
                      "Connect remote fabric mem engine failed:%s.", remote_engine.c_str());
  HIXL_DISMISSABLE_GUARD(close_fd, ([&conn_fd]() {
    if (conn_fd >= 0) {
      (void)close(conn_fd);
      conn_fd = -1;
    }
  }));

  nlohmann::json req;
  req["channel_id"] = "";
  req["comm_res"] = "";
  req["timeout"] = timeout_ms;
  req["addrs"] = nlohmann::json::array();
  req["share_handles"] = nlohmann::json::array();
  Status ret = SendFabricMemMsg(conn_fd, FabricMemMsgType::kConnect, req.dump());
  if (ret != SUCCESS) return ret;

  int32_t msg_type = 0;
  std::string payload;
  ret = RecvFabricMemMsg(conn_fd, timeout_ms, msg_type, payload);
  if (ret != SUCCESS) return ret;
  if (msg_type != FabricMemMsgType::kConnect) return PARAM_INVALID;

  ret = ParseShareHandlesFromResponse(payload, share_handles);
  if (ret != SUCCESS) return ret;

  ret = RecvFabricMemMsg(conn_fd, timeout_ms, msg_type, payload);
  if (ret != SUCCESS) return ret;

  HIXL_DISMISS_GUARD(close_fd);
  HIXL_LOGI("Fetch received %zu share handles from remote:%s, conn_fd:%d.",
            share_handles.size(), remote_engine.c_str(), conn_fd);
  return SUCCESS;
}

Status FabricMemControlClient::SendNotify(const std::string &remote_engine, const NotifyDesc &notify,
                                          int32_t timeout_ms) {
  int32_t conn_fd = -1;
  HIXL_CHK_STATUS_RET(ConnectToEngine(remote_engine, timeout_ms, conn_fd),
                      "Connect remote fabric mem engine failed:%s.", remote_engine.c_str());
  HIXL_MAKE_GUARD(close_fd, ([conn_fd]() { (void)close(conn_fd); }));
  nlohmann::json payload;
  payload["name"] = notify.name.GetString();
  payload["notify_msg"] = notify.notify_msg.GetString();
  HIXL_CHK_STATUS_RET(SendFabricMemMsg(conn_fd, FabricMemMsgType::kSendNotifyReq, payload.dump()),
                      "Send fabric mem notify failed.");
  return SUCCESS;
}

Status FabricMemControlClient::FetchNotifies(const std::string &remote_engine, int32_t timeout_ms,
                                             std::vector<NotifyDesc> &notifies) {
  int32_t conn_fd = -1;
  HIXL_CHK_STATUS_RET(ConnectToEngine(remote_engine, timeout_ms, conn_fd),
                      "Connect remote fabric mem engine failed:%s.", remote_engine.c_str());
  HIXL_MAKE_GUARD(close_fd, ([conn_fd]() { (void)close(conn_fd); }));
  HIXL_CHK_STATUS_RET(SendFabricMemMsg(conn_fd, FabricMemMsgType::kGetNotifiesReq, ""),
                      "Send fabric mem get notifies request failed.");
  int32_t msg_type = 0;
  std::string payload;
  HIXL_CHK_STATUS_RET(RecvFabricMemMsg(conn_fd, timeout_ms, msg_type, payload),
                      "Recv fabric mem get notifies response failed.");
  HIXL_CHK_BOOL_RET_STATUS(msg_type == FabricMemMsgType::kGetNotifiesResp, PARAM_INVALID,
                           "Unexpected fabric mem response type:%d.", msg_type);
  try {
    auto json = nlohmann::json::parse(payload);
    const auto result = json.at("result").get<Status>();
    HIXL_CHK_STATUS_RET(result, "Peer failed to provide notifies.");
    notifies.clear();
    for (const auto &item : json.at("notifies")) {
      NotifyDesc notify;
      notify.name = AscendString(item.at("name").get<std::string>().c_str());
      notify.notify_msg = AscendString(item.at("notify_msg").get<std::string>().c_str());
      notifies.emplace_back(std::move(notify));
    }
    return SUCCESS;
  } catch (const nlohmann::json::exception &e) {
    HIXL_LOGE(PARAM_INVALID, "Parse fabric mem get notifies response failed:%s", e.what());
    return PARAM_INVALID;
  }
}
}  // namespace hixl
