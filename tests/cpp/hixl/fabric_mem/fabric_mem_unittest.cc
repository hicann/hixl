/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <securec.h>

#define private public
#include "engine/fabric_mem_engine.h"
#include "fabric_mem/fabric_mem_control.h"
#include "fabric_mem/fabric_mem_statistic.h"
#include "fabric_mem/fabric_mem_transfer_service.h"
#undef private

#include "common/statistic_utils.h"
#include "depends/ascendcl/src/ascendcl_stub.h"
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
  aclError aclrtPointerGetAttributes(const void *ptr, aclrtPtrAttributes *attributes) override {
    (void)ptr;
    if (pointer_attr_error_ != ACL_ERROR_NONE) {
      return pointer_attr_error_;
    }
    attributes->location.type = pointer_is_host_ ? ACL_MEM_LOCATION_TYPE_HOST : ACL_MEM_LOCATION_TYPE_DEVICE;
    return ACL_ERROR_NONE;
  }

  aclError aclrtQueryEventWaitStatus(aclrtEvent event, aclrtEventWaitStatus *status) override {
    (void)event;
    if (query_event_error_ != ACL_ERROR_NONE) {
      return query_event_error_;
    }
    *status = event_wait_status_;
    return ACL_ERROR_NONE;
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
  aclError pointer_attr_error_{ACL_ERROR_NONE};
  aclError query_event_error_{ACL_ERROR_NONE};
  aclrtEventWaitStatus event_wait_status_{ACL_EVENT_WAIT_STATUS_COMPLETE};
  size_t malloc_physical_count_{0U};
  aclrtPhysicalMemProp last_physical_mem_prop_{};
};

int32_t GetUnusedLocalPort() {
  const int32_t fd = socket(AF_INET, SOCK_STREAM, 0);
  EXPECT_GE(fd, 0);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  EXPECT_EQ(bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)), 0);
  socklen_t addr_len = sizeof(addr);
  EXPECT_EQ(getsockname(fd, reinterpret_cast<sockaddr *>(&addr), &addr_len), 0);
  const int32_t port = ntohs(addr.sin_port);
  (void)close(fd);
  return port;
}

ShareHandleInfo BuildShareHandle(uintptr_t va_addr = kRemoteOldAddr, size_t len = kLen) {
  ShareHandleInfo info{};
  info.va_addr = va_addr;
  info.len = len;
  for (size_t i = 0; i < sizeof(info.share_handle.data); ++i) {
    info.share_handle.data[i] = static_cast<uint8_t>(i + 1U);
  }
  return info;
}

FabricMemTransferContext BuildContext(FabricMemTransferStatisticInfo *stat_info = nullptr) {
  FabricMemTransferContext context;
  context.channel_id = kChannelId;
  context.statistic_channel_id = kStatChannelId;
  context.remote_va_to_old_va.emplace(kRemoteNewAddr, VaInfo{kRemoteOldAddr, kLen * 4U});
  context.stat_info = stat_info;
  return context;
}

std::vector<TransferOpDesc> BuildOpDescs(uint8_t *local, uint8_t *remote) {
  return {{reinterpret_cast<uintptr_t>(local), reinterpret_cast<uintptr_t>(remote), kLen}};
}

void SendRawFabricMemMsg(int32_t fd, int32_t msg_type, const std::string &payload) {
  const uint64_t length = static_cast<uint64_t>(sizeof(msg_type)) + payload.size();
  ASSERT_EQ(send(fd, &kFabricMemMagic, sizeof(kFabricMemMagic), 0),
            static_cast<ssize_t>(sizeof(kFabricMemMagic)));
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
  const int32_t port = GetUnusedLocalPort();
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
  EXPECT_EQ(service_.Initialize(0U, 1U, &statistic_), PARAM_INVALID);
  EXPECT_EQ(service_.Initialize(1U, 0U, &statistic_), PARAM_INVALID);

  llm::GetAclStubMock() = "aclrtGetDevice";
  EXPECT_NE(service_.Initialize(1U, 1U, &statistic_), SUCCESS);
  llm::GetAclStubMock().clear();

  EXPECT_EQ(service_.Initialize(3U, 1U, &statistic_), SUCCESS);
  EXPECT_EQ(service_.device_id_, 0);
  EXPECT_EQ(service_.task_stream_num_, 1U);
  EXPECT_EQ(service_.async_task_stream_num_, 2U);
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

TEST_F(FabricMemTransferServiceUTest, RegisterDeregisterAndGetShareHandles) {
  ASSERT_EQ(service_.Initialize(4U, 1U, &statistic_), SUCCESS);
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

TEST_F(FabricMemTransferServiceUTest, StreamPoolCreateReuseRollbackAndDestroy) {
  ASSERT_EQ(service_.Initialize(2U, 1U, &statistic_), SUCCESS);
  std::vector<aclrtStream> streams;
  EXPECT_EQ(service_.TryGetStreamOnce(streams, 1U), SUCCESS);
  ASSERT_EQ(streams.size(), 1U);
  const auto first_stream = streams[0];
  service_.ReleaseStreams(streams);
  EXPECT_TRUE(streams.empty() || service_.stream_pool_[first_stream]);

  EXPECT_EQ(service_.TryGetStreamOnce(streams, 1U), SUCCESS);
  ASSERT_EQ(streams.size(), 1U);
  EXPECT_EQ(streams[0], first_stream);
  EXPECT_FALSE(service_.stream_pool_[first_stream]);
  service_.ReleaseStreams(streams);

  EXPECT_EQ(service_.TryGetStreamOnce(streams, 3U), FAILED);
  EXPECT_TRUE(streams.empty());
  EXPECT_TRUE(service_.stream_pool_[first_stream]);

  service_.DestroyAsyncResources({{first_stream, nullptr}, {nullptr, nullptr}});
  EXPECT_TRUE(service_.stream_pool_.empty());
}

TEST_F(FabricMemTransferServiceUTest, AddressTranslationAndCopyValidation) {
  uint8_t local[kLen] = {};
  uint8_t remote[kLen] = {};
  std::fill(std::begin(local), std::end(local), 7U);
  ASSERT_EQ(service_.Initialize(4U, 1U, &statistic_), SUCCESS);

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
  EXPECT_EQ(service_.TransLocalHostOpAddr(kLocalAddr + 8U, 8U, new_addr), SUCCESS);
  EXPECT_EQ(new_addr, kImportedLocalAddr + 8U);

  std::vector<TransferOpDesc> op_descs = {{kLocalAddr + 2U, kRemoteOldAddr, 8U}};
  EXPECT_EQ(service_.TransLocalHostOpAddrs(op_descs), SUCCESS);
  EXPECT_EQ(op_descs[0].local_addr, kImportedLocalAddr + 2U);
  op_descs = {{kLocalAddr + kLen * 2U, kRemoteOldAddr, 8U}};
  EXPECT_EQ(service_.TransLocalHostOpAddrs(op_descs), PARAM_INVALID);

  std::vector<aclrtStream> streams;
  ASSERT_EQ(service_.TryGetStreamOnce(streams, 2U), SUCCESS);
  EXPECT_EQ(FabricMemTransferService::ProcessCopyWithAsync({}, WRITE, BuildOpDescs(local, remote)), PARAM_INVALID);
  EXPECT_EQ(FabricMemTransferService::ProcessCopyWithAsync(streams, WRITE, BuildOpDescs(local, remote)), SUCCESS);
  EXPECT_EQ(remote[0], 7U);
  std::fill(std::begin(remote), std::end(remote), 9U);
  EXPECT_EQ(FabricMemTransferService::ProcessCopyWithAsync(streams, READ, BuildOpDescs(local, remote)), SUCCESS);
  EXPECT_EQ(local[0], 9U);
  EXPECT_EQ(
      FabricMemTransferService::ProcessCopyWithAsync(streams, static_cast<TransferOp>(99), BuildOpDescs(local, remote)),
      PARAM_INVALID);
  service_.ReleaseStreams(streams);
}

TEST_F(FabricMemTransferServiceUTest, NeedTransLocalAddrHandlesHostDeviceEmptyAndFailure) {
  ASSERT_EQ(service_.Initialize(3U, 1U, &statistic_), SUCCESS);
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
  FabricMemTransferStatisticInfo direct_info;
  auto context = BuildContext(&direct_info);
  ASSERT_EQ(service_.Initialize(3U, 1U, &statistic_), SUCCESS);
  service_.UpdateStats(context, 10U, 4U, 512U, 2U);
  EXPECT_EQ(direct_info.transfer.total_cost.load(std::memory_order_relaxed), 10UL);
  EXPECT_EQ(direct_info.real_copy.total_cost.load(std::memory_order_relaxed), 4UL);

  context.stat_info = nullptr;
  service_.UpdateStats(context, 20U, 8U, 1024U, 4U);
  EXPECT_EQ(statistic_.GetSnapshot(kStatChannelId).transfer.total_cost, 20UL);

  FabricMemTransferService no_stat_service;
  ASSERT_EQ(no_stat_service.Initialize(1U, 1U, nullptr), SUCCESS);
  no_stat_service.UpdateStats(BuildContext(), 1U, 1U, 1U, 1U);
  no_stat_service.Finalize();
}

TEST_F(FabricMemTransferServiceUTest, AsyncTransferRecordCompletesAndUpdatesStats) {
  ASSERT_EQ(service_.Initialize(4U, 1U, &statistic_), SUCCESS);
  std::vector<aclrtStream> streams;
  ASSERT_EQ(service_.TryGetStreamOnce(streams, 2U), SUCCESS);

  std::vector<AsyncResource> resources;
  ASSERT_EQ(service_.RecordCopyStreamEvents(streams[0], {streams[1]}, resources), SUCCESS);
  TransferReq req = reinterpret_cast<TransferReq>(0x1234UL);
  const auto start = std::chrono::steady_clock::now();
  service_.RegisterAsyncTransferRecord(BuildContext(), req, std::move(resources), start, start, 256U, 2U);

  TransferStatus status = TransferStatus::WAITING;
  EXPECT_EQ(service_.GetTransferStatus(BuildContext(), req, status), SUCCESS);
  EXPECT_EQ(status, TransferStatus::COMPLETED);
  EXPECT_EQ(service_.GetTransferStatus(BuildContext(), req, status), FAILED);
  EXPECT_EQ(statistic_.GetSnapshot(kStatChannelId).transfer.times, 1UL);
  EXPECT_TRUE(service_.req_2_async_record_.empty());
  EXPECT_TRUE(service_.channel_2_req_[kChannelId].empty());
}

TEST_F(FabricMemTransferServiceUTest, AsyncTransferRecordHandlesQueryFailureAndRemoveChannel) {
  ASSERT_EQ(service_.Initialize(4U, 1U, &statistic_), SUCCESS);
  std::vector<aclrtStream> streams;
  ASSERT_EQ(service_.TryGetStreamOnce(streams, 2U), SUCCESS);
  std::vector<AsyncResource> resources;
  ASSERT_EQ(service_.RecordCopyStreamEvents(streams[0], {streams[1]}, resources), SUCCESS);
  runtime_->query_event_error_ = ACL_ERROR_RT_INTERNAL_ERROR;

  TransferReq req = reinterpret_cast<TransferReq>(0x5678UL);
  const auto start = std::chrono::steady_clock::now();
  service_.RegisterAsyncTransferRecord(BuildContext(), req, std::move(resources), start, start, 128U, 1U);
  TransferStatus status = TransferStatus::WAITING;
  EXPECT_EQ(service_.GetTransferStatus(BuildContext(), req, status), FAILED);
  EXPECT_EQ(status, TransferStatus::FAILED);
  EXPECT_TRUE(service_.req_2_async_record_.empty());

  runtime_->query_event_error_ = ACL_ERROR_NONE;
  ASSERT_EQ(service_.TryGetStreamOnce(streams, 2U), SUCCESS);
  resources.clear();
  ASSERT_EQ(service_.RecordCopyStreamEvents(streams[0], {streams[1]}, resources), SUCCESS);
  req = reinterpret_cast<TransferReq>(0x9ABCUL);
  service_.RegisterAsyncTransferRecord(BuildContext(), req, std::move(resources), start, start, 128U, 1U);
  service_.RemoveChannel("missing");
  EXPECT_FALSE(service_.req_2_async_record_.empty());
  service_.RemoveChannel(kChannelId);
  EXPECT_TRUE(service_.req_2_async_record_.empty());
  EXPECT_TRUE(service_.channel_2_req_.find(kChannelId) == service_.channel_2_req_.end());
}

TEST(FabricMemEngineUTest, ConnectAndDisconnectRoundTrip) {
  const int32_t port = GetUnusedLocalPort();
  ASSERT_GT(port, 0);
  FabricMemControlServer server;
  ASSERT_EQ(
      server.Start("127.0.0.1:" + std::to_string(port),
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
  // Cleanup any state created during the connect attempt
  engine.DisconnectLocked(remote, kClientTimeoutMs);
  engine.Disconnect();
  engine.CleanupFabricMemLocked();
  server.Stop();
}

TEST(FabricMemEngineUTest, DisconnectNoConnection) {
  FabricMemEngine engine(AscendString("test_engine"));
  AscendString remote("127.0.0.1:12345");
  EXPECT_EQ(engine.Disconnect(remote, kClientTimeoutMs), NOT_CONNECTED);
  EXPECT_EQ(engine.DisconnectLocked(remote, kClientTimeoutMs), NOT_CONNECTED);
  engine.Disconnect();
}

}  // namespace hixl
