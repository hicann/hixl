/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_TESTS_CPP_HIXL_UBMEM_UBMEM_RUNTIME_STUB_H_
#define CANN_HIXL_TESTS_CPP_HIXL_UBMEM_UBMEM_RUNTIME_STUB_H_

#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#define private public
#define protected public
#include "engine/hixl_options.h"
#include "cs/ubmem/ubmem_aicpu_types.h"
#include "cs/ubmem/ubmem_allocator.h"
#include "cs/ubmem/ubmem_memory.h"
#include "cs/ubmem/ubmem_types.h"
#undef protected
#undef private

#include "common/hixl_utils.h"
#include "common/optional_aclrt_context.h"
#include "depends/ascendcl/src/ascendcl_stub.h"
#include "ubmem_test_utils.h"
#include "cs/ubmem/ubmem_virtual_memory_manager.h"
#include "hixl/hixl_types.h"

namespace hixl::ubmem_test {
constexpr uintptr_t kLocalAddr = 0x100000UL;
constexpr uintptr_t kRemoteOldAddr = 0x200000UL;
constexpr uintptr_t kRemoteNewAddr = 0x300000UL;
constexpr size_t kLen = 32U;
// Device id used by the memory-layer tests; identity binding only applies on the same device.
constexpr int32_t kTestDeviceId = 0;
constexpr int32_t kOtherDeviceId = 1;
// VISIBLE_DEVICES-style remap so tests can tell user id 0 from driver logical id.
constexpr int32_t kUserToDriverLogicIdOffset = 4;

class ScopedRuntimeMock {
 public:
  explicit ScopedRuntimeMock(const std::shared_ptr<llm::AclRuntimeStub> &instance) {
    llm::AclRuntimeStub::SetInstance(instance);
    UbMemAllocator::ResetForeignShareHandleCacheForTest();
  }

  ~ScopedRuntimeMock() {
    llm::GetAclStubMock().clear();
    llm::AclRuntimeStub::Reset();
  }

  ScopedRuntimeMock(const ScopedRuntimeMock &) = delete;
  ScopedRuntimeMock &operator=(const ScopedRuntimeMock &) = delete;
};

class UbMemRuntimeStub : public llm::AclRuntimeStub {
 public:
  enum class StreamLifecycleEvent { kAbortControl, kDrainControl, kStopWorker };
  enum class SubmissionEvent { kKernelLaunch, kNotifyWait, kNotifyRecord, kHostFlagCopy };

  aclError aclrtSetCurrentContext(aclrtContext context) override;
  aclError aclrtGetCurrentContext(aclrtContext *context) override;
  aclError aclrtPointerGetAttributes(const void *ptr, aclrtPtrAttributes *attributes) override;
  aclError aclrtMemcpyAsync(void *dst, size_t dest_max, const void *src, size_t src_count, aclrtMemcpyKind kind,
                            aclrtStream stream) override;
  aclError aclrtMemcpy(void *dst, size_t dest_max, const void *src, size_t count, aclrtMemcpyKind kind) override;
  aclError aclrtBinaryLoadFromFile(const char *path, aclrtBinaryLoadOptions *options,
                                   aclrtBinHandle *bin_handle) override;
  aclError aclrtBinaryGetFunction(aclrtBinHandle bin_handle, const char *function_name,
                                  aclrtFuncHandle *function_handle) override;
  aclError aclrtStreamGetId(aclrtStream stream, int32_t *stream_id) override;
  aclError aclrtCreateStreamWithConfig(aclrtStream *stream, uint32_t priority, uint32_t flag) override;
  aclError aclrtCreateNotify(aclrtNotify *notify, uint64_t flag) override;
  aclError aclrtGetNotifyId(aclrtNotify notify, uint32_t *notify_id) override;
  aclError aclrtDestroyNotify(aclrtNotify notify) override;
  aclError aclrtRecordNotify(aclrtNotify notify, aclrtStream stream) override;
  aclError aclrtWaitAndResetNotify(aclrtNotify notify, aclrtStream stream, uint32_t timeout) override;
  aclError aclrtDestroyStream(aclrtStream stream) override;
  aclError aclrtLaunchKernelWithConfig(aclrtFuncHandle function, uint32_t block_dim, aclrtStream stream,
                                       aclrtLaunchKernelCfg *config, aclrtArgsHandle args, void *reserved) override;
  aclError aclrtLaunchKernelV2(aclrtFuncHandle function, uint32_t num_blocks, const void *args_data, size_t args_size,
                               aclrtLaunchKernelCfg *cfg, aclrtStream stream) override;
  aclError aclrtKernelArgsAppend(aclrtArgsHandle args_handle, void *data, size_t size,
                                 aclrtParamHandle *param_handle) override;
  aclError aclrtFree(void *ptr) override;
  aclError aclrtStreamQuery(aclrtStream stream, aclrtStreamStatus *status) override;
  aclError aclrtSetStreamFailureMode(aclrtStream stream, uint64_t mode) override;
  aclError aclrtSynchronizeStream(aclrtStream stream) override;
  aclError aclrtSynchronizeStreamWithTimeout(aclrtStream stream, int32_t timeout) override;
  aclError aclrtStreamAbort(aclrtStream stream) override;
  aclError aclrtStreamStop(aclrtStream stream) override;
  aclError aclrtMallocPhysical(aclrtDrvMemHandle *handle, size_t size, const aclrtPhysicalMemProp *prop,
                               uint64_t flags) override;
  aclError aclrtFreePhysical(aclrtDrvMemHandle handle) override;
  aclError aclrtMemGetAddressRange(void *ptr, void **pbase, size_t *psize) override;
  aclError aclrtMemRetainAllocationHandle(void *devPtr, aclrtDrvMemHandle *handle) override;
  aclError aclrtMemExportToShareableHandleV2(aclrtDrvMemHandle handle, uint64_t flags, aclrtMemSharedHandleType type,
                                             void *shareableHandle) override;
  // Test hook: when set, aclrtMemExportToShareableHandleV2 returns this error and does not touch the
  // output handle. Cleared after one call so later registrations can retry successfully.
  aclError export_fail_once_{ACL_SUCCESS};
  aclError aclrtMemImportFromShareableHandleV2(void *shareableHandle, aclrtMemSharedHandleType type, uint64_t flags,
                                               aclrtDrvMemHandle *handle) override;
  aclError aclrtMapMem(void *devPtr, size_t size, size_t offset, aclrtDrvMemHandle handle, uint64_t flags) override;
  aclError aclrtUnmapMem(void *devPtr) override;
  aclError aclrtMemSetAccess(void *virPtr, size_t size, aclrtMemAccessDesc *desc, size_t count) override;
  aclError aclrtGetLogicDevIdByUserDevId(const int32_t userDevid, int32_t *const logicDevId) override;
  bool IsDeviceOnlyStream(aclrtStream stream) const;
  void SetAddressRanges(std::vector<std::pair<uintptr_t, size_t>> ranges);

  bool pointer_is_host_{false};
  bool get_context_returns_null_{false};
  aclError pointer_attr_error_{ACL_SUCCESS};
  aclError memcpy_async_error_{ACL_SUCCESS};
  aclrtMemcpyKind memcpy_async_fail_kind_{ACL_MEMCPY_DEVICE_TO_DEVICE};
  size_t memcpy_async_fail_on_count_{0U};
  size_t memcpy_async_count_{0U};
  size_t memcpy_count_{0U};
  size_t host_flag_d2h_count_{0U};
  aclError kernel_binary_load_error_{ACL_SUCCESS};
  aclError kernel_function_lookup_error_{ACL_SUCCESS};
  aclError kernel_launch_error_{ACL_SUCCESS};
  size_t kernel_launch_fail_on_count_{0U};
  size_t kernel_binary_load_count_{0U};
  size_t kernel_function_lookup_count_{0U};
  size_t kernel_launch_count_{0U};
  size_t notify_create_count_{0U};
  size_t notify_id_query_count_{0U};
  size_t notify_destroy_count_{0U};
  size_t notify_record_count_{0U};
  size_t notify_wait_count_{0U};
  uint32_t next_notify_id_{1U};
  aclError notify_create_error_{ACL_SUCCESS};
  aclError notify_id_error_{ACL_SUCCESS};
  aclError notify_record_error_{ACL_SUCCESS};
  aclError notify_wait_error_{ACL_SUCCESS};
  std::unordered_map<aclrtNotify, uint32_t> notify_ids_;
  std::vector<UbMemAicpuKernelParam> kernel_params_;
  std::vector<SubmissionEvent> submission_events_;
  size_t free_count_{0U};
  size_t stream_query_count_{0U};
  size_t stream_failure_mode_count_{0U};
  uint64_t last_stream_failure_mode_{0U};
  size_t device_only_stream_failure_mode_count_{0U};
  size_t stream_abort_count_{0U};
  size_t device_only_stream_abort_count_{0U};
  size_t stream_stop_count_{0U};
  size_t device_only_stream_stop_count_{0U};
  aclError stream_query_error_{ACL_SUCCESS};
  aclError stream_sync_error_{ACL_SUCCESS};
  size_t stream_sync_fail_on_count_{0U};
  size_t stream_sync_count_{0U};
  size_t device_only_stream_sync_count_{0U};
  std::atomic<bool> block_sync_with_timeout_{false};
  std::atomic<bool> unblock_sync_with_timeout_{false};
  std::atomic<uint32_t> sync_with_timeout_entered_{0U};
  std::atomic<bool> block_status_download_{false};
  std::atomic<bool> unblock_status_download_{false};
  std::atomic<uint32_t> status_download_entered_{0U};
  std::set<aclrtStream> streams_not_complete_;
  std::unordered_map<aclrtStream, uint32_t> stream_flags_;
  std::vector<StreamLifecycleEvent> stream_lifecycle_events_;
  size_t malloc_physical_count_{0U};
  size_t free_physical_count_{0U};
  size_t get_address_range_count_{0U};
  size_t retain_count_{0U};
  size_t retain_fail_on_count_{0U};
  size_t mem_export_count_{0U};
  size_t mem_import_count_{0U};
  size_t mem_map_count_{0U};
  size_t mem_unmap_count_{0U};
  aclError get_address_range_error_{ACL_SUCCESS};
  std::vector<uintptr_t> retained_addresses_;
  std::vector<std::pair<uintptr_t, size_t>> address_ranges_;
  size_t mem_set_access_count_{0U};
  // Number of distinct share handles produced by the stub export, used to distinguish real ACL
  // exports from process-level cache hits in tests.
  size_t exported_handle_count_{0U};
  size_t last_mem_set_access_size_{0U};
  size_t last_mem_set_access_count_{0U};
  aclrtMemAccessDesc last_mem_access_desc_{};
  size_t set_current_context_count_{0U};
  aclrtContext last_set_context_{nullptr};
  aclrtPhysicalMemProp last_physical_mem_prop_{};
};

inline ShareHandleInfo BuildShareHandle(uintptr_t va_addr = kRemoteOldAddr, size_t len = kLen) {
  ShareHandleInfo info{};
  info.va_addr = va_addr;
  info.len = len;
  for (size_t i = 0; i < sizeof(info.share_handle.data); ++i) {
    info.share_handle.data[i] = static_cast<uint8_t>(i + 1U);
  }
  return info;
}

class UbMemMemoryUTest : public ::testing::Test {
 protected:
  void SetUp() override {
    runtime_ = std::make_shared<UbMemRuntimeStub>();
    runtime_->SetAddressRanges({{kLocalAddr, kLen}});
    scoped_runtime_ = std::make_unique<ScopedRuntimeMock>(runtime_);
    VirtualMemoryManager::GetInstance().Finalize();
    ASSERT_EQ(VirtualMemoryManager::GetInstance().Initialize(), SUCCESS);
  }

  void TearDown() override {
    memory_.Finalize();
    VirtualMemoryManager::GetInstance().Finalize();
    scoped_runtime_.reset();
    runtime_.reset();
  }

  std::shared_ptr<UbMemRuntimeStub> runtime_;
  std::unique_ptr<ScopedRuntimeMock> scoped_runtime_;
  UbMemMemory memory_;
};

class UbMemAllocatorUTest : public ::testing::Test {
 protected:
  void SetUp() override {
    runtime_ = std::make_shared<UbMemRuntimeStub>();
    scoped_runtime_ = std::make_unique<ScopedRuntimeMock>(runtime_);
    VirtualMemoryManager::GetInstance().Finalize();
    ASSERT_EQ(VirtualMemoryManager::GetInstance().Initialize(), SUCCESS);
  }

  void TearDown() override {
    VirtualMemoryManager::GetInstance().Finalize();
    scoped_runtime_.reset();
    runtime_.reset();
  }

  std::shared_ptr<UbMemRuntimeStub> runtime_;
  std::unique_ptr<ScopedRuntimeMock> scoped_runtime_;
};
}  // namespace hixl::ubmem_test

#endif  // CANN_HIXL_TESTS_CPP_HIXL_UBMEM_UBMEM_RUNTIME_STUB_H_
