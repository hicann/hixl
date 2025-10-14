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
#include "channel_manager.h"
#include "common/msg_handler_plugin.h"
#include "segment_table.h"

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
};

struct ChannelStatus {
  uint32_t error_code;
  std::string error_message;
};

struct ChannelDisconnectInfo {
  std::string channel_id;
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
  Status Initialize(const std::map<AscendString, AscendString> &options, SegmentTable *segment_table);
  void Finalize();

  Status RegisterMem(const MemDesc &mem, MemType type, MemHandle &mem_handle);
  Status DeregisterMem(MemHandle mem_handle);

  Status Connect(const std::string &remote_engine, int32_t timeout_in_millis);
  Status Disconnect(const std::string &remote_engine, int32_t timeout_in_millis);

 private:
  static Status ParseListenInfo(const std::string &listen_info, std::string &listen_ip, int32_t &listen_port);
  Status StartDaemon(uint32_t listen_port);
  Status StopDaemon();
  Status CreateChannel(const ChannelInfo &channel_info, bool is_client, const std::vector<AddrInfo> &remote_addrs);
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

  std::string listen_info_;
  ChannelManager *channel_manager_;
  int32_t device_id_;
  std::string local_ip_;
  int32_t listen_port_;
  llm::MsgHandlerPlugin handler_plugin_;

  std::mutex mutex_;
  std::map<MemHandle, void *> handle_to_addr_;

  std::mutex addr_info_mutex_;
  std::vector<AddrInfo> addr_infos_;

  std::string local_comm_name_;
  std::string local_comm_res_;
  HcclCommConfig comm_config_;

  SegmentTable *segment_table_ = nullptr;
};
}  // namespace adxl

#endif  // CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_CHANNEL_MSG_HANDLE_H_
