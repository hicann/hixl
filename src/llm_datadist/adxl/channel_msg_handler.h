/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_CHANNEL_MSG_HANDLE_H_
#define CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_CHANNEL_MSG_HANDLE_H_

#include <map>
#include <mutex>
#include <queue>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <optional>
#include <chrono>
#include "channel_manager.h"
#include "common/msg_handler_plugin.h"
#include "segment_table.h"
#include "fabric_mem_transfer_service.h"
#include "adxl_utils.h"

namespace adxl {
enum class ChannelMsgType : int32_t {
  kConnect = 1,
  kDisconnect = 2,
  kStatus = 3,
  kEnd
};

struct AddrInfo {
  uintptr_t start_addr{0};
  uintptr_t end_addr{0};
  MemType mem_type{MemType::MEM_DEVICE};
};

struct ChannelConnectInfo {
  std::string channel_id;
  std::string comm_res;
  std::string comm_name;
  int32_t timeout;
  std::vector<AddrInfo> addrs;
  std::vector<ShareHandleInfo> share_handles;
};

struct ChannelStatus {
  uint32_t error_code;
  std::string error_message;
};

struct ChannelDisconnectInfo {
  std::string channel_id;
};

struct EvictItem {
    std::string channel_id;
    ChannelType channel_type;
    int32_t timeout_ms{1000};
};

struct PendingDisconnectRequest {
    std::condition_variable cv;
    bool received{false};
    RequestDisconnectResp resp;
};

class ChannelMsgHandler {
 public:
  ChannelMsgHandler(const std::string &listen_info, ChannelManager *channel_manager)
      : listen_info_(listen_info),
        channel_manager_(channel_manager),
        device_id_(0),
        listen_port_(-1),
        comm_config_{} {};

  ~ChannelMsgHandler() = default;
  Status Initialize(const std::map<AscendString, AscendString> &options, SegmentTable *segment_table,
                    FabricMemTransferService *fabric_mem_transfer_service);
  void Finalize();

  Status RegisterMem(const MemDesc &mem, MemType type, MemHandle &mem_handle);
  Status DeregisterMem(MemHandle mem_handle);

  Status Connect(const std::string &remote_engine, int32_t timeout_in_millis);
  Status Disconnect(const std::string &remote_engine, int32_t timeout_in_millis);

  const std::string& GetListenInfo() const {
    return listen_info_;
  }

  void SetUserChannelPoolConfig() {
    user_config_channel_pool_ = true;
  }

  void SetHighWaterline(const int32_t high_waterline) {
    high_waterline_ = high_waterline;
  }

  void SetLowWaterline(const int32_t low_waterline) {
    low_waterline_ = low_waterline;
  }

  void SetMaxChannel(const int32_t max_channel) {
    max_channel_ = max_channel;
  }

 private:
  static Status ParseListenInfo(const std::string &listen_info, std::string &listen_ip, int32_t &listen_port);
  Status StartDaemon(const std::string &ip, uint32_t listen_port);
  Status StopDaemon();
  Status CreateChannel(const ChannelInfo &channel_info, bool is_client, const ChannelConnectInfo &peer_channel_info);
  Status ConnectInfoProcess(const ChannelConnectInfo &peer_channel_info,
                            int32_t timeout, bool is_client);
  Status ProcessConnectRequest(int32_t fd, const std::vector<char> &msg, bool &keep_fd);
  Status DisconnectInfoProcess(ChannelType channel_type, const ChannelDisconnectInfo &peer_disconnect_info);
  Status ProcessDisconnectRequest(int32_t fd, const std::vector<char> &msg);
  Status ConnectedProcess(int32_t fd, bool &keep_fd);
  template<typename T>
  static Status SendMsg(int32_t fd, ChannelMsgType msg_type, const T &msg);
  template<typename T>
  static Status RecvMsg(int32_t fd, ChannelMsgType msg_type, T &msg);
  template<typename T>
  static Status Serialize(const T &msg, std::string &msg_str);
  template<typename T>
  static Status Deserialize(const std::vector<char> &msg_str, T &msg);
  Status ParseTrafficClass(const std::map<AscendString, AscendString> &options);
  Status ParseServiceLevel(const std::map<AscendString, AscendString> &options);
  Status DoConnect(const std::string &remote_engine, int32_t timeout_in_millis);
  Status InitChannelPool();

  int32_t GetTotalChannelCount() const;
  bool ShouldTriggerEviction() const;
  Status NotifyEviction();
  Status ProcessEviction(const EvictItem& item);
  Status ResetAllTransferFlags();
  void EvictionLoop();
  std::vector<EvictItem> SelectEvictionCandidates(int32_t need_expire);
  Status StartEvictionThread();
  Status SetupChannelManagerCallbacks();

  Status ProcessServerEviction(const std::string& channel_id, ChannelPtr channel);
  Status ProcessClientEviction(const std::string& channel_id, int32_t timeout_ms);

  int32_t max_channel_{kDefaultMaxChannel};
  int32_t high_waterline_{0};
  int32_t low_waterline_{0};

  std::mutex evict_mutex_;
  std::condition_variable evict_cv_;
  std::queue<EvictItem> evict_queue_;
  std::atomic<bool> stop_eviction_{false};
  std::thread eviction_thread_;
  std::mutex pending_req_mutex_;
  std::map<uint64_t, std::shared_ptr<PendingDisconnectRequest>> pending_disconnect_requests_;
  std::atomic<uint64_t> next_req_id_{1};

  std::string listen_info_;
  ChannelManager *channel_manager_;
  int32_t device_id_;
  std::string local_ip_;
  int32_t listen_port_;
  llm::MsgHandlerPlugin handler_plugin_;

  bool user_config_channel_pool_{false};
  aclrtContext aclrt_context_{nullptr};
  std::mutex mutex_;
  std::map<MemHandle, AddrInfo> handle_to_addr_;

  std::string local_comm_name_;
  std::string local_comm_res_;
  HcclCommConfig comm_config_;

  SegmentTable *segment_table_ = nullptr;
  bool enable_use_fabric_mem_ = false;
  FabricMemTransferService *fabric_mem_transfer_service_ = nullptr;
  uint32_t pid_ = 0;
};
}  // namespace adxl

#endif  // CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_CHANNEL_MSG_HANDLE_H_
