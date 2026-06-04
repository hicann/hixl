/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_FABRIC_MEM_FABRIC_MEM_TRANSFER_SERVICE_H_
#define CANN_HIXL_SRC_HIXL_FABRIC_MEM_FABRIC_MEM_TRANSFER_SERVICE_H_

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <set>
#include <unordered_map>
#include <vector>

#include "fabric_mem/fabric_mem_statistic.h"
#include "fabric_mem/fabric_mem_types.h"
#include "hixl/hixl_types.h"

namespace hixl {
class FabricMemTransferService {
 public:
  FabricMemTransferService() = default;
  ~FabricMemTransferService();
  FabricMemTransferService(const FabricMemTransferService &) = delete;
  FabricMemTransferService &operator=(const FabricMemTransferService &) = delete;
  FabricMemTransferService(FabricMemTransferService &&) = delete;
  FabricMemTransferService &operator=(FabricMemTransferService &&) = delete;

  Status Initialize(int32_t device_id, size_t max_stream_num, size_t task_stream_num,
                    FabricMemStatistic *statistic);
  void Finalize();

  Status RegisterMem(const MemDesc &mem, MemType type, MemHandle &mem_handle);
  Status DeregisterMem(MemHandle mem_handle);

  Status Transfer(const FabricMemTransferContext &context, TransferOp operation,
                  const std::vector<TransferOpDesc> &op_descs, int32_t timeout_in_millis);
  Status TransferAsync(const FabricMemTransferContext &context, TransferOp operation,
                       const std::vector<TransferOpDesc> &op_descs, TransferReq &req);
  Status GetTransferStatus(const FabricMemTransferContext &context, const TransferReq &req, TransferStatus &status);

  std::vector<ShareHandleInfo> GetShareHandles();
  void RemoveChannel(const std::string &channel_id);
  // Atomically look up and remove an async transfer record by req_id,
  // releasing its slot. Returns true if the record was found and removed,
  // false if it had already been consumed (e.g. by RemoveChannel or
  // GetTransferStatus). Use this instead of GetTransferStatus for
  // deterministic cleanup during disconnect races.
  bool CancelAsyncTransfer(TransferReq req);

  static Status MallocMem(MemType type, size_t size, void **ptr);
  static Status FreeMem(void *ptr);

 private:
  enum class AsyncStreamQueryResult { kWaiting, kFailed, kComplete };

  Status InitDevConstOne();
  void FreeDevConstOne();
  Status TryAcquireAsyncSlot(AsyncSlot &slot);
  Status AppendHostFlagCopies(const AsyncSlot &slot) const;
  static bool AllHostFlagsDone(const AsyncSlot &slot);
  static AsyncStreamQueryResult QueryAsyncSlotStreams(const AsyncSlot &slot);
  static Status SynchronizeAsyncSlotStreams(const AsyncSlot &slot);
  Status HandleAsyncStreamQueryFailure(const FabricMemTransferContext &context, uint64_t req_id,
                                       AsyncRecord &async_record, TransferStatus &status);
  void RegisterAsyncTransferRecord(const FabricMemTransferContext &context, TransferReq &req, AsyncSlot &&slot,
                                   const std::chrono::steady_clock::time_point &transfer_start,
                                   const std::chrono::steady_clock::time_point &real_copy_start,
                                   uint64_t transfer_bytes, uint64_t op_desc_count);
  Status CompleteAsyncTransferAndUpdateStats(const FabricMemTransferContext &context, uint64_t req_id,
                                             AsyncRecord &async_record, TransferStatus &status);
  bool TryFastPathComplete(const FabricMemTransferContext &context, uint64_t req_id, AsyncRecord &async_record,
                           TransferStatus &status);
  std::shared_ptr<std::mutex> GetChannelMutex(const std::string &channel_id);
  AsyncStreamQueryResult ResolveAsyncRecord(uint64_t req_id, const AsyncStreamQueryResult &query_result,
                                            AsyncRecord &async_record);
  Status TryAcquireSlotLocked(AsyncSlot &slot);
  static Status CreateSlotStream(aclrtStream &stream);
  static void DestroySlotStreams(std::vector<aclrtStream> &streams, bool abort_streams);
  static void DestroySlotHostFlags(std::vector<void *> &host_flags);
  static void ResetSlotHostFlags(const std::vector<void *> &host_flags);
  static void DestroyCreatedSlotEntry(AsyncSlot &entry);
  Status PopulateSlotStreams(AsyncSlot &entry);
  Status PopulateSlotHostFlags(AsyncSlot &entry);
  Status CreateSlotEntryLocked(AsyncSlot &entry);
  void DestroySlotEntryLocked(AsyncSlot &entry, bool abort_streams);
  Status TryAcquireSlotWithTimeout(AsyncSlot &slot, uint64_t timeout_us);
  static Status ProcessCopyWithAsync(const AsyncSlot &slot, TransferOp operation,
                                     const std::vector<TransferOpDesc> &op_descs);
  Status DoTransfer(const AsyncSlot &slot, const FabricMemTransferContext &context, TransferOp operation,
                    std::vector<TransferOpDesc> &op_descs, std::chrono::steady_clock::time_point &start);
  void ClearReleasedSlotLocked(AsyncSlot &slot);
  bool ReleaseSlotEntryLocked(AsyncSlot &slot, bool destroy_slot);
  void RebuildFreeSlotIndicesLocked();
  void ReleaseSlot(AsyncSlot &slot, bool destroy_slot);
  void RemoveChannelReqRelation(const std::string &channel_id, uint64_t req_id);
  static Status TransOpAddr(uintptr_t old_addr, size_t len,
                            const std::unordered_map<uintptr_t, VaInfo> &new_va_to_old_va, uintptr_t &new_addr);
  Status TransLocalHostOpAddrs(std::vector<TransferOpDesc> &op_descs);
  bool FindLocalHostRegisteredAddrLocked(uintptr_t old_addr, size_t len, uintptr_t &new_addr) const;
  void UpdateStats(const FabricMemTransferContext &context, uint64_t transfer_cost, uint64_t real_copy_cost,
                   uint64_t transfer_bytes, uint64_t op_desc_count) const;
  Status NeedTransLocalAddr(const std::vector<TransferOpDesc> &op_descs, bool &need_trans_local_addr) const;
  void FinalizeShareHandles();
  Status ImportHostMemoryForRegister(const MemDesc &mem, aclrtMemFabricHandle &share_handle,
                                     aclrtDrvMemHandle &imported_pa_handle, uintptr_t &imported_va);

  // Lock hierarchy (must be acquired in this order):
  //   per-channel mutex -> async_req_mutex_ -> channel_2_req_mutex_
  //   share_handle_mutex_ / stream_pool_mutex_ are independent leaf locks
  std::mutex channel_mutex_map_mutex_;
  std::unordered_map<std::string, std::shared_ptr<std::mutex>> channel_mutexes_;
  std::mutex share_handle_mutex_;
  std::unordered_map<aclrtDrvMemHandle, ShareHandleInfo> share_handles_;
  int32_t device_id_{-1};
  size_t max_stream_num_{0};
  size_t task_stream_num_{0};
  size_t max_async_slot_num_{0};
  FabricMemStatistic *statistic_{nullptr};

  std::mutex stream_pool_mutex_;
  std::condition_variable slot_pool_cv_;
  std::vector<AsyncSlot> slot_pool_;
  std::queue<size_t> free_slot_indices_;
  void *dev_const_one_{nullptr};

  std::mutex async_req_mutex_;
  std::unordered_map<uint64_t, AsyncRecord> req_2_async_record_;

  std::mutex channel_2_req_mutex_;
  std::unordered_map<std::string, std::set<uint64_t>> channel_2_req_;

  bool has_host_memory_{false};
};
}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_FABRIC_MEM_FABRIC_MEM_TRANSFER_SERVICE_H_
