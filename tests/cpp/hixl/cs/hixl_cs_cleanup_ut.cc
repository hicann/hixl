/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>
#include <netinet/in.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <gtest/gtest.h>

#include "depends/ctrl_msg_test_helper.h"
#include "common/scope_guard.h"
#define private public
#include "cs/hixl_cs_server.h"
#undef private

namespace hixl {
namespace {
constexpr auto kCleanupWait = std::chrono::seconds(2);

struct CleanupGate {
  std::promise<void> started;
  std::promise<void> release;
  std::shared_future<void> released = release.get_future().share();
  std::atomic<uint32_t> calls{0U};
  std::atomic<bool> fail_destroy_once{false};
  bool opened = false;

  void Open() {
    if (!opened) {
      opened = true;
      release.set_value();
    }
  }
};

class BlockingCleanupChannel : public Channel {
 public:
  explicit BlockingCleanupChannel(std::shared_ptr<CleanupGate> gate) : gate_(std::move(gate)) {
    channel_handle_ = 1UL;
  }

  Status Initialize(const ChannelContext &) override {
    return SUCCESS;
  }
  Status Connect(const ChannelDesc &, uint32_t) override {
    return SUCCESS;
  }
  Status Disconnect() override {
    return SUCCESS;
  }
  Status Destroy() override {
    if (gate_->calls.fetch_add(1U) == 0U) {
      gate_->started.set_value();
    }
    gate_->released.wait();
    return gate_->fail_destroy_once.exchange(false) ? FAILED : SUCCESS;
  }
  Status QueryCompleteStatus(void *, HixlCompleteStatus *) override {
    return SUCCESS;
  }
  Status RegMem(const char *, const CommMem *, MemHandle *) override {
    return SUCCESS;
  }
  Status UnregMem(MemHandle) override {
    return SUCCESS;
  }
  Status SetRemoteMemInfo(const CommMem &) override {
    return SUCCESS;
  }

 protected:
  Status DoBatchTransferSync(bool, const HixlOneSideOpDesc *, uint32_t, uint32_t) override {
    return SUCCESS;
  }
  Status DoBatchTransferAsync(bool, const HixlOneSideOpDesc *, uint32_t, void **) override {
    return SUCCESS;
  }

 private:
  std::shared_ptr<CleanupGate> gate_;
};
}  // namespace

class HixlCSCleanupUt : public test::PlainTlsTest {
 protected:
  void SetUp() override {
    ASSERT_NO_FATAL_FAILURE(test::PlainTlsTest::SetUp());
    EndpointDesc desc{};
    desc.loc.locType = ENDPOINT_LOC_TYPE_HOST;
    desc.protocol = COMM_PROTOCOL_UBC_CTP;
    desc.commAddr.type = COMM_ADDR_TYPE_ID;
    desc.commAddr.id = 1U;
    server_ = std::make_unique<HixlCSServer>("127.0.0.1", 0U, GlobalConfig{});
    ASSERT_EQ(server_->Initialize(&desc, 1U), SUCCESS);
    endpoint_handle_ = server_->endpoint_store_.GetAllEndpointHandles().front();
    endpoint_ = server_->endpoint_store_.GetEndpoint(endpoint_handle_);
  }
  void TearDown() override {
    gate_->Open();
    if (server_ != nullptr) {
      EXPECT_EQ(server_->Finalize(), SUCCESS);
    }
    for (const int fd : peers_) {
      (void)close(fd);
    }
  }
  void AddClient(CtrlMsgPluginPtr &connection) {
    int fds[2] = {-1, -1};
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
    peers_.push_back(fds[1]);
    connection = CtrlMsgPluginTestAccess::Own(fds[0]);
    ASSERT_NE(connection, nullptr);
    ASSERT_EQ(connection->AddToEpoll(server_->epoll_fd_), SUCCESS);
    server_->clients_[connection->Fd()] = std::make_shared<MsgReceiver>(connection);
  }
  void AddBlockedChannel(CtrlMsgPluginPtr &connection) {
    ASSERT_NO_FATAL_FAILURE(AddClient(connection));
    auto channel = std::make_shared<BlockingCleanupChannel>(gate_);
    endpoint_->channels_[channel->GetHandle()] = channel;
    server_->channels_[connection] = {endpoint_handle_, channel->GetHandle()};
  }
  void StartCleanup(const CtrlMsgPluginPtr &connection) {
    auto started = gate_->started.get_future();
    auto cleanup = std::async(std::launch::async, [&]() { server_->CleanupClient(connection); });
    // Unblock even a regressed synchronous implementation before destroying the future.
    const auto ready = cleanup.wait_for(kCleanupWait);
    if (ready != std::future_status::ready) {
      gate_->Open();
    }
    cleanup.get();
    ASSERT_EQ(ready, std::future_status::ready);
    ASSERT_EQ(started.wait_for(kCleanupWait), std::future_status::ready);
  }
  void AcceptAnotherClient() {
    ASSERT_EQ(server_->listener_connection_->Listen("127.0.0.1", 0U, 16), SUCCESS);
    ASSERT_EQ(server_->listener_connection_->AddToEpoll(server_->epoll_fd_), SUCCESS);
    sockaddr_in addr{};
    socklen_t size = sizeof(addr);
    ASSERT_EQ(getsockname(server_->listener_connection_->Fd(), reinterpret_cast<sockaddr *>(&addr), &size), 0);
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(fd, 0);
    peers_.push_back(fd);
    ASSERT_EQ(connect(fd, reinterpret_cast<sockaddr *>(&addr), size), 0);
    ASSERT_EQ(server_->DoWait(), SUCCESS);
  }
  void EchoClient(int peer) {
    ASSERT_EQ(server_->RegProc(CtrlMsgType::kNotify, [](const CtrlMsgPluginPtr &connection, const char *msg,
                                                        uint64_t len) { return connection->Send(msg, len); }),
              SUCCESS);
    const CtrlMsgHeader header{kMagicNumber, sizeof(CtrlMsgType) + 1U};
    const CtrlMsgType type = CtrlMsgType::kNotify;
    ASSERT_EQ(write(peer, &header, sizeof(header)), static_cast<ssize_t>(sizeof(header)));
    ASSERT_EQ(write(peer, &type, sizeof(type)), static_cast<ssize_t>(sizeof(type)));
    ASSERT_EQ(write(peer, "x", 1U), 1);
    ASSERT_EQ(server_->DoWait(), SUCCESS);
    pollfd event{peer, POLLIN, 0};
    ASSERT_EQ(poll(&event, 1, 2000), 1);
    char byte = 0;
    ASSERT_EQ(read(peer, &byte, 1U), 1);
    EXPECT_EQ(byte, 'x');
  }

  std::unique_ptr<HixlCSServer> server_;
  EndpointHandle endpoint_handle_ = nullptr;
  EndpointPtr endpoint_;
  std::vector<int> peers_;
  std::shared_ptr<CleanupGate> gate_ = std::make_shared<CleanupGate>();
};

TEST_F(HixlCSCleanupUt, BlockedDestroyDoesNotDelayAcceptOrExistingMessages) {
  CtrlMsgPluginPtr disconnected, existing;
  ASSERT_NO_FATAL_FAILURE(AddBlockedChannel(disconnected));
  ASSERT_NO_FATAL_FAILURE(AddClient(existing));
  const int peer = peers_.back();
  ASSERT_NO_FATAL_FAILURE(StartCleanup(disconnected));
  ASSERT_NO_FATAL_FAILURE(AcceptAnotherClient());
  EXPECT_EQ(server_->clients_.size(), 2U);
  ASSERT_NO_FATAL_FAILURE(EchoClient(peer));
  // Repeated cleanup must not remove either live connection or destroy the channel twice.
  server_->CleanupClient(disconnected);
  EXPECT_EQ(server_->clients_.size(), 2U);
  gate_->Open();
  server_->msg_handler_.Finalize();
  EXPECT_EQ(disconnected->Fd(), -1);
  EXPECT_EQ(gate_->calls.load(), 1U);
  EXPECT_TRUE(server_->disconnected_clients_.empty());
  EXPECT_TRUE(server_->channels_.empty());
  EXPECT_TRUE(endpoint_->channels_.empty());
}

TEST_F(HixlCSCleanupUt, FinalizeWaitsForCleanupBeforeReleasingEndpoint) {
  CtrlMsgPluginPtr disconnected;
  ASSERT_NO_FATAL_FAILURE(AddBlockedChannel(disconnected));
  ASSERT_NO_FATAL_FAILURE(StartCleanup(disconnected));
  auto finished = std::async(std::launch::async, [&]() { return server_->Finalize(); });
  const auto ready = finished.wait_for(std::chrono::milliseconds(50));
  EXPECT_EQ(ready, std::future_status::timeout);
  const bool store_available = server_->endpoint_store_.mutex_.try_lock();
  EXPECT_TRUE(store_available);
  if (store_available) {
    EXPECT_EQ(server_->endpoint_store_.endpoints_.count(endpoint_handle_), 1U);
    server_->endpoint_store_.mutex_.unlock();
  }
  gate_->Open();
  ASSERT_EQ(finished.wait_for(kCleanupWait), std::future_status::ready);
  EXPECT_EQ(finished.get(), SUCCESS);
  EXPECT_EQ(gate_->calls.load(), 1U);
  EXPECT_TRUE(endpoint_->channels_.empty());
  EXPECT_EQ(server_->endpoint_store_.GetEndpoint(endpoint_handle_), nullptr);
}

TEST_F(HixlCSCleanupUt, InFlightCreateRollsBackAfterDisconnect) {
  CtrlMsgPluginPtr disconnected;
  ASSERT_NO_FATAL_FAILURE(AddClient(disconnected));
  CreateChannelReq request{};
  request.src = endpoint_->GetEndpoint();
  request.dst_ep_handle = reinterpret_cast<uintptr_t>(endpoint_handle_);
  request.timeout_ms = 1000U;
  std::unique_lock<std::mutex> endpoint_lock(endpoint_->mutex_);
  std::promise<void> started;
  auto creating = started.get_future();
  auto created = std::async(std::launch::async, [&]() {
    started.set_value();
    return server_->CreateChannel(disconnected, reinterpret_cast<const char *>(&request), sizeof(request));
  });
  HIXL_MAKE_GUARD(unlock, ([&]() {
                    if (endpoint_lock.owns_lock()) {
                      endpoint_lock.unlock();
                    }
                  }));
  ASSERT_EQ(creating.wait_for(kCleanupWait), std::future_status::ready);
  EXPECT_EQ(created.wait_for(std::chrono::milliseconds(50)), std::future_status::timeout);
  server_->CleanupClient(disconnected);
  server_->msg_handler_.Finalize();
  endpoint_lock.unlock();
  ASSERT_EQ(created.wait_for(kCleanupWait), std::future_status::ready);
  EXPECT_EQ(created.get(), FAILED);
  EXPECT_TRUE(server_->channels_.empty());
  EXPECT_TRUE(endpoint_->channels_.empty());
}

TEST_F(HixlCSCleanupUt, FinalizeReclaimsCleanupStillInMessageQueue) {
  CtrlMsgPluginPtr disconnected;
  ASSERT_NO_FATAL_FAILURE(AddBlockedChannel(disconnected));
  auto started = gate_->started.get_future();
  auto &handler = server_->msg_handler_;
  std::unique_lock<std::mutex> dispatcher_lock(handler.processors_mutex_);
  auto message = std::make_shared<CtrlMsg>();
  message->msg_type = CtrlMsgType::kNotify;
  handler.SubmitMsg(disconnected, message);
  server_->CleanupClient(disconnected);
  EXPECT_TRUE(server_->clients_.empty());
  EXPECT_EQ(server_->disconnected_clients_.count(disconnected), 1U);
  auto finalized = std::async(std::launch::async, [&]() { return server_->Finalize(); });
  {
    std::unique_lock<std::mutex> lock(handler.req_mutex_);
    EXPECT_TRUE(handler.req_cv_.wait_for(lock, kCleanupWait, [&]() { return !handler.running_.load(); }));
  }
  dispatcher_lock.unlock();
  EXPECT_EQ(started.wait_for(kCleanupWait), std::future_status::ready);
  // Endpoint teardown, rather than a cleanup worker, now owns the remaining channel.
  EXPECT_EQ(disconnected->Fd(), -1);
  gate_->Open();
  ASSERT_EQ(finalized.wait_for(kCleanupWait), std::future_status::ready);
  EXPECT_EQ(finalized.get(), SUCCESS);
  EXPECT_EQ(gate_->calls.load(), 1U);
  EXPECT_TRUE(handler.req_queue_.empty());
  EXPECT_TRUE(server_->disconnected_clients_.empty());
  EXPECT_TRUE(server_->channels_.empty());
  EXPECT_TRUE(endpoint_->channels_.empty());
}

TEST_F(HixlCSCleanupUt, FinalizeReclaimsCleanupStillInWorkerQueue) {
  CtrlMsgPluginPtr disconnected;
  ASSERT_NO_FATAL_FAILURE(AddBlockedChannel(disconnected));
  auto &handler = server_->msg_handler_;
  handler.thread_pool_->Destroy();
  handler.thread_pool_ = std::make_unique<ThreadPool>("cleanup_ut", 1U, 1U);
  auto worker_gate = std::make_shared<CleanupGate>();
  auto started = worker_gate->started.get_future();
  HIXL_MAKE_GUARD(unblock, ([worker_gate]() { worker_gate->Open(); }));
  ASSERT_EQ(server_->RegProc(CtrlMsgType::kNotify,
                             [worker_gate](const CtrlMsgPluginPtr &, const char *, uint64_t) {
                               worker_gate->started.set_value();
                               worker_gate->released.wait();
                               return SUCCESS;
                             }),
            SUCCESS);
  auto message = std::make_shared<CtrlMsg>();
  message->msg_type = CtrlMsgType::kNotify;
  handler.SubmitMsg(disconnected, message);
  ASSERT_EQ(started.wait_for(kCleanupWait), std::future_status::ready);
  server_->CleanupClient(disconnected);
  auto *pool = handler.thread_pool_.get();
  {
    std::unique_lock<std::mutex> lock(pool->m_lock_);
    ASSERT_TRUE(pool->cond_var_.wait_for(lock, kCleanupWait, [pool]() { return !pool->tasks_.empty(); }));
  }
  gate_->Open();
  auto finalized = std::async(std::launch::async, [&]() { return server_->Finalize(); });
  {
    std::unique_lock<std::mutex> lock(pool->m_lock_);
    EXPECT_TRUE(pool->cond_var_.wait_for(lock, kCleanupWait, [pool]() { return pool->is_stopped_.load(); }));
  }
  EXPECT_EQ(gate_->calls.load(), 0U);
  worker_gate->Open();
  ASSERT_EQ(finalized.wait_for(kCleanupWait), std::future_status::ready);
  EXPECT_EQ(finalized.get(), SUCCESS);
  EXPECT_EQ(disconnected->Fd(), -1);
  EXPECT_EQ(gate_->calls.load(), 1U);
  EXPECT_EQ(handler.thread_pool_, nullptr);
  EXPECT_TRUE(server_->disconnected_clients_.empty());
  EXPECT_TRUE(endpoint_->channels_.empty());
}

TEST_F(HixlCSCleanupUt, FinalizeReclaimsCleanupWithStoppedWorkerPool) {
  CtrlMsgPluginPtr disconnected;
  ASSERT_NO_FATAL_FAILURE(AddBlockedChannel(disconnected));
  server_->msg_handler_.thread_pool_->Destroy();
  server_->CleanupClient(disconnected);
  gate_->Open();
  EXPECT_EQ(server_->Finalize(), SUCCESS);
  EXPECT_EQ(disconnected->Fd(), -1);
  EXPECT_EQ(gate_->calls.load(), 1U);
  EXPECT_TRUE(server_->clients_.empty());
  EXPECT_TRUE(server_->disconnected_clients_.empty());
  EXPECT_TRUE(server_->channels_.empty());
  EXPECT_TRUE(endpoint_->channels_.empty());
}

TEST_F(HixlCSCleanupUt, FailedChannelDestroyRemainsTrackedUntilServerFinalize) {
  CtrlMsgPluginPtr disconnected;
  ASSERT_NO_FATAL_FAILURE(AddBlockedChannel(disconnected));
  server_->msg_handler_.thread_pool_->Destroy();
  gate_->fail_destroy_once = true;
  gate_->Open();
  server_->CleanupClient(disconnected);
  const auto processor = server_->msg_handler_.processors_.at(CtrlMsgType::kDestroyChannelReq);
  EXPECT_NE(processor(disconnected, nullptr, 0U), SUCCESS);
  EXPECT_EQ(disconnected->Fd(), -1);
  EXPECT_EQ(server_->disconnected_clients_.count(disconnected), 1U);
  EXPECT_EQ(endpoint_->channels_.size(), 1U);
  EXPECT_EQ(gate_->calls.load(), 1U);
  EXPECT_EQ(server_->Finalize(), SUCCESS);
  EXPECT_EQ(gate_->calls.load(), 2U);
  EXPECT_TRUE(server_->disconnected_clients_.empty());
  EXPECT_TRUE(endpoint_->channels_.empty());
}

TEST_F(HixlCSCleanupUt, FailedConnectionCloseRemainsTrackedUntilServerFinalize) {
  CtrlMsgPluginPtr disconnected;
  ASSERT_NO_FATAL_FAILURE(AddClient(disconnected));
  server_->msg_handler_.thread_pool_->Destroy();
  server_->CleanupClient(disconnected);
  ASSERT_EQ(close(disconnected->Fd()), 0);
  const auto processor = server_->msg_handler_.processors_.at(CtrlMsgType::kDestroyChannelReq);
  EXPECT_NE(processor(disconnected, nullptr, 0U), SUCCESS);
  EXPECT_EQ(server_->disconnected_clients_.count(disconnected), 1U);
  EXPECT_EQ(server_->Finalize(), SUCCESS);
  EXPECT_TRUE(server_->disconnected_clients_.empty());
}

TEST_F(HixlCSCleanupUt, FinalizeClosesDisconnectedClientWithoutChannel) {
  CtrlMsgPluginPtr disconnected;
  ASSERT_NO_FATAL_FAILURE(AddClient(disconnected));
  std::unique_lock<std::mutex> dispatcher_lock(server_->msg_handler_.processors_mutex_);
  auto message = std::make_shared<CtrlMsg>();
  message->msg_type = CtrlMsgType::kNotify;
  server_->msg_handler_.SubmitMsg(disconnected, message);
  server_->CleanupClient(disconnected);
  auto finalized = std::async(std::launch::async, [&]() { return server_->Finalize(); });
  {
    auto &handler = server_->msg_handler_;
    std::unique_lock<std::mutex> lock(handler.req_mutex_);
    EXPECT_TRUE(handler.req_cv_.wait_for(lock, kCleanupWait, [&]() { return !handler.running_.load(); }));
  }
  dispatcher_lock.unlock();
  ASSERT_EQ(finalized.wait_for(kCleanupWait), std::future_status::ready);
  EXPECT_EQ(finalized.get(), SUCCESS);
  EXPECT_EQ(disconnected->Fd(), -1);
  EXPECT_TRUE(server_->disconnected_clients_.empty());
}

TEST_F(HixlCSCleanupUt, FinalizeInterruptsActiveReceiveBeforeJoiningWorkers) {
  CtrlMsgPluginPtr connection;
  ASSERT_NO_FATAL_FAILURE(AddClient(connection));
  std::promise<void> started;
  auto receiving = started.get_future();
  ASSERT_EQ(server_->RegProc(CtrlMsgType::kNotify,
                             [&](const CtrlMsgPluginPtr &client, const char *, uint64_t) {
                               started.set_value();
                               char byte = 0;
                               return client->Recv(&byte, sizeof(byte), 5000U);
                             }),
            SUCCESS);
  auto message = std::make_shared<CtrlMsg>();
  message->msg_type = CtrlMsgType::kNotify;
  server_->msg_handler_.SubmitMsg(connection, message);
  ASSERT_EQ(receiving.wait_for(kCleanupWait), std::future_status::ready);
  auto finalized = std::async(std::launch::async, [&]() { return server_->Finalize(); });
  ASSERT_EQ(finalized.wait_for(kCleanupWait), std::future_status::ready);
  EXPECT_EQ(finalized.get(), SUCCESS);
  EXPECT_EQ(connection->Fd(), -1);
  EXPECT_TRUE(server_->clients_.empty());
  EXPECT_TRUE(server_->disconnected_clients_.empty());
}
}  // namespace hixl
