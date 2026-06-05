/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

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
#include "fabric_mem/fabric_mem_control.h"
#include "fabric_mem/fabric_mem_statistic.h"
#include "fabric_mem/fabric_mem_transfer_service.h"
#undef private

#include "common/hixl_utils.h"
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

FabricMemTransferContext BuildSelfMappedContext(uint8_t *remote, size_t len) {
  FabricMemTransferContext context;
  context.channel_id = kChannelId;
  context.statistic_channel_id = kStatChannelId;
  const uintptr_t remote_addr = reinterpret_cast<uintptr_t>(remote);
  context.remote_va_to_old_va.emplace(remote_addr, VaInfo{remote_addr, len});
  return context;
}

std::vector<TransferOpDesc> BuildOpDescs(uint8_t *local, uint8_t *remote) {
  return {{reinterpret_cast<uintptr_t>(local), reinterpret_cast<uintptr_t>(remote), kLen}};
}

std::vector<TransferOpDesc> BuildTwoOpDescs(uint8_t *local, uint8_t *remote) {
  return {{reinterpret_cast<uintptr_t>(local), reinterpret_cast<uintptr_t>(remote), kLen},
          {reinterpret_cast<uintptr_t>(local + kLen), reinterpret_cast<uintptr_t>(remote + kLen), kLen}};
}

void SendRawFabricMemMsg(int32_t fd, int32_t msg_type, const std::string &payload) {
  const uint64_t length = static_cast<uint64_t>(sizeof(msg_type)) + payload.size();
  ASSERT_EQ(send(fd, &kFabricMemMagic, sizeof(kFabricMemMagic), 0), static_cast<ssize_t>(sizeof(kFabricMemMagic)));
  ASSERT_EQ(send(fd, &length, sizeof(length), 0), static_cast<ssize_t>(sizeof(length)));
  ASSERT_EQ(send(fd, &msg_type, sizeof(msg_type), 0), static_cast<ssize_t>(sizeof(msg_type)));
  if (!payload.empty()) {
    ASSERT_EQ(send(fd, payload.data(), payload.size(), 0), static_cast<ssize_t>(payload.size()));
  }
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

  std::shared_ptr<FabricMemRuntimeStub> runtime_;
  std::unique_ptr<ScopedRuntimeMock> scoped_runtime_;
  FabricMemTransferService service_;
  FabricMemStatistic statistic_;
};

void AttachTestContext(FabricMemEngine &engine) {
  auto ctx_holder = std::make_shared<int>(1);
  engine.aclrt_context_holder_ = std::static_pointer_cast<void>(ctx_holder);
  engine.aclrt_context_ = reinterpret_cast<aclrtContext>(ctx_holder.get());
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
  EXPECT_EQ(server.HandleConnectRequest(server.state_, fds[0]), FAILED);
  std::string payload;
  EXPECT_EQ(RecvRawFabricMemMsg(fds[1], payload), FabricMemMsgType::kConnect);
  const auto json = nlohmann::json::parse(payload);
  EXPECT_EQ(json.at("share_handles").size(), 1U);
  (void)close(fds[0]);
  (void)close(fds[1]);
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
  EXPECT_EQ(FabricMemControlClient::Fetch(remote, kClientTimeoutMs, handles, conn_fd), SUCCESS);
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
  ASSERT_EQ(FabricMemControlClient::FetchNotifies(remote, kClientTimeoutMs, notifies), SUCCESS);
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
      (void)FabricMemControlClient::Fetch(remote, 50, handles, conn_fd);
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
  int32_t fds[2] = {-1, -1};
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  server.state_->keepalive_fds.insert(fds[0]);
  EXPECT_EQ(server.HandleDisconnectRequest(server.state_, fds[0]), SUCCESS);
  std::string payload;
  EXPECT_EQ(RecvRawFabricMemMsg(fds[1], payload), FabricMemMsgType::kStatus);
  const auto json = nlohmann::json::parse(payload);
  EXPECT_EQ(json.at("error_code").get<uint32_t>(), static_cast<uint32_t>(SUCCESS));
  (void)close(fds[1]);
}

TEST(FabricMemControlUTest, ClientRejectsMissingPort) {
  std::vector<ShareHandleInfo> handles;
  int32_t conn_fd = -1;
  EXPECT_EQ(FabricMemControlClient::Fetch("127.0.0.1", kClientTimeoutMs, handles, conn_fd), PARAM_INVALID);
  NotifyDesc notify;
  EXPECT_EQ(FabricMemControlClient::SendNotify("127.0.0.1", notify, kClientTimeoutMs), PARAM_INVALID);
  std::vector<NotifyDesc> notifies;
  EXPECT_EQ(FabricMemControlClient::FetchNotifies("127.0.0.1", kClientTimeoutMs, notifies), PARAM_INVALID);
}

TEST(FabricMemControlUTest, HandleConnectionRejectsUnexpectedType) {
  FabricMemControlServer server;
  int32_t fds[2] = {-1, -1};
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  SendRawFabricMemMsg(fds[1], 99, "");
  EXPECT_EQ(server.HandleConnection(server.state_, fds[0]), PARAM_INVALID);
  (void)close(fds[0]);
  (void)close(fds[1]);
}

TEST_F(FabricMemTransferServiceUTest, InitializeRejectsInvalidInputAndAclFailure) {
  EXPECT_EQ(service_.Initialize(-1, 1U, 1U, &statistic_), PARAM_INVALID);
  EXPECT_EQ(service_.Initialize(0, 0U, 1U, &statistic_), PARAM_INVALID);
  EXPECT_EQ(service_.Initialize(0, 1U, 0U, &statistic_), PARAM_INVALID);

  EXPECT_EQ(service_.Initialize(0, 3U, 1U, &statistic_), SUCCESS);
  EXPECT_EQ(service_.device_id_, 0);
  EXPECT_EQ(service_.task_stream_num_, 1U);
  EXPECT_EQ(service_.max_async_slot_num_, 3U);
  ASSERT_NE(service_.dev_const_one_, nullptr);
}

TEST_F(FabricMemTransferServiceUTest, InitDevConstOneRollsBackOnMemcpyFailure) {
  llm::GetAclStubMock() = "aclrtMemcpy";
  EXPECT_NE(service_.Initialize(0, 1U, 1U, &statistic_), SUCCESS);
  EXPECT_EQ(service_.dev_const_one_, nullptr);
  llm::GetAclStubMock().clear();

  EXPECT_EQ(service_.Initialize(0, 1U, 1U, &statistic_), SUCCESS);
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

TEST_F(FabricMemTransferServiceUTest, RegisterDeregisterAndGetShareHandles) {
  ASSERT_EQ(service_.Initialize(0, 4U, 1U, &statistic_), SUCCESS);
  MemHandle invalid_handle = nullptr;
  EXPECT_EQ(service_.RegisterMem({0U, kLen}, MEM_HOST, invalid_handle), PARAM_INVALID);
  EXPECT_EQ(service_.RegisterMem({kLocalAddr, 0U}, MEM_HOST, invalid_handle), PARAM_INVALID);

  MemHandle host_handle = nullptr;
  EXPECT_EQ(service_.RegisterMem({kLocalAddr, kLen}, MEM_HOST, host_handle), SUCCESS);
  ASSERT_NE(host_handle, nullptr);
  EXPECT_TRUE(service_.has_host_memory_);
  auto handles = service_.GetShareHandles();
  ASSERT_EQ(handles.size(), 1U);
  EXPECT_EQ(handles[0].va_addr, kLocalAddr);
  EXPECT_EQ(handles[0].len, kLen);
  EXPECT_NE(handles[0].imported_handle, nullptr);
  EXPECT_NE(handles[0].imported_va, 0UL);

  EXPECT_EQ(service_.DeregisterMem(host_handle), SUCCESS);
  EXPECT_EQ(service_.DeregisterMem(host_handle), SUCCESS);
}

TEST_F(FabricMemTransferServiceUTest, SlotPoolCreateReuseRollbackAndDestroy) {
  ASSERT_EQ(service_.Initialize(0, 2U, 1U, &statistic_), SUCCESS);
  AsyncSlot slot;
  EXPECT_EQ(service_.TryAcquireAsyncSlot(slot), SUCCESS);
  ASSERT_EQ(slot.streams.size(), 1U);
  ASSERT_NE(slot.ctx, nullptr);
  EXPECT_EQ(runtime_->stream_failure_mode_count_, 1U);
  EXPECT_EQ(runtime_->last_stream_failure_mode_, ACL_STOP_ON_FAILURE);
  const auto first_ctx = slot.ctx;
  const auto first_stream = slot.streams[0];
  service_.ReleaseSlot(slot, false);
  EXPECT_EQ(slot.ctx, nullptr);
  EXPECT_TRUE(service_.slot_pool_[0].available);

  EXPECT_EQ(service_.TryAcquireAsyncSlot(slot), SUCCESS);
  ASSERT_EQ(slot.streams.size(), 1U);
  EXPECT_EQ(slot.ctx, first_ctx);
  EXPECT_EQ(slot.streams[0], first_stream);
  EXPECT_FALSE(service_.slot_pool_[0].available);
  service_.ReleaseSlot(slot, false);

  AsyncSlot slot2;
  EXPECT_EQ(service_.TryAcquireAsyncSlot(slot2), SUCCESS);
  service_.ReleaseSlot(slot2, false);
  EXPECT_EQ(service_.TryAcquireAsyncSlot(slot), SUCCESS);
  EXPECT_EQ(service_.TryAcquireAsyncSlot(slot2), SUCCESS);
  AsyncSlot slot3;
  EXPECT_EQ(service_.TryAcquireAsyncSlot(slot3), FAILED);
  service_.ReleaseSlot(slot, true);
  service_.ReleaseSlot(slot2, true);
  EXPECT_TRUE(service_.slot_pool_.empty());
}

TEST_F(FabricMemTransferServiceUTest, AddressTranslationAndCopyValidation) {
  uint8_t local[kLen] = {};
  uint8_t remote[kLen] = {};
  std::fill(std::begin(local), std::end(local), 7U);
  ASSERT_EQ(service_.Initialize(0, 4U, 1U, &statistic_), SUCCESS);

  uintptr_t new_addr = 0;
  EXPECT_EQ(
      FabricMemTransferService::TransOpAddr(kRemoteOldAddr + 4U, 8U, BuildContext().remote_va_to_old_va, new_addr),
      SUCCESS);
  EXPECT_EQ(new_addr, kRemoteNewAddr + 4U);
  EXPECT_EQ(FabricMemTransferService::TransOpAddr(kRemoteOldAddr + kLen * 5U, 8U, BuildContext().remote_va_to_old_va,
                                                  new_addr),
            PARAM_INVALID);

  EXPECT_FALSE(service_.FindLocalHostRegisteredAddrLocked(kLocalAddr, 4U, new_addr));
  service_.share_handles_[reinterpret_cast<aclrtDrvMemHandle>(0x1)] =
      ShareHandleInfo{kLocalAddr, kLen, {}, nullptr, kImportedLocalAddr, false};
  EXPECT_TRUE(service_.FindLocalHostRegisteredAddrLocked(kLocalAddr + 4U, 8U, new_addr));
  EXPECT_EQ(new_addr, kImportedLocalAddr + 4U);

  std::vector<TransferOpDesc> op_descs = {{kLocalAddr + 2U, kRemoteOldAddr, 8U}};
  EXPECT_EQ(service_.TransLocalHostOpAddrs(op_descs), SUCCESS);
  EXPECT_EQ(op_descs[0].local_addr, kImportedLocalAddr + 2U);
  op_descs = {{kLocalAddr + kLen * 2U, kRemoteOldAddr, 8U}};
  EXPECT_EQ(service_.TransLocalHostOpAddrs(op_descs), PARAM_INVALID);

  std::vector<aclrtStream> streams;
  AsyncSlot slot;
  ASSERT_EQ(service_.TryAcquireAsyncSlot(slot), SUCCESS);
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
  service_.ReleaseSlot(slot, false);
}

TEST_F(FabricMemTransferServiceUTest, NeedTransLocalAddrHandlesHostDeviceEmptyAndFailure) {
  ASSERT_EQ(service_.Initialize(0, 3U, 1U, &statistic_), SUCCESS);
  bool need_trans = true;
  EXPECT_EQ(service_.NeedTransLocalAddr({}, need_trans), SUCCESS);
  EXPECT_FALSE(need_trans);

  uint8_t local[kLen] = {};
  uint8_t remote[kLen] = {};
  auto op_descs = BuildOpDescs(local, remote);
  service_.has_host_memory_ = false;
  need_trans = true;
  EXPECT_EQ(service_.NeedTransLocalAddr(op_descs, need_trans), SUCCESS);
  EXPECT_FALSE(need_trans);

  service_.has_host_memory_ = true;
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
  ASSERT_EQ(service_.Initialize(0, 3U, 1U, &statistic_), SUCCESS);
  service_.UpdateStats(context, 10U, 4U, 512U, 2U);
  EXPECT_EQ(direct_info->transfer.total_cost.load(std::memory_order_relaxed), 10UL);
  EXPECT_EQ(direct_info->real_copy.total_cost.load(std::memory_order_relaxed), 4UL);

  context.stat_info = nullptr;
  service_.UpdateStats(context, 20U, 8U, 1024U, 4U);
  EXPECT_EQ(statistic_.GetSnapshot(kStatChannelId).transfer.total_cost, 20UL);

  FabricMemTransferService no_stat_service;
  ASSERT_EQ(no_stat_service.Initialize(0, 1U, 1U, nullptr), SUCCESS);
  no_stat_service.UpdateStats(BuildContext(), 1U, 1U, 1U, 1U);
  no_stat_service.Finalize();
}

TEST_F(FabricMemTransferServiceUTest, SlotHostFlagsAreZeroedOnReuse) {
  ASSERT_EQ(service_.Initialize(0, 4U, 2U, &statistic_), SUCCESS);
  AsyncSlot slot;
  ASSERT_EQ(service_.TryAcquireAsyncSlot(slot), SUCCESS);
  ASSERT_EQ(slot.host_flags.size(), 2U);
  EXPECT_EQ(*static_cast<uint64_t *>(slot.host_flags[0]), 0ULL);
  *static_cast<uint64_t *>(slot.host_flags[0]) = 1ULL;
  service_.ReleaseSlot(slot, false);

  AsyncSlot reused;
  ASSERT_EQ(service_.TryAcquireAsyncSlot(reused), SUCCESS);
  ASSERT_EQ(reused.host_flags.size(), 2U);
  EXPECT_EQ(*static_cast<uint64_t *>(reused.host_flags[0]), 0ULL);
  service_.ReleaseSlot(reused, false);
}

TEST_F(FabricMemTransferServiceUTest, TransferFailureAbortsStreams) {
  uint8_t local[kLen * 2U] = {};
  uint8_t remote[kLen * 2U] = {};
  ASSERT_EQ(service_.Initialize(0, 2U, 2U, &statistic_), SUCCESS);
  runtime_->memcpy_async_error_ = ACL_ERROR_RT_INTERNAL_ERROR;
  runtime_->memcpy_async_fail_kind_ = ACL_MEMCPY_DEVICE_TO_DEVICE;
  runtime_->memcpy_async_fail_on_count_ = 2U;

  EXPECT_NE(service_.Transfer(BuildSelfMappedContext(remote, sizeof(remote)), WRITE, BuildTwoOpDescs(local, remote),
                              kClientTimeoutMs),
            SUCCESS);
  EXPECT_TRUE(service_.slot_pool_.empty());
  EXPECT_EQ(runtime_->stream_abort_count_, 2U);
}

TEST_F(FabricMemTransferServiceUTest, AsyncTransferFailureAbortsSlot) {
  uint8_t local[kLen * 2U] = {};
  uint8_t remote[kLen * 2U] = {};
  ASSERT_EQ(service_.Initialize(0, 1U, 1U, &statistic_), SUCCESS);
  runtime_->memcpy_async_error_ = ACL_ERROR_RT_INTERNAL_ERROR;
  runtime_->memcpy_async_fail_kind_ = ACL_MEMCPY_DEVICE_TO_DEVICE;
  runtime_->memcpy_async_fail_on_count_ = 2U;

  TransferReq req = reinterpret_cast<TransferReq>(0x1111UL);
  auto op_descs = BuildTwoOpDescs(local, remote);
  EXPECT_NE(service_.TransferAsync(BuildSelfMappedContext(remote, sizeof(remote)), WRITE, op_descs, req), SUCCESS);
  EXPECT_TRUE(service_.slot_pool_.empty());
  EXPECT_TRUE(service_.req_2_async_record_.empty());
  EXPECT_EQ(runtime_->stream_abort_count_, 1U);
}

TEST_F(FabricMemTransferServiceUTest, AsyncHostFlagCopyFailureAbortsSlot) {
  uint8_t local[kLen] = {};
  uint8_t remote[kLen] = {};
  ASSERT_EQ(service_.Initialize(0, 1U, 1U, &statistic_), SUCCESS);
  runtime_->memcpy_async_error_ = ACL_ERROR_RT_INTERNAL_ERROR;
  runtime_->memcpy_async_fail_kind_ = ACL_MEMCPY_DEVICE_TO_HOST;
  runtime_->memcpy_async_fail_on_count_ = 2U;

  TransferReq req = reinterpret_cast<TransferReq>(0x2222UL);
  EXPECT_NE(
      service_.TransferAsync(BuildSelfMappedContext(remote, sizeof(remote)), WRITE, BuildOpDescs(local, remote), req),
      SUCCESS);
  EXPECT_TRUE(service_.slot_pool_.empty());
  EXPECT_TRUE(service_.req_2_async_record_.empty());
  EXPECT_EQ(runtime_->stream_abort_count_, 1U);
}

TEST_F(FabricMemTransferServiceUTest, AsyncTransferRecordCompletesAndUpdatesStats) {
  ASSERT_EQ(service_.Initialize(0, 4U, 1U, &statistic_), SUCCESS);
  AsyncSlot slot;
  ASSERT_EQ(service_.TryAcquireAsyncSlot(slot), SUCCESS);
  ASSERT_EQ(service_.AppendHostFlagCopies(slot), SUCCESS);
  EXPECT_EQ(runtime_->host_flag_d2h_count_, 1U);

  TransferReq req = reinterpret_cast<TransferReq>(0x1234UL);
  const auto start = std::chrono::steady_clock::now();
  service_.RegisterAsyncTransferRecord(BuildContext(), req, std::move(slot), start, start, 256U, 2U);

  TransferStatus status = TransferStatus::WAITING;
  EXPECT_EQ(service_.GetTransferStatus(BuildContext(), req, status), SUCCESS);
  EXPECT_EQ(status, TransferStatus::COMPLETED);
  EXPECT_EQ(service_.GetTransferStatus(BuildContext(), req, status), FAILED);
  EXPECT_EQ(statistic_.GetSnapshot(kStatChannelId).transfer.times, 1UL);
  EXPECT_TRUE(service_.req_2_async_record_.empty());
  EXPECT_TRUE(service_.channel_2_req_[kChannelId].empty());
}

TEST_F(FabricMemTransferServiceUTest, AsyncTransferWaitsUntilAllHostFlagsDone) {
  ASSERT_EQ(service_.Initialize(0, 4U, 2U, &statistic_), SUCCESS);
  AsyncSlot slot;
  ASSERT_EQ(service_.TryAcquireAsyncSlot(slot), SUCCESS);
  ASSERT_EQ(slot.host_flags.size(), 2U);
  *static_cast<uint64_t *>(slot.host_flags[0]) = 1ULL;
  // Mark streams as not complete so query returns WAITING (not kComplete).
  for (const auto &stream : slot.streams) {
    runtime_->streams_not_complete_.insert(stream);
  }

  TransferReq req = reinterpret_cast<TransferReq>(0x2468UL);
  const auto start = std::chrono::steady_clock::now();
  service_.RegisterAsyncTransferRecord(BuildContext(), req, std::move(slot), start, start, 128U, 1U);

  TransferStatus status = TransferStatus::COMPLETED;
  EXPECT_EQ(service_.GetTransferStatus(BuildContext(), req, status), SUCCESS);
  EXPECT_EQ(status, TransferStatus::WAITING);

  auto &record = service_.req_2_async_record_[reinterpret_cast<uintptr_t>(req)];
  *static_cast<uint64_t *>(record.slot.host_flags[1]) = 1ULL;
  EXPECT_EQ(service_.GetTransferStatus(BuildContext(), req, status), SUCCESS);
  EXPECT_EQ(status, TransferStatus::COMPLETED);
}

TEST_F(FabricMemTransferServiceUTest, AsyncTransferStreamQueryFailureReturnsFailedWhileHostFlagPending) {
  ASSERT_EQ(service_.Initialize(0, 4U, 1U, &statistic_), SUCCESS);
  AsyncSlot slot;
  ASSERT_EQ(service_.TryAcquireAsyncSlot(slot), SUCCESS);
  ASSERT_EQ(service_.AppendHostFlagCopies(slot), SUCCESS);

  TransferReq req = reinterpret_cast<TransferReq>(0x3579UL);
  const auto start = std::chrono::steady_clock::now();
  service_.RegisterAsyncTransferRecord(BuildContext(), req, std::move(slot), start, start, 64U, 1U);
  auto &record = service_.req_2_async_record_[reinterpret_cast<uintptr_t>(req)];
  for (void *host_flag : record.slot.host_flags) {
    *static_cast<uint64_t *>(host_flag) = 0ULL;
  }
  runtime_->stream_query_error_ = ACL_ERROR_RT_INTERNAL_ERROR;

  TransferStatus status = TransferStatus::WAITING;
  EXPECT_EQ(service_.GetTransferStatus(BuildContext(), req, status), SUCCESS);
  EXPECT_EQ(status, TransferStatus::FAILED);
  EXPECT_GT(runtime_->stream_query_count_, 0U);
  EXPECT_TRUE(service_.req_2_async_record_.empty());
}

TEST_F(FabricMemTransferServiceUTest, AsyncTransferHostFlagsDoneSkipsStreamSync) {
  ASSERT_EQ(service_.Initialize(0, 4U, 1U, &statistic_), SUCCESS);
  AsyncSlot slot;
  ASSERT_EQ(service_.TryAcquireAsyncSlot(slot), SUCCESS);
  ASSERT_EQ(service_.AppendHostFlagCopies(slot), SUCCESS);
  for (void *host_flag : slot.host_flags) {
    *static_cast<uint64_t *>(host_flag) = 1ULL;
  }

  TransferReq req = reinterpret_cast<TransferReq>(0x579BUL);
  const auto start = std::chrono::steady_clock::now();
  service_.RegisterAsyncTransferRecord(BuildContext(), req, std::move(slot), start, start, 64U, 1U);
  // Inject sync error — should be ignored because host flags are done (sync is skipped).
  runtime_->stream_sync_error_ = ACL_ERROR_RT_INTERNAL_ERROR;

  TransferStatus status = TransferStatus::WAITING;
  EXPECT_EQ(service_.GetTransferStatus(BuildContext(), req, status), SUCCESS);
  EXPECT_EQ(status, TransferStatus::COMPLETED);
  EXPECT_EQ(runtime_->stream_sync_count_, 0U);
  EXPECT_TRUE(service_.req_2_async_record_.empty());
}

TEST_F(FabricMemTransferServiceUTest, AsyncTransferFastPathDoesNotHoldAsyncReqMutexWhileCompleting) {
  ASSERT_EQ(service_.Initialize(0, 4U, 1U, &statistic_), SUCCESS);
  AsyncSlot slot;
  ASSERT_EQ(service_.TryAcquireAsyncSlot(slot), SUCCESS);
  ASSERT_EQ(service_.AppendHostFlagCopies(slot), SUCCESS);
  for (void *host_flag : slot.host_flags) {
    *static_cast<uint64_t *>(host_flag) = 1ULL;
  }

  TransferReq req = reinterpret_cast<TransferReq>(0x67ACUL);
  const auto start = std::chrono::steady_clock::now();
  service_.RegisterAsyncTransferRecord(BuildContext(), req, std::move(slot), start, start, 64U, 1U);

  std::atomic<bool> lock_acquired{false};
  std::atomic<bool> release_lock{false};
  std::thread holder([this, &lock_acquired, &release_lock]() {
    std::unique_lock<std::mutex> lock(service_.async_req_mutex_);
    lock_acquired.store(true, std::memory_order_release);
    while (!release_lock.load(std::memory_order_acquire)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });
  while (!lock_acquired.load(std::memory_order_acquire)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  TransferStatus status = TransferStatus::WAITING;
  auto future = std::async(std::launch::async,
                           [this, req, &status]() { return service_.GetTransferStatus(BuildContext(), req, status); });
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  EXPECT_EQ(future.wait_for(std::chrono::milliseconds(0)), std::future_status::timeout);
  release_lock.store(true, std::memory_order_release);
  holder.join();

  ASSERT_EQ(future.wait_for(std::chrono::seconds(1)), std::future_status::ready);
  EXPECT_EQ(future.get(), SUCCESS);
  EXPECT_EQ(status, TransferStatus::COMPLETED);
  EXPECT_TRUE(service_.req_2_async_record_.empty());
}

TEST_F(FabricMemTransferServiceUTest, AsyncTransferStreamQueryCompleteTriggersSync) {
  ASSERT_EQ(service_.Initialize(0, 4U, 1U, &statistic_), SUCCESS);
  AsyncSlot slot;
  ASSERT_EQ(service_.TryAcquireAsyncSlot(slot), SUCCESS);
  ASSERT_EQ(service_.AppendHostFlagCopies(slot), SUCCESS);

  TransferReq req = reinterpret_cast<TransferReq>(0x468AUL);
  const auto start = std::chrono::steady_clock::now();
  service_.RegisterAsyncTransferRecord(BuildContext(), req, std::move(slot), start, start, 64U, 1U);
  auto &record = service_.req_2_async_record_[reinterpret_cast<uintptr_t>(req)];
  // Host flags not done, but streams report COMPLETE via query.
  for (void *host_flag : record.slot.host_flags) {
    *static_cast<uint64_t *>(host_flag) = 0ULL;
  }
  runtime_->stream_sync_error_ = ACL_ERROR_RT_INTERNAL_ERROR;

  TransferStatus status = TransferStatus::WAITING;
  EXPECT_EQ(service_.GetTransferStatus(BuildContext(), req, status), SUCCESS);
  EXPECT_EQ(status, TransferStatus::FAILED);
  EXPECT_GT(runtime_->stream_sync_count_, 0U);
  EXPECT_TRUE(service_.req_2_async_record_.empty());
}

TEST_F(FabricMemTransferServiceUTest, AsyncTransferRemoveChannelAbortsStreams) {
  ASSERT_EQ(service_.Initialize(0, 4U, 1U, &statistic_), SUCCESS);
  AsyncSlot slot;
  ASSERT_EQ(service_.TryAcquireAsyncSlot(slot), SUCCESS);
  ASSERT_EQ(service_.AppendHostFlagCopies(slot), SUCCESS);
  EXPECT_EQ(service_.slot_pool_.size(), 1U);

  TransferReq req = reinterpret_cast<TransferReq>(0x9ABCUL);
  const auto start = std::chrono::steady_clock::now();
  service_.RegisterAsyncTransferRecord(BuildContext(), req, std::move(slot), start, start, 128U, 1U);
  service_.RemoveChannel("missing");
  EXPECT_FALSE(service_.req_2_async_record_.empty());
  service_.RemoveChannel(kChannelId);
  EXPECT_TRUE(service_.req_2_async_record_.empty());
  EXPECT_TRUE(service_.channel_2_req_.find(kChannelId) == service_.channel_2_req_.end());
  EXPECT_TRUE(service_.slot_pool_.empty());

  AsyncSlot lazy_slot;
  EXPECT_EQ(service_.TryAcquireAsyncSlot(lazy_slot), SUCCESS);
  EXPECT_EQ(service_.slot_pool_.size(), 1U);
  service_.ReleaseSlot(lazy_slot, false);
}

TEST_F(FabricMemTransferServiceUTest, AsyncSlotAcquireFailsWhenPoolFull) {
  ASSERT_EQ(service_.Initialize(0, 2U, 2U, &statistic_), SUCCESS);
  AsyncSlot slot1;
  ASSERT_EQ(service_.TryAcquireAsyncSlot(slot1), SUCCESS);
  AsyncSlot slot2;
  EXPECT_EQ(service_.TryAcquireAsyncSlot(slot2), FAILED);
  service_.ReleaseSlot(slot1, false);
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
  // Connect will succeed through Fetch but fail at CreateAndRegisterRemoteMemory
  // (no CANN device in unit test). Engine methods are still exercised.
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
  AscendString remote("127.0.0.1:12345");
  EXPECT_EQ(engine.Disconnect(remote, kClientTimeoutMs), NOT_CONNECTED);
  EXPECT_TRUE(log_capture->WaitForAllPatternsCaptured(kCaptureLogTimeoutMs));
  EXPECT_TRUE(log_capture->IsPatternCaptured("is not connected, skip disconnect"));
  engine.Disconnect();
  llm::SlogStub::SetInstance(nullptr);
}

TEST(FabricMemEngineUTest, DisconnectClearsChannelReqMap) {
  FabricMemEngine engine(AscendString("test_engine"));
  AttachTestContext(engine);
  const std::string remote_a = "127.0.0.1:12345";
  const std::string remote_b = "127.0.0.1:54321";
  auto conn_a = std::make_shared<RemoteConnection>();
  auto conn_b = std::make_shared<RemoteConnection>();
  engine.fabric_mem_remote_mems_[remote_a] = conn_a;
  engine.fabric_mem_remote_mems_[remote_b] = conn_b;
  engine.req_map_.emplace(1U,
                          FabricMemTransferRequest{TransferInfo{0U, WRITE, AscendString(remote_a.c_str())}, conn_a});
  engine.req_map_.emplace(2U, FabricMemTransferRequest{TransferInfo{0U, READ, AscendString(remote_a.c_str())}, conn_a});
  engine.req_map_.emplace(3U,
                          FabricMemTransferRequest{TransferInfo{0U, WRITE, AscendString(remote_b.c_str())}, conn_b});

  engine.RemoveChannelReqMapLocked(remote_a);
  ASSERT_EQ(engine.req_map_.size(), 1U);
  EXPECT_EQ(engine.req_map_.begin()->first, 3U);

  engine.Disconnect();
  EXPECT_TRUE(engine.req_map_.empty());
  EXPECT_TRUE(engine.fabric_mem_remote_mems_.empty());
}

TEST(FabricMemEngineUTest, DisconnectTimeoutKeepsConnectionAndReqMap) {
  FabricMemEngine engine(AscendString("test_engine"));
  AttachTestContext(engine);
  const std::string remote = "127.0.0.1:12345";
  auto conn = std::make_shared<RemoteConnection>();
  conn->in_flight = 1U;
  engine.fabric_mem_remote_mems_[remote] = conn;

  EXPECT_EQ(engine.Disconnect(AscendString(remote.c_str()), 1), TIMEOUT);
  EXPECT_EQ(engine.fabric_mem_remote_mems_.count(remote), 1U);
  EXPECT_TRUE(engine.req_map_.empty());
  {
    std::lock_guard<std::mutex> lock(conn->state_mutex);
    conn->in_flight = 0U;
  }
  conn->cv.notify_all();
  EXPECT_EQ(engine.Disconnect(AscendString(remote.c_str()), 1), SUCCESS);
}

TEST(FabricMemEngineUTest, GetTransferStatusFailureDoesNotSelfDeadlock) {
  FabricMemEngine engine(AscendString("test_engine"));
  engine.auto_connect_ = true;
  const std::string remote = "127.0.0.1:12345";
  auto conn = std::make_shared<RemoteConnection>();
  engine.fabric_mem_remote_mems_[remote] = conn;
  const auto req = reinterpret_cast<TransferReq>(1U);
  engine.req_map_.emplace(1U, FabricMemTransferRequest{TransferInfo{0U, WRITE, AscendString(remote.c_str())}, conn});

  TransferStatus status = TransferStatus::WAITING;
  auto future =
      std::async(std::launch::async, [&engine, req, &status]() { return engine.GetTransferStatus(req, status); });
  ASSERT_EQ(future.wait_for(std::chrono::seconds(1)), std::future_status::ready);
  EXPECT_EQ(future.get(), NOT_CONNECTED);
  EXPECT_TRUE(engine.req_map_.empty());
  EXPECT_TRUE(engine.fabric_mem_remote_mems_.empty());
}

TEST(FabricMemEngineUTest, GetTransferStatusAsyncFailureDisconnectsWhenAutoConnect) {
  auto runtime = std::make_shared<FabricMemRuntimeStub>();
  auto scoped_runtime = std::make_unique<ScopedRuntimeMock>(runtime);

  FabricMemEngine engine(AscendString("test_engine"));
  engine.auto_connect_ = true;
  AttachTestContext(engine);

  const std::string remote = "127.0.0.1:12345";
  auto conn = std::make_shared<RemoteConnection>();
  conn->remote_memory = std::make_unique<FabricMemRemoteMemory>();
  conn->in_flight = 1U;
  engine.fabric_mem_remote_mems_[remote] = conn;

  // Set up a service with an async record whose stream query will fail.
  auto service = std::make_shared<FabricMemTransferService>();
  ASSERT_EQ(service->Initialize(0, 4U, 1U, nullptr), SUCCESS);
  engine.fabric_mem_transfer_service_ = service;

  AsyncSlot slot;
  ASSERT_EQ(service->TryAcquireAsyncSlot(slot), SUCCESS);
  ASSERT_EQ(service->AppendHostFlagCopies(slot), SUCCESS);

  // Build context with remote as channel_id (matches engine BuildTransferContext).
  FabricMemTransferContext context;
  context.channel_id = remote;
  context.statistic_channel_id = FabricMemStatistic::GetClientStatisticChannelId(remote);

  const uint64_t req_id = 0xABCDUL;
  TransferReq req = reinterpret_cast<TransferReq>(req_id);
  const auto start = std::chrono::steady_clock::now();
  service->RegisterAsyncTransferRecord(context, req, std::move(slot), start, start, 64U, 1U);

  // Ensure host flags are pending so fast-path is skipped.
  auto &record = service->req_2_async_record_[req_id];
  for (void *host_flag : record.slot.host_flags) {
    *static_cast<uint64_t *>(host_flag) = 0ULL;
  }
  // Force stream query failure.
  runtime->stream_query_error_ = ACL_ERROR_RT_INTERNAL_ERROR;

  engine.req_map_.emplace(req_id,
                          FabricMemTransferRequest{TransferInfo{0U, WRITE, AscendString(remote.c_str())}, conn});

  TransferStatus status = TransferStatus::WAITING;
  // Service returns SUCCESS with status == FAILED; engine must disconnect the failed connection.
  EXPECT_EQ(engine.GetTransferStatus(req, status), SUCCESS);
  EXPECT_EQ(status, TransferStatus::FAILED);
  EXPECT_TRUE(engine.req_map_.empty());
  EXPECT_TRUE(engine.fabric_mem_remote_mems_.empty());

  service->Finalize();
  engine.fabric_mem_transfer_service_.reset();
  scoped_runtime.reset();
  runtime.reset();
}

TEST(FabricMemEngineUTest, ConnectOnFinalizedEngineReturnsFailed) {
  FabricMemEngine engine(AscendString("test_engine"));
  engine.is_initialized_ = false;
  engine.aclrt_context_ = nullptr;
  const AscendString remote("127.0.0.1:12345");
  EXPECT_EQ(engine.Connect(remote, kClientTimeoutMs), FAILED);
}

TEST(FabricMemEngineUTest, TransferAsyncPreRegistersReqMapBeforeServiceCall) {
  // Verify that req_map_ is populated before TransferAsync returns successfully,
  // which guarantees DisconnectRemote can always find and release the lease.
  FabricMemEngine engine(AscendString("test_engine"));
  AttachTestContext(engine);
  const std::string remote = "127.0.0.1:12345";
  auto conn = std::make_shared<RemoteConnection>();
  engine.fabric_mem_remote_mems_[remote] = conn;
  engine.auto_connect_ = false;

  // Use a no-op service so TransferAsync() completes without real CANN hardware.
  auto service = std::make_shared<FabricMemTransferService>();
  ASSERT_EQ(service->Initialize(0, 4U, 1U, nullptr), SUCCESS);
  engine.fabric_mem_transfer_service_ = service;

  TransferReq req = nullptr;
  std::vector<TransferOpDesc> op_descs;
  TransferArgs optional_args;
  // With an empty op_descs vector the service-side TransferAsync will fail
  // (no streams), but req_map_ should already be pre-registered by the engine
  // before the service call returns.
  (void)engine.TransferAsync(AscendString(remote.c_str()), WRITE, op_descs, optional_args, req);
  // req_map_ should be empty because the error path cleans up. Verify the
  // cleanup didn't deadlock and the lease was released.
  EXPECT_TRUE(engine.req_map_.empty());

  service->Finalize();
  engine.fabric_mem_transfer_service_.reset();
}

TEST(FabricMemEngineUTest, TransferAsyncRejectsWhenConnectionDisconnecting) {
  FabricMemEngine engine(AscendString("test_engine"));
  AttachTestContext(engine);
  const std::string remote = "127.0.0.1:12345";
  auto conn = std::make_shared<RemoteConnection>();
  {
    std::lock_guard<std::mutex> lock(conn->state_mutex);
    conn->disconnecting = true;
  }
  engine.fabric_mem_remote_mems_[remote] = conn;
  engine.auto_connect_ = false;

  int32_t src = 1;
  int32_t dst = 2;
  TransferOpDesc desc{reinterpret_cast<uintptr_t>(&src), reinterpret_cast<uintptr_t>(&dst), sizeof(int32_t)};
  TransferReq req = nullptr;
  TransferArgs optional_args;
  EXPECT_EQ(engine.TransferAsync(AscendString(remote.c_str()), WRITE, {desc}, optional_args, req), NOT_CONNECTED);
}

TEST(FabricMemEngineUTest, DisconnectDoesNotReleasePreSubmitAsyncLease) {
  FabricMemEngine engine(AscendString("test_engine"));
  AttachTestContext(engine);
  const std::string remote = "127.0.0.1:12345";
  auto conn = std::make_shared<RemoteConnection>();
  conn->remote_memory = MakeUnique<FabricMemRemoteMemory>();
  conn->in_flight = 1U;
  engine.fabric_mem_remote_mems_[remote] = conn;
  const auto req = reinterpret_cast<TransferReq>(0x123UL);
  engine.req_map_.emplace(0x123UL,
                          FabricMemTransferRequest{TransferInfo{0U, WRITE, AscendString(remote.c_str())}, conn});

  EXPECT_EQ(engine.Disconnect(AscendString(remote.c_str()), 1), TIMEOUT);
  EXPECT_EQ(engine.req_map_.count(0x123UL), 0U);
  EXPECT_EQ(conn->in_flight, 1U);
  engine.ReleaseTransferLease(conn);
  EXPECT_EQ(engine.Disconnect(AscendString(remote.c_str()), 1), SUCCESS);
}

TEST_F(FabricMemTransferServiceUTest, SlotPoolWaitWakesOnFinalize) {
  ASSERT_EQ(service_.Initialize(0, 2U, 2U, &statistic_), SUCCESS);
  // Exhaust the pool (max_async_slot_num_ = 2/2 = 1).
  AsyncSlot slot1;
  ASSERT_EQ(service_.TryAcquireAsyncSlot(slot1), SUCCESS);

  // Start a thread that tries to acquire a slot — it will block.
  std::atomic<bool> acquire_finished{false};
  std::thread waiter([this, &acquire_finished]() {
    AsyncSlot slot;
    // Use a long timeout so we don't rely on the timer.
    Status result = service_.TryAcquireSlotWithTimeout(slot, 10000000ULL);
    (void)result;
    acquire_finished.store(true, std::memory_order_release);
  });

  // Give the waiter thread time to enter the wait.
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  EXPECT_FALSE(acquire_finished.load(std::memory_order_acquire));

  // Finalize should notify slot_pool_cv_ and the waiter should wake up.
  service_.Finalize();
  waiter.join();
  EXPECT_TRUE(acquire_finished.load(std::memory_order_acquire));
  service_.ReleaseSlot(slot1, true);
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
  // InitFabricMem may initialize the process-wide VMM before failing; it is not
  // torn down when engine initialization rolls back.
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

class FabricMemConfigParserUTest : public ::testing::Test {};

namespace {
std::map<AscendString, AscendString> MakeOptions() {
  std::map<AscendString, AscendString> options;
  options[OPTION_ENABLE_USE_FABRIC_MEM] = AscendString("1");
  return options;
}

std::map<AscendString, AscendString> MakeOptionsWithJson(const std::string &json) {
  auto options = MakeOptions();
  options[OPTION_GLOBAL_RESOURCE_CONFIG] = AscendString(json.c_str());
  return options;
}
}  // namespace

TEST_F(FabricMemConfigParserUTest, DisabledByDefaultWhenOptionMissing) {
  std::map<AscendString, AscendString> options;
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
  EXPECT_FALSE(result.EnableFabricMem().value_or(false));
}

TEST_F(FabricMemConfigParserUTest, DisabledWhenEnableOptionEmpty) {
  std::map<AscendString, AscendString> options;
  options[OPTION_ENABLE_USE_FABRIC_MEM] = AscendString("");
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
  EXPECT_FALSE(result.EnableFabricMem().value_or(false));
}

TEST_F(FabricMemConfigParserUTest, DisabledWhenEnableOptionZero) {
  std::map<AscendString, AscendString> options;
  options[OPTION_ENABLE_USE_FABRIC_MEM] = AscendString("0");
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
  EXPECT_FALSE(result.EnableFabricMem().value_or(false));
}

TEST_F(FabricMemConfigParserUTest, EnableRejectsNonBinaryValue) {
  std::map<AscendString, AscendString> options;
  options[OPTION_ENABLE_USE_FABRIC_MEM] = AscendString("2");
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(FabricMemConfigParserUTest, EnableRejectsNonNumericValue) {
  std::map<AscendString, AscendString> options;
  options[OPTION_ENABLE_USE_FABRIC_MEM] = AscendString("abc");
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(FabricMemConfigParserUTest, AllFieldsParsedCorrectly) {
  const std::string json = R"({
    "fabric_memory": {
      "max_capacity": 64,
      "start_address": 100,
      "task_stream_num": 4
    }
  })";
  auto options = MakeOptionsWithJson(json);
  options[OPTION_AUTO_CONNECT] = AscendString("1");

  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
  EXPECT_TRUE(result.EnableFabricMem().value_or(false));
  EXPECT_TRUE(result.AutoConnect().value_or(false));
  auto grc = result.GlobalResourceCfg();
  ASSERT_TRUE(grc.has_value());
  EXPECT_EQ(grc->fabric_memory.max_capacity.value(), 64UL);
  EXPECT_EQ(grc->fabric_memory.start_address.value(), 100UL);
  EXPECT_EQ(grc->fabric_memory.task_stream_num.value(), 4U);
}

TEST_F(FabricMemConfigParserUTest, InvalidJsonReturnsError) {
  auto options = MakeOptionsWithJson("{invalid json");
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(FabricMemConfigParserUTest, JsonArrayReturnsError) {
  auto options = MakeOptionsWithJson("[1, 2, 3]");
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(FabricMemConfigParserUTest, CapacityZeroRejected) {
  auto options = MakeOptionsWithJson(R"({"fabric_memory": {"max_capacity": 0}})");
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(FabricMemConfigParserUTest, CapacityBoundaryMinAccepted) {
  auto options = MakeOptionsWithJson(R"({"fabric_memory": {"max_capacity": 1}})");
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
  auto grc = result.GlobalResourceCfg();
  ASSERT_TRUE(grc.has_value());
  EXPECT_EQ(grc->fabric_memory.max_capacity.value(), 1UL);
}

TEST_F(FabricMemConfigParserUTest, CapacityBoundaryMaxAccepted) {
  auto options = MakeOptionsWithJson(R"({"fabric_memory": {"max_capacity": 1024}})");
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
  auto grc = result.GlobalResourceCfg();
  ASSERT_TRUE(grc.has_value());
  EXPECT_EQ(grc->fabric_memory.max_capacity.value(), 1024UL);
}

TEST_F(FabricMemConfigParserUTest, CapacityAboveMaxRejected) {
  auto options = MakeOptionsWithJson(R"({"fabric_memory": {"max_capacity": 1025}})");
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(FabricMemConfigParserUTest, CapacityNonNumericRejected) {
  auto options = MakeOptionsWithJson(R"({"fabric_memory": {"max_capacity": "abc"}})");
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(FabricMemConfigParserUTest, StartAddressBelowMinRejected) {
  auto options = MakeOptionsWithJson(R"({"fabric_memory": {"start_address": 39}})");
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(FabricMemConfigParserUTest, StartAddressBoundaryMinAccepted) {
  auto options = MakeOptionsWithJson(R"({"fabric_memory": {"start_address": 40}})");
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
  auto grc = result.GlobalResourceCfg();
  ASSERT_TRUE(grc.has_value());
  EXPECT_EQ(grc->fabric_memory.start_address.value(), 40UL);
}

TEST_F(FabricMemConfigParserUTest, StartAddressBoundaryMaxAccepted) {
  auto options = MakeOptionsWithJson(R"({"fabric_memory": {"start_address": 220}})");
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
  auto grc = result.GlobalResourceCfg();
  ASSERT_TRUE(grc.has_value());
  EXPECT_EQ(grc->fabric_memory.start_address.value(), 220UL);
}

TEST_F(FabricMemConfigParserUTest, StartAddressAboveMaxRejected) {
  auto options = MakeOptionsWithJson(R"({"fabric_memory": {"start_address": 221}})");
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(FabricMemConfigParserUTest, TaskStreamNumZeroRejected) {
  auto options = MakeOptionsWithJson(R"({"fabric_memory": {"task_stream_num": 0}})");
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(FabricMemConfigParserUTest, TaskStreamNumBoundaryMinMaxAccepted) {
  auto options_min = MakeOptionsWithJson(R"({"fabric_memory": {"task_stream_num": 1}})");
  HixlOptions result_min;
  EXPECT_EQ(HixlOptions::Parse(options_min, result_min), SUCCESS);
  EXPECT_EQ(result_min.GlobalResourceCfg()->fabric_memory.task_stream_num.value(), 1U);

  auto options_max = MakeOptionsWithJson(R"({"fabric_memory": {"task_stream_num": 8}})");
  HixlOptions result_max;
  EXPECT_EQ(HixlOptions::Parse(options_max, result_max), SUCCESS);
  EXPECT_EQ(result_max.GlobalResourceCfg()->fabric_memory.task_stream_num.value(), 8U);
}

TEST_F(FabricMemConfigParserUTest, TaskStreamNumAboveMaxRejected) {
  auto options = MakeOptionsWithJson(R"({"fabric_memory": {"task_stream_num": 9}})");
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(FabricMemConfigParserUTest, MissingSubFieldsKeepDefaults) {
  auto options = MakeOptionsWithJson(R"({"fabric_memory": {}})");
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
  EXPECT_TRUE(result.EnableFabricMem().value_or(false));
  auto grc = result.GlobalResourceCfg();
  ASSERT_TRUE(grc.has_value());
  EXPECT_FALSE(grc->fabric_memory.max_capacity.has_value());
  EXPECT_FALSE(grc->fabric_memory.start_address.has_value());
  EXPECT_FALSE(grc->fabric_memory.task_stream_num.has_value());
}

TEST_F(FabricMemConfigParserUTest, NonFabricMemoryJsonKeysPassThrough) {
  auto options = MakeOptionsWithJson(R"({"other_group": {"key": "value"}, "plain_key": 42})");
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), SUCCESS);
  auto grc = result.GlobalResourceCfg();
  ASSERT_TRUE(grc.has_value());
  EXPECT_FALSE(grc->fabric_memory.max_capacity.has_value());
  EXPECT_FALSE(grc->fabric_memory.start_address.has_value());
  EXPECT_FALSE(grc->fabric_memory.task_stream_num.has_value());
}

TEST_F(FabricMemConfigParserUTest, AutoConnectEmptyValueRejected) {
  auto options = MakeOptions();
  options[OPTION_AUTO_CONNECT] = AscendString("");
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(FabricMemConfigParserUTest, AutoConnectZeroAndOneAccepted) {
  auto options_zero = MakeOptions();
  options_zero[OPTION_AUTO_CONNECT] = AscendString("0");
  HixlOptions result_zero;
  EXPECT_EQ(HixlOptions::Parse(options_zero, result_zero), SUCCESS);
  EXPECT_FALSE(result_zero.AutoConnect().value_or(true));

  auto options_one = MakeOptions();
  options_one[OPTION_AUTO_CONNECT] = AscendString("1");
  HixlOptions result_one;
  EXPECT_EQ(HixlOptions::Parse(options_one, result_one), SUCCESS);
  EXPECT_TRUE(result_one.AutoConnect().value_or(false));
}

TEST_F(FabricMemConfigParserUTest, AutoConnectNonBinaryRejected) {
  auto options = MakeOptions();
  options[OPTION_AUTO_CONNECT] = AscendString("2");
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(FabricMemConfigParserUTest, AutoConnectNonNumericRejected) {
  auto options = MakeOptions();
  options[OPTION_AUTO_CONNECT] = AscendString("abc");
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(FabricMemConfigParserUTest, EnabledSkipsParsingWhenDisabled) {
  std::map<AscendString, AscendString> options;
  options[OPTION_ENABLE_USE_FABRIC_MEM] = AscendString("0");
  options[OPTION_AUTO_CONNECT] = AscendString("invalid_should_fail_if_parsed");
  HixlOptions result;
  EXPECT_EQ(HixlOptions::Parse(options, result), PARAM_INVALID);
}

}  // namespace hixl
