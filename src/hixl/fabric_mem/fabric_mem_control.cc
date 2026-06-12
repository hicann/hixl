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

#include <atomic>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/epoll.h>
#include <sys/socket.h>
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
constexpr uint32_t kFabricMemMagic = 0xA4B3C2D1;
constexpr size_t kMaxMsgLen = 1ULL << 20;
constexpr size_t kRecvChunkSize = 4096U;
constexpr int32_t kMaxEpollEvents = 64;
constexpr int64_t kDefaultHeartbeatTimeoutMs = 120000;
constexpr int32_t kHeartBeatMsgType = static_cast<int32_t>(FabricMemAdxlMsgType::kHeartBeat);
constexpr int64_t kAdxlMicrosPerMillis = 1000;
constexpr size_t kFabricMemRequestHeaderSize = sizeof(uint32_t) + sizeof(uint64_t);
// Bounds how long a control-plane send may block when a peer stops reading. Used for server-side
// responses that have no caller-supplied timeout; client requests derive it from their timeout_ms.
constexpr uint64_t kDefaultCtrlSendTimeoutUs = 3ULL * 1000ULL * 1000ULL;

std::atomic<int64_t> heartbeat_timeout_ms_{kDefaultHeartbeatTimeoutMs};

int64_t RemainingAdxlMicros(const std::chrono::steady_clock::time_point &start, uint64_t timeout_us) {
  const auto elapsed =
      std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
  return static_cast<int64_t>(timeout_us) - elapsed;
}

Status PollForAdxlWriteReady(int32_t fd, int64_t remaining_us) {
  if (remaining_us <= 0) {
    return TIMEOUT;
  }
  pollfd pfd{};
  pfd.fd = fd;
  pfd.events = POLLOUT;
  const int poll_timeout_ms = static_cast<int>(remaining_us / kAdxlMicrosPerMillis);
  const int poll_ret = poll(&pfd, 1, poll_timeout_ms);
  if (poll_ret == 0) {
    return TIMEOUT;
  }
  if (poll_ret < 0) {
    if (errno == EINTR) {
      return SUCCESS;
    }
    HIXL_LOGE(FAILED, "Socket poll failed, errno:%d, msg:%s.", errno, strerror(errno));
    return FAILED;
  }
  return SUCCESS;
}

Status WriteAdxlWithTimeout(int32_t fd, const void *buf, size_t len, uint64_t timeout_us,
                            const std::chrono::steady_clock::time_point &start) {
  const auto *data = static_cast<const char *>(buf);
  size_t written = 0U;
  while (written < len) {
    const ssize_t ret = send(fd, data + written, len - written, MSG_NOSIGNAL);
    if (ret < 0) {
      if (errno == EINTR) {
        continue;
      }
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        HIXL_CHK_STATUS_RET(PollForAdxlWriteReady(fd, RemainingAdxlMicros(start, timeout_us)), "Socket poll failed.");
        continue;
      }
      HIXL_LOGE(FAILED, "Socket send failed, errno:%d, msg:%s.", errno, strerror(errno));
      return FAILED;
    }
    written += static_cast<size_t>(ret);
  }
  return SUCCESS;
}

Status SendAdxlMsgByProtocol(int32_t fd, int32_t msg_type, const std::string &msg_str, uint64_t timeout_us) {
  auto start = std::chrono::steady_clock::now();
  const uint64_t body_size = static_cast<uint64_t>(sizeof(msg_type)) + msg_str.size();
  FabricMemAdxlProtocolHeader header{kFabricMemAdxlMagic, body_size};
  HIXL_CHK_STATUS_RET(WriteAdxlWithTimeout(fd, &header, sizeof(header), timeout_us, start), "Failed to write header.");
  HIXL_CHK_STATUS_RET(WriteAdxlWithTimeout(fd, &msg_type, sizeof(msg_type), timeout_us, start),
                      "Failed to write msg type.");
  if (!msg_str.empty()) {
    HIXL_CHK_STATUS_RET(WriteAdxlWithTimeout(fd, msg_str.data(), msg_str.size(), timeout_us, start),
                        "Failed to write msg body.");
  }
  return SUCCESS;
}

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

uint64_t CtrlSendTimeoutUs(int32_t timeout_ms) {
  return timeout_ms > 0 ? static_cast<uint64_t>(timeout_ms) * static_cast<uint64_t>(kAdxlMicrosPerMillis)
                        : kDefaultCtrlSendTimeoutUs;
}

// Sends a FabricMem control message through the timeout-aware write path so a peer that stops reading
// cannot make the worker (server) or caller (client) spin/block forever on EAGAIN. The whole message
// must be flushed within timeout_us; the wire format (magic + length + msg_type + payload) is unchanged.
Status SendFabricMemMsg(int32_t fd, int32_t msg_type, const std::string &payload,
                        uint64_t timeout_us = kDefaultCtrlSendTimeoutUs) {
  const uint64_t length = static_cast<uint64_t>(sizeof(msg_type)) + payload.size();
  auto start = std::chrono::steady_clock::now();
  HIXL_CHK_STATUS_RET(WriteAdxlWithTimeout(fd, &kFabricMemMagic, sizeof(kFabricMemMagic), timeout_us, start),
                      "Send fabric mem magic failed.");
  HIXL_CHK_STATUS_RET(WriteAdxlWithTimeout(fd, &length, sizeof(length), timeout_us, start),
                      "Send fabric mem length failed.");
  HIXL_CHK_STATUS_RET(WriteAdxlWithTimeout(fd, &msg_type, sizeof(msg_type), timeout_us, start),
                      "Send fabric mem msg type failed.");
  if (!payload.empty()) {
    HIXL_CHK_STATUS_RET(WriteAdxlWithTimeout(fd, payload.data(), payload.size(), timeout_us, start),
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

Status ConnectToEngine(const std::string &remote_engine, int32_t timeout_ms, int32_t &conn_fd) {
  std::string remote_ip;
  int32_t remote_port = -1;
  HIXL_CHK_STATUS_RET(ParseListenInfo(remote_engine, remote_ip, remote_port), "Parse remote fabric mem engine failed.");
  HIXL_CHK_BOOL_RET_STATUS(remote_port > 0, PARAM_INVALID, "Remote fabric mem engine port must be greater than zero.");
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
    try {
      from_json(item, info);
    } catch (const nlohmann::json::exception &e) {
      HIXL_LOGE(PARAM_INVALID, "Parse share handle item from connect response failed:%s", e.what());
      return PARAM_INVALID;
    }
    share_handles.emplace_back(std::move(info));
  }
  return SUCCESS;
}

Status ParseStatusFromPayload(const std::string &payload) {
  try {
    const auto json = nlohmann::json::parse(payload);
    const auto error_code = json.at("error_code").get<uint32_t>();
    return static_cast<Status>(error_code);
  } catch (const nlohmann::json::exception &e) {
    HIXL_LOGE(PARAM_INVALID, "Parse fabric mem status failed:%s", e.what());
    return PARAM_INVALID;
  }
}

std::string ParseClientIdFromConnectPayload(const std::string &payload) {
  if (payload.empty()) {
    return "";
  }
  try {
    const auto json = nlohmann::json::parse(payload);
    if (json.contains("channel_id") && json["channel_id"].is_string()) {
      return json["channel_id"].get<std::string>();
    }
  } catch (const nlohmann::json::exception &e) {
    HIXL_LOGW("Parse connect request channel_id failed:%s", e.what());
  }
  return "";
}

Status ValidateNotifyContent(const std::string &name, const std::string &notify_msg) {
  if (name.size() > kMaxNotifyNameLen) {
    HIXL_LOGE(PARAM_INVALID, "Notify name length invalid, size:%zu, max:%zu.", name.size(), kMaxNotifyNameLen);
    return PARAM_INVALID;
  }
  if (notify_msg.size() > kMaxNotifyMsgLen) {
    HIXL_LOGE(PARAM_INVALID, "Notify message too long, size:%zu, max:%zu.", notify_msg.size(), kMaxNotifyMsgLen);
    return PARAM_INVALID;
  }
  return SUCCESS;
}

Status SetNonBlocking(int32_t fd) {
  const int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    HIXL_LOGE(FAILED, "fcntl F_GETFL failed, fd:%d, errno:%d.", fd, errno);
    return FAILED;
  }
  if (fcntl(fd, F_SETFL, static_cast<uint32_t>(flags) | static_cast<uint32_t>(O_NONBLOCK)) < 0) {
    HIXL_LOGE(FAILED, "fcntl F_SETFL failed, fd:%d, errno:%d.", fd, errno);
    return FAILED;
  }
  return SUCCESS;
}

void RemoveFdFromEpoll(int32_t epoll_fd, int32_t fd) {
  if (epoll_fd >= 0 && fd >= 0) {
    (void)epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
  }
}

Status ValidateAdxlHeader(const FabricMemAdxlProtocolHeader &header) {
  HIXL_CHK_BOOL_RET_STATUS(header.magic == kFabricMemAdxlMagic, PARAM_INVALID, "Invalid adxl magic:0x%x.",
                           header.magic);
  HIXL_CHK_BOOL_RET_STATUS(header.body_size >= sizeof(int32_t) && header.body_size <= kMaxMsgLen, PARAM_INVALID,
                           "Invalid adxl body size:%lu.", header.body_size);
  return SUCCESS;
}

Status ValidateFabricMemRequestHeader(uint32_t magic, uint64_t body_size) {
  HIXL_CHK_BOOL_RET_STATUS(magic == kFabricMemMagic, PARAM_INVALID, "Invalid fabric mem magic:0x%x.", magic);
  HIXL_CHK_BOOL_RET_STATUS(body_size >= sizeof(int32_t) && body_size <= kMaxMsgLen, PARAM_INVALID,
                           "Invalid fabric mem msg length:%lu.", body_size);
  return SUCCESS;
}

Status SendConnectResponseMessages(int32_t fd, const std::string &client_id,
                                   const std::vector<ShareHandleInfo> &share_handles, Status provider_result) {
  nlohmann::json response;
  response["channel_id"] = client_id;
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
  nlohmann::json status;
  status["error_code"] = static_cast<uint32_t>(provider_result);
  status["error_message"] = "";
  HIXL_CHK_STATUS_RET(SendFabricMemMsg(fd, FabricMemMsgType::kStatus, status.dump()), "Send status response failed.");
  return SUCCESS;
}

}  // namespace

void FabricMemControlServer::ShiftRecvBuffer(std::vector<char> &recv_buffer, size_t &bytes_received, size_t consumed,
                                             size_t remaining) {
  if (remaining > 0U) {
    const errno_t rc = memmove_s(recv_buffer.data(), recv_buffer.size(), recv_buffer.data() + consumed, remaining);
    if (rc != EOK) {
      HIXL_LOGE(FAILED, "FabricMemControlServer memmove_s recv buffer failed, rc:%d, max:%zu, count:%zu.",
                static_cast<int32_t>(rc), recv_buffer.size(), remaining);
      bytes_received = 0U;
      return;
    }
  }
  bytes_received = remaining;
}

bool FabricMemControlServer::IsSessionHeartbeatTimeout(const ClientSession &session) {
  if (!session.with_heartbeat) {
    return false;
  }
  const auto now = std::chrono::steady_clock::now();
  const auto cost = std::chrono::duration_cast<std::chrono::milliseconds>(now - session.last_heartbeat_time).count();
  return cost >= heartbeat_timeout_ms_.load(std::memory_order_relaxed);
}

void FabricMemControlServer::SetHeartbeatTimeoutMs(int64_t timeout_ms) {
  heartbeat_timeout_ms_.store(timeout_ms > 0 ? timeout_ms : kDefaultHeartbeatTimeoutMs, std::memory_order_relaxed);
}

FabricMemControlServer::~FabricMemControlServer() {
  Stop();
}

Status FabricMemControlServer::Start(const std::string &listen_info, FabricMemShareHandleProvider provider,
                                     bool auto_cleanup_enabled) {
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
    state->auto_cleanup_enabled = auto_cleanup_enabled;
    HIXL_LOGI("FabricMemControlServer does not listen because port:%d.", port);
    return SUCCESS;
  }
  std::lock_guard<std::mutex> lock(state->mutex);
  if (state->running.load(std::memory_order_acquire)) {
    return SUCCESS;
  }
  state->auto_cleanup_enabled = auto_cleanup_enabled;
  state->local_engine_id = listen_info;
  CtrlMsgPlugin::Initialize();
  HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Listen(ip, static_cast<uint32_t>(port), kBacklog, state->listen_fd),
                      "Fabric mem control listen failed, ip:%s, port:%d.", ip.c_str(), port);
  state->epoll_fd = epoll_create1(0);
  HIXL_CHK_BOOL_RET_STATUS(state->epoll_fd >= 0, FAILED, "Failed to create epoll fd.");
  HIXL_CHK_STATUS_RET(CtrlMsgPlugin::AddFdToEpoll(state->epoll_fd, state->listen_fd), "Failed to add listen fd.");
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
      (void)shutdown(state->listen_fd, SHUT_RDWR);
      (void)close(state->listen_fd);
      state->listen_fd = -1;
    }
  }
  // Join before clearing sessions/pending_connections: the worker exits within kPollTimeoutMs once
  // running is false, after which it can no longer touch the maps, so the cleanup below races with
  // nobody (pending_connections is worker-exclusive at runtime).
  if (worker_.joinable()) {
    worker_.join();
  }
  std::lock_guard<std::mutex> lock(state->mutex);
  for (const auto &item : state->sessions) {
    if (item.first >= 0) {
      (void)close(item.first);
    }
  }
  state->sessions.clear();
  state->client_id_to_fd.clear();
  for (const auto &item : state->pending_connections) {
    if (item.first >= 0) {
      (void)close(item.first);
    }
  }
  state->pending_connections.clear();
  if (state->epoll_fd >= 0) {
    (void)close(state->epoll_fd);
    state->epoll_fd = -1;
  }
}

bool FabricMemControlServer::IsListening() const {
  std::lock_guard<std::mutex> lock(state_->mutex);
  return state_->listen_fd >= 0 && state_->running.load(std::memory_order_acquire);
}

void FabricMemControlServer::CheckClientHeartbeatTimeouts() {
  auto state = state_;
  std::lock_guard<std::mutex> lock(state->mutex);
  if (!state->running.load(std::memory_order_acquire) || !state->auto_cleanup_enabled) {
    return;
  }
  CheckClientHeartbeatTimeoutsLocked(state, state->epoll_fd);
}

void FabricMemControlServer::CheckClientHeartbeatTimeoutsLocked(const std::shared_ptr<State> &state, int32_t epoll_fd) {
  if (!state->auto_cleanup_enabled) {
    return;
  }
  std::vector<int32_t> timeout_fds;
  for (const auto &item : state->sessions) {
    if (FabricMemControlServer::IsSessionHeartbeatTimeout(item.second)) {
      timeout_fds.push_back(item.first);
    }
  }
  for (const int32_t fd : timeout_fds) {
    HIXL_LOGW("FabricMemControlServer client heartbeat timeout, fd:%d.", fd);
    CloseClientConnection(state, epoll_fd, fd);
  }
}

void FabricMemControlServer::EraseClientSessionLocked(const std::shared_ptr<State> &state, int32_t fd) {
  const auto session_it = state->sessions.find(fd);
  if (session_it == state->sessions.end()) {
    return;
  }
  if (!session_it->second.client_id.empty()) {
    const auto client_it = state->client_id_to_fd.find(session_it->second.client_id);
    if (client_it != state->client_id_to_fd.end() && client_it->second == fd) {
      state->client_id_to_fd.erase(client_it);
    }
  }
  state->sessions.erase(session_it);
}

void FabricMemControlServer::CloseClientConnection(const std::shared_ptr<State> &state, int32_t epoll_fd, int32_t fd) {
  RemoveFdFromEpoll(epoll_fd, fd);
  EraseClientSessionLocked(state, fd);
  if (fd >= 0) {
    (void)close(fd);
  }
}

void FabricMemControlServer::ClosePendingConnection(const std::shared_ptr<State> &state, int32_t epoll_fd, int32_t fd) {
  RemoveFdFromEpoll(epoll_fd, fd);
  state->pending_connections.erase(fd);
  if (fd >= 0) {
    (void)close(fd);
  }
}

void FabricMemControlServer::RegisterClientSession(const std::shared_ptr<State> &state, int32_t epoll_fd, int32_t fd,
                                                   const std::string &client_id) {
  if (!client_id.empty()) {
    const auto existing_it = state->client_id_to_fd.find(client_id);
    if (existing_it != state->client_id_to_fd.end() && existing_it->second != fd) {
      if (state->auto_cleanup_enabled) {
        HIXL_LOGI("FabricMemControlServer replacing existing client session, client_id:%s, old_fd:%d, new_fd:%d.",
                  client_id.c_str(), existing_it->second, fd);
        CloseClientConnection(state, epoll_fd, existing_it->second);
      }
    }
    state->client_id_to_fd[client_id] = fd;
  }

  ClientSession session;
  session.fd = fd;
  session.client_id = client_id;
  session.with_heartbeat = true;
  session.last_heartbeat_time = std::chrono::steady_clock::now();
  state->sessions[fd] = std::move(session);
}

void FabricMemControlServer::RegisterPendingConnection(const std::shared_ptr<State> &state, int32_t epoll_fd,
                                                       int32_t fd) {
  state->pending_connections[fd] = PendingConnection{};
  const Status epoll_ret = CtrlMsgPlugin::AddFdToEpoll(epoll_fd, fd);
  if (epoll_ret != SUCCESS) {
    ClosePendingConnection(state, epoll_fd, fd);
  }
}

Status FabricMemControlServer::AppendSessionRecv(ClientSession &session, int32_t fd) {
  if (session.recv_buffer.size() < session.bytes_received + kRecvChunkSize) {
    session.recv_buffer.resize(session.bytes_received + kRecvChunkSize);
  }
  const ssize_t n = recv(fd, session.recv_buffer.data() + session.bytes_received,
                         session.recv_buffer.size() - session.bytes_received, 0);
  if (n == 0) {
    HIXL_LOGI("FabricMemControlServer connection closed by peer, fd:%d.", fd);
    return FAILED;
  }
  if (n < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
      return SUCCESS;
    }
    HIXL_LOGE(FAILED, "FabricMemControlServer recv failed, fd:%d, errno:%d.", fd, errno);
    return FAILED;
  }
  session.bytes_received += static_cast<size_t>(n);
  return SUCCESS;
}

Status FabricMemControlServer::AppendPendingRecv(PendingConnection &pending, int32_t fd) {
  if (pending.recv_buffer.size() < pending.bytes_received + kRecvChunkSize) {
    pending.recv_buffer.resize(pending.bytes_received + kRecvChunkSize);
  }
  const ssize_t n = recv(fd, pending.recv_buffer.data() + pending.bytes_received,
                         pending.recv_buffer.size() - pending.bytes_received, 0);
  if (n == 0) {
    HIXL_LOGI("FabricMemControlServer pending connection closed by peer, fd:%d.", fd);
    return FAILED;
  }
  if (n < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
      return SUCCESS;
    }
    HIXL_LOGE(FAILED, "FabricMemControlServer pending recv failed, fd:%d, errno:%d.", fd, errno);
    return FAILED;
  }
  pending.bytes_received += static_cast<size_t>(n);
  return SUCCESS;
}

bool FabricMemControlServer::ProcessWaitingHeader(const std::shared_ptr<State> &state, int32_t epoll_fd, int32_t fd,
                                                  ClientSession &session) {
  if (session.bytes_received < sizeof(FabricMemAdxlProtocolHeader)) {
    return false;
  }
  FabricMemAdxlProtocolHeader header{};
  const errno_t rc = memcpy_s(&header, sizeof(header), session.recv_buffer.data(), sizeof(header));
  if (rc != EOK) {
    HIXL_LOGE(FAILED, "FabricMemControlServer memcpy_s adxl header failed, rc:%d.", static_cast<int32_t>(rc));
    CloseClientConnection(state, epoll_fd, fd);
    return false;
  }
  if (ValidateAdxlHeader(header) != SUCCESS) {
    CloseClientConnection(state, epoll_fd, fd);
    return false;
  }
  session.expected_body_size = header.body_size;
  session.recv_state = RecvState::kWaitingBody;
  if (session.bytes_received > sizeof(FabricMemAdxlProtocolHeader)) {
    const size_t remaining = session.bytes_received - sizeof(FabricMemAdxlProtocolHeader);
    ShiftRecvBuffer(session.recv_buffer, session.bytes_received, sizeof(FabricMemAdxlProtocolHeader), remaining);
  } else {
    session.bytes_received = 0U;
  }
  return true;
}

bool FabricMemControlServer::ProcessWaitingBody(ClientSession &session) {
  if (session.bytes_received < session.expected_body_size) {
    return false;
  }
  int32_t msg_type = 0;
  const errno_t rc = memcpy_s(&msg_type, sizeof(msg_type), session.recv_buffer.data(), sizeof(msg_type));
  if (rc != EOK) {
    HIXL_LOGE(FAILED, "FabricMemControlServer memcpy_s adxl msg type failed, rc:%d.", static_cast<int32_t>(rc));
    return false;
  }
  if (msg_type == kHeartBeatMsgType) {
    session.last_heartbeat_time = std::chrono::steady_clock::now();
  } else {
    HIXL_LOGW("FabricMemControlServer unexpected adxl msg type:%d on fd:%d.", msg_type, session.fd);
  }
  if (session.bytes_received > session.expected_body_size) {
    const size_t remaining = session.bytes_received - session.expected_body_size;
    ShiftRecvBuffer(session.recv_buffer, session.bytes_received, session.expected_body_size, remaining);
    session.recv_state = RecvState::kWaitingHeader;
    return true;
  }
  session.bytes_received = 0U;
  session.recv_state = RecvState::kWaitingHeader;
  return false;
}

bool FabricMemControlServer::ProcessPendingWaitingHeader(const std::shared_ptr<State> &state, int32_t epoll_fd,
                                                         int32_t fd, PendingConnection &pending) {
  if (pending.bytes_received < kFabricMemRequestHeaderSize) {
    return false;
  }
  uint32_t magic = 0U;
  uint64_t body_size = 0ULL;
  const errno_t magic_rc = memcpy_s(&magic, sizeof(magic), pending.recv_buffer.data(), sizeof(magic));
  const errno_t size_rc =
      memcpy_s(&body_size, sizeof(body_size), pending.recv_buffer.data() + sizeof(magic), sizeof(body_size));
  if (magic_rc != EOK || size_rc != EOK) {
    HIXL_LOGE(FAILED, "FabricMemControlServer memcpy_s request header failed, magic_rc:%d, size_rc:%d.",
              static_cast<int32_t>(magic_rc), static_cast<int32_t>(size_rc));
    ClosePendingConnection(state, epoll_fd, fd);
    return false;
  }
  if (ValidateFabricMemRequestHeader(magic, body_size) != SUCCESS) {
    ClosePendingConnection(state, epoll_fd, fd);
    return false;
  }
  pending.expected_body_size = body_size;
  pending.recv_state = FabricMemRequestRecvState::kWaitingBody;
  if (pending.bytes_received > kFabricMemRequestHeaderSize) {
    const size_t remaining = pending.bytes_received - kFabricMemRequestHeaderSize;
    ShiftRecvBuffer(pending.recv_buffer, pending.bytes_received, kFabricMemRequestHeaderSize, remaining);
  } else {
    pending.bytes_received = 0U;
  }
  return true;
}

bool FabricMemControlServer::ProcessPendingWaitingBody(const std::shared_ptr<State> &state, int32_t epoll_fd,
                                                       int32_t fd, PendingConnection &pending) {
  if (pending.bytes_received < pending.expected_body_size) {
    return false;
  }
  int32_t msg_type = 0;
  const errno_t rc = memcpy_s(&msg_type, sizeof(msg_type), pending.recv_buffer.data(), sizeof(msg_type));
  if (rc != EOK) {
    HIXL_LOGE(FAILED, "FabricMemControlServer memcpy_s request msg type failed, rc:%d.", static_cast<int32_t>(rc));
    ClosePendingConnection(state, epoll_fd, fd);
    return false;
  }
  const size_t payload_size = static_cast<size_t>(pending.expected_body_size) - sizeof(msg_type);
  std::string payload;
  if (payload_size > 0U) {
    payload.assign(pending.recv_buffer.data() + sizeof(msg_type), payload_size);
  }
  state->pending_connections.erase(fd);
  (void)DispatchFabricMemRequest(state, fd, epoll_fd, msg_type, payload);
  return false;
}

void FabricMemControlServer::DrainAdxlMessages(const std::shared_ptr<State> &state, int32_t epoll_fd, int32_t fd,
                                               ClientSession &session) {
  while (true) {
    if (session.recv_state == RecvState::kWaitingHeader) {
      if (!ProcessWaitingHeader(state, epoll_fd, fd, session)) {
        break;
      }
      continue;
    }
    if (session.recv_state == RecvState::kWaitingBody && !ProcessWaitingBody(session)) {
      break;
    }
  }
}

void FabricMemControlServer::DrainPendingFabricMemMessages(const std::shared_ptr<State> &state, int32_t epoll_fd,
                                                           int32_t fd, PendingConnection &pending) {
  while (true) {
    if (pending.recv_state == FabricMemRequestRecvState::kWaitingHeader) {
      if (!ProcessPendingWaitingHeader(state, epoll_fd, fd, pending)) {
        break;
      }
      continue;
    }
    if (pending.recv_state == FabricMemRequestRecvState::kWaitingBody &&
        !ProcessPendingWaitingBody(state, epoll_fd, fd, pending)) {
      break;
    }
  }
}

void FabricMemControlServer::HandleClientRead(const std::shared_ptr<State> &state, int32_t epoll_fd, int32_t fd) {
  // The session read path performs no sends (only non-blocking recv + heartbeat bookkeeping), so it is
  // safe to hold state->mutex across the whole call. This serializes session mutations against Stop()
  // and the keepalive monitor's CheckClientHeartbeatTimeouts().
  std::lock_guard<std::mutex> lock(state->mutex);
  auto session_it = state->sessions.find(fd);
  if (session_it == state->sessions.end()) {
    return;
  }
  auto &session = session_it->second;
  if (AppendSessionRecv(session, fd) != SUCCESS) {
    CloseClientConnection(state, epoll_fd, fd);
    return;
  }
  DrainAdxlMessages(state, epoll_fd, fd, session);
}

void FabricMemControlServer::HandlePendingRead(const std::shared_ptr<State> &state, int32_t epoll_fd, int32_t fd) {
  auto pending_it = state->pending_connections.find(fd);
  if (pending_it == state->pending_connections.end()) {
    return;
  }
  auto &pending = pending_it->second;
  if (AppendPendingRecv(pending, fd) != SUCCESS) {
    ClosePendingConnection(state, epoll_fd, fd);
    return;
  }
  DrainPendingFabricMemMessages(state, epoll_fd, fd, pending);
}

void FabricMemControlServer::HandleEpollEvent(const std::shared_ptr<State> &state, int32_t epoll_fd, int32_t listen_fd,
                                              int32_t fd) {
  if (fd == listen_fd) {
    int32_t conn_fd = -1;
    if (CtrlMsgPlugin::Accept(listen_fd, conn_fd) != SUCCESS || conn_fd < 0) {
      return;
    }
    (void)CtrlMsgPlugin::SetTcpNoDelay(conn_fd);
    (void)CtrlMsgPlugin::SetTcpKeepAlive(conn_fd);
    if (SetNonBlocking(conn_fd) != SUCCESS) {
      (void)close(conn_fd);
      return;
    }
    std::lock_guard<std::mutex> lock(state->mutex);
    RegisterPendingConnection(state, epoll_fd, conn_fd);
    return;
  }
  bool handle_pending = false;
  bool handle_session = false;
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    handle_pending = state->pending_connections.find(fd) != state->pending_connections.end();
    if (!handle_pending) {
      handle_session = state->sessions.find(fd) != state->sessions.end();
    }
  }
  if (handle_pending) {
    HandlePendingRead(state, epoll_fd, fd);
    return;
  }
  if (handle_session) {
    HandleClientRead(state, epoll_fd, fd);
  }
}

void FabricMemControlServer::Run(std::shared_ptr<State> state) {
  epoll_event events[kMaxEpollEvents];
  while (state->running.load(std::memory_order_acquire)) {
    int32_t epoll_fd = -1;
    int32_t listen_fd = -1;
    {
      std::lock_guard<std::mutex> lock(state->mutex);
      epoll_fd = state->epoll_fd;
      listen_fd = state->listen_fd;
    }
    if (epoll_fd < 0 || listen_fd < 0) {
      break;
    }

    const int event_count = epoll_wait(epoll_fd, events, kMaxEpollEvents, kPollTimeoutMs);
    if (event_count < 0 && errno != EINTR) {
      HIXL_LOGE(FAILED, "FabricMemControlServer epoll_wait failed, errno:%d.", errno);
      break;
    }
    if (event_count <= 0) {
      continue;
    }

    {
      std::lock_guard<std::mutex> lock(state->mutex);
      // Stop() holds state->mutex while it shuts down and closes listen_fd. Re-check running/listen_fd
      // under the same lock after epoll_wait so a teardown that raced the wait makes the worker exit
      // instead of letting HandleEpollEvent call accept() on an already-closed listen_fd (which would
      // log a spurious "Failed to accept: Bad file descriptor"). See bugfix 553ec23.
      if (!state->running.load(std::memory_order_acquire) || state->listen_fd != listen_fd) {
        break;
      }
      if (state->auto_cleanup_enabled) {
        CheckClientHeartbeatTimeoutsLocked(state, epoll_fd);
      }
    }

    for (int i = 0; i < event_count; ++i) {
      HandleEpollEvent(state, epoll_fd, listen_fd, events[i].data.fd);
    }
  }
}

Status FabricMemControlServer::DispatchFabricMemRequest(const std::shared_ptr<State> &state, int32_t fd,
                                                        int32_t epoll_fd, int32_t msg_type,
                                                        const std::string &payload) {
  switch (msg_type) {
    case FabricMemMsgType::kConnect: {
      const Status ret = HandleConnectRequest(state, fd, epoll_fd, payload);
      bool has_session = false;
      {
        std::lock_guard<std::mutex> lock(state->mutex);
        has_session = state->sessions.find(fd) != state->sessions.end();
      }
      if (ret != SUCCESS && !has_session) {
        RemoveFdFromEpoll(epoll_fd, fd);
        if (fd >= 0) {
          (void)close(fd);
        }
      }
      return ret;
    }
    case FabricMemMsgType::kSendNotifyReq: {
      const Status ret = HandleSendNotify(state, payload);
      RemoveFdFromEpoll(epoll_fd, fd);
      if (fd >= 0) {
        (void)close(fd);
      }
      return ret;
    }
    case FabricMemMsgType::kDisconnect:
      return HandleDisconnectRequest(state, fd, epoll_fd, payload);
    default:
      HIXL_LOGE(PARAM_INVALID, "Unexpected fabric mem request type:%d.", msg_type);
      RemoveFdFromEpoll(epoll_fd, fd);
      if (fd >= 0) {
        (void)close(fd);
      }
      return PARAM_INVALID;
  }
}

Status FabricMemControlServer::HandleConnectRequest(const std::shared_ptr<State> &state, int32_t fd, int32_t epoll_fd,
                                                    const std::string &payload) {
  const std::string client_id = ParseClientIdFromConnectPayload(payload);
  std::vector<ShareHandleInfo> share_handles;
  FabricMemShareHandleProvider provider;
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    provider = state->provider;
  }
  HIXL_CHK_BOOL_RET_STATUS(static_cast<bool>(provider), FAILED, "FabricMemControlServer provider is empty.");
  const Status result = provider(share_handles);
  HIXL_CHK_STATUS_RET(SendConnectResponseMessages(fd, client_id, share_handles, result),
                      "Send connect response failed.");

  if (result != SUCCESS) {
    HIXL_LOGW("FabricMemControlServer connect provider failed, client_id:%s, ret:%u.", client_id.c_str(),
              static_cast<uint32_t>(result));
    return result;
  }

  HIXL_CHK_STATUS_RET(SetNonBlocking(fd), "Failed to set non-blocking mode.");
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    // On failure here, leave fd open and let DispatchFabricMemRequest be the single owner of the
    // connect-failure close (it removes fd from epoll and closes it). Closing here too would
    // double-close fd, which is dangerous if another thread has reused the fd number meanwhile.
    if (!state->auto_cleanup_enabled && !client_id.empty() &&
        state->client_id_to_fd.find(client_id) != state->client_id_to_fd.end()) {
      HIXL_LOGW("FabricMemControlServer rejecting duplicate connect, client_id:%s.", client_id.c_str());
      return NOT_CONNECTED;
    }
    RegisterClientSession(state, epoll_fd, fd, client_id);
    RemoveFdFromEpoll(epoll_fd, fd);
    const Status epoll_ret = CtrlMsgPlugin::AddFdToEpoll(epoll_fd, fd);
    if (epoll_ret != SUCCESS) {
      EraseClientSessionLocked(state, fd);
      return epoll_ret;
    }
  }
  HIXL_EVENT("FabricMemControlServer handled connect request, client_id:%s, shared %zu handles.", client_id.c_str(),
             share_handles.size());
  return SUCCESS;
}

Status FabricMemControlServer::HandleSendNotify(const std::shared_ptr<State> &state, const std::string &payload) {
  try {
    auto json = nlohmann::json::parse(payload);
    const std::string name = json.at("name").get<std::string>();
    const std::string notify_msg = json.at("notify_msg").get<std::string>();
    HIXL_CHK_STATUS_RET(ValidateNotifyContent(name, notify_msg), "Invalid fabric mem notify content.");
    std::lock_guard<std::mutex> lock(state->notify_mutex);
    if (state->notify_queue.size() >= kMaxNotifyQueueSize) {
      HIXL_LOGE(RESOURCE_EXHAUSTED, "Fabric mem notify queue is full, size:%zu, max:%zu.", state->notify_queue.size(),
                kMaxNotifyQueueSize);
      return RESOURCE_EXHAUSTED;
    }
    NotifyDesc notify;
    notify.name = AscendString(name.c_str());
    notify.notify_msg = AscendString(notify_msg.c_str());
    state->notify_queue.emplace_back(std::move(notify));
    return SUCCESS;
  } catch (const nlohmann::json::exception &e) {
    HIXL_LOGE(PARAM_INVALID, "Parse fabric mem notify failed:%s", e.what());
    return PARAM_INVALID;
  }
}

Status FabricMemControlServer::DequeueNotifies(std::vector<NotifyDesc> &notifies) {
  std::lock_guard<std::mutex> lock(state_->notify_mutex);
  notifies.swap(state_->notify_queue);
  return SUCCESS;
}

Status FabricMemControlServer::HandleDisconnectRequest(const std::shared_ptr<State> &state, int32_t fd,
                                                       int32_t epoll_fd, const std::string &payload) {
  const std::string client_id = ParseClientIdFromConnectPayload(payload);
  HIXL_LOGI("FabricMemControlServer received disconnect request, client_id:%s, fd:%d.", client_id.c_str(), fd);

  nlohmann::json status;
  status["error_code"] = static_cast<uint32_t>(SUCCESS);
  status["error_message"] = "";
  HIXL_CHK_STATUS_RET(SendFabricMemMsg(fd, FabricMemMsgType::kStatus, status.dump()), "Send disconnect status failed.");

  {
    std::lock_guard<std::mutex> lock(state->mutex);
    if (!client_id.empty()) {
      const auto client_it = state->client_id_to_fd.find(client_id);
      if (client_it != state->client_id_to_fd.end()) {
        CloseClientConnection(state, epoll_fd, client_it->second);
      }
    } else if (state->sessions.find(fd) != state->sessions.end()) {
      CloseClientConnection(state, epoll_fd, fd);
    }
    RemoveFdFromEpoll(epoll_fd, fd);
    state->pending_connections.erase(fd);
    if (fd >= 0) {
      (void)close(fd);
    }
  }
  return SUCCESS;
}

Status FabricMemControlClient::Fetch(const std::string &remote_engine, const std::string &channel_id,
                                     int32_t timeout_ms, std::vector<ShareHandleInfo> &share_handles,
                                     int32_t &conn_fd) {
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
  req["channel_id"] = channel_id;
  req["comm_res"] = "";
  req["timeout"] = timeout_ms;
  req["addrs"] = nlohmann::json::array();
  req["share_handles"] = nlohmann::json::array();
  Status ret = SendFabricMemMsg(conn_fd, FabricMemMsgType::kConnect, req.dump(), CtrlSendTimeoutUs(timeout_ms));
  if (ret != SUCCESS) {
    return ret;
  }

  int32_t msg_type = 0;
  std::string payload;
  ret = RecvFabricMemMsg(conn_fd, timeout_ms, msg_type, payload);
  if (ret != SUCCESS) {
    return ret;
  }
  if (msg_type != FabricMemMsgType::kConnect) {
    return PARAM_INVALID;
  }

  ret = ParseShareHandlesFromResponse(payload, share_handles);
  if (ret != SUCCESS) {
    return ret;
  }

  ret = RecvFabricMemMsg(conn_fd, timeout_ms, msg_type, payload);
  if (ret != SUCCESS) {
    return ret;
  }
  HIXL_CHK_BOOL_RET_STATUS(msg_type == FabricMemMsgType::kStatus, PARAM_INVALID,
                           "Unexpected fabric mem response type:%d.", msg_type);
  ret = ParseStatusFromPayload(payload);
  if (ret != SUCCESS) {
    return ret;
  }

  HIXL_DISMISS_GUARD(close_fd);
  HIXL_CHK_STATUS_RET(CtrlMsgPlugin::SetTcpKeepAlive(conn_fd), "Failed to set tcp keep alive, fd:%d.", conn_fd);
  HIXL_LOGI("Fetch received %zu share handles from remote:%s, conn_fd:%d.", share_handles.size(), remote_engine.c_str(),
            conn_fd);
  return SUCCESS;
}

Status FabricMemControlClient::Disconnect(const std::string &remote_engine, const std::string &channel_id,
                                          int32_t timeout_ms, bool from_auto_cleanup) {
  const char *scene_prefix = from_auto_cleanup ? "Auto cleanup disconnect, " : "";
  if (from_auto_cleanup) {
    HIXL_EVENT("FabricMemControlClient disconnect from auto cleanup, remote:%s, channel_id:%s.", remote_engine.c_str(),
               channel_id.c_str());
  }
  int32_t conn_fd = -1;
  const Status connect_ret = ConnectToEngine(remote_engine, timeout_ms, conn_fd);
  if (connect_ret != SUCCESS) {
    HIXL_REPORT_ERR_MSG("E19999",
                        "Call ConnectToEngine(remote_engine, timeout_ms, conn_fd) fail. "
                        "%sconnect remote fabric mem engine failed:%s.",
                        scene_prefix, remote_engine.c_str());
    HIXL_LOGE(connect_ret, "%sconnect remote fabric mem engine failed:%s.", scene_prefix, remote_engine.c_str());
    return connect_ret;
  }
  HIXL_MAKE_GUARD(close_fd, ([conn_fd]() { (void)close(conn_fd); }));

  nlohmann::json req;
  req["channel_id"] = channel_id;
  HIXL_CHK_STATUS_RET(
      SendFabricMemMsg(conn_fd, FabricMemMsgType::kDisconnect, req.dump(), CtrlSendTimeoutUs(timeout_ms)),
      "Send fabric mem disconnect failed.");

  int32_t msg_type = 0;
  std::string payload;
  HIXL_CHK_STATUS_RET(RecvFabricMemMsg(conn_fd, timeout_ms, msg_type, payload),
                      "Recv fabric mem disconnect status failed.");
  HIXL_CHK_BOOL_RET_STATUS(msg_type == FabricMemMsgType::kStatus, PARAM_INVALID,
                           "Unexpected fabric mem disconnect response type:%d.", msg_type);
  HIXL_CHK_STATUS_RET(ParseStatusFromPayload(payload), "Peer failed to process disconnect.");
  return SUCCESS;
}

Status FabricMemControlClient::SendNotify(const std::string &remote_engine, const NotifyDesc &notify,
                                          int32_t timeout_ms) {
  const std::string name = notify.name.GetString();
  const std::string notify_msg = notify.notify_msg.GetString();
  HIXL_CHK_STATUS_RET(ValidateNotifyContent(name, notify_msg), "Invalid fabric mem notify content.");

  int32_t conn_fd = -1;
  HIXL_CHK_STATUS_RET(ConnectToEngine(remote_engine, timeout_ms, conn_fd),
                      "Connect remote fabric mem engine failed:%s.", remote_engine.c_str());
  HIXL_MAKE_GUARD(close_fd, ([conn_fd]() { (void)close(conn_fd); }));
  nlohmann::json payload;
  payload["name"] = name;
  payload["notify_msg"] = notify_msg;
  HIXL_CHK_STATUS_RET(
      SendFabricMemMsg(conn_fd, FabricMemMsgType::kSendNotifyReq, payload.dump(), CtrlSendTimeoutUs(timeout_ms)),
      "Send fabric mem notify failed.");
  return SUCCESS;
}

Status FabricMemControlClient::SendHeartBeat(int32_t fd, uint64_t timeout_us) {
  FabricMemAdxlHeartBeatMsg msg{};
  msg.msg = 'H';
  std::string payload;
  try {
    nlohmann::json j = msg;
    payload = j.dump();
  } catch (const nlohmann::json::exception &e) {
    HIXL_LOGE(PARAM_INVALID, "Failed to serialize heartbeat msg:%s.", e.what());
    return PARAM_INVALID;
  }
  return SendAdxlMsgByProtocol(fd, kHeartBeatMsgType, payload, timeout_us);
}

Status FabricMemControlClient::SendAdxlMsg(int32_t fd, FabricMemAdxlMsgType msg_type, const std::string &payload,
                                           uint64_t timeout_us) {
  return SendAdxlMsgByProtocol(fd, static_cast<int32_t>(msg_type), payload, timeout_us);
}
}  // namespace hixl
