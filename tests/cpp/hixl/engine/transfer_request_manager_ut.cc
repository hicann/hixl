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
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <set>
#include <thread>
#include <vector>
#include <gtest/gtest.h>
#define private public
#include "engine/hixl_engine.h"
#undef private
#include "common/scope_guard.h"

namespace {
// One-shot, thread-local allocation failure injection; production has no test hooks.
thread_local int64_t g_request_allocations_before_failure = -1;
}  // namespace
extern "C" void *__real__Znwm(size_t size);
extern "C" void *__wrap__Znwm(size_t size) {
  if (g_request_allocations_before_failure == 0) {
    g_request_allocations_before_failure = -1;
    throw std::bad_alloc();
  }
  if (g_request_allocations_before_failure > 0) {
    --g_request_allocations_before_failure;
  }
  return __real__Znwm(size);
}

namespace hixl {
namespace {
class RequestIdHandler : public IClientHandler {
 public:
  Status Connect(uint32_t) override {
    return SUCCESS;
  }
  Status RegisterMem(const MemHandleInfo &) override {
    return SUCCESS;
  }
  Status DeregisterMem(MemHandle) override {
    return SUCCESS;
  }
  Status TransferSync(const std::vector<TransferOpDesc> &, TransferOp, uint32_t) override {
    return SUCCESS;
  }
  Status TransferAsync(const std::vector<TransferOpDesc> &, TransferOp, TransferReq &req) override {
    ++submit_calls;
    req = backend_req;
    if (submit_ret == SUCCESS) {
      ++pending_tasks;
    }
    return submit_ret;
  }
  Status GetTransferStatus(const TransferReq &req, TransferStatus &status) override {
    ++query_calls;
    EXPECT_EQ(req, backend_req);
    status = query_status;
    return query_ret;
  }
  Status Finalize() override {
    ++finalize_calls;
    pending_tasks = 0;
    return finalize_ret;
  }
  void Dump(const char *, DumpLogLevel) const override {
    ++dump_calls;
  }
  TransferReq backend_req = this;
  Status submit_ret = SUCCESS;
  Status query_ret = SUCCESS;
  Status finalize_ret = SUCCESS;
  TransferStatus query_status = TransferStatus::WAITING;
  uint32_t submit_calls = 0;
  uint32_t query_calls = 0;
  uint32_t finalize_calls = 0;
  uint32_t pending_tasks = 0;
  mutable uint32_t dump_calls = 0;
};

std::shared_ptr<HixlClient> MakeIdClient() {
  ClientConfig config{};
  config.remote_engine = "127.0.0.1:26300";
  auto client = std::make_shared<HixlClient>("127.0.0.1", 26300, config);
  client->client_handler_ = std::make_unique<RequestIdHandler>();
  client->is_connected_ = true;
  return client;
}

TransferReq SubmitId(HixlClient &client) {
  TransferReq req = nullptr;
  EXPECT_EQ(client.TransferAsync({{1, 2, 1}}, READ, {}, req), SUCCESS);
  return req;
}
}  // namespace

class ClientRequestIdTest : public ::testing::Test {
 protected:
  void TearDown() override {
    g_request_allocations_before_failure = -1;
  }
};

TEST_F(ClientRequestIdTest, ManagerCopiesCompleteRequestAndRemovesIt) {
  TransferRequestManager manager;
  TransferReq first = nullptr;
  TransferRequest input{reinterpret_cast<TransferReq>(0x1234), {0U, WRITE, AscendString()}};
  ASSERT_EQ(manager.Add(input, first), SUCCESS);
  ASSERT_NE(first, nullptr);
  const auto *record = manager.Get(first);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(input.backend_req, reinterpret_cast<TransferReq>(0x1234));
  EXPECT_EQ(input.info.op_type, WRITE);
  input.backend_req = nullptr;
  input.info.op_type = READ;
  TransferReq second = nullptr;
  ASSERT_EQ(manager.Add({}, second), SUCCESS);
  EXPECT_NE(first, second);
  EXPECT_EQ(manager.Get(first), record);
  EXPECT_EQ(record->backend_req, reinterpret_cast<TransferReq>(0x1234));
  EXPECT_EQ(record->info.op_type, WRITE);
  manager.Remove(first);
  manager.Remove(first);
  EXPECT_EQ(manager.Get(first), nullptr);
  EXPECT_EQ(manager.Get(nullptr), nullptr);
  EXPECT_FALSE(manager.requests_.empty());
  manager.Clear();
  EXPECT_TRUE(manager.requests_.empty());
}

TEST_F(ClientRequestIdTest, ManagerAllocationFailureDoesNotExposeId) {
  TransferRequestManager manager;
  TransferReq req = reinterpret_cast<TransferReq>(0x99);
  g_request_allocations_before_failure = 0;
  const auto ret = manager.Add({}, req);
  EXPECT_EQ(ret, RESOURCE_EXHAUSTED);
  EXPECT_EQ(req, nullptr);
  EXPECT_TRUE(manager.requests_.empty());
}

TEST_F(ClientRequestIdTest, IdsAreUniqueAcrossConcurrentManagers) {
  std::vector<std::vector<TransferReq>> ids(8, std::vector<TransferReq>(32));
  std::vector<std::thread> workers;
  for (auto &group : ids) {
    auto *output = &group;
    workers.emplace_back([output]() {
      TransferRequestManager manager;
      for (auto &req : *output) {
        EXPECT_EQ(manager.Add({}, req), SUCCESS);
      }
    });
  }
  for (auto &worker : workers) {
    worker.join();
  }
  std::set<TransferReq> unique;
  for (const auto &group : ids) {
    unique.insert(group.begin(), group.end());
  }
  EXPECT_EQ(unique.size(), 256U);
  EXPECT_EQ(unique.count(nullptr), 0U);
}

TEST_F(ClientRequestIdTest, ManagerIdExhaustionDoesNotWrap) {
  TransferRequestManager manager;
  const auto saved = TransferRequestManager::next_id_.exchange(std::numeric_limits<uintptr_t>::max() - 1U);
  HIXL_MAKE_GUARD(restore, ([saved]() { TransferRequestManager::next_id_.store(saved); }));
  TransferReq last = nullptr;
  ASSERT_EQ(manager.Add({}, last), SUCCESS);
  EXPECT_EQ(reinterpret_cast<uintptr_t>(last), std::numeric_limits<uintptr_t>::max() - 1U);
  for (uint32_t attempt = 0; attempt < 2U; ++attempt) {
    TransferReq req = last;
    EXPECT_EQ(manager.Add({}, req), RESOURCE_EXHAUSTED);
    EXPECT_EQ(req, nullptr);
  }
  EXPECT_NE(manager.Get(last), nullptr);
  EXPECT_EQ(TransferRequestManager::next_id_.load(), std::numeric_limits<uintptr_t>::max());
}

TEST_F(ClientRequestIdTest, IdExhaustionDoesNotSubmitBackend) {
  auto client = MakeIdClient();
  auto other = MakeIdClient();
  const auto old_id = SubmitId(*client);
  const auto other_id = SubmitId(*other);
  auto &handler = static_cast<RequestIdHandler &>(*client->client_handler_);
  auto &other_handler = static_cast<RequestIdHandler &>(*other->client_handler_);
  const auto saved = TransferRequestManager::next_id_.exchange(std::numeric_limits<uintptr_t>::max());
  HIXL_MAKE_GUARD(restore, ([saved]() { TransferRequestManager::next_id_.store(saved); }));
  TransferReq req = old_id;
  EXPECT_EQ(client->TransferAsync({{1, 2, 1}}, READ, {}, req), RESOURCE_EXHAUSTED);
  EXPECT_EQ(req, nullptr);
  EXPECT_EQ(handler.submit_calls, 1U);
  EXPECT_EQ(handler.finalize_calls, 0U);
  EXPECT_EQ(handler.pending_tasks, 1U);
  EXPECT_EQ(handler.dump_calls, 1U);
  EXPECT_NE(client->request_manager_.Get(old_id), nullptr);
  EXPECT_TRUE(client->is_connected_);
  EXPECT_FALSE(client->is_finalized_);
  EXPECT_EQ(client->Finalize(), SUCCESS);
  EXPECT_EQ(handler.finalize_calls, 1U);
  EXPECT_EQ(handler.pending_tasks, 0U);
  EXPECT_TRUE(client->request_manager_.requests_.empty());
  EXPECT_EQ(client->Finalize(), SUCCESS);
  EXPECT_EQ(handler.finalize_calls, 1U);
  EXPECT_NE(other->request_manager_.Get(other_id), nullptr);
  EXPECT_TRUE(other->is_connected_);
  EXPECT_EQ(other_handler.finalize_calls, 0U);
  EXPECT_EQ(other->Finalize(), SUCCESS);
}

TEST_F(ClientRequestIdTest, ReusedBackendHandleGetsNewPublicId) {
  auto client = MakeIdClient();
  auto &handler = static_cast<RequestIdHandler &>(*client->client_handler_);
  const auto first = SubmitId(*client);
  EXPECT_NE(first, handler.backend_req);
  EXPECT_FALSE(client->request_manager_.requests_.empty());
  TransferStatus status = TransferStatus::FAILED;
  EXPECT_EQ(client->GetTransferStatus(first, status), SUCCESS);
  EXPECT_EQ(status, TransferStatus::WAITING);
  EXPECT_NE(client->request_manager_.Get(first), nullptr);
  handler.query_status = TransferStatus::COMPLETED;
  EXPECT_EQ(client->GetTransferStatus(first, status), SUCCESS);
  EXPECT_EQ(client->request_manager_.Get(first), nullptr);
  EXPECT_TRUE(client->request_manager_.requests_.empty());
  const auto second = SubmitId(*client);
  EXPECT_NE(second, first);
  EXPECT_EQ(client->GetTransferStatus(first, status), PARAM_INVALID);
  EXPECT_EQ(handler.query_calls, 2U);
  EXPECT_NE(client->request_manager_.Get(second), nullptr);
  EXPECT_EQ(client->GetTransferStatus(second, status), SUCCESS);
  EXPECT_EQ(handler.query_calls, 3U);
  EXPECT_EQ(client->Finalize(), SUCCESS);
}

TEST_F(ClientRequestIdTest, OldEngineCleanupCannotRemoveReusedBackendRequest) {
  HixlEngine engine("127.0.0.1:26200");
  auto client = MakeIdClient();
  auto &handler = static_cast<RequestIdHandler &>(*client->client_handler_);
  auto old_id = SubmitId(*client);
  engine.client_manager_.RegisterTransferReq(old_id, client, nullptr);
  handler.query_status = TransferStatus::COMPLETED;
  TransferStatus status;
  // Deterministically pause the old Engine query after Client returns, before index removal.
  ASSERT_EQ(client->GetTransferStatus(old_id, status), SUCCESS);
  auto new_id = SubmitId(*client);
  engine.client_manager_.RegisterTransferReq(new_id, client, nullptr);
  engine.client_manager_.EraseTransferReq(old_id);
  EXPECT_NE(old_id, new_id);
  EXPECT_EQ(engine.client_manager_.GetClientByReq(new_id), client);
  EXPECT_EQ(engine.GetTransferStatus(new_id, status), SUCCESS);
  EXPECT_EQ(status, TransferStatus::COMPLETED);
  EXPECT_EQ(engine.client_manager_.GetClientByReq(new_id), nullptr);
  EXPECT_EQ(client->Finalize(), SUCCESS);
}

TEST_F(ClientRequestIdTest, IdsDoNotRestartWhenClientsAreRecreated) {
  auto first_client = MakeIdClient();
  const auto first = SubmitId(*first_client);
  EXPECT_EQ(first_client->Finalize(), SUCCESS);
  EXPECT_TRUE(first_client->request_manager_.requests_.empty());
  first_client.reset();
  auto next_client = MakeIdClient();
  const auto second = SubmitId(*next_client);
  auto other_client = MakeIdClient();
  const auto third = SubmitId(*other_client);
  EXPECT_NE(first, second);
  EXPECT_NE(second, third);
  EXPECT_EQ(other_client->Finalize(), SUCCESS);
  EXPECT_EQ(next_client->Finalize(), SUCCESS);
}

TEST_F(ClientRequestIdTest, SubmissionFailureDoesNotRegisterOrFinalize) {
  auto client = MakeIdClient();
  auto &handler = static_cast<RequestIdHandler &>(*client->client_handler_);
  handler.submit_ret = FAILED;
  TransferReq req = reinterpret_cast<TransferReq>(0x99);
  EXPECT_EQ(client->TransferAsync({{1, 2, 1}}, READ, {}, req), FAILED);
  EXPECT_EQ(req, nullptr);
  EXPECT_TRUE(client->request_manager_.requests_.empty());
  EXPECT_EQ(handler.submit_calls, 1U);
  EXPECT_EQ(handler.finalize_calls, 0U);
  EXPECT_EQ(client->Finalize(), SUCCESS);
}

TEST_F(ClientRequestIdTest, RecordAllocationFailureDoesNotSubmitBackend) {
  auto client = MakeIdClient();
  auto &handler = static_cast<RequestIdHandler &>(*client->client_handler_);
  const auto old_id = SubmitId(*client);
  const std::vector<TransferOpDesc> descs{{1, 2, 1}};
  const TransferArgs args{};
  TransferReq req = old_id;
  // Fail the request map reservation before backend submission.
  g_request_allocations_before_failure = 0;
  const auto ret = client->TransferAsync(descs, READ, args, req);
  g_request_allocations_before_failure = -1;
  EXPECT_EQ(ret, RESOURCE_EXHAUSTED);
  EXPECT_EQ(req, nullptr);
  EXPECT_EQ(handler.submit_calls, 1U);
  EXPECT_EQ(handler.finalize_calls, 0U);
  EXPECT_EQ(handler.pending_tasks, 1U);
  EXPECT_NE(client->request_manager_.Get(old_id), nullptr);
  EXPECT_FALSE(client->is_finalized_);
  EXPECT_TRUE(client->is_connected_);
  EXPECT_EQ(client->Finalize(), SUCCESS);
  EXPECT_EQ(handler.pending_tasks, 0U);
  EXPECT_TRUE(client->request_manager_.requests_.empty());
  EXPECT_EQ(handler.finalize_calls, 1U);
}

TEST_F(ClientRequestIdTest, ReservationFailureDoesNotCreatePendingWork) {
  auto client = MakeIdClient();
  auto &handler = static_cast<RequestIdHandler &>(*client->client_handler_);
  const auto saved = TransferRequestManager::next_id_.exchange(std::numeric_limits<uintptr_t>::max());
  HIXL_MAKE_GUARD(restore, ([saved]() { TransferRequestManager::next_id_.store(saved); }));
  TransferReq req = nullptr;
  EXPECT_EQ(client->TransferAsync({{1, 2, 1}}, READ, {}, req), RESOURCE_EXHAUSTED);
  EXPECT_EQ(req, nullptr);
  EXPECT_EQ(handler.submit_calls, 0U);
  EXPECT_EQ(handler.pending_tasks, 0U);
  EXPECT_EQ(handler.finalize_calls, 0U);
  EXPECT_TRUE(client->request_manager_.requests_.empty());
  EXPECT_TRUE(client->is_connected_);
  EXPECT_FALSE(client->is_finalized_);
  EXPECT_EQ(client->Finalize(), SUCCESS);
}

TEST_F(ClientRequestIdTest, QueryFailureAndEveryTerminalStatusRemoveRecord) {
  auto client = MakeIdClient();
  auto &handler = static_cast<RequestIdHandler &>(*client->client_handler_);
  for (const auto terminal : {TransferStatus::COMPLETED, TransferStatus::FAILED, TransferStatus::TIMEOUT}) {
    auto req = SubmitId(*client);
    handler.query_status = terminal;
    TransferStatus status = TransferStatus::WAITING;
    EXPECT_EQ(client->GetTransferStatus(req, status), SUCCESS);
    EXPECT_EQ(status, terminal);
    EXPECT_EQ(client->request_manager_.Get(req), nullptr);
  }
  auto req = SubmitId(*client);
  handler.query_ret = FAILED;
  TransferStatus status;
  EXPECT_EQ(client->GetTransferStatus(req, status), FAILED);
  EXPECT_EQ(client->request_manager_.Get(req), nullptr);
  const auto calls = handler.query_calls;
  EXPECT_EQ(client->GetTransferStatus(req, status), PARAM_INVALID);
  EXPECT_EQ(client->GetTransferStatus(nullptr, status), PARAM_INVALID);
  EXPECT_EQ(handler.query_calls, calls);
  EXPECT_EQ(status, TransferStatus::FAILED);
  EXPECT_EQ(client->Finalize(), SUCCESS);
}

TEST_F(ClientRequestIdTest, ClientLockSerializesQuery) {
  auto client = MakeIdClient();
  auto &handler = static_cast<RequestIdHandler &>(*client->client_handler_);
  auto req = SubmitId(*client);
  std::unique_lock<std::mutex> lock(client->mutex_);
  std::promise<void> started;
  auto started_future = started.get_future();
  auto query = std::async(std::launch::async, [&]() {
    TransferStatus status = TransferStatus::FAILED;
    started.set_value();
    EXPECT_EQ(client->GetTransferStatus(req, status), SUCCESS);
    EXPECT_EQ(status, TransferStatus::WAITING);
  });
  started_future.wait();
  EXPECT_EQ(query.wait_for(std::chrono::milliseconds(20)), std::future_status::timeout);
  lock.unlock();
  query.get();
  EXPECT_EQ(handler.query_calls, 1U);
  EXPECT_NE(client->request_manager_.Get(req), nullptr);
  EXPECT_EQ(client->Finalize(), SUCCESS);
}
}  // namespace hixl
