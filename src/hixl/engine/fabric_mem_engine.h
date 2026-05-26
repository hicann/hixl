/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HIXL_SRC_HIXL_ENGINE_FABRIC_MEM_ENGINE_H_
#define HIXL_SRC_HIXL_ENGINE_FABRIC_MEM_ENGINE_H_

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "common/hixl_inner_types.h"
#include "engine.h"
#include "fabric_mem/fabric_mem_config.h"
#include "fabric_mem/fabric_mem_control.h"
#include "fabric_mem/fabric_mem_remote_memory.h"
#include "fabric_mem/fabric_mem_statistic.h"
#include "fabric_mem/fabric_mem_transfer_service.h"
#include "hixl/hixl_types.h"

namespace hixl {
class FabricMemEngine : public hixl::Engine {
 public:
  explicit FabricMemEngine(const AscendString &local_engine) : Engine(local_engine){};

  ~FabricMemEngine() override = default;

  Status Initialize(const std::map<AscendString, AscendString> &options) override;
  void Finalize() override;
  bool IsInitialized() const override;

  Status RegisterMem(const MemDesc &mem, MemType type, MemHandle &mem_handle) override;
  Status DeregisterMem(MemHandle mem_handle) override;
  Status Connect(const AscendString &remote_engine, int32_t timeout_in_millis) override;
  Status Disconnect(const AscendString &remote_engine, int32_t timeout_in_millis) override;
  void Disconnect() override;

  Status TransferSync(const AscendString &remote_engine, TransferOp operation,
                      const std::vector<TransferOpDesc> &op_descs, int32_t timeout_in_millis) override;
  Status TransferAsync(const AscendString &remote_engine, TransferOp operation,
                       const std::vector<TransferOpDesc> &op_descs, const TransferArgs &optional_args,
                       TransferReq &req) override;
  Status GetTransferStatus(const TransferReq &req, TransferStatus &status) override;

  Status SendNotify(const AscendString &remote_engine, const NotifyDesc &notify,
                    int32_t timeout_in_millis = 1000) override;
  Status GetNotifies(std::vector<NotifyDesc> &notifies) override;
  Status RegisterCallbackProcessor(int32_t msg_type, CallbackProcessor processor) override;

 private:
  Status ApplyVirtualMemoryConfig();
  Status InitTransferService();
  Status StartControlServer();
  Status InitFabricMem();
  Status AcquireVirtualMemoryManager();
  void ReleaseVirtualMemoryManager();
  void CleanupFabricMemLocked();
  bool HasConnectionsLocked() const;
  Status BuildTransferContextLocked(const std::string &remote_engine, FabricMemTransferContext &context);
  Status ConnectLocked(const AscendString &remote_engine, int32_t timeout_in_millis);
  Status DisconnectLocked(const AscendString &remote_engine, int32_t timeout_in_millis);
  // Ensures connection to remote without holding mutex_ during network I/O.
  // Uses double-checked locking: quick check under lock, network fetch without lock, install under lock.
  Status EnsureConnected(const AscendString &remote_engine, int32_t timeout_in_millis);
  Status CreateAndRegisterRemoteMemory(const std::vector<ShareHandleInfo> &share_handles,
                                       const std::string &remote);
  void RemoveChannelReqMapLocked(const std::string &remote_engine);

  // Lock hierarchy (must be acquired in this order):
  //   mutex_ -> stream_pool_mutex_ -> channel_2_req_mutex_ -> async_req_mutex_
  //   mutex_ -> share_handle_mutex_
  std::mutex mutex_;
  std::atomic<bool> is_initialized_{false};

  FabricMemConfig fabric_mem_config_;
  FabricMemStatistic fabric_mem_statistic_;
  std::unique_ptr<FabricMemTransferService> fabric_mem_transfer_service_;
  std::unique_ptr<FabricMemControlServer> fabric_mem_control_server_;
  std::unordered_map<std::string, std::unique_ptr<FabricMemRemoteMemory>> fabric_mem_remote_mems_;
  std::unordered_map<std::string, int32_t> keepalive_fds_;
  std::unordered_map<void *, MemInfo> mem_map_;
  std::unordered_map<uint64_t, TransferInfo> req_map_;
  std::atomic<uint64_t> next_req_id_{1U};
  bool has_acquired_virtual_memory_ = false;
  bool auto_connect_{false};
};
}  // namespace hixl

#endif  // HIXL_SRC_HIXL_ENGINE_FABRIC_MEM_ENGINE_H_
