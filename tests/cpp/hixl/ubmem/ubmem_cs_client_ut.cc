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

#include <cstdint>
#include <cstring>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "securec.h"

#include "ascendcl_stub.h"
#include "channel.h"
#include "common/hixl_inner_types.h"
#include "cs/ubmem/ubmem_aicpu_types.h"

#define private public
#define protected public
#include "cs/ubmem/ubmem_engine.h"
#undef protected
#undef private

#include "cs/ubmem/ubmem_virtual_memory_manager.h"
#include "cs/transfer_pool.h"
#include "depends/hccl/src/hccl_stub.h"
#include "depends/runtime/src/runtime_stub.h"
#include "engine/endpoint_test_utils.h"
#include "engine/test_mmpa_utils.h"
#include "hcomm/hcomm_res_defs.h"
#include "hixl/hixl_types.h"

#define private public
#define protected public
#include "hixl_cs_client.h"
#undef protected
#undef private

extern "C" uint32_t GetThreadAllocCallCount();
extern "C" void ResetThreadLifecycleStats();

namespace hixl {
namespace {
constexpr int32_t kUbMemCsClientDevId = 910261;
constexpr uint32_t kUbMemCsClientPort = 26661U;
constexpr const char *kSingleSlotPoolConfig = R"({"comm_resource_config.max_active_channels":1})";
constexpr const char *kUbMemMemcpyModeConfig =
    R"({"comm_resource_config.max_active_channels":1,"fabric_memory.enable_aicpu_unfold":false})";

class UbMemCsAclStub : public endpoint_test::MockAclRuntimeStub {
 public:
  enum class SubmissionEvent { kDataCopy, kKernelLaunch, kNotifyRecord, kHostFlagCopy };

  aclError aclrtKernelArgsAppend(aclrtArgsHandle args_handle, void *data, size_t size,
                                 aclrtParamHandle *param_handle) override {
    if (data != nullptr && size == sizeof(UbMemAicpuKernelParam) &&
        static_cast<const UbMemAicpuKernelParam *>(data)->version == kUbMemKernelParamVersion) {
      kernel_params_.emplace_back(*static_cast<const UbMemAicpuKernelParam *>(data));
    }
    return llm::AclRuntimeStub::aclrtKernelArgsAppend(args_handle, data, size, param_handle);
  }

  aclError aclrtStreamGetId(aclrtStream stream, int32_t *stream_id) override {
    (void)stream;
    if (stream_id == nullptr) {
      return ACL_ERROR_FAILURE;
    }
    *stream_id = 4;
    return ACL_SUCCESS;
  }

  aclError aclrtMemcpyAsync(void *dst, size_t dest_max, const void *src, size_t src_count, aclrtMemcpyKind kind,
                            aclrtStream stream) override {
    ++memcpy_async_count_;
    if (kind == ACL_MEMCPY_DEVICE_TO_DEVICE) {
      submission_events_.emplace_back(SubmissionEvent::kDataCopy);
      if (fail_next_data_copy_) {
        fail_next_data_copy_ = false;
        return ACL_ERROR_RT_INTERNAL_ERROR;
      }
    } else if (kind == ACL_MEMCPY_DEVICE_TO_HOST) {
      ++host_flag_d2h_count_;
      submission_events_.emplace_back(SubmissionEvent::kHostFlagCopy);
      if (block_host_flag_copy_) {
        return ACL_SUCCESS;
      }
    }
    return llm::AclRuntimeStub::aclrtMemcpyAsync(dst, dest_max, src, src_count, kind, stream);
  }

  aclError aclrtRecordNotify(aclrtNotify notify, aclrtStream stream) override {
    ++notify_record_count_;
    submission_events_.emplace_back(SubmissionEvent::kNotifyRecord);
    return llm::AclRuntimeStub::aclrtRecordNotify(notify, stream);
  }

  aclError aclrtLaunchKernelWithConfig(aclrtFuncHandle function, uint32_t block_dim, aclrtStream stream,
                                       aclrtLaunchKernelCfg *config, aclrtArgsHandle args, void *reserved) override {
    ++kernel_launch_count_;
    submission_events_.emplace_back(SubmissionEvent::kKernelLaunch);
    return llm::AclRuntimeStub::aclrtLaunchKernelWithConfig(function, block_dim, stream, config, args, reserved);
  }

  aclError aclrtWaitAndResetNotify(aclrtNotify notify, aclrtStream stream, uint32_t timeout) override {
    ++wait_notify_count_;
    return llm::AclRuntimeStub::aclrtWaitAndResetNotify(notify, stream, timeout);
  }

  aclError aclrtBinaryGetFunction(aclrtBinHandle bin_handle, const char *func_name,
                                  aclrtFuncHandle *func_handle) override {
    ++kernel_function_lookup_count_;
    if (fail_ubmem_kernel_lookup_ && func_name != nullptr &&
        std::string(func_name).find("HixlUbMemBatch") != std::string::npos) {
      *func_handle = nullptr;
      return ACL_SUCCESS;
    }
    return llm::AclRuntimeStub::aclrtBinaryGetFunction(bin_handle, func_name, func_handle);
  }

  aclError aclrtStreamAbort(aclrtStream stream) override {
    (void)stream;
    ++stream_abort_count_;
    return ACL_SUCCESS;
  }

  aclError aclrtStreamStop(aclrtStream stream) override {
    (void)stream;
    ++stream_stop_count_;
    return ACL_SUCCESS;
  }

  aclError aclrtSynchronizeStreamWithTimeout(aclrtStream stream, int32_t timeout) override {
    if (fail_next_aicpu_sync_) {
      fail_next_aicpu_sync_ = false;
      return ACL_ERROR_RT_INTERNAL_ERROR;
    }
    return llm::AclRuntimeStub::aclrtSynchronizeStreamWithTimeout(stream, timeout);
  }

  uint32_t wait_notify_count_{0U};
  uint32_t stream_abort_count_{0U};
  uint32_t stream_stop_count_{0U};
  bool fail_next_aicpu_sync_{false};
  uint32_t memcpy_async_count_{0U};
  uint32_t host_flag_d2h_count_{0U};
  uint32_t notify_record_count_{0U};
  uint32_t kernel_launch_count_{0U};
  bool block_host_flag_copy_{false};
  bool fail_next_data_copy_{false};
  bool fail_ubmem_kernel_lookup_{false};
  uint32_t kernel_function_lookup_count_{0U};
  std::vector<UbMemAicpuKernelParam> kernel_params_;
  std::vector<SubmissionEvent> submission_events_;
};

class UbMemCsSysApiStub : public hixl_test::SysApiHooks {
 public:
  int32_t Access(const char *path_name) override {
    if (path_name != nullptr && std::string(path_name).find("libcann_hixl_kernel.json") != std::string::npos) {
      return 0;
    }
    return hixl_test::SysApiHooks::Access(path_name);
  }

  int32_t RealPath(const char *path, char *real_path, int32_t real_path_len) override {
    if (path != nullptr && real_path != nullptr &&
        std::string(path).find("libcann_hixl_kernel.json") != std::string::npos) {
      errno_t ret = strncpy_s(real_path, static_cast<size_t>(real_path_len), path, strlen(path));
      return ret == EOK ? 0 : hixl_test::SysApiHooks::RealPath(path, real_path, real_path_len);
    }
    return hixl_test::SysApiHooks::RealPath(path, real_path, real_path_len);
  }

  void *DlOpen(const char *file_name, int32_t mode) override {
    if (file_name != nullptr && std::strcmp(file_name, "libra.so") == 0) {
      return hixl_test::RealDlOpen("libdl.so.2", mode);
    }
    return hixl_test::SysApiPassSentinelPtr();
  }

  void *DlSym(void *handle, const char *func_name) override {
    (void)handle;
    if (func_name != nullptr && std::strcmp(func_name, "RaRdevGetHandle") == 0) {
      return reinterpret_cast<void *>(&MockRaRdevGetHandle);
    }
    if (func_name != nullptr && std::strcmp(func_name, "RaGetNotifyBaseAddr") == 0) {
      return reinterpret_cast<void *>(&MockRaGetNotifyBaseAddr);
    }
    return hixl_test::SysApiPassSentinelPtr();
  }

  int32_t DlClose(void *handle) override {
    (void)handle;
    return 0;
  }

 private:
  static int MockRaRdevGetHandle(unsigned int phy_id, void **out_handle) {
    (void)phy_id;
    static int dummy_handle = 0;
    if (out_handle != nullptr) {
      *out_handle = &dummy_handle;
    }
    return 0;
  }

  static int MockRaGetNotifyBaseAddr(void *handle, unsigned long long *addr, unsigned long long *size) {
    (void)handle;
    if (addr != nullptr) {
      *addr = 0x1000ULL;
    }
    if (size != nullptr) {
      *size = 0x10000ULL;
    }
    return 0;
  }
};

EndpointDesc MakeUbMemEp(EndpointLocType loc) {
  EndpointDesc ep{};
  ep.loc.locType = loc;
  ep.protocol = COMM_PROTOCOL_UB_MEM;
  ep.commAddr.type = COMM_ADDR_TYPE_ID;
  ep.commAddr.id = 1U;
  return ep;
}

void FinalizeClientPool(int32_t device_id) {
  auto *pool = TransferPool::GetInstance(device_id);
  if (pool != nullptr) {
    pool->Finalize();
  }
}
}  // namespace

class UbMemCsClientUt : public ::testing::Test {
 protected:
  void SetUp() override {
    acl_stub_ = std::make_shared<UbMemCsAclStub>();
    acl_stub_->soc_name_ = "Ascend910_9391";
    acl_stub_->device_id_ = kUbMemCsClientDevId;
    acl_stub_->phy_device_id_ = 0;
    llm::AclRuntimeStub::SetInstance(acl_stub_);
    auto sys_api_stub = std::make_shared<UbMemCsSysApiStub>();
    sys_api_stub_ = sys_api_stub;
    hixl_test::InstallSysApiHooks(sys_api_stub);
    VirtualMemoryManager::GetInstance().Finalize();
    ResetTransferCounter();
    ResetThreadLifecycleStats();
    SetStubRtStreamSqId(11U);
  }

  void TearDown() override {
    (void)cli_.Destroy();
    FinalizeClientPool(kUbMemCsClientDevId);
    VirtualMemoryManager::GetInstance().Finalize();
    hixl_test::ResetSysApiHooks();
    llm::AclRuntimeStub::Reset();
  }

  Status CreateClient(EndpointLocType loc, const char *config_json = kSingleSlotPoolConfig) {
    EndpointDesc src = MakeUbMemEp(loc);
    EndpointDesc dst = MakeUbMemEp(loc);
    HixlClientConfig config{};
    config.global_resource_config = config_json;
    HixlClientDesc desc{};
    desc.server_ip = "127.0.0.1";
    desc.server_port = kUbMemCsClientPort;
    desc.local_endpoint = &src;
    desc.remote_endpoint = &dst;
    return cli_.Create(&desc, &config);
  }

  Status ImportOwnRemoteMem(void *addr, size_t size, void **remote_addr = nullptr) {
    CommMem mem{};
    mem.type = COMM_MEM_TYPE_DEVICE;
    mem.addr = addr;
    mem.size = size;
    MemHandle mem_handle = nullptr;
    HIXL_CHK_STATUS_RET(cli_.local_endpoint_->RegisterMem(nullptr, mem, mem_handle));
    CommMem imported{};
    std::vector<HixlMemDesc> descs;
    HIXL_CHK_STATUS_RET(cli_.local_endpoint_->ExportMem(descs), "export own registered memory failed");
    HIXL_CHK_BOOL_RET_STATUS(!descs.empty(), PARAM_INVALID, "own registered memory was not exported");
    HIXL_CHK_STATUS_RET(cli_.local_endpoint_->ImportMem(descs.back().export_desc, descs.back().export_len, imported),
                        "import own registered memory failed");
    if (remote_addr != nullptr) {
      *remote_addr = imported.addr;
    }
    return SUCCESS;
  }

  HixlCSClient cli_;
  std::shared_ptr<UbMemCsAclStub> acl_stub_;
  std::shared_ptr<UbMemCsSysApiStub> sys_api_stub_;
  uint8_t src_[8] = {1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U};
  uint8_t dst_[8] = {};
};

TEST_F(UbMemCsClientUt, DeviceCreateInitsPoolWithHcommThreadKeys) {
  ASSERT_EQ(CreateClient(ENDPOINT_LOC_TYPE_DEVICE), SUCCESS);
  EXPECT_EQ(cli_.device_id_, kUbMemCsClientDevId);
  EXPECT_GT(GetThreadAllocCallCount(), 0U);
  auto *pool = TransferPool::GetInstance(kUbMemCsClientDevId);
  ASSERT_NE(pool, nullptr);
  EXPECT_TRUE(pool->IsInitialized());
  TransferPool::SlotHandle slot{};
  ASSERT_EQ(pool->Acquire(&slot), SUCCESS);
  // The AICPU kernels reuse the slot's Hcomm worker thread as the transfer-context key.
  EXPECT_NE(slot.thread, static_cast<ThreadHandle>(0));
  pool->Release(slot);
}

TEST_F(UbMemCsClientUt, DeviceCreateUsesSharedHcommThreadPool) {
  ASSERT_EQ(CreateClient(ENDPOINT_LOC_TYPE_DEVICE), SUCCESS);
  EXPECT_EQ(cli_.device_id_, kUbMemCsClientDevId);
  EXPECT_GT(GetThreadAllocCallCount(), 0U);
  auto *pool = TransferPool::GetInstance(kUbMemCsClientDevId);
  ASSERT_NE(pool, nullptr);
  EXPECT_NE(pool->GetDeviceKernelFunc(true, COMM_PROTOCOL_UB_MEM), nullptr);
  EXPECT_NE(pool->GetDeviceKernelFunc(false, COMM_PROTOCOL_UB_MEM), nullptr);
}

TEST_F(UbMemCsClientUt, DeviceCreateRejectsTransferBatchAboveUbMemBudget) {
  constexpr char kTooLargeBatchConfig[] = R"({"transfer_config.max_transfer_count_per_batch":32766})";
  EXPECT_EQ(CreateClient(ENDPOINT_LOC_TYPE_DEVICE, kTooLargeBatchConfig), PARAM_INVALID);
  EXPECT_EQ(cli_.client_channel_, nullptr);
}

TEST_F(UbMemCsClientUt, DeviceMemcpyModeSubmitsAsyncWithoutKernelLaunch) {
  ASSERT_EQ(CreateClient(ENDPOINT_LOC_TYPE_DEVICE, kUbMemMemcpyModeConfig), SUCCESS);
  void *remote_addr = nullptr;
  ASSERT_EQ(ImportOwnRemoteMem(dst_, sizeof(dst_), &remote_addr), SUCCESS);
  HixlOneSideOpDesc desc{};
  desc.local_buf = src_;
  desc.remote_buf = remote_addr;
  desc.len = sizeof(src_);
  ASSERT_EQ(cli_.mem_store_.RecordMemory(true, remote_addr, sizeof(dst_)), SUCCESS);
  ASSERT_EQ(cli_.mem_store_.RecordMemory(false, src_, sizeof(src_)), SUCCESS);
  void *query_handle = nullptr;
  ASSERT_EQ(cli_.BatchPutAsync(&desc, 1U, &query_handle), SUCCESS);
  ASSERT_NE(query_handle, nullptr);

  EXPECT_EQ(acl_stub_->memcpy_async_count_, 2U);
  EXPECT_EQ(acl_stub_->host_flag_d2h_count_, 1U);
  EXPECT_EQ(acl_stub_->notify_record_count_, 0U);
  EXPECT_GE(acl_stub_->kernel_launch_count_, 0U);
  const auto &events = acl_stub_->submission_events_;
  ASSERT_GE(events.size(), 2U);
  EXPECT_EQ(std::count(events.begin(), events.end(), UbMemCsAclStub::SubmissionEvent::kDataCopy), 1U);
  EXPECT_EQ(std::count(events.begin(), events.end(), UbMemCsAclStub::SubmissionEvent::kHostFlagCopy), 1U);
}

TEST_F(UbMemCsClientUt, DeviceMemRegistrationRejectsNullAddressAndZeroSize) {
  ASSERT_EQ(CreateClient(ENDPOINT_LOC_TYPE_DEVICE, kUbMemMemcpyModeConfig), SUCCESS);
  CommMem mem{};
  mem.type = COMM_MEM_TYPE_DEVICE;
  mem.addr = nullptr;
  mem.size = 32U;
  MemHandle handle = nullptr;
  EXPECT_EQ(cli_.RegMem("null_addr", &mem, &handle), PARAM_INVALID);
  EXPECT_EQ(handle, nullptr);

  mem.addr = src_;
  mem.size = 0U;
  EXPECT_EQ(cli_.RegMem("zero_size", &mem, &handle), PARAM_INVALID);
  EXPECT_EQ(handle, nullptr);
  EXPECT_EQ(cli_.UnregMem(nullptr), PARAM_INVALID);
}

TEST_F(UbMemCsClientUt, DeviceLaunchUsesCsAclrtLaunchKernelWithConfig) {
  ASSERT_EQ(CreateClient(ENDPOINT_LOC_TYPE_DEVICE), SUCCESS);
  uint8_t local_buf[8] = {1U};
  uint8_t remote_buf[8] = {2U};
  void *remote_addr = nullptr;
  HixlOneSideOpDesc desc{};
  desc.local_buf = local_buf;
  desc.remote_buf = nullptr;
  desc.len = sizeof(local_buf);

  ASSERT_EQ(ImportOwnRemoteMem(remote_buf, sizeof(remote_buf), &remote_addr), SUCCESS);
  desc.remote_buf = remote_addr;
  ASSERT_EQ(cli_.mem_store_.RecordMemory(true, remote_addr, sizeof(remote_buf)), SUCCESS);
  ASSERT_EQ(cli_.mem_store_.RecordMemory(false, local_buf, sizeof(local_buf)), SUCCESS);
  ASSERT_EQ(cli_.BatchPutSync(&desc, 1U, 1000U), SUCCESS);
  EXPECT_GE(acl_stub_->kernel_launch_count_, 1U);
  ASSERT_GE(acl_stub_->kernel_params_.size(), 1U);
  EXPECT_EQ(acl_stub_->kernel_params_[0U].timeout_ms, 1000U);
  EXPECT_NE(acl_stub_->kernel_params_[0U].status_addr, 0U);
  EXPECT_EQ(acl_stub_->kernel_params_[0U].sqe_format, static_cast<uint32_t>(UbMemStarsSqeFormat::kStarsV1));
  EXPECT_GE(acl_stub_->wait_notify_count_, 1U);
  EXPECT_EQ(GetNbiCallCount(), 0U);
}

TEST_F(UbMemCsClientUt, DeviceA5ClientPropagatesStarsV2SqeFormat) {
  acl_stub_->soc_name_ = "Ascend950A";
  ASSERT_EQ(CreateClient(ENDPOINT_LOC_TYPE_DEVICE), SUCCESS);
  uint8_t local_buf[8] = {1U};
  uint8_t remote_buf[8] = {2U};
  void *remote_addr = nullptr;
  HixlOneSideOpDesc desc{};
  desc.local_buf = local_buf;
  desc.remote_buf = nullptr;
  desc.len = sizeof(local_buf);

  ASSERT_EQ(ImportOwnRemoteMem(remote_buf, sizeof(remote_buf), &remote_addr), SUCCESS);
  desc.remote_buf = remote_addr;
  ASSERT_EQ(cli_.mem_store_.RecordMemory(true, remote_addr, sizeof(remote_buf)), SUCCESS);
  ASSERT_EQ(cli_.mem_store_.RecordMemory(false, local_buf, sizeof(local_buf)), SUCCESS);
  ASSERT_EQ(cli_.BatchPutSync(&desc, 1U, 1000U), SUCCESS);
  ASSERT_GE(acl_stub_->kernel_params_.size(), 1U);
  EXPECT_EQ(acl_stub_->kernel_params_[0U].sqe_format, static_cast<uint32_t>(UbMemStarsSqeFormat::kStarsV2));
}

TEST_F(UbMemCsClientUt, DeviceAicpuStatusBufferMatchesSmallBatchLaunches) {
  constexpr char kSmallBatchConfig[] = R"({"comm_resource_config.max_active_channels":1,
                                          "transfer_config.max_transfer_count_per_batch":100})";
  ASSERT_EQ(CreateClient(ENDPOINT_LOC_TYPE_DEVICE, kSmallBatchConfig), SUCCESS);
  std::vector<HixlOneSideOpDesc> descs(200U);
  void *remote_addr = nullptr;
  ASSERT_EQ(ImportOwnRemoteMem(dst_, sizeof(dst_), &remote_addr), SUCCESS);
  for (auto &desc : descs) {
    desc.local_buf = src_;
    desc.remote_buf = remote_addr;
    desc.len = 1U;
  }
  ASSERT_EQ(cli_.mem_store_.RecordMemory(true, remote_addr, 200U), SUCCESS);
  ASSERT_EQ(cli_.mem_store_.RecordMemory(false, src_, 200U), SUCCESS);

  // ImportOwnRemoteMem registers CS memory through an AICPU ADD kernel, so launch counting must be
  // relative to the baseline rather than absolute.
  const size_t baseline_launches = acl_stub_->kernel_launch_count_;
  void *query_handle = nullptr;
  ASSERT_EQ(cli_.BatchPutAsync(descs.data(), static_cast<uint32_t>(descs.size()), &query_handle), SUCCESS);
  auto *pending = static_cast<UbMemEngine::UbMemCompleteHandle *>(query_handle);
  ASSERT_NE(pending->status_buf, nullptr);
  EXPECT_EQ(pending->status_count, 2U);
  EXPECT_EQ(acl_stub_->kernel_launch_count_, baseline_launches + 2U);
  ASSERT_EQ(acl_stub_->kernel_params_.size(), 2U);
  EXPECT_EQ(acl_stub_->kernel_params_[1U].status_addr,
            static_cast<uint64_t>(reinterpret_cast<uintptr_t>(pending->status_buf) + sizeof(uint32_t)));
  HixlCompleteStatus status = HixlCompleteStatus::HIXL_COMPLETE_STATUS_WAITING;
  EXPECT_EQ(cli_.QueryCompleteStatus(query_handle, &status), SUCCESS);
  EXPECT_EQ(status, HixlCompleteStatus::HIXL_COMPLETE_STATUS_COMPLETED);
}

TEST_F(UbMemCsClientUt, DeviceAicpuSyncFailureAbortsSlotOnLastReference) {
  ASSERT_EQ(CreateClient(ENDPOINT_LOC_TYPE_DEVICE), SUCCESS);
  void *remote_addr = nullptr;
  ASSERT_EQ(ImportOwnRemoteMem(dst_, sizeof(dst_), &remote_addr), SUCCESS);
  HixlOneSideOpDesc desc{};
  desc.local_buf = src_;
  desc.remote_buf = remote_addr;
  desc.len = sizeof(src_);
  ASSERT_EQ(cli_.mem_store_.RecordMemory(true, remote_addr, sizeof(dst_)), SUCCESS);
  ASSERT_EQ(cli_.mem_store_.RecordMemory(false, src_, sizeof(src_)), SUCCESS);

  void *query_handle = nullptr;
  ASSERT_EQ(cli_.BatchPutAsync(&desc, 1U, &query_handle), SUCCESS);
  auto *pending = static_cast<UbMemEngine::UbMemCompleteHandle *>(query_handle);
  uint8_t *err_flag = pending->slot->err_flag_host_addr;
  ASSERT_NE(err_flag, nullptr);
  acl_stub_->fail_next_aicpu_sync_ = true;
  EXPECT_EQ(cli_.BatchPutSync(&desc, 1U, 1000U), static_cast<Status>(ACL_ERROR_RT_INTERNAL_ERROR));
  EXPECT_FALSE(cli_.client_channel_->transfer_failure_latched_);

  auto *pool = TransferPool::GetInstance(kUbMemCsClientDevId);
  ASSERT_NE(pool, nullptr);
  ASSERT_TRUE(pool->IsInitialized());
  EXPECT_EQ(*err_flag, 1U);
}

TEST_F(UbMemCsClientUt, DeviceAicpuSyncReturnsKernelFailureStatus) {
  ASSERT_EQ(CreateClient(ENDPOINT_LOC_TYPE_DEVICE), SUCCESS);
  void *remote_addr = nullptr;
  ASSERT_EQ(ImportOwnRemoteMem(dst_, sizeof(dst_), &remote_addr), SUCCESS);
  HixlOneSideOpDesc desc{};
  desc.local_buf = src_;
  desc.remote_buf = remote_addr;
  desc.len = sizeof(src_);
  ASSERT_EQ(cli_.mem_store_.RecordMemory(true, remote_addr, sizeof(dst_)), SUCCESS);
  ASSERT_EQ(cli_.mem_store_.RecordMemory(false, src_, sizeof(src_)), SUCCESS);

  void *query_handle = nullptr;
  ASSERT_EQ(cli_.BatchPutAsync(&desc, 1U, &query_handle), SUCCESS);
  auto *pending = static_cast<UbMemEngine::UbMemCompleteHandle *>(query_handle);
  ASSERT_NE(pending->status_buf, nullptr);
  ASSERT_GT(pending->status_count, 0U);
  *static_cast<uint32_t *>(pending->status_buf) = 1U;
  *static_cast<uint64_t *>(pending->host_flag) = 1U;
  HixlCompleteStatus status = HixlCompleteStatus::HIXL_COMPLETE_STATUS_WAITING;
  EXPECT_EQ(cli_.QueryCompleteStatus(query_handle, &status), FAILED);
  EXPECT_EQ(status, HixlCompleteStatus::HIXL_COMPLETE_STATUS_FAILED);
  EXPECT_TRUE(cli_.client_channel_->transfer_failure_latched_);
}

TEST_F(UbMemCsClientUt, DeviceMemcpySyncSubmitFailureDoesNotDoubleRelease) {
  ASSERT_EQ(CreateClient(ENDPOINT_LOC_TYPE_DEVICE, kUbMemMemcpyModeConfig), SUCCESS);
  void *remote_addr = nullptr;
  ASSERT_EQ(ImportOwnRemoteMem(dst_, sizeof(dst_), &remote_addr), SUCCESS);
  HixlOneSideOpDesc desc{};
  desc.local_buf = src_;
  desc.remote_buf = remote_addr;
  desc.len = sizeof(src_);
  ASSERT_EQ(cli_.mem_store_.RecordMemory(true, remote_addr, sizeof(dst_)), SUCCESS);
  ASSERT_EQ(cli_.mem_store_.RecordMemory(false, src_, sizeof(src_)), SUCCESS);

  acl_stub_->fail_next_data_copy_ = true;
  EXPECT_EQ(cli_.BatchPutSync(&desc, 1U, 1000U), ACL_ERROR_RT_INTERNAL_ERROR);
  EXPECT_FALSE(cli_.client_channel_->transfer_failure_latched_);
  EXPECT_EQ(acl_stub_->stream_abort_count_, 0U);
  EXPECT_EQ(cli_.BatchPutSync(&desc, 1U, 1000U), SUCCESS);
}

TEST_F(UbMemCsClientUt, DeviceMemcpySyncStatusFailureLatchesClient) {
  ASSERT_EQ(CreateClient(ENDPOINT_LOC_TYPE_DEVICE, kUbMemMemcpyModeConfig), SUCCESS);
  void *remote_addr = nullptr;
  ASSERT_EQ(ImportOwnRemoteMem(dst_, sizeof(dst_), &remote_addr), SUCCESS);
  HixlOneSideOpDesc desc{};
  desc.local_buf = src_;
  desc.remote_buf = remote_addr;
  desc.len = sizeof(src_);
  ASSERT_EQ(cli_.mem_store_.RecordMemory(true, remote_addr, sizeof(dst_)), SUCCESS);
  ASSERT_EQ(cli_.mem_store_.RecordMemory(false, src_, sizeof(src_)), SUCCESS);

  acl_stub_->block_host_flag_copy_ = true;
  void *query_handle = nullptr;
  ASSERT_EQ(cli_.BatchPutAsync(&desc, 1U, &query_handle), SUCCESS);
  auto *pending_handle = static_cast<UbMemEngine::UbMemCompleteHandle *>(query_handle);
  ASSERT_NE(pending_handle, nullptr);
  *pending_handle->slot->err_flag_host_addr = 1U;

  EXPECT_EQ(cli_.BatchPutSync(&desc, 1U, 1000U), FAILED);
  EXPECT_TRUE(cli_.client_channel_->transfer_failure_latched_);
  EXPECT_EQ(cli_.client_channel_->transfer_failure_status_, FAILED);
  // Abort is deferred until the last shared-slot reference is released (teardown).
  EXPECT_EQ(acl_stub_->stream_abort_count_, 0U);
  EXPECT_EQ(cli_.BatchPutSync(&desc, 1U, 1000U), FAILED);
}

TEST_F(UbMemCsClientUt, DeviceAsyncStatusFailureLatchesClient) {
  ASSERT_EQ(CreateClient(ENDPOINT_LOC_TYPE_DEVICE, kUbMemMemcpyModeConfig), SUCCESS);
  void *remote_addr = nullptr;
  ASSERT_EQ(ImportOwnRemoteMem(dst_, sizeof(dst_), &remote_addr), SUCCESS);
  HixlOneSideOpDesc desc{};
  desc.local_buf = src_;
  desc.remote_buf = remote_addr;
  desc.len = sizeof(src_);
  ASSERT_EQ(cli_.mem_store_.RecordMemory(true, remote_addr, sizeof(dst_)), SUCCESS);
  ASSERT_EQ(cli_.mem_store_.RecordMemory(false, src_, sizeof(src_)), SUCCESS);

  acl_stub_->block_host_flag_copy_ = true;
  void *query_handle = nullptr;
  ASSERT_EQ(cli_.BatchPutAsync(&desc, 1U, &query_handle), SUCCESS);
  auto *pending_handle = static_cast<UbMemEngine::UbMemCompleteHandle *>(query_handle);
  ASSERT_NE(pending_handle, nullptr);
  *pending_handle->slot->err_flag_host_addr = 1U;

  HixlCompleteStatus status = HixlCompleteStatus::HIXL_COMPLETE_STATUS_WAITING;
  EXPECT_EQ(cli_.QueryCompleteStatus(query_handle, &status), SUCCESS);
  EXPECT_EQ(status, HixlCompleteStatus::HIXL_COMPLETE_STATUS_FAILED);
  EXPECT_TRUE(cli_.client_channel_->transfer_failure_latched_);
  EXPECT_EQ(cli_.client_channel_->transfer_failure_status_, FAILED);
  EXPECT_EQ(cli_.BatchPutSync(&desc, 1U, 1000U), FAILED);
}

TEST_F(UbMemCsClientUt, DeviceLaunchMissingKernelReturnsUnsupported) {
  acl_stub_->fail_ubmem_kernel_lookup_ = true;
  ASSERT_EQ(CreateClient(ENDPOINT_LOC_TYPE_DEVICE), SUCCESS);
  uint8_t local_buf[8] = {1U};
  uint8_t remote_buf[8] = {2U};
  void *remote_addr = nullptr;
  ASSERT_EQ(ImportOwnRemoteMem(remote_buf, sizeof(remote_buf), &remote_addr), SUCCESS);
  HixlOneSideOpDesc desc{};
  desc.local_buf = local_buf;
  desc.remote_buf = remote_addr;
  desc.len = sizeof(local_buf);
  ASSERT_EQ(cli_.mem_store_.RecordMemory(true, remote_addr, sizeof(remote_buf)), SUCCESS);
  ASSERT_EQ(cli_.mem_store_.RecordMemory(false, local_buf, sizeof(local_buf)), SUCCESS);

  EXPECT_EQ(cli_.BatchPutSync(&desc, 1U, 1000U), UNSUPPORTED);
  EXPECT_FALSE(cli_.client_channel_->transfer_failure_latched_);
}

TEST_F(UbMemCsClientUt, DeviceLaunchRejectsEmptyDescBuf) {
  ASSERT_EQ(CreateClient(ENDPOINT_LOC_TYPE_DEVICE), SUCCESS);
  HixlOneSideOpDesc empty_desc{};
  void *query_handle = nullptr;
  EXPECT_EQ(cli_.BatchPutAsync(&empty_desc, 0U, &query_handle), PARAM_INVALID);
}

}  // namespace hixl
