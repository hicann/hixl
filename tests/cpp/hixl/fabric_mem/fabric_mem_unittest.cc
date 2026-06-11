/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <securec.h>

#define private public
#include "engine/fabric_mem_engine.h"
#include "engine/hixl_options.h"
#include "fabric_mem/fabric_mem_channel_manager.h"
#include "fabric_mem/fabric_mem_control.h"
#include "fabric_mem/fabric_mem_memory.h"
#include "fabric_mem/fabric_mem_slot_pool.h"
#include "fabric_mem/fabric_mem_statistic.h"
#include "fabric_mem/fabric_mem_transfer_service.h"
#undef private

#include "common/hixl_utils.h"
#include "common/ctrl_msg.h"
#include "common/statistic_utils.h"
#include "depends/ascendcl/src/ascendcl_stub.h"
#include "depends/slog/src/slog_stub.h"
#include "fabric_mem_test_utils.h"
#include "fabric_mem/virtual_memory_manager.h"
#include "nlohmann/json.hpp"

namespace hixl {
namespace {
constexpr char kChannelId[] = "fabric_mem_test_channel";
constexpr char kStatChannelId[] = "fabric_mem_stat_channel";
constexpr uintptr_t kLocalAddr = 0x100000UL;
constexpr uintptr_t kRemoteOldAddr = 0x200000UL;
constexpr uintptr_t kRemoteNewAddr = 0x300000UL;
constexpr uintptr_t kImportedLocalAddr = 0x400000UL;
constexpr size_t kLen = 32U;
constexpr uint32_t kFabricMemMagic = 0xA4B3C2D1;
constexpr int32_t kClientTimeoutMs = 10;
constexpr uint32_t kCaptureLogTimeoutMs = 1000U;

class ScopedRuntimeMock {
 public:
  explicit ScopedRuntimeMock(const std::shared_ptr<llm::AclRuntimeStub> &instance) {
    llm::AclRuntimeStub::SetInstance(instance);
  }

  ~ScopedRuntimeMock() {
    llm::GetAclStubMock().clear();
    llm::AclRuntimeStub::Reset();
  }

  ScopedRuntimeMock(const ScopedRuntimeMock &) = delete;
  ScopedRuntimeMock &operator=(const ScopedRuntimeMock &) = delete;
};

class FabricMemRuntimeStub : public llm::AclRuntimeStub {
 public:
  aclError aclrtSetCurrentContext(aclrtContext context) override {
    ++set_current_context_count_;
    last_set_context_ = context;
    return llm::AclRuntimeStub::aclrtSetCurrentContext(context);
  }

  aclError aclrtGetCurrentContext(aclrtContext *context) override {
    if (get_context_returns_null_) {
      *context = nullptr;
      return ACL_ERROR_NONE;
    }
    return llm::AclRuntimeStub::aclrtGetCurrentContext(context);
  }

  aclError aclrtPointerGetAttributes(const void *ptr, aclrtPtrAttributes *attributes) override {
    (void)ptr;
    if (pointer_attr_error_ != ACL_ERROR_NONE) {
      return pointer_attr_error_;
    }
    attributes->location.type = pointer_is_host_ ? ACL_MEM_LOCATION_TYPE_HOST : ACL_MEM_LOCATION_TYPE_DEVICE;
    return ACL_ERROR_NONE;
  }

  aclError aclrtMemcpyAsync(void *dst, size_t dest_max, const void *src, size_t src_count, aclrtMemcpyKind kind,
                            aclrtStream stream) override {
    ++memcpy_async_count_;
    if (kind == ACL_MEMCPY_DEVICE_TO_HOST) {
      ++host_flag_d2h_count_;
    }
    if (memcpy_async_error_ != ACL_ERROR_NONE && memcpy_async_count_ == memcpy_async_fail_on_count_ &&
        kind == memcpy_async_fail_kind_) {
      return memcpy_async_error_;
    }
    return llm::AclRuntimeStub::aclrtMemcpyAsync(dst, dest_max, src, src_count, kind, stream);
  }

  aclError aclrtStreamQuery(aclrtStream stream, aclrtStreamStatus *status) override {
    ++stream_query_count_;
    if (stream_query_error_ != ACL_ERROR_NONE) {
      return stream_query_error_;
    }
    if (status == nullptr) {
      return ACL_ERROR_INVALID_PARAM;
    }
    if (streams_not_complete_.count(stream) != 0U) {
      *status = ACL_STREAM_STATUS_NOT_READY;
      return ACL_ERROR_NONE;
    }
    return llm::AclRuntimeStub::aclrtStreamQuery(stream, status);
  }

  aclError aclrtSetStreamFailureMode(aclrtStream stream, uint64_t mode) override {
    ++stream_failure_mode_count_;
    last_stream_failure_mode_ = mode;
    (void)stream;
    return ACL_ERROR_NONE;
  }

  aclError aclrtSynchronizeStream(aclrtStream stream) override {
    ++stream_sync_count_;
    streams_not_complete_.erase(stream);
    if (stream_sync_error_ != ACL_ERROR_NONE) {
      return stream_sync_error_;
    }
    return llm::AclRuntimeStub::aclrtSynchronizeStream(stream);
  }

  aclError aclrtSynchronizeStreamWithTimeout(aclrtStream stream, int32_t timeout) override {
    (void)timeout;
    sync_with_timeout_entered_.fetch_add(1U, std::memory_order_release);
    if (block_sync_with_timeout_.load(std::memory_order_acquire)) {
      while (!unblock_sync_with_timeout_.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    }
    ++stream_sync_count_;
    streams_not_complete_.erase(stream);
    if (stream_sync_error_ != ACL_ERROR_NONE) {
      return stream_sync_error_;
    }
    return llm::AclRuntimeStub::aclrtSynchronizeStreamWithTimeout(stream, timeout);
  }

  aclError aclrtStreamAbort(aclrtStream stream) override {
    ++stream_abort_count_;
    return llm::AclRuntimeStub::aclrtStreamAbort(stream);
  }

  aclError aclrtMallocPhysical(aclrtDrvMemHandle *handle, size_t size, const aclrtPhysicalMemProp *prop,
                               uint64_t flags) override {
    (void)size;
    (void)flags;
    ++malloc_physical_count_;
    last_physical_mem_prop_ = *prop;
    *handle = reinterpret_cast<aclrtDrvMemHandle>(new uint8_t[8]);
    return ACL_ERROR_NONE;
  }

  bool pointer_is_host_{false};
  bool get_context_returns_null_{false};
  aclError pointer_attr_error_{ACL_ERROR_NONE};
  aclError memcpy_async_error_{ACL_ERROR_NONE};
  aclrtMemcpyKind memcpy_async_fail_kind_{ACL_MEMCPY_DEVICE_TO_DEVICE};
  size_t memcpy_async_fail_on_count_{0U};
  size_t memcpy_async_count_{0U};
  size_t host_flag_d2h_count_{0U};
  size_t stream_query_count_{0U};
  size_t stream_failure_mode_count_{0U};
  uint64_t last_stream_failure_mode_{0U};
  size_t stream_abort_count_{0U};
  aclError stream_query_error_{ACL_ERROR_NONE};
  aclError stream_sync_error_{ACL_ERROR_NONE};
  size_t stream_sync_count_{0U};
  std::atomic<bool> block_sync_with_timeout_{false};
  std::atomic<bool> unblock_sync_with_timeout_{false};
  std::atomic<uint32_t> sync_with_timeout_entered_{0U};
  std::set<aclrtStream> streams_not_complete_;
  size_t malloc_physical_count_{0U};
  size_t set_current_context_count_{0U};
  aclrtContext last_set_context_{nullptr};
  aclrtPhysicalMemProp last_physical_mem_prop_{};
};

ShareHandleInfo BuildShareHandle(uintptr_t va_addr = kRemoteOldAddr, size_t len = kLen) {
  ShareHandleInfo info{};
  info.va_addr = va_addr;
  info.len = len;
  for (size_t i = 0; i < sizeof(info.share_handle.data); ++i) {
    info.share_handle.data[i] = static_cast<uint8_t>(i + 1U);
  }
  return info;
}

FabricMemTransferContext BuildContext(std::shared_ptr<FabricMemTransferStatisticInfo> stat_info = nullptr) {
  FabricMemTransferContext context;
  context.channel_id = kChannelId;
  context.statistic_channel_id = kStatChannelId;
  context.remote_va_to_old_va.emplace(kRemoteNewAddr, VaInfo{kRemoteOldAddr, kLen * 4U});
  context.stat_info = std::move(stat_info);
  return context;
}

std::vector<TransferOpDesc> BuildOpDescs(uint8_t *local, uint8_t *remote) {
  return {{reinterpret_cast<uintptr_t>(local), reinterpret_cast<uintptr_t>(remote), kLen}};
}

std::vector<TransferOpDesc> BuildTwoOpDescs(uint8_t *local, uint8_t *remote) {
  return {{reinterpret_cast<uintptr_t>(local), reinterpret_cast<uintptr_t>(remote), kLen},
          {reinterpret_cast<uintptr_t>(local + kLen), reinterpret_cast<uintptr_t>(remote + kLen), kLen}};
}

void SendAdxlHeartBeat(int32_t fd) {
  ASSERT_EQ(FabricMemControlClient::SendHeartBeat(fd), SUCCESS);
}

void SendRawAdxlMsg(int32_t fd, uint32_t magic, int32_t msg_type, const std::string &payload = "") {
  const uint64_t body_size = static_cast<uint64_t>(sizeof(msg_type)) + payload.size();
  const FabricMemAdxlProtocolHeader header{magic, body_size};
  ASSERT_EQ(send(fd, &header, sizeof(header), 0), static_cast<ssize_t>(sizeof(header)));
  ASSERT_EQ(send(fd, &msg_type, sizeof(msg_type), 0), static_cast<ssize_t>(sizeof(msg_type)));
  if (!payload.empty()) {
    ASSERT_EQ(send(fd, payload.data(), payload.size(), 0), static_cast<ssize_t>(payload.size()));
  }
}

void DrainSocketInBackground(int32_t fd) {
  std::thread([fd]() {
    char buffer[512];
    while (recv(fd, buffer, sizeof(buffer), 0) > 0) {
    }
  }).detach();
}

bool StartDefaultShareHandleServer(FabricMemControlServer &server, int32_t &port, std::string &remote,
                                   bool auto_cleanup_enabled = true) {
  port = test::AllocateFabricMemTestPort();
  if (port <= 0) {
    return false;
  }
  remote = "127.0.0.1:" + std::to_string(port);
  return server.Start(remote,
                      [](std::vector<ShareHandleInfo> &handles) {
                        handles.emplace_back(BuildShareHandle());
                        return SUCCESS;
                      },
                      auto_cleanup_enabled) == SUCCESS;
}

Status FetchFabricMemClientConn(const std::string &remote, const std::string &channel_id, int32_t &conn_fd) {
  std::vector<ShareHandleInfo> handles;
  return FabricMemControlClient::Fetch(remote, channel_id, kClientTimeoutMs, handles, conn_fd);
}

int32_t RecvRawFabricMemMsg(int32_t fd, std::string &payload) {
  uint32_t magic = 0U;
  EXPECT_EQ(recv(fd, &magic, sizeof(magic), MSG_WAITALL), static_cast<ssize_t>(sizeof(magic)));
  EXPECT_EQ(magic, kFabricMemMagic);
  uint64_t length = 0ULL;
  EXPECT_EQ(recv(fd, &length, sizeof(length), MSG_WAITALL), static_cast<ssize_t>(sizeof(length)));
  int32_t msg_type = 0;
  EXPECT_EQ(recv(fd, &msg_type, sizeof(msg_type), MSG_WAITALL), static_cast<ssize_t>(sizeof(msg_type)));
  const size_t data_len = static_cast<size_t>(length) - sizeof(msg_type);
  payload.resize(data_len);
  if (data_len > 0U) {
    EXPECT_EQ(recv(fd, payload.data(), data_len, MSG_WAITALL), static_cast<ssize_t>(data_len));
  }
  return msg_type;
}

// Registers an async record directly on a channel and routes its request id through the manager so
// that FabricMemTransferService::GetTransferStatus can find it without issuing a real transfer.
void RegisterServiceAsyncRecord(FabricMemTransferService &service, const std::shared_ptr<FabricMemChannel> &channel,
                                const FabricMemTransferContext &context, TransferReq req, AsyncSlot &&slot,
                                uint64_t transfer_bytes, uint64_t op_desc_count, TransferOp op = WRITE) {
  const uint64_t req_id = reinterpret_cast<uintptr_t>(req);
  AsyncRecord record;
  record.slot = std::move(slot);
  const auto start = std::chrono::steady_clock::now();
  record.transfer_start = start;
  record.real_copy_start = start;
  record.transfer_bytes = transfer_bytes;
  record.op_desc_count = op_desc_count;
  record.channel_id = context.channel_id;
  record.statistic_channel_id = context.statistic_channel_id;
  record.stat_info = context.stat_info;
  record.op_type = op;
  {
    std::lock_guard<std::mutex> lock(channel->records_mutex);
    channel->async_records[req_id] = std::move(record);
  }
  service.channel_manager_.AddReqRoute(req_id, channel);
}

std::shared_ptr<FabricMemChannel> AddServiceChannel(FabricMemTransferService &service, const std::string &remote) {
  auto channel = std::make_shared<FabricMemChannel>();
  std::lock_guard<std::mutex> lock(service.channel_manager_.channels_mutex_);
  service.channel_manager_.channels_[remote] = channel;
  return channel;
}

std::shared_ptr<FabricMemChannel> AddMappedServiceChannel(FabricMemTransferService &service, const std::string &remote,
                                                          uint8_t *remote_buf, size_t len) {
  auto channel = std::make_shared<FabricMemChannel>();
  channel->remote_memory = std::make_unique<FabricMemRemoteMemory>();
  EXPECT_EQ(channel->remote_memory->Import({BuildShareHandle(reinterpret_cast<uintptr_t>(remote_buf), len)}, 0),
            SUCCESS);
  std::lock_guard<std::mutex> lock(service.channel_manager_.channels_mutex_);
  service.channel_manager_.channels_[remote] = channel;
  return channel;
}

// Builds a valid base init param; tests tweak individual fields to exercise validation.
FabricMemTransferServiceInitParam MakeServiceInitParam(FabricMemStatistic *statistic,
                                                       FabricMemLocalMemory *local_memory) {
  FabricMemTransferServiceInitParam param;
  param.device_id = 0;
  param.max_stream_num = 4U;
  param.task_stream_num = 1U;
  param.local_engine = "127.0.0.1:0";
  param.statistic = statistic;
  param.local_memory = local_memory;
  return param;
}

FabricMemChannelManagerInitParam MakeManagerInitParam(FabricMemStatistic *statistic, FabricMemSlotPool *slot_pool) {
  FabricMemChannelManagerInitParam param;
  param.local_engine = "127.0.0.1:0";
  param.statistic = statistic;
  param.slot_pool = slot_pool;
  return param;
}

class FabricMemStatisticUTest : public ::testing::Test {
 protected:
  void TearDown() override {
    statistic_.StopPeriodicDump();
  }

  FabricMemStatistic statistic_;
};

class FabricMemTransferServiceUTest : public ::testing::Test {
 protected:
  void SetUp() override {
    runtime_ = std::make_shared<FabricMemRuntimeStub>();
    scoped_runtime_ = std::make_unique<ScopedRuntimeMock>(runtime_);
  }

  void TearDown() override {
    service_.Finalize();
    scoped_runtime_.reset();
    runtime_.reset();
  }

  Status InitService(size_t max_stream, size_t task_stream) {
    auto param = MakeServiceInitParam(&statistic_, &local_memory_);
    param.max_stream_num = max_stream;
    param.task_stream_num = task_stream;
    return service_.Initialize(param);
  }

  std::shared_ptr<FabricMemRuntimeStub> runtime_;
  std::unique_ptr<ScopedRuntimeMock> scoped_runtime_;
  FabricMemStatistic statistic_;
  FabricMemLocalMemory local_memory_;
  FabricMemTransferService service_;
};

class FabricMemSlotPoolUTest : public ::testing::Test {
 protected:
  void SetUp() override {
    runtime_ = std::make_shared<FabricMemRuntimeStub>();
    scoped_runtime_ = std::make_unique<ScopedRuntimeMock>(runtime_);
  }

  void TearDown() override {
    pool_.AbortAndDestroyAll();
    scoped_runtime_.reset();
    runtime_.reset();
  }

  std::shared_ptr<FabricMemRuntimeStub> runtime_;
  std::unique_ptr<ScopedRuntimeMock> scoped_runtime_;
  FabricMemSlotPool pool_;
};

class FabricMemLocalMemoryUTest : public ::testing::Test {
 protected:
  void SetUp() override {
    runtime_ = std::make_shared<FabricMemRuntimeStub>();
    scoped_runtime_ = std::make_unique<ScopedRuntimeMock>(runtime_);
    VirtualMemoryManager::GetInstance().Finalize();
    ASSERT_EQ(VirtualMemoryManager::GetInstance().Initialize(), SUCCESS);
  }

  void TearDown() override {
    local_memory_.Finalize();
    VirtualMemoryManager::GetInstance().Finalize();
    scoped_runtime_.reset();
    runtime_.reset();
  }

  std::shared_ptr<FabricMemRuntimeStub> runtime_;
  std::unique_ptr<ScopedRuntimeMock> scoped_runtime_;
  FabricMemLocalMemory local_memory_;
};

class FabricMemChannelManagerUTest : public ::testing::Test {
 protected:
  void SetUp() override {
    runtime_ = std::make_shared<FabricMemRuntimeStub>();
    scoped_runtime_ = std::make_unique<ScopedRuntimeMock>(runtime_);
    ASSERT_EQ(slot_pool_.Initialize(0, 4U, 1U), SUCCESS);
    ASSERT_EQ(manager_.Initialize(MakeManagerInitParam(&statistic_, &slot_pool_)), SUCCESS);
  }

  void TearDown() override {
    manager_.Finalize();
    slot_pool_.AbortAndDestroyAll();
    scoped_runtime_.reset();
    runtime_.reset();
  }

  std::shared_ptr<FabricMemChannel> AddChannel(const std::string &remote) {
    auto channel = std::make_shared<FabricMemChannel>();
    std::lock_guard<std::mutex> lock(manager_.channels_mutex_);
    manager_.channels_[remote] = channel;
    return channel;
  }

  std::shared_ptr<FabricMemRuntimeStub> runtime_;
  std::unique_ptr<ScopedRuntimeMock> scoped_runtime_;
  FabricMemStatistic statistic_;
  FabricMemSlotPool slot_pool_;
  FabricMemChannelManager manager_;
};

void AttachTestContext(FabricMemEngine &engine) {
  auto ctx_holder = std::make_shared<int>(1);
  engine.aclrt_context_holder_ = std::static_pointer_cast<void>(ctx_holder);
  engine.aclrt_context_ = reinterpret_cast<aclrtContext>(ctx_holder.get());
  engine.is_initialized_ = true;
  if (engine.fabric_mem_transfer_service_ == nullptr) {
    auto service = std::make_shared<FabricMemTransferService>();
    auto param = MakeServiceInitParam(&engine.fabric_mem_statistic_, &engine.local_memory_);
    param.auto_connect = engine.auto_connect_;
    param.aclrt_context = engine.aclrt_context_;
    ASSERT_EQ(service->Initialize(param), SUCCESS);
    engine.fabric_mem_transfer_service_ = service;
  }
}

FabricMemChannelManager &EngineManager(FabricMemEngine &engine) {
  return engine.fabric_mem_transfer_service_->channel_manager_;
}

std::shared_ptr<FabricMemChannel> AddEngineChannel(FabricMemEngine &engine, const std::string &remote) {
  auto channel = std::make_shared<FabricMemChannel>();
  std::lock_guard<std::mutex> lock(EngineManager(engine).channels_mutex_);
  EngineManager(engine).channels_[remote] = channel;
  return channel;
}

constexpr char kConfigEngineLocalId[] = "127.0.0.1:28000";
constexpr char kConfigRemoteEngineId[] = "127.0.0.1:28001";
constexpr size_t k1GB = 1024UL * 1024UL * 1024UL;

std::map<AscendString, AscendString> BuildFabricMemOptions() {
  std::map<AscendString, AscendString> options;
  options[OPTION_ENABLE_USE_FABRIC_MEM] = AscendString("1");
  return options;
}

Status InitEngineWithOptions(FabricMemEngine &engine, const std::map<AscendString, AscendString> &raw_options) {
  HixlOptions parsed;
  HIXL_CHK_STATUS_RET(HixlOptions::Parse(raw_options, parsed), "Failed to parse options");
  return engine.Initialize(parsed);
}

class FabricMemEngineInitUTest : public ::testing::Test {
 protected:
  void SetUp() override {
    runtime_ = std::make_shared<FabricMemRuntimeStub>();
    scoped_runtime_ = std::make_unique<ScopedRuntimeMock>(runtime_);
  }

  void TearDown() override {
    scoped_runtime_.reset();
    runtime_.reset();
  }

  std::shared_ptr<FabricMemRuntimeStub> runtime_;
  std::unique_ptr<ScopedRuntimeMock> scoped_runtime_;
};
}  // namespace

TEST_F(FabricMemStatisticUTest, SnapshotDumpRemoveAndPeriodicDump) {
  EXPECT_EQ(FabricMemStatistic::GetClientStatisticChannelId(kChannelId), std::string("client:") + kChannelId);
  EXPECT_EQ(FabricMemStatistic::GetServerStatisticChannelId(kChannelId), std::string("server:") + kChannelId);

  statistic_.RegisterChannel(kChannelId);
  auto empty_snapshot = statistic_.GetSnapshot("missing");
  EXPECT_EQ(empty_snapshot.transfer.times, 0UL);
  statistic_.Dump();

  statistic_.UpdateCosts(kChannelId, 10U, 4U, 4096U, 2U);
  statistic_.UpdateCosts(kChannelId, 30U, 6U, 2048U, 1U);
  const auto snapshot = statistic_.GetSnapshot(kChannelId);
  EXPECT_EQ(snapshot.transfer.times, 2UL);
  EXPECT_EQ(snapshot.transfer.max_cost, 30UL);
  EXPECT_EQ(snapshot.transfer.total_cost, 40UL);
  EXPECT_EQ(snapshot.real_copy.max_cost, 6UL);
  EXPECT_EQ(snapshot.total_bytes, 6144UL);
  EXPECT_EQ(snapshot.total_op_desc_count, 3UL);
  statistic_.Dump();

  EXPECT_EQ(statistic_.StartPeriodicDump(), SUCCESS);
  EXPECT_EQ(statistic_.StartPeriodicDump(), SUCCESS);
  statistic_.StopPeriodicDump();

  statistic_.RemoveStatisticChannel(kChannelId);
  EXPECT_EQ(statistic_.GetSnapshot(kChannelId).transfer.times, 0UL);
}

TEST_F(FabricMemStatisticUTest, UpdateCostsDirectResetsAfterThreshold) {
  FabricMemTransferStatisticInfo info;
  for (uint64_t i = 0; i <= statistic::kResetTimes; ++i) {
    statistic_.UpdateCostsDirect(info, 1U, 1U, 128U, 1U);
  }

  EXPECT_EQ(info.transfer.times.load(std::memory_order_relaxed), 0UL);
  EXPECT_EQ(info.real_copy.times.load(std::memory_order_relaxed), 0UL);
  EXPECT_EQ(info.total_bytes.load(std::memory_order_relaxed), 0UL);
  EXPECT_EQ(info.total_op_desc_count.load(std::memory_order_relaxed), 0UL);
}

TEST(FabricMemControlUTest, StartRejectsEmptyProviderAndAcceptsDisabledPort) {
  FabricMemControlServer server;
  EXPECT_EQ(server.Start("127.0.0.1:0", nullptr), PARAM_INVALID);
  EXPECT_EQ(server.Start("127.0.0.1:0",
                         [](std::vector<ShareHandleInfo> &handles) {
                           handles.emplace_back(BuildShareHandle());
                           return SUCCESS;
                         }),
            SUCCESS);
  server.Stop();
}

TEST(FabricMemControlUTest, ServerPrivateHandlersSerializeResponses) {
  FabricMemControlServer server;
  server.state_->provider = [](std::vector<ShareHandleInfo> &handles) {
    handles.emplace_back(BuildShareHandle(0x1234UL, 64U));
    return FAILED;
  };

  EXPECT_EQ(server.HandleSendNotify(server.state_, "{"), PARAM_INVALID);
  EXPECT_EQ(server.HandleSendNotify(server.state_, R"({"name":"n0","notify_msg":"m0"})"), SUCCESS);
  std::vector<NotifyDesc> notifies;
  EXPECT_EQ(server.DequeueNotifies(notifies), SUCCESS);
  ASSERT_EQ(notifies.size(), 1U);
  EXPECT_STREQ(notifies[0].name.GetString(), "n0");
  EXPECT_STREQ(notifies[0].notify_msg.GetString(), "m0");

  int32_t fds[2] = {-1, -1};
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  const int32_t epoll_fd = epoll_create1(0);
  ASSERT_GE(epoll_fd, 0);
  const std::string connect_payload =
      R"({"channel_id":"client_a","comm_res":"","timeout":10,"addrs":[],"share_handles":[]})";
  EXPECT_EQ(server.HandleConnectRequest(server.state_, fds[0], epoll_fd, connect_payload), FAILED);
  std::string payload;
  EXPECT_EQ(RecvRawFabricMemMsg(fds[1], payload), FabricMemMsgType::kConnect);
  const auto json = nlohmann::json::parse(payload);
  EXPECT_EQ(json.at("share_handles").size(), 1U);
  EXPECT_EQ(json.at("channel_id").get<std::string>(), "client_a");
  (void)close(fds[0]);
  (void)close(fds[1]);
  (void)close(epoll_fd);
}

TEST(FabricMemControlUTest, ClientFetchNotifyRoundTrip) {
  const int32_t port = test::AllocateFabricMemTestPort();
  ASSERT_GT(port, 0);
  FabricMemControlServer server;
  ASSERT_EQ(server.Start("127.0.0.1:" + std::to_string(port),
                         [](std::vector<ShareHandleInfo> &handles) {
                           handles.emplace_back(BuildShareHandle(0x3456UL, 256U));
                           return SUCCESS;
                         }),
            SUCCESS);

  const std::string remote = "127.0.0.1:" + std::to_string(port);
  std::vector<ShareHandleInfo> handles;
  int32_t conn_fd = -1;
  EXPECT_EQ(FabricMemControlClient::Fetch(remote, "client_engine", kClientTimeoutMs, handles, conn_fd), SUCCESS);
  EXPECT_GE(conn_fd, 0);
  ASSERT_EQ(handles.size(), 1U);
  EXPECT_EQ(handles[0].va_addr, 0x3456UL);
  EXPECT_EQ(handles[0].len, 256U);
  EXPECT_EQ(handles[0].share_handle.data[0], 1U);

  NotifyDesc notify;
  notify.name = AscendString("notify");
  notify.notify_msg = AscendString("payload");
  EXPECT_EQ(FabricMemControlClient::SendNotify(remote, notify, kClientTimeoutMs), SUCCESS);

  std::vector<NotifyDesc> notifies;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
  while (notifies.empty() && std::chrono::steady_clock::now() < deadline) {
    EXPECT_EQ(server.DequeueNotifies(notifies), SUCCESS);
    if (notifies.empty()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
  ASSERT_EQ(notifies.size(), 1U);
  EXPECT_STREQ(notifies[0].name.GetString(), "notify");
  EXPECT_STREQ(notifies[0].notify_msg.GetString(), "payload");
  server.Stop();
}

TEST(FabricMemControlUTest, StopWhileClientConnectingDoesNotHang) {
  const int32_t port = test::AllocateFabricMemTestPort();
  ASSERT_GT(port, 0);
  const std::string remote = "127.0.0.1:" + std::to_string(port);
  FabricMemControlServer server;
  ASSERT_EQ(server.Start(remote,
                         [](std::vector<ShareHandleInfo> &handles) {
                           handles.emplace_back(BuildShareHandle());
                           return SUCCESS;
                         }),
            SUCCESS);

  std::atomic<bool> client_running{true};
  std::thread client([&] {
    while (client_running.load(std::memory_order_relaxed)) {
      std::vector<ShareHandleInfo> handles;
      int32_t conn_fd = -1;
      (void)FabricMemControlClient::Fetch(remote, "client_engine", 50, handles, conn_fd);
      if (conn_fd >= 0) {
        (void)close(conn_fd);
      }
    }
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  client_running.store(false, std::memory_order_relaxed);
  server.Stop();
  client.join();
}

TEST(FabricMemControlUTest, HandleDisconnectRequestSendsStatus) {
  FabricMemControlServer server;
  int32_t session_fds[2] = {-1, -1};
  int32_t request_fds[2] = {-1, -1};
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, session_fds), 0);
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, request_fds), 0);
  const int32_t epoll_fd = epoll_create1(0);
  ASSERT_GE(epoll_fd, 0);
  FabricMemControlServer::ClientSession session;
  session.fd = session_fds[0];
  session.client_id = "client_a";
  session.with_heartbeat = true;
  session.last_heartbeat_time = std::chrono::steady_clock::now();
  server.state_->sessions[session_fds[0]] = session;
  server.state_->client_id_to_fd["client_a"] = session_fds[0];
  const std::string disconnect_payload = R"({"channel_id":"client_a"})";
  EXPECT_EQ(server.HandleDisconnectRequest(server.state_, request_fds[0], epoll_fd, disconnect_payload), SUCCESS);
  std::string payload;
  EXPECT_EQ(RecvRawFabricMemMsg(request_fds[1], payload), FabricMemMsgType::kStatus);
  const auto json = nlohmann::json::parse(payload);
  EXPECT_EQ(json.at("error_code").get<uint32_t>(), static_cast<uint32_t>(SUCCESS));
  EXPECT_TRUE(server.state_->sessions.empty());
  (void)close(session_fds[1]);
  (void)close(request_fds[1]);
  (void)close(epoll_fd);
}

TEST(FabricMemControlUTest, ClientRejectsMissingPort) {
  std::vector<ShareHandleInfo> handles;
  int32_t conn_fd = -1;
  EXPECT_EQ(FabricMemControlClient::Fetch("127.0.0.1", "client", kClientTimeoutMs, handles, conn_fd), PARAM_INVALID);
  NotifyDesc notify;
  EXPECT_EQ(FabricMemControlClient::SendNotify("127.0.0.1", notify, kClientTimeoutMs), PARAM_INVALID);
}

TEST(FabricMemControlUTest, FetchRejectsFailedConnectStatus) {
  const int32_t port = test::AllocateFabricMemTestPort();
  ASSERT_GT(port, 0);
  FabricMemControlServer server;
  ASSERT_EQ(server.Start("127.0.0.1:" + std::to_string(port),
                         [](std::vector<ShareHandleInfo> &handles) {
                           handles.emplace_back(BuildShareHandle());
                           return FAILED;
                         }),
            SUCCESS);

  const std::string remote = "127.0.0.1:" + std::to_string(port);
  std::vector<ShareHandleInfo> handles;
  int32_t conn_fd = -1;
  EXPECT_EQ(FabricMemControlClient::Fetch(remote, "client_engine", kClientTimeoutMs, handles, conn_fd), FAILED);
  EXPECT_EQ(conn_fd, -1);
  server.Stop();
}

TEST(FabricMemControlUTest, ClientDisconnectClosesRemoteSession) {
  const int32_t port = test::AllocateFabricMemTestPort();
  ASSERT_GT(port, 0);
  FabricMemControlServer server;
  ASSERT_EQ(server.Start("127.0.0.1:" + std::to_string(port),
                         [](std::vector<ShareHandleInfo> &handles) {
                           handles.emplace_back(BuildShareHandle());
                           return SUCCESS;
                         }),
            SUCCESS);

  const std::string remote = "127.0.0.1:" + std::to_string(port);
  int32_t conn_fd = -1;
  ASSERT_EQ(FetchFabricMemClientConn(remote, "disconnect_client", conn_fd), SUCCESS);
  ASSERT_GE(conn_fd, 0);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  {
    std::lock_guard<std::mutex> lock(server.state_->mutex);
    EXPECT_EQ(server.state_->sessions.size(), 1U);
  }

  EXPECT_EQ(FabricMemControlClient::Disconnect(remote, "disconnect_client", kClientTimeoutMs), SUCCESS);
  (void)close(conn_fd);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  {
    std::lock_guard<std::mutex> lock(server.state_->mutex);
    EXPECT_TRUE(server.state_->sessions.empty());
  }
  server.Stop();
}

TEST(FabricMemControlUTest, HandleSendNotifyRejectsOversizedFieldsAndFullQueue) {
  FabricMemControlServer server;
  const std::string long_name(kMaxNotifyNameLen + 1U, 'n');
  const std::string long_msg(kMaxNotifyMsgLen + 1U, 'm');
  EXPECT_EQ(server.HandleSendNotify(server.state_, R"({"name":")" + long_name + R"(","notify_msg":"m"})"),
            PARAM_INVALID);
  EXPECT_EQ(server.HandleSendNotify(server.state_, R"({"name":"n","notify_msg":")" + long_msg + R"("})"),
            PARAM_INVALID);

  for (size_t i = 0; i < kMaxNotifyQueueSize; ++i) {
    EXPECT_EQ(server.HandleSendNotify(server.state_,
                                      R"({"name":"n)" + std::to_string(i) + R"(","notify_msg":"m"})"),
              SUCCESS);
  }
  EXPECT_EQ(server.HandleSendNotify(server.state_, R"({"name":"overflow","notify_msg":"m"})"), RESOURCE_EXHAUSTED);
}

TEST(FabricMemControlUTest, DispatchRejectsUnexpectedType) {
  FabricMemControlServer server;
  int32_t fds[2] = {-1, -1};
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  const int32_t epoll_fd = epoll_create1(0);
  ASSERT_GE(epoll_fd, 0);
  // The default (unexpected type) branch closes fds[0] itself, so only close fds[1] and epoll_fd here.
  EXPECT_EQ(server.DispatchFabricMemRequest(server.state_, fds[0], epoll_fd, 99, ""), PARAM_INVALID);
  (void)close(fds[1]);
  (void)close(epoll_fd);
}

TEST(FabricMemControlUTest, ServerReceivesAdxlHeartbeatAndUpdatesSession) {
  int32_t port = 0;
  std::string remote;
  FabricMemControlServer server;
  ASSERT_TRUE(StartDefaultShareHandleServer(server, port, remote));

  int32_t conn_fd = -1;
  ASSERT_EQ(FetchFabricMemClientConn(remote, "heartbeat_client", conn_fd), SUCCESS);
  ASSERT_GE(conn_fd, 0);

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  {
    std::lock_guard<std::mutex> lock(server.state_->mutex);
    ASSERT_EQ(server.state_->sessions.size(), 1U);
    EXPECT_EQ(server.state_->sessions.begin()->second.client_id, "heartbeat_client");
  }

  SendAdxlHeartBeat(conn_fd);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  server.CheckClientHeartbeatTimeouts();
  {
    std::lock_guard<std::mutex> lock(server.state_->mutex);
    EXPECT_EQ(server.state_->sessions.size(), 1U);
  }

  (void)close(conn_fd);
  server.Stop();
}

TEST(FabricMemControlUTest, ServerClosesClientOnHeartbeatTimeout) {
  FabricMemControlServer::SetHeartbeatTimeoutMs(100);
  int32_t port = 0;
  std::string remote;
  FabricMemControlServer server;
  ASSERT_TRUE(StartDefaultShareHandleServer(server, port, remote));

  int32_t conn_fd = -1;
  ASSERT_EQ(FetchFabricMemClientConn(remote, "timeout_client", conn_fd), SUCCESS);
  ASSERT_GE(conn_fd, 0);

  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  server.CheckClientHeartbeatTimeouts();
  {
    std::lock_guard<std::mutex> lock(server.state_->mutex);
    EXPECT_TRUE(server.state_->sessions.empty());
  }

  (void)close(conn_fd);
  server.Stop();
  FabricMemControlServer::SetHeartbeatTimeoutMs(120000);
}

TEST(FabricMemControlUTest, ServerAppliesHeartbeatTimeoutToSelfLoopback) {
  FabricMemControlServer::SetHeartbeatTimeoutMs(100);
  int32_t port = 0;
  std::string remote;
  FabricMemControlServer server;
  ASSERT_TRUE(StartDefaultShareHandleServer(server, port, remote));

  int32_t self_client_fd = -1;
  ASSERT_EQ(FetchFabricMemClientConn(remote, remote, self_client_fd), SUCCESS);
  ASSERT_GE(self_client_fd, 0);

  int32_t peer_client_fd = -1;
  ASSERT_EQ(FetchFabricMemClientConn(remote, "peer_client", peer_client_fd), SUCCESS);
  ASSERT_GE(peer_client_fd, 0);

  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  server.CheckClientHeartbeatTimeouts();
  {
    std::lock_guard<std::mutex> lock(server.state_->mutex);
    EXPECT_TRUE(server.state_->sessions.empty());
    EXPECT_EQ(server.state_->client_id_to_fd.count(remote), 0U);
    EXPECT_EQ(server.state_->client_id_to_fd.count("peer_client"), 0U);
  }

  (void)close(self_client_fd);
  (void)close(peer_client_fd);
  server.Stop();
  FabricMemControlServer::SetHeartbeatTimeoutMs(120000);
}

TEST(FabricMemControlUTest, ServerSkipsHeartbeatTimeoutWithoutAutoCleanup) {
  FabricMemControlServer::SetHeartbeatTimeoutMs(100);
  FabricMemControlServer server;
  ASSERT_EQ(server.Start("127.0.0.1:0",
                         [](std::vector<ShareHandleInfo> &handles) {
                           handles.emplace_back(BuildShareHandle());
                           return SUCCESS;
                         },
                         false),
            SUCCESS);

  int32_t fds[2] = {-1, -1};
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  const int32_t epoll_fd = epoll_create1(0);
  ASSERT_GE(epoll_fd, 0);
  FabricMemControlServer::ClientSession session;
  session.fd = fds[0];
  session.client_id = "timeout_client";
  session.with_heartbeat = true;
  session.last_heartbeat_time = std::chrono::steady_clock::now() - std::chrono::seconds(10);
  {
    std::lock_guard<std::mutex> lock(server.state_->mutex);
    server.state_->running.store(true, std::memory_order_release);
    server.state_->epoll_fd = epoll_fd;
    server.state_->sessions[fds[0]] = session;
    server.state_->client_id_to_fd["timeout_client"] = fds[0];
  }

  server.CheckClientHeartbeatTimeouts();
  {
    std::lock_guard<std::mutex> lock(server.state_->mutex);
    EXPECT_EQ(server.state_->sessions.size(), 1U);
  }

  (void)close(fds[1]);
  server.Stop();
  FabricMemControlServer::SetHeartbeatTimeoutMs(120000);
}

TEST(FabricMemControlUTest, SameClientIdReconnectReplacesOldSession) {
  int32_t port = 0;
  std::string remote;
  FabricMemControlServer server;
  ASSERT_TRUE(StartDefaultShareHandleServer(server, port, remote, true));

  int32_t first_client_fd = -1;
  ASSERT_EQ(FetchFabricMemClientConn(remote, "same_client", first_client_fd), SUCCESS);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  int32_t first_server_fd = -1;
  {
    std::lock_guard<std::mutex> lock(server.state_->mutex);
    ASSERT_EQ(server.state_->sessions.size(), 1U);
    first_server_fd = server.state_->client_id_to_fd["same_client"];
    ASSERT_GE(first_server_fd, 0);
  }

  int32_t second_client_fd = -1;
  ASSERT_EQ(FetchFabricMemClientConn(remote, "same_client", second_client_fd), SUCCESS);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  {
    std::lock_guard<std::mutex> lock(server.state_->mutex);
    EXPECT_EQ(server.state_->sessions.size(), 1U);
    const int32_t active_server_fd = server.state_->client_id_to_fd["same_client"];
    EXPECT_EQ(server.state_->sessions.count(active_server_fd), 1U);
    EXPECT_EQ(server.state_->sessions.count(first_server_fd), 0U);
  }

  (void)close(first_client_fd);
  (void)close(second_client_fd);
  server.Stop();
}

TEST(FabricMemControlUTest, AdxlControlSendMsgOverSocketPair) {
  int32_t fds[2] = {-1, -1};
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  DrainSocketInBackground(fds[1]);
  EXPECT_EQ(FabricMemControlClient::SendHeartBeat(fds[0]), SUCCESS);
  EXPECT_EQ(FabricMemControlClient::SendAdxlMsg(fds[0], FabricMemAdxlMsgType::kHeartBeat, R"({"msg":"H"})", 3000000ULL),
            SUCCESS);
  (void)close(fds[0]);
  (void)close(fds[1]);
}

TEST(FabricMemControlUTest, ServerClosesClientOnInvalidAdxlHeader) {
  int32_t port = 0;
  std::string remote;
  FabricMemControlServer server;
  ASSERT_TRUE(StartDefaultShareHandleServer(server, port, remote));

  int32_t conn_fd = -1;
  ASSERT_EQ(FetchFabricMemClientConn(remote, "invalid_header_client", conn_fd), SUCCESS);
  SendRawAdxlMsg(conn_fd, 0xDEADBEEFU, static_cast<int32_t>(FabricMemAdxlMsgType::kHeartBeat));
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  server.CheckClientHeartbeatTimeouts();
  {
    std::lock_guard<std::mutex> lock(server.state_->mutex);
    EXPECT_TRUE(server.state_->sessions.empty());
  }
  (void)close(conn_fd);
  server.Stop();
}

TEST(FabricMemControlUTest, AdxlControlSendHeartBeatOnClosedFd) {
  int32_t fds[2] = {-1, -1};
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  (void)close(fds[0]);
  (void)close(fds[1]);
  EXPECT_NE(FabricMemControlClient::SendHeartBeat(fds[0]), SUCCESS);
}

TEST(FabricMemControlUTest, ServerProcessesBackToBackAdxlHeartbeats) {
  int32_t port = 0;
  std::string remote;
  FabricMemControlServer server;
  ASSERT_TRUE(StartDefaultShareHandleServer(server, port, remote));

  int32_t conn_fd = -1;
  ASSERT_EQ(FetchFabricMemClientConn(remote, "multi_hb_client", conn_fd), SUCCESS);
  SendAdxlHeartBeat(conn_fd);
  SendAdxlHeartBeat(conn_fd);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  server.CheckClientHeartbeatTimeouts();
  {
    std::lock_guard<std::mutex> lock(server.state_->mutex);
    EXPECT_EQ(server.state_->sessions.size(), 1U);
  }
  (void)close(conn_fd);
  server.Stop();
}

TEST(FabricMemControlUTest, ServerAcceptsUnexpectedAdxlMsgType) {
  int32_t port = 0;
  std::string remote;
  FabricMemControlServer server;
  ASSERT_TRUE(StartDefaultShareHandleServer(server, port, remote));

  int32_t conn_fd = -1;
  ASSERT_EQ(FetchFabricMemClientConn(remote, "unexpected_type_client", conn_fd), SUCCESS);
  SendRawAdxlMsg(conn_fd, kFabricMemAdxlMagic, 99);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  server.CheckClientHeartbeatTimeouts();
  {
    std::lock_guard<std::mutex> lock(server.state_->mutex);
    EXPECT_EQ(server.state_->sessions.size(), 1U);
  }
  (void)close(conn_fd);
  server.Stop();
}

TEST_F(FabricMemLocalMemoryUTest, RegisterDeregisterAndGetShareHandles) {
  MemHandle invalid_handle = nullptr;
  EXPECT_EQ(local_memory_.RegisterMem({0U, kLen}, MEM_HOST, invalid_handle), PARAM_INVALID);
  EXPECT_EQ(local_memory_.RegisterMem({kLocalAddr, 0U}, MEM_HOST, invalid_handle), PARAM_INVALID);

  MemHandle host_handle = nullptr;
  EXPECT_EQ(local_memory_.RegisterMem({kLocalAddr, kLen}, MEM_HOST, host_handle), SUCCESS);
  ASSERT_NE(host_handle, nullptr);
  EXPECT_TRUE(local_memory_.HasHostMemory());
  auto handles = local_memory_.GetShareHandles();
  ASSERT_EQ(handles.size(), 1U);
  EXPECT_EQ(handles[0].va_addr, kLocalAddr);
  EXPECT_EQ(handles[0].len, kLen);
  EXPECT_NE(handles[0].imported_handle, nullptr);
  EXPECT_NE(handles[0].imported_va, 0UL);

  MemHandle duplicate_handle = nullptr;
  EXPECT_EQ(local_memory_.RegisterMem({kLocalAddr, kLen}, MEM_HOST, duplicate_handle), SUCCESS);
  EXPECT_EQ(duplicate_handle, host_handle);

  EXPECT_EQ(local_memory_.DeregisterMem(host_handle), SUCCESS);
  EXPECT_EQ(local_memory_.DeregisterMem(host_handle), SUCCESS);
  EXPECT_TRUE(local_memory_.GetShareHandles().empty());
}

TEST_F(FabricMemLocalMemoryUTest, RegisterDeviceMemoryHasNoHostFlag) {
  MemHandle device_handle = nullptr;
  EXPECT_EQ(local_memory_.RegisterMem({kLocalAddr, kLen}, MEM_DEVICE, device_handle), SUCCESS);
  ASSERT_NE(device_handle, nullptr);
  EXPECT_FALSE(local_memory_.HasHostMemory());
  auto handles = local_memory_.GetShareHandles();
  ASSERT_EQ(handles.size(), 1U);
  EXPECT_EQ(handles[0].imported_va, 0UL);
  EXPECT_EQ(local_memory_.DeregisterMem(device_handle), SUCCESS);
}

TEST_F(FabricMemLocalMemoryUTest, RegisterRejectsOverflowRange) {
  MemHandle handle = nullptr;
  MemDesc mem{};
  mem.addr = std::numeric_limits<uintptr_t>::max();
  mem.len = 2U;
  EXPECT_EQ(local_memory_.RegisterMem(mem, MEM_HOST, handle), PARAM_INVALID);
  EXPECT_EQ(handle, nullptr);
}

TEST_F(FabricMemLocalMemoryUTest, TranslateLocalHostOpAddrsResolvesImportedMapping) {
  MemHandle host_handle = nullptr;
  ASSERT_EQ(local_memory_.RegisterMem({kLocalAddr, kLen}, MEM_HOST, host_handle), SUCCESS);
  const uintptr_t imported_va = local_memory_.GetShareHandles().front().imported_va;
  ASSERT_NE(imported_va, 0UL);

  std::vector<TransferOpDesc> op_descs = {{kLocalAddr + 2U, kRemoteOldAddr, 8U}};
  EXPECT_EQ(local_memory_.TranslateLocalHostOpAddrs(op_descs), SUCCESS);
  EXPECT_EQ(op_descs[0].local_addr, imported_va + 2U);

  std::vector<TransferOpDesc> unregistered = {{kLocalAddr + kLen * 2U, kRemoteOldAddr, 8U}};
  EXPECT_EQ(local_memory_.TranslateLocalHostOpAddrs(unregistered), PARAM_INVALID);
}

TEST_F(FabricMemLocalMemoryUTest, DeregisterUnknownHandleIsNoOp) {
  EXPECT_EQ(local_memory_.DeregisterMem(reinterpret_cast<MemHandle>(0xDEAD)), SUCCESS);
}

TEST(FabricMemRemoteMemoryUTest, ImportFinalizeRoundTrip) {
  auto runtime = std::make_shared<FabricMemRuntimeStub>();
  ScopedRuntimeMock scoped_runtime(runtime);
  VirtualMemoryManager::GetInstance().Finalize();
  ASSERT_EQ(VirtualMemoryManager::GetInstance().Initialize(), SUCCESS);

  FabricMemRemoteMemory remote_memory;
  EXPECT_TRUE(remote_memory.GetNewVaToOldVa().empty());
  ASSERT_EQ(remote_memory.Import({BuildShareHandle(kRemoteOldAddr, kLen)}, 0), SUCCESS);
  const auto mapping = remote_memory.GetNewVaToOldVa();
  ASSERT_EQ(mapping.size(), 1U);
  EXPECT_EQ(mapping.begin()->second.va_addr, kRemoteOldAddr);
  EXPECT_EQ(mapping.begin()->second.len, kLen);

  remote_memory.Finalize();
  EXPECT_TRUE(remote_memory.GetNewVaToOldVa().empty());
  VirtualMemoryManager::GetInstance().Finalize();
}

TEST(FabricMemRemoteMemoryUTest, ImportRollsBackOnMapFailure) {
  auto runtime = std::make_shared<FabricMemRuntimeStub>();
  ScopedRuntimeMock scoped_runtime(runtime);
  VirtualMemoryManager::GetInstance().Finalize();
  ASSERT_EQ(VirtualMemoryManager::GetInstance().Initialize(), SUCCESS);

  llm::GetAclStubMock() = "aclrtMapMem";
  FabricMemRemoteMemory remote_memory;
  EXPECT_NE(remote_memory.Import({BuildShareHandle(kRemoteOldAddr, kLen)}, 0), SUCCESS);
  EXPECT_TRUE(remote_memory.GetNewVaToOldVa().empty());
  llm::GetAclStubMock().clear();
  VirtualMemoryManager::GetInstance().Finalize();
}

TEST_F(FabricMemSlotPoolUTest, InitializeRejectsInvalidParams) {
  EXPECT_EQ(pool_.Initialize(-1, 1U, 1U), PARAM_INVALID);
  EXPECT_EQ(pool_.Initialize(0, 0U, 1U), PARAM_INVALID);
  EXPECT_EQ(pool_.Initialize(0, 1U, 0U), PARAM_INVALID);
  EXPECT_EQ(pool_.Initialize(0, 2U, 1U), SUCCESS);
}

TEST_F(FabricMemSlotPoolUTest, AcquireReuseAndDestroy) {
  ASSERT_EQ(pool_.Initialize(0, 2U, 1U), SUCCESS);
  AsyncSlot slot;
  EXPECT_EQ(pool_.AcquireAsync(slot), SUCCESS);
  ASSERT_EQ(slot.streams.size(), 1U);
  ASSERT_NE(slot.ctx, nullptr);
  EXPECT_EQ(runtime_->stream_failure_mode_count_, 1U);
  EXPECT_EQ(runtime_->last_stream_failure_mode_, ACL_STOP_ON_FAILURE);
  const auto first_ctx = slot.ctx;
  const auto first_stream = slot.streams[0];
  pool_.Release(slot, false);
  EXPECT_EQ(slot.ctx, nullptr);
  EXPECT_TRUE(pool_.slot_pool_[0].available);

  EXPECT_EQ(pool_.AcquireAsync(slot), SUCCESS);
  ASSERT_EQ(slot.streams.size(), 1U);
  EXPECT_EQ(slot.ctx, first_ctx);
  EXPECT_EQ(slot.streams[0], first_stream);
  EXPECT_FALSE(pool_.slot_pool_[0].available);
  pool_.Release(slot, false);

  AsyncSlot slot2;
  EXPECT_EQ(pool_.AcquireAsync(slot2), SUCCESS);
  pool_.Release(slot2, false);
  EXPECT_EQ(pool_.AcquireAsync(slot), SUCCESS);
  EXPECT_EQ(pool_.AcquireAsync(slot2), SUCCESS);
  AsyncSlot slot3;
  EXPECT_EQ(pool_.AcquireAsync(slot3), FAILED);
  pool_.Release(slot, true);
  pool_.Release(slot2, true);
  EXPECT_TRUE(pool_.slot_pool_.empty());
}

TEST_F(FabricMemSlotPoolUTest, HostFlagsAreZeroedOnReuse) {
  ASSERT_EQ(pool_.Initialize(0, 2U, 2U), SUCCESS);
  AsyncSlot slot;
  ASSERT_EQ(pool_.AcquireAsync(slot), SUCCESS);
  ASSERT_EQ(slot.host_flags.size(), 2U);
  EXPECT_EQ(*static_cast<uint64_t *>(slot.host_flags[0]), 0ULL);
  *static_cast<uint64_t *>(slot.host_flags[0]) = 1ULL;
  pool_.Release(slot, false);

  AsyncSlot reused;
  ASSERT_EQ(pool_.AcquireAsync(reused), SUCCESS);
  ASSERT_EQ(reused.host_flags.size(), 2U);
  EXPECT_EQ(*static_cast<uint64_t *>(reused.host_flags[0]), 0ULL);
  pool_.Release(reused, false);
}

TEST_F(FabricMemSlotPoolUTest, AcquireWithTimeoutFailsWhenPoolFull) {
  ASSERT_EQ(pool_.Initialize(0, 1U, 1U), SUCCESS);
  AsyncSlot slot1;
  ASSERT_EQ(pool_.AcquireAsync(slot1), SUCCESS);
  AsyncSlot slot2;
  EXPECT_EQ(pool_.AcquireWithTimeout(slot2, 1000ULL), TIMEOUT);
  pool_.Release(slot1, false);
}

TEST_F(FabricMemSlotPoolUTest, AcquireWithTimeoutWakesOnAbortAndDestroyAll) {
  ASSERT_EQ(pool_.Initialize(0, 1U, 1U), SUCCESS);
  AsyncSlot slot1;
  ASSERT_EQ(pool_.AcquireAsync(slot1), SUCCESS);

  std::atomic<bool> acquire_finished{false};
  std::thread waiter([this, &acquire_finished]() {
    AsyncSlot slot;
    constexpr uint64_t kWaitTimeoutUs = 500000ULL;
    (void)pool_.AcquireWithTimeout(slot, kWaitTimeoutUs);
    acquire_finished.store(true, std::memory_order_release);
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  EXPECT_FALSE(acquire_finished.load(std::memory_order_acquire));

  const auto join_start = std::chrono::steady_clock::now();
  pool_.AbortAndDestroyAll();
  waiter.join();
  const auto join_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - join_start).count();
  EXPECT_TRUE(acquire_finished.load(std::memory_order_acquire));
  EXPECT_LT(join_ms, 500);
}

TEST_F(FabricMemSlotPoolUTest, AbortSlotStreamsAbortsWithoutDestroying) {
  ASSERT_EQ(pool_.Initialize(0, 1U, 2U), SUCCESS);
  AsyncSlot slot;
  ASSERT_EQ(pool_.AcquireAsync(slot), SUCCESS);
  FabricMemSlotPool::AbortSlotStreams(slot);
  EXPECT_EQ(runtime_->stream_abort_count_, 2U);
  EXPECT_EQ(pool_.slot_pool_.size(), 1U);
  pool_.Release(slot, true);
}

TEST_F(FabricMemTransferServiceUTest, InitializeRejectsInvalidInputAndAclFailure) {
  auto bad_device = MakeServiceInitParam(&statistic_, &local_memory_);
  bad_device.device_id = -1;
  EXPECT_EQ(service_.Initialize(bad_device), PARAM_INVALID);
  auto zero_max = MakeServiceInitParam(&statistic_, &local_memory_);
  zero_max.max_stream_num = 0U;
  EXPECT_EQ(service_.Initialize(zero_max), PARAM_INVALID);
  auto zero_task = MakeServiceInitParam(&statistic_, &local_memory_);
  zero_task.task_stream_num = 0U;
  EXPECT_EQ(service_.Initialize(zero_task), PARAM_INVALID);
  auto max_lt_task = MakeServiceInitParam(&statistic_, &local_memory_);
  max_lt_task.max_stream_num = 1U;
  max_lt_task.task_stream_num = 2U;
  EXPECT_EQ(service_.Initialize(max_lt_task), PARAM_INVALID);
  EXPECT_EQ(service_.Initialize(MakeServiceInitParam(nullptr, &local_memory_)), PARAM_INVALID);
  EXPECT_EQ(service_.Initialize(MakeServiceInitParam(&statistic_, nullptr)), PARAM_INVALID);

  EXPECT_EQ(InitService(3U, 1U), SUCCESS);
  EXPECT_EQ(service_.device_id_, 0);
  EXPECT_EQ(service_.task_stream_num_, 1U);
  EXPECT_EQ(service_.slot_pool_.max_async_slot_num_, 3U);
  ASSERT_NE(service_.dev_const_one_, nullptr);
}

TEST_F(FabricMemTransferServiceUTest, InitDevConstOneRollsBackOnMemcpyFailure) {
  llm::GetAclStubMock() = "aclrtMemcpy";
  EXPECT_NE(InitService(1U, 1U), SUCCESS);
  EXPECT_EQ(service_.dev_const_one_, nullptr);
  llm::GetAclStubMock().clear();

  EXPECT_EQ(InitService(1U, 1U), SUCCESS);
  ASSERT_NE(service_.dev_const_one_, nullptr);
}

TEST_F(FabricMemTransferServiceUTest, MallocMemSupportsDeviceMemory) {
  VirtualMemoryManager::GetInstance().Finalize();

  void *device_ptr = nullptr;
  ASSERT_EQ(FabricMemTransferService::MallocMem(MEM_DEVICE, kLen, &device_ptr), SUCCESS);
  ASSERT_NE(device_ptr, nullptr);
  EXPECT_EQ(runtime_->malloc_physical_count_, 1U);
  EXPECT_EQ(runtime_->last_physical_mem_prop_.location.type, ACL_MEM_LOCATION_TYPE_DEVICE);
  EXPECT_EQ(runtime_->last_physical_mem_prop_.location.id, 0U);
  EXPECT_EQ(runtime_->last_physical_mem_prop_.memAttr, ACL_HBM_MEM_HUGE);

  EXPECT_EQ(FabricMemTransferService::FreeMem(device_ptr), SUCCESS);
  VirtualMemoryManager::GetInstance().Finalize();
}

TEST_F(FabricMemTransferServiceUTest, MallocMemAndFreeMemHost) {
  VirtualMemoryManager::GetInstance().Finalize();
  ASSERT_EQ(VirtualMemoryManager::GetInstance().Initialize(), SUCCESS);

  void *host_ptr = nullptr;
  ASSERT_EQ(FabricMemTransferService::MallocMem(MEM_HOST, sizeof(int32_t), &host_ptr), SUCCESS);
  ASSERT_NE(host_ptr, nullptr);
  auto *value = static_cast<int32_t *>(host_ptr);
  *value = 123;
  EXPECT_EQ(*value, 123);
  EXPECT_EQ(FabricMemTransferService::FreeMem(host_ptr), SUCCESS);
  VirtualMemoryManager::GetInstance().Finalize();
}

TEST_F(FabricMemTransferServiceUTest, MallocMemRejectsNullPtr) {
  EXPECT_EQ(FabricMemTransferService::MallocMem(MEM_HOST, sizeof(int32_t), nullptr), PARAM_INVALID);
}

TEST_F(FabricMemTransferServiceUTest, AddressTranslationAndCopyValidation) {
  uint8_t local[kLen] = {};
  uint8_t remote[kLen] = {};
  std::fill(std::begin(local), std::end(local), 7U);
  ASSERT_EQ(InitService(4U, 1U), SUCCESS);

  uintptr_t new_addr = 0;
  EXPECT_EQ(
      FabricMemTransferService::TransOpAddr(kRemoteOldAddr + 4U, 8U, BuildContext().remote_va_to_old_va, new_addr),
      SUCCESS);
  EXPECT_EQ(new_addr, kRemoteNewAddr + 4U);
  EXPECT_EQ(FabricMemTransferService::TransOpAddr(kRemoteOldAddr + kLen * 5U, 8U, BuildContext().remote_va_to_old_va,
                                                  new_addr),
            PARAM_INVALID);

  AsyncSlot slot;
  ASSERT_EQ(service_.slot_pool_.AcquireAsync(slot), SUCCESS);
  TemporaryRtContext ctx_guard(slot.ctx);
  AsyncSlot empty_slot;
  EXPECT_EQ(FabricMemTransferService::ProcessCopyWithAsync(empty_slot, WRITE, BuildOpDescs(local, remote)),
            PARAM_INVALID);
  EXPECT_EQ(FabricMemTransferService::ProcessCopyWithAsync(slot, WRITE, BuildOpDescs(local, remote)), SUCCESS);
  EXPECT_EQ(remote[0], 7U);
  std::fill(std::begin(remote), std::end(remote), 9U);
  EXPECT_EQ(FabricMemTransferService::ProcessCopyWithAsync(slot, READ, BuildOpDescs(local, remote)), SUCCESS);
  EXPECT_EQ(local[0], 9U);
  EXPECT_EQ(
      FabricMemTransferService::ProcessCopyWithAsync(slot, static_cast<TransferOp>(99), BuildOpDescs(local, remote)),
      PARAM_INVALID);
  service_.slot_pool_.Release(slot, false);
}

TEST_F(FabricMemTransferServiceUTest, NeedTransLocalAddrHandlesHostDeviceEmptyAndFailure) {
  ASSERT_EQ(InitService(3U, 1U), SUCCESS);
  bool need_trans = true;
  EXPECT_EQ(service_.NeedTransLocalAddr({}, need_trans), SUCCESS);
  EXPECT_FALSE(need_trans);

  uint8_t local[kLen] = {};
  uint8_t remote[kLen] = {};
  auto op_descs = BuildOpDescs(local, remote);
  local_memory_.has_host_memory_.store(false);
  need_trans = true;
  EXPECT_EQ(service_.NeedTransLocalAddr(op_descs, need_trans), SUCCESS);
  EXPECT_FALSE(need_trans);

  local_memory_.has_host_memory_.store(true);
  runtime_->pointer_is_host_ = false;
  EXPECT_EQ(service_.NeedTransLocalAddr(op_descs, need_trans), SUCCESS);
  EXPECT_FALSE(need_trans);

  runtime_->pointer_is_host_ = true;
  EXPECT_EQ(service_.NeedTransLocalAddr(op_descs, need_trans), SUCCESS);
  EXPECT_TRUE(need_trans);

  runtime_->pointer_attr_error_ = ACL_ERROR_RT_INTERNAL_ERROR;
  EXPECT_NE(service_.NeedTransLocalAddr(op_descs, need_trans), SUCCESS);
}

TEST_F(FabricMemTransferServiceUTest, UpdateStatsSupportsDirectInfoChannelAndNullStatistic) {
  auto direct_info = std::make_shared<FabricMemTransferStatisticInfo>();
  auto context = BuildContext(direct_info);
  ASSERT_EQ(InitService(3U, 1U), SUCCESS);
  service_.UpdateStats(context, 10U, 4U, 512U, 2U);
  EXPECT_EQ(direct_info->transfer.total_cost.load(std::memory_order_relaxed), 10UL);
  EXPECT_EQ(direct_info->real_copy.total_cost.load(std::memory_order_relaxed), 4UL);

  context.stat_info = nullptr;
  service_.UpdateStats(context, 20U, 8U, 1024U, 4U);
  EXPECT_EQ(statistic_.GetSnapshot(kStatChannelId).transfer.total_cost, 20UL);

  service_.statistic_ = nullptr;
  service_.UpdateStats(BuildContext(), 1U, 1U, 1U, 1U);
  service_.statistic_ = &statistic_;
}

TEST_F(FabricMemTransferServiceUTest, TransferSyncFailureAbortsStreams) {
  uint8_t local[kLen * 2U] = {};
  uint8_t remote[kLen * 2U] = {};
  ASSERT_EQ(InitService(2U, 2U), SUCCESS);
  const std::string remote_engine = "127.0.0.1:13000";
  AddMappedServiceChannel(service_, remote_engine, remote, sizeof(remote));
  // Fail the very first device-to-device copy so no real copy touches the imported mapping; both
  // task streams are still aborted when the slot is destroyed on the failure path.
  runtime_->memcpy_async_error_ = ACL_ERROR_RT_INTERNAL_ERROR;
  runtime_->memcpy_async_fail_kind_ = ACL_MEMCPY_DEVICE_TO_DEVICE;
  runtime_->memcpy_async_fail_on_count_ = 1U;

  EXPECT_NE(service_.TransferSync(remote_engine, WRITE, BuildTwoOpDescs(local, remote), kClientTimeoutMs), SUCCESS);
  EXPECT_TRUE(service_.slot_pool_.slot_pool_.empty());
  EXPECT_EQ(runtime_->stream_abort_count_, 2U);
}

TEST_F(FabricMemTransferServiceUTest, TransferAsyncFailureAbortsSlot) {
  uint8_t local[kLen * 2U] = {};
  uint8_t remote[kLen * 2U] = {};
  ASSERT_EQ(InitService(1U, 1U), SUCCESS);
  const std::string remote_engine = "127.0.0.1:13001";
  AddMappedServiceChannel(service_, remote_engine, remote, sizeof(remote));
  runtime_->memcpy_async_error_ = ACL_ERROR_RT_INTERNAL_ERROR;
  runtime_->memcpy_async_fail_kind_ = ACL_MEMCPY_DEVICE_TO_DEVICE;
  runtime_->memcpy_async_fail_on_count_ = 1U;

  TransferReq req = nullptr;
  auto op_descs = BuildTwoOpDescs(local, remote);
  EXPECT_NE(service_.TransferAsync(remote_engine, WRITE, op_descs, req), SUCCESS);
  EXPECT_TRUE(service_.slot_pool_.slot_pool_.empty());
  EXPECT_TRUE(service_.channel_manager_.req_2_channel_.empty());
  EXPECT_EQ(runtime_->stream_abort_count_, 1U);
}

TEST_F(FabricMemTransferServiceUTest, TransferAsyncHostFlagCopyFailureAbortsSlot) {
  uint8_t local[kLen] = {};
  uint8_t remote[kLen] = {};
  // The data copy must succeed (writing into the imported mapping) before the host-flag copy fails,
  // so reset the VMM to hand out a freshly mapped block for the imported remote buffer.
  VirtualMemoryManager::GetInstance().Finalize();
  ASSERT_EQ(InitService(1U, 1U), SUCCESS);
  const std::string remote_engine = "127.0.0.1:13002";
  AddMappedServiceChannel(service_, remote_engine, remote, sizeof(remote));
  runtime_->memcpy_async_error_ = ACL_ERROR_RT_INTERNAL_ERROR;
  runtime_->memcpy_async_fail_kind_ = ACL_MEMCPY_DEVICE_TO_HOST;
  runtime_->memcpy_async_fail_on_count_ = 2U;

  TransferReq req = nullptr;
  EXPECT_NE(service_.TransferAsync(remote_engine, WRITE, BuildOpDescs(local, remote), req), SUCCESS);
  EXPECT_TRUE(service_.slot_pool_.slot_pool_.empty());
  EXPECT_TRUE(service_.channel_manager_.req_2_channel_.empty());
  EXPECT_EQ(runtime_->stream_abort_count_, 1U);
}

TEST_F(FabricMemTransferServiceUTest, TransferSyncRejectsUnknownRemote) {
  uint8_t local[kLen] = {};
  uint8_t remote[kLen] = {};
  ASSERT_EQ(InitService(2U, 1U), SUCCESS);
  EXPECT_EQ(service_.TransferSync("127.0.0.1:404", WRITE, BuildOpDescs(local, remote), kClientTimeoutMs),
            NOT_CONNECTED);
}

TEST_F(FabricMemTransferServiceUTest, AsyncTransferRecordCompletesAndUpdatesStats) {
  ASSERT_EQ(InitService(4U, 1U), SUCCESS);
  auto channel = AddServiceChannel(service_, kChannelId);
  AsyncSlot slot;
  ASSERT_EQ(service_.slot_pool_.AcquireAsync(slot), SUCCESS);
  ASSERT_EQ(service_.AppendHostFlagCopies(slot), SUCCESS);
  EXPECT_EQ(runtime_->host_flag_d2h_count_, 1U);

  TransferReq req = reinterpret_cast<TransferReq>(0x1234UL);
  RegisterServiceAsyncRecord(service_, channel, BuildContext(), req, std::move(slot), 256U, 2U);

  TransferStatus status = TransferStatus::WAITING;
  EXPECT_EQ(service_.GetTransferStatus(req, status), SUCCESS);
  EXPECT_EQ(status, TransferStatus::COMPLETED);
  EXPECT_EQ(service_.GetTransferStatus(req, status), FAILED);
  EXPECT_EQ(statistic_.GetSnapshot(kStatChannelId).transfer.times, 1UL);
  {
    std::lock_guard<std::mutex> lock(channel->records_mutex);
    EXPECT_TRUE(channel->async_records.empty());
  }
  EXPECT_TRUE(service_.channel_manager_.req_2_channel_.empty());
}

TEST_F(FabricMemTransferServiceUTest, AsyncTransferWaitsUntilAllHostFlagsDone) {
  ASSERT_EQ(InitService(4U, 2U), SUCCESS);
  auto channel = AddServiceChannel(service_, kChannelId);
  AsyncSlot slot;
  ASSERT_EQ(service_.slot_pool_.AcquireAsync(slot), SUCCESS);
  ASSERT_EQ(slot.host_flags.size(), 2U);
  *static_cast<uint64_t *>(slot.host_flags[0]) = 1ULL;
  for (const auto &stream : slot.streams) {
    runtime_->streams_not_complete_.insert(stream);
  }

  TransferReq req = reinterpret_cast<TransferReq>(0x2468UL);
  RegisterServiceAsyncRecord(service_, channel, BuildContext(), req, std::move(slot), 128U, 1U);

  TransferStatus status = TransferStatus::COMPLETED;
  EXPECT_EQ(service_.GetTransferStatus(req, status), SUCCESS);
  EXPECT_EQ(status, TransferStatus::WAITING);

  {
    std::lock_guard<std::mutex> lock(channel->records_mutex);
    auto &record = channel->async_records[reinterpret_cast<uintptr_t>(req)];
    *static_cast<uint64_t *>(record.slot.host_flags[1]) = 1ULL;
  }
  EXPECT_EQ(service_.GetTransferStatus(req, status), SUCCESS);
  EXPECT_EQ(status, TransferStatus::COMPLETED);
}

TEST_F(FabricMemTransferServiceUTest, AsyncTransferStreamQueryFailureReturnsFailed) {
  ASSERT_EQ(InitService(4U, 1U), SUCCESS);
  auto channel = AddServiceChannel(service_, kChannelId);
  AsyncSlot slot;
  ASSERT_EQ(service_.slot_pool_.AcquireAsync(slot), SUCCESS);
  ASSERT_EQ(service_.AppendHostFlagCopies(slot), SUCCESS);

  TransferReq req = reinterpret_cast<TransferReq>(0x3579UL);
  RegisterServiceAsyncRecord(service_, channel, BuildContext(), req, std::move(slot), 64U, 1U);
  {
    std::lock_guard<std::mutex> lock(channel->records_mutex);
    for (void *host_flag : channel->async_records[reinterpret_cast<uintptr_t>(req)].slot.host_flags) {
      *static_cast<uint64_t *>(host_flag) = 0ULL;
    }
  }
  runtime_->stream_query_error_ = ACL_ERROR_RT_INTERNAL_ERROR;

  TransferStatus status = TransferStatus::WAITING;
  EXPECT_EQ(service_.GetTransferStatus(req, status), SUCCESS);
  EXPECT_EQ(status, TransferStatus::FAILED);
  EXPECT_GT(runtime_->stream_query_count_, 0U);
  EXPECT_TRUE(service_.channel_manager_.req_2_channel_.empty());
}

TEST_F(FabricMemTransferServiceUTest, AsyncTransferHostFlagsDoneSkipsStreamSync) {
  ASSERT_EQ(InitService(4U, 1U), SUCCESS);
  auto channel = AddServiceChannel(service_, kChannelId);
  AsyncSlot slot;
  ASSERT_EQ(service_.slot_pool_.AcquireAsync(slot), SUCCESS);
  ASSERT_EQ(service_.AppendHostFlagCopies(slot), SUCCESS);
  for (void *host_flag : slot.host_flags) {
    *static_cast<uint64_t *>(host_flag) = 1ULL;
  }

  TransferReq req = reinterpret_cast<TransferReq>(0x579BUL);
  RegisterServiceAsyncRecord(service_, channel, BuildContext(), req, std::move(slot), 64U, 1U);
  runtime_->stream_sync_error_ = ACL_ERROR_RT_INTERNAL_ERROR;

  TransferStatus status = TransferStatus::WAITING;
  EXPECT_EQ(service_.GetTransferStatus(req, status), SUCCESS);
  EXPECT_EQ(status, TransferStatus::COMPLETED);
  EXPECT_EQ(runtime_->stream_sync_count_, 0U);
}

TEST_F(FabricMemTransferServiceUTest, AsyncTransferStreamQueryCompleteTriggersSync) {
  ASSERT_EQ(InitService(4U, 1U), SUCCESS);
  auto channel = AddServiceChannel(service_, kChannelId);
  AsyncSlot slot;
  ASSERT_EQ(service_.slot_pool_.AcquireAsync(slot), SUCCESS);
  ASSERT_EQ(service_.AppendHostFlagCopies(slot), SUCCESS);

  TransferReq req = reinterpret_cast<TransferReq>(0x468AUL);
  RegisterServiceAsyncRecord(service_, channel, BuildContext(), req, std::move(slot), 64U, 1U);
  {
    std::lock_guard<std::mutex> lock(channel->records_mutex);
    for (void *host_flag : channel->async_records[reinterpret_cast<uintptr_t>(req)].slot.host_flags) {
      *static_cast<uint64_t *>(host_flag) = 0ULL;
    }
  }
  runtime_->stream_sync_error_ = ACL_ERROR_RT_INTERNAL_ERROR;

  TransferStatus status = TransferStatus::WAITING;
  EXPECT_EQ(service_.GetTransferStatus(req, status), SUCCESS);
  EXPECT_EQ(status, TransferStatus::FAILED);
  EXPECT_GT(runtime_->stream_sync_count_, 0U);
  EXPECT_TRUE(service_.channel_manager_.req_2_channel_.empty());
}

TEST_F(FabricMemTransferServiceUTest, CleanupAsyncTransferReleasesRecord) {
  ASSERT_EQ(InitService(4U, 1U), SUCCESS);
  auto channel = AddServiceChannel(service_, kChannelId);
  AsyncSlot slot;
  ASSERT_EQ(service_.slot_pool_.AcquireAsync(slot), SUCCESS);
  ASSERT_EQ(service_.AppendHostFlagCopies(slot), SUCCESS);
  EXPECT_EQ(service_.slot_pool_.slot_pool_.size(), 1U);

  TransferReq req = reinterpret_cast<TransferReq>(0x9ABCUL);
  RegisterServiceAsyncRecord(service_, channel, BuildContext(), req, std::move(slot), 128U, 1U);

  service_.CleanupAsyncTransfer(reinterpret_cast<TransferReq>(0xDEADUL));
  EXPECT_FALSE(service_.channel_manager_.req_2_channel_.empty());

  service_.CleanupAsyncTransfer(req);
  EXPECT_TRUE(service_.channel_manager_.req_2_channel_.empty());
  EXPECT_TRUE(service_.slot_pool_.slot_pool_.empty());
  {
    std::lock_guard<std::mutex> lock(channel->records_mutex);
    EXPECT_TRUE(channel->async_records.empty());
  }
}

TEST_F(FabricMemChannelManagerUTest, DoubleInitializeFails) {
  EXPECT_EQ(manager_.Initialize(MakeManagerInitParam(&statistic_, &slot_pool_)), FAILED);
}

TEST_F(FabricMemChannelManagerUTest, InitializeRejectsNullDependencies) {
  FabricMemChannelManager manager;
  EXPECT_EQ(manager.Initialize(MakeManagerInitParam(nullptr, &slot_pool_)), PARAM_INVALID);
  EXPECT_EQ(manager.Initialize(MakeManagerInitParam(&statistic_, nullptr)), PARAM_INVALID);
}

TEST_F(FabricMemChannelManagerUTest, GetChannelAndConnectionState) {
  std::shared_ptr<FabricMemChannel> channel;
  EXPECT_EQ(manager_.GetChannel("missing", channel), NOT_CONNECTED);
  EXPECT_FALSE(manager_.HasChannels());
  EXPECT_FALSE(manager_.IsConnected("missing"));

  const std::string remote = "127.0.0.1:13100";
  AddChannel(remote);
  EXPECT_EQ(manager_.GetChannel(remote, channel), SUCCESS);
  EXPECT_NE(channel, nullptr);
  EXPECT_TRUE(manager_.HasChannels());
  EXPECT_TRUE(manager_.IsConnected(remote));
}

TEST_F(FabricMemChannelManagerUTest, RequestRoutingAddFindRemove) {
  const std::string remote = "127.0.0.1:13101";
  auto channel = AddChannel(remote);
  std::shared_ptr<FabricMemChannel> found;
  EXPECT_EQ(manager_.FindChannelByReq(42U, found), FAILED);

  manager_.AddReqRoute(42U, channel);
  EXPECT_EQ(manager_.FindChannelByReq(42U, found), SUCCESS);
  EXPECT_EQ(found, channel);

  manager_.RemoveReqRoute(42U);
  EXPECT_EQ(manager_.FindChannelByReq(42U, found), FAILED);
}

TEST_F(FabricMemChannelManagerUTest, BuildTransferContextPopulatesMapping) {
  const std::string remote = "127.0.0.1:13102";
  auto channel = AddChannel(remote);
  channel->remote_memory = std::make_unique<FabricMemRemoteMemory>();
  VirtualMemoryManager::GetInstance().Finalize();
  ASSERT_EQ(VirtualMemoryManager::GetInstance().Initialize(), SUCCESS);
  ASSERT_EQ(channel->remote_memory->Import({BuildShareHandle(kRemoteOldAddr, kLen)}, 0), SUCCESS);

  FabricMemTransferContext context;
  EXPECT_EQ(manager_.BuildTransferContext(remote, &statistic_, context), SUCCESS);
  EXPECT_EQ(context.channel_id, remote);
  EXPECT_EQ(context.remote_va_to_old_va.size(), 1U);
  EXPECT_NE(context.stat_info, nullptr);
  VirtualMemoryManager::GetInstance().Finalize();
}

TEST_F(FabricMemChannelManagerUTest, DisconnectAbortsAsyncRecordsAndClearsRoutes) {
  const std::string remote = "127.0.0.1:13103";
  auto channel = AddChannel(remote);
  channel->remote_memory = std::make_unique<FabricMemRemoteMemory>();

  AsyncSlot slot;
  ASSERT_EQ(slot_pool_.AcquireAsync(slot), SUCCESS);
  const uint64_t req_id = 0x55AAUL;
  AsyncRecord record;
  record.slot = std::move(slot);
  record.channel_id = remote;
  {
    std::lock_guard<std::mutex> lock(channel->records_mutex);
    channel->async_records[req_id] = std::move(record);
  }
  manager_.AddReqRoute(req_id, channel);

  EXPECT_EQ(manager_.Disconnect(AscendString(remote.c_str()), 0), SUCCESS);
  EXPECT_FALSE(manager_.IsConnected(remote));
  EXPECT_TRUE(manager_.req_2_channel_.empty());
  EXPECT_TRUE(slot_pool_.slot_pool_.empty());
  EXPECT_GT(runtime_->stream_abort_count_, 0U);
}

TEST_F(FabricMemChannelManagerUTest, DisconnectUnknownRemoteReturnsNotConnected) {
  EXPECT_EQ(manager_.Disconnect(AscendString("127.0.0.1:404"), 0), NOT_CONNECTED);
}

TEST_F(FabricMemChannelManagerUTest, DisconnectAllClearsChannels) {
  AddChannel("127.0.0.1:13104");
  AddChannel("127.0.0.1:13105");
  manager_.DisconnectAll();
  EXPECT_FALSE(manager_.HasChannels());
}

TEST_F(FabricMemChannelManagerUTest, ConnectAndDisconnectRoundTrip) {
  VirtualMemoryManager::GetInstance().Finalize();
  ASSERT_EQ(VirtualMemoryManager::GetInstance().Initialize(), SUCCESS);
  int32_t port = 0;
  std::string remote;
  FabricMemControlServer server;
  ASSERT_TRUE(StartDefaultShareHandleServer(server, port, remote));

  EXPECT_EQ(manager_.Connect(AscendString(remote.c_str()), kClientTimeoutMs), SUCCESS);
  EXPECT_TRUE(manager_.IsConnected(remote));
  EXPECT_EQ(manager_.Connect(AscendString(remote.c_str()), kClientTimeoutMs), ALREADY_CONNECTED);
  EXPECT_EQ(manager_.EnsureConnected(AscendString(remote.c_str()), kClientTimeoutMs), SUCCESS);

  EXPECT_EQ(manager_.Disconnect(AscendString(remote.c_str()), kClientTimeoutMs), SUCCESS);
  EXPECT_FALSE(manager_.IsConnected(remote));
  server.Stop();
  VirtualMemoryManager::GetInstance().Finalize();
}

TEST_F(FabricMemChannelManagerUTest, SendOutboundHeartbeatsAutoDisconnectsDeadRemote) {
  manager_.auto_connect_ = true;
  const std::string remote = "127.0.0.1:13106";
  int fds[2] = {-1, -1};
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  auto channel = AddChannel(remote);
  channel->keepalive_fd = fds[0];
  (void)close(fds[1]);

  manager_.SendOutboundHeartbeats();
  EXPECT_FALSE(manager_.IsConnected(remote));
}

TEST_F(FabricMemChannelManagerUTest, SendOutboundHeartbeatsKeepsLiveRemote) {
  manager_.auto_connect_ = true;
  const std::string remote = "127.0.0.1:13107";
  int fds[2] = {-1, -1};
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  DrainSocketInBackground(fds[1]);
  auto channel = AddChannel(remote);
  channel->keepalive_fd = fds[0];

  manager_.SendOutboundHeartbeats();
  EXPECT_TRUE(manager_.IsConnected(remote));
  (void)close(fds[0]);
  (void)close(fds[1]);
}

TEST_F(FabricMemChannelManagerUTest, SendOutboundHeartbeatFailureKeepsRemoteWithoutAutoConnect) {
  manager_.auto_connect_ = false;
  const std::string remote = "127.0.0.1:13108";
  int fds[2] = {-1, -1};
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  auto channel = AddChannel(remote);
  channel->keepalive_fd = fds[0];
  (void)close(fds[1]);

  manager_.SendOutboundHeartbeats();
  EXPECT_TRUE(manager_.IsConnected(remote));
  (void)close(fds[0]);
}

TEST_F(FabricMemChannelManagerUTest, RemoveChannelEntryLockedFinalizesImportedMemory) {
  VirtualMemoryManager::GetInstance().Finalize();
  ASSERT_EQ(VirtualMemoryManager::GetInstance().Initialize(), SUCCESS);
  const std::string remote = "127.0.0.1:13109";
  auto channel = AddChannel(remote);
  channel->remote_memory = MakeUnique<FabricMemRemoteMemory>();
  ASSERT_EQ(channel->remote_memory->Import({BuildShareHandle(0x5000UL, 512U)}, 0), SUCCESS);

  {
    std::lock_guard<std::mutex> lock(manager_.channels_mutex_);
    manager_.RemoveChannelEntryLocked(remote);
  }
  EXPECT_FALSE(manager_.IsConnected(remote));
  EXPECT_TRUE(channel->remote_memory->GetNewVaToOldVa().empty());
  VirtualMemoryManager::GetInstance().Finalize();
}

TEST_F(FabricMemChannelManagerUTest, KeepaliveMonitorStartStop) {
  EXPECT_EQ(manager_.StartKeepaliveMonitor(), SUCCESS);
  EXPECT_TRUE(manager_.keepalive_monitor_.joinable());
  manager_.CheckKeepaliveFds();
  manager_.StopKeepaliveMonitor();
  EXPECT_FALSE(manager_.keepalive_monitor_.joinable());
}

TEST(FabricMemEngineUTest, ConnectWhenAlreadyConnectedReturnsAlreadyConnected) {
  FabricMemEngine engine(AscendString("test_engine"));
  AttachTestContext(engine);
  const std::string remote = "127.0.0.1:12345";
  AddEngineChannel(engine, remote);
  EXPECT_EQ(engine.Connect(AscendString(remote.c_str()), kClientTimeoutMs), ALREADY_CONNECTED);
  EXPECT_TRUE(EngineManager(engine).IsConnected(remote));
}

TEST(FabricMemEngineUTest, TransferSyncConcurrentDisconnectAbortsAndCompletes) {
  VirtualMemoryManager::GetInstance().Finalize();
  ASSERT_EQ(VirtualMemoryManager::GetInstance().Initialize(), SUCCESS);

  auto runtime = std::make_shared<FabricMemRuntimeStub>();
  auto scoped_runtime = std::make_unique<ScopedRuntimeMock>(runtime);
  struct UnblockSyncGuard {
    std::shared_ptr<FabricMemRuntimeStub> runtime;
    ~UnblockSyncGuard() {
      if (runtime != nullptr) {
        runtime->unblock_sync_with_timeout_.store(true, std::memory_order_release);
      }
    }
  } unblock_guard{runtime};

  FabricMemEngine engine(AscendString("test_engine"));
  AttachTestContext(engine);
  const std::string remote = "127.0.0.1:12345";
  uint8_t remote_buf[kLen] = {};
  AddMappedServiceChannel(*engine.fabric_mem_transfer_service_, remote, remote_buf, kLen);

  uint8_t local_buf[kLen] = {};
  TransferOpDesc desc{reinterpret_cast<uintptr_t>(local_buf), reinterpret_cast<uintptr_t>(remote_buf), kLen};

  runtime->block_sync_with_timeout_.store(true, std::memory_order_release);
  auto transfer_future = std::async(std::launch::async, [&]() {
    return engine.TransferSync(AscendString(remote.c_str()), WRITE, {desc}, 60000);
  });

  auto wait_for_sync = [&]() {
    for (int i = 0; i < 500; ++i) {
      if (runtime->sync_with_timeout_entered_.load(std::memory_order_acquire) > 0U) {
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
  };
  ASSERT_TRUE(wait_for_sync());

  // Disconnect aborts the in-flight sync transfer's streams immediately (no wait) and removes the
  // channel right away.
  EXPECT_EQ(engine.Disconnect(AscendString(remote.c_str()), 0), SUCCESS);
  EXPECT_FALSE(EngineManager(engine).IsConnected(remote));
  EXPECT_GT(runtime->stream_abort_count_, 0U);

  runtime->unblock_sync_with_timeout_.store(true, std::memory_order_release);
  ASSERT_EQ(transfer_future.wait_for(std::chrono::seconds(5)), std::future_status::ready);
  (void)transfer_future.get();

  engine.fabric_mem_transfer_service_->Finalize();
  engine.fabric_mem_transfer_service_.reset();
  scoped_runtime.reset();
  runtime.reset();
  VirtualMemoryManager::GetInstance().Finalize();
}

void SetupPendingAsyncBeforeDisconnect(FabricMemEngine &engine, std::shared_ptr<FabricMemRuntimeStub> &runtime,
                                       std::unique_ptr<ScopedRuntimeMock> &scoped_runtime, const std::string &remote,
                                       TransferReq &req) {
  runtime = std::make_shared<FabricMemRuntimeStub>();
  scoped_runtime = std::make_unique<ScopedRuntimeMock>(runtime);
  AttachTestContext(engine);
  auto channel = AddEngineChannel(engine, remote);
  channel->remote_memory = std::make_unique<FabricMemRemoteMemory>();
  auto &service = *engine.fabric_mem_transfer_service_;
  AsyncSlot slot;
  ASSERT_EQ(service.slot_pool_.AcquireAsync(slot), SUCCESS);
  ASSERT_EQ(service.AppendHostFlagCopies(slot), SUCCESS);
  for (auto &stream : slot.streams) {
    runtime->streams_not_complete_.insert(stream);
  }
  for (void *host_flag : slot.host_flags) {
    *static_cast<uint64_t *>(host_flag) = 0ULL;
  }
  FabricMemTransferContext context;
  context.channel_id = remote;
  context.statistic_channel_id = FabricMemStatistic::GetClientStatisticChannelId(remote);
  req = reinterpret_cast<TransferReq>(static_cast<uintptr_t>(0xBEEFUL));
  RegisterServiceAsyncRecord(service, channel, context, req, std::move(slot), 64U, 1U);
}

TEST(FabricMemEngineUTest, DisconnectConcurrentWithSubmittedAsyncGetTransferStatus) {
  FabricMemEngine engine(AscendString("test_engine"));
  const std::string remote = "127.0.0.1:12345";
  std::shared_ptr<FabricMemRuntimeStub> runtime;
  std::unique_ptr<ScopedRuntimeMock> scoped_runtime;
  TransferReq req = nullptr;
  SetupPendingAsyncBeforeDisconnect(engine, runtime, scoped_runtime, remote, req);
  auto &service = *engine.fabric_mem_transfer_service_;

  std::atomic<bool> disconnect_done{false};
  std::atomic<Status> final_status_ret{SUCCESS};
  std::atomic<int32_t> final_transfer_status{-1};

  std::thread disconnect_thread([&]() {
    EXPECT_EQ(engine.Disconnect(AscendString(remote.c_str()), 100), SUCCESS);
    disconnect_done.store(true, std::memory_order_release);
  });

  std::thread status_thread([&]() {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
      TransferStatus status = TransferStatus::WAITING;
      const Status ret = engine.GetTransferStatus(req, status);
      if (ret != SUCCESS) {
        final_status_ret.store(ret, std::memory_order_release);
        break;
      }
      if (status != TransferStatus::WAITING) {
        final_transfer_status.store(static_cast<int32_t>(status), std::memory_order_release);
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });

  disconnect_thread.join();
  status_thread.join();

  EXPECT_TRUE(disconnect_done.load(std::memory_order_acquire));
  EXPECT_TRUE(service.channel_manager_.req_2_channel_.empty());
  EXPECT_FALSE(EngineManager(engine).IsConnected(remote));
  EXPECT_GT(runtime->stream_abort_count_, 0U);
  const Status status_ret = final_status_ret.load(std::memory_order_acquire);
  const int32_t transfer_status = final_transfer_status.load(std::memory_order_acquire);
  EXPECT_TRUE(status_ret == PARAM_INVALID || status_ret == FAILED ||
              transfer_status == static_cast<int32_t>(TransferStatus::FAILED));

  service.Finalize();
  engine.fabric_mem_transfer_service_.reset();
  scoped_runtime.reset();
  runtime.reset();
}

TEST(FabricMemEngineUTest, ConnectAndDisconnectRoundTrip) {
  const int32_t port = test::AllocateFabricMemTestPort();
  ASSERT_GT(port, 0);
  FabricMemControlServer server;
  ASSERT_EQ(server.Start("127.0.0.1:" + std::to_string(port),
                         [](std::vector<ShareHandleInfo> &handles) {
                           handles.emplace_back(BuildShareHandle(0xABCDUL, 512U));
                           return SUCCESS;
                         }),
            SUCCESS);

  FabricMemEngine engine(AscendString("test_engine"));
  AscendString remote(std::string("127.0.0.1:" + std::to_string(port)).c_str());
  engine.Connect(remote, kClientTimeoutMs);
  engine.Disconnect(remote, kClientTimeoutMs);
  engine.Disconnect();
  server.Stop();
}

TEST(FabricMemEngineUTest, DisconnectNoConnection) {
  auto log_capture = std::make_shared<llm::LogCaptureStub>();
  log_capture->SetLevel(DLOG_WARN);
  log_capture->AddCapturePattern("is not connected, skip disconnect");
  llm::SlogStub::SetInstance(log_capture);

  FabricMemEngine engine(AscendString("test_engine"));
  AttachTestContext(engine);
  AscendString remote("127.0.0.1:12345");
  EXPECT_EQ(engine.Disconnect(remote, kClientTimeoutMs), NOT_CONNECTED);
  EXPECT_TRUE(log_capture->WaitForAllPatternsCaptured(kCaptureLogTimeoutMs));
  EXPECT_TRUE(log_capture->IsPatternCaptured("is not connected, skip disconnect"));
  engine.Disconnect();
  llm::SlogStub::SetInstance(nullptr);
}

TEST(FabricMemEngineUTest, SetKeepaliveCheckIntervalMsClampsInvalidValue) {
  FabricMemEngine::SetKeepaliveCheckIntervalMs(0);
  FabricMemEngine::SetKeepaliveCheckIntervalMs(-1);
  FabricMemEngine::SetKeepaliveCheckIntervalMs(5000);
  FabricMemEngine::SetKeepaliveCheckIntervalMs(10000);
}

TEST(FabricMemEngineUTest, GetTransferStatusReturnsNotFoundForUnknownReq) {
  FabricMemEngine engine(AscendString("test_engine"));
  AttachTestContext(engine);
  const auto req = reinterpret_cast<TransferReq>(999U);
  TransferStatus status = TransferStatus::WAITING;
  EXPECT_EQ(engine.GetTransferStatus(req, status), PARAM_INVALID);
}

TEST(FabricMemEngineUTest, GetTransferStatusAsyncFailureDisconnectsWhenAutoConnect) {
  auto runtime = std::make_shared<FabricMemRuntimeStub>();
  auto scoped_runtime = std::make_unique<ScopedRuntimeMock>(runtime);

  FabricMemEngine engine(AscendString("test_engine"));
  engine.auto_connect_ = true;
  AttachTestContext(engine);

  const std::string remote = "127.0.0.1:12345";
  auto channel = AddEngineChannel(engine, remote);
  channel->remote_memory = std::make_unique<FabricMemRemoteMemory>();

  auto &service = *engine.fabric_mem_transfer_service_;
  AsyncSlot slot;
  ASSERT_EQ(service.slot_pool_.AcquireAsync(slot), SUCCESS);
  ASSERT_EQ(service.AppendHostFlagCopies(slot), SUCCESS);

  FabricMemTransferContext context;
  context.channel_id = remote;
  context.statistic_channel_id = FabricMemStatistic::GetClientStatisticChannelId(remote);

  const uint64_t req_id = 0xABCDUL;
  TransferReq req = reinterpret_cast<TransferReq>(req_id);
  RegisterServiceAsyncRecord(service, channel, context, req, std::move(slot), 64U, 1U);
  {
    std::lock_guard<std::mutex> lock(channel->records_mutex);
    for (void *host_flag : channel->async_records[req_id].slot.host_flags) {
      *static_cast<uint64_t *>(host_flag) = 0ULL;
    }
  }
  runtime->stream_query_error_ = ACL_ERROR_RT_INTERNAL_ERROR;

  TransferStatus status = TransferStatus::WAITING;
  EXPECT_EQ(engine.GetTransferStatus(req, status), SUCCESS);
  EXPECT_EQ(status, TransferStatus::FAILED);
  EXPECT_TRUE(service.channel_manager_.req_2_channel_.empty());
  EXPECT_FALSE(EngineManager(engine).IsConnected(remote));

  service.Finalize();
  engine.fabric_mem_transfer_service_.reset();
  scoped_runtime.reset();
  runtime.reset();
}

TEST(FabricMemEngineUTest, ConnectOnFinalizedEngineReturnsFailed) {
  FabricMemEngine engine(AscendString("test_engine"));
  engine.is_initialized_ = false;
  const AscendString remote("127.0.0.1:12345");
  EXPECT_EQ(engine.Connect(remote, kClientTimeoutMs), FAILED);
}

TEST(FabricMemEngineUTest, GetTransferStatusRequiresInitializedEngine) {
  FabricMemEngine engine(AscendString("test_engine"));
  const auto req = reinterpret_cast<TransferReq>(1U);
  TransferStatus status = TransferStatus::WAITING;
  EXPECT_EQ(engine.GetTransferStatus(req, status), FAILED);
}

TEST(FabricMemEngineUTest, TransferAsyncRejectsEmptyOpDescs) {
  FabricMemEngine engine(AscendString("test_engine"));
  AttachTestContext(engine);
  const std::string remote = "127.0.0.1:12345";
  AddEngineChannel(engine, remote);
  engine.auto_connect_ = false;

  TransferReq req = nullptr;
  std::vector<TransferOpDesc> op_descs;
  TransferArgs optional_args;
  EXPECT_EQ(engine.TransferAsync(AscendString(remote.c_str()), WRITE, op_descs, optional_args, req), PARAM_INVALID);
  EXPECT_TRUE(engine.fabric_mem_transfer_service_->channel_manager_.req_2_channel_.empty());
}

TEST(FabricMemEngineUTest, TransferAsyncRejectsWhenConnectionDisconnecting) {
  FabricMemEngine engine(AscendString("test_engine"));
  AttachTestContext(engine);
  const std::string remote = "127.0.0.1:12345";
  auto channel = AddEngineChannel(engine, remote);
  channel->remote_memory = std::make_unique<FabricMemRemoteMemory>();
  {
    std::lock_guard<std::mutex> lock(channel->records_mutex);
    channel->disconnecting = true;
  }
  engine.auto_connect_ = false;

  int32_t src = 1;
  int32_t dst = 2;
  TransferOpDesc desc{reinterpret_cast<uintptr_t>(&src), reinterpret_cast<uintptr_t>(&dst), sizeof(int32_t)};
  TransferReq req = nullptr;
  TransferArgs optional_args;
  EXPECT_EQ(engine.TransferAsync(AscendString(remote.c_str()), WRITE, {desc}, optional_args, req), NOT_CONNECTED);
}

TEST_F(FabricMemEngineInitUTest, KeepaliveMonitorStartsWithAutoConnect) {
  auto options = BuildFabricMemOptions();
  options[OPTION_AUTO_CONNECT] = AscendString("1");
  FabricMemEngine engine(AscendString("127.0.0.1:26000"));
  ASSERT_EQ(InitEngineWithOptions(engine, options), SUCCESS);
  EXPECT_TRUE(engine.fabric_mem_transfer_service_->channel_manager_.keepalive_monitor_.joinable());
  engine.Finalize();
  FabricMemEngine::SetKeepaliveCheckIntervalMs(10000);
}

TEST_F(FabricMemEngineInitUTest, KeepaliveMonitorNotStartedWithoutAutoConnect) {
  FabricMemEngine engine(AscendString("127.0.0.1:0"));
  ASSERT_EQ(InitEngineWithOptions(engine, BuildFabricMemOptions()), SUCCESS);
  EXPECT_FALSE(engine.fabric_mem_transfer_service_->channel_manager_.keepalive_monitor_.joinable());
  engine.Finalize();
}

TEST_F(FabricMemEngineInitUTest, KeepaliveMonitorStartsWhenListeningWithoutAutoConnect) {
  FabricMemEngine engine(AscendString("127.0.0.1:26002"));
  ASSERT_EQ(InitEngineWithOptions(engine, BuildFabricMemOptions()), SUCCESS);
  EXPECT_TRUE(engine.fabric_mem_transfer_service_->channel_manager_.keepalive_monitor_.joinable());
  engine.Finalize();
}

TEST_F(FabricMemEngineInitUTest, CheckKeepaliveFdsRunsServerTimeoutCheck) {
  FabricMemEngine engine(AscendString("127.0.0.1:26003"));
  ASSERT_EQ(InitEngineWithOptions(engine, BuildFabricMemOptions()), SUCCESS);
  engine.fabric_mem_transfer_service_->channel_manager_.CheckKeepaliveFds();
  engine.Finalize();
}

TEST_F(FabricMemEngineInitUTest, FabricMemoryCapacityConfig) {
  VirtualMemoryManager::GetInstance().Finalize();

  constexpr size_t kCustomCapacityTB = 32UL;
  const std::string json_config = R"({
    "fabric_memory": {
      "max_capacity": )" + std::to_string(kCustomCapacityTB) +
                                  R"(
    }
  })";

  FabricMemEngine engine{AscendString(kConfigEngineLocalId)};
  auto options = BuildFabricMemOptions();
  options[OPTION_GLOBAL_RESOURCE_CONFIG] = AscendString(json_config.c_str());
  EXPECT_EQ(InitEngineWithOptions(engine, options), SUCCESS);

  uintptr_t addr = 0U;
  EXPECT_EQ(VirtualMemoryManager::GetInstance().ReserveMemory(k1GB, addr), SUCCESS);
  EXPECT_NE(addr, 0U);
  EXPECT_EQ(VirtualMemoryManager::GetInstance().ReleaseMemory(addr), SUCCESS);

  engine.Finalize();
}

TEST_F(FabricMemEngineInitUTest, FabricMemoryInitFailureRollback) {
  VirtualMemoryManager::GetInstance().Finalize();

  FabricMemEngine engine{AscendString("invalid_local_engine")};
  EXPECT_NE(InitEngineWithOptions(engine, BuildFabricMemOptions()), SUCCESS);
  EXPECT_EQ(VirtualMemoryManager::GetInstance().Initialize(), SUCCESS);
  EXPECT_EQ(VirtualMemoryManager::GetInstance().SetGlobalStartAddress(50UL), PARAM_INVALID);
  VirtualMemoryManager::GetInstance().Finalize();
}

TEST_F(FabricMemEngineInitUTest, FabricMemEnabledOptionInitializesEngine) {
  VirtualMemoryManager::GetInstance().Finalize();

  FabricMemEngine engine{AscendString(kConfigEngineLocalId)};
  const auto options = BuildFabricMemOptions();
  HixlOptions parsed;
  ASSERT_EQ(HixlOptions::Parse(options, parsed), SUCCESS);
  EXPECT_TRUE(parsed.EnableFabricMem().value_or(false));
  EXPECT_EQ(InitEngineWithOptions(engine, options), SUCCESS);
  engine.Finalize();
  VirtualMemoryManager::GetInstance().Finalize();
}

TEST_F(FabricMemEngineInitUTest, StartAddressConfig) {
  for (size_t start_address_tb : {40UL, 220UL}) {
    VirtualMemoryManager::GetInstance().Finalize();
    const std::string json_config =
        R"({"fabric_memory": {"start_address": )" + std::to_string(start_address_tb) + R"(}})";
    FabricMemEngine engine{AscendString(kConfigEngineLocalId)};
    auto options = BuildFabricMemOptions();
    options[OPTION_GLOBAL_RESOURCE_CONFIG] = AscendString(json_config.c_str());
    EXPECT_EQ(InitEngineWithOptions(engine, options), SUCCESS);
    engine.Finalize();
    VirtualMemoryManager::GetInstance().Finalize();
  }

  VirtualMemoryManager::GetInstance().Finalize();
  EXPECT_EQ(VirtualMemoryManager::GetInstance().Initialize(), SUCCESS);
  EXPECT_EQ(VirtualMemoryManager::GetInstance().SetGlobalStartAddress(40UL), PARAM_INVALID);
  VirtualMemoryManager::GetInstance().Finalize();
}

TEST_F(FabricMemEngineInitUTest, StartAddressInvalidConfig) {
  VirtualMemoryManager::GetInstance().Finalize();

  FabricMemEngine engine{AscendString(kConfigEngineLocalId)};
  auto options = BuildFabricMemOptions();
  options[OPTION_GLOBAL_RESOURCE_CONFIG] = AscendString(R"({"fabric_memory": {"start_address": 221}})");
  EXPECT_EQ(InitEngineWithOptions(engine, options), PARAM_INVALID);
}

TEST_F(FabricMemEngineInitUTest, TaskStreamNumConfig) {
  VirtualMemoryManager::GetInstance().Finalize();

  for (size_t task_stream_num = 1U; task_stream_num <= 8U; ++task_stream_num) {
    const std::string json_config =
        R"({"fabric_memory": {"task_stream_num": )" + std::to_string(task_stream_num) + R"(}})";
    FabricMemEngine engine{AscendString(kConfigEngineLocalId)};
    auto options = BuildFabricMemOptions();
    options[OPTION_GLOBAL_RESOURCE_CONFIG] = AscendString(json_config.c_str());
    EXPECT_EQ(InitEngineWithOptions(engine, options), SUCCESS);
    engine.Finalize();
  }
  VirtualMemoryManager::GetInstance().Finalize();
}

TEST_F(FabricMemEngineInitUTest, TaskStreamNumInvalidConfig) {
  VirtualMemoryManager::GetInstance().Finalize();

  for (const char *json_config :
       {R"({"fabric_memory": {"task_stream_num": 0}})", R"({"fabric_memory": {"task_stream_num": 9}})"}) {
    FabricMemEngine engine{AscendString(kConfigEngineLocalId)};
    auto options = BuildFabricMemOptions();
    options[OPTION_GLOBAL_RESOURCE_CONFIG] = AscendString(json_config);
    EXPECT_EQ(InitEngineWithOptions(engine, options), PARAM_INVALID);
  }
}

TEST_F(FabricMemEngineInitUTest, TaskStreamNumInvalidString) {
  VirtualMemoryManager::GetInstance().Finalize();

  FabricMemEngine engine{AscendString(kConfigEngineLocalId)};
  auto options = BuildFabricMemOptions();
  options[OPTION_GLOBAL_RESOURCE_CONFIG] = AscendString(R"({"fabric_memory": {"task_stream_num": "not_a_number"}})");
  EXPECT_EQ(InitEngineWithOptions(engine, options), PARAM_INVALID);
}

TEST_F(FabricMemEngineInitUTest, TaskStreamNumEmptyString) {
  VirtualMemoryManager::GetInstance().Finalize();

  FabricMemEngine engine{AscendString(kConfigEngineLocalId)};
  auto options = BuildFabricMemOptions();
  options[OPTION_GLOBAL_RESOURCE_CONFIG] = AscendString(R"({"fabric_memory": {"task_stream_num": ""}})");
  EXPECT_EQ(InitEngineWithOptions(engine, options), PARAM_INVALID);
}

TEST_F(FabricMemEngineInitUTest, FabricMemRegisterMemOverflow) {
  VirtualMemoryManager::GetInstance().Finalize();

  FabricMemEngine engine{AscendString(kConfigEngineLocalId)};
  ASSERT_EQ(InitEngineWithOptions(engine, BuildFabricMemOptions()), SUCCESS);

  MemDesc mem{};
  mem.addr = std::numeric_limits<uintptr_t>::max();
  mem.len = 2U;
  MemHandle handle = nullptr;
  EXPECT_EQ(engine.RegisterMem(mem, MEM_HOST, handle), PARAM_INVALID);
  EXPECT_EQ(handle, nullptr);

  engine.Finalize();
}

TEST_F(FabricMemEngineInitUTest, FabricMemTransferAsyncFailureClearsReq) {
  VirtualMemoryManager::GetInstance().Finalize();

  FabricMemEngine engine{AscendString(kConfigEngineLocalId)};
  ASSERT_EQ(InitEngineWithOptions(engine, BuildFabricMemOptions()), SUCCESS);

  int32_t src = 1;
  int32_t dst = 2;
  TransferOpDesc desc{reinterpret_cast<uintptr_t>(&src), reinterpret_cast<uintptr_t>(&dst), sizeof(int32_t)};
  TransferReq req = reinterpret_cast<TransferReq>(0x1234U);
  EXPECT_EQ(engine.TransferAsync(AscendString(kConfigRemoteEngineId), WRITE, {desc}, {}, req), NOT_CONNECTED);
  EXPECT_EQ(req, nullptr);

  engine.Finalize();
}

}  // namespace hixl
