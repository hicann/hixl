/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_CS_HOST_ROCE_CHANNEL_H_
#define CANN_HIXL_SRC_HIXL_CS_HOST_ROCE_CHANNEL_H_

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>
#include "cs/channel.h"
#include "cs/host_roce/host_roce_endpoint.h"

namespace hixl {
class HostRoceChannel final : public Channel {
 public:
  explicit HostRoceChannel(Endpoint &endpoint);
  HostRoceChannel(const HostRoceChannel &) = delete;
  HostRoceChannel &operator=(const HostRoceChannel &) = delete;
  HostRoceChannel(HostRoceChannel &&) = delete;
  HostRoceChannel &operator=(HostRoceChannel &&) = delete;
  ~HostRoceChannel() override;

  Status Initialize(const ChannelContext &ctx) override;
  Status StepServerHandshake(const std::vector<uint8_t> &in_payload, std::vector<uint8_t> &out_payload,
                             bool &done) override;
  Status Disconnect() override;
  Status Destroy() override;
  Status RegMem(const char *tag, const CommMem *mem, MemHandle *handle) override;
  Status UnregMem(MemHandle handle) override;
  Status SetRemoteMemInfo(const CommMem &flag) override;
  Status QueryCompleteStatus(void *handle, HixlCompleteStatus *status) override;

  // Handshake state transitions used by StepClientHandshake/StepServerHandshake and unit tests.
  Status SetPeer(const HostRocePeer &peer);
  Status Negotiate(const HostRocePeer &peer);
  Status ConnectPeer();
  Status MarkReady();
  bool MatchesPeer(const HostRocePeer &peer) const;
  HostRocePeer LocalPeer() const;

 protected:
  Status DoConnect(const ChannelDesc &desc, uint32_t timeout_ms) override;
  Status StepClientHandshake(const ChannelDesc &desc, const std::vector<uint8_t> &in_payload,
                             std::vector<uint8_t> &out_payload, bool &done) override;
  Status DoBatchTransferSync(bool read, const HixlOneSideOpDesc *descs, uint32_t count, uint32_t timeout_ms) override;
  Status DoBatchTransferAsync(bool read, const HixlOneSideOpDesc *descs, uint32_t count, void **handle) override;

 private:
  struct Operation {
    HixlOneSideOpDesc desc{};
    std::shared_ptr<HostRoceLocalMemory> local;
    HostRoceMemDesc remote;
    bool fence_before{false};
  };
  struct Request {
    bool read{false};
    std::vector<Operation> ops;
    uint32_t max_descriptors_per_post{0};
    const std::chrono::steady_clock::time_point *deadline{nullptr};
    size_t next{0};
    uint64_t offset{0};
    uint32_t outstanding{0};
  };
  enum class AsyncTaskState : uint8_t {
    kRunningData = 0,
    kPendingFlush,
    kPendingFlag,
    kRunningFlag,
    kWaitingFlagValue,
    kCompleted,
    kFailed,
  };
  struct AsyncTask {
    uint32_t flag_index{0};
    std::unique_ptr<Request> data_request;
    std::vector<std::pair<uint32_t, std::shared_ptr<HostRoceLocalMemory>>> flush_targets;
    size_t next_flush{0};
    HostRoceFlushRequest flush_request;
    std::unique_ptr<Request> flag_request;
    bool has_deadline{false};
    std::chrono::steady_clock::time_point deadline{};
    AsyncTaskState state{AsyncTaskState::kRunningData};
    Status result{SUCCESS};
  };
  // O(1) zero-allocation open-addressing table for in-flight wr_id -> Request* lookup.
  class OutstandingTable {
   public:
    void Reset(size_t capacity);
    void Clear();
    void Insert(uint64_t wr_id, Request *request);
    bool Take(uint64_t wr_id, Request *&request);
    bool Empty() const {
      return count_ == 0U;
    }
    size_t Size() const {
      return count_;
    }

   private:
    struct Slot {
      uint64_t wr_id{0};
      Request *request{nullptr};
    };
    void Grow(size_t min_slots);
    std::vector<Slot> slots_;
    size_t mask_{0};
    size_t count_{0};
  };

  Status Configure(const ChannelDesc &desc, HostRoceConfig &config) const;
  Status DisconnectLocked();
  Status StepClientPrepareSentLocked(const std::vector<uint8_t> &in_payload, std::vector<uint8_t> &out_payload);
  Status StepClientNegotiatingLocked(const std::vector<uint8_t> &in_payload, std::vector<uint8_t> &out_payload);
  Status StepClientReadySentLocked(const std::vector<uint8_t> &in_payload, bool &done);
  Status InitFlagQueue();
  Status ReleaseFlags();
  bool CancellationRequested() const;
  Status RegisterAsyncTaskLocked(std::unique_ptr<Request> data_request,
                                 const std::chrono::steady_clock::time_point *deadline, void *&complete_handle);
  Status SubmitTransfer(bool read, const HixlOneSideOpDesc *descs, uint32_t count,
                        const std::chrono::steady_clock::time_point *deadline, void *&complete);
  Status DriveTransfer(void *complete, bool wait_for_completion);
  Status PrepareOperations(Request &request, uint32_t count, const HixlOneSideOpDesc *descs) const;
  Status BuildBatch(const Request &request, std::vector<HostRoceWorkRequest> &wrs) const;
  static void AdvanceCursor(Request &request, uint32_t size);
  void CommitPosts(Request &request, const std::vector<HostRoceWorkRequest> &wrs, size_t posted);
  Status Post(Request &request);
  Status Poll(size_t *polled_count = nullptr);
  Status DrainReady();
  Status PostPendingLocked();
  bool ProgressLocked();
  static Status CollectFlushTargetsLocked(
      const Request &request, std::vector<std::pair<uint32_t, std::shared_ptr<HostRoceLocalMemory>>> &targets);
  void PollFlagTasksLocked(bool &progressed);
  bool AdvanceRunningDataLocked(AsyncTask &task, bool &progressed);
  bool AdvanceFlushLocked(AsyncTask &task, bool &progressed);
  void AdvanceAsyncTasksLocked(bool &progressed);
  bool TryReapTerminalTaskLocked(std::map<void *, std::unique_ptr<AsyncTask>>::iterator it, Status &ret, bool &failed);
  void HandleSyncDeadlineLocked(const AsyncTask &task);
  void HandleAsyncSubmitDeadlineLocked(const AsyncTask &task);
  void FailAllAsyncTasksLocked(Status error);
  void RemoveFlagTaskLocked(const AsyncTask *task);
  void ReleaseFlagLocked(uint32_t index);
  Status Fail(Status error);
  Status StopQp();
  void RecordTeardownFailure(Status status);

  // Lifecycle takes lifecycle_mutex_ before mutex_. Progress uses only mutex_.
  mutable std::mutex lifecycle_mutex_;
  mutable std::mutex mutex_;
  HostRoceEndpoint *endpoint_{nullptr};
  std::shared_ptr<HostRoceVerbs> verbs_;
  std::shared_ptr<HostRoceDevice> device_;
  std::atomic<bool> closing_{false};
  std::unique_ptr<HostRoceQp> qp_;
  HostRoceConfig config_;
  HostRocePeer peer_;
  enum class HandshakeState : uint8_t {
    kInit = 0,
    kClientPrepareSent,
    kClientNegotiating,
    kClientReadySent,
    kServerPrepared,
    kServerNegotiating,
    kDone,
  };
  HandshakeState handshake_state_{HandshakeState::kInit};
  uint64_t handshake_index_{0U};
  HostRocePeer client_server_feature_{};
  uint32_t negotiate_round_{0U};
  bool peer_set_{false};
  bool negotiated_{false};
  bool connected_{false};
  bool ready_{false};
  Status failure_{SUCCESS};
  Status teardown_failure_{SUCCESS};
  uint64_t next_wr_{1};
  static constexpr size_t kFlagQueueSize = 4096U;
  static constexpr uint64_t kDefaultAsyncSubmitTimeoutMs = 20ULL * 60ULL * 1000ULL;
  uint64_t async_submit_timeout_ms_{kDefaultAsyncSubmitTimeoutMs};
  uint64_t *flag_queue_{nullptr};
  std::shared_ptr<uint64_t> flag_storage_;
  MemHandle flag_mem_handle_{nullptr};
  CommMem remote_trans_flag_{};
  std::vector<uint32_t> available_indices_;
  std::deque<Request *> pending_;
  OutstandingTable outstanding_;
  std::vector<HostRoceWorkRequest> post_wrs_;
  std::vector<HostRoceCompletion> poll_completions_;
  std::map<void *, std::unique_ptr<AsyncTask>> async_handles_;
  std::deque<AsyncTask *> async_queue_;
  std::vector<AsyncTask *> flag_tasks_;
};
}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_CS_HOST_ROCE_CHANNEL_H_
