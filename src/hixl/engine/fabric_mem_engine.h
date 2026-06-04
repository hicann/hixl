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
#include <condition_variable>
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

#include "acl/acl.h"

namespace hixl {
struct RemoteConnection {
  std::unique_ptr<FabricMemRemoteMemory> remote_memory;
  std::mutex state_mutex;
  std::condition_variable cv;
  uint32_t in_flight{0U};
  bool disconnecting{false};
};

struct FabricMemTransferRequest {
  TransferInfo info;
  std::shared_ptr<RemoteConnection> conn;
  bool release_on_disconnect{false};
};

class FabricMemEngine : public hixl::Engine {
 public:
  explicit FabricMemEngine(const AscendString &local_engine) : Engine(local_engine) {};

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
  Status GetTransferStatus(const GetTransferStatusArgs &args, std::vector<TransferResult> &results) override;

  Status SendNotify(const AscendString &remote_engine, const NotifyDesc &notify,
                    int32_t timeout_in_millis = 1000) override;
  Status GetNotifies(std::vector<NotifyDesc> &notifies) override;
  Status RegisterCallbackProcessor(int32_t msg_type, CallbackProcessor processor) override;

 private:
  class OperationGuard {
   public:
    explicit OperationGuard(FabricMemEngine &engine) : engine_(engine) {
      acquired_ = engine_.TryAcquireOperation(context_);
    }
    ~OperationGuard() {
      if (acquired_) {
        engine_.ReleaseOperation();
      }
    }
    OperationGuard(const OperationGuard &) = delete;
    OperationGuard &operator=(const OperationGuard &) = delete;
    bool Acquired() const {
      return acquired_;
    }
    // ACL context captured under mutex_ at acquisition time. Valid for the whole guard
    // lifetime (OperationGuard blocks Finalize from tearing the context down). May be
    // nullptr if the engine is not initialized; callers that require a context must check.
    aclrtContext Context() const {
      return context_;
    }

   private:
    FabricMemEngine &engine_;
    aclrtContext context_{nullptr};
    bool acquired_{false};
  };

  bool TryAcquireOperation(aclrtContext &ctx);
  void ReleaseOperation();
  Status ApplyVirtualMemoryConfig();
  Status InitTransferService();
  Status StartControlServer();
  Status InitFabricMem();
  Status AcquireVirtualMemoryManager();
  void ReleaseVirtualMemoryManager();
  void CleanupFabricMemLocked();
  Status BuildTransferContext(const RemoteConnection &conn, const std::string &remote_engine,
                              FabricMemTransferContext &context);
  Status AcquireTransferLease(const std::string &remote_engine, std::shared_ptr<RemoteConnection> &conn);
  void ReleaseTransferLease(const std::shared_ptr<RemoteConnection> &conn);
  Status DisconnectRemote(const AscendString &remote_engine, int32_t timeout_in_millis, bool wait_in_flight = true);
  // Ensures connection to remote without holding mutex_ during network I/O.
  Status EnsureConnected(const AscendString &remote_engine, int32_t timeout_in_millis);
  Status CreateAndRegisterRemoteMemory(const std::vector<ShareHandleInfo> &share_handles, const std::string &remote);
  void RemoveChannelReqMapLocked(const std::string &remote_engine);
  Status DisconnectOnTransferError(const AscendString &remote_engine, int32_t timeout_in_millis);
  void ReleasePendingAsyncLeasesLocked(const std::string &remote);
  void RemoveConnectionEntryLocked(const std::string &remote);
  Status BuildTransferServiceContext(const std::shared_ptr<RemoteConnection> &conn, const std::string &remote,
                                     FabricMemTransferContext &context,
                                     std::shared_ptr<FabricMemTransferService> &service);
  // Shared transfer preparation: optional auto-connect, acquire the transfer lease and build the
  // transfer context/service. On build failure the lease is released and, when
  // disconnect_on_build_failure is set, the auto-connected remote is torn down.
  Status PrepareTransfer(const AscendString &remote_engine, int32_t connect_timeout_in_millis,
                         bool disconnect_on_build_failure, std::shared_ptr<RemoteConnection> &conn,
                         FabricMemTransferContext &context, std::shared_ptr<FabricMemTransferService> &service);
  Status CleanupAsyncTransferOnFailure(uint64_t id, const std::shared_ptr<RemoteConnection> &conn,
                                       const std::shared_ptr<FabricMemTransferService> &service, TransferReq new_req,
                                       Status ret, const AscendString &remote_engine);
  Status PreRegisterAndSubmitAsync(const std::shared_ptr<FabricMemTransferService> &transfer_service,
                                   const FabricMemTransferContext &context, TransferOp operation,
                                   const std::vector<TransferOpDesc> &op_descs, const AscendString &remote_engine,
                                   const std::shared_ptr<RemoteConnection> &conn, TransferReq new_req, uint64_t id);
  bool MarkAsyncRequestSubmitted(uint64_t id, uint64_t start_time);
  Status LookupAndBuildTransferContext(const TransferReq &req, FabricMemTransferRequest &transfer_req,
                                       FabricMemTransferContext &context,
                                       std::shared_ptr<FabricMemTransferService> &service, bool &conn_invalid);
  bool EraseRequestAndReleaseLease(uint64_t id, const std::shared_ptr<RemoteConnection> &conn);

  // mutex_: engine lifecycle, mem_map_, remote connection table, keepalive_fds_.
  // connection_mutex_: serializes remote install after network fetch.
  // req_map_mutex_: protects req_map_.
  // RemoteConnection::state_mutex: per-remote transfer/disconnect exclusion.
  std::mutex mutex_;
  std::mutex connection_mutex_;
  std::mutex req_map_mutex_;
  std::condition_variable lifecycle_cv_;
  uint32_t active_operations_{0U};
  bool finalizing_{false};
  std::atomic<bool> is_initialized_{false};

  FabricMemConfig fabric_mem_config_;
  FabricMemStatistic fabric_mem_statistic_;
  std::shared_ptr<FabricMemTransferService> fabric_mem_transfer_service_;
  std::unique_ptr<FabricMemControlServer> fabric_mem_control_server_;
  std::unordered_map<std::string, std::shared_ptr<RemoteConnection>> fabric_mem_remote_mems_;
  std::unordered_map<std::string, int32_t> keepalive_fds_;
  std::unordered_map<void *, MemInfo> mem_map_;
  std::unordered_map<uint64_t, FabricMemTransferRequest> req_map_;
  std::atomic<uint64_t> next_req_id_{1U};
  bool has_acquired_virtual_memory_ = false;
  bool auto_connect_{false};
  int32_t device_id_{-1};
  std::shared_ptr<void> aclrt_context_holder_;
  aclrtContext aclrt_context_{nullptr};
};
}  // namespace hixl

#endif  // HIXL_SRC_HIXL_ENGINE_FABRIC_MEM_ENGINE_H_
