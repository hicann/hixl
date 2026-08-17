/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_TESTS_CPP_HIXL_FABRIC_MEM_FABRIC_MEM_RUNTIME_STUB_H_
#define CANN_HIXL_TESTS_CPP_HIXL_FABRIC_MEM_FABRIC_MEM_RUNTIME_STUB_H_

#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#define private public
#define protected public
#include "engine/fabric_mem_engine.h"
#include "engine/hixl_options.h"
#include "fabric_mem/fabric_mem_aicpu_dispatcher.h"
#include "fabric_mem/fabric_mem_aicpu_transfer_service.h"
#include "fabric_mem/fabric_mem_channel_manager.h"
#include "fabric_mem/fabric_mem_control.h"
#include "fabric_mem/fabric_mem_host_transfer_service.h"
#include "fabric_mem/fabric_mem_memory.h"
#include "fabric_mem/fabric_mem_slot_pool.h"
#include "fabric_mem/fabric_mem_statistic.h"
#include "fabric_mem/fabric_mem_transfer_service.h"
#undef protected
#undef private

#include "common/hixl_utils.h"
#include "depends/ascendcl/src/ascendcl_stub.h"
#include "fabric_mem_test_utils.h"
#include "fabric_mem/virtual_memory_manager.h"
#include "hixl/hixl_types.h"

namespace hixl::fabric_mem_test {
constexpr char kChannelId[] = "fabric_mem_test_channel";
constexpr char kStatChannelId[] = "fabric_mem_stat_channel";
constexpr uintptr_t kLocalAddr = 0x100000UL;
constexpr uintptr_t kRemoteOldAddr = 0x200000UL;
constexpr uintptr_t kRemoteNewAddr = 0x300000UL;
constexpr uintptr_t kImportedLocalAddr = 0x400000UL;
constexpr size_t kLen = 32U;
constexpr uint32_t kFabricMemMagic = 0xA4B3C2D1;
constexpr int32_t kClientTimeoutMs = 10;
constexpr uint32_t kCaptureLogTimeoutMs = 1000U;
constexpr char kConfigEngineLocalId[] = "127.0.0.1:28000";
constexpr char kConfigRemoteEngineId[] = "127.0.0.1:28001";
constexpr size_t k1GB = 1024UL * 1024UL * 1024UL;

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
  enum class StreamLifecycleEvent { kAbortControl, kDrainControl, kStopWorker };
  enum class SubmissionEvent { kKernelLaunch, kNotifyWait, kHostFlagCopy };

  aclError aclrtSetCurrentContext(aclrtContext context) override;
  aclError aclrtGetCurrentContext(aclrtContext *context) override;
  aclError aclrtPointerGetAttributes(const void *ptr, aclrtPtrAttributes *attributes) override;
  const char *aclrtGetSocName() override;
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
  aclError aclrtMemImportFromShareableHandleV2(void *shareableHandle, aclrtMemSharedHandleType type, uint64_t flags,
                                               aclrtDrvMemHandle *handle) override;
  aclError aclrtMemSetAccess(void *virPtr, size_t size, aclrtMemAccessDesc *desc, size_t count) override;
  bool IsDeviceOnlyStream(aclrtStream stream) const;
  void SetAddressRanges(std::vector<std::pair<uintptr_t, size_t>> ranges);

  bool pointer_is_host_{false};
  bool get_context_returns_null_{false};
  aclError pointer_attr_error_{ACL_ERROR_NONE};
  aclError memcpy_async_error_{ACL_ERROR_NONE};
  aclrtMemcpyKind memcpy_async_fail_kind_{ACL_MEMCPY_DEVICE_TO_DEVICE};
  size_t memcpy_async_fail_on_count_{0U};
  size_t memcpy_async_count_{0U};
  size_t memcpy_count_{0U};
  size_t host_flag_d2h_count_{0U};
  aclError kernel_binary_load_error_{ACL_ERROR_NONE};
  aclError kernel_function_lookup_error_{ACL_ERROR_NONE};
  aclError kernel_launch_error_{ACL_ERROR_NONE};
  // When non-zero, kernel_launch_error_ applies only on this 1-based launch count.
  size_t kernel_launch_fail_on_count_{0U};
  size_t kernel_binary_load_count_{0U};
  size_t kernel_function_lookup_count_{0U};
  size_t kernel_launch_count_{0U};
  size_t notify_create_count_{0U};
  size_t notify_id_query_count_{0U};
  size_t notify_destroy_count_{0U};
  size_t notify_wait_count_{0U};
  uint32_t next_notify_id_{1U};
  aclError notify_create_error_{ACL_ERROR_NONE};
  aclError notify_id_error_{ACL_ERROR_NONE};
  aclError notify_wait_error_{ACL_ERROR_NONE};
  std::unordered_map<aclrtNotify, uint32_t> notify_ids_;
  std::vector<FabricMemAicpuKernelParam> kernel_params_;
  std::vector<SubmissionEvent> submission_events_;
  size_t free_count_{0U};
  std::string soc_name_;
  size_t stream_query_count_{0U};
  size_t stream_failure_mode_count_{0U};
  uint64_t last_stream_failure_mode_{0U};
  size_t device_only_stream_failure_mode_count_{0U};
  size_t stream_abort_count_{0U};
  size_t device_only_stream_abort_count_{0U};
  size_t stream_stop_count_{0U};
  size_t device_only_stream_stop_count_{0U};
  aclError stream_query_error_{ACL_ERROR_NONE};
  aclError stream_sync_error_{ACL_ERROR_NONE};
  // When non-zero, stream_sync_error_ applies only on this 1-based SynchronizeStreamWithTimeout count.
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
  aclError get_address_range_error_{ACL_ERROR_NONE};
  std::vector<uintptr_t> retained_addresses_;
  std::vector<std::pair<uintptr_t, size_t>> address_ranges_;
  size_t mem_set_access_count_{0U};
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

inline FabricMemTransferContext BuildContext(std::shared_ptr<FabricMemTransferStatisticInfo> stat_info = nullptr) {
  FabricMemTransferContext context;
  context.channel_id = kChannelId;
  context.statistic_channel_id = kStatChannelId;
  context.remote_va_to_old_va.emplace(kRemoteNewAddr, VaInfo{kRemoteOldAddr, kLen * 4U});
  context.stat_info = std::move(stat_info);
  return context;
}

inline std::vector<TransferOpDesc> BuildOpDescs(uint8_t *local, uint8_t *remote) {
  return {{reinterpret_cast<uintptr_t>(local), reinterpret_cast<uintptr_t>(remote), kLen}};
}

inline std::vector<TransferOpDesc> BuildTwoOpDescs(uint8_t *local, uint8_t *remote) {
  return {{reinterpret_cast<uintptr_t>(local), reinterpret_cast<uintptr_t>(remote), kLen},
          {reinterpret_cast<uintptr_t>(local + kLen), reinterpret_cast<uintptr_t>(remote + kLen), kLen}};
}

inline void SendAdxlHeartBeat(int32_t fd) {
  ASSERT_EQ(FabricMemControlClient::SendHeartBeat(fd), SUCCESS);
}

inline void SendRawAdxlMsg(int32_t fd, uint32_t magic, int32_t msg_type, const std::string &payload = "") {
  const uint64_t body_size = static_cast<uint64_t>(sizeof(msg_type)) + payload.size();
  const FabricMemAdxlProtocolHeader header{magic, body_size};
  ASSERT_EQ(send(fd, &header, sizeof(header), 0), static_cast<ssize_t>(sizeof(header)));
  ASSERT_EQ(send(fd, &msg_type, sizeof(msg_type), 0), static_cast<ssize_t>(sizeof(msg_type)));
  if (!payload.empty()) {
    ASSERT_EQ(send(fd, payload.data(), payload.size(), 0), static_cast<ssize_t>(payload.size()));
  }
}

inline void DrainSocketInBackground(int32_t fd) {
  std::thread([fd]() {
    char buffer[512];
    constexpr int kMaxDrainLoops = 1024;
    for (int i = 0; i < kMaxDrainLoops; ++i) {
      if (recv(fd, buffer, sizeof(buffer), 0) <= 0) {
        break;
      }
    }
  }).detach();
}

inline bool StartDefaultShareHandleServer(FabricMemControlServer &server, int32_t &port, std::string &remote,
                                          bool auto_cleanup_enabled = true) {
  port = test::AllocateFabricMemTestPort();
  if (port <= 0) {
    return false;
  }
  remote = "127.0.0.1:" + std::to_string(port);
  return server.Start(
             remote,
             [](std::vector<ShareHandleInfo> &handles) {
               handles.emplace_back(BuildShareHandle());
               return SUCCESS;
             },
             auto_cleanup_enabled) == SUCCESS;
}

inline Status FetchFabricMemClientConn(const std::string &remote, const std::string &channel_id, int32_t &conn_fd) {
  std::vector<ShareHandleInfo> handles;
  return FabricMemControlClient::Fetch(remote, channel_id, kClientTimeoutMs, handles, conn_fd);
}

inline int32_t RecvRawFabricMemMsg(int32_t fd, std::string &payload) {
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

inline void RegisterServiceAsyncRecord(FabricMemTransferService &service,
                                       const std::shared_ptr<FabricMemChannel> &channel,
                                       const FabricMemTransferContext &context, TransferReq req, AsyncSlot &&slot,
                                       uint64_t transfer_bytes, uint64_t op_desc_count, TransferOp op = WRITE) {
  const uint64_t req_id = reinterpret_cast<uintptr_t>(req);
  AsyncRecord record;
  record.slot = std::move(slot);
  const auto start = std::chrono::steady_clock::now();
  record.transfer_start = start;
  record.real_copy_start = start;
  record.transfer_bytes = transfer_bytes;
  record.op_desc_count = op_desc_count;
  record.channel_id = context.channel_id;
  record.statistic_channel_id = context.statistic_channel_id;
  record.stat_info = context.stat_info;
  record.op_type = op;
  {
    std::lock_guard<std::mutex> lock(channel->records_mutex);
    channel->async_records[req_id] = std::move(record);
  }
  service.channel_manager_.AddReqRoute(req_id, channel);
}

inline std::shared_ptr<FabricMemChannel> AddServiceChannel(FabricMemTransferService &service,
                                                           const std::string &remote) {
  auto channel = std::make_shared<FabricMemChannel>();
  std::lock_guard<std::mutex> lock(service.channel_manager_.channels_mutex_);
  service.channel_manager_.channels_[remote] = channel;
  return channel;
}

inline std::shared_ptr<FabricMemChannel> AddMappedServiceChannel(FabricMemTransferService &service,
                                                                 const std::string &remote, uint8_t *remote_buf,
                                                                 size_t len) {
  auto channel = std::make_shared<FabricMemChannel>();
  channel->remote_memory = std::make_unique<FabricMemRemoteMemory>();
  EXPECT_EQ(channel->remote_memory->Import({BuildShareHandle(reinterpret_cast<uintptr_t>(remote_buf), len)}, 0),
            SUCCESS);
  std::lock_guard<std::mutex> lock(service.channel_manager_.channels_mutex_);
  service.channel_manager_.channels_[remote] = channel;
  return channel;
}

inline FabricMemTransferServiceInitParam MakeServiceInitParam(FabricMemStatistic *statistic,
                                                              FabricMemLocalMemory *local_memory) {
  FabricMemTransferServiceInitParam param;
  param.device_id = 0;
  param.max_stream_num = 4U;
  param.task_stream_num = 1U;
  param.local_engine = "127.0.0.1:0";
  param.statistic = statistic;
  param.local_memory = local_memory;
  return param;
}

inline FabricMemChannelManagerInitParam MakeManagerInitParam(FabricMemStatistic *statistic,
                                                             FabricMemSlotPool *slot_pool) {
  FabricMemChannelManagerInitParam param;
  param.local_engine = "127.0.0.1:0";
  param.statistic = statistic;
  param.slot_pool = slot_pool;
  return param;
}

inline void AttachTestContext(FabricMemEngine &engine) {
  auto ctx_holder = std::make_shared<int>(1);
  engine.aclrt_context_holder_ = std::static_pointer_cast<void>(ctx_holder);
  engine.aclrt_context_ = reinterpret_cast<aclrtContext>(ctx_holder.get());
  engine.is_initialized_ = true;
  // FabricMemEngineUTest covers the host path; AICPU unfold has dedicated UTs.
  engine.fabric_mem_config_.enable_aicpu_unfold = false;
  if (engine.fabric_mem_transfer_service_ == nullptr) {
    auto service = std::make_shared<FabricMemHostTransferService>();
    auto param = MakeServiceInitParam(&engine.fabric_mem_statistic_, &engine.local_memory_);
    param.auto_connect = engine.auto_connect_;
    param.aclrt_context = engine.aclrt_context_;
    param.enable_aicpu_unfold = false;
    ASSERT_EQ(service->Initialize(param), SUCCESS);
    engine.fabric_mem_transfer_service_ = service;
  }
}

inline FabricMemChannelManager &EngineManager(FabricMemEngine &engine) {
  return engine.fabric_mem_transfer_service_->channel_manager_;
}

inline std::shared_ptr<FabricMemChannel> AddEngineChannel(FabricMemEngine &engine, const std::string &remote) {
  auto channel = std::make_shared<FabricMemChannel>();
  std::lock_guard<std::mutex> lock(EngineManager(engine).channels_mutex_);
  EngineManager(engine).channels_[remote] = channel;
  return channel;
}

inline std::map<AscendString, AscendString> BuildFabricMemOptions() {
  std::map<AscendString, AscendString> options;
  options[OPTION_ENABLE_USE_FABRIC_MEM] = AscendString("1");
  // Engine init UTs cover the host path; production default enable_aicpu_unfold is true.
  options[OPTION_GLOBAL_RESOURCE_CONFIG] = AscendString(R"({"fabric_memory":{"enable_aicpu_unfold":false}})");
  return options;
}

inline Status InitEngineWithOptions(FabricMemEngine &engine, const std::map<AscendString, AscendString> &raw_options) {
  HixlOptions parsed;
  HIXL_CHK_STATUS_RET(HixlOptions::Parse(raw_options, parsed), "Failed to parse options");
  return engine.Initialize(parsed);
}

class FabricMemTransferServiceTestBase : public ::testing::Test {
 protected:
  void SetUp() override;
  void TearDown() override;
  Status InitService(size_t max_stream, size_t task_stream);

  std::shared_ptr<FabricMemRuntimeStub> runtime_;
  std::unique_ptr<ScopedRuntimeMock> scoped_runtime_;
  FabricMemStatistic statistic_;
  FabricMemLocalMemory local_memory_;
  FabricMemHostTransferService service_;
};

class FabricMemAicpuTransferServiceTestBase : public ::testing::Test {
 protected:
  void SetUp() override;
  void TearDown() override;
  Status InitService(size_t max_stream, size_t task_stream);

  std::shared_ptr<FabricMemRuntimeStub> runtime_;
  std::unique_ptr<ScopedRuntimeMock> scoped_runtime_;
  FabricMemStatistic statistic_;
  FabricMemLocalMemory local_memory_;
  FabricMemAicpuTransferService service_;
};

class FabricMemSlotPoolUTest : public ::testing::Test {
 protected:
  void SetUp() override {
    runtime_ = std::make_shared<FabricMemRuntimeStub>();
    scoped_runtime_ = std::make_unique<ScopedRuntimeMock>(runtime_);
  }

  void TearDown() override {
    pool_.AbortAndDestroyAll();
    scoped_runtime_.reset();
    runtime_.reset();
  }

  std::shared_ptr<FabricMemRuntimeStub> runtime_;
  std::unique_ptr<ScopedRuntimeMock> scoped_runtime_;
  FabricMemSlotPool pool_;
};

class FabricMemLocalMemoryUTest : public ::testing::Test {
 protected:
  void SetUp() override {
    runtime_ = std::make_shared<FabricMemRuntimeStub>();
    runtime_->SetAddressRanges({{kLocalAddr, kLen}});
    scoped_runtime_ = std::make_unique<ScopedRuntimeMock>(runtime_);
    VirtualMemoryManager::GetInstance().Finalize();
    ASSERT_EQ(VirtualMemoryManager::GetInstance().Initialize(), SUCCESS);
  }

  void TearDown() override {
    local_memory_.Finalize();
    VirtualMemoryManager::GetInstance().Finalize();
    scoped_runtime_.reset();
    runtime_.reset();
  }

  std::shared_ptr<FabricMemRuntimeStub> runtime_;
  std::unique_ptr<ScopedRuntimeMock> scoped_runtime_;
  FabricMemLocalMemory local_memory_;
};

class FabricMemChannelManagerUTest : public ::testing::Test {
 protected:
  void SetUp() override {
    runtime_ = std::make_shared<FabricMemRuntimeStub>();
    scoped_runtime_ = std::make_unique<ScopedRuntimeMock>(runtime_);
    ASSERT_EQ(slot_pool_.Initialize(0, 4U, 1U), SUCCESS);
    ASSERT_EQ(manager_.Initialize(MakeManagerInitParam(&statistic_, &slot_pool_)), SUCCESS);
  }

  void TearDown() override {
    manager_.Finalize();
    slot_pool_.AbortAndDestroyAll();
    scoped_runtime_.reset();
    runtime_.reset();
  }

  std::shared_ptr<FabricMemChannel> AddChannel(const std::string &remote) {
    auto channel = std::make_shared<FabricMemChannel>();
    std::lock_guard<std::mutex> lock(manager_.channels_mutex_);
    manager_.channels_[remote] = channel;
    return channel;
  }

  std::shared_ptr<FabricMemRuntimeStub> runtime_;
  std::unique_ptr<ScopedRuntimeMock> scoped_runtime_;
  FabricMemStatistic statistic_;
  FabricMemSlotPool slot_pool_;
  FabricMemChannelManager manager_;
};

class FabricMemEngineInitUTest : public ::testing::Test {
 protected:
  void SetUp() override {
    runtime_ = std::make_shared<FabricMemRuntimeStub>();
    scoped_runtime_ = std::make_unique<ScopedRuntimeMock>(runtime_);
  }

  void TearDown() override {
    scoped_runtime_.reset();
    runtime_.reset();
  }

  std::shared_ptr<FabricMemRuntimeStub> runtime_;
  std::unique_ptr<ScopedRuntimeMock> scoped_runtime_;
};
}  // namespace hixl::fabric_mem_test

#endif  // CANN_HIXL_TESTS_CPP_HIXL_FABRIC_MEM_FABRIC_MEM_RUNTIME_STUB_H_
