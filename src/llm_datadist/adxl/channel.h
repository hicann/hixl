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
#include <atomic>
#include <utility>
#include <chrono>
#include "nlohmann/json.hpp"
#include "acl/acl.h"
#include "adxl/adxl_types.h"
#include "hccl/hccl_adapter.h"
#include "control_msg_handler.h"
#include "adxl/stream_pool.h"

namespace adxl {

struct ShareHandleInfo {
  uintptr_t va_addr;
  size_t len;
  aclrtMemFabricHandle share_handle;
};

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

using AsyncResource = std::pair<aclrtStream, aclrtEvent>;
struct AsyncRecord {
  std::vector<AsyncResource> async_resources;
  std::chrono::steady_clock::time_point real_start;
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
  WAITING_FOR_HEADER,
  WAITING_FOR_BODY
};

class Channel {
 public:
  explicit Channel(ChannelInfo info)
      : channel_info_(std::move(info)) {};
  Status Initialize(bool enable_use_fabric_mem = false);
  Status Finalize();
  std::string GetChannelId() const;
  Status TransferSync(TransferOp operation,
                      const std::vector<TransferOpDesc> &op_descs,
                      int32_t timeout_in_millis);
  Status TransferAsync(TransferOp operation, const std::vector<TransferOpDesc> &op_descs,
                       aclrtStream stream);
  Status TransferAsync(TransferOp operation,
                       const std::vector<TransferOpDesc> &op_descs,
                       const TransferArgs &optional_args,
                       TransferReq &req);
  Status GetTransferStatus(const TransferReq &req, TransferStatus &status);
  Status SetSocketNonBlocking(int32_t fd);
  void StopHeartbeat();
  Status SendControlMsg(const std::function<Status(int32_t fd)> &func);
  Status CommWithFd(const std::function<Status(int32_t)> &func);
  Status SendHeartBeat(const std::function<Status(int32_t)> &func);
  static void SetHeartbeatTimeout(int64_t timeout_in_millis);
  int32_t GetFd() const { return fd_; }
  void UpdateHeartbeatTime();
  bool IsHeartbeatTimeout() const;
  void SetStreamPool(StreamPool *stream_pool);
  StreamPool* GetStreamPool();

  std::mutex &GetTransferMutex();
  
  void GetNotifyMessages(std::vector<NotifyDesc> &notifies);

  Status ImportMem(const std::vector<ShareHandleInfo> &remote_share_handles, int32_t device_id);
  std::unordered_map<uintptr_t, ShareHandleInfo> GetNewVaToOldVa();

  int32_t GetTransferCount() const {
    return transfer_count_.load(std::memory_order_acquire);
  }
  bool IsDisconnecting() const {
    return disconnect_flag_.load(std::memory_order_acquire);
  }
  bool GetHasTransferred() const {
    return has_transfered_.load(std::memory_order_acquire);
  }
  void SetHasTransferred(bool value) {
    has_transfered_.store(value, std::memory_order_release);
  }
  void IncrementTransferCount() {
    transfer_count_++;
  }
  void DecrementTransferCount() {
    transfer_count_--;
  }
  void SetDisconnecting(bool value) {
    disconnect_flag_.store(value, std::memory_order_release);
  }
  Status TransferAsyncWithTimeout(TransferOp operation, const std::vector<TransferOpDesc> &op_descs,
                                  aclrtStream stream, uint64_t timeout);

 private:
  Status ClearResources();
  void ClearNotifyMessages();
  void ClearImportedMem();
  ChannelInfo channel_info_;
  // mutex for fd
  std::mutex mutex_;
  std::atomic<bool> with_heartbeat_{false};
  std::chrono::steady_clock::time_point last_heartbeat_time_;
  static int64_t timeout_in_millis_;

  // mutex for disconnect and transfer synchronize
  std::mutex transfer_mutex_;

  std::atomic<int32_t> transfer_count_{0};
  std::atomic<bool> disconnect_flag_{false};
  std::atomic<bool> has_transfered_{false};

  int32_t fd_ = -1;
  RecvState recv_state_ = RecvState::WAITING_FOR_HEADER;
  std::vector<char> recv_buffer_;
  size_t expected_body_size_ = 0;
  size_t bytes_received_ = 0;

  // lock for push/fetch items from notify_messages_
  std::mutex notify_message_mutex_;
  std::vector<NotifyMsg> notify_messages_;
  
  friend class ChannelManager;
  std::mutex transfer_reqs_mutex_;
  std::unordered_map<uint64_t, AsyncRecord> req_2_async_record_;
  StreamPool *stream_pool_ = nullptr;

  // mutex for va map and pa handlers
  std::mutex va_map_mutex_;
  std::unordered_map<uintptr_t, ShareHandleInfo> new_va_to_old_va_;
  std::vector<aclrtDrvMemHandle> remote_pa_handles_;
  bool enable_use_fabric_mem_ = false;
};
using ChannelPtr = std::shared_ptr<Channel>;
}  // namespace adxl

#endif  // CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_CHANNEL_H_
