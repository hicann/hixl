/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_GRAPH_ENGINE_HCCS_TRANSFER_SERVICE_H
#define CANN_GRAPH_ENGINE_HCCS_TRANSFER_SERVICE_H

#include <cstdint>
#include <future>
#include <utility>
#include <vector>
#include "adxl/adxl_types.h"
#include "channel.h"
#include "control_msg_handler.h"
#include "runtime/rt.h"

namespace adxl {
using AsyncResource = std::pair<rtStream_t, rtEvent_t>;
struct AsyncRecord {
  std::vector<AsyncResource> async_resources;
  std::chrono::steady_clock::time_point real_start;
};
class FabricMemTransferService {
 public:
  FabricMemTransferService() = default;

  Status Initialize(size_t max_stream_num);

  void Finalize();

  Status RegisterMem(const MemDesc &mem, MemType type, MemHandle &mem_handle);

  Status DeregisterMem(MemHandle mem_handle);

  Status Transfer(const ChannelPtr &channel, TransferOp operation, const std::vector<TransferOpDesc> &op_descs,
                  int32_t timeout_in_millis);

  Status TransferAsync(const ChannelPtr &channel, TransferOp operation, const std::vector<TransferOpDesc> &op_descs,
                       TransferReq &req);

  Status GetTransferStatus(const ChannelPtr &channel, const TransferReq &req, TransferStatus &status);

  std::vector<ShareHandleInfo> GetShareHandles();

  Status ImportMem(const ChannelPtr &channel, const std::vector<ShareHandleInfo> &remote_share_handles) const;

  void RemoveChannel(const std::string &channel_id);

 private:
  static Status IsTransferDone(const std::vector<AsyncResource> &async_resources, uint64_t req_id,
                               TransferStatus &status, bool &completed);
  Status TryGetStreamOnce(std::vector<rtStream_t> &streams, size_t stream_num);
  Status TryGetStream(std::vector<rtStream_t> &streams, uint64_t timeout);
  static Status ProcessCopyWithAsync(const std::vector<rtStream_t> &streams, TransferOp operation,
                                     const std::vector<TransferOpDesc> &op_descs);
  Status DoTransfer(const std::vector<rtStream_t> &streams, const ChannelPtr &channel, TransferOp operation,
                    const std::vector<TransferOpDesc> &op_descs, std::chrono::steady_clock::time_point &start);
  void ReleaseStreams(std::vector<rtStream_t> &streams);
  void DestroyAsyncResources(const std::vector<AsyncResource> &async_resources);
  void RemoveChannelReqRelation(const std::string &channel_id, uint64_t req_id);
  static void SynchronizeStream(const std::vector<AsyncResource> &async_resources, uint64_t req_id,
                                TransferStatus &status);
  static Status TransOpAddr(uintptr_t old_addr, size_t len,
                            std::unordered_map<uintptr_t, ShareHandleInfo> &new_va_to_old_va, uintptr_t &new_addr);

  std::mutex share_handle_mutex_;
  std::unordered_map<rtDrvMemHandle, ShareHandleInfo> share_handles_;
  int32_t device_id_{-1};
  size_t max_stream_num_{0};

  std::mutex stream_pool_mutex_;
  std::unordered_map<rtStream_t, bool> stream_pool_;

  std::mutex async_req_mutex_;
  std::unordered_map<uint64_t, AsyncRecord> req_2_async_record_;

  std::mutex channel_2_req_mutex_;
  std::unordered_map<std::string, std::set<uint64_t>> channel_2_req_;

  std::mutex local_va_map_mutex_;
  std::unordered_map<uintptr_t, ShareHandleInfo> local_va_to_old_va_;
};
}  // namespace adxl

#endif  // CANN_GRAPH_ENGINE_HCCS_TRANSFER_SERVICE_H
