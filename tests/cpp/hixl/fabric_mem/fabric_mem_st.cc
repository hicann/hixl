/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "depends/ascendcl/src/ascendcl_stub.h"
#include "engine/fabric_mem_engine.h"
#include "engine/hixl_options.h"
#include "fabric_mem_test_utils.h"
#include "hixl/hixl_types.h"

namespace hixl {
namespace {
constexpr size_t kTransferSize = 1024U;
constexpr uint8_t kDataPattern = 0xAAU;
constexpr uint8_t kInitPattern = 0U;
constexpr int32_t kDeviceId0 = 0;
constexpr int32_t kDeviceId1 = 1;
const char *const kEnableFabricMem = "1";
const char *const kEngine1Ip = "127.0.0.1";
constexpr int32_t kTimeoutMs = 1000;
constexpr size_t kAsyncRequestCount = 10U;
constexpr int64_t kAsyncWaitSeconds = 5;
constexpr int64_t kAsyncPollIntervalMs = 10;

class FabricMemRuntimeMock : public llm::AclRuntimeStub {
 public:
  static void Install() {
    llm::AclRuntimeStub::SetInstance(std::make_shared<FabricMemRuntimeMock>());
  }

  static void Reset() {
    llm::AclRuntimeStub::Reset();
  }

  static void SetDevice(int32_t device_id) {
    device_id_ = device_id;
  }

  aclError aclrtGetDevice(int32_t *device_id) override {
    *device_id = device_id_;
    return ACL_ERROR_NONE;
  }

  const char *aclrtGetSocName() override {
    return "Ascend910_9391";
  }

 private:
  static inline int32_t device_id_{0};
};

struct DeviceBuffer {
  explicit DeviceBuffer(uint8_t pattern) : data(kTransferSize, pattern) {}

  std::vector<uint8_t> data;
  MemHandle handle{nullptr};
};

struct TransferBuffers {
  DeviceBuffer src{kDataPattern};
  DeviceBuffer dst{kInitPattern};
  DeviceBuffer remote{kInitPattern};
};

std::string BuildRemoteEngineId(int32_t port) {
  return std::string(kEngine1Ip) + ":" + std::to_string(port);
}

std::map<AscendString, AscendString> BuildEngineOptions(bool auto_connect) {
  std::map<AscendString, AscendString> options;
  options[OPTION_ENABLE_USE_FABRIC_MEM] = kEnableFabricMem;
  if (auto_connect) {
    options[OPTION_AUTO_CONNECT] = kEnableFabricMem;
  }
  return options;
}

Status InitializeEngine(FabricMemEngine &engine, int32_t device_id, bool auto_connect = false) {
  FabricMemRuntimeMock::SetDevice(device_id);
  HixlOptions parsed;
  EXPECT_EQ(HixlOptions::Parse(BuildEngineOptions(auto_connect), parsed), SUCCESS);
  return engine.Initialize(parsed);
}

MemDesc BuildMemDesc(DeviceBuffer &buffer) {
  MemDesc mem{};
  mem.addr = reinterpret_cast<uintptr_t>(buffer.data.data());
  mem.len = buffer.data.size();
  return mem;
}

Status RegisterDeviceBuffer(FabricMemEngine &engine, DeviceBuffer &buffer) {
  return engine.RegisterMem(BuildMemDesc(buffer), MEM_DEVICE, buffer.handle);
}

TransferOpDesc BuildTransferDesc(DeviceBuffer &local, DeviceBuffer &remote) {
  return {reinterpret_cast<uintptr_t>(local.data.data()), reinterpret_cast<uintptr_t>(remote.data.data()),
          local.data.size()};
}

void ExpectPattern(const DeviceBuffer &buffer, uint8_t pattern, const char *message) {
  for (size_t i = 0U; i < buffer.data.size(); ++i) {
    EXPECT_EQ(buffer.data[i], pattern) << message << i;
  }
}

struct FabricMemTestEnv {
  explicit FabricMemTestEnv(std::string remote_id, bool auto_connect = false)
      : remote_engine_id(std::move(remote_id)),
        engine1(AscendString(kEngine1Ip)),
        engine2(AscendString(remote_engine_id.c_str())) {
    EXPECT_EQ(InitializeEngine(engine1, kDeviceId0, auto_connect), SUCCESS);
    EXPECT_EQ(InitializeEngine(engine2, kDeviceId1), SUCCESS);
  }

  AscendString RemoteEngine() const {
    return AscendString(remote_engine_id.c_str());
  }

  void Finalize() {
    engine1.Finalize();
    engine2.Finalize();
  }

  std::string remote_engine_id;
  FabricMemEngine engine1;
  FabricMemEngine engine2;
};

struct TripleFabricMemTestEnv {
  TripleFabricMemTestEnv(int32_t port2, int32_t port3)
      : engine2_ip(BuildRemoteEngineId(port2)),
        engine3_ip(BuildRemoteEngineId(port3)),
        engine1(AscendString(kEngine1Ip)),
        engine2(AscendString(engine2_ip.c_str())),
        engine3(AscendString(engine3_ip.c_str())) {
    EXPECT_EQ(InitializeEngine(engine1, kDeviceId0), SUCCESS);
    EXPECT_EQ(InitializeEngine(engine2, kDeviceId1), SUCCESS);
    EXPECT_EQ(InitializeEngine(engine3, kDeviceId0), SUCCESS);
  }

  void Finalize() {
    engine1.Finalize();
    engine2.Finalize();
    engine3.Finalize();
  }

  std::string engine2_ip;
  std::string engine3_ip;
  FabricMemEngine engine1;
  FabricMemEngine engine2;
  FabricMemEngine engine3;
};

void RegisterStandardBuffers(FabricMemTestEnv &env, TransferBuffers &buffers) {
  EXPECT_EQ(RegisterDeviceBuffer(env.engine1, buffers.src), SUCCESS);
  EXPECT_EQ(RegisterDeviceBuffer(env.engine1, buffers.dst), SUCCESS);
  EXPECT_EQ(RegisterDeviceBuffer(env.engine2, buffers.remote), SUCCESS);
}

void DeregisterStandardBuffers(FabricMemTestEnv &env, TransferBuffers &buffers) {
  EXPECT_EQ(env.engine1.DeregisterMem(buffers.src.handle), SUCCESS);
  EXPECT_EQ(env.engine1.DeregisterMem(buffers.dst.handle), SUCCESS);
  EXPECT_EQ(env.engine2.DeregisterMem(buffers.remote.handle), SUCCESS);
}

void TransferWriteReadSync(FabricMemTestEnv &env, TransferBuffers &buffers) {
  const AscendString remote_engine = env.RemoteEngine();
  const TransferOpDesc write_desc = BuildTransferDesc(buffers.src, buffers.remote);
  EXPECT_EQ(env.engine1.TransferSync(remote_engine, WRITE, {write_desc}, kTimeoutMs), SUCCESS);

  const TransferOpDesc read_desc = BuildTransferDesc(buffers.dst, buffers.remote);
  EXPECT_EQ(env.engine1.TransferSync(remote_engine, READ, {read_desc}, kTimeoutMs), SUCCESS);
}

void WaitForAsyncComplete(FabricMemEngine &engine, TransferReq req) {
  TransferStatus status = TransferStatus::WAITING;
  while (status == TransferStatus::WAITING) {
    EXPECT_EQ(engine.GetTransferStatus(req, status), SUCCESS);
  }
  EXPECT_EQ(status, TransferStatus::COMPLETED);
}

void SubmitAsyncTransfer(FabricMemTestEnv &env, TransferOp op, DeviceBuffer &local, DeviceBuffer &remote,
                         TransferReq &req) {
  const TransferOpDesc desc = BuildTransferDesc(local, remote);
  EXPECT_EQ(env.engine1.TransferAsync(env.RemoteEngine(), op, {desc}, {}, req), SUCCESS);
  EXPECT_NE(req, nullptr);
}

void JoinThreads(std::vector<std::thread> &threads) {
  for (auto &thread : threads) {
    thread.join();
  }
}

void PollRequestUntilDone(FabricMemEngine &engine, TransferReq req, std::atomic<size_t> &completed,
                          const std::atomic<bool> &stop) {
  TransferStatus status = TransferStatus::WAITING;
  while (!stop.load() && status == TransferStatus::WAITING) {
    if (engine.GetTransferStatus(req, status) != SUCCESS) {
      return;
    }
    if (status == TransferStatus::COMPLETED) {
      completed.fetch_add(1U);
      return;
    }
    if (status == TransferStatus::FAILED) {
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(kAsyncPollIntervalMs));
  }
}

void WaitForAllAsyncWrites(FabricMemEngine &engine, const std::array<TransferReq, kAsyncRequestCount> &reqs) {
  std::atomic<size_t> completed{0U};
  std::atomic<bool> stop{false};
  std::vector<std::thread> poll_threads;
  for (auto req : reqs) {
    poll_threads.emplace_back([&engine, req, &completed, &stop]() {
      PollRequestUntilDone(engine, req, completed, stop);
    });
  }
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(kAsyncWaitSeconds);
  while (std::chrono::steady_clock::now() < deadline && completed.load() < kAsyncRequestCount) {
    std::this_thread::sleep_for(std::chrono::milliseconds(kAsyncPollIntervalMs));
  }
  stop = true;
  JoinThreads(poll_threads);
  EXPECT_EQ(completed.load(), kAsyncRequestCount);
}
}  // namespace

class FabricMemSTest : public ::testing::Test {
 protected:
  void SetUp() override {
    FabricMemRuntimeMock::Install();
  }

  void TearDown() override {
    FabricMemRuntimeMock::Reset();
  }
};

TEST_F(FabricMemSTest, TestHixlFabricMem) {
  const int32_t port = test::AllocateFabricMemTestPort();
  ASSERT_GT(port, 0);
  FabricMemTestEnv env(BuildRemoteEngineId(port));
  TransferBuffers buffers;
  RegisterStandardBuffers(env, buffers);

  const AscendString remote_engine = env.RemoteEngine();
  EXPECT_EQ(env.engine1.Connect(remote_engine, kTimeoutMs), SUCCESS);
  TransferWriteReadSync(env, buffers);
  ExpectPattern(buffers.dst, kDataPattern, "Verification failed at index ");

  EXPECT_EQ(env.engine1.Disconnect(remote_engine, kTimeoutMs), SUCCESS);
  DeregisterStandardBuffers(env, buffers);
  env.Finalize();
}

TEST_F(FabricMemSTest, TestHixlFabricMemAutoConnect) {
  const int32_t port = test::AllocateFabricMemTestPort();
  ASSERT_GT(port, 0);
  FabricMemTestEnv env(BuildRemoteEngineId(port), true);
  TransferBuffers buffers;
  RegisterStandardBuffers(env, buffers);

  TransferWriteReadSync(env, buffers);
  ExpectPattern(buffers.dst, kDataPattern, "Sync verification failed at index ");
  std::fill(buffers.dst.data.begin(), buffers.dst.data.end(), kInitPattern);

  TransferReq write_req = nullptr;
  SubmitAsyncTransfer(env, WRITE, buffers.src, buffers.remote, write_req);
  WaitForAsyncComplete(env.engine1, write_req);

  TransferReq read_req = nullptr;
  SubmitAsyncTransfer(env, READ, buffers.dst, buffers.remote, read_req);
  WaitForAsyncComplete(env.engine1, read_req);
  ExpectPattern(buffers.dst, kDataPattern, "Async verification failed at index ");

  EXPECT_EQ(env.engine1.Disconnect(env.RemoteEngine(), kTimeoutMs), SUCCESS);
  DeregisterStandardBuffers(env, buffers);
  env.Finalize();
}

TEST_F(FabricMemSTest, TestHixlFabricMemMultiTargetParallel) {
  const int32_t port2 = test::AllocateFabricMemTestPort();
  ASSERT_GT(port2, 0);
  const int32_t port3 = test::AllocateFabricMemTestPort();
  ASSERT_GT(port3, 0);
  TripleFabricMemTestEnv env(port2, port3);

  DeviceBuffer src{kDataPattern};
  DeviceBuffer remote_buf2{kInitPattern};
  DeviceBuffer remote_buf3{kInitPattern};
  EXPECT_EQ(RegisterDeviceBuffer(env.engine1, src), SUCCESS);
  EXPECT_EQ(RegisterDeviceBuffer(env.engine2, remote_buf2), SUCCESS);
  EXPECT_EQ(RegisterDeviceBuffer(env.engine3, remote_buf3), SUCCESS);

  const AscendString remote2(env.engine2_ip.c_str());
  const AscendString remote3(env.engine3_ip.c_str());
  EXPECT_EQ(env.engine1.Connect(remote2, kTimeoutMs), SUCCESS);
  EXPECT_EQ(env.engine1.Connect(remote3, kTimeoutMs), SUCCESS);

  const TransferOpDesc desc2 = BuildTransferDesc(src, remote_buf2);
  const TransferOpDesc desc3 = BuildTransferDesc(src, remote_buf3);
  std::vector<std::thread> threads;
  threads.emplace_back(
      [&]() { EXPECT_EQ(env.engine1.TransferSync(remote2, WRITE, {desc2}, kTimeoutMs), SUCCESS); });
  threads.emplace_back(
      [&]() { EXPECT_EQ(env.engine1.TransferSync(remote3, WRITE, {desc3}, kTimeoutMs), SUCCESS); });
  JoinThreads(threads);

  EXPECT_EQ(env.engine1.Disconnect(remote2, kTimeoutMs), SUCCESS);
  EXPECT_EQ(env.engine1.Disconnect(remote3, kTimeoutMs), SUCCESS);
  EXPECT_EQ(env.engine1.DeregisterMem(src.handle), SUCCESS);
  EXPECT_EQ(env.engine2.DeregisterMem(remote_buf2.handle), SUCCESS);
  EXPECT_EQ(env.engine3.DeregisterMem(remote_buf3.handle), SUCCESS);
  env.Finalize();
}

TEST_F(FabricMemSTest, TestHixlFabricMemConcurrentAsync) {
  const int32_t port = test::AllocateFabricMemTestPort();
  ASSERT_GT(port, 0);
  FabricMemTestEnv env(BuildRemoteEngineId(port), true);
  DeviceBuffer src{kDataPattern};
  DeviceBuffer remote_buf{kInitPattern};
  EXPECT_EQ(RegisterDeviceBuffer(env.engine1, src), SUCCESS);
  EXPECT_EQ(RegisterDeviceBuffer(env.engine2, remote_buf), SUCCESS);

  std::array<TransferReq, kAsyncRequestCount> reqs{};
  std::vector<std::thread> async_threads;
  for (size_t i = 0U; i < reqs.size(); ++i) {
    async_threads.emplace_back([&, i]() { SubmitAsyncTransfer(env, WRITE, src, remote_buf, reqs[i]); });
  }
  JoinThreads(async_threads);
  WaitForAllAsyncWrites(env.engine1, reqs);

  EXPECT_EQ(env.engine1.Disconnect(env.RemoteEngine(), kTimeoutMs), SUCCESS);
  EXPECT_EQ(env.engine1.DeregisterMem(src.handle), SUCCESS);
  EXPECT_EQ(env.engine2.DeregisterMem(remote_buf.handle), SUCCESS);
  env.Finalize();
}

TEST_F(FabricMemSTest, TestHixlFabricMemDisconnectDuringAsync) {
  const int32_t port = test::AllocateFabricMemTestPort();
  ASSERT_GT(port, 0);
  FabricMemTestEnv env(BuildRemoteEngineId(port));
  DeviceBuffer src{kDataPattern};
  DeviceBuffer remote_buf{kInitPattern};
  EXPECT_EQ(RegisterDeviceBuffer(env.engine1, src), SUCCESS);
  EXPECT_EQ(RegisterDeviceBuffer(env.engine2, remote_buf), SUCCESS);

  const AscendString remote_engine = env.RemoteEngine();
  EXPECT_EQ(env.engine1.Connect(remote_engine, kTimeoutMs), SUCCESS);

  TransferReq req = nullptr;
  SubmitAsyncTransfer(env, WRITE, src, remote_buf, req);

  EXPECT_EQ(env.engine1.Disconnect(remote_engine, kTimeoutMs), SUCCESS);

  TransferStatus status = TransferStatus::WAITING;
  Status get_status_ret = env.engine1.GetTransferStatus(req, status);
  // After Disconnect, ReleasePendingAsyncLeasesLocked erases the request from req_map_,
  // so GetTransferStatus may return PARAM_INVALID (request not found) in addition to
  // NOT_CONNECTED, COMPLETED, or FAILED.
  EXPECT_TRUE(get_status_ret == NOT_CONNECTED || get_status_ret == PARAM_INVALID ||
              status == TransferStatus::COMPLETED || status == TransferStatus::FAILED);

  EXPECT_EQ(env.engine1.DeregisterMem(src.handle), SUCCESS);
  EXPECT_EQ(env.engine2.DeregisterMem(remote_buf.handle), SUCCESS);
  env.Finalize();
}

}  // namespace hixl
