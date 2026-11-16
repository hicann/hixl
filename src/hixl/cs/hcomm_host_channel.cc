/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm_host_channel.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <thread>

#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/scope_guard.h"
#include "cs/global_config.h"
#include "proxy/hcomm_proxy.h"

namespace hixl {
namespace {
constexpr uint32_t kRoceCompleteMagic = 0x524F4345U;
constexpr uint32_t kFlagSizeBytes = 8;
constexpr uint64_t kFlagDoneValue = 1ULL;
constexpr uint64_t kFlagResetValue = 0ULL;
constexpr uint32_t kMaxHostBatchSize = 128U;
constexpr auto kSyncQueryIntervalUs = 10U;
}  // namespace

HcommHostChannel::HcommHostChannel(Endpoint &local_endpoint) : HcommChannel(local_endpoint) {}

HcommHostChannel::~HcommHostChannel() {
  (void)Destroy();
}

Status HcommHostChannel::Initialize(const ChannelContext &ctx) {
  HIXL_CHK_STATUS_RET(HcommChannel::Initialize(ctx), "[Channel] base Initialize failed");
  return InitFlagQueue();
}

Status HcommHostChannel::Destroy() {
  ReleaseLegacyHandles();
  if (flag_mem_handle_ != nullptr) {
    const Status unreg_ret = UnregMem(flag_mem_handle_);
    if (unreg_ret != SUCCESS) {
      HIXL_LOGW("[HcommHostChannel] Deregister flag mem failed in Destroy, handle:%p, ret:%u", flag_mem_handle_,
                static_cast<uint32_t>(unreg_ret));
    }
    flag_mem_handle_ = nullptr;
  }
  if (flag_queue_ != nullptr) {
    free(flag_queue_);
    flag_queue_ = nullptr;
  }
  return HcommChannel::Destroy();
}

Status HcommHostChannel::InitFlagQueue() noexcept {
  if (flag_queue_ != nullptr) {
    return SUCCESS;  // 已初始化
  }
  void *tmp = malloc(kFlagQueueSize * sizeof(uint64_t));
  HIXL_DISMISSABLE_GUARD(free_flag_mem, [&tmp]() {
    if (tmp != nullptr) {
      free(tmp);
      tmp = nullptr;
    }
  });
  HIXL_CHK_BOOL_RET_STATUS(tmp != nullptr, FAILED, "Call api:malloc failed, size:%zu bytes",
                           kFlagQueueSize * sizeof(uint64_t));
  auto *flag_queue = static_cast<uint64_t *>(tmp);
  for (size_t i = 0; i < kFlagQueueSize; ++i) {
    flag_queue[i] = 0;
  }
  CommMem mem{};
  mem.type = COMM_MEM_TYPE_HOST;
  mem.addr = flag_queue;
  mem.size = kFlagQueueSize * sizeof(uint64_t);
  MemHandle flag_handle = nullptr;
  HIXL_CHK_STATUS_RET(RegMem(kBuiltinHostTransFlagName, &mem, &flag_handle),
                      "Failed to reg HOST trans finished flag, mem.addr: %p, mem.size: %lu.", mem.addr, mem.size);
  flag_queue_ = flag_queue;
  flag_mem_handle_ = flag_handle;
  available_indices_.resize(kFlagQueueSize);
  for (size_t i = 0U; i < kFlagQueueSize; ++i) {
    available_indices_[i] = static_cast<uint32_t>(i);
  }
  live_handles_.assign(kFlagQueueSize, nullptr);
  top_index_ = kFlagQueueSize;
  HIXL_DISMISS_GUARD(free_flag_mem);
  return SUCCESS;
}

int32_t HcommHostChannel::AcquireFlagIndex() {
  if (top_index_ == 0U) {
    return -1;
  }
  --top_index_;
  return static_cast<int32_t>(available_indices_[top_index_]);
}

void HcommHostChannel::ReleaseFlagIndex(int32_t flag_index) {
  if (top_index_ < available_indices_.size()) {
    available_indices_[top_index_] = static_cast<uint32_t>(flag_index);
    flag_queue_[flag_index] = kFlagResetValue;  // 将flag重置为0
    ++top_index_;
  }
}

Status HcommHostChannel::ReleaseCompleteHandle(CompleteHandleInfo *query_handle) {
  HIXL_CHECK_NOTNULL(query_handle);
  if (top_index_ < live_handles_.size()) {
    ReleaseFlagIndex(query_handle->flag_index);
    live_handles_[query_handle->flag_index] = nullptr;
  }
  delete query_handle;
  return SUCCESS;
}

Status HcommHostChannel::TransferWithRetry(bool is_get, uint64_t channel_handle, void *dst_buf, const void *src_buf,
                                           uint64_t len) const {
  constexpr int64_t kRetryTimeoutMs = 20 * 60 * 1000;  // 20 minutes in milliseconds

  auto start_time = std::chrono::steady_clock::now();
  int32_t hccl_ret = HCCL_SUCCESS;

  while (true) {
    if (is_get) {
      hccl_ret = HcommProxy::ReadNbiOnThread(static_cast<ThreadHandle>(0), channel_handle, dst_buf, src_buf, len);
    } else {
      hccl_ret = HcommProxy::WriteNbiOnThread(static_cast<ThreadHandle>(0), channel_handle, dst_buf, src_buf, len);
    }

    if (hccl_ret == HCCL_SUCCESS) {
      return SUCCESS;
    }

    if (hccl_ret != HCCL_E_AGAIN) {
      HIXL_CHK_HCCL_RET(static_cast<HcclResult>(hccl_ret),
                        "[HcommHostChannel] Transfer failed, is_get:%d, channel_handle:%lu, dst_addr:%p, src_addr:%p, "
                        "mem_len:%lu bytes",
                        static_cast<int32_t>(is_get), channel_handle, dst_buf, src_buf, len);
    }

    // 检查超时
    auto current_time = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - start_time).count();
    HIXL_CHK_BOOL_RET_STATUS(elapsed_ms < kRetryTimeoutMs, TIMEOUT,
                             "[HcommHostChannel] Transfer timed out, elapsed:%ld ms, is_get:%d, channel_handle:%lu, "
                             "dst_addr:%p, src_addr:%p, mem_len:%lu bytes, hccl_ret:%d",
                             elapsed_ms, is_get, channel_handle, dst_buf, src_buf, len, hccl_ret);

    HIXL_LOGW(
        "[HcommHostChannel] Transfer ret=%d, retrying. elapsed_ms=%ld, dst_addr=%p, src_addr=%p, len=%lu, is_get=%d.",
        hccl_ret, elapsed_ms, dst_buf, src_buf, len, is_get);

    // 执行 Fence 后重试，执行Fence后通常极少会出现再次重试的问题
    HIXL_CHK_HCCL_RET(
        static_cast<HcclResult>(HcommProxy::ChannelFenceOnThread(static_cast<ThreadHandle>(0), channel_handle)),
        "[HcommHostChannel] channel_handle:%lu", channel_handle);
  }
}

Status HcommHostChannel::BatchTransferTask(bool is_get, uint32_t list_num, const HixlOneSideOpDesc *desc_list) const {
  for (uint32_t i = 0; i < list_num; i++) {
    void *dst = is_get ? desc_list[i].local_buf : desc_list[i].remote_buf;
    const void *src = is_get ? desc_list[i].remote_buf : desc_list[i].local_buf;
    HIXL_CHK_STATUS_RET(TransferWithRetry(is_get, GetHandle(), dst, src, desc_list[i].len),
                        "[HcommHostChannel] TransferWithRetry failed, is_get:%d, channel_handle:%lu, size:%lu bytes",
                        static_cast<int32_t>(is_get), GetHandle(), desc_list[i].len);
  }
  HIXL_CHK_HCCL_RET(
      static_cast<HcclResult>(HcommProxy::ChannelFenceOnThread(static_cast<ThreadHandle>(0), GetHandle())),
      "[HcommHostChannel] channel_handle:%lu", GetHandle());
  return SUCCESS;
}

Status HcommHostChannel::DoBatchTransferAsync(bool is_get, const HixlOneSideOpDesc *desc_list, uint32_t list_num,
                                              void **query_handle) {
  HIXL_CHK_BOOL_RET_STATUS(list_num > 0U, PARAM_INVALID, "[HcommHostChannel] list_num must be > 0");
  HIXL_CHK_BOOL_RET_STATUS(remote_trans_flag_.addr != nullptr, PARAM_INVALID,
                           "[HcommHostChannel] builtin trans flag is not set, call GetRemoteMem before transfer");
  const uint32_t batch_size =
      ctx_.configurable_transfer_supported ? ctx_.global_config->MaxTransferCountPerBatch() : kMaxHostBatchSize;
  const uint32_t num_chunks = list_num / batch_size + static_cast<uint32_t>(list_num % batch_size != 0U);
  for (uint32_t chunk_idx = 0U; chunk_idx < num_chunks; ++chunk_idx) {
    uint32_t chunk_offset = chunk_idx * batch_size;
    uint32_t chunk_size = std::min(batch_size, list_num - chunk_offset);
    HIXL_CHK_STATUS_RET(BatchTransferTask(is_get, chunk_size, desc_list + chunk_offset),
                        "[HcommHostChannel] BatchTransferTask failed for chunk %u/%u", chunk_idx, num_chunks);
  }
  int32_t flag_index = AcquireFlagIndex();
  if (flag_index == -1) {
    HIXL_LOGE(RESOURCE_EXHAUSTED,
              "There are a large number of transfer tasks with no query results, making it impossible to create new "
              "transfer tasks. Please first call HixlCSClientQueryCompleteStatus to check whether the transfer tasks "
              "that have been created are completed, and then create new transfer tasks.");
    return RESOURCE_EXHAUSTED;
  }
  // 使用 scope_guard 自动管理 flag 资源的释放
  HIXL_DISMISSABLE_GUARD(flag_guard, ([this, flag_index]() { ReleaseFlagIndex(flag_index); }));
  uint64_t *flag_addr = &flag_queue_[flag_index];
  HIXL_CHK_STATUS_RET(
      TransferWithRetry(true, GetHandle(), flag_addr, remote_trans_flag_.addr, kFlagSizeBytes),
      "[HcommHostChannel] Transfer completion flag failed, channel_handle:%lu, dst_addr:%p, src_addr:%p, size:%u bytes",
      GetHandle(), flag_addr, remote_trans_flag_.addr, kFlagSizeBytes);
  auto *query_mem_handle = new (std::nothrow) CompleteHandleInfo();
  if (query_mem_handle == nullptr) {
    HIXL_LOGE(FAILED, "Memory allocate failed; unable to generate query handle.");
    return FAILED;
  }
  query_mem_handle->magic = kRoceCompleteMagic;
  query_mem_handle->flag_index = flag_index;
  query_mem_handle->flag_address = flag_addr;
  // 需要先创建query_handle实体，之后再传给指针。
  *query_handle = query_mem_handle;
  live_handles_[flag_index] = query_mem_handle;
  // 成功后 dismiss guard，避免重复释放
  HIXL_DISMISS_GUARD(flag_guard);
  return SUCCESS;
}

Status HcommHostChannel::DoBatchTransferSync(bool is_get, const HixlOneSideOpDesc *desc_list, uint32_t list_num,
                                             uint32_t timeout_ms) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  void *raw_handle = nullptr;
  HIXL_CHK_STATUS_RET(DoBatchTransferAsync(is_get, desc_list, list_num, &raw_handle),
                      "[HcommHostChannel] DoBatchTransferAsync failed");
  HIXL_CHECK_NOTNULL(raw_handle);
  while (true) {
    if (std::chrono::steady_clock::now() >= deadline) {
      (void)ReleaseCompleteHandle(static_cast<CompleteHandleInfo *>(raw_handle));
      HIXL_LOGE(TIMEOUT, "[HcommHostChannel] BatchTransferHostSync timeout after %u ms", timeout_ms);
      return TIMEOUT;
    }
    HixlCompleteStatus st = HixlCompleteStatus::HIXL_COMPLETE_STATUS_WAITING;
    const Status cs = CheckStatusHost(*static_cast<CompleteHandleInfo *>(raw_handle), st);
    if (cs != SUCCESS) {
      (void)ReleaseCompleteHandle(static_cast<CompleteHandleInfo *>(raw_handle));
      return cs;
    }
    if (st == HixlCompleteStatus::HIXL_COMPLETE_STATUS_COMPLETED) {
      return SUCCESS;
    }
    std::this_thread::sleep_for(std::chrono::microseconds(kSyncQueryIntervalUs));
  }
}

Status HcommHostChannel::CheckStatusHost(CompleteHandleInfo &query_handle, HixlCompleteStatus &status) {
  // 检验query_handle中的序号是否合规
  if (query_handle.flag_index < 0 || query_handle.flag_index >= static_cast<int32_t>(kFlagQueueSize)) {
    HIXL_LOGE(PARAM_INVALID,
              "The value of query_handle->flag_index is outside the valid verification range; please check the "
              "query_handle. query_handle->flag_index:%d",
              query_handle.flag_index);
    return PARAM_INVALID;
  }
  // 通过读取query_handle中地址的值，来判断任务的完成状态
  HIXL_CHECK_NOTNULL(query_handle.flag_address);
  volatile uint64_t *flag_ptr = query_handle.flag_address;
  const uint64_t flag_val = *flag_ptr;
  // 查到flag变成1之后，就把其重置为0，之后告知用户读写任务已经完成。
  if (flag_val == kFlagDoneValue) {
    status = HixlCompleteStatus::HIXL_COMPLETE_STATUS_COMPLETED;
    HIXL_LOGI("The current transmission task has been completed.");
    return ReleaseCompleteHandle(&query_handle);  // 释放内存并回收索引
  }
  status = HixlCompleteStatus::HIXL_COMPLETE_STATUS_WAITING;
  HIXL_LOGI("The current transmission task has not been completed.");
  return SUCCESS;
}

Status HcommHostChannel::QueryCompleteStatus(void *complete_handle, HixlCompleteStatus *status) {
  HIXL_CHECK_NOTNULL(complete_handle);
  HIXL_CHECK_NOTNULL(status);
  auto *handle = static_cast<CompleteHandleInfo *>(complete_handle);
  HIXL_CHK_BOOL_RET_STATUS(handle->magic == kRoceCompleteMagic, PARAM_INVALID,
                           "[HcommHostChannel] CheckStatus bad magic=0x%X", handle->magic);
  return CheckStatusHost(*handle, *status);
}

void HcommHostChannel::ReleaseLegacyHandles() {
  uint32_t live_cnt = 0U;
  for (size_t i = 0U; i < live_handles_.size(); ++i) {
    if (live_handles_[i] != nullptr) {
      live_cnt += 1U;
    }
  }
  if (live_cnt > 0U) {
    HIXL_LOGW("[HcommHostChannel] Destroy: %u legacy complete_handle still live. Force releasing them.", live_cnt);
    for (size_t i = 0U; i < live_handles_.size(); ++i) {
      if (live_handles_[i] != nullptr) {
        delete live_handles_[i];
        live_handles_[i] = nullptr;
      }
    }
    for (size_t i = 0U; i < available_indices_.size(); ++i) {
      available_indices_[i] = static_cast<uint32_t>(i);
    }
    top_index_ = available_indices_.size();
  }
}

}  // namespace hixl
