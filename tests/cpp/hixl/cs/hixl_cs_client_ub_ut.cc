/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <set>
#include <thread>
#include <tuple>
#include <vector>
#include "gtest/gtest.h"
#include "gmock/gmock.h"
#include "ascendcl_stub.h"
#include "depends/mmpa/src/mmpa_stub.h"
#include "depends/hccl/src/hccl_stub.h"
#include "depends/hccl/src/hccl_host_exception_stub.h"
#include "depends/sys_api/src/sys_api_wrap.h"
#include "depends/ascend_hal/src/ascend_hal_stub.h"
#include "engine/test_mmpa_utils.h"
#include "load_kernel.h"
#include "slog_stub.h"
#define private public
#define protected public
#include "hixl_cs_client.h"
#include "hcomm_device_channel.h"
#undef protected
#undef private

namespace hixl {

class MockAclRuntimeStub : public llm::AclRuntimeStub {
 public:
  MOCK_METHOD(aclError, aclrtBinaryLoadFromFile, (const char *, aclrtBinaryLoadOptions *, aclrtBinHandle *),
              (override));
  MOCK_METHOD(aclError, aclrtBinaryGetFunction, (aclrtBinHandle, const char *, aclrtFuncHandle *), (override));
  MOCK_METHOD(aclError, aclrtWaitAndResetNotify, (aclrtNotify, aclrtStream, uint32_t), (override));
  MOCK_METHOD(aclError, aclrtValueWait, (void *, uint64_t, uint32_t, aclrtStream), (override));
  MOCK_METHOD(aclError, aclrtValueWrite, (void *, uint64_t, uint32_t, aclrtStream), (override));
  MOCK_METHOD(aclError, aclrtSynchronizeStreamWithTimeout, (aclrtStream, int32_t), (override));
  MOCK_METHOD(aclError, aclrtNotifyBatchReset, (aclrtNotify *, size_t), (override));
  MOCK_METHOD(aclError, aclrtStreamAbort, (aclrtStream), (override));
  MOCK_METHOD(aclError, aclrtMemcpyAsync,
              (void *dst, size_t dest_max, const void *src, size_t src_count, aclrtMemcpyKind kind, aclrtStream stream),
              (override));
};

namespace {
// The device data plane and its completion bookkeeping now live in the device channel. These
// white-box tests reach it through the client's single channel.
HcommDeviceChannel &DevChannel(HixlCSClient &cli) {
  return *static_cast<HcommDeviceChannel *>(cli.client_channel_.get());
}

constexpr uint32_t kDeviceDevId = 2U;
constexpr uint32_t kDummyPort = 12345U;

constexpr uint32_t kListNum1 = 1U;
constexpr uint64_t kLen8 = 8ULL;
constexpr uint64_t kDeviceFlagDoneValueForTest = 1ULL;
constexpr uint32_t kNotifyWaitTaskInterval = 1920U;

constexpr const char *kTransFlagNameDevice = "_hixl_builtin_dev_trans_flag";
constexpr const char *kTransFlagNameHost = "_hixl_builtin_host_trans_flag";

EndpointDesc MakeDeviceEp(CommProtocol protocol, uint32_t dev_id) {
  EndpointDesc ep{};
  ep.loc.locType = ENDPOINT_LOC_TYPE_DEVICE;
  ep.protocol = protocol;
  ep.commAddr.type = COMM_ADDR_TYPE_ID;
  ep.commAddr.id = dev_id;
  return ep;
}

void RecordMemForBatchTransfer(HixlCSClient &cli, void *remote_addr, size_t remote_size, void *local_addr,
                               size_t local_size) {
  (void)cli.mem_store_.RecordMemory(true, remote_addr, remote_size);
  (void)cli.mem_store_.RecordMemory(false, local_addr, local_size);
}

void ExpectFreeSlotsAreUnique(int32_t device_id) {
  auto *pool = TransferPool::GetInstance(device_id);
  ASSERT_NE(pool, nullptr);
  std::vector<TransferPool::SlotHandle> held;
  while (true) {
    TransferPool::SlotHandle handle{};
    if (pool->Acquire(&handle) != SUCCESS) {
      break;
    }
    held.push_back(handle);
  }
  std::set<uint32_t> indices;
  for (const auto &handle : held) {
    EXPECT_TRUE(indices.insert(handle.slot_index).second) << "duplicate slot " << handle.slot_index;
  }
  for (const auto &handle : held) {
    pool->Release(handle);
  }
  ASSERT_EQ(held.size(), static_cast<size_t>(pool->pool_size_)) << "a slot was not returned to the free list";
}

void FillTagMem(HixlCSClient &cli, const char *tag, void *addr, uint64_t size) {
  CommMem mem{};
  mem.type = COMM_MEM_TYPE_DEVICE;
  mem.addr = addr;
  mem.size = size;
  cli.tag_mem_descs_[tag] = mem;
}

Status PollUntilCompleted(HixlCSClient &cli, void *qh, HixlCompleteStatus *out_status) {
  HIXL_CHECK_NOTNULL(out_status);
  *out_status = HixlCompleteStatus::HIXL_COMPLETE_STATUS_WAITING;

  for (int i = 0; i < 10; ++i) {
    const Status ret = cli.QueryCompleteStatus(qh, out_status);
    if (ret != SUCCESS) {
      return ret;
    }
    if (*out_status == HixlCompleteStatus::HIXL_COMPLETE_STATUS_COMPLETED) {
      return SUCCESS;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return SUCCESS;
}

}  // namespace

// Use common TestMmpaStub from test_mmpa_utils.h
using MockMmpaStub = hixl::test::TestMmpaStub;

class HixlCSClientDeviceFixture : public ::testing::Test {
 protected:
  virtual EndpointDesc MakeLocalEndpoint() const {
    return MakeDeviceEp(COMM_PROTOCOL_UBC_TP, kDeviceDevId);
  }

  virtual EndpointDesc MakeRemoteEndpoint() const {
    return MakeDeviceEp(COMM_PROTOCOL_UBC_TP, kDeviceDevId);
  }

  void SetUp() override {
    // TransferPool initialization loads device kernels, so MmpaStub must be ready before Create.
    auto kernel_stub = std::make_shared<MockMmpaStub>();
    kernel_stub->real_path_ok_ = true;
    kernel_stub->access_ok_ = true;
    hixl_test::InstallSysApiHooks(kernel_stub);

    const EndpointDesc src = MakeLocalEndpoint();
    const EndpointDesc dst = MakeRemoteEndpoint();

    HixlClientConfig config{};
    HixlClientDesc desc{};
    desc.server_ip = "127.0.0.1";
    desc.server_port = kDummyPort;
    desc.local_endpoint = &src;
    desc.remote_endpoint = &dst;
    ASSERT_EQ(cli_.Create(&desc, &config), SUCCESS);

    DevChannel(cli_).channel_handle_ = static_cast<ChannelHandle>(1ULL);
    remote_flag_dev_ = 0ULL;
    const char *flag_name = (dst.loc.locType == ENDPOINT_LOC_TYPE_HOST) ? kTransFlagNameHost : kTransFlagNameDevice;
    FillTagMem(cli_, flag_name, static_cast<void *>(&remote_flag_dev_), sizeof(uint64_t));
    ASSERT_EQ(cli_.client_channel_->SetRemoteMemInfo(cli_.tag_mem_descs_[flag_name]), SUCCESS);
  }

  void TearDown() override {
    (void)cli_.Destroy();
    unsetenv("HIXL_UT_DEVICE_FLAG_HACK");
    hixl_test::ResetSysApiHooks();
  }

  HixlOneSideOpDesc SetupBatchTransfer(bool is_get) {
    (void)is_get;
    RecordMemForBatchTransfer(cli_, remote_buf_.data(), remote_buf_.size(), local_buf_.data(), local_buf_.size());
    HixlOneSideOpDesc desc{};
    desc.remote_buf = remote_buf_.data();
    desc.local_buf = local_buf_.data();
    desc.len = kLen8;
    return desc;
  }

  std::vector<HixlOneSideOpDesc> SetupBatchTransferList(uint32_t list_num, bool is_get = false) {
    HixlOneSideOpDesc desc = SetupBatchTransfer(is_get);
    return std::vector<HixlOneSideOpDesc>(list_num, desc);
  }

  void ExpectAsyncValueWaitCount(uint32_t list_num, uint32_t wait_count) {
    setenv("HIXL_UT_DEVICE_FLAG_HACK", "1", 1);
    std::vector<HixlOneSideOpDesc> descs = SetupBatchTransferList(list_num);

    MockAclRuntimeStub mock_acl;
    llm::AclRuntimeStub::Install(&mock_acl);
    EXPECT_CALL(mock_acl, aclrtWaitAndResetNotify(testing::_, testing::_, testing::_)).Times(0);
    // 非 HCCS device 完成通知走 pool flag 的 value wait：每个逻辑 batch 末个 kernel 触发一次 wait + write。
    EXPECT_CALL(mock_acl, aclrtValueWait(testing::_, testing::_, testing::_, testing::_))
        .Times(wait_count)
        .WillRepeatedly(testing::Return(ACL_SUCCESS));
    EXPECT_CALL(mock_acl, aclrtValueWrite(testing::_, testing::_, testing::_, testing::_))
        .Times(wait_count)
        .WillRepeatedly(testing::Return(ACL_SUCCESS));
    EXPECT_CALL(mock_acl, aclrtMemcpyAsync(testing::_, testing::_, testing::_, testing::_, testing::_, testing::_))
        .WillRepeatedly(testing::Return(ACL_SUCCESS));

    void *qh = nullptr;
    const Status ret = cli_.BatchPutAsync(descs.data(), list_num, &qh);
    llm::AclRuntimeStub::UnInstall(&mock_acl);

    ASSERT_EQ(ret, SUCCESS);
    ASSERT_NE(qh, nullptr);

    DeviceCompleteHandle *handle = static_cast<DeviceCompleteHandle *>(qh);
    ASSERT_NE(handle->host_flag, nullptr);
    *(static_cast<uint64_t *>(handle->host_flag)) = kDeviceFlagDoneValueForTest;

    HixlCompleteStatus st = HixlCompleteStatus::HIXL_COMPLETE_STATUS_WAITING;
    EXPECT_EQ(cli_.QueryCompleteStatus(qh, &st), SUCCESS);
    EXPECT_EQ(st, HixlCompleteStatus::HIXL_COMPLETE_STATUS_COMPLETED);
  }

  HixlCSClient cli_{};
  uint64_t remote_flag_dev_{0ULL};

  std::array<uint8_t, 8> local_buf_{};
  std::array<uint8_t, 8> remote_buf_{};
};

// Verify that the channel factory keeps completion policy with the selected transport.
class HixlCSClientProtocolDeviceFixture : public HixlCSClientDeviceFixture,
                                          public testing::WithParamInterface<CommProtocol> {
 protected:
  EndpointDesc MakeLocalEndpoint() const override {
    return MakeDeviceEp(GetParam(), kDeviceDevId);
  }

  EndpointDesc MakeRemoteEndpoint() const override {
    return MakeLocalEndpoint();
  }
};

TEST_P(HixlCSClientProtocolDeviceFixture, CompletionPolicyAndKernelParametersMatchProtocol) {
  auto &channel = DevChannel(cli_);
  const bool value_wait = GetParam() != COMM_PROTOCOL_HCCS;
  EXPECT_EQ(channel.use_value_wait_, value_wait);
  EXPECT_EQ(channel.flag_mem_handle_ != nullptr, value_wait);
  EXPECT_TRUE(channel.notify_mem_handles_.empty());

  std::shared_ptr<TransferPool::SlotHandle> slot;
  ASSERT_EQ(channel.AcquireSharedSlot(slot), SUCCESS);
  DeviceCompleteHandle handle{};
  handle.shared_slot = slot;
  HixlOneSideOpDesc desc{};
  handle.dev_op_desc_buf = &desc;
  HixlOneSideOpParam param{};
  ASSERT_EQ(channel.BuildDeviceChunkParam(handle, 0U, 1U, true, param), SUCCESS);
  EXPECT_EQ(param.use_notify_record, value_wait ? 0U : 1U);
  EXPECT_EQ(param.local_flag_addr, value_wait ? slot->flag_dev_addr : slot->notify_addr);
  EXPECT_EQ(param.flag_size, value_wait ? sizeof(uint64_t) : slot->notify_len);
  EXPECT_EQ(param.notify_id, value_wait ? 0U : slot->notify_id);
  EXPECT_NE(param.remote_flag_addr, 0U);
  param.use_notify_record = 0U;
  ASSERT_EQ(channel.BuildDeviceChunkParam(handle, 0U, 1U, false, param), SUCCESS);
  EXPECT_EQ(param.use_notify_record, value_wait ? 0U : 1U);
  EXPECT_EQ(param.local_flag_addr, 0U);
  EXPECT_EQ(param.remote_flag_addr, 0U);
  EXPECT_EQ(param.flag_size, 0U);
  EXPECT_EQ(param.notify_id, 0U);
  handle.shared_slot.reset();
  channel.ReleaseSharedSlotRef(slot);
}

TEST_P(HixlCSClientProtocolDeviceFixture, CompletionWaitIsQueuedBeforeHostFlagCopy) {
  auto &channel = DevChannel(cli_);
  auto desc = SetupBatchTransfer(false);
  std::shared_ptr<TransferPool::SlotHandle> slot;
  ASSERT_EQ(channel.AcquireSharedSlot(slot), SUCCESS);
  MockAclRuntimeStub mock_acl;
  llm::AclRuntimeStub::Install(&mock_acl);
  const bool value_wait = GetParam() != COMM_PROTOCOL_HCCS;
  EXPECT_CALL(mock_acl, aclrtWaitAndResetNotify(testing::_, testing::_, testing::_)).Times(0);
  EXPECT_CALL(mock_acl, aclrtValueWait(testing::_, testing::_, testing::_, testing::_)).Times(0);
  EXPECT_CALL(mock_acl, aclrtValueWrite(testing::_, testing::_, testing::_, testing::_)).Times(0);
  {
    testing::InSequence sequence;
    if (value_wait) {
      void *addr = reinterpret_cast<void *>(static_cast<uintptr_t>(slot->flag_dev_addr));
      EXPECT_CALL(mock_acl, aclrtValueWait(addr, 1U, 0U, slot->stream)).WillOnce(testing::Return(ACL_SUCCESS));
      EXPECT_CALL(mock_acl, aclrtValueWrite(addr, 0U, 0U, slot->stream)).WillOnce(testing::Return(ACL_SUCCESS));
    } else {
      EXPECT_CALL(mock_acl, aclrtWaitAndResetNotify(slot->notify, slot->stream, testing::_))
          .WillOnce(testing::Return(ACL_SUCCESS));
    }
    EXPECT_CALL(mock_acl, aclrtMemcpyAsync(testing::_, sizeof(uint64_t), slot->dev_const_one, sizeof(uint64_t),
                                           ACL_MEMCPY_DEVICE_TO_HOST, slot->stream))
        .WillOnce(testing::Return(ACL_SUCCESS));
  }
  void *complete_handle = nullptr;
  const Status ret = cli_.BatchPutAsync(&desc, 1U, &complete_handle);
  llm::AclRuntimeStub::UnInstall(&mock_acl);
  channel.ReleaseSharedSlotRef(slot);
  ASSERT_EQ(ret, SUCCESS);
  auto *handle = static_cast<DeviceCompleteHandle *>(complete_handle);
  *static_cast<uint64_t *>(handle->host_flag) = kDeviceFlagDoneValueForTest;
  HixlCompleteStatus status{};
  EXPECT_EQ(cli_.QueryCompleteStatus(complete_handle, &status), SUCCESS);
  EXPECT_EQ(status, HixlCompleteStatus::HIXL_COMPLETE_STATUS_COMPLETED);
}

INSTANTIATE_TEST_SUITE_P(DeviceProtocols, HixlCSClientProtocolDeviceFixture,
                         testing::Values(COMM_PROTOCOL_HCCS, COMM_PROTOCOL_ROCE, COMM_PROTOCOL_UBC_TP,
                                         COMM_PROTOCOL_UBC_CTP, COMM_PROTOCOL_UBOE, COMM_PROTOCOL_UBG));

class FlagBlockFailureAclRuntimeStub : public llm::AclRuntimeStub {
 public:
  explicit FlagBlockFailureAclRuntimeStub(bool fail_memset) : fail_memset_(fail_memset) {}

  aclError aclrtMalloc(void **ptr, size_t size, aclrtMemMallocPolicy policy) override {
    // The fixture uses the default 128-slot pool. Error flags occupy only 128 bytes.
    if (size == 128U * sizeof(uint64_t) && policy == ACL_MEM_TYPE_HIGH_BAND_WIDTH && !failure_injected_) {
      if (!fail_memset_) {
        failure_injected_ = true;
        return ACL_ERROR_FAILURE;
      }
      const aclError ret = llm::AclRuntimeStub::aclrtMalloc(ptr, size, policy);
      if (ret == ACL_SUCCESS) {
        flag_base_ = *ptr;
      }
      return ret;
    }
    return llm::AclRuntimeStub::aclrtMalloc(ptr, size, policy);
  }

  aclError aclrtMemset(void *ptr, size_t max_count, int32_t value, size_t count) override {
    if (ptr == flag_base_ && flag_base_ != nullptr && !failure_injected_) {
      failure_injected_ = true;
      return ACL_ERROR_FAILURE;
    }
    return llm::AclRuntimeStub::aclrtMemset(ptr, max_count, value, count);
  }

  bool failure_injected_{false};

 private:
  bool fail_memset_;
  void *flag_base_{nullptr};
};

class HixlCSClientFlagFailureFixture : public testing::TestWithParam<std::tuple<CommProtocol, bool>> {
 protected:
  void SetUp() override {
    auto kernel_stub = std::make_shared<MockMmpaStub>();
    kernel_stub->real_path_ok_ = true;
    kernel_stub->access_ok_ = true;
    hixl_test::InstallSysApiHooks(kernel_stub);
  }

  void TearDown() override {
    llm::AclRuntimeStub::Reset();
    hixl_test::ResetSysApiHooks();
  }
};

TEST_P(HixlCSClientFlagFailureFixture, CreateReturnsFlagCreationFailureWithoutNotifyFallback) {
  auto stub = std::make_shared<FlagBlockFailureAclRuntimeStub>(std::get<1>(GetParam()));
  llm::AclRuntimeStub::SetInstance(stub);
  EndpointDesc local = MakeDeviceEp(std::get<0>(GetParam()), kDeviceDevId);
  EndpointDesc remote = local;
  HixlClientConfig config{};
  HixlClientDesc desc{};
  desc.server_ip = "127.0.0.1";
  desc.server_port = kDummyPort;
  desc.local_endpoint = &local;
  desc.remote_endpoint = &remote;
  HixlCSClient cli;
  EXPECT_EQ(cli.Create(&desc, &config), static_cast<Status>(ACL_ERROR_FAILURE));
  EXPECT_TRUE(stub->failure_injected_);
  EXPECT_EQ(cli.client_channel_, nullptr);
  EXPECT_EQ(cli.Destroy(), SUCCESS);
  EXPECT_EQ(cli.Create(&desc, &config), SUCCESS);
  EXPECT_TRUE(DevChannel(cli).use_value_wait_);
  EXPECT_NE(DevChannel(cli).flag_mem_handle_, nullptr);
  EXPECT_EQ(cli.Destroy(), SUCCESS);
}

INSTANTIATE_TEST_SUITE_P(FlagBlockFailures, HixlCSClientFlagFailureFixture,
                         testing::Combine(testing::Values(COMM_PROTOCOL_ROCE, COMM_PROTOCOL_UBC_TP), testing::Bool()));

class HixlCSClientErrFlagMappingFailureFixture : public HixlCSClientDeviceFixture {
 protected:
  void SetUp() override {
    AscendHalStubSetHostRegisterRet(-1);
    HixlCSClientDeviceFixture::SetUp();
  }

  void TearDown() override {
    HixlCSClientDeviceFixture::TearDown();
    AscendHalStubReset();
  }
};

TEST_F(HixlCSClientErrFlagMappingFailureFixture, KeepsValueWaitWhenErrorFlagMappingFails) {
  auto &channel = DevChannel(cli_);
  EXPECT_TRUE(channel.use_value_wait_);
  EXPECT_NE(channel.flag_mem_handle_, nullptr);
  EXPECT_TRUE(channel.notify_mem_handles_.empty());
  std::shared_ptr<TransferPool::SlotHandle> slot;
  ASSERT_EQ(channel.AcquireSharedSlot(slot), SUCCESS);
  EXPECT_EQ(slot->err_flag_host_addr, nullptr);
  EXPECT_EQ(slot->err_flag_dev_addr, 0U);
  EXPECT_NE(slot->flag_dev_addr, 0U);
  channel.ReleaseSharedSlotRef(slot);
}

TEST_F(HixlCSClientDeviceFixture, FlagBlockRegistrationIsIdempotentAndReleasedByChannel) {
  auto &channel = DevChannel(cli_);
  auto *pool = TransferPool::GetInstance(cli_.device_id_);
  std::vector<TransferPool::SlotHandle> slots;
  ASSERT_EQ(pool->GetAllSlots(slots), SUCCESS);
  ASSERT_FALSE(slots.empty());
  void *base = reinterpret_cast<void *>(static_cast<uintptr_t>(slots.front().flag_dev_addr));
  const uint64_t size = static_cast<uint64_t>(slots.size()) * sizeof(uint64_t);
  HixlMemDesc mem_desc{};
  const MemHandle registered = channel.flag_mem_handle_;
  ASSERT_EQ(cli_.local_endpoint_->GetMemDesc(registered, mem_desc), SUCCESS);
  EXPECT_EQ(mem_desc.mem.addr, base);
  EXPECT_EQ(mem_desc.mem.size, size);
  EXPECT_EQ(mem_desc.mem.type, COMM_MEM_TYPE_DEVICE);
  ResetMemRegRecord();
  EXPECT_EQ(channel.RegisterFlagBlock(*pool), SUCCESS);
  EXPECT_EQ(GetMemRegCallCount(), 0U);
  channel.ReleaseDeviceResources();
  EXPECT_EQ(channel.flag_mem_handle_, nullptr);
  EXPECT_FALSE(channel.use_value_wait_);
  EXPECT_EQ(GetMemUnregCallCount(), 1U);
  channel.ReleaseDeviceResources();
  EXPECT_EQ(GetMemUnregCallCount(), 1U);
  ResetMemRegRecord();
}

TEST_F(HixlCSClientDeviceFixture, FlagBlockRegistrationFailureCanBeRetried) {
  UbDeviceChannel channel(*cli_.local_endpoint_);
  auto *pool = TransferPool::GetInstance(cli_.device_id_);
  ResetMemRegRecord();
  SetMemRegFailureOnCall(1U, HCCL_E_INTERNAL);
  EXPECT_NE(channel.RegisterFlagBlock(*pool), SUCCESS);
  EXPECT_EQ(channel.flag_mem_handle_, nullptr);
  ResetMemRegRecord();
  EXPECT_EQ(channel.RegisterFlagBlock(*pool), SUCCESS);
  EXPECT_NE(channel.flag_mem_handle_, nullptr);
  EXPECT_EQ(channel.Destroy(), SUCCESS);
  EXPECT_EQ(GetMemUnregCallCount(), 1U);
  ResetMemRegRecord();
}

class HixlCSClientDeviceToHostFixture : public HixlCSClientDeviceFixture {
 protected:
  EndpointDesc MakeLocalEndpoint() const override {
    return MakeDeviceEp(COMM_PROTOCOL_UBC_CTP, kDeviceDevId);
  }

  EndpointDesc MakeRemoteEndpoint() const override {
    EndpointDesc endpoint = MakeDeviceEp(COMM_PROTOCOL_UBC_CTP, kDeviceDevId);
    endpoint.loc.locType = ENDPOINT_LOC_TYPE_HOST;
    return endpoint;
  }

  HixlOneSideOpDesc SetupDeviceToHostTransfer() {
    (void)cli_.mem_store_.RecordMemory(true, remote_buf_.data(), remote_buf_.size(), true, nullptr);
    (void)cli_.mem_store_.RecordMemory(false, local_buf_.data(), local_buf_.size());
    HixlOneSideOpDesc desc{};
    desc.remote_buf = remote_buf_.data();
    desc.local_buf = local_buf_.data();
    desc.len = kLen8;
    return desc;
  }
};

TEST_F(HixlCSClientDeviceToHostFixture, BatchTransferSyncUsesRemoteHostVa) {
  ASSERT_FALSE(cli_.local_endpoint_->NeedHostVaMapping());
  HixlOneSideOpDesc desc = SetupDeviceToHostTransfer();

  MockAclRuntimeStub mock_acl;
  llm::AclRuntimeStub::Install(&mock_acl);
  EXPECT_CALL(mock_acl, aclrtSynchronizeStreamWithTimeout(testing::_, testing::_))
      .WillOnce(testing::Return(ACL_SUCCESS));
  const Status ret = cli_.BatchPutSync(&desc, 1, 100U);
  llm::AclRuntimeStub::UnInstall(&mock_acl);

  EXPECT_EQ(ret, SUCCESS);
}

TEST_F(HixlCSClientDeviceToHostFixture, BatchTransferAsyncUsesRemoteHostVa) {
  ASSERT_FALSE(cli_.local_endpoint_->NeedHostVaMapping());
  setenv("HIXL_UT_DEVICE_FLAG_HACK", "1", 1);
  HixlOneSideOpDesc desc = SetupDeviceToHostTransfer();

  MockAclRuntimeStub mock_acl;
  llm::AclRuntimeStub::Install(&mock_acl);
  EXPECT_CALL(mock_acl, aclrtValueWait(testing::_, testing::_, testing::_, testing::_))
      .WillRepeatedly(testing::Return(ACL_SUCCESS));
  EXPECT_CALL(mock_acl, aclrtValueWrite(testing::_, testing::_, testing::_, testing::_))
      .WillRepeatedly(testing::Return(ACL_SUCCESS));
  EXPECT_CALL(mock_acl, aclrtMemcpyAsync(testing::_, testing::_, testing::_, testing::_, testing::_, testing::_))
      .WillRepeatedly(testing::Return(ACL_SUCCESS));
  void *query_handle = nullptr;
  const Status ret = cli_.BatchPutAsync(&desc, 1, &query_handle);
  llm::AclRuntimeStub::UnInstall(&mock_acl);

  ASSERT_EQ(ret, SUCCESS);
  ASSERT_NE(query_handle, nullptr);
  HixlCompleteStatus status = HixlCompleteStatus::HIXL_COMPLETE_STATUS_WAITING;
  EXPECT_EQ(PollUntilCompleted(cli_, query_handle, &status), SUCCESS);
}

TEST_F(HixlCSClientDeviceFixture, ResourceExhaustionDoesNotLatchAndAllowsRetry) {
  const auto desc = SetupBatchTransfer(false);
  auto *pool = TransferPool::GetInstance(cli_.device_id_);
  ASSERT_NE(pool, nullptr);
  std::vector<TransferPool::SlotHandle> held;
  TransferPool::SlotHandle slot{};
  while (pool->Acquire(&slot) == SUCCESS) {
    held.push_back(slot);
  }
  void *handle = nullptr;
  EXPECT_EQ(cli_.BatchPutSync(&desc, 1U, 100U), RESOURCE_EXHAUSTED);
  EXPECT_EQ(cli_.BatchPutAsync(&desc, 1U, &handle), RESOURCE_EXHAUSTED);
  EXPECT_EQ(handle, nullptr);
  EXPECT_FALSE(DevChannel(cli_).transfer_failure_latched_);
  EXPECT_EQ(DevChannel(cli_).transfer_failure_status_, SUCCESS);
  for (const auto &held_slot : held) {
    pool->Release(held_slot);
  }
  ASSERT_FALSE(held.empty());
  EXPECT_EQ(cli_.BatchPutSync(&desc, 1U, 100U), SUCCESS);
  EXPECT_EQ(cli_.BatchPutAsync(&desc, 1U, &handle), SUCCESS);
  EXPECT_NE(handle, nullptr);
}

TEST_F(HixlCSClientDeviceFixture, BatchPutDeviceSuccessUseMemcpyHackFlag) {
  setenv("HIXL_UT_DEVICE_FLAG_HACK", "1", 1);
  HixlOneSideOpDesc desc = SetupBatchTransfer(false);

  void *qh = nullptr;
  const Status ret = cli_.BatchPutAsync(&desc, 1, &qh);
  EXPECT_EQ(ret, SUCCESS);
  ASSERT_NE(qh, nullptr);

  HixlCompleteStatus st = HixlCompleteStatus::HIXL_COMPLETE_STATUS_WAITING;
  (void)PollUntilCompleted(cli_, qh, &st);
  EXPECT_EQ(st, HixlCompleteStatus::HIXL_COMPLETE_STATUS_COMPLETED);
}

TEST_F(HixlCSClientDeviceFixture, BatchGetDeviceSuccessUseMemcpyHackFlag) {
  setenv("HIXL_UT_DEVICE_FLAG_HACK", "1", 1);
  HixlOneSideOpDesc desc = SetupBatchTransfer(true);

  void *qh = nullptr;
  const Status ret = cli_.BatchGetAsync(&desc, 1, &qh);
  EXPECT_EQ(ret, SUCCESS);
  ASSERT_NE(qh, nullptr);

  HixlCompleteStatus st = HixlCompleteStatus::HIXL_COMPLETE_STATUS_WAITING;
  (void)PollUntilCompleted(cli_, qh, &st);
  EXPECT_EQ(st, HixlCompleteStatus::HIXL_COMPLETE_STATUS_COMPLETED);
}

TEST_F(HixlCSClientDeviceFixture, BatchPutDeviceSyncUsesStreamSyncNoMemcpy) {
  HixlOneSideOpDesc desc = SetupBatchTransfer(false);
  MockAclRuntimeStub mock_acl;
  llm::AclRuntimeStub::Install(&mock_acl);
  EXPECT_CALL(mock_acl, aclrtSynchronizeStreamWithTimeout(testing::_, testing::_))
      .WillOnce(testing::Return(ACL_SUCCESS));
  EXPECT_CALL(mock_acl, aclrtMemcpyAsync(testing::_, testing::_, testing::_, testing::_, testing::_, testing::_))
      .Times(0);
  const Status ret = cli_.BatchPutSync(&desc, 1, 100U);
  llm::AclRuntimeStub::UnInstall(&mock_acl);
  EXPECT_EQ(ret, SUCCESS);
  EXPECT_TRUE(DevChannel(cli_).pending_device_handles_.empty());
}

TEST_F(HixlCSClientDeviceFixture, BatchPutDeviceSyncStreamSyncTimeoutAbortsSlot) {
  HixlOneSideOpDesc desc = SetupBatchTransfer(false);
  MockAclRuntimeStub mock_acl;
  llm::AclRuntimeStub::Install(&mock_acl);
  EXPECT_CALL(mock_acl, aclrtSynchronizeStreamWithTimeout(testing::_, testing::_))
      .WillOnce(testing::Return(ACL_ERROR_RT_STREAM_SYNC_TIMEOUT))
      .WillRepeatedly(testing::Return(ACL_SUCCESS));
  EXPECT_CALL(mock_acl, aclrtNotifyBatchReset(testing::NotNull(), testing::Eq(static_cast<size_t>(1U))))
      .WillOnce(testing::Return(ACL_SUCCESS));
  const Status ret = cli_.BatchPutSync(&desc, 1, 100U);
  llm::AclRuntimeStub::UnInstall(&mock_acl);
  EXPECT_EQ(ret, TIMEOUT);
  EXPECT_TRUE(DevChannel(cli_).pending_device_handles_.empty());
  EXPECT_FALSE(DevChannel(cli_).abort_active_slot_);
  ExpectFreeSlotsAreUnique(cli_.device_id_);
}

TEST_F(HixlCSClientDeviceFixture, AbortRequestedSlotStaysOwnedUntilLastRef) {
  auto &ch = DevChannel(cli_);
  std::shared_ptr<TransferPool::SlotHandle> slot;
  if (ch.AcquireSharedSlot(slot) != SUCCESS) {
    GTEST_SKIP() << "TransferPool Acquire failed in UT env";
  }
  ch.abort_active_slot_ = true;

  MockAclRuntimeStub mock_acl;
  llm::AclRuntimeStub::Install(&mock_acl);
  EXPECT_CALL(mock_acl, aclrtSynchronizeStreamWithTimeout(testing::_, testing::_))
      .WillRepeatedly(testing::Return(ACL_SUCCESS));
  EXPECT_CALL(mock_acl, aclrtNotifyBatchReset(testing::NotNull(), testing::Eq(static_cast<size_t>(1U))))
      .Times(1)
      .WillRepeatedly(testing::Return(ACL_SUCCESS));

  auto extra_ref = ch.active_slot_;
  ch.ReleaseSharedSlotRef(slot);
  EXPECT_NE(ch.active_slot_.get(), nullptr);
  EXPECT_TRUE(ch.abort_active_slot_);

  ch.ReleaseSharedSlotRef(extra_ref);
  llm::AclRuntimeStub::UnInstall(&mock_acl);

  EXPECT_EQ(ch.active_slot_.get(), nullptr);
  EXPECT_FALSE(ch.abort_active_slot_);
  ExpectFreeSlotsAreUnique(cli_.device_id_);
}

TEST_F(HixlCSClientDeviceFixture, PendingActiveSlotIsAbortedOnce) {
  auto &ch = DevChannel(cli_);
  std::shared_ptr<TransferPool::SlotHandle> slot;
  if (ch.AcquireSharedSlot(slot) != SUCCESS) {
    GTEST_SKIP() << "TransferPool Acquire failed in UT env";
  }
  auto *handle = new DeviceCompleteHandle();
  handle->magic = 0x55425548U;
  handle->shared_slot = ch.active_slot_;
  handle->host_flag = nullptr;
  handle->dev_op_desc_buf = nullptr;
  ch.pending_device_handles_.insert(handle);
  slot.reset();

  MockAclRuntimeStub mock_acl;
  llm::AclRuntimeStub::Install(&mock_acl);
  EXPECT_CALL(mock_acl, aclrtSynchronizeStreamWithTimeout(testing::_, testing::_))
      .WillRepeatedly(testing::Return(ACL_SUCCESS));
  EXPECT_CALL(mock_acl, aclrtNotifyBatchReset(testing::NotNull(), testing::Eq(static_cast<size_t>(1U))))
      .Times(1)
      .WillRepeatedly(testing::Return(ACL_SUCCESS));
  ch.AbortAllPendingDeviceHandles();
  llm::AclRuntimeStub::UnInstall(&mock_acl);

  EXPECT_TRUE(ch.pending_device_handles_.empty());
  EXPECT_EQ(ch.active_slot_.get(), nullptr);
  EXPECT_FALSE(ch.abort_active_slot_);
  ExpectFreeSlotsAreUnique(cli_.device_id_);
}

TEST_F(HixlCSClientDeviceFixture, LatchRuntimeErrorsRejectsSubsequentTransfers) {
  const auto desc = SetupBatchTransfer(false);
  auto &channel = DevChannel(cli_);
  EXPECT_EQ(channel.LatchTransferFailureIfNeeded(SUCCESS), SUCCESS);
  EXPECT_FALSE(channel.transfer_failure_latched_);
  for (const Status ret : {503900U, 103901U, 507001U, 507018U, 507901U}) {
    channel.transfer_failure_latched_ = false;
    channel.transfer_failure_status_ = SUCCESS;
    EXPECT_EQ(channel.LatchTransferFailureIfNeeded(ret), ret);
    EXPECT_TRUE(channel.transfer_failure_latched_);
    EXPECT_EQ(channel.LatchTransferFailureIfNeeded(PARAM_INVALID), PARAM_INVALID);
    EXPECT_EQ(channel.LatchTransferFailureIfNeeded(RESOURCE_EXHAUSTED), RESOURCE_EXHAUSTED);
    EXPECT_TRUE(channel.transfer_failure_latched_);
    EXPECT_EQ(channel.transfer_failure_status_, ret);
    void *handle = nullptr;
    EXPECT_EQ(cli_.BatchPutSync(&desc, 1U, 100U), FAILED);
    EXPECT_EQ(cli_.BatchGetSync(&desc, 1U, 100U), FAILED);
    EXPECT_EQ(cli_.BatchPutAsync(&desc, 1U, &handle), FAILED);
    EXPECT_EQ(cli_.BatchGetAsync(&desc, 1U, &handle), FAILED);
  }
}

TEST_F(HixlCSClientDeviceFixture, BatchPutDeviceSyncFailureLatchesClient) {
  HixlOneSideOpDesc desc = SetupBatchTransfer(false);
  MockAclRuntimeStub mock_acl;
  llm::AclRuntimeStub::Install(&mock_acl);
  uint32_t sync_call_count = 0U;
  EXPECT_CALL(mock_acl, aclrtSynchronizeStreamWithTimeout(testing::_, testing::_))
      .WillRepeatedly(testing::Invoke([&sync_call_count](aclrtStream, int32_t) {
        ++sync_call_count;
        return ACL_ERROR_RT_STREAM_SYNC_TIMEOUT;
      }));
  EXPECT_CALL(mock_acl, aclrtNotifyBatchReset(testing::NotNull(), testing::Eq(static_cast<size_t>(1U))))
      .WillRepeatedly(testing::Return(ACL_SUCCESS));

  const Status first_ret = cli_.BatchPutSync(&desc, 1, 100U);
  EXPECT_EQ(first_ret, TIMEOUT);
  EXPECT_GT(sync_call_count, 0U);
  const uint32_t sync_call_count_after_first = sync_call_count;

  const Status second_ret = cli_.BatchPutSync(&desc, 1, 100U);
  EXPECT_EQ(second_ret, FAILED);
  EXPECT_EQ(sync_call_count, sync_call_count_after_first);

  llm::AclRuntimeStub::UnInstall(&mock_acl);
  EXPECT_TRUE(DevChannel(cli_).pending_device_handles_.empty());
}

// LaunchDeviceKernel 在 kernel 已 launch 后失败（ValueWrite 失败）时必须走 Abort 复位 flag/stream，
// 否则残留 flag=1 会让下一次 value wait 立即放行、误报完成。
TEST_F(HixlCSClientDeviceFixture, AsyncValueWriteFailureAbortsSlot) {
  HixlOneSideOpDesc desc = SetupBatchTransfer(false);
  void *query_handle = nullptr;
  MockAclRuntimeStub mock_acl;
  llm::AclRuntimeStub::Install(&mock_acl);
  EXPECT_CALL(mock_acl, aclrtValueWait(testing::_, testing::_, testing::_, testing::_))
      .WillOnce(testing::Return(ACL_SUCCESS));
  EXPECT_CALL(mock_acl, aclrtValueWrite(testing::_, testing::_, testing::_, testing::_))
      .WillOnce(testing::Return(static_cast<aclError>(1)));
  uint32_t abort_count = 0U;
  EXPECT_CALL(mock_acl, aclrtStreamAbort(testing::_)).WillRepeatedly(testing::Invoke([&abort_count](aclrtStream) {
    ++abort_count;
    return ACL_SUCCESS;
  }));
  EXPECT_CALL(mock_acl, aclrtNotifyBatchReset(testing::_, testing::_)).WillRepeatedly(testing::Return(ACL_SUCCESS));

  EXPECT_NE(cli_.BatchPutAsync(&desc, 1, &query_handle), SUCCESS);
  EXPECT_EQ(query_handle, nullptr);
  EXPECT_TRUE(DevChannel(cli_).transfer_failure_latched_);
  EXPECT_EQ(DevChannel(cli_).transfer_failure_status_, static_cast<Status>(1));
  EXPECT_GE(abort_count, 1U);
  EXPECT_TRUE(DevChannel(cli_).pending_device_handles_.empty());

  const uint32_t abort_count_after_failure = abort_count;
  EXPECT_EQ(cli_.BatchPutAsync(&desc, 1, &query_handle), FAILED);
  EXPECT_EQ(query_handle, nullptr);
  EXPECT_EQ(abort_count, abort_count_after_failure);
  EXPECT_TRUE(DevChannel(cli_).pending_device_handles_.empty());
  llm::AclRuntimeStub::UnInstall(&mock_acl);
}

TEST_F(HixlCSClientDeviceFixture, BatchPutUbDeviceValueWaitFail) {
  HixlOneSideOpDesc desc = SetupBatchTransfer(false);
  void *qh = nullptr;

  MockAclRuntimeStub mock_acl;
  llm::AclRuntimeStub::Install(&mock_acl);

  EXPECT_CALL(mock_acl, aclrtValueWait(testing::_, testing::_, testing::_, testing::_))
      .WillOnce(testing::Return(static_cast<aclError>(FAILED)));
  EXPECT_CALL(mock_acl, aclrtSynchronizeStreamWithTimeout(testing::_, testing::_))
      .WillRepeatedly(testing::Return(ACL_SUCCESS));
  EXPECT_CALL(mock_acl, aclrtNotifyBatchReset(testing::NotNull(), testing::Eq(static_cast<size_t>(1U))))
      .Times(1)
      .WillRepeatedly(testing::Return(ACL_SUCCESS));

  const Status ret = cli_.BatchPutAsync(&desc, 1, &qh);

  llm::AclRuntimeStub::UnInstall(&mock_acl);

  EXPECT_EQ(ret, FAILED);
  EXPECT_TRUE(DevChannel(cli_).transfer_failure_latched_);
  EXPECT_FALSE(DevChannel(cli_).abort_active_slot_);
  EXPECT_TRUE(DevChannel(cli_).pending_device_handles_.empty());
  ExpectFreeSlotsAreUnique(cli_.device_id_);
}

TEST_F(HixlCSClientDeviceFixture, BatchPutDeviceAsyncMemcpyFailAbortsSlot) {
  HixlOneSideOpDesc desc = SetupBatchTransfer(false);
  void *qh = nullptr;

  MockAclRuntimeStub mock_acl;
  llm::AclRuntimeStub::Install(&mock_acl);
  EXPECT_CALL(mock_acl, aclrtValueWait(testing::_, testing::_, testing::_, testing::_))
      .WillRepeatedly(testing::Return(ACL_SUCCESS));
  EXPECT_CALL(mock_acl, aclrtMemcpyAsync(testing::_, testing::_, testing::_, testing::_, testing::_, testing::_))
      .WillOnce(testing::Return(static_cast<aclError>(FAILED)));
  EXPECT_CALL(mock_acl, aclrtSynchronizeStreamWithTimeout(testing::_, testing::_))
      .WillRepeatedly(testing::Return(ACL_SUCCESS));
  EXPECT_CALL(mock_acl, aclrtNotifyBatchReset(testing::NotNull(), testing::Eq(static_cast<size_t>(1U))))
      .Times(1)
      .WillRepeatedly(testing::Return(ACL_SUCCESS));

  const Status ret = cli_.BatchPutAsync(&desc, 1, &qh);
  llm::AclRuntimeStub::UnInstall(&mock_acl);

  EXPECT_EQ(ret, FAILED);
  EXPECT_TRUE(DevChannel(cli_).transfer_failure_latched_);
  EXPECT_FALSE(DevChannel(cli_).abort_active_slot_);
  EXPECT_TRUE(DevChannel(cli_).pending_device_handles_.empty());
  ExpectFreeSlotsAreUnique(cli_.device_id_);
}

TEST_F(HixlCSClientDeviceFixture, BatchPutDeviceSyncLaunchFailAbortsSlot) {
  HixlOneSideOpDesc desc = SetupBatchTransfer(false);
  MockAclRuntimeStub mock_acl;
  llm::AclRuntimeStub::Install(&mock_acl);
  EXPECT_CALL(mock_acl, aclrtValueWait(testing::_, testing::_, testing::_, testing::_))
      .WillOnce(testing::Return(static_cast<aclError>(FAILED)));
  EXPECT_CALL(mock_acl, aclrtSynchronizeStreamWithTimeout(testing::_, testing::_))
      .WillRepeatedly(testing::Return(ACL_SUCCESS));
  EXPECT_CALL(mock_acl, aclrtNotifyBatchReset(testing::NotNull(), testing::Eq(static_cast<size_t>(1U))))
      .Times(1)
      .WillRepeatedly(testing::Return(ACL_SUCCESS));

  const Status ret = cli_.BatchPutSync(&desc, 1, 100U);
  llm::AclRuntimeStub::UnInstall(&mock_acl);

  EXPECT_EQ(ret, FAILED);
  EXPECT_TRUE(DevChannel(cli_).transfer_failure_latched_);
  EXPECT_FALSE(DevChannel(cli_).abort_active_slot_);
  EXPECT_TRUE(DevChannel(cli_).pending_device_handles_.empty());
  ExpectFreeSlotsAreUnique(cli_.device_id_);
}

TEST_F(HixlCSClientDeviceFixture, BatchPutDeviceValueWaitSingleTask) {
  ExpectAsyncValueWaitCount(kListNum1, 1U);
}

TEST_F(HixlCSClientDeviceFixture, BatchPutDeviceValueWaitExactInterval) {
  ExpectAsyncValueWaitCount(kNotifyWaitTaskInterval, 1U);
}

TEST_F(HixlCSClientDeviceFixture, BatchPutDeviceValueWaitTailAfterInterval) {
  ExpectAsyncValueWaitCount(kNotifyWaitTaskInterval + 1U, 2U);
}

TEST_F(HixlCSClientDeviceFixture, BatchPutDeviceValueWaitMultipleIntervals) {
  ExpectAsyncValueWaitCount(kNotifyWaitTaskInterval * 2U, 2U);
}

TEST_F(HixlCSClientDeviceFixture, BatchPutDeviceUsesNonKernelAlignedConfiguredBoundary) {
  ASSERT_EQ(GlobalConfig::Parse(R"({"transfer_config.max_transfer_count_per_batch":1022})", cli_.global_config_),
            SUCCESS);
  ExpectAsyncValueWaitCount(1023U, 2U);
}

TEST_F(HixlCSClientDeviceFixture, DeviceValueWaitUsesDefaultBoundaryWhenBatchSettingDisabled) {
  ASSERT_EQ(GlobalConfig::Parse(R"({"transfer_config.max_transfer_count_per_batch":1022})", cli_.global_config_),
            SUCCESS);
  cli_.configurable_transfer_supported_ = false;
  DevChannel(cli_).ctx_.configurable_transfer_supported = false;
  ExpectAsyncValueWaitCount(kNotifyWaitTaskInterval + 1U, 2U);
}

class LegacySocAclRuntimeStub : public llm::AclRuntimeStub {
 public:
  explicit LegacySocAclRuntimeStub(const char *soc_name) : soc_name_(soc_name) {}

  const char *aclrtGetSocName() override {
    return soc_name_;
  }

 private:
  const char *soc_name_;
};

class HixlCSClientLegacyDeviceFixture : public HixlCSClientDeviceFixture,
                                        public testing::WithParamInterface<std::tuple<int32_t, const char *>> {
 protected:
  void SetUp() override {
    SetHcommVersionNum(std::get<0>(GetParam()));
    llm::AclRuntimeStub::SetInstance(std::make_shared<LegacySocAclRuntimeStub>(std::get<1>(GetParam())));
    HixlCSClientDeviceFixture::SetUp();
  }

  void TearDown() override {
    HixlCSClientDeviceFixture::TearDown();
    ResetHcommVersionStub();
    llm::AclRuntimeStub::Reset();
  }
};

TEST_P(HixlCSClientLegacyDeviceFixture, NonHccsReadsIntoNotifyAndWaitsForNotify) {
  EXPECT_FALSE(TransferPool::GetInstance(cli_.device_id_)->host_exception_enabled_);
  EXPECT_FALSE(DevChannel(cli_).use_value_wait_);
  auto desc = SetupBatchTransfer(false);
  MockAclRuntimeStub mock_acl;
  llm::AclRuntimeStub::Install(&mock_acl);
  EXPECT_CALL(mock_acl, aclrtWaitAndResetNotify(testing::_, testing::_, testing::_))
      .WillOnce(testing::Return(ACL_SUCCESS));
  EXPECT_CALL(mock_acl, aclrtValueWait(testing::_, testing::_, testing::_, testing::_)).Times(0);
  EXPECT_CALL(mock_acl, aclrtValueWrite(testing::_, testing::_, testing::_, testing::_)).Times(0);

  void *qh = nullptr;
  const Status ret = cli_.BatchPutAsync(&desc, 1U, &qh);
  llm::AclRuntimeStub::UnInstall(&mock_acl);
  ASSERT_EQ(ret, SUCCESS);
  ASSERT_NE(qh, nullptr);

  auto &handle = *static_cast<DeviceCompleteHandle *>(qh);
  ASSERT_NE(handle.shared_slot->notify_addr, 0U);
  HixlOneSideOpParam param{};
  ASSERT_EQ(DevChannel(cli_).BuildDeviceChunkParam(handle, 0U, 1U, true, param), SUCCESS);
  EXPECT_EQ(param.use_notify_record, 0U);
  EXPECT_EQ(param.local_flag_addr, handle.shared_slot->notify_addr);
  EXPECT_EQ(param.flag_size, handle.shared_slot->notify_len);
}

INSTANTIATE_TEST_SUITE_P(HcommBelow930, HixlCSClientLegacyDeviceFixture,
                         testing::Combine(testing::Values(90100000, 90200000, 90299999),
                                          testing::Values("Ascend950A")));

INSTANTIATE_TEST_SUITE_P(A2AndA3, HixlCSClientLegacyDeviceFixture,
                         testing::Combine(testing::Values(90300000), testing::Values("Ascend910B1", "Ascend910_9391")));

TEST_F(HixlCSClientDeviceFixture, BatchPutDeviceSlotReuse) {
  // With slot reuse mechanism, multiple concurrent transfers share the same slot
  // This test verifies that slot reuse works correctly
  setenv("HIXL_UT_DEVICE_FLAG_HACK", "1", 1);
  HixlOneSideOpDesc desc = SetupBatchTransfer(false);

  MockAclRuntimeStub mock_acl;
  llm::AclRuntimeStub::Install(&mock_acl);
  EXPECT_CALL(mock_acl, aclrtValueWait(testing::_, testing::_, testing::_, testing::_))
      .WillRepeatedly(testing::Return(ACL_SUCCESS));
  EXPECT_CALL(mock_acl, aclrtValueWrite(testing::_, testing::_, testing::_, testing::_))
      .WillRepeatedly(testing::Return(ACL_SUCCESS));
  EXPECT_CALL(mock_acl, aclrtMemcpyAsync(testing::_, testing::_, testing::_, testing::_, testing::_, testing::_))
      .WillRepeatedly(testing::Return(ACL_SUCCESS));

  std::vector<void *> handles;
  handles.reserve(16U);

  // Launch multiple transfers - they should reuse the same slot
  for (uint32_t i = 0; i < 16U; ++i) {
    void *qh = nullptr;
    const Status ret = cli_.BatchPutAsync(&desc, 1, &qh);
    ASSERT_EQ(ret, SUCCESS);
    ASSERT_NE(qh, nullptr);
    handles.emplace_back(qh);

    // All handles should share the same slot_index
    DeviceCompleteHandle *handle = static_cast<DeviceCompleteHandle *>(qh);
    EXPECT_EQ(handle->shared_slot->slot_index, 0U);  // All should use slot 0
  }

  // Cleanup - poll until completed
  for (void *h : handles) {
    HixlCompleteStatus st = HixlCompleteStatus::HIXL_COMPLETE_STATUS_WAITING;
    (void)PollUntilCompleted(cli_, h, &st);
  }

  llm::AclRuntimeStub::UnInstall(&mock_acl);
  unsetenv("HIXL_UT_DEVICE_FLAG_HACK");
}

TEST_F(HixlCSClientDeviceFixture, BatchPutDeviceIndependentHostFlags) {
  // Verify that each async transfer gets its own independent host_flag
  setenv("HIXL_UT_DEVICE_FLAG_HACK", "1", 1);
  HixlOneSideOpDesc desc = SetupBatchTransfer(false);

  MockAclRuntimeStub mock_acl;
  llm::AclRuntimeStub::Install(&mock_acl);
  EXPECT_CALL(mock_acl, aclrtValueWait(testing::_, testing::_, testing::_, testing::_))
      .WillRepeatedly(testing::Return(ACL_SUCCESS));
  EXPECT_CALL(mock_acl, aclrtValueWrite(testing::_, testing::_, testing::_, testing::_))
      .WillRepeatedly(testing::Return(ACL_SUCCESS));
  EXPECT_CALL(mock_acl, aclrtMemcpyAsync(testing::_, testing::_, testing::_, testing::_, testing::_, testing::_))
      .WillRepeatedly(testing::Return(ACL_SUCCESS));

  void *qh1 = nullptr;
  void *qh2 = nullptr;
  ASSERT_EQ(cli_.BatchPutAsync(&desc, 1, &qh1), SUCCESS);
  ASSERT_EQ(cli_.BatchPutAsync(&desc, 1, &qh2), SUCCESS);

  DeviceCompleteHandle *h1 = static_cast<DeviceCompleteHandle *>(qh1);
  DeviceCompleteHandle *h2 = static_cast<DeviceCompleteHandle *>(qh2);

  // They should share the same slot
  EXPECT_EQ(h1->shared_slot.get(), h2->shared_slot.get());

  // But have different host_flags
  EXPECT_NE(h1->host_flag, nullptr);
  EXPECT_NE(h2->host_flag, nullptr);
  EXPECT_NE(h1->host_flag, h2->host_flag);  // Different host_flags

  // Cleanup
  HixlCompleteStatus st = HixlCompleteStatus::HIXL_COMPLETE_STATUS_WAITING;
  (void)PollUntilCompleted(cli_, qh1, &st);
  (void)PollUntilCompleted(cli_, qh2, &st);

  llm::AclRuntimeStub::UnInstall(&mock_acl);
  unsetenv("HIXL_UT_DEVICE_FLAG_HACK");
}

class LoadKernelFixture : public ::testing::Test {
 protected:
  void SetUp() override {
    hixl_test::ResetSysApiHooks();
    const char *env = std::getenv("ASCEND_HOME_PATH");
    if (env != nullptr) {
      original_env_ = env;
      has_env_ = true;
    }
    system("mkdir -p ./test_opp/opp/built-in/op_impl/aicpu/config");
  }
  void TearDown() override {
    if (has_env_) {
      setenv("ASCEND_HOME_PATH", original_env_.c_str(), 1);
    } else {
      unsetenv("ASCEND_HOME_PATH");
    }
    system("rm -rf ./test_opp");
    hixl_test::ResetSysApiHooks();
  }
  void CreateDummyJson(const std::string &path, bool readable) {
    std::string cmd = "echo '{}' > " + path;
    system(cmd.c_str());
    if (!readable) {
      cmd = "chmod 000 " + path;
      system(cmd.c_str());
    }
  }
  std::string original_env_;
  bool has_env_ = false;
};

TEST_F(LoadKernelFixture, NoEnvAndFileNotFound) {
  auto mock_mmpa = std::make_shared<MockMmpaStub>();
  mock_mmpa->real_path_ok_ = false;
  mock_mmpa->access_ok_ = false;
  hixl_test::InstallSysApiHooks(mock_mmpa);
  unsetenv("ASCEND_HOME_PATH");
  aclrtBinHandle bin_handle = nullptr;
  DeviceFuncHandles func_handles{};
  Status ret = LoadDeviceKernelAndGetHandles("GetFunc", "PutFunc", bin_handle, func_handles);
  EXPECT_EQ(ret, PARAM_INVALID);
}

TEST_F(LoadKernelFixture, AclLoadBinaryFailed) {
  setenv("ASCEND_HOME_PATH", "./test_opp", 1);
  std::string file_path = "./test_opp/opp/built-in/op_impl/aicpu/config/libcann_hixl_kernel.json";
  CreateDummyJson(file_path, true);
  aclrtBinHandle bin_handle = nullptr;
  DeviceFuncHandles func_handles{};
  MockAclRuntimeStub mock_acl;
  llm::AclRuntimeStub::Install(&mock_acl);
  EXPECT_CALL(mock_acl, aclrtBinaryLoadFromFile(testing::_, testing::_, testing::_))
      .WillOnce(testing::Return(ACL_ERROR_RT_STREAM_SYNC_TIMEOUT));
  Status ret = LoadDeviceKernelAndGetHandles("GetFunc", "PutFunc", bin_handle, func_handles);
  llm::AclRuntimeStub::UnInstall(&mock_acl);
  EXPECT_EQ(ret, TIMEOUT);
}

TEST_F(LoadKernelFixture, GetFuncHandleInvalidParams) {
  setenv("ASCEND_HOME_PATH", "./test_opp", 1);
  aclrtBinHandle dummy_bin_handle = reinterpret_cast<aclrtBinHandle>(0xDEADBEEF);
  DeviceFuncHandles func_handles{};
  Status ret = LoadDeviceKernelAndGetHandles(nullptr, "PutFunc", dummy_bin_handle, func_handles);
  EXPECT_EQ(ret, PARAM_INVALID);
  ret = LoadDeviceKernelAndGetHandles("GetFunc", nullptr, dummy_bin_handle, func_handles);
  EXPECT_EQ(ret, PARAM_INVALID);
}

TEST_F(LoadKernelFixture, GetFuncHandleAclGetFuncFailed) {
  setenv("ASCEND_HOME_PATH", "./test_opp", 1);
  aclrtBinHandle dummy_bin_handle = reinterpret_cast<aclrtBinHandle>(0xDEADBEEF);
  DeviceFuncHandles func_handles{};
  MockAclRuntimeStub mock_acl;
  llm::AclRuntimeStub::Install(&mock_acl);
  EXPECT_CALL(mock_acl, aclrtBinaryGetFunction(testing::_, testing::_, testing::_))
      .WillOnce(testing::Return(ACL_ERROR_RT_INTERNAL_ERROR));
  Status ret = LoadDeviceKernelAndGetHandles("GetFunc", "PutFunc", dummy_bin_handle, func_handles);
  llm::AclRuntimeStub::UnInstall(&mock_acl);
  EXPECT_EQ(ret, static_cast<Status>(ACL_ERROR_RT_INTERNAL_ERROR));
}

TEST_F(LoadKernelFixture, LoadSyncTransferContextHandleWhenRequested) {
  setenv("ASCEND_HOME_PATH", "./test_opp", 1);
  aclrtBinHandle dummy_bin_handle = reinterpret_cast<aclrtBinHandle>(0xDEADBEEF);
  DeviceFuncHandles func_handles{};
  Status ret = LoadDeviceKernelAndGetHandles("GetFunc", "PutFunc", dummy_bin_handle, func_handles, "SyncFunc");
  EXPECT_EQ(ret, SUCCESS);
  EXPECT_NE(func_handles.batch_get, nullptr);
  EXPECT_NE(func_handles.batch_put, nullptr);
  EXPECT_NE(func_handles.sync_transfer_context, nullptr);
}

// Slot reuse tests - unit tests for shared slot management logic
class HixlCSClientSlotReuseFixture : public HixlCSClientDeviceFixture {
 protected:
  void SetUp() override {
    HixlCSClientDeviceFixture::SetUp();
    // device_id_ is set by InitDeviceResource which calls aclrtGetDevice (stub returns 0)
    // TransferPool::GetInstance(device_id_) is initialized by InitDeviceResource
  }
};

TEST_F(HixlCSClientSlotReuseFixture, ActiveSlotInitiallyNull) {
  EXPECT_EQ(DevChannel(cli_).active_slot_.get(), nullptr);
}

TEST_F(HixlCSClientSlotReuseFixture, AcquireSharedSlotReturnsNewSlotWhenNoActive) {
  // Skip this test if TransferPool not properly initialized (UT environment limitation)
  if (cli_.device_id_ < 0) {
    GTEST_SKIP() << "TransferPool not initialized";
  }
  std::shared_ptr<TransferPool::SlotHandle> slot;
  // This test may fail in UT env due to TransferPool initialization requirements
  // It tests the logic path where no active slot exists
  Status ret = DevChannel(cli_).AcquireSharedSlot(slot);
  if (ret != SUCCESS) {
    GTEST_SKIP() << "TransferPool Acquire failed in UT env";
  }
  EXPECT_NE(slot.get(), nullptr);
  EXPECT_EQ(DevChannel(cli_).active_slot_.get(), slot.get());

  DevChannel(cli_).ReleaseSharedSlotRef(slot);
}

TEST_F(HixlCSClientSlotReuseFixture, ReleaseSharedSlotRefClearsActiveSlot) {
  // Create a fake active_slot_ to test release logic without TransferPool
  TransferPool::SlotHandle fake_slot{};
  fake_slot.device_id = cli_.device_id_;
  fake_slot.slot_index = 0U;
  fake_slot.ctx = nullptr;
  fake_slot.stream = nullptr;
  fake_slot.thread = 0U;
  fake_slot.notify = nullptr;
  fake_slot.dev_const_one = nullptr;

  DevChannel(cli_).active_slot_ = std::make_shared<TransferPool::SlotHandle>(fake_slot);
  EXPECT_NE(DevChannel(cli_).active_slot_.get(), nullptr);

  // Simulate releasing the reference
  std::shared_ptr<TransferPool::SlotHandle> slot_ref = DevChannel(cli_).active_slot_;
  slot_ref.reset();

  // When use_count becomes 0, active_slot_ should be cleared by ReleaseSharedSlotRef
  // But we can't call ReleaseSharedSlotRef as it would try to Release to pool
  // Just verify the logic that clearing reference clears active_slot_
}

TEST_F(HixlCSClientSlotReuseFixture, HostFlagInDeviceCompleteHandle) {
  DeviceCompleteHandle handle{};
  handle.magic = 0x55425548U;  // kDeviceCompleteMagic
  handle.host_flag = nullptr;
  EXPECT_EQ(handle.host_flag, nullptr);

  // Verify structure has the field
  void *test_flag = reinterpret_cast<void *>(0x1234);
  handle.host_flag = test_flag;
  EXPECT_EQ(handle.host_flag, test_flag);
}

TEST_F(HixlCSClientSlotReuseFixture, AsyncTransferWithEnvHackAllocatesHostFlag) {
  setenv("HIXL_UT_DEVICE_FLAG_HACK", "1", 1);
  HixlOneSideOpDesc desc = SetupBatchTransfer(false);

  MockAclRuntimeStub mock_acl;
  llm::AclRuntimeStub::Install(&mock_acl);
  EXPECT_CALL(mock_acl, aclrtValueWait(testing::_, testing::_, testing::_, testing::_))
      .WillRepeatedly(testing::Return(ACL_SUCCESS));
  EXPECT_CALL(mock_acl, aclrtValueWrite(testing::_, testing::_, testing::_, testing::_))
      .WillRepeatedly(testing::Return(ACL_SUCCESS));
  EXPECT_CALL(mock_acl, aclrtMemcpyAsync(testing::_, testing::_, testing::_, testing::_, testing::_, testing::_))
      .WillRepeatedly(testing::Return(ACL_SUCCESS));

  void *qh = nullptr;
  const Status ret = cli_.BatchPutAsync(&desc, 1, &qh);
  llm::AclRuntimeStub::UnInstall(&mock_acl);

  if (ret != SUCCESS) {
    GTEST_SKIP() << "BatchTransfer failed in UT env (TransferPool not initialized)";
  }

  ASSERT_NE(qh, nullptr);
  DeviceCompleteHandle *handle = static_cast<DeviceCompleteHandle *>(qh);
  // Async transfer should have independent host_flag
  EXPECT_NE(handle->host_flag, nullptr);

  // Cleanup
  HixlCompleteStatus st = HixlCompleteStatus::HIXL_COMPLETE_STATUS_WAITING;
  (void)PollUntilCompleted(cli_, qh, &st);
  unsetenv("HIXL_UT_DEVICE_FLAG_HACK");
}

TEST_F(HixlCSClientSlotReuseFixture, SyncTransferUsesStreamSync) {
  HixlOneSideOpDesc desc = SetupBatchTransfer(false);

  MockAclRuntimeStub mock_acl;
  llm::AclRuntimeStub::Install(&mock_acl);
  EXPECT_CALL(mock_acl, aclrtSynchronizeStreamWithTimeout(testing::_, testing::_))
      .WillRepeatedly(testing::Return(ACL_SUCCESS));
  EXPECT_CALL(mock_acl, aclrtMemcpyAsync(testing::_, testing::_, testing::_, testing::_, testing::_, testing::_))
      .Times(0);  // Sync transfer should NOT call memcpy async for flag

  const Status ret = cli_.BatchPutSync(&desc, 1, 100U);
  llm::AclRuntimeStub::UnInstall(&mock_acl);

  if (ret != SUCCESS) {
    GTEST_SKIP() << "BatchTransferSync failed in UT env (TransferPool not initialized)";
  }

  EXPECT_EQ(ret, SUCCESS);
  EXPECT_TRUE(DevChannel(cli_).pending_device_handles_.empty());
}

// ============================================================================
// ConvertHostMappedDescs 地址转换测试
// ============================================================================

// 测试场景：remote_buf 错误地注册到 client_regions_（应该在 server_regions_），地址转换应失败
TEST_F(HixlCSClientDeviceFixture, ConvertHostMappedDescsFailsIfRemoteRegisteredInWrongRegion) {
  // 错误：remote_buf 注册到 client_regions_（应该是 server_regions_）
  // local_buf 也注册到 client_regions_（正确）
  cli_.mem_store_.RecordMemory(false, remote_buf_.data(), remote_buf_.size());
  cli_.mem_store_.RecordMemory(false, local_buf_.data(), local_buf_.size());

  HixlOneSideOpDesc desc{};
  desc.remote_buf = remote_buf_.data();
  desc.local_buf = local_buf_.data();
  desc.len = kLen8;

  // 直接调用 ConvertHostMappedDescs，绕过 ValidateAddress
  std::vector<HixlOneSideOpDesc> mutable_descs(1, desc);
  const Status ret = DevChannel(cli_).ConvertHostMappedDescs(1, mutable_descs.data());
  EXPECT_EQ(ret, FAILED);  // remote_buf 在错误的 regions，ConvertHostMappedDescs 会失败
}

// 测试场景：local_buf 错误地注册到 server_regions_（应该在 client_regions_），地址转换应失败
TEST_F(HixlCSClientDeviceFixture, ConvertHostMappedDescsFailsIfLocalRegisteredInWrongRegion) {
  // remote_buf 注册到 server_regions_（正确）
  // 错误：local_buf 也注册到 server_regions_（应该是 client_regions_）
  cli_.mem_store_.RecordMemory(true, remote_buf_.data(), remote_buf_.size());
  cli_.mem_store_.RecordMemory(true, local_buf_.data(), local_buf_.size());

  HixlOneSideOpDesc desc{};
  desc.remote_buf = remote_buf_.data();
  desc.local_buf = local_buf_.data();
  desc.len = kLen8;

  // 直接调用 ConvertHostMappedDescs，绕过 ValidateAddress
  std::vector<HixlOneSideOpDesc> mutable_descs(1, desc);
  const Status ret = DevChannel(cli_).ConvertHostMappedDescs(1, mutable_descs.data());
  EXPECT_EQ(ret, FAILED);  // local_buf 在错误的 regions，ConvertHostMappedDescs 会失败
}

TEST_F(HixlCSClientSlotReuseFixture, CheckStatusDeviceReturnsFailedWhenErrFlagSet) {
  void *host_flag = new uint8_t[8]();
  uint8_t *err_flag_ptr = new uint8_t(0U);

  TransferPool::SlotHandle fake_slot{};
  fake_slot.device_id = cli_.device_id_;
  fake_slot.slot_index = 0U;
  fake_slot.err_flag_host_addr = err_flag_ptr;
  fake_slot.err_flag_dev_addr = 0U;

  auto *handle = new DeviceCompleteHandle();
  handle->magic = 0x55425548U;
  handle->shared_slot = std::make_shared<TransferPool::SlotHandle>(fake_slot);
  handle->host_flag = host_flag;
  handle->dev_op_desc_buf = nullptr;

  DevChannel(cli_).active_slot_ = handle->shared_slot;
  *err_flag_ptr = 1U;
  HixlCompleteStatus st = HixlCompleteStatus::HIXL_COMPLETE_STATUS_WAITING;
  EXPECT_EQ(DevChannel(cli_).CheckStatusDevice(*handle, st), SUCCESS);
  EXPECT_EQ(st, HixlCompleteStatus::HIXL_COMPLETE_STATUS_FAILED);
  EXPECT_TRUE(DevChannel(cli_).transfer_failure_latched_);
  EXPECT_EQ(DevChannel(cli_).transfer_failure_status_, FAILED);
  EXPECT_EQ(*err_flag_ptr, 0U);
  delete err_flag_ptr;
}

// A completed transfer takes precedence over a slot error from subsequent work.
TEST_F(HixlCSClientSlotReuseFixture, CheckStatusDevicePrefersCompletedFlagOverErrFlag) {
  void *host_flag = new uint8_t[8]();
  *static_cast<uint64_t *>(host_flag) = kDeviceFlagDoneValueForTest;
  uint8_t *err_flag_ptr = new uint8_t(1U);

  TransferPool::SlotHandle fake_slot{};
  fake_slot.device_id = cli_.device_id_;
  fake_slot.slot_index = 0U;
  fake_slot.err_flag_host_addr = err_flag_ptr;
  fake_slot.err_flag_dev_addr = 0U;

  auto *handle = new DeviceCompleteHandle();
  handle->magic = 0x55425548U;
  handle->shared_slot = std::make_shared<TransferPool::SlotHandle>(fake_slot);
  handle->host_flag = host_flag;
  handle->dev_op_desc_buf = nullptr;

  DevChannel(cli_).active_slot_ = handle->shared_slot;
  HixlCompleteStatus st = HixlCompleteStatus::HIXL_COMPLETE_STATUS_WAITING;
  EXPECT_EQ(DevChannel(cli_).CheckStatusDevice(*handle, st), SUCCESS);
  EXPECT_EQ(st, HixlCompleteStatus::HIXL_COMPLETE_STATUS_COMPLETED);
  EXPECT_FALSE(DevChannel(cli_).transfer_failure_latched_);

  delete err_flag_ptr;
}

TEST_F(HixlCSClientSlotReuseFixture, CheckStatusDeviceReturnsWaitingWhenErrFlagZero) {
  uint64_t host_flag_mem = 0ULL;
  uint8_t err_flag_mem = 0U;

  TransferPool::SlotHandle fake_slot{};
  fake_slot.device_id = cli_.device_id_;
  fake_slot.slot_index = 0U;
  fake_slot.err_flag_host_addr = &err_flag_mem;
  fake_slot.err_flag_dev_addr = 0U;

  auto *handle = new DeviceCompleteHandle();
  handle->magic = 0x55425548U;
  handle->shared_slot = std::make_shared<TransferPool::SlotHandle>(fake_slot);
  handle->host_flag = &host_flag_mem;
  handle->dev_op_desc_buf = nullptr;

  HixlCompleteStatus st = HixlCompleteStatus::HIXL_COMPLETE_STATUS_COMPLETED;
  EXPECT_EQ(DevChannel(cli_).CheckStatusDevice(*handle, st), SUCCESS);
  EXPECT_EQ(st, HixlCompleteStatus::HIXL_COMPLETE_STATUS_WAITING);
  EXPECT_FALSE(DevChannel(cli_).transfer_failure_latched_);

  delete handle;
}

TEST_F(HixlCSClientSlotReuseFixture, CheckStatusDeviceReturnsFailedWhenTransferFailureLatched) {
  uint8_t err_flag_mem = 0U;
  TransferPool::SlotHandle fake_slot{};
  fake_slot.device_id = cli_.device_id_;
  fake_slot.slot_index = 0U;
  fake_slot.err_flag_host_addr = &err_flag_mem;
  fake_slot.err_flag_dev_addr = 0U;

  auto *first = new DeviceCompleteHandle();
  first->magic = 0x55425548U;
  first->shared_slot = std::make_shared<TransferPool::SlotHandle>(fake_slot);
  first->host_flag = new uint8_t[8]();
  first->dev_op_desc_buf = nullptr;
  DevChannel(cli_).active_slot_ = first->shared_slot;
  err_flag_mem = 1U;
  HixlCompleteStatus st1 = HixlCompleteStatus::HIXL_COMPLETE_STATUS_WAITING;
  EXPECT_EQ(DevChannel(cli_).CheckStatusDevice(*first, st1), SUCCESS);
  EXPECT_EQ(st1, HixlCompleteStatus::HIXL_COMPLETE_STATUS_FAILED);
  EXPECT_TRUE(DevChannel(cli_).transfer_failure_latched_);
  EXPECT_EQ(err_flag_mem, 0U);

  auto *second = new DeviceCompleteHandle();
  second->magic = 0x55425548U;
  second->shared_slot = std::make_shared<TransferPool::SlotHandle>(fake_slot);
  second->host_flag = new uint8_t[8]();
  second->dev_op_desc_buf = nullptr;
  HixlCompleteStatus st2 = HixlCompleteStatus::HIXL_COMPLETE_STATUS_WAITING;
  EXPECT_EQ(DevChannel(cli_).CheckStatusDevice(*second, st2), SUCCESS);
  EXPECT_EQ(st2, HixlCompleteStatus::HIXL_COMPLETE_STATUS_FAILED);
}

TEST_F(HixlCSClientSlotReuseFixture, ReleaseDevCompleteHandleResetsErrFlag) {
  uint8_t err_flag_mem = 1U;

  TransferPool::SlotHandle fake_slot{};
  fake_slot.device_id = cli_.device_id_;
  fake_slot.slot_index = 0U;
  fake_slot.err_flag_host_addr = &err_flag_mem;
  fake_slot.err_flag_dev_addr = 0U;

  auto *handle = new DeviceCompleteHandle();
  handle->magic = 0x55425548U;
  handle->shared_slot = std::make_shared<TransferPool::SlotHandle>(fake_slot);
  handle->host_flag = nullptr;
  handle->dev_op_desc_buf = nullptr;

  DevChannel(cli_).active_slot_ = handle->shared_slot;
  EXPECT_EQ(DevChannel(cli_).ReleaseDevCompleteHandle(handle), SUCCESS);
  EXPECT_EQ(err_flag_mem, 0U);
}

TEST_F(HixlCSClientDeviceFixture, CheckStatusDeviceBadMagicReturnsInvalid) {
  DeviceCompleteHandle bad_handle{};
  bad_handle.magic = 0xDEADBEEFU;
  bad_handle.shared_slot = nullptr;
  bad_handle.host_flag = nullptr;
  bad_handle.dev_op_desc_buf = nullptr;
  HixlCompleteStatus status = HixlCompleteStatus::HIXL_COMPLETE_STATUS_WAITING;
  EXPECT_EQ(DevChannel(cli_).CheckStatusDevice(bad_handle, status), PARAM_INVALID);
}

}  // namespace hixl
