/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_CS_HCOMM_HOST_CHANNEL_H_
#define CANN_HIXL_SRC_HIXL_CS_HCOMM_HOST_CHANNEL_H_

#include <cstddef>
#include <cstdint>
#include <vector>

#include "cs/hcomm_channel.h"

namespace hixl {

// Completion handle for the host (CPU) data plane: a index into the host flag queue and the address of
// the flag the peer writes when the batch completes.
struct CompleteHandleInfo {
  uint32_t magic;
  int32_t flag_index;
  uint64_t *flag_address;
};

// Host HCOMM channel. The data plane runs on the CPU engine (HcommProxy Read/Write Nbi on thread 0) and
// completion is observed by reading back a host flag queue entry that the peer's data-plane transfer
// fills in.
class HcommHostChannel : public HcommChannel {
 public:
  explicit HcommHostChannel(Endpoint &local_endpoint);
  ~HcommHostChannel() override;

  Status Initialize(const ChannelContext &ctx) override;
  Status Destroy() override;
  Status QueryCompleteStatus(void *complete_handle, HixlCompleteStatus *status) override;

 protected:
  Status DoBatchTransferSync(bool is_get, const HixlOneSideOpDesc *desc_list, uint32_t list_num,
                             uint32_t timeout_ms) override;
  Status DoBatchTransferAsync(bool is_get, const HixlOneSideOpDesc *desc_list, uint32_t list_num,
                              void **complete_handle) override;

 private:
  static constexpr size_t kFlagQueueSize = 4096;  // 用于初始化队列和内存地址列表

  Status InitFlagQueue() noexcept;
  int32_t AcquireFlagIndex();
  void ReleaseFlagIndex(int32_t flag_index);
  Status ReleaseCompleteHandle(CompleteHandleInfo *query_handle);
  Status CheckStatusHost(CompleteHandleInfo &query_handle, HixlCompleteStatus &status);
  Status TransferWithRetry(bool is_get, uint64_t channel_handle, void *dst_buf, const void *src_buf,
                           uint64_t len) const;
  Status BatchTransferTask(bool is_get, uint32_t list_num, const HixlOneSideOpDesc *desc_list) const;
  void ReleaseLegacyHandles();

  // Only the client data plane uses the queue, so its bookkeeping is sized lazily in InitFlagQueue.
  // The server reuses this type for its per-connection channels but never calls Initialize.
  uint64_t *flag_queue_{nullptr};
  MemHandle flag_mem_handle_{nullptr};
  std::vector<uint32_t> available_indices_{};
  size_t top_index_{0};                               // 栈顶指针
  std::vector<CompleteHandleInfo *> live_handles_{};  // 用来记录读写生成的 query_handle
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_CS_HCOMM_HOST_CHANNEL_H_
