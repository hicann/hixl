/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_FABRIC_MEM_FABRIC_MEM_CONTROL_H_
#define CANN_HIXL_SRC_HIXL_FABRIC_MEM_FABRIC_MEM_CONTROL_H_

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "fabric_mem/fabric_mem_types.h"
#include "hixl/hixl_types.h"
#include "nlohmann/json.hpp"

namespace hixl {

constexpr uint32_t kFabricMemAdxlMagic = 0xA1B2C3D4U;

struct FabricMemAdxlProtocolHeader {
  uint32_t magic;
  uint64_t body_size;
};

enum class FabricMemAdxlMsgType : int32_t {
  kHeartBeat = 1,
};

struct FabricMemAdxlHeartBeatMsg {
  char msg;
};

inline void to_json(nlohmann::json &j, const FabricMemAdxlHeartBeatMsg &msg) {
  j = nlohmann::json{{"msg", msg.msg}};
}

// FabricMem dedicated message types. TCP transport is fully isolated from other hixl TCP channels.
// Type numbers 1-3 are kept compatible with the old protocol (ChannelMsgType).
struct FabricMemMsgType {
  static constexpr int32_t kConnect = 1;
  static constexpr int32_t kDisconnect = 2;
  static constexpr int32_t kStatus = 3;
  static constexpr int32_t kSendNotifyReq = 4;
};

using FabricMemShareHandleProvider = std::function<Status(std::vector<ShareHandleInfo> &)>;

class FabricMemControlServer {
 public:
  FabricMemControlServer() = default;
  ~FabricMemControlServer();
  FabricMemControlServer(const FabricMemControlServer &) = delete;
  FabricMemControlServer &operator=(const FabricMemControlServer &) = delete;
  FabricMemControlServer(FabricMemControlServer &&) = delete;
  FabricMemControlServer &operator=(FabricMemControlServer &&) = delete;

  Status Start(const std::string &listen_info, FabricMemShareHandleProvider provider,
               bool auto_cleanup_enabled = false);
  void Stop();
  Status DequeueNotifies(std::vector<NotifyDesc> &notifies);
  bool IsListening() const;
  void CheckClientHeartbeatTimeouts();
  static void SetHeartbeatTimeoutMs(int64_t timeout_ms);

 private:
  enum class RecvState { kWaitingHeader, kWaitingBody };
  enum class FabricMemRequestRecvState { kWaitingHeader, kWaitingBody };

  struct ClientSession {
    int32_t fd{-1};
    std::string client_id;
    std::chrono::steady_clock::time_point last_heartbeat_time{};
    bool with_heartbeat{false};
    RecvState recv_state{RecvState::kWaitingHeader};
    std::vector<char> recv_buffer;
    size_t bytes_received{0};
    uint64_t expected_body_size{0};
  };

  struct PendingConnection {
    FabricMemRequestRecvState recv_state{FabricMemRequestRecvState::kWaitingHeader};
    std::vector<char> recv_buffer;
    size_t bytes_received{0};
    uint64_t expected_body_size{0};
  };

  struct State {
    // Guards sessions/client_id_to_fd and the listen/epoll fds. Sends are always issued outside this
    // lock so a slow peer cannot stall Stop() or the keepalive monitor. pending_connections is mutated
    // only by the worker thread (Stop() clears the maps after joining the worker), so it needs no lock.
    std::mutex mutex;
    std::mutex notify_mutex;
    std::atomic<bool> running{false};
    int32_t listen_fd{-1};
    int32_t epoll_fd{-1};
    std::string local_engine_id;
    FabricMemShareHandleProvider provider;
    std::vector<NotifyDesc> notify_queue;
    std::unordered_map<int32_t, ClientSession> sessions;
    std::unordered_map<std::string, int32_t> client_id_to_fd;
    std::unordered_map<int32_t, PendingConnection> pending_connections;
    bool auto_cleanup_enabled{false};
  };

  static void Run(std::shared_ptr<State> state);
  static Status DispatchFabricMemRequest(const std::shared_ptr<State> &state, int32_t fd, int32_t epoll_fd,
                                         int32_t msg_type, const std::string &payload);
  static Status HandleConnectRequest(const std::shared_ptr<State> &state, int32_t fd, int32_t epoll_fd,
                                     const std::string &payload);
  static Status HandleSendNotify(const std::shared_ptr<State> &state, const std::string &payload);
  static Status HandleDisconnectRequest(const std::shared_ptr<State> &state, int32_t fd, int32_t epoll_fd,
                                        const std::string &payload);
  static void HandleClientRead(const std::shared_ptr<State> &state, int32_t epoll_fd, int32_t fd);
  static void HandlePendingRead(const std::shared_ptr<State> &state, int32_t epoll_fd, int32_t fd);
  static Status AppendSessionRecv(ClientSession &session, int32_t fd);
  static Status AppendPendingRecv(PendingConnection &pending, int32_t fd);
  static bool ProcessWaitingHeader(const std::shared_ptr<State> &state, int32_t epoll_fd, int32_t fd,
                                   ClientSession &session);
  static bool ProcessWaitingBody(ClientSession &session);
  static bool ProcessPendingWaitingHeader(const std::shared_ptr<State> &state, int32_t epoll_fd, int32_t fd,
                                          PendingConnection &pending);
  static bool ProcessPendingWaitingBody(const std::shared_ptr<State> &state, int32_t epoll_fd, int32_t fd,
                                        PendingConnection &pending);
  static void DrainAdxlMessages(const std::shared_ptr<State> &state, int32_t epoll_fd, int32_t fd,
                                ClientSession &session);
  static void DrainPendingFabricMemMessages(const std::shared_ptr<State> &state, int32_t epoll_fd, int32_t fd,
                                            PendingConnection &pending);
  static void HandleEpollEvent(const std::shared_ptr<State> &state, int32_t epoll_fd, int32_t listen_fd, int32_t fd);
  static void CloseClientConnection(const std::shared_ptr<State> &state, int32_t epoll_fd, int32_t fd);
  static void EraseClientSessionLocked(const std::shared_ptr<State> &state, int32_t fd);
  static void ClosePendingConnection(const std::shared_ptr<State> &state, int32_t epoll_fd, int32_t fd);
  static void RegisterClientSession(const std::shared_ptr<State> &state, int32_t epoll_fd, int32_t fd,
                                    const std::string &client_id);
  static void RegisterPendingConnection(const std::shared_ptr<State> &state, int32_t epoll_fd, int32_t fd);
  static void CheckClientHeartbeatTimeoutsLocked(const std::shared_ptr<State> &state, int32_t epoll_fd);
  static void ShiftRecvBuffer(std::vector<char> &recv_buffer, size_t &bytes_received, size_t consumed,
                              size_t remaining);
  static bool IsSessionHeartbeatTimeout(const ClientSession &session);

  std::shared_ptr<State> state_{std::make_shared<State>()};
  std::thread worker_;
};

class FabricMemControlClient {
 public:
  static Status Fetch(const std::string &remote_engine, const std::string &channel_id, int32_t timeout_ms,
                      std::vector<ShareHandleInfo> &share_handles, int32_t &conn_fd);
  static Status Disconnect(const std::string &remote_engine, const std::string &channel_id, int32_t timeout_ms,
                         bool from_auto_cleanup = false);
  static Status SendNotify(const std::string &remote_engine, const NotifyDesc &notify, int32_t timeout_ms);
  static Status SendHeartBeat(int32_t fd, uint64_t timeout_us = 3000000ULL);
  static Status SendAdxlMsg(int32_t fd, FabricMemAdxlMsgType msg_type, const std::string &payload, uint64_t timeout_us);
};
}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_FABRIC_MEM_FABRIC_MEM_CONTROL_H_
