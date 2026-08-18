/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "fabric_mem_runtime_stub.h"

namespace hixl::fabric_mem_test {

aclError FabricMemRuntimeStub::aclrtSetCurrentContext(aclrtContext context) {
  ++set_current_context_count_;
  last_set_context_ = context;
  return llm::AclRuntimeStub::aclrtSetCurrentContext(context);
}

aclError FabricMemRuntimeStub::aclrtGetCurrentContext(aclrtContext *context) {
  if (get_context_returns_null_) {
    *context = nullptr;
    return ACL_ERROR_NONE;
  }
  return llm::AclRuntimeStub::aclrtGetCurrentContext(context);
}

aclError FabricMemRuntimeStub::aclrtPointerGetAttributes(const void *ptr, aclrtPtrAttributes *attributes) {
  (void)ptr;
  if (pointer_attr_error_ != ACL_ERROR_NONE) {
    return pointer_attr_error_;
  }
  attributes->location.type = pointer_is_host_ ? ACL_MEM_LOCATION_TYPE_HOST : ACL_MEM_LOCATION_TYPE_DEVICE;
  return ACL_ERROR_NONE;
}

const char *FabricMemRuntimeStub::aclrtGetSocName() {
  return soc_name_.empty() ? llm::AclRuntimeStub::aclrtGetSocName() : soc_name_.c_str();
}

aclError FabricMemRuntimeStub::aclrtMemcpyAsync(void *dst, size_t dest_max, const void *src, size_t src_count,
                                                aclrtMemcpyKind kind, aclrtStream stream) {
  ++memcpy_async_count_;
  if (kind == ACL_MEMCPY_DEVICE_TO_HOST) {
    ++host_flag_d2h_count_;
    submission_events_.emplace_back(SubmissionEvent::kHostFlagCopy);
  }
  if (memcpy_async_error_ != ACL_ERROR_NONE && memcpy_async_count_ == memcpy_async_fail_on_count_ &&
      kind == memcpy_async_fail_kind_) {
    return memcpy_async_error_;
  }
  return llm::AclRuntimeStub::aclrtMemcpyAsync(dst, dest_max, src, src_count, kind, stream);
}

aclError FabricMemRuntimeStub::aclrtMemcpy(void *dst, size_t dest_max, const void *src, size_t count,
                                           aclrtMemcpyKind kind) {
  ++memcpy_count_;
  // Only block CheckRequestStatus downloads after a FabricMem batch kernel has been recorded.
  // Sync ADD/DELETE also do D2H copies and must not be stalled by this test hook.
  if (kind == ACL_MEMCPY_DEVICE_TO_HOST && block_status_download_.load(std::memory_order_acquire) &&
      !kernel_params_.empty()) {
    status_download_entered_.fetch_add(1U, std::memory_order_release);
    while (!unblock_status_download_.load(std::memory_order_acquire)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
  return llm::AclRuntimeStub::aclrtMemcpy(dst, dest_max, src, count, kind);
}

aclError FabricMemRuntimeStub::aclrtBinaryLoadFromFile(const char *path, aclrtBinaryLoadOptions *options,
                                                       aclrtBinHandle *bin_handle) {
  ++kernel_binary_load_count_;
  if (kernel_binary_load_error_ != ACL_ERROR_NONE) {
    return kernel_binary_load_error_;
  }
  return llm::AclRuntimeStub::aclrtBinaryLoadFromFile(path, options, bin_handle);
}

aclError FabricMemRuntimeStub::aclrtBinaryGetFunction(aclrtBinHandle bin_handle, const char *function_name,
                                                      aclrtFuncHandle *function_handle) {
  ++kernel_function_lookup_count_;
  if (kernel_function_lookup_error_ != ACL_ERROR_NONE) {
    return kernel_function_lookup_error_;
  }
  return llm::AclRuntimeStub::aclrtBinaryGetFunction(bin_handle, function_name, function_handle);
}

aclError FabricMemRuntimeStub::aclrtStreamGetId(aclrtStream stream, int32_t *stream_id) {
  if (stream == nullptr || stream_id == nullptr) {
    return ACL_ERROR_INVALID_PARAM;
  }
  // Keep the stubbed stream id inside the A3 SQE uint16 ABI range.
  *stream_id = static_cast<int32_t>(reinterpret_cast<uintptr_t>(stream) & 0xFFFFU);
  return ACL_SUCCESS;
}

aclError FabricMemRuntimeStub::aclrtCreateStreamWithConfig(aclrtStream *stream, uint32_t priority, uint32_t flag) {
  const aclError ret = llm::AclRuntimeStub::aclrtCreateStreamWithConfig(stream, priority, flag);
  if (ret == ACL_SUCCESS && stream != nullptr && *stream != nullptr) {
    stream_flags_[*stream] = flag;
  }
  return ret;
}

aclError FabricMemRuntimeStub::aclrtCreateNotify(aclrtNotify *notify, uint64_t flag) {
  (void)flag;
  if (notify == nullptr) {
    return ACL_ERROR_INVALID_PARAM;
  }
  ++notify_create_count_;
  if (notify_create_error_ != ACL_ERROR_NONE) {
    return notify_create_error_;
  }
  *notify = reinterpret_cast<aclrtNotify>(0x10000000U + notify_create_count_ * 0x100U);
  notify_ids_[*notify] = next_notify_id_++;
  return ACL_SUCCESS;
}

aclError FabricMemRuntimeStub::aclrtGetNotifyId(aclrtNotify notify, uint32_t *notify_id) {
  ++notify_id_query_count_;
  if (notify_id_error_ != ACL_ERROR_NONE) {
    return notify_id_error_;
  }
  const auto it = notify_ids_.find(notify);
  if (notify_id == nullptr || it == notify_ids_.end()) {
    return ACL_ERROR_INVALID_PARAM;
  }
  *notify_id = it->second;
  return ACL_SUCCESS;
}

aclError FabricMemRuntimeStub::aclrtDestroyNotify(aclrtNotify notify) {
  ++notify_destroy_count_;
  notify_ids_.erase(notify);
  return ACL_SUCCESS;
}

aclError FabricMemRuntimeStub::aclrtWaitAndResetNotify(aclrtNotify notify, aclrtStream stream, uint32_t timeout) {
  (void)notify;
  (void)stream;
  (void)timeout;
  ++notify_wait_count_;
  submission_events_.emplace_back(SubmissionEvent::kNotifyWait);
  return notify_wait_error_;
}

aclError FabricMemRuntimeStub::aclrtDestroyStream(aclrtStream stream) {
  stream_flags_.erase(stream);
  return llm::AclRuntimeStub::aclrtDestroyStream(stream);
}

aclError FabricMemRuntimeStub::aclrtLaunchKernelWithConfig(aclrtFuncHandle function, uint32_t block_dim,
                                                           aclrtStream stream, aclrtLaunchKernelCfg *config,
                                                           aclrtArgsHandle args, void *reserved) {
  ++kernel_launch_count_;
  submission_events_.emplace_back(SubmissionEvent::kKernelLaunch);
  if (kernel_launch_error_ != ACL_ERROR_NONE &&
      (kernel_launch_fail_on_count_ == 0U || kernel_launch_count_ == kernel_launch_fail_on_count_)) {
    return kernel_launch_error_;
  }
  return llm::AclRuntimeStub::aclrtLaunchKernelWithConfig(function, block_dim, stream, config, args, reserved);
}

aclError FabricMemRuntimeStub::aclrtLaunchKernelV2(aclrtFuncHandle function, uint32_t num_blocks, const void *args_data,
                                                   size_t args_size, aclrtLaunchKernelCfg *cfg, aclrtStream stream) {
  ++kernel_launch_count_;
  submission_events_.emplace_back(SubmissionEvent::kKernelLaunch);
  if (args_data != nullptr && args_size == sizeof(FabricMemAicpuKernelParam) &&
      static_cast<const FabricMemAicpuKernelParam *>(args_data)->version == kFabricMemKernelParamVersion) {
    kernel_params_.emplace_back(*static_cast<const FabricMemAicpuKernelParam *>(args_data));
  }
  if (kernel_launch_error_ != ACL_ERROR_NONE &&
      (kernel_launch_fail_on_count_ == 0U || kernel_launch_count_ == kernel_launch_fail_on_count_)) {
    return kernel_launch_error_;
  }
  return llm::AclRuntimeStub::aclrtLaunchKernelV2(function, num_blocks, args_data, args_size, cfg, stream);
}

aclError FabricMemRuntimeStub::aclrtKernelArgsAppend(aclrtArgsHandle args_handle, void *data, size_t size,
                                                     aclrtParamHandle *param_handle) {
  if (data != nullptr && size == sizeof(FabricMemAicpuKernelParam) &&
      static_cast<const FabricMemAicpuKernelParam *>(data)->version == kFabricMemKernelParamVersion) {
    kernel_params_.emplace_back(*static_cast<FabricMemAicpuKernelParam *>(data));
  }
  return llm::AclRuntimeStub::aclrtKernelArgsAppend(args_handle, data, size, param_handle);
}

aclError FabricMemRuntimeStub::aclrtFree(void *ptr) {
  ++free_count_;
  return llm::AclRuntimeStub::aclrtFree(ptr);
}

aclError FabricMemRuntimeStub::aclrtStreamQuery(aclrtStream stream, aclrtStreamStatus *status) {
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

aclError FabricMemRuntimeStub::aclrtSetStreamFailureMode(aclrtStream stream, uint64_t mode) {
  if (IsDeviceOnlyStream(stream)) {
    ++device_only_stream_failure_mode_count_;
    // Match runtime: DEVICE_USE_ONLY streams reject stop-on-failure (207000).
    return ACL_ERROR_RT_FEATURE_NOT_SUPPORT;
  }
  ++stream_failure_mode_count_;
  last_stream_failure_mode_ = mode;
  return ACL_ERROR_NONE;
}

aclError FabricMemRuntimeStub::aclrtSynchronizeStream(aclrtStream stream) {
  ++stream_sync_count_;
  if (IsDeviceOnlyStream(stream)) {
    ++device_only_stream_sync_count_;
  } else {
    stream_lifecycle_events_.emplace_back(StreamLifecycleEvent::kDrainControl);
  }
  streams_not_complete_.erase(stream);
  if (stream_sync_error_ != ACL_ERROR_NONE) {
    return stream_sync_error_;
  }
  return llm::AclRuntimeStub::aclrtSynchronizeStream(stream);
}

aclError FabricMemRuntimeStub::aclrtSynchronizeStreamWithTimeout(aclrtStream stream, int32_t timeout) {
  (void)timeout;
  sync_with_timeout_entered_.fetch_add(1U, std::memory_order_release);
  if (block_sync_with_timeout_.load(std::memory_order_acquire)) {
    while (!unblock_sync_with_timeout_.load(std::memory_order_acquire)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
  ++stream_sync_count_;
  if (IsDeviceOnlyStream(stream)) {
    ++device_only_stream_sync_count_;
  }
  streams_not_complete_.erase(stream);
  if (stream_sync_error_ != ACL_ERROR_NONE &&
      (stream_sync_fail_on_count_ == 0U || stream_sync_count_ == stream_sync_fail_on_count_)) {
    return stream_sync_error_;
  }
  return llm::AclRuntimeStub::aclrtSynchronizeStreamWithTimeout(stream, timeout);
}

aclError FabricMemRuntimeStub::aclrtStreamAbort(aclrtStream stream) {
  ++stream_abort_count_;
  if (IsDeviceOnlyStream(stream)) {
    ++device_only_stream_abort_count_;
  } else {
    stream_lifecycle_events_.emplace_back(StreamLifecycleEvent::kAbortControl);
  }
  return llm::AclRuntimeStub::aclrtStreamAbort(stream);
}

aclError FabricMemRuntimeStub::aclrtStreamStop(aclrtStream stream) {
  ++stream_stop_count_;
  if (IsDeviceOnlyStream(stream)) {
    ++device_only_stream_stop_count_;
    stream_lifecycle_events_.emplace_back(StreamLifecycleEvent::kStopWorker);
  }
  return llm::AclRuntimeStub::aclrtStreamStop(stream);
}

aclError FabricMemRuntimeStub::aclrtMallocPhysical(aclrtDrvMemHandle *handle, size_t size,
                                                   const aclrtPhysicalMemProp *prop, uint64_t flags) {
  (void)size;
  (void)flags;
  ++malloc_physical_count_;
  last_physical_mem_prop_ = *prop;
  *handle = reinterpret_cast<aclrtDrvMemHandle>(new uint8_t[8]);
  return ACL_ERROR_NONE;
}

aclError FabricMemRuntimeStub::aclrtFreePhysical(aclrtDrvMemHandle handle) {
  ++free_physical_count_;
  return llm::AclRuntimeStub::aclrtFreePhysical(handle);
}

aclError FabricMemRuntimeStub::aclrtMemGetAddressRange(void *ptr, void **pbase, size_t *psize) {
  ++get_address_range_count_;
  if (get_address_range_error_ != ACL_ERROR_NONE) {
    return get_address_range_error_;
  }
  if (ptr == nullptr || pbase == nullptr || psize == nullptr) {
    return ACL_ERROR_INVALID_PARAM;
  }
  const auto addr = reinterpret_cast<uintptr_t>(ptr);
  for (const auto &range : address_ranges_) {
    if (addr >= range.first && addr - range.first < range.second) {
      *pbase = reinterpret_cast<void *>(range.first);
      *psize = range.second;
      return ACL_ERROR_NONE;
    }
  }
  if (!address_ranges_.empty()) {
    return ACL_ERROR_INVALID_PARAM;
  }
  return llm::AclRuntimeStub::aclrtMemGetAddressRange(ptr, pbase, psize);
}

aclError FabricMemRuntimeStub::aclrtMemRetainAllocationHandle(void *devPtr, aclrtDrvMemHandle *handle) {
  ++retain_count_;
  retained_addresses_.emplace_back(reinterpret_cast<uintptr_t>(devPtr));
  if (retain_fail_on_count_ == retain_count_) {
    return ACL_ERROR_RT_INTERNAL_ERROR;
  }
  return llm::AclRuntimeStub::aclrtMemRetainAllocationHandle(devPtr, handle);
}

aclError FabricMemRuntimeStub::aclrtMemExportToShareableHandleV2(aclrtDrvMemHandle handle, uint64_t flags,
                                                                 aclrtMemSharedHandleType type, void *shareableHandle) {
  ++mem_export_count_;
  return llm::AclRuntimeStub::aclrtMemExportToShareableHandleV2(handle, flags, type, shareableHandle);
}

aclError FabricMemRuntimeStub::aclrtMemImportFromShareableHandleV2(void *shareableHandle, aclrtMemSharedHandleType type,
                                                                   uint64_t flags, aclrtDrvMemHandle *handle) {
  ++mem_import_count_;
  return llm::AclRuntimeStub::aclrtMemImportFromShareableHandleV2(shareableHandle, type, flags, handle);
}

void FabricMemRuntimeStub::SetAddressRanges(std::vector<std::pair<uintptr_t, size_t>> ranges) {
  address_ranges_ = std::move(ranges);
}

aclError FabricMemRuntimeStub::aclrtMemSetAccess(void *virPtr, size_t size, aclrtMemAccessDesc *desc, size_t count) {
  ++mem_set_access_count_;
  last_mem_set_access_size_ = size;
  last_mem_set_access_count_ = count;
  if (desc != nullptr && count > 0U) {
    last_mem_access_desc_ = desc[0];
  }
  return llm::AclRuntimeStub::aclrtMemSetAccess(virPtr, size, desc, count);
}

bool FabricMemRuntimeStub::IsDeviceOnlyStream(aclrtStream stream) const {
  const auto it = stream_flags_.find(stream);
  return it != stream_flags_.end() && it->second == ACL_STREAM_DEVICE_USE_ONLY;
}

void FabricMemTransferServiceTestBase::SetUp() {
  runtime_ = std::make_shared<FabricMemRuntimeStub>();
  scoped_runtime_ = std::make_unique<ScopedRuntimeMock>(runtime_);
}

void FabricMemTransferServiceTestBase::TearDown() {
  service_.Finalize();
  scoped_runtime_.reset();
  runtime_.reset();
}

Status FabricMemTransferServiceTestBase::InitService(size_t max_stream, size_t task_stream) {
  auto param = MakeServiceInitParam(&statistic_, &local_memory_);
  param.max_stream_num = max_stream;
  param.task_stream_num = task_stream;
  return service_.Initialize(param);
}

void FabricMemAicpuTransferServiceTestBase::SetUp() {
  runtime_ = std::make_shared<FabricMemRuntimeStub>();
  scoped_runtime_ = std::make_unique<ScopedRuntimeMock>(runtime_);
  runtime_->soc_name_ = "Ascend910_9391";
}

void FabricMemAicpuTransferServiceTestBase::TearDown() {
  service_.Finalize();
  scoped_runtime_.reset();
  runtime_.reset();
}

Status FabricMemAicpuTransferServiceTestBase::InitService(size_t max_stream, size_t task_stream) {
  auto param = MakeServiceInitParam(&statistic_, &local_memory_);
  param.max_stream_num = max_stream;
  param.task_stream_num = task_stream;
  param.enable_aicpu_unfold = true;
  return service_.Initialize(param);
}

}  // namespace hixl::fabric_mem_test
