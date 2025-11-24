/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_CHANNEL_H_
#define CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_CHANNEL_H_

#include <mutex>
#include <utility>
#include "nlohmann/json.hpp"
#include "runtime/rt.h"
#include "adxl/adxl_types.h"
#include "adxl_checker.h"
#include "hccl/hccl_adapter.h"
#include "control_msg_handler.h"

namespace adxl {

enum class ChannelType {
  kClient = 0,
  kServer = 1,
};

struct ChannelInfo {
  ChannelType channel_type;
  std::string channel_id;
  uint32_t peer_rank_id;
  uint32_t local_rank_id;
  HcclCommConfig comm_config;
  std::string rank_table;
  std::map<MemHandle, void *> registered_mems;
  HcclComm comm;
  int32_t timeout_sec;
};

class BufferedTransfer {
 public:
  explicit BufferedTransfer(std::function<Status(HcclOneSideOpDesc *descs, uint32_t desc_num)> trans_func);
  Status Put(const std::vector<TransferOpDesc> &op_descs);

 private:
  Status Flush();

  std::vector<HcclOneSideOpDesc> op_descs_;
  std::function<Status(HcclOneSideOpDesc *descs, uint32_t desc_num)> trans_func_;
};

enum class RecvState {
  WAITING_FOR_HEADER,  // 等待接收协议头
  WAITING_FOR_BODY     // 已收到头，等待接收完整数据体
};

class Channel {
 public:
  explicit Channel(ChannelInfo info)
      : channel_info_(std::move(info)) {};
  Status Initialize();
  Status Finalize();
  std::string GetChannelId() const;
  Status TransferSync(TransferOp operation,
                      const std::vector<TransferOpDesc> &op_descs,
                      int32_t timeout_in_millis);
  Status TransferAsync(TransferOp operation, const std::vector<TransferOpDesc> &op_descs,
                       rtStream_t stream);
  Status TransferAsync(TransferOp operation,
                       const std::vector<TransferOpDesc> &op_descs,
                       const TransferArgs &optional_args,
                       std::function<TransferStatus()> &closure);
  Status SetSocketNonBlocking(int32_t fd);
  void StopHeartbeat();
  Status SendControlMsg(const std::function<Status(int32_t fd)> &func);
  Status CommWithFd(const std::function<Status(int32_t)> &func);
  Status SendHeartBeat(const std::function<Status(int32_t)> &func);
  static void SetHeartbeatTimeout(int64_t timeout_in_millis);
  int32_t GetFd() const { return fd_; }
  void UpdateHeartbeatTime();
  bool IsHeartbeatTimeout() const;

  rtStream_t &GetStream();
  std::mutex &GetTransferMutex();

 private:
  ChannelInfo channel_info_;
  rtStream_t stream_ = nullptr;
  std::mutex mutex_;
  std::atomic<bool> with_heartbeat_{false};
  std::chrono::steady_clock::time_point last_heartbeat_time_;
  static int64_t timeout_in_millis_;

  std::mutex transfer_mutex_;

  int32_t fd_ = -1;
  RecvState recv_state_ = RecvState::WAITING_FOR_HEADER;
  std::vector<char> recv_buffer_;
  size_t expected_body_size_ = 0;
  size_t bytes_received_ = 0;
  friend class ChannelManager;
};
using ChannelPtr = std::shared_ptr<Channel>;
}  // namespace adxl

#endif  // CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_CHANNEL_H_
