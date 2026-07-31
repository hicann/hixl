/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <algorithm>
#include <chrono>
#include <future>
#include <limits>
#include <string>
#include <thread>
#include <vector>

#include "fabric_mem_runtime_stub.h"
#include "transfer_context_manager.h"

namespace hixl {
namespace {
using namespace fabric_mem_test;

class FabricMemAicpuUnfoldUTest : public FabricMemAicpuTransferServiceTestBase {};
}  // namespace

TEST_F(FabricMemAicpuUnfoldUTest, AicpuUnfoldInitializationReturnsUnsupportedWhenKernelUnavailable) {
  auto param = MakeServiceInitParam(&statistic_, &local_memory_);
  param.enable_aicpu_unfold = true;
  runtime_->soc_name_ = "Ascend910_9391";
  runtime_->kernel_binary_load_error_ = ACL_ERROR_RT_INTERNAL_ERROR;

  EXPECT_EQ(service_.Initialize(param), UNSUPPORTED);
  EXPECT_EQ(runtime_->kernel_binary_load_count_, 1U);
  EXPECT_FALSE(service_.aicpu_dispatcher_.IsInitialized());
  EXPECT_EQ(service_.dev_const_one_, nullptr);
}

TEST_F(FabricMemAicpuUnfoldUTest, AicpuUnfoldCreatesDeviceOnlyWorkerStreams) {
  auto param = MakeServiceInitParam(&statistic_, &local_memory_);
  param.enable_aicpu_unfold = true;
  runtime_->soc_name_ = "Ascend910_9391";
  ASSERT_EQ(service_.Initialize(param), SUCCESS);

  AsyncSlot slot;
  ASSERT_EQ(service_.slot_pool_.AcquireAsync(slot), SUCCESS);
  ASSERT_EQ(service_.slot_pool_.EnsureAicpuRtsqStreams(slot), SUCCESS);
  ASSERT_EQ(slot.sdma_streams.size(), 1U);
  ASSERT_EQ(slot.notifies.size(), 1U);
  ASSERT_EQ(slot.notify_ids.size(), 1U);
  EXPECT_EQ(runtime_->notify_create_count_, 1U);
  EXPECT_EQ(runtime_->notify_id_query_count_, 1U);
  EXPECT_EQ(runtime_->stream_flags_.at(slot.sdma_streams[0]), ACL_STREAM_DEVICE_USE_ONLY);
  EXPECT_EQ(runtime_->device_only_stream_failure_mode_count_, 0U);
  service_.slot_pool_.Release(slot, true);
  EXPECT_EQ(runtime_->device_only_stream_abort_count_, 0U);
  EXPECT_EQ(runtime_->device_only_stream_stop_count_, 1U);
  EXPECT_EQ(runtime_->notify_destroy_count_, 1U);
}

TEST_F(FabricMemAicpuUnfoldUTest, AicpuUnfoldRollsBackNotifyWhenIdQueryFails) {
  auto param = MakeServiceInitParam(&statistic_, &local_memory_);
  param.enable_aicpu_unfold = true;
  runtime_->soc_name_ = "Ascend910_9391";
  ASSERT_EQ(service_.Initialize(param), SUCCESS);

  AsyncSlot slot;
  ASSERT_EQ(service_.slot_pool_.AcquireAsync(slot), SUCCESS);
  runtime_->notify_id_error_ = ACL_ERROR_INVALID_PARAM;
  EXPECT_NE(service_.slot_pool_.EnsureAicpuRtsqStreams(slot), SUCCESS);
  EXPECT_EQ(runtime_->notify_create_count_, 1U);
  EXPECT_EQ(runtime_->notify_destroy_count_, 1U);
  EXPECT_TRUE(slot.sdma_streams.empty());
  EXPECT_TRUE(slot.notifies.empty());

  service_.slot_pool_.Release(slot, true);
}

TEST_F(FabricMemAicpuUnfoldUTest, AicpuUnfoldRejectsNotifyIdOutsideA3Abi) {
  auto param = MakeServiceInitParam(&statistic_, &local_memory_);
  param.enable_aicpu_unfold = true;
  runtime_->soc_name_ = "Ascend910_9391";
  ASSERT_EQ(service_.Initialize(param), SUCCESS);

  AsyncSlot slot;
  ASSERT_EQ(service_.slot_pool_.AcquireAsync(slot), SUCCESS);
  runtime_->next_notify_id_ = 1U << 13U;
  EXPECT_EQ(service_.slot_pool_.EnsureAicpuRtsqStreams(slot), UNSUPPORTED);
  EXPECT_EQ(runtime_->notify_destroy_count_, 1U);
  service_.slot_pool_.Release(slot, true);
}

TEST_F(FabricMemAicpuUnfoldUTest, AicpuUnfoldDispatchesKernelWithoutHostD2DCopies) {
  auto param = MakeServiceInitParam(&statistic_, &local_memory_);
  param.enable_aicpu_unfold = true;
  runtime_->soc_name_ = "Ascend910_9391";
  ASSERT_EQ(service_.Initialize(param), SUCCESS);
  EXPECT_TRUE(service_.aicpu_dispatcher_.IsInitialized());
  EXPECT_EQ(runtime_->kernel_binary_load_count_, 1U);
  EXPECT_EQ(runtime_->kernel_function_lookup_count_, 3U);

  uint8_t local[kLen] = {};
  uint8_t remote[kLen] = {};
  AsyncSlot slot;
  ASSERT_EQ(service_.slot_pool_.AcquireAsync(slot), SUCCESS);
  ASSERT_EQ(service_.slot_pool_.EnsureAicpuRtsqStreams(slot), SUCCESS);
  slot.has_aicpu_unfold = true;
  FabricMemAicpuRequestResource aicpu_resource;
  const size_t async_copy_count_before = runtime_->memcpy_async_count_;
  const size_t memcpy_count_before = runtime_->memcpy_count_;
  {
    TemporaryRtContext ctx_guard(slot.ctx);
    ASSERT_EQ(service_.ProcessCopyWithAsync(slot, WRITE, BuildOpDescs(local, remote), aicpu_resource), SUCCESS);
    ASSERT_NE(aicpu_resource.descriptor_buffer, nullptr);
    ASSERT_EQ(aclrtSynchronizeStream(slot.streams[0]), ACL_SUCCESS);
    FabricMemAicpuDispatcher::ReleaseRequestResource(aicpu_resource);
  }
  // Sync ADD + one FabricMem batch kernel.
  EXPECT_EQ(runtime_->kernel_launch_count_, 2U);
  EXPECT_EQ(runtime_->notify_wait_count_, 1U);
  ASSERT_EQ(runtime_->kernel_params_.size(), 1U);
  EXPECT_EQ(runtime_->kernel_params_[0U].desc_count, 1U);
  EXPECT_EQ(runtime_->kernel_params_[0U].emit_notify_record, 1U);
  EXPECT_EQ(runtime_->kernel_params_[0U].notify_id, slot.notify_ids[0U]);
  EXPECT_EQ(runtime_->kernel_params_[0U].transfer_ctx_key, slot.transfer_ctx_key);
  EXPECT_NE(slot.transfer_ctx_key, 0U);
  EXPECT_EQ(runtime_->memcpy_async_count_, async_copy_count_before);
  // Descriptor/status/kernel-args upload (3) + Sync ADD entry/state/args (3).
  EXPECT_EQ(runtime_->memcpy_count_, memcpy_count_before + 6U);
  EXPECT_EQ(aicpu_resource.descriptor_buffer, nullptr);
  EXPECT_EQ(aicpu_resource.status_buffer, nullptr);
  service_.slot_pool_.Release(slot, false);
}

TEST_F(FabricMemAicpuUnfoldUTest, AicpuUnfoldRejectsNonOneTaskStreamNum) {
  auto param = MakeServiceInitParam(&statistic_, &local_memory_);
  param.max_stream_num = 8U;
  param.task_stream_num = 4U;
  param.enable_aicpu_unfold = true;
  runtime_->soc_name_ = "Ascend910_9391";
  EXPECT_EQ(service_.Initialize(param), PARAM_INVALID);
  EXPECT_FALSE(service_.aicpu_dispatcher_.IsInitialized());
}

TEST_F(FabricMemAicpuUnfoldUTest, AicpuUnfoldDefaultsToSingleTaskStream) {
  auto param = MakeServiceInitParam(&statistic_, &local_memory_);
  param.max_stream_num = 8U;
  param.task_stream_num = 1U;
  param.enable_aicpu_unfold = true;
  runtime_->soc_name_ = "Ascend910_9391";
  ASSERT_EQ(service_.Initialize(param), SUCCESS);
  EXPECT_EQ(service_.task_stream_num_, 1U);
  // An AICPU slot costs a control stream plus its device-only RTSQ worker stream.
  EXPECT_EQ(service_.slot_pool_.max_async_slot_num_, 4U);

  AsyncSlot slot;
  ASSERT_EQ(service_.slot_pool_.AcquireAsync(slot), SUCCESS);
  ASSERT_EQ(service_.slot_pool_.EnsureAicpuRtsqStreams(slot), SUCCESS);
  EXPECT_EQ(slot.streams.size(), 1U);
  EXPECT_EQ(slot.sdma_streams.size(), 1U);
  EXPECT_EQ(slot.notifies.size(), 1U);
  EXPECT_EQ(slot.notify_ids.size(), 1U);
  EXPECT_EQ(slot.host_flags.size(), 1U);
  service_.slot_pool_.Release(slot, false);
}

TEST_F(FabricMemAicpuUnfoldUTest, AicpuUnfoldBatchesDescriptorsOnSingleStream) {
  constexpr size_t kDescriptorCount = 8194U;
  auto param = MakeServiceInitParam(&statistic_, &local_memory_);
  param.enable_aicpu_unfold = true;
  runtime_->soc_name_ = "Ascend910_9391";
  ASSERT_EQ(service_.Initialize(param), SUCCESS);

  uint8_t local[kLen] = {};
  uint8_t remote[kLen] = {};
  std::vector<TransferOpDesc> op_descs(kDescriptorCount,
                                       {reinterpret_cast<uintptr_t>(local), reinterpret_cast<uintptr_t>(remote), kLen});
  AsyncSlot slot;
  ASSERT_EQ(service_.slot_pool_.AcquireAsync(slot), SUCCESS);
  ASSERT_EQ(service_.slot_pool_.EnsureAicpuRtsqStreams(slot), SUCCESS);
  slot.has_aicpu_unfold = true;
  FabricMemAicpuRequestResource aicpu_resource;
  {
    TemporaryRtContext ctx_guard(slot.ctx);
    ASSERT_EQ(service_.ProcessCopyWithAsync(slot, WRITE, op_descs, aicpu_resource), SUCCESS);
    // Sync ADD + ceil(8194 / 128) batch kernels; ceil(8194 / 1920) notify waits on one stream.
    EXPECT_EQ(runtime_->kernel_launch_count_, 66U);
    EXPECT_EQ(runtime_->notify_wait_count_, 5U);
    ASSERT_EQ(runtime_->kernel_params_.size(), 65U);
    size_t notify_param_count = 0U;
    for (const auto &kernel_param : runtime_->kernel_params_) {
      EXPECT_LE(kernel_param.desc_count, 128U);
      notify_param_count += kernel_param.emit_notify_record != 0U ? 1U : 0U;
    }
    EXPECT_EQ(notify_param_count, 5U);
    ASSERT_EQ(aclrtSynchronizeStream(slot.streams[0U]), ACL_SUCCESS);
    FabricMemAicpuDispatcher::ReleaseRequestResource(aicpu_resource);
  }
  service_.slot_pool_.Release(slot, false);
}

TEST_F(FabricMemAicpuUnfoldUTest, AicpuUnfoldUses128KernelAnd1920NotifyBoundaries) {
  struct BatchCase {
    size_t descriptor_count;
    size_t kernel_count;
    size_t notify_count;
  };
  const std::vector<BatchCase> cases = {
      {1U, 1U, 1U},     {128U, 1U, 1U},   {129U, 2U, 1U},   {1920U, 15U, 1U},
      {1921U, 16U, 2U}, {3840U, 30U, 2U}, {3841U, 31U, 3U},
  };
  auto param = MakeServiceInitParam(&statistic_, &local_memory_);
  param.enable_aicpu_unfold = true;
  runtime_->soc_name_ = "Ascend910_9391";
  ASSERT_EQ(service_.Initialize(param), SUCCESS);

  uint8_t local[kLen] = {};
  uint8_t remote[kLen] = {};
  for (const auto &batch_case : cases) {
    std::vector<TransferOpDesc> op_descs(
        batch_case.descriptor_count, {reinterpret_cast<uintptr_t>(local), reinterpret_cast<uintptr_t>(remote), kLen});
    AsyncSlot slot;
    ASSERT_EQ(service_.slot_pool_.AcquireAsync(slot), SUCCESS);
    ASSERT_EQ(service_.slot_pool_.EnsureAicpuRtsqStreams(slot), SUCCESS);
    slot.has_aicpu_unfold = true;
    FabricMemAicpuRequestResource aicpu_resource;
    const size_t launch_before = runtime_->kernel_launch_count_;
    const size_t wait_before = runtime_->notify_wait_count_;
    const size_t params_before = runtime_->kernel_params_.size();
    {
      TemporaryRtContext ctx_guard(slot.ctx);
      ASSERT_EQ(service_.ProcessCopyWithAsync(slot, WRITE, op_descs, aicpu_resource), SUCCESS);
      ASSERT_EQ(aclrtSynchronizeStream(slot.streams[0]), ACL_SUCCESS);
      FabricMemAicpuDispatcher::ReleaseRequestResource(aicpu_resource);
    }
    // Sync ADD once per ProcessCopyWithAsync when transfer_ctx_key is unset, plus batch kernels.
    EXPECT_EQ(runtime_->kernel_launch_count_ - launch_before, batch_case.kernel_count + 1U);
    EXPECT_EQ(runtime_->notify_wait_count_ - wait_before, batch_case.notify_count);
    ASSERT_EQ(runtime_->kernel_params_.size() - params_before, batch_case.kernel_count);
    size_t emitted_notifies = 0U;
    for (size_t idx = params_before; idx < runtime_->kernel_params_.size(); ++idx) {
      EXPECT_LE(runtime_->kernel_params_[idx].desc_count, 128U);
      emitted_notifies += runtime_->kernel_params_[idx].emit_notify_record != 0U ? 1U : 0U;
    }
    EXPECT_EQ(emitted_notifies, batch_case.notify_count);
    service_.slot_pool_.Release(slot, false);
  }
  EXPECT_EQ(runtime_->notify_create_count_, 1U);
  EXPECT_EQ(runtime_->notify_destroy_count_, 0U);
}

TEST_F(FabricMemAicpuUnfoldUTest, AicpuUnfoldSplitsDescriptorsLongerThanOneSdmaTask) {
  constexpr uint64_t kMaxSdmaTransferBytes = std::numeric_limits<uint32_t>::max();
  auto param = MakeServiceInitParam(&statistic_, &local_memory_);
  param.enable_aicpu_unfold = true;
  runtime_->soc_name_ = "Ascend910_9391";
  ASSERT_EQ(service_.Initialize(param), SUCCESS);

  // One SDMA task moves at most kMaxSdmaTransferBytes, so this length needs three of them. The host
  // must emit three device descriptors, otherwise its RTSQ entry accounting undercounts.
  constexpr uintptr_t kLocalAddr = 0x100000U;
  constexpr uintptr_t kRemoteAddr = 0x900000U;
  const std::vector<TransferOpDesc> op_descs = {
      {kLocalAddr, kRemoteAddr, static_cast<size_t>((2U * kMaxSdmaTransferBytes) + 1U)}};
  AsyncSlot slot;
  ASSERT_EQ(service_.slot_pool_.AcquireAsync(slot), SUCCESS);
  ASSERT_EQ(service_.slot_pool_.EnsureAicpuRtsqStreams(slot), SUCCESS);
  slot.has_aicpu_unfold = true;
  FabricMemAicpuRequestResource aicpu_resource;
  {
    TemporaryRtContext ctx_guard(slot.ctx);
    ASSERT_EQ(service_.ProcessCopyWithAsync(slot, WRITE, op_descs, aicpu_resource), SUCCESS);
    EXPECT_EQ(aicpu_resource.descriptor_buffer_size, 3U * sizeof(FabricMemAicpuTransferDesc));
    ASSERT_EQ(runtime_->kernel_params_.size(), 1U);
    EXPECT_EQ(runtime_->kernel_params_[0U].desc_count, 3U);
    const auto *const device_descs = static_cast<const FabricMemAicpuTransferDesc *>(aicpu_resource.descriptor_buffer);
    EXPECT_EQ(device_descs[0U].length, kMaxSdmaTransferBytes);
    EXPECT_EQ(device_descs[1U].length, kMaxSdmaTransferBytes);
    EXPECT_EQ(device_descs[2U].length, 1U);
    EXPECT_EQ(device_descs[0U].src_addr, kLocalAddr);
    EXPECT_EQ(device_descs[1U].src_addr, kLocalAddr + kMaxSdmaTransferBytes);
    EXPECT_EQ(device_descs[2U].src_addr, kLocalAddr + (2U * kMaxSdmaTransferBytes));
    EXPECT_EQ(device_descs[2U].dst_addr, kRemoteAddr + (2U * kMaxSdmaTransferBytes));
    ASSERT_EQ(aclrtSynchronizeStream(slot.streams[0U]), ACL_SUCCESS);
    FabricMemAicpuDispatcher::ReleaseRequestResource(aicpu_resource);
  }
  service_.slot_pool_.Release(slot, false);
}

TEST_F(FabricMemAicpuUnfoldUTest, AicpuUnfoldAsyncOwnsDescriptorsUntilCompletion) {
  auto param = MakeServiceInitParam(&statistic_, &local_memory_);
  param.enable_aicpu_unfold = true;
  runtime_->soc_name_ = "Ascend910_9391";
  ASSERT_EQ(service_.Initialize(param), SUCCESS);

  uint8_t local[kLen] = {};
  uint8_t remote[kLen] = {};
  const std::string remote_engine = "127.0.0.1:13003";
  auto channel = AddMappedServiceChannel(service_, remote_engine, remote, sizeof(remote));
  TransferReq req = nullptr;
  ASSERT_EQ(service_.TransferAsync(remote_engine, WRITE, BuildOpDescs(local, remote), req), SUCCESS);
  ASSERT_GE(runtime_->submission_events_.size(), 3U);
  const size_t event_count = runtime_->submission_events_.size();
  EXPECT_EQ(runtime_->submission_events_[event_count - 3U], FabricMemRuntimeStub::SubmissionEvent::kKernelLaunch);
  EXPECT_EQ(runtime_->submission_events_[event_count - 2U], FabricMemRuntimeStub::SubmissionEvent::kNotifyWait);
  EXPECT_EQ(runtime_->submission_events_[event_count - 1U], FabricMemRuntimeStub::SubmissionEvent::kHostFlagCopy);
  {
    std::lock_guard<std::mutex> lock(channel->records_mutex);
    ASSERT_EQ(channel->async_records.size(), 1U);
    const auto &record = channel->async_records.begin()->second;
    EXPECT_TRUE(record.slot.has_aicpu_unfold);
    EXPECT_NE(record.aicpu_resource.descriptor_buffer, nullptr);
    EXPECT_NE(record.aicpu_resource.status_buffer, nullptr);
    EXPECT_EQ(record.aicpu_resource.status_count, 1U);
  }

  const size_t free_count_before = runtime_->free_count_;
  TransferStatus status = TransferStatus::WAITING;
  EXPECT_EQ(service_.GetTransferStatus(req, status), SUCCESS);
  EXPECT_EQ(status, TransferStatus::COMPLETED);
  // Descriptor/status/kernel-args free (3).
  EXPECT_EQ(runtime_->free_count_, free_count_before + 3U);
  EXPECT_GE(runtime_->kernel_launch_count_, 1U);
  EXPECT_EQ(runtime_->device_only_stream_sync_count_, 0U);
}

TEST_F(FabricMemAicpuUnfoldUTest, AicpuUnfoldSyncTransferWaitsForRtsqWithoutStreamSynchronization) {
  auto param = MakeServiceInitParam(&statistic_, &local_memory_);
  param.enable_aicpu_unfold = true;
  runtime_->soc_name_ = "Ascend910_9391";
  ASSERT_EQ(service_.Initialize(param), SUCCESS);

  uint8_t local[kLen] = {};
  uint8_t remote[kLen] = {};
  const std::string remote_engine = "127.0.0.1:13008";
  AddMappedServiceChannel(service_, remote_engine, remote, sizeof(remote));
  EXPECT_EQ(service_.TransferSync(remote_engine, WRITE, BuildOpDescs(local, remote), kClientTimeoutMs), SUCCESS);
  EXPECT_EQ(runtime_->device_only_stream_sync_count_, 0U);
}

TEST_F(FabricMemAicpuUnfoldUTest, AicpuUnfoldSyncTransferFailsWhenDisconnectWaitsForLock) {
  // AICPU sync holds transfer_mu through stream wait; Disconnect marks disconnecting then blocks on
  // the mutex (no abort-during-wait). After wait unblocks, sync observes disconnecting.
  auto param = MakeServiceInitParam(&statistic_, &local_memory_);
  param.enable_aicpu_unfold = true;
  runtime_->soc_name_ = "Ascend910_9391";
  ASSERT_EQ(service_.Initialize(param), SUCCESS);

  uint8_t local[kLen] = {};
  uint8_t remote[kLen] = {};
  const std::string remote_engine = "127.0.0.1:13011";
  auto channel = AddMappedServiceChannel(service_, remote_engine, remote, sizeof(remote));
  runtime_->block_sync_with_timeout_.store(true, std::memory_order_release);
  constexpr int32_t kSyncHoldTimeoutMs = 1000;
  auto transfer = std::async(std::launch::async, [this, &remote_engine, &local, &remote]() {
    return service_.TransferSync(remote_engine, WRITE, BuildOpDescs(local, remote), kSyncHoldTimeoutMs);
  });
  for (size_t attempt = 0U; attempt < 100U; ++attempt) {
    if (runtime_->sync_with_timeout_entered_.load(std::memory_order_acquire) > 0U) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  if (runtime_->sync_with_timeout_entered_.load(std::memory_order_acquire) == 0U) {
    runtime_->unblock_sync_with_timeout_.store(true, std::memory_order_release);
    FAIL() << "Sync transfer did not start waiting on its control stream.";
  }

  auto disconnect = std::async(std::launch::async, [this, &remote_engine]() {
    return service_.Disconnect(AscendString(remote_engine.c_str()), kClientTimeoutMs);
  });
  for (size_t attempt = 0U; attempt < 100U && !channel->disconnecting.load(std::memory_order_acquire); ++attempt) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  EXPECT_TRUE(channel->disconnecting.load(std::memory_order_acquire));
  EXPECT_EQ(disconnect.wait_for(std::chrono::milliseconds(10)), std::future_status::timeout);

  runtime_->unblock_sync_with_timeout_.store(true, std::memory_order_release);
  EXPECT_EQ(transfer.get(), NOT_CONNECTED);
  EXPECT_EQ(disconnect.get(), SUCCESS);
}

TEST_F(FabricMemAicpuUnfoldUTest, AicpuUnfoldSyncCompletionProtectsReusableSlotFromDisconnect) {
  auto param = MakeServiceInitParam(&statistic_, &local_memory_);
  param.enable_aicpu_unfold = true;
  runtime_->soc_name_ = "Ascend910_9391";
  ASSERT_EQ(service_.Initialize(param), SUCCESS);

  uint8_t local[kLen] = {};
  uint8_t remote[kLen] = {};
  const std::string remote_engine = "127.0.0.1:13014";
  auto channel = AddMappedServiceChannel(service_, remote_engine, remote, sizeof(remote));
  runtime_->block_status_download_.store(true, std::memory_order_release);
  auto transfer = std::async(std::launch::async, [this, &remote_engine, &local, &remote]() {
    return service_.TransferSync(remote_engine, WRITE, BuildOpDescs(local, remote), kClientTimeoutMs);
  });
  for (size_t attempt = 0U; attempt < 100U; ++attempt) {
    if (runtime_->status_download_entered_.load(std::memory_order_acquire) > 0U) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  if (runtime_->status_download_entered_.load(std::memory_order_acquire) == 0U) {
    runtime_->unblock_status_download_.store(true, std::memory_order_release);
    EXPECT_EQ(transfer.get(), SUCCESS);
    FAIL() << "Sync transfer did not reach request-status processing.";
  }

  auto disconnect = std::async(std::launch::async, [this, &remote_engine]() {
    return service_.Disconnect(AscendString(remote_engine.c_str()), kClientTimeoutMs);
  });
  for (size_t attempt = 0U; attempt < 100U && !channel->disconnecting.load(std::memory_order_acquire); ++attempt) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  EXPECT_TRUE(channel->disconnecting.load(std::memory_order_acquire));
  EXPECT_EQ(disconnect.wait_for(std::chrono::milliseconds(10)), std::future_status::timeout);

  runtime_->unblock_status_download_.store(true, std::memory_order_release);
  EXPECT_EQ(transfer.get(), SUCCESS);
  EXPECT_EQ(disconnect.get(), SUCCESS);
  EXPECT_EQ(runtime_->device_only_stream_stop_count_, 0U);
}

TEST_F(FabricMemAicpuUnfoldUTest, AicpuUnfoldDisconnectAbortsAndDestroysBeforeReleasingDescriptors) {
  auto param = MakeServiceInitParam(&statistic_, &local_memory_);
  param.enable_aicpu_unfold = true;
  runtime_->soc_name_ = "Ascend910_9391";
  ASSERT_EQ(service_.Initialize(param), SUCCESS);

  uint8_t local[kLen] = {};
  uint8_t remote[kLen] = {};
  const std::string remote_engine = "127.0.0.1:13004";
  AddMappedServiceChannel(service_, remote_engine, remote, sizeof(remote));
  TransferReq req = nullptr;
  ASSERT_EQ(service_.TransferAsync(remote_engine, WRITE, BuildOpDescs(local, remote), req), SUCCESS);

  const size_t free_count_before = runtime_->free_count_;
  runtime_->stream_lifecycle_events_.clear();
  EXPECT_EQ(service_.Disconnect(AscendString(remote_engine.c_str()), kClientTimeoutMs), SUCCESS);
  // Descriptor/status/kernel-args free (3) + Sync DELETE entry/state/args free (3).
  EXPECT_EQ(runtime_->free_count_, free_count_before + 6U);
  EXPECT_GT(runtime_->stream_abort_count_, 0U);
  EXPECT_GT(runtime_->device_only_stream_stop_count_, 0U);
  EXPECT_EQ(runtime_->device_only_stream_abort_count_, 0U);
  const auto abort = std::find(runtime_->stream_lifecycle_events_.begin(), runtime_->stream_lifecycle_events_.end(),
                               FabricMemRuntimeStub::StreamLifecycleEvent::kAbortControl);
  const auto stop = std::find(runtime_->stream_lifecycle_events_.begin(), runtime_->stream_lifecycle_events_.end(),
                              FabricMemRuntimeStub::StreamLifecycleEvent::kStopWorker);
  ASSERT_NE(abort, runtime_->stream_lifecycle_events_.end());
  ASSERT_NE(stop, runtime_->stream_lifecycle_events_.end());
  EXPECT_LT(abort, stop);
}

TEST_F(FabricMemAicpuUnfoldUTest, AicpuUnfoldSyncFailureAbortsAndReleasesImmediately) {
  auto param = MakeServiceInitParam(&statistic_, &local_memory_);
  param.enable_aicpu_unfold = true;
  runtime_->soc_name_ = "Ascend910_9391";
  ASSERT_EQ(service_.Initialize(param), SUCCESS);

  uint8_t local[kLen] = {};
  uint8_t remote[kLen] = {};
  const std::string remote_engine = "127.0.0.1:13009";
  auto channel = AddMappedServiceChannel(service_, remote_engine, remote, sizeof(remote));
  const size_t free_count_before = runtime_->free_count_;
  // Fail control-stream wait (2nd SynchronizeStreamWithTimeout), not Sync ADD (1st).
  runtime_->stream_sync_error_ = ACL_ERROR_RT_STREAM_SYNC_TIMEOUT;
  runtime_->stream_sync_fail_on_count_ = 2U;
  EXPECT_NE(service_.TransferSync(remote_engine, WRITE, BuildOpDescs(local, remote), kClientTimeoutMs), SUCCESS);
  // Sync ADD free (3) + descriptor/status/kernel-args free (3) + Sync DELETE free (3).
  EXPECT_EQ(runtime_->free_count_, free_count_before + 9U);
  std::lock_guard<std::mutex> lock(channel->records_mutex);
  EXPECT_TRUE(channel->async_records.empty());
  EXPECT_TRUE(channel->active_sync_slots.empty());
}

TEST_F(FabricMemAicpuUnfoldUTest, AicpuUnfoldAsyncSubmitFailureAbortsAndReleasesImmediately) {
  auto param = MakeServiceInitParam(&statistic_, &local_memory_);
  param.enable_aicpu_unfold = true;
  runtime_->soc_name_ = "Ascend910_9391";
  ASSERT_EQ(service_.Initialize(param), SUCCESS);

  uint8_t local[kLen] = {};
  uint8_t remote[kLen] = {};
  const std::string remote_engine = "127.0.0.1:13010";
  auto channel = AddMappedServiceChannel(service_, remote_engine, remote, sizeof(remote));
  const size_t free_count_before = runtime_->free_count_;
  // Fail the FabricMem batch launch (2nd), not Sync ADD (1st).
  runtime_->kernel_launch_error_ = ACL_ERROR_INVALID_PARAM;
  runtime_->kernel_launch_fail_on_count_ = 2U;
  TransferReq req = nullptr;
  EXPECT_NE(service_.TransferAsync(remote_engine, WRITE, BuildOpDescs(local, remote), req), SUCCESS);
  // Sync ADD free (3) + descriptor/status/kernel-args free (3) + Sync DELETE free (3).
  EXPECT_EQ(runtime_->free_count_, free_count_before + 9U);
  std::lock_guard<std::mutex> lock(channel->records_mutex);
  EXPECT_TRUE(channel->async_records.empty());
}

TEST_F(FabricMemAicpuUnfoldUTest, AicpuUnfoldNotifyWaitEnqueueFailureAbortsAndReleasesImmediately) {
  auto param = MakeServiceInitParam(&statistic_, &local_memory_);
  param.enable_aicpu_unfold = true;
  runtime_->soc_name_ = "Ascend910_9391";
  ASSERT_EQ(service_.Initialize(param), SUCCESS);

  uint8_t local[kLen] = {};
  uint8_t remote[kLen] = {};
  const std::string remote_engine = "127.0.0.1:13015";
  auto channel = AddMappedServiceChannel(service_, remote_engine, remote, sizeof(remote));
  const size_t free_count_before = runtime_->free_count_;
  runtime_->notify_wait_error_ = ACL_ERROR_INVALID_PARAM;
  TransferReq req = nullptr;

  EXPECT_NE(service_.TransferAsync(remote_engine, WRITE, BuildOpDescs(local, remote), req), SUCCESS);
  // Sync ADD + FabricMem batch + Sync DELETE on failure cleanup.
  EXPECT_EQ(runtime_->kernel_launch_count_, 3U);
  EXPECT_EQ(runtime_->notify_wait_count_, 1U);
  // Sync ADD free (3) + descriptor/status/kernel-args free (3) + Sync DELETE free (3).
  EXPECT_EQ(runtime_->free_count_, free_count_before + 9U);
  std::lock_guard<std::mutex> lock(channel->records_mutex);
  EXPECT_TRUE(channel->async_records.empty());
}

TEST_F(FabricMemAicpuUnfoldUTest, AicpuUnfoldDisconnectDeletesTransferContext) {
  auto param = MakeServiceInitParam(&statistic_, &local_memory_);
  param.enable_aicpu_unfold = true;
  runtime_->soc_name_ = "Ascend910_9391";
  ASSERT_EQ(service_.Initialize(param), SUCCESS);

  uint8_t local[kLen] = {};
  uint8_t remote[kLen] = {};
  const std::string remote_engine = "127.0.0.1:13023";
  auto channel = AddMappedServiceChannel(service_, remote_engine, remote, sizeof(remote));
  TransferReq req = nullptr;
  ASSERT_EQ(service_.TransferAsync(remote_engine, WRITE, BuildOpDescs(local, remote), req), SUCCESS);

  uint64_t transfer_ctx_key = 0U;
  {
    std::lock_guard<std::mutex> transfer_lock(channel->transfer_mu);
    transfer_ctx_key = channel->bound_slot.transfer_ctx_key;
  }
  ASSERT_NE(transfer_ctx_key, 0U);
  EXPECT_NE(TransferContextManager::Instance().Get(static_cast<ThreadHandle>(transfer_ctx_key)), nullptr);

  EXPECT_EQ(service_.Disconnect(AscendString(remote_engine.c_str()), kClientTimeoutMs), SUCCESS);
  EXPECT_EQ(TransferContextManager::Instance().Get(static_cast<ThreadHandle>(transfer_ctx_key)), nullptr);
}

TEST_F(FabricMemAicpuUnfoldUTest, AicpuUnfoldDisconnectReleasesInFlightAsyncRequest) {
  auto param = MakeServiceInitParam(&statistic_, &local_memory_);
  param.enable_aicpu_unfold = true;
  runtime_->soc_name_ = "Ascend910_9391";
  ASSERT_EQ(service_.Initialize(param), SUCCESS);

  uint8_t local[kLen] = {};
  uint8_t remote[kLen] = {};
  const std::string remote_engine = "127.0.0.1:13005";
  AddMappedServiceChannel(service_, remote_engine, remote, sizeof(remote));
  TransferReq req = nullptr;
  ASSERT_EQ(service_.TransferAsync(remote_engine, WRITE, BuildOpDescs(local, remote), req), SUCCESS);

  const size_t free_count_before = runtime_->free_count_;
  EXPECT_EQ(service_.Disconnect(AscendString(remote_engine.c_str()), kClientTimeoutMs), SUCCESS);
  EXPECT_FALSE(service_.channel_manager_.IsConnected(remote_engine));
  // Descriptor/status/kernel-args free (3) + Sync DELETE entry/state/args free (3).
  EXPECT_EQ(runtime_->free_count_, free_count_before + 6U);
}

TEST_F(FabricMemAicpuUnfoldUTest, AicpuUnfoldFinalizeReleasesInFlightAsyncRequest) {
  auto param = MakeServiceInitParam(&statistic_, &local_memory_);
  param.enable_aicpu_unfold = true;
  runtime_->soc_name_ = "Ascend910_9391";
  ASSERT_EQ(service_.Initialize(param), SUCCESS);

  uint8_t local[kLen] = {};
  uint8_t remote[kLen] = {};
  const std::string remote_engine = "127.0.0.1:13006";
  auto channel = AddMappedServiceChannel(service_, remote_engine, remote, sizeof(remote));
  int32_t keepalive_fd = -1;
  {
    keepalive_fd = ::dup(STDOUT_FILENO);
    ASSERT_GE(keepalive_fd, 0);
    channel->keepalive_fd = keepalive_fd;
  }
  TransferReq req = nullptr;
  ASSERT_EQ(service_.TransferAsync(remote_engine, WRITE, BuildOpDescs(local, remote), req), SUCCESS);

  const size_t free_count_before = runtime_->free_count_;
  service_.Finalize();
  EXPECT_FALSE(service_.channel_manager_.IsConnected(remote_engine));
  EXPECT_FALSE(service_.channel_manager_.HasChannels());
  EXPECT_GE(runtime_->free_count_, free_count_before + 3U);
  EXPECT_EQ(::close(keepalive_fd), -1);
}

TEST_F(FabricMemAicpuUnfoldUTest, AicpuUnfoldReportsFailedLaunchStatus) {
  auto param = MakeServiceInitParam(&statistic_, &local_memory_);
  param.enable_aicpu_unfold = true;
  runtime_->soc_name_ = "Ascend910_9391";
  ASSERT_EQ(service_.Initialize(param), SUCCESS);

  uint8_t local[kLen] = {};
  uint8_t remote[kLen] = {};
  const std::string remote_engine = "127.0.0.1:13007";
  auto channel = AddMappedServiceChannel(service_, remote_engine, remote, sizeof(remote));
  TransferReq req = nullptr;
  ASSERT_EQ(service_.TransferAsync(remote_engine, WRITE, BuildOpDescs(local, remote), req), SUCCESS);
  {
    std::lock_guard<std::mutex> lock(channel->records_mutex);
    ASSERT_EQ(channel->async_records.size(), 1U);
    auto &resource = channel->async_records.begin()->second.aicpu_resource;
    ASSERT_NE(resource.status_buffer, nullptr);
    const uint32_t failed = 1U;
    ASSERT_EQ(aclrtMemcpy(resource.status_buffer, sizeof(failed), &failed, sizeof(failed), ACL_MEMCPY_HOST_TO_DEVICE),
              ACL_SUCCESS);
  }

  TransferStatus status = TransferStatus::WAITING;
  EXPECT_EQ(service_.GetTransferStatus(req, status), SUCCESS);
  EXPECT_EQ(status, TransferStatus::FAILED);
}

TEST_F(FabricMemAicpuUnfoldUTest, AicpuUnfoldPipelinesAsyncOnSharedSlot) {
  auto param = MakeServiceInitParam(&statistic_, &local_memory_);
  param.enable_aicpu_unfold = true;
  runtime_->soc_name_ = "Ascend910_9391";
  ASSERT_EQ(service_.Initialize(param), SUCCESS);

  uint8_t local[kLen] = {};
  uint8_t remote[kLen] = {};
  const std::string remote_engine = "127.0.0.1:13020";
  auto channel = AddMappedServiceChannel(service_, remote_engine, remote, sizeof(remote));
  ASSERT_NE(channel, nullptr);

  // Like hixl_cs: one channel / one slot; submit returns then the next async can enqueue.
  TransferReq req1 = nullptr;
  TransferReq req2 = nullptr;
  ASSERT_EQ(service_.TransferAsync(remote_engine, WRITE, BuildOpDescs(local, remote), req1), SUCCESS);
  ASSERT_EQ(service_.TransferAsync(remote_engine, WRITE, BuildOpDescs(local, remote), req2), SUCCESS);

  EXPECT_EQ(channel->bound_slot_refs, 2U);
  EXPECT_EQ(channel->async_records.size(), 2U);

  TransferStatus status = TransferStatus::WAITING;
  EXPECT_EQ(service_.GetTransferStatus(req1, status), SUCCESS);
  EXPECT_EQ(status, TransferStatus::COMPLETED);
  EXPECT_EQ(service_.GetTransferStatus(req2, status), SUCCESS);
  EXPECT_EQ(status, TransferStatus::COMPLETED);
  EXPECT_EQ(channel->bound_slot_refs, 0U);
}

TEST_F(FabricMemAicpuUnfoldUTest, AicpuUnfoldAllowsConcurrentTransfersOnDifferentChannels) {
  auto param = MakeServiceInitParam(&statistic_, &local_memory_);
  param.enable_aicpu_unfold = true;
  runtime_->soc_name_ = "Ascend910_9391";
  ASSERT_EQ(service_.Initialize(param), SUCCESS);

  uint8_t local[kLen] = {};
  uint8_t remote0[kLen] = {};
  uint8_t remote1[kLen] = {};
  const std::string remote_engine0 = "127.0.0.1:13021";
  const std::string remote_engine1 = "127.0.0.1:13022";
  AddMappedServiceChannel(service_, remote_engine0, remote0, sizeof(remote0));
  AddMappedServiceChannel(service_, remote_engine1, remote1, sizeof(remote1));

  TransferReq req0 = nullptr;
  TransferReq req1 = nullptr;
  ASSERT_EQ(service_.TransferAsync(remote_engine0, WRITE, BuildOpDescs(local, remote0), req0), SUCCESS);
  ASSERT_EQ(service_.TransferAsync(remote_engine1, WRITE, BuildOpDescs(local, remote1), req1), SUCCESS);

  TransferStatus status = TransferStatus::WAITING;
  EXPECT_EQ(service_.GetTransferStatus(req0, status), SUCCESS);
  EXPECT_EQ(status, TransferStatus::COMPLETED);
  EXPECT_EQ(service_.GetTransferStatus(req1, status), SUCCESS);
  EXPECT_EQ(status, TransferStatus::COMPLETED);
}

}  // namespace hixl
