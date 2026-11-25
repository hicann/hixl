/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "cs/host_roce/host_roce_channel.h"

#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <cstdlib>
#include <exception>
#include <iterator>
#include <new>
#include <thread>
#include "common/hixl_checker.h"
#include "common/scope_guard.h"
#include "cs/global_config.h"
#include "cs/hixl_mem_store.h"
#include "cs/host_roce/host_roce_handshake.h"
#include "cs/host_roce/host_roce_qos.h"

namespace hixl {
namespace {
constexpr uint32_t kMaxQpSl = 7U;
constexpr uint32_t kMaxQpRetryCount = 7U;
constexpr uint32_t kMaxQpTimeout = 31U;
constexpr uint32_t kMaxQpPsn = 0xffffffU;
constexpr uint32_t kMaxQpReadDepth = 255U;
constexpr uint32_t kMinPeerMtu = 1U;
constexpr uint32_t kMaxNegotiateRounds = 4U;
constexpr uint32_t kMaxChunkDescriptorCount = 65535U;
constexpr size_t kMinOutstandingSlots = 64U;
constexpr size_t kMaxPollCqesPerProgressStep = 256U;
constexpr uint64_t kFlagPendingValue = 0ULL;
constexpr uint64_t kFlagCompletedValue = 1ULL;

std::atomic<uintptr_t> g_next_complete_handle{1U};

void *NextCompleteHandle() {
  // Flag slots can be reused immediately, but a late query must still identify the original transfer.
  auto id = g_next_complete_handle.load(std::memory_order_relaxed);
  while (id != UINTPTR_MAX) {
    if (g_next_complete_handle.compare_exchange_weak(id, id + 1U, std::memory_order_relaxed)) {
      return reinterpret_cast<void *>(id);
    }
  }
  return nullptr;
}

using AddressRanges = std::map<uint64_t, uint64_t>;

size_t NextPowerOfTwo(size_t value) {
  size_t size = kMinOutstandingSlots;
  while (size < value && size <= (SIZE_MAX >> 1U)) {
    size <<= 1U;
  }
  return size;
}

bool Overlaps(const AddressRanges &ranges, uint64_t begin, uint64_t size) {
  const auto it = ranges.lower_bound(begin);
  if (it != ranges.end() && it->first < begin + size) {
    return true;
  }
  return it != ranges.begin() && std::prev(it)->second > begin;
}

void RecordRange(AddressRanges &ranges, uint64_t begin, uint64_t size) {
  auto end = begin + size;
  auto it = ranges.lower_bound(begin);
  if (it != ranges.begin() && std::prev(it)->second >= begin) {
    --it;
  }
  while (it != ranges.end() && it->first <= end) {
    begin = std::min(begin, it->first);
    end = std::max(end, it->second);
    it = ranges.erase(it);
  }
  ranges.emplace(begin, end);
}

bool ReadDependency(const HixlOneSideOpDesc &desc, AddressRanges &reads, AddressRanges &writes) {
  // Both spans have already passed the endpoint's overflow-safe registered-range validation.
  const auto local = reinterpret_cast<uint64_t>(desc.local_buf);
  const auto remote = reinterpret_cast<uint64_t>(desc.remote_buf);
  const bool fence =
      Overlaps(writes, local, desc.len) || Overlaps(writes, remote, desc.len) || Overlaps(reads, local, desc.len);
  if (fence) {
    reads.clear();
    writes.clear();
  }
  RecordRange(reads, remote, desc.len);
  RecordRange(writes, local, desc.len);
  return fence;
}
}  // namespace

void HostRoceChannel::OutstandingTable::Reset(size_t capacity) {
  const size_t min_slots = capacity > (SIZE_MAX >> 1U) ? capacity : capacity * 2U;
  const size_t table_size = NextPowerOfTwo(min_slots);
  slots_.assign(table_size, Slot{});
  mask_ = table_size - 1U;
  count_ = 0U;
}

void HostRoceChannel::OutstandingTable::Clear() {
  if (count_ != 0U) {
    std::fill(slots_.begin(), slots_.end(), Slot{});
    count_ = 0U;
  }
}

void HostRoceChannel::OutstandingTable::Grow(size_t min_slots) {
  const size_t table_size = NextPowerOfTwo(min_slots);
  std::vector<Slot> old_slots;
  old_slots.swap(slots_);
  slots_.assign(table_size, Slot{});
  mask_ = table_size - 1U;
  count_ = 0U;
  for (const auto &slot : old_slots) {
    if (slot.wr_id != 0U) {
      Insert(slot.wr_id, slot.request);
    }
  }
}

void HostRoceChannel::OutstandingTable::Insert(uint64_t wr_id, Request *request) {
  if (slots_.empty() || (count_ + 1U) * 2U > slots_.size()) {
    Grow(slots_.empty() ? kMinOutstandingSlots : slots_.size() * 2U);
  }
  size_t idx = static_cast<size_t>(wr_id) & mask_;
  while (slots_[idx].wr_id != 0U) {
    idx = (idx + 1U) & mask_;
  }
  slots_[idx] = Slot{wr_id, request};
  ++count_;
}

bool HostRoceChannel::OutstandingTable::Take(uint64_t wr_id, Request *&request) {
  if (count_ == 0U || wr_id == 0U || slots_.empty()) {
    return false;
  }
  size_t idx = static_cast<size_t>(wr_id) & mask_;
  while (slots_[idx].wr_id != 0U) {
    if (slots_[idx].wr_id == wr_id) {
      request = slots_[idx].request;
      --count_;
      size_t hole = idx;
      size_t cur = (hole + 1U) & mask_;
      while (slots_[cur].wr_id != 0U) {
        const size_t home = static_cast<size_t>(slots_[cur].wr_id) & mask_;
        if (((cur - home) & mask_) >= ((cur - hole) & mask_)) {
          slots_[hole] = slots_[cur];
          hole = cur;
        }
        cur = (cur + 1U) & mask_;
      }
      slots_[hole] = Slot{};
      return true;
    }
    idx = (idx + 1U) & mask_;
  }
  return false;
}

HostRoceChannel::HostRoceChannel(Endpoint &endpoint)
    : endpoint_(endpoint.Capabilities().backend == BackendKind::kHixl && IsHostRoce(endpoint.GetEndpoint())
                    ? static_cast<HostRoceEndpoint *>(&endpoint)
                    : nullptr) {}

HostRoceChannel::~HostRoceChannel() {
  try {
    (void)Destroy();
  } catch (const std::exception &e) {
    HIXL_LOGE(FAILED, "[HostRoce] Channel Destroy caught exception:%s", e.what());
  }
}

Status HostRoceChannel::Initialize(const ChannelContext &ctx) {
  std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
  HIXL_CHK_BOOL_RET_STATUS(ctx.local_endpoint == endpoint_ && endpoint_ != nullptr, PARAM_INVALID,
                           "[HostRoce] Initialize endpoint mismatch");
  HIXL_CHK_STATUS_RET(InitContext(ctx));
  const Status ret = InitFlagQueue();
  if (ret != SUCCESS) {
    std::lock_guard<std::mutex> lock(mutex_);
    initialized_ = false;
    ctx_ = ChannelContext{};
  }
  return ret;
}

Status HostRoceChannel::InitFlagQueue() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (flag_queue_ != nullptr) {
      return SUCCESS;
    }
  }
  std::shared_ptr<uint64_t> flags(static_cast<uint64_t *>(std::calloc(kFlagQueueSize, sizeof(uint64_t))), std::free);
  HIXL_CHECK_NOTNULL(flags);
  CommMem mem{};
  mem.type = COMM_MEM_TYPE_HOST;
  mem.addr = flags.get();
  mem.size = kFlagQueueSize * sizeof(uint64_t);
  MemHandle handle = nullptr;
  HIXL_CHK_STATUS_RET(RegMem(kBuiltinHostTransFlagName, &mem, &handle));
  std::lock_guard<std::mutex> lock(mutex_);
  flag_mem_handle_ = handle;
  flag_storage_ = std::move(flags);
  flag_queue_ = flag_storage_.get();
  for (uint32_t i = 0; i < kFlagQueueSize; ++i) {
    available_indices_.push_back(i);
  }
  return SUCCESS;
}

Status HostRoceChannel::RegMem(const char *tag, const CommMem *mem, MemHandle *handle) {
  HIXL_CHK_STATUS_RET(EnsureInitialized("RegMem"));
  HIXL_CHECK_NOTNULL(handle);
  *handle = nullptr;
  HIXL_CHECK_NOTNULL(mem);
  HIXL_CHK_BOOL_RET_STATUS(
      mem->addr != nullptr && mem->size != 0 && !ctx_.mem_store->CheckMemoryForRegister(false, mem->addr, mem->size),
      PARAM_INVALID, "[HostRoce] Invalid or overlapping local memory, size=%" PRIu64 " bytes", mem->size);
  HIXL_CHK_STATUS_RET(endpoint_->RegisterMem(tag, *mem, *handle));
  ScopeGuard rollback([this, handle] {
    (void)endpoint_->DeregisterMem(*handle);
    *handle = nullptr;
  });
  HIXL_CHK_STATUS_RET(ctx_.mem_store->RecordMemory(false, mem->addr, mem->size, mem->type == COMM_MEM_TYPE_HOST));
  rollback.Dismiss();
  return SUCCESS;
}

Status HostRoceChannel::UnregMem(MemHandle handle) {
  HIXL_CHK_STATUS_RET(EnsureInitialized("UnregMem"));
  HIXL_CHECK_NOTNULL(handle);
  HixlMemDesc desc{};
  HIXL_CHK_STATUS_RET(endpoint_->GetMemDesc(handle, desc));
  HIXL_CHK_STATUS_RET(endpoint_->DeregisterMem(handle));
  return ctx_.mem_store->UnrecordMemory(false, desc.mem.addr);
}

Status HostRoceChannel::SetRemoteMemInfo(const CommMem &flag) {
  HIXL_CHK_STATUS_RET(EnsureInitialized("SetRemoteMemInfo"));
  HIXL_CHK_BOOL_RET_STATUS(flag.addr == nullptr || flag.size >= sizeof(uint64_t), PARAM_INVALID,
                           "[HostRoce] Remote completion flag is too small, size=%" PRIu64 " bytes", flag.size);
  HIXL_CHK_BOOL_RET_STATUS(flag.addr == nullptr || flag.type == COMM_MEM_TYPE_DEVICE, PARAM_INVALID,
                           "[HostRoce] Remote read-after-write flag must be HBM, memory_type=%u",
                           static_cast<uint32_t>(flag.type));
  std::lock_guard<std::mutex> lock(mutex_);
  remote_trans_flag_ = flag;
  return SUCCESS;
}

Status HostRoceChannel::DoConnect(const ChannelDesc &desc, uint32_t timeout_ms) {
  (void)timeout_ms;  // The control handshake supplies the connection deadline.
  std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
  std::lock_guard<std::mutex> lock(mutex_);
  HIXL_CHK_BOOL_RET_STATUS(endpoint_ != nullptr && IsHostRoce(desc.remote_endpoint), PARAM_INVALID,
                           "[HostRoce] Connect requires two native host endpoints");
  HIXL_CHK_STATUS_RET(teardown_failure_, "[HostRoce] Previous channel teardown failed");
  HIXL_CHK_BOOL_RET_STATUS(!closing_ && device_ == nullptr, ALREADY_CONNECTED, "[HostRoce] QP already exists");
  auto verbs = endpoint_->Verbs();
  auto device = endpoint_->Device();
  HIXL_CHECK_NOTNULL(verbs);
  HIXL_CHECK_NOTNULL(device);
  HostRoceConfig next_config{};
  HIXL_CHK_STATUS_RET(Configure(desc, next_config));
  config_ = next_config;
  verbs_ = std::move(verbs);
  device_ = std::move(device);
  const auto created = verbs_->CreateQp(device_, config_, qp_);
  if (created != SUCCESS || qp_ == nullptr || qp_->capacity == 0U) {
    (void)StopQp();
    device_.reset();
    verbs_.reset();
    return created != SUCCESS ? created : FAILED;
  }
  device_->flush_cache.OnChannelConnected();
  outstanding_.Reset(qp_->capacity);
  if (desc.channel_type == ChannelType::kClient && qp_->local.plane_count != 0U) {
    qp_->local.plane = static_cast<uint32_t>(device_->next_plane->fetch_add(1) % qp_->local.plane_count);
  }
  const auto &local = qp_->local;
  HIXL_EVENT(
      "[HostRoce] Prepared local QP, role=%s, qpn=%u, psn=%u, capacity=%u, queue_depth=%u, max_batch=%u, "
      "mtu=%u, read_depth=%u, plane_count=%u, plane=%u",
      desc.channel_type == ChannelType::kClient ? "client" : "server", local.qpn, local.psn, qp_->capacity,
      config_.queue_depth, desc.max_transfer_count_per_batch, local.mtu, local.read_depth, local.plane_count,
      local.plane);
  channel_handle_ = reinterpret_cast<uint64_t>(this);
  return SUCCESS;
}

Status HostRoceChannel::StepClientPrepareSentLocked(const std::vector<uint8_t> &in_payload,
                                                    std::vector<uint8_t> &out_payload) {
  HostRoceHandshake rx;
  HIXL_CHK_STATUS_RET(DecodeHostRoceHandshake(in_payload.data(), in_payload.size(), rx));
  HIXL_CHK_BOOL_RET_STATUS(rx.step == HostRoceHandshakeStep::kPrepare && rx.index == handshake_index_, PARAM_INVALID,
                           "[HostRoce] Unexpected Prepare response");
  HIXL_CHK_STATUS_RET(SetPeer(rx.peer));
  client_server_feature_ = rx.peer;
  HostRoceHandshake tx;
  tx.step = HostRoceHandshakeStep::kNegotiate;
  tx.index = handshake_index_;
  tx.peer = LocalPeer();
  out_payload = EncodeHostRoceHandshake(tx);
  handshake_state_ = HandshakeState::kClientNegotiating;
  return SUCCESS;
}

Status HostRoceChannel::StepClientNegotiatingLocked(const std::vector<uint8_t> &in_payload,
                                                    std::vector<uint8_t> &out_payload) {
  HostRoceHandshake rx;
  HIXL_CHK_STATUS_RET(DecodeHostRoceHandshake(in_payload.data(), in_payload.size(), rx));
  HIXL_CHK_BOOL_RET_STATUS(rx.step == HostRoceHandshakeStep::kNegotiate && rx.index == handshake_index_, PARAM_INVALID,
                           "[HostRoce] Unexpected Negotiate response");
  HIXL_CHK_STATUS_RET(Negotiate(client_server_feature_));
  const auto local = LocalPeer();
  if (!local.more && !rx.peer.more) {
    HIXL_CHK_BOOL_RET_STATUS(local.has_feature == rx.peer.has_feature, FAILED,
                             "[HostRoce] Hyper RoCE fallback is not agreed by both peers");
    HIXL_CHK_STATUS_RET(ConnectPeer());
    HostRoceHandshake tx;
    tx.step = HostRoceHandshakeStep::kReady;
    tx.index = handshake_index_;
    tx.peer = local;
    out_payload = EncodeHostRoceHandshake(tx);
    handshake_state_ = HandshakeState::kClientReadySent;
    return SUCCESS;
  }
  ++negotiate_round_;
  HIXL_CHK_BOOL_RET_STATUS(negotiate_round_ < kMaxNegotiateRounds, UNSUPPORTED,
                           "[HostRoce] Hyper RoCE negotiation exceeded four rounds");
  client_server_feature_ = rx.peer;
  HostRoceHandshake tx;
  tx.step = HostRoceHandshakeStep::kNegotiate;
  tx.index = handshake_index_;
  tx.peer = local;
  out_payload = EncodeHostRoceHandshake(tx);
  return SUCCESS;
}

Status HostRoceChannel::StepClientReadySentLocked(const std::vector<uint8_t> &in_payload, bool &done) {
  HostRoceHandshake rx;
  HIXL_CHK_STATUS_RET(DecodeHostRoceHandshake(in_payload.data(), in_payload.size(), rx));
  HIXL_CHK_BOOL_RET_STATUS(rx.step == HostRoceHandshakeStep::kReady && rx.index == handshake_index_, PARAM_INVALID,
                           "[HostRoce] Unexpected Ready response");
  HIXL_CHK_STATUS_RET(MarkReady());
  handshake_state_ = HandshakeState::kDone;
  done = true;
  return SUCCESS;
}

Status HostRoceChannel::StepClientHandshake(const ChannelDesc &desc, const std::vector<uint8_t> &in_payload,
                                            std::vector<uint8_t> &out_payload, bool &done) {
  std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
  out_payload.clear();
  done = false;
  HIXL_CHK_BOOL_RET_STATUS(!CancellationRequested() && device_ != nullptr, NOT_CONNECTED,
                           "[HostRoce] Channel is not connected for client handshake");
  switch (handshake_state_) {
    case HandshakeState::kInit: {
      HostRoceHandshake tx;
      tx.step = HostRoceHandshakeStep::kPrepare;
      tx.index = desc.channel_index;
      tx.peer = LocalPeer();
      handshake_index_ = desc.channel_index;
      negotiate_round_ = 0U;
      out_payload = EncodeHostRoceHandshake(tx);
      handshake_state_ = HandshakeState::kClientPrepareSent;
      return SUCCESS;
    }
    case HandshakeState::kClientPrepareSent:
      return StepClientPrepareSentLocked(in_payload, out_payload);
    case HandshakeState::kClientNegotiating:
      return StepClientNegotiatingLocked(in_payload, out_payload);
    case HandshakeState::kClientReadySent:
      return StepClientReadySentLocked(in_payload, done);
    default:
      HIXL_LOGE(PARAM_INVALID, "[HostRoce] Invalid client handshake state=%u", static_cast<uint32_t>(handshake_state_));
      return PARAM_INVALID;
  }
}

Status HostRoceChannel::StepServerHandshake(const std::vector<uint8_t> &in_payload, std::vector<uint8_t> &out_payload,
                                            bool &done) {
  std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
  out_payload.clear();
  done = false;
  HIXL_CHK_BOOL_RET_STATUS(!CancellationRequested() && device_ != nullptr, NOT_CONNECTED,
                           "[HostRoce] Channel is not connected for server handshake");
  HostRoceHandshake req;
  HIXL_CHK_STATUS_RET(DecodeHostRoceHandshake(in_payload.data(), in_payload.size(), req));
  if (req.step == HostRoceHandshakeStep::kPrepare) {
    HIXL_CHK_BOOL_RET_STATUS(handshake_state_ == HandshakeState::kInit, ALREADY_CONNECTED,
                             "[HostRoce] Duplicate Prepare request");
    HIXL_CHK_STATUS_RET(SetPeer(req.peer));
    handshake_index_ = req.index;
    negotiate_round_ = 0U;
    handshake_state_ = HandshakeState::kServerPrepared;
    HostRoceHandshake resp = req;
    resp.peer = LocalPeer();
    out_payload = EncodeHostRoceHandshake(resp);
    return SUCCESS;
  }
  const bool active =
      handshake_state_ == HandshakeState::kServerPrepared || handshake_state_ == HandshakeState::kServerNegotiating;
  HIXL_CHK_BOOL_RET_STATUS(active && req.index == handshake_index_, PARAM_INVALID,
                           "[HostRoce] Handshake step out of order, step=%u, state=%u", static_cast<uint32_t>(req.step),
                           static_cast<uint32_t>(handshake_state_));
  if (req.step == HostRoceHandshakeStep::kNegotiate) {
    HIXL_CHK_BOOL_RET_STATUS(negotiate_round_ < kMaxNegotiateRounds, UNSUPPORTED,
                             "[HostRoce] Hyper RoCE server negotiation exceeded four rounds");
    HIXL_CHK_STATUS_RET(Negotiate(req.peer));
    ++negotiate_round_;
    handshake_state_ = HandshakeState::kServerNegotiating;
    HostRoceHandshake resp = req;
    resp.peer = LocalPeer();
    out_payload = EncodeHostRoceHandshake(resp);
    return SUCCESS;
  }
  if (req.step == HostRoceHandshakeStep::kReady) {
    HIXL_CHK_BOOL_RET_STATUS(MatchesPeer(req.peer), PARAM_INVALID, "[HostRoce] Invalid READY request");
    HIXL_CHK_STATUS_RET(ConnectPeer());
    HIXL_CHK_STATUS_RET(MarkReady());
    handshake_state_ = HandshakeState::kDone;
    done = true;
    HostRoceHandshake resp = req;
    resp.peer = LocalPeer();
    out_payload = EncodeHostRoceHandshake(resp);
    return SUCCESS;
  }
  return PARAM_INVALID;
}

Status HostRoceChannel::Configure(const ChannelDesc &desc, HostRoceConfig &config) const {
  HostRoceConfig next{};
  next.tc = desc.tc;
  next.sl = desc.sl;
  if (desc.qos != kQosUnset) {
    uint8_t tc = 0;
    uint8_t sl = 0;
    const Status qos_ret = ApplyHostRoceQos(desc.qos, tc, sl);
    if (qos_ret != SUCCESS) {
      return qos_ret;
    }
    next.tc = tc;
    next.sl = sl;
  }
  next.retry_count = desc.retry_cnt;
  next.timeout = desc.retry_interval;
  HIXL_CHK_BOOL_RET_STATUS(next.sl <= kMaxQpSl && next.retry_count <= kMaxQpRetryCount && next.timeout <= kMaxQpTimeout,
                           PARAM_INVALID, "[HostRoce] Invalid verbs QP config, sl=%u, retry_count=%u, timeout=%u",
                           next.sl, next.retry_count, next.timeout);
  next.queue_depth = kDefaultHostRoceQueueDepth;
  if (desc.channel_type == ChannelType::kServer) {
    next.queue_depth = kMinTransportQueueDepth;
  } else if (desc.configurable_transfer_supported) {
    HIXL_CHK_BOOL_RET_STATUS(desc.max_transfer_count_per_batch <= kMaxTransferCountPerBatch, PARAM_INVALID,
                             "[HostRoce] max_transfer_count_per_batch=%u exceeds max=%u",
                             desc.max_transfer_count_per_batch, kMaxTransferCountPerBatch);
    next.queue_depth = CalculateTransportQueueDepth(desc.max_transfer_count_per_batch);
  }
  config = next;
  return SUCCESS;
}

Status HostRoceChannel::DisconnectLocked() {
  closing_.store(true, std::memory_order_release);
  std::lock_guard<std::mutex> lock(mutex_);
  ready_ = false;
  (void)StopQp();
  outstanding_.Clear();
  pending_.clear();
  FailAllAsyncTasksLocked(NOT_CONNECTED);
  if (device_ != nullptr) {
    RecordTeardownFailure(device_->flush_cache.OnChannelDisconnected());
    device_.reset();
  }
  verbs_.reset();
  connected_ = false;
  peer_set_ = false;
  negotiated_ = false;
  handshake_state_ = HandshakeState::kInit;
  handshake_index_ = 0U;
  client_server_feature_ = HostRocePeer{};
  negotiate_round_ = 0U;
  failure_ = SUCCESS;
  channel_handle_ = 0U;
  closing_.store(false, std::memory_order_release);
  return teardown_failure_;
}

Status HostRoceChannel::Disconnect() {
  std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
  return DisconnectLocked();
}

Status HostRoceChannel::ReleaseFlags() {
  static std::mutex quarantine_mutex;
  static auto *quarantine = new std::map<MemHandle, std::shared_ptr<uint64_t>>();
  MemHandle flag_mem = nullptr;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    async_queue_.clear();
    flag_tasks_.clear();
    async_handles_.clear();
    available_indices_.clear();
    flag_mem = flag_mem_handle_;
  }
  if (flag_mem != nullptr) {
    const Status unreg_ret = UnregMem(flag_mem);
    if (unreg_ret != SUCCESS) {
      // Keep flag_mem_handle_ for retry and retain the backing buffer while its MR remains registered.
      std::shared_ptr<uint64_t> storage;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        storage = flag_storage_;
      }
      if (storage != nullptr) {
        std::lock_guard<std::mutex> q_lock(quarantine_mutex);
        (*quarantine)[flag_mem] = std::move(storage);
      }
      HIXL_LOGE(unreg_ret, "[HostRoce] Flag MR deregistration failed; retaining flag handle for retry, ret=%u",
                static_cast<uint32_t>(unreg_ret));
      return unreg_ret;
    }
    std::lock_guard<std::mutex> q_lock(quarantine_mutex);
    quarantine->erase(flag_mem);
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    flag_mem_handle_ = nullptr;
    flag_storage_.reset();
    flag_queue_ = nullptr;
  }
  return SUCCESS;
}

Status HostRoceChannel::Destroy() {
  std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
  const auto stopped = DisconnectLocked();
  const auto released = ReleaseFlags();
  if (released == SUCCESS) {
    std::lock_guard<std::mutex> lock(mutex_);
    initialized_ = false;
    remote_trans_flag_ = CommMem{};
  }
  return stopped != SUCCESS ? stopped : released;
}

HostRocePeer HostRoceChannel::LocalPeer() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return qp_ != nullptr ? qp_->local : HostRocePeer{};
}

Status HostRoceChannel::SetPeer(const HostRocePeer &peer) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto local = qp_ != nullptr ? qp_->local : HostRocePeer{};
  const bool gid_nonzero = std::any_of(peer.gid.begin(), peer.gid.end(), [](uint8_t x) { return x != 0U; });
  HIXL_CHK_BOOL_RET_STATUS(qp_ != nullptr && !peer_set_ && !connected_ && peer.qpn != 0U && peer.psn <= kMaxQpPsn &&
                               peer.mtu >= kMinPeerMtu && peer.read_depth > 0U && peer.read_depth <= kMaxQpReadDepth &&
                               gid_nonzero && peer.plane_count == local.plane_count,
                           PARAM_INVALID,
                           "[HostRoce] Invalid peer QP/plane capabilities, qp_exists=%d, peer_set=%d, connected=%d, "
                           "local[qpn=%u, psn=%u, mtu=%u, read_depth=%u, plane_count=%u, plane=%u], "
                           "peer[qpn=%u, psn=%u, mtu=%u, read_depth=%u, gid_nonzero=%d, plane_count=%u, plane=%u]",
                           static_cast<int>(qp_ != nullptr), static_cast<int>(peer_set_), static_cast<int>(connected_),
                           local.qpn, local.psn, local.mtu, local.read_depth, local.plane_count, local.plane, peer.qpn,
                           peer.psn, peer.mtu, peer.read_depth, static_cast<int>(gid_nonzero), peer.plane_count,
                           peer.plane);
  if (peer.plane_count != 0U) {
    HIXL_CHK_BOOL_RET_STATUS(
        peer.plane < peer.plane_count && (qp_->local.plane == kHostRoceNoPlane || qp_->local.plane == peer.plane),
        PARAM_INVALID, "[HostRoce] Invalid peer plane=%u, peer_plane_count=%u, local_plane=%u, local_plane_count=%u",
        peer.plane, peer.plane_count, local.plane, local.plane_count);
    qp_->local.plane = peer.plane;
  } else {
    HIXL_CHK_BOOL_RET_STATUS(peer.plane == kHostRoceNoPlane, PARAM_INVALID,
                             "[HostRoce] Unexpected plane on standard NIC");
  }
  peer_ = peer;
  peer_set_ = true;
  return SUCCESS;
}

Status HostRoceChannel::Negotiate(const HostRocePeer &peer) {
  std::lock_guard<std::mutex> lock(mutex_);
  HIXL_CHK_BOOL_RET_STATUS(qp_ != nullptr && peer_set_ && !connected_ && peer.qpn == peer_.qpn &&
                               peer.psn == peer_.psn && peer.gid == peer_.gid && peer.mtu == peer_.mtu &&
                               peer.read_depth == peer_.read_depth && peer.plane_count == peer_.plane_count &&
                               peer.plane == qp_->local.plane,
                           PARAM_INVALID, "[HostRoce] Feature negotiation peer mismatch");
  HostRocePeer local;
  HIXL_CHK_STATUS_RET(verbs_->Negotiate(*qp_, peer, local));
  qp_->local = local;
  peer_ = peer;
  negotiated_ = true;
  return SUCCESS;
}

Status HostRoceChannel::ConnectPeer() {
  std::lock_guard<std::mutex> lock(mutex_);
  HIXL_CHK_BOOL_RET_STATUS(qp_ != nullptr && peer_set_ && negotiated_ && !connected_ && !qp_->local.more, FAILED,
                           "[HostRoce] QP is not ready to connect");
  const auto ret = verbs_->Connect(*qp_, peer_, config_);
  if (ret != SUCCESS) {
    return Fail(ret);
  }
  connected_ = true;
  HIXL_LOGI("[HostRoce] Connected one QP, qpn=%u, plane=%u, plane_count=%u", qp_->local.qpn, qp_->local.plane,
            qp_->local.plane_count);
  return SUCCESS;
}

bool HostRoceChannel::MatchesPeer(const HostRocePeer &peer) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return qp_ != nullptr && peer_set_ && peer.qpn == peer_.qpn && peer.psn == peer_.psn && peer.gid == peer_.gid &&
         peer.mtu == peer_.mtu && peer.read_depth == peer_.read_depth && peer.plane_count == peer_.plane_count &&
         peer.plane == qp_->local.plane && peer.has_feature == qp_->local.has_feature && !peer.more;
}

Status HostRoceChannel::MarkReady() {
  std::lock_guard<std::mutex> lock(mutex_);
  HIXL_CHK_BOOL_RET_STATUS(connected_ && !closing_ && failure_ == SUCCESS, FAILED,
                           "[HostRoce] READY requires a connected QP");
  ready_ = true;
  return SUCCESS;
}

Status HostRoceChannel::StopQp() {
  if (qp_ == nullptr) {
    return SUCCESS;
  }
  const auto ret = verbs_->Stop(*qp_);
  if (ret == SUCCESS) {
    qp_.reset();
    return SUCCESS;
  }
  RecordTeardownFailure(ret);
  // A provider that refuses QP destruction cannot prove that DMA has stopped. Keep its PD/MRs alive.
  struct Quarantine {
    std::unique_ptr<HostRoceQp> qp;
    std::vector<std::shared_ptr<HostRoceMr>> memories;
    std::shared_ptr<uint64_t> flags;
  };
  static std::mutex quarantine_mutex;
  try {
    static auto *quarantine = new std::vector<Quarantine>();
    Quarantine resources;
    {
      std::lock_guard<std::mutex> lock(device_->memory_mutex);
      resources.memories.reserve(device_->registrations.size());
      for (const auto &entry : device_->registrations) {
        if (auto mr = entry.second.lock()) {
          resources.memories.push_back(std::move(mr));
        }
      }
    }
    std::lock_guard<std::mutex> lock(quarantine_mutex);
    quarantine->reserve(quarantine->size() + 1U);
    // Commit non-throwing move only after all container allocations succeed.
    resources.qp = std::move(qp_);
    resources.flags = flag_storage_;
    quarantine->push_back(std::move(resources));
    HIXL_LOGE(ret, "[HostRoce] QP destruction failed; retaining QP/PD/MRs until process exit, ret=%u", ret);
  } catch (const std::exception &e) {
    // Abandon QP destruction intentionally so ~HostRoceQp will not run while DMA may still be active.
    (void)qp_.release();
    (void)new (std::nothrow) std::shared_ptr<uint64_t>(flag_storage_);
    HIXL_LOGE(ret, "[HostRoce] Retain failed QP resources caught exception:%s, ret=%u", e.what(), ret);
  }
  return ret;
}

void HostRoceChannel::RecordTeardownFailure(Status status) {
  if (teardown_failure_ == SUCCESS) {
    teardown_failure_ = status;
  }
}

Status HostRoceChannel::Fail(Status error) {
  if (error == TIMEOUT && qp_ != nullptr) {
    HIXL_LOGE(TIMEOUT,
              "[HostRoce] Transfer deadline expired, qpn=%u, peer_qpn=%u, outstanding_wrs=%zu, "
              "pending_requests=%zu, retry_cnt=%u, timeout_exp=%u",
              qp_->local.qpn, peer_.qpn, outstanding_.Size(), pending_.size(), config_.retry_count, config_.timeout);
  }
  failure_ = error;
  ready_ = false;
  (void)StopQp();
  outstanding_.Clear();
  pending_.clear();
  FailAllAsyncTasksLocked(error);
  return error;
}

Status HostRoceChannel::PrepareOperations(Request &request, uint32_t count, const HixlOneSideOpDesc *descs) const {
  AddressRanges reads;
  AddressRanges writes;
  request.ops.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    Operation op;
    op.desc = descs[i];
    if (op.desc.len != 0U) {
      HIXL_CHK_STATUS_RET(endpoint_->FindLocal(reinterpret_cast<uint64_t>(op.desc.local_buf), op.desc.len, op.local));
      HIXL_CHK_STATUS_RET(
          endpoint_->FindRemote(reinterpret_cast<uint64_t>(op.desc.remote_buf), op.desc.len, op.remote));
      if (request.read) {
        op.fence_before = ReadDependency(op.desc, reads, writes);
      }
    }
    request.ops.push_back(std::move(op));
  }
  return SUCCESS;
}

void HostRoceChannel::AdvanceCursor(Request &request, uint32_t size) {
  request.offset += size;
  while (request.next < request.ops.size() && request.offset == request.ops[request.next].desc.len) {
    ++request.next;
    request.offset = 0U;
  }
}

Status HostRoceChannel::BuildBatch(const Request &request, std::vector<HostRoceWorkRequest> &wrs) const {
  if (outstanding_.Size() >= qp_->capacity) {
    return SUCCESS;
  }
  const auto capacity = qp_->capacity - outstanding_.Size();
  size_t next = request.next;
  uint64_t offset = request.offset;
  // Only force a leading fence on cross-request/flag boundaries; intra-request batches pipeline freely
  // and rely on PrepareOperations's ReadDependency for overlapping Read descriptors.
  bool force_fence = outstanding_.Size() > request.outstanding;
  uint32_t batch_descriptors = 0U;
  wrs.reserve(capacity);
  while (next < request.ops.size() && wrs.size() < capacity) {
    const auto &op = request.ops[next];
    if (offset == op.desc.len) {
      ++next;
      offset = 0U;
      continue;
    }
    if (request.max_descriptors_per_post != 0U && offset == 0U &&
        batch_descriptors >= request.max_descriptors_per_post) {
      break;
    }
    HIXL_CHK_BOOL_RET_STATUS(next_wr_ < UINT64_MAX - wrs.size(), RESOURCE_EXHAUSTED,
                             "[HostRoce] WR identifier exhausted while preparing a batch");
    const auto size = static_cast<uint32_t>(std::min<uint64_t>(op.desc.len - offset, device_->max_message_size));
    HIXL_CHK_BOOL_RET_STATUS(size != 0U, PARAM_INVALID, "[HostRoce] Cannot post a zero-sized data slice");
    const bool tail = size == op.desc.len - offset;
    wrs.push_back({reinterpret_cast<uint64_t>(op.desc.local_buf) + offset,
                   reinterpret_cast<uint64_t>(op.desc.remote_buf) + offset, next_wr_ + wrs.size(), op.local->mr->lkey,
                   op.remote.rkey, size, true, (force_fence || op.fence_before) && offset == 0U});
    offset += size;
    if (tail) {
      force_fence = false;
      ++batch_descriptors;
    }
  }
  while (next < request.ops.size() && offset == request.ops[next].desc.len) {
    ++next;
    offset = 0U;
  }
  if (!wrs.empty() && request.read && next == request.ops.size()) {
    // Fence the final WQE of a Read request so all prior Reads finish before the request closes.
    wrs.back().fence = true;
  }
  return SUCCESS;
}

void HostRoceChannel::CommitPosts(Request &request, const std::vector<HostRoceWorkRequest> &wrs, size_t posted) {
  for (size_t i = 0; i < posted; ++i) {
    outstanding_.Insert(wrs[i].id, &request);
    ++request.outstanding;
    AdvanceCursor(request, wrs[i].size);
  }
  next_wr_ += posted;
}

Status HostRoceChannel::Post(Request &request) {
  AdvanceCursor(request, 0U);
  post_wrs_.clear();
  const auto built = BuildBatch(request, post_wrs_);
  if (built != SUCCESS) {
    return Fail(built);
  }
  if (post_wrs_.empty()) {
    return SUCCESS;
  }
  size_t posted = 0U;
  const auto ret = verbs_->PostBatch(*qp_, request.read, post_wrs_, posted);
  if (!(posted <= post_wrs_.size() && (ret == SUCCESS ? posted == post_wrs_.size() : posted < post_wrs_.size()))) {
    HIXL_LOGE(FAILED, "[HostRoce] Invalid batch post result, ret=%u, posted=%zu, count=%zu", ret, posted,
              post_wrs_.size());
    return Fail(FAILED);
  }
  CommitPosts(request, post_wrs_, posted);
  return ret == SUCCESS || ret == RESOURCE_EXHAUSTED ? SUCCESS : Fail(ret);
}

Status HostRoceChannel::Poll(size_t *polled_count) {
  if (polled_count != nullptr) {
    *polled_count = 0U;
  }
  const auto ret = verbs_->Poll(*qp_, poll_completions_);
  if (ret != SUCCESS) {
    return Fail(ret);
  }
  if (polled_count != nullptr) {
    *polled_count = poll_completions_.size();
  }
  for (const auto &c : poll_completions_) {
    Request *req = nullptr;
    if (c.status != 0U || !outstanding_.Take(c.id, req)) {
      HIXL_LOGE(FAILED,
                "[HostRoce] ibv_poll_cq returned a failed or unexpected completion, qpn=%u, wr_id=%" PRIu64
                ", "
                "wc_status=%u, vendor_err=%u",
                qp_->local.qpn, c.id, c.status, c.vendor_error);
      return Fail(FAILED);
    }
    // Every WR has its own CQE. Reclaim only its entry, including out-of-order read completions.
    --req->outstanding;
  }
  return SUCCESS;
}

Status HostRoceChannel::DrainReady() {
  if (outstanding_.Empty()) {
    return SUCCESS;
  }
  size_t before = 0U;
  do {
    before = outstanding_.Size();
    HIXL_CHK_STATUS_RET(Poll());
  } while (!outstanding_.Empty() && outstanding_.Size() < before);
  return SUCCESS;
}

Status HostRoceChannel::PostPendingLocked() {
  while (!pending_.empty() && outstanding_.Size() < qp_->capacity) {
    if (CancellationRequested()) {
      return Fail(NOT_CONNECTED);
    }
    auto *request = pending_.front();
    if (request->deadline != nullptr && std::chrono::steady_clock::now() >= *request->deadline) {
      const auto ret = DrainReady();
      return ret == SUCCESS ? Fail(TIMEOUT) : ret;
    }
    const auto before = next_wr_;
    HIXL_CHK_STATUS_RET(Post(*request));
    if (request->next == request->ops.size()) {
      pending_.pop_front();
    } else if (before == next_wr_) {
      break;
    }
  }
  return SUCCESS;
}

bool HostRoceChannel::CancellationRequested() const {
  return closing_.load(std::memory_order_acquire);
}

bool HostRoceChannel::ProgressLocked() {
  if (!ready_) {
    return false;
  }
  const uint64_t posted_before = next_wr_;
  size_t total_polled = 0U;
  while (!outstanding_.Empty() && total_polled < kMaxPollCqesPerProgressStep) {
    if (CancellationRequested()) {
      break;
    }
    size_t batch_polled = 0U;
    if (Poll(&batch_polled) != SUCCESS) {
      return true;
    }
    total_polled += batch_polled;
    if (batch_polled < static_cast<size_t>(kHostRocePollBatchSize)) {
      break;
    }
  }
  if (CancellationRequested()) {
    (void)Fail(NOT_CONNECTED);
    return true;
  }
  bool progressed = false;
  AdvanceAsyncTasksLocked(progressed);
  if (ready_ && PostPendingLocked() != SUCCESS) {
    return true;
  }
  if (ready_ && CancellationRequested()) {
    (void)Fail(NOT_CONNECTED);
    return true;
  }
  return total_polled != 0U || next_wr_ != posted_before || progressed;
}

void HostRoceChannel::ReleaseFlagLocked(uint32_t index) {
  __atomic_store_n(&flag_queue_[index], kFlagPendingValue, __ATOMIC_RELAXED);
  available_indices_.push_back(index);
}

void HostRoceChannel::RemoveFlagTaskLocked(const AsyncTask *task) {
  const auto it = std::find(flag_tasks_.begin(), flag_tasks_.end(), task);
  if (it != flag_tasks_.end()) {
    flag_tasks_.erase(it);
  }
}

void HostRoceChannel::FailAllAsyncTasksLocked(Status error) {
  async_queue_.clear();
  flag_tasks_.clear();
  for (auto &entry : async_handles_) {
    auto &task = *entry.second;
    task.data_request.reset();
    if (task.next_flush < task.flush_targets.size()) {
      RecordTeardownFailure(
          device_->flush_cache.Discard(task.flush_targets[task.next_flush].first, task.flush_request));
      task.flush_targets.clear();
    }
    task.flag_request.reset();
    if (task.state != AsyncTaskState::kCompleted && task.state != AsyncTaskState::kFailed) {
      task.result = error;
      task.state = AsyncTaskState::kFailed;
    }
  }
}

Status HostRoceChannel::CollectFlushTargetsLocked(
    const Request &request, std::vector<std::pair<uint32_t, std::shared_ptr<HostRoceLocalMemory>>> &targets) {
  targets.clear();
  if (!request.read) {
    return SUCCESS;
  }
  std::map<uint32_t, std::shared_ptr<HostRoceLocalMemory>> devices;
  for (const auto &op : request.ops) {
    if (op.desc.len == 0U || op.local == nullptr || op.local->memory.type != COMM_MEM_TYPE_DEVICE) {
      continue;
    }
    if (op.local->mr->memory_device == UINT32_MAX) {
      HIXL_LOGE(UNSUPPORTED, "[HostRoce] Cannot resolve the memory device for local HBM flush");
      return UNSUPPORTED;
    }
    devices.emplace(op.local->mr->memory_device, op.local);
  }
  targets.reserve(devices.size());
  for (auto &entry : devices) {
    targets.emplace_back(entry.first, std::move(entry.second));
  }
  return SUCCESS;
}

void HostRoceChannel::PollFlagTasksLocked(bool &progressed) {
  for (auto it = flag_tasks_.begin(); it != flag_tasks_.end();) {
    AsyncTask *task = *it;
    if (task->state == AsyncTaskState::kRunningFlag) {
      if (task->flag_request == nullptr) {
        task->result = failure_ != SUCCESS ? failure_ : NOT_CONNECTED;
        task->state = AsyncTaskState::kFailed;
        it = flag_tasks_.erase(it);
        progressed = true;
        continue;
      }
      const auto &req = *task->flag_request;
      if (req.next == req.ops.size() && req.outstanding == 0U) {
        task->flag_request.reset();
        task->state = AsyncTaskState::kWaitingFlagValue;
        progressed = true;
      }
    }
    if (task->state == AsyncTaskState::kWaitingFlagValue) {
      if (__atomic_load_n(&flag_queue_[task->flag_index], __ATOMIC_ACQUIRE) == kFlagCompletedValue) {
        task->state = AsyncTaskState::kCompleted;
        it = flag_tasks_.erase(it);
        progressed = true;
        continue;
      }
    }
    ++it;
  }
}

bool HostRoceChannel::AdvanceRunningDataLocked(AsyncTask &task, bool &progressed) {
  if (task.data_request == nullptr) {
    return false;
  }
  const auto &req = *task.data_request;
  if (req.next != req.ops.size() || req.outstanding != 0U) {
    return false;
  }
  const Status flush_collect = CollectFlushTargetsLocked(req, task.flush_targets);
  task.data_request.reset();
  if (flush_collect != SUCCESS) {
    (void)Fail(flush_collect);
    return false;
  }
  progressed = true;
  if (!task.flush_targets.empty()) {
    task.next_flush = 0U;
    task.state = AsyncTaskState::kPendingFlush;
  } else {
    task.state = AsyncTaskState::kPendingFlag;
  }
  return true;
}

bool HostRoceChannel::AdvanceFlushLocked(AsyncTask &task, bool &progressed) {
  bool done = false;
  const auto ret = device_->flush_cache.Advance(*verbs_, device_, task.flush_targets[task.next_flush].second->mr,
                                                config_, task.flush_request, done);
  if (ret != SUCCESS) {
    RecordTeardownFailure(task.flush_request.teardown_failure);
    (void)Fail(ret);
    return false;
  }
  if (!done) {
    return false;
  }
  progressed = true;
  task.flush_request = {};
  ++task.next_flush;
  if (task.next_flush == task.flush_targets.size()) {
    task.flush_targets.clear();
    task.state = AsyncTaskState::kPendingFlag;
  }
  return true;
}

void HostRoceChannel::AdvanceAsyncTasksLocked(bool &progressed) {
  PollFlagTasksLocked(progressed);
  while (!async_queue_.empty()) {
    if (CancellationRequested()) {
      (void)Fail(NOT_CONNECTED);
      return;
    }
    auto &task = *async_queue_.front();
    if (task.has_deadline && std::chrono::steady_clock::now() >= task.deadline) {
      HandleSyncDeadlineLocked(task);
      return;
    }
    if (task.state == AsyncTaskState::kRunningData && !AdvanceRunningDataLocked(task, progressed)) {
      return;
    }
    if (task.state == AsyncTaskState::kPendingFlush) {
      if (!AdvanceFlushLocked(task, progressed)) {
        return;
      }
      continue;
    }
    pending_.push_back(task.flag_request.get());
    task.state = AsyncTaskState::kRunningFlag;
    async_queue_.pop_front();
    flag_tasks_.push_back(&task);
    progressed = true;
  }
}

Status HostRoceChannel::RegisterAsyncTaskLocked(std::unique_ptr<Request> data_request,
                                                const std::chrono::steady_clock::time_point *deadline,
                                                void *&complete_handle) {
  HIXL_CHK_BOOL_RET_STATUS(remote_trans_flag_.addr != nullptr, PARAM_INVALID,
                           "[HostRoce] Transfer needs an imported completion flag");
  HIXL_CHK_BOOL_RET_STATUS(!available_indices_.empty(), RESOURCE_EXHAUSTED,
                           "[HostRoce] Completion flag queue is full, query outstanding transfers first");
  const auto index = available_indices_.back();
  available_indices_.pop_back();
  __atomic_store_n(&flag_queue_[index], kFlagPendingValue, __ATOMIC_RELAXED);
  ScopeGuard rollback([this, index] { available_indices_.push_back(index); });

  auto flag_req = std::make_unique<Request>();
  flag_req->read = true;
  const HixlOneSideOpDesc flag_op{remote_trans_flag_.addr, &flag_queue_[index], sizeof(uint64_t)};
  HIXL_CHK_STATUS_RET(PrepareOperations(*flag_req, 1U, &flag_op));
  flag_req->ops.front().fence_before = true;

  auto complete = NextCompleteHandle();
  HIXL_CHK_BOOL_RET_STATUS(complete != nullptr, RESOURCE_EXHAUSTED, "[HostRoce] Completion identifiers exhausted");
  auto task = std::make_unique<AsyncTask>();
  task->flag_index = index;
  task->data_request = std::move(data_request);
  task->flag_request = std::move(flag_req);
  if (deadline != nullptr) {
    task->has_deadline = true;
    task->deadline = *deadline;
    task->data_request->deadline = &task->deadline;
    task->flag_request->deadline = &task->deadline;
  }

  auto *task_raw = task.get();
  async_handles_.emplace(complete, std::move(task));
  async_queue_.push_back(task_raw);
  pending_.push_back(task_raw->data_request.get());
  rollback.Dismiss();
  complete_handle = complete;
  return SUCCESS;
}

Status HostRoceChannel::SubmitTransfer(bool read, const HixlOneSideOpDesc *descs, uint32_t count,
                                       const std::chrono::steady_clock::time_point *deadline, void *&complete) {
  HIXL_CHK_BOOL_RET_STATUS(descs != nullptr && count != 0U, PARAM_INVALID,
                           "[HostRoce] Transfer requires non-empty descriptors");
  std::lock_guard<std::mutex> lock(mutex_);
  if (!ready_ || CancellationRequested()) {
    return failure_ == SUCCESS ? NOT_CONNECTED : failure_;
  }
  const uint32_t batch_size = std::max<uint32_t>(
      1U, std::min<uint32_t>(ctx_.global_config->MaxTransferCountPerBatch(), kMaxChunkDescriptorCount));
  auto data_request = std::make_unique<Request>();
  data_request->read = read;
  data_request->max_descriptors_per_post = batch_size;
  HIXL_CHK_STATUS_RET(PrepareOperations(*data_request, count, descs));
  return RegisterAsyncTaskLocked(std::move(data_request), deadline, complete);
}

Status HostRoceChannel::DoBatchTransferAsync(bool read, const HixlOneSideOpDesc *descs, uint32_t count, void **handle) {
  HIXL_CHECK_NOTNULL(handle);
  *handle = nullptr;
  void *complete = nullptr;
  HIXL_CHK_STATUS_RET(SubmitTransfer(read, descs, count, nullptr, complete));
  HIXL_CHK_STATUS_RET(DriveTransfer(complete, false));
  *handle = complete;
  return SUCCESS;
}

Status HostRoceChannel::QueryCompleteStatus(void *handle, HixlCompleteStatus *status) {
  HIXL_CHK_STATUS_RET(EnsureInitialized("QueryCompleteStatus"));
  HIXL_CHECK_NOTNULL(handle);
  HIXL_CHECK_NOTNULL(status);
  Status ret = SUCCESS;
  bool failed = false;
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = async_handles_.find(handle);
  HIXL_CHK_BOOL_RET_STATUS(it != async_handles_.end(), PARAM_INVALID, "[HostRoce] Unknown host completion handle");
  if (it->second->state != AsyncTaskState::kCompleted && it->second->state != AsyncTaskState::kFailed) {
    (void)ProgressLocked();
    it = async_handles_.find(handle);
  }
  if (!TryReapTerminalTaskLocked(it, ret, failed)) {
    *status = HIXL_COMPLETE_STATUS_WAITING;
    return SUCCESS;
  }
  *status = failed ? HIXL_COMPLETE_STATUS_FAILED : HIXL_COMPLETE_STATUS_COMPLETED;
  if (failed && ret != NOT_CONNECTED) {
    LatchTransferFailure(ret);
  }
  return SUCCESS;
}

bool HostRoceChannel::TryReapTerminalTaskLocked(std::map<void *, std::unique_ptr<AsyncTask>>::iterator it, Status &ret,
                                                bool &failed) {
  if (it == async_handles_.end()) {
    ret = failure_ != SUCCESS ? failure_ : NOT_CONNECTED;
    failed = (ret != SUCCESS);
    return true;
  }
  auto &task = *it->second;
  if (task.state == AsyncTaskState::kWaitingFlagValue &&
      __atomic_load_n(&flag_queue_[task.flag_index], __ATOMIC_ACQUIRE) == kFlagCompletedValue) {
    task.state = AsyncTaskState::kCompleted;
    RemoveFlagTaskLocked(it->second.get());
  }
  if (task.state == AsyncTaskState::kCompleted) {
    ret = SUCCESS;
    failed = false;
    ReleaseFlagLocked(task.flag_index);
    async_handles_.erase(it);
    return true;
  }
  if (task.state == AsyncTaskState::kFailed) {
    ret = task.result != SUCCESS ? task.result : FAILED;
    failed = true;
    ReleaseFlagLocked(task.flag_index);
    async_handles_.erase(it);
    return true;
  }
  return false;
}

void HostRoceChannel::HandleSyncDeadlineLocked(const AsyncTask &task) {
  // Check queued data and flag completions before destroying the QP at the deadline.
  if (DrainReady() != SUCCESS) {
    return;
  }
  bool progressed = false;
  PollFlagTasksLocked(progressed);
  if (task.state != AsyncTaskState::kCompleted && task.state != AsyncTaskState::kFailed) {
    (void)Fail(TIMEOUT);
  }
}

void HostRoceChannel::HandleAsyncSubmitDeadlineLocked(const AsyncTask &task) {
  // Drain any ready completions (and surface queued CQE errors) before timing out async submission.
  if (DrainReady() != SUCCESS || PostPendingLocked() != SUCCESS) {
    return;
  }
  if (task.state != AsyncTaskState::kFailed && task.data_request != nullptr &&
      task.data_request->next < task.data_request->ops.size()) {
    (void)Fail(TIMEOUT);
  }
}

Status HostRoceChannel::DriveTransfer(void *complete, bool wait_for_completion) {
  const auto async_submit_deadline =
      wait_for_completion ? std::chrono::steady_clock::time_point::max()
                          : std::chrono::steady_clock::now() + std::chrono::milliseconds(async_submit_timeout_ms_);
  while (true) {
    bool progressed = false;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      const auto it = async_handles_.find(complete);
      if (it == async_handles_.end()) {
        return NOT_CONNECTED;
      }
      const auto &task = *it->second;
      if (ready_ && CancellationRequested()) {
        (void)Fail(NOT_CONNECTED);
      }
      if (task.state != AsyncTaskState::kCompleted && task.state != AsyncTaskState::kFailed) {
        if (task.has_deadline && std::chrono::steady_clock::now() >= task.deadline) {
          HandleSyncDeadlineLocked(task);
        } else if (!wait_for_completion && std::chrono::steady_clock::now() >= async_submit_deadline) {
          HandleAsyncSubmitDeadlineLocked(task);
        } else {
          progressed = ProgressLocked();
        }
      }
      if (!wait_for_completion && task.state != AsyncTaskState::kFailed &&
          (task.data_request == nullptr || task.data_request->next == task.data_request->ops.size())) {
        return SUCCESS;
      }
      Status ret = SUCCESS;
      bool failed = false;
      if (TryReapTerminalTaskLocked(it, ret, failed)) {
        return ret;
      }
    }
    if (!progressed) {
      std::this_thread::yield();
    }
  }
}

Status HostRoceChannel::DoBatchTransferSync(bool read, const HixlOneSideOpDesc *descs, uint32_t count,
                                            uint32_t timeout_ms) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  void *complete = nullptr;
  HIXL_CHK_STATUS_RET(SubmitTransfer(read, descs, count, &deadline, complete));
  return DriveTransfer(complete, true);
}
}  // namespace hixl
