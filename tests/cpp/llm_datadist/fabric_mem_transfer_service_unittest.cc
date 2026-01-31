/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <gtest/gtest.h>
#include <vector>
#include <memory>
#include <numeric>
#include <cstring>
#include <algorithm>
#include <cstdlib>
#include "adxl/fabric_mem_transfer_service.h"
#include "adxl/channel.h"
#include "depends/ascendcl/src/ascendcl_stub.h"
#include "acl/acl.h"
#include "common/def_types.h"
#include "adxl/virtual_memory_manager.h"

namespace adxl {
namespace {
const std::string kChannelId = "test_channel";
constexpr uint64_t kMemAddr = 0x1000;
constexpr uint64_t kMemLen = 1024;
constexpr uint64_t kRemoteAddr = 0x3000;
constexpr uint64_t kTransferLen = 100;
constexpr int32_t kDeviceId = 0;
constexpr int32_t kPeerRankId = 1;
constexpr int32_t kTimeout = 1000;
constexpr uint8_t kPatternA = 0xAA;
constexpr int32_t kMaxPollRetries = 10;
constexpr uint64_t kReqBase = 0x1000;
constexpr size_t kStreamMax = 256;
constexpr size_t kMemOverLen = 2;
constexpr const char *kSoName = "libascendcl_stub.so";

class ScopedRuntimeMock {
 public:
  explicit ScopedRuntimeMock(const std::shared_ptr<llm::AclRuntimeStub> &instance) {
    llm::AclRuntimeStub::SetInstance(instance);
  }
  ~ScopedRuntimeMock() {
    llm::AclRuntimeStub::Reset();
  }
  ScopedRuntimeMock(const ScopedRuntimeMock &) = delete;
  ScopedRuntimeMock &operator=(const ScopedRuntimeMock &) = delete;

  ScopedRuntimeMock(const ScopedRuntimeMock &&) = delete;
  ScopedRuntimeMock &operator=(const ScopedRuntimeMock &&) = delete;
};

class ScopedRuntimeFunctionFail {
 public:
  explicit ScopedRuntimeFunctionFail(const std::string &func_name) : old_(llm::GetAclStubMock()) {
    llm::GetAclStubMock() = func_name;
  }
  ~ScopedRuntimeFunctionFail() {
    llm::GetAclStubMock() = old_;
  }
  ScopedRuntimeFunctionFail(const ScopedRuntimeFunctionFail &) = delete;
  ScopedRuntimeFunctionFail &operator=(const ScopedRuntimeFunctionFail &) = delete;

  ScopedRuntimeFunctionFail(ScopedRuntimeFunctionFail &&) = delete;
  ScopedRuntimeFunctionFail &operator=(ScopedRuntimeFunctionFail &&) = delete;

 private:
  std::string old_;
};

ChannelPtr CreateInitializedChannel() {
  ChannelInfo channel_info{};
  channel_info.channel_id = kChannelId;
  channel_info.local_rank_id = 0;
  channel_info.peer_rank_id = kPeerRankId;
  auto channel = std::make_shared<Channel>(channel_info);
  EXPECT_EQ(channel->Initialize(true), SUCCESS);
  return channel;
}

void *GetBackingRemotePtr(const ChannelPtr &channel, uint64_t remote_addr) {
  auto va_map = channel->GetNewVaToOldVa();
  if (va_map.empty()) {
    return nullptr;
  }
  for (auto &kv : va_map) {
    auto new_va = kv.first;
    auto &info = kv.second;
    if (info.va_addr == remote_addr) {
      return llm::ValueToPtr(new_va);
    }
  }
  return nullptr;
}
}  // namespace

class FabricMemTransferServiceUTest : public ::testing::Test {
 protected:
  void SetUp() override {
    mock_runtime_ = std::make_shared<llm::AclRuntimeStub>();
    scoped_mock_ = std::make_unique<ScopedRuntimeMock>(mock_runtime_);
    VirtualMemoryManager::GetInstance().SetSoName(kSoName);
    VirtualMemoryManager::GetInstance().Initialize();
    service_ = std::make_shared<FabricMemTransferService>();
  }
  void TearDown() override {
    if (service_) {
      service_->Finalize();
    }
    scoped_mock_.reset();
    mock_runtime_.reset();
    VirtualMemoryManager::GetInstance().Finalize();
  }
  std::shared_ptr<FabricMemTransferService> service_;
  std::shared_ptr<llm::AclRuntimeStub> mock_runtime_;
  std::unique_ptr<ScopedRuntimeMock> scoped_mock_;
};

TEST_F(FabricMemTransferServiceUTest, TestInitialize) {
  EXPECT_EQ(service_->Initialize(kStreamMax), SUCCESS);
}

TEST_F(FabricMemTransferServiceUTest, TestRegisterMem) {
  ASSERT_EQ(service_->Initialize(kStreamMax), SUCCESS);
  MemDesc mem_desc;
  mem_desc.addr = kMemAddr;
  mem_desc.len = kMemLen;
  MemHandle handle;
  EXPECT_EQ(service_->RegisterMem(mem_desc, MemType::MEM_DEVICE, handle), SUCCESS);
  EXPECT_EQ(service_->DeregisterMem(handle), SUCCESS);
}

TEST_F(FabricMemTransferServiceUTest, TestTransfer) {
  ASSERT_EQ(service_->Initialize(kStreamMax), SUCCESS);

  auto channel = CreateInitializedChannel();

  ShareHandleInfo share_info;
  share_info.va_addr = kRemoteAddr;
  share_info.len = kMemLen;
  std::vector<ShareHandleInfo> share_handles = {share_info};
  EXPECT_EQ(channel->ImportMem(share_handles, kDeviceId), SUCCESS);

  // Simulate remote memory backing buffer
  void *backing_remote_ptr = GetBackingRemotePtr(channel, kRemoteAddr);
  ASSERT_NE(backing_remote_ptr, nullptr);

  std::vector<uint8_t> local_buf(kMemLen);
  std::vector<TransferOpDesc> op_descs;
  TransferOpDesc desc;
  desc.local_addr = (uintptr_t)local_buf.data();
  desc.remote_addr = kRemoteAddr;
  desc.len = kTransferLen;
  op_descs.push_back(desc);
  // Case 1: Write (Local -> Remote)
  std::fill(local_buf.begin(), local_buf.end(), kPatternA);
  EXPECT_EQ(service_->Transfer(channel, TransferOp::WRITE, op_descs, kTimeout), SUCCESS);

  for (size_t i = 0; i < kTransferLen; ++i) {
    EXPECT_EQ(((uint8_t *)backing_remote_ptr)[i], kPatternA) << "Write verification failed at index " << i;
  }

  // Case 2: Read (Remote -> Local)
  std::fill(local_buf.begin(), local_buf.end(), 0);  // Clear local

  EXPECT_EQ(service_->Transfer(channel, TransferOp::READ, op_descs, kTimeout), SUCCESS);

  for (size_t i = 0; i < kTransferLen; ++i) {
    EXPECT_EQ(local_buf[i], kPatternA) << "Read verification failed at index " << i;
  }

  channel->Finalize();
}

TEST_F(FabricMemTransferServiceUTest, TestTransferAsync) {
  ASSERT_EQ(service_->Initialize(kStreamMax), SUCCESS);
  auto channel = CreateInitializedChannel();

  std::vector<uint8_t> local_buf(kMemLen);
  std::vector<TransferOpDesc> op_descs;
  TransferOpDesc desc;
  desc.local_addr = (uintptr_t)local_buf.data();
  desc.remote_addr = kRemoteAddr;
  desc.len = kTransferLen;
  op_descs.push_back(desc);

  ShareHandleInfo share_info;
  share_info.va_addr = kRemoteAddr;
  share_info.len = kMemLen;
  std::vector<ShareHandleInfo> share_handles = {share_info};
  EXPECT_EQ(channel->ImportMem(share_handles, kDeviceId), SUCCESS);

  // Simulate remote memory backing buffer
  void *backing_remote_ptr = GetBackingRemotePtr(channel, kRemoteAddr);
  ASSERT_NE(backing_remote_ptr, nullptr);

  // Case 1: Write (Local -> Remote)
  std::fill(local_buf.begin(), local_buf.end(), kPatternA);
  TransferReq req = (void *)1;
  EXPECT_EQ(service_->TransferAsync(channel, TransferOp::WRITE, op_descs, req), SUCCESS);

  // Verify status (should be COMPLETED immediately in stub env usually, or WAITING if events not recorded?)
  // In stub, aclrtRecordEvent is just a no-op or records immediately.
  TransferStatus status = TransferStatus::WAITING;
  // Poll until completed.
  int32_t retries = 0;
  while (status != TransferStatus::COMPLETED && retries++ < kMaxPollRetries) {
    EXPECT_EQ(service_->GetTransferStatus(channel, req, status), SUCCESS);
  }
  EXPECT_EQ(status, TransferStatus::COMPLETED);

  // For async, we need to synchronize to ensure data is transferred before checking
  // In a real scenario, this would involve waiting on an event or stream.
  // For stubbed runtime, we assume it's "fast enough" or mock a sync.
  // Since aclrtMemcpyAsync in stub performs direct memcpy, we can check immediately.
  for (size_t i = 0; i < kTransferLen; ++i) {
    EXPECT_EQ(((uint8_t *)backing_remote_ptr)[i], kPatternA) << "Async Write verification failed at index " << i;
  }

  // Case 2: Read (Remote -> Local)
  std::fill(local_buf.begin(), local_buf.end(), 0);  // Clear local
  EXPECT_EQ(service_->TransferAsync(channel, TransferOp::READ, op_descs, req), SUCCESS);

  status = TransferStatus::WAITING;
  retries = 0;
  while (status != TransferStatus::COMPLETED && retries++ < kMaxPollRetries) {
    EXPECT_EQ(service_->GetTransferStatus(channel, req, status), SUCCESS);
  }
  EXPECT_EQ(status, TransferStatus::COMPLETED);

  // Similar to write, check immediately due to stubbed memcpy.
  for (size_t i = 0; i < kTransferLen; ++i) {
    EXPECT_EQ(local_buf[i], kPatternA) << "Async Read verification failed at index " << i;
  }
  channel->Finalize();
}

TEST_F(FabricMemTransferServiceUTest, TestTransferAsync_RemoteAddrNotFound) {
  ASSERT_EQ(service_->Initialize(kStreamMax), SUCCESS);
  auto channel = CreateInitializedChannel();
  ShareHandleInfo share_info;
  share_info.va_addr = kRemoteAddr;
  share_info.len = kMemLen;
  EXPECT_EQ(channel->ImportMem({share_info}, kDeviceId), SUCCESS);

  std::vector<uint8_t> local_buf(kMemLen, 0);
  TransferOpDesc desc{};
  desc.local_addr = llm::PtrToValue(local_buf.data());
  desc.remote_addr = kRemoteAddr + kMemLen + 1;
  desc.len = kTransferLen;

  TransferReq req = llm::ValueToPtr(kReqBase);
  EXPECT_EQ(service_->TransferAsync(channel, TransferOp::WRITE, {desc}, req), PARAM_INVALID);

  TransferStatus status = TransferStatus::WAITING;
  EXPECT_EQ(service_->GetTransferStatus(channel, req, status), FAILED);
  channel->Finalize();
}

TEST_F(FabricMemTransferServiceUTest, TestTransferAsync_RemoteAddrOutOfRange) {
  ASSERT_EQ(service_->Initialize(kStreamMax), SUCCESS);
  auto channel = CreateInitializedChannel();
  ShareHandleInfo share_info;
  share_info.va_addr = kRemoteAddr;
  share_info.len = kMemLen;
  EXPECT_EQ(channel->ImportMem({share_info}, kDeviceId), SUCCESS);

  std::vector<uint8_t> local_buf(kMemLen, 0);
  TransferOpDesc desc{};
  desc.local_addr = llm::PtrToValue(local_buf.data());
  desc.remote_addr = kRemoteAddr + kMemLen - 1;
  desc.len = kMemOverLen;

  TransferReq req = llm::ValueToPtr(kReqBase + 1);
  EXPECT_EQ(service_->TransferAsync(channel, TransferOp::WRITE, {desc}, req), PARAM_INVALID);

  TransferStatus status = TransferStatus::WAITING;
  EXPECT_EQ(service_->GetTransferStatus(channel, req, status), FAILED);
  channel->Finalize();
}

TEST_F(FabricMemTransferServiceUTest, TestTransferAsync_EventCreateFail_CleanupOk) {
  ASSERT_EQ(service_->Initialize(kStreamMax), SUCCESS);
  auto channel = CreateInitializedChannel();
  ShareHandleInfo share_info;
  share_info.va_addr = kRemoteAddr;
  share_info.len = kMemLen;
  EXPECT_EQ(channel->ImportMem({share_info}, kDeviceId), SUCCESS);

  std::vector<uint8_t> local_buf(kMemLen);
  TransferOpDesc desc{};
  desc.local_addr = llm::PtrToValue(local_buf.data());
  desc.remote_addr = kRemoteAddr;
  desc.len = kTransferLen;
  void *backing_remote_ptr = GetBackingRemotePtr(channel, kRemoteAddr);
  ASSERT_NE(backing_remote_ptr, nullptr);

  std::fill(local_buf.begin(), local_buf.end(), kPatternA);

  TransferReq req = llm::ValueToPtr(kReqBase);
  {
    ScopedRuntimeFunctionFail fail("aclrtCreateEvent");
    EXPECT_NE(service_->TransferAsync(channel, TransferOp::WRITE, {desc}, req), SUCCESS);
  }

  TransferStatus status = TransferStatus::WAITING;
  EXPECT_EQ(service_->GetTransferStatus(channel, req, status), FAILED);

  // Verify service remains usable after cleanup.
  req = llm::ValueToPtr(kReqBase + 1);
  EXPECT_EQ(service_->TransferAsync(channel, TransferOp::WRITE, {desc}, req), SUCCESS);
  status = TransferStatus::WAITING;
  int32_t retries = 0;
  while (status != TransferStatus::COMPLETED && retries++ < kMaxPollRetries) {
    EXPECT_EQ(service_->GetTransferStatus(channel, req, status), SUCCESS);
  }
  EXPECT_EQ(status, TransferStatus::COMPLETED);
  for (size_t i = 0; i < kTransferLen; ++i) {
    EXPECT_EQ(((uint8_t *)backing_remote_ptr)[i], kPatternA) << "Write verification failed at index " << i;
  }
  channel->Finalize();
}

TEST_F(FabricMemTransferServiceUTest, TestGetTransferStatus_QueryStatusFail_CleanupOk) {
  ASSERT_EQ(service_->Initialize(kStreamMax), SUCCESS);
  auto channel = CreateInitializedChannel();
  ShareHandleInfo share_info_test;
  share_info_test.va_addr = kRemoteAddr;
  share_info_test.len = kMemLen;
  EXPECT_EQ(channel->ImportMem({share_info_test}, kDeviceId), SUCCESS);

  std::vector<uint8_t> test_local_buf(kMemLen, 0);
  TransferOpDesc desc{};
  desc.remote_addr = kRemoteAddr;
  desc.len = kTransferLen;
  desc.local_addr = llm::PtrToValue(test_local_buf.data());

  TransferReq req = llm::ValueToPtr(kReqBase);
  EXPECT_EQ(service_->TransferAsync(channel, TransferOp::WRITE, {desc}, req), SUCCESS);

  class QueryFailRuntimeMock : public llm::AclRuntimeStub {
   public:
    aclError aclrtQueryEventStatus(aclrtEvent evt, aclrtEventRecordedStatus *status) override {
      (void)evt;
      (void)status;
      return -1;
    }
  };
  [[maybe_unused]] ScopedRuntimeMock runtime_mock(std::make_shared<QueryFailRuntimeMock>());

  TransferStatus status = TransferStatus::WAITING;
  EXPECT_EQ(service_->GetTransferStatus(channel, req, status), FAILED);
  EXPECT_EQ(status, TransferStatus::FAILED);

  // Ensure record is erased and no crash.
  status = TransferStatus::WAITING;
  EXPECT_EQ(service_->GetTransferStatus(channel, req, status), FAILED);
  channel->Finalize();
}

TEST_F(FabricMemTransferServiceUTest, TestTransferAsync_StreamPoolFull) {
  ASSERT_EQ(service_->Initialize(kStreamMax), SUCCESS);
  auto channel = CreateInitializedChannel();
  ShareHandleInfo share_info;
  share_info.va_addr = kRemoteAddr;
  share_info.len = kMemLen;
  EXPECT_EQ(channel->ImportMem({share_info}, kDeviceId), SUCCESS);

  std::vector<uint8_t> local_buf(kMemLen, 0);
  TransferOpDesc desc{};
  desc.local_addr = llm::PtrToValue(local_buf.data());
  desc.remote_addr = kRemoteAddr;
  desc.len = kTransferLen;

  // kStreamPoolMaxNum=256, kTaskStreamNum=4 -> 64 concurrent reqs will occupy all streams.
  constexpr size_t kMaxConcurrentReq = 52;
  for (size_t i = 0; i < kMaxConcurrentReq; ++i) {
    TransferReq req = llm::ValueToPtr(kReqBase + i);
    EXPECT_EQ(service_->TransferAsync(channel, TransferOp::WRITE, {desc}, req), SUCCESS);
  }
  TransferReq overflow_req = llm::ValueToPtr(kReqBase + kMaxConcurrentReq);
  EXPECT_EQ(service_->TransferAsync(channel, TransferOp::WRITE, {desc}, overflow_req), FAILED);

  // Cleanup should not crash and should allow future TransferAsync.
  service_->RemoveChannel(kChannelId);

  TransferReq req = llm::ValueToPtr(kReqBase);
  EXPECT_EQ(service_->TransferAsync(channel, TransferOp::WRITE, {desc}, req), SUCCESS);
  TransferStatus status = TransferStatus::WAITING;
  int32_t retries = 0;
  while (status != TransferStatus::COMPLETED && retries++ < kMaxPollRetries) {
    EXPECT_EQ(service_->GetTransferStatus(channel, req, status), SUCCESS);
  }
  EXPECT_EQ(status, TransferStatus::COMPLETED);
  channel->Finalize();
}

TEST_F(FabricMemTransferServiceUTest, TestGetShareHandles) {
  ASSERT_EQ(service_->Initialize(kStreamMax), SUCCESS);
  MemDesc mem_desc;
  mem_desc.addr = kMemAddr;
  mem_desc.len = kMemLen;
  MemHandle handle;
  EXPECT_EQ(service_->RegisterMem(mem_desc, MemType::MEM_DEVICE, handle), SUCCESS);

  auto handles = service_->GetShareHandles();
  EXPECT_EQ(handles.size(), 1);
  EXPECT_EQ(handles[0].va_addr, kMemAddr);

  EXPECT_EQ(service_->DeregisterMem(handle), SUCCESS);
}

}  // namespace adxl
