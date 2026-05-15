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

#include "common/ctrl_msg.h"
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
constexpr size_t kShareHandleDataSize = sizeof(aclrtMemFabricHandle{}.data);
constexpr int32_t kShareHandleArrayCheckErrCode = 401;

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

Status SendTypedMsg(int32_t fd, CtrlMsgType msg_type, const std::string &payload) {
  CtrlMsgHeader header{kMagicNumber, sizeof(CtrlMsgType) + payload.size()};
  HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Send(fd, &header, sizeof(header)), "Send fabric mem msg header failed.");
  HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Send(fd, &msg_type, sizeof(msg_type)), "Send fabric mem msg type failed.");
  if (!payload.empty()) {
    HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Send(fd, payload.data(), payload.size()), "Send fabric mem msg body failed.");
  }
  return SUCCESS;
}

Status RecvTypedMsg(int32_t fd, int32_t timeout_ms, CtrlMsgType &msg_type, std::string &payload) {
  CtrlMsgHeader header{};
  HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Recv(fd, &header, sizeof(header), timeout_ms),
                      "Recv fabric mem msg header failed.");
  HIXL_CHK_BOOL_RET_STATUS(header.magic == kMagicNumber, PARAM_INVALID, "Invalid fabric mem control magic:0x%x.",
                           header.magic);
  HIXL_CHK_BOOL_RET_STATUS(header.body_size >= sizeof(CtrlMsgType), PARAM_INVALID,
                           "Invalid fabric mem control body size:%lu.", header.body_size);
  std::vector<char> body(header.body_size);
  HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Recv(fd, body.data(), body.size(), timeout_ms),
                      "Recv fabric mem msg body failed.");
  const errno_t rc = memcpy_s(&msg_type, sizeof(msg_type), body.data(), sizeof(msg_type));
  HIXL_CHK_BOOL_RET_STATUS(rc == EOK, FAILED, "memcpy_s fabric mem msg type failed, rc=%d", static_cast<int32_t>(rc));
  payload.assign(body.data() + sizeof(CtrlMsgType), body.data() + body.size());
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
  if (port <= 0) {
    provider_ = std::move(provider);
    HIXL_LOGI("FabricMemControlServer does not listen because port:%d.", port);
    return SUCCESS;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  if (running_.load(std::memory_order_acquire)) {
    return SUCCESS;
  }
  CtrlMsgPlugin::Initialize();
  HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Listen(ip, static_cast<uint32_t>(port), kBacklog, listen_fd_),
                      "Fabric mem control listen failed, ip:%s, port:%d.", ip.c_str(), port);
  provider_ = std::move(provider);
  running_.store(true, std::memory_order_release);
  worker_ = std::thread(&FabricMemControlServer::Run, this);
  HIXL_EVENT("FabricMemControlServer started, listen:%s:%d.", ip.c_str(), port);
  return SUCCESS;
}

void FabricMemControlServer::Stop() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_.load(std::memory_order_acquire) && listen_fd_ < 0) {
      return;
    }
    running_.store(false, std::memory_order_release);
    if (listen_fd_ >= 0) {
      (void)close(listen_fd_);
      listen_fd_ = -1;
    }
  }
  if (worker_.joinable()) {
    worker_.join();
  }
}

void FabricMemControlServer::Run() {
  while (running_.load(std::memory_order_acquire)) {
    pollfd pfd{};
    pfd.fd = listen_fd_;
    pfd.events = POLLIN;
    const int32_t ret = poll(&pfd, 1, kPollTimeoutMs);
    if (ret <= 0) {
      continue;
    }
    int32_t conn_fd = -1;
    if (CtrlMsgPlugin::Accept(listen_fd_, conn_fd) != SUCCESS || conn_fd < 0) {
      continue;
    }
    HIXL_MAKE_GUARD(close_conn, ([conn_fd]() { (void)close(conn_fd); }));
    (void)CtrlMsgPlugin::SetTcpNoDelay(conn_fd);
    (void)CtrlMsgPlugin::SetTcpKeepAlive(conn_fd);
    (void)HandleConnection(conn_fd);
  }
}

Status FabricMemControlServer::HandleConnection(int32_t fd) {
  CtrlMsgType msg_type{};
  std::string payload;
  HIXL_CHK_STATUS_RET(RecvTypedMsg(fd, kPollTimeoutMs * 10, msg_type, payload), "Recv fabric mem request failed.");
  switch (msg_type) {
    case CtrlMsgType::kGetFabricMemInfoReq:
      return HandleGetFabricMemInfo(fd);
    case CtrlMsgType::kSendNotifyReq:
      return HandleSendNotify(payload);
    case CtrlMsgType::kGetNotifiesReq:
      return HandleGetNotifies(fd);
    default:
      HIXL_LOGE(PARAM_INVALID, "Unexpected fabric mem request type:%d.", static_cast<int32_t>(msg_type));
      return PARAM_INVALID;
  }
}

Status FabricMemControlServer::HandleGetFabricMemInfo(int32_t fd) {
  std::vector<ShareHandleInfo> share_handles;
  Status result = provider_(share_handles);
  HIXL_CHK_STATUS_RET(SendShareHandleResponse(fd, result, share_handles), "Send fabric mem response failed.");
  return result;
}

Status FabricMemControlServer::HandleSendNotify(const std::string &payload) {
  try {
    auto json = nlohmann::json::parse(payload);
    NotifyDesc notify;
    notify.name = AscendString(json.at("name").get<std::string>().c_str());
    notify.notify_msg = AscendString(json.at("notify_msg").get<std::string>().c_str());
    std::lock_guard<std::mutex> lock(notify_mutex_);
    notify_queue_.emplace_back(std::move(notify));
    return SUCCESS;
  } catch (const nlohmann::json::exception &e) {
    HIXL_LOGE(PARAM_INVALID, "Parse fabric mem notify failed:%s", e.what());
    return PARAM_INVALID;
  }
}

Status FabricMemControlServer::HandleGetNotifies(int32_t fd) {
  std::vector<NotifyDesc> notifies;
  {
    std::lock_guard<std::mutex> lock(notify_mutex_);
    notifies.swap(notify_queue_);
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
  HIXL_CHK_STATUS_RET(SendTypedMsg(fd, CtrlMsgType::kGetNotifiesResp, response.dump()),
                      "Send fabric mem get notifies response failed.");
  return SUCCESS;
}

Status FabricMemControlServer::DequeueNotifies(std::vector<NotifyDesc> &notifies) {
  std::lock_guard<std::mutex> lock(notify_mutex_);
  notifies.swap(notify_queue_);
  return SUCCESS;
}

Status FabricMemControlServer::SendShareHandleResponse(int32_t fd, Status result,
                                                       const std::vector<ShareHandleInfo> &share_handles) {
  nlohmann::json response;
  response["result"] = result;
  auto share_handle_array = nlohmann::json::array();
  for (const auto &share_handle : share_handles) {
    nlohmann::json item;
    to_json(item, share_handle);
    share_handle_array.push_back(std::move(item));
  }
  response["share_handles"] = std::move(share_handle_array);
  HIXL_CHK_STATUS_RET(SendTypedMsg(fd, CtrlMsgType::kGetFabricMemInfoResp, response.dump()),
                      "Send fabric mem share handle response failed.");
  return SUCCESS;
}

Status FabricMemControlClient::Fetch(const std::string &remote_engine, int32_t timeout_ms,
                                     std::vector<ShareHandleInfo> &share_handles) {
  std::string remote_ip;
  int32_t remote_port = -1;
  HIXL_CHK_STATUS_RET(ParseListenInfo(remote_engine, remote_ip, remote_port), "Parse remote fabric mem engine failed.");
  HIXL_CHK_BOOL_RET_STATUS(remote_port > 0, PARAM_INVALID, "Remote fabric mem engine port must be greater than zero.");
  CtrlMsgPlugin::Initialize();
  int32_t conn_fd = -1;
  HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Connect(remote_ip, static_cast<uint32_t>(remote_port), conn_fd, timeout_ms),
                      "Connect remote fabric mem engine failed:%s.", remote_engine.c_str());
  HIXL_MAKE_GUARD(close_fd, ([conn_fd]() { (void)close(conn_fd); }));
  HIXL_CHK_STATUS_RET(SendTypedMsg(conn_fd, CtrlMsgType::kGetFabricMemInfoReq, ""),
                      "Send fabric mem share handle request failed.");

  CtrlMsgType msg_type{};
  std::string payload;
  HIXL_CHK_STATUS_RET(RecvTypedMsg(conn_fd, timeout_ms, msg_type, payload),
                      "Recv fabric mem share handle response failed.");
  HIXL_CHK_BOOL_RET_STATUS(msg_type == CtrlMsgType::kGetFabricMemInfoResp, PARAM_INVALID,
                           "Unexpected fabric mem response type:%d.", static_cast<int32_t>(msg_type));
  try {
    auto json = nlohmann::json::parse(payload);
    const auto result = json.at("result").get<Status>();
    HIXL_CHK_STATUS_RET(result, "Peer failed to provide fabric mem share handles.");
    share_handles.clear();
    for (const auto &item : json.at("share_handles")) {
      ShareHandleInfo share_handle{};
      from_json(item, share_handle);
      share_handles.emplace_back(share_handle);
    }
    return SUCCESS;
  } catch (const nlohmann::json::exception &e) {
    HIXL_LOGE(PARAM_INVALID, "Parse fabric mem share handle response failed:%s", e.what());
    return PARAM_INVALID;
  }
}

Status FabricMemControlClient::SendNotify(const std::string &remote_engine, const NotifyDesc &notify,
                                          int32_t timeout_ms) {
  std::string remote_ip;
  int32_t remote_port = -1;
  HIXL_CHK_STATUS_RET(ParseListenInfo(remote_engine, remote_ip, remote_port), "Parse remote fabric mem engine failed.");
  HIXL_CHK_BOOL_RET_STATUS(remote_port > 0, PARAM_INVALID, "Remote fabric mem engine port must be greater than zero.");
  CtrlMsgPlugin::Initialize();
  int32_t conn_fd = -1;
  HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Connect(remote_ip, static_cast<uint32_t>(remote_port), conn_fd, timeout_ms),
                      "Connect remote fabric mem engine failed:%s.", remote_engine.c_str());
  HIXL_MAKE_GUARD(close_fd, ([conn_fd]() { (void)close(conn_fd); }));
  nlohmann::json payload;
  payload["name"] = notify.name.GetString();
  payload["notify_msg"] = notify.notify_msg.GetString();
  HIXL_CHK_STATUS_RET(SendTypedMsg(conn_fd, CtrlMsgType::kSendNotifyReq, payload.dump()),
                      "Send fabric mem notify failed.");
  return SUCCESS;
}

Status FabricMemControlClient::FetchNotifies(const std::string &remote_engine, int32_t timeout_ms,
                                             std::vector<NotifyDesc> &notifies) {
  std::string remote_ip;
  int32_t remote_port = -1;
  HIXL_CHK_STATUS_RET(ParseListenInfo(remote_engine, remote_ip, remote_port), "Parse remote fabric mem engine failed.");
  HIXL_CHK_BOOL_RET_STATUS(remote_port > 0, PARAM_INVALID, "Remote fabric mem engine port must be greater than zero.");
  CtrlMsgPlugin::Initialize();
  int32_t conn_fd = -1;
  HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Connect(remote_ip, static_cast<uint32_t>(remote_port), conn_fd, timeout_ms),
                      "Connect remote fabric mem engine failed:%s.", remote_engine.c_str());
  HIXL_MAKE_GUARD(close_fd, ([conn_fd]() { (void)close(conn_fd); }));
  HIXL_CHK_STATUS_RET(SendTypedMsg(conn_fd, CtrlMsgType::kGetNotifiesReq, ""),
                      "Send fabric mem get notifies request failed.");
  CtrlMsgType msg_type{};
  std::string payload;
  HIXL_CHK_STATUS_RET(RecvTypedMsg(conn_fd, timeout_ms, msg_type, payload),
                      "Recv fabric mem get notifies response failed.");
  HIXL_CHK_BOOL_RET_STATUS(msg_type == CtrlMsgType::kGetNotifiesResp, PARAM_INVALID,
                           "Unexpected fabric mem response type:%d.", static_cast<int32_t>(msg_type));
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
