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
#include <unordered_set>

#include "engine.h"
#include "fabric_mem/fabric_mem_config.h"
#include "fabric_mem/fabric_mem_control.h"
#include "fabric_mem/fabric_mem_memory.h"
#include "fabric_mem/fabric_mem_statistic.h"
#include "fabric_mem/fabric_mem_transfer_service.h"
#include "hixl/hixl_types.h"
#include "hixl_options.h"

#include "acl/acl.h"

namespace hixl {

class FabricMemEngine : public hixl::Engine {
 public:
  explicit FabricMemEngine(const AscendString &local_engine) : Engine(local_engine) {}

  ~FabricMemEngine() override = default;

  Status Initialize(const HixlOptions &options) override;
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

  static void SetKeepaliveCheckIntervalMs(int64_t interval_ms);

 private:
  Status CheckInitialized() const;
  static const std::unordered_set<std::string> kSupportedOptions;
  Status ApplyVirtualMemoryConfig();
  Status InitTransferService();
  Status StartControlServer();
  Status InitFabricMem();
  Status InitializeLocked(const HixlOptions &options, bool &start_keepalive_monitor);
  void CleanupFabricMemLocked();
  Status EnsureAutoConnected(const AscendString &remote_engine);
  void DisconnectOnTransferError(const AscendString &remote_engine);

  // mutex_: coordinates Initialize/Finalize only.
  std::mutex mutex_;
  std::atomic<bool> is_initialized_{false};

  FabricMemConfig fabric_mem_config_;
  FabricMemStatistic fabric_mem_statistic_;
  FabricMemLocalMemory local_memory_;
  std::shared_ptr<FabricMemTransferService> fabric_mem_transfer_service_;
  std::unique_ptr<FabricMemControlServer> fabric_mem_control_server_;

  bool auto_connect_{false};
  int32_t device_id_{-1};
  std::shared_ptr<void> aclrt_context_holder_;
  aclrtContext aclrt_context_{nullptr};
};
}  // namespace hixl

#endif  // HIXL_SRC_HIXL_ENGINE_FABRIC_MEM_ENGINE_H_
