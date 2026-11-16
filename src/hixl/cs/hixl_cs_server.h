/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_CS_HIXL_CS_SERVER_H_
#define CANN_HIXL_SRC_HIXL_CS_HIXL_CS_SERVER_H_

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <thread>
#include <map>
#include <set>
#include <vector>
#include <mutex>
#include <memory>
#include "hixl/hixl_types.h"
#include "common/hixl_utils.h"
#include "endpoint_store.h"
#include "global_config.h"
#include "msg_handler.h"
#include "msg_receiver.h"

namespace hixl {
struct EndpointMemInfo {
  EndpointHandle endpoint_handle;
  MemHandle mem_handle;
};

struct EndpointChannelInfo {
  EndpointHandle endpoint_handle;
  ChannelHandle channel_handle;
};

class HixlCSServer {
 public:
  HixlCSServer(const char *ip, uint32_t port, const GlobalConfig &global_config)
      : ip_(ip), port_(port), global_config_(global_config) {}

  ~HixlCSServer() = default;

  Status Initialize(const EndpointDesc *endpoint_list, uint32_t list_num);
  Status Finalize();
  Status RegisterMem(const char *mem_tag, const CommMem *mem, MemHandle *mem_handle);
  Status DeregisterMem(MemHandle mem_handle);
  Status Listen(uint32_t backlog);
  Status RegProc(CtrlMsgType msg_type, MsgProcessor proc);

 private:
  struct PendingHandshake {
    CtrlMsgPluginPtr connection;
    std::chrono::steady_clock::time_point deadline;
  };
  static constexpr size_t kMaxPendingHandshakes = 128U;

  template <typename T>
  static Status Serialize(const T &msg, std::string &msg_str);
  Status RegisterMsgProcessors();
  Status MatchEndpointMsg(const CtrlMsgPluginPtr &connection, const char *msg, uint64_t msg_len) const;
  Status CreateChannel(const CtrlMsgPluginPtr &connection, const char *msg, uint64_t msg_len);
  bool TrackChannelIfConnected(const CtrlMsgPluginPtr &connection, const EndpointPtr &endpoint, ChannelHandle channel);
  Status DestroyChannel(const CtrlMsgPluginPtr &connection, const char *msg, uint64_t msg_len);
  Status ExportMem(const CtrlMsgPluginPtr &connection, const char *msg, uint64_t msg_len) const;
  Status DoWait();
  Status AcceptClient();
  Status AdvanceHandshake(int32_t fd);
  Status UpdateClientEvents(int32_t fd, uint32_t events) const;
  void HandleClientEvent(int32_t fd, uint32_t events);
  void ExpireHandshakes();
  Status ProClientMsg(const CtrlMsgPluginPtr &connection, std::shared_ptr<MsgReceiver> receiver);
  bool ProcessPendingMsg();
  Status InitTransFinishedFlag();
  Status RegisterHostTransFinishedFlag(const std::vector<EndpointPtr> &host_endpoints);
  Status RegisterDeviceTransFinishedFlag(const std::vector<EndpointPtr> &device_endpoints, bool resolve_notify_addr);
  static Status SendCreateChannelResp(const CtrlMsgPluginPtr &connection, const CreateChannelResp &resp);
  static Status SendMatchEndpointResp(const CtrlMsgPluginPtr &connection, const MatchEndpointResp &resp);
  static Status SendRemoteMemResp(const CtrlMsgPluginPtr &connection, const GetRemoteMemResp &resp);
  static void FreeDeviceMem(void *&ptr);
  void CleanupClient(const CtrlMsgPluginPtr &connection);
  void CloseClientSocket(const CtrlMsgPluginPtr &connection) const;
  void ShutdownAllClients();
  void CloseAllClients();

  // 获取 context 切换 guard，用于对外接口的 context 管理
  std::unique_ptr<hixl::TemporaryRtContext> GetContextGuard() const;

  std::string ip_;
  uint32_t port_ = 0U;
  CtrlMsgPluginPtr listener_connection_;
  int32_t epoll_fd_ = -1;
  std::atomic<bool> listener_running_{false};
  std::thread listener_;

  std::mutex client_mutex_;
  std::map<int32_t, std::shared_ptr<MsgReceiver>> clients_;
  // Owned by the listener thread; Finalize clears it only after joining that thread.
  std::map<int32_t, PendingHandshake> pending_handshakes_;

  std::mutex reg_mutex_;
  std::map<MemHandle, std::vector<EndpointMemInfo>> reg_mems_;

  std::mutex chn_mutex_;
  std::map<CtrlMsgPluginPtr, EndpointChannelInfo> channels_;

  MsgHandler msg_handler_;
  EndpointStore endpoint_store_;
  GlobalConfig global_config_;
  bool configurable_transfer_supported_{false};

  void *host_trans_flag_ = nullptr;  // Host 侧 Flag 内存指针

  void *dev_trans_flag_ = nullptr;  // Device 侧 Flag 内存指针

  int32_t device_id_{-1};  // has_device_ep 时初始化 TransferPool，Finalize 时释放
  // Protected by client_mutex_; retained until cleanup runs or Finalize closes the connection.
  std::set<CtrlMsgPluginPtr> disconnected_clients_;
};
}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_CS_HIXL_CS_SERVER_H_
