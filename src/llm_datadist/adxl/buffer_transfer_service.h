/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_GRAPH_ENGINE_BUFFER_TRANSFER_SERVICE_H
#define CANN_GRAPH_ENGINE_BUFFER_TRANSFER_SERVICE_H

#include <future>
#include <utility>
#include "adxl/adxl_types.h"
#include "common/llm_mem_pool.h"
#include "common/llm_thread_pool.h"
#include "channel.h"
#include "control_msg_handler.h"

namespace adxl {
using CopyExtraInfo = std::pair<aclrtMemcpyKind, uint64_t>;
struct SliceInfo {
  std::vector<uintptr_t> src_addrs;
  std::vector<uintptr_t> dst_addrs;
  std::vector<size_t> sizes;
};
class BufferTransferService {
 public:
  BufferTransferService(std::vector<llm::LlmMemPool *> npu_mem_pools, uint64_t buffer_size)
      : npu_mem_pools_(std::move(npu_mem_pools)), buffer_size_(buffer_size) {}

  Status Initialize();

  void Finalize();

  Status Transfer(const ChannelPtr &channel, TransferType type, const std::vector<TransferOpDesc> &op_descs,
                  int32_t timeout_in_millis);

  Status PushBufferReq(const ChannelPtr &channel, BufferReq &buffer_req);

  Status PushBufferResp(const ChannelPtr &channel, BufferResp &buffer_resp);

 private:
  Status TryGetBuffer(void *&buffer_addr, uint64_t timeout, size_t pool_index = 0U);
  void ReleaseBuffer(void *buffer_addr, size_t pool_index = 0U);

  Status TryGetServerBuffer(void *&buffer_addr, uint64_t timeout);
  void ReleaseServerBuffer(void *buffer_addr);

  void ProcessBufferReqFirstStep();
  Status HandleBufferD2D(const ChannelPtr &channel, BufferReq &buffer_req);

  void ProcessBufferResp();
  Status HandleBufferResp(const ChannelPtr &channel, BufferResp &buffer_resp);

  void ProcessBufferReqSecondStep();
  Status HandleBufferCopy(const ChannelPtr &channel, BufferReq &buffer_req);

  void ProcessCtrlMsg();
  Status HandleCtrlMsg(const ChannelPtr &channel, const BufferReq &buffer_req);

  Status DoTransferTask(const ChannelPtr &channel, const std::vector<TransferOpDesc> &op_descs, uint64_t timeout,
                        TransferType type, uint64_t req_id);
  Status FlushBatch(const ChannelPtr &channel, const std::vector<TransferOpDesc> &op_descs, uint64_t timeout,
                    uint64_t req_id, TransferType type);
  static Status SendBufferReq(const ChannelPtr &channel, BufferReq &buffer_req, uint64_t timeout,
                              std::chrono::steady_clock::time_point start);
  Status TransferLargeData(const ChannelPtr &channel, const TransferOpDesc &op_desc, uint64_t timeout, uint64_t req_id,
                           TransferType type);
  static std::vector<uintptr_t> GenerateBufferReq(BufferReq &buffer_req, uintptr_t addr, uintptr_t remote_addr,
                                                  uintptr_t dev_buffer_addr, uint64_t count);
  Status CheckReqFinishStatus(uint64_t timeout, uint64_t req_id);

  Status PushSecondStepReq(const ChannelPtr &channel, BufferReq &buffer_req);

  void PushCtrlMsg(const ChannelPtr &channel, BufferReq &buffer_req);

  Status ProcessCopy(const ChannelPtr &channel, const std::vector<uintptr_t> &src_addrs,
                     const std::vector<uintptr_t> &dst_addrs, std::vector<size_t> &sizes, CopyExtraInfo extra_info);

  Status ProcessCopyWithBatch(const SliceInfo &slice_info, uint64_t batch_num, uint64_t slice_index,
                              CopyExtraInfo extra_info) const;

  static Status ProcessCopyWithAsync(const ChannelPtr &channel, const std::vector<uintptr_t> &src_addrs,
                                     const std::vector<uintptr_t> &dst_addrs, std::vector<size_t> &sizes,
                                     CopyExtraInfo extra_info);

  static Status D2DTransfer(const ChannelPtr &channel, TransferOp transfer_op,
                            const std::vector<TransferOpDesc> &op_descs, uint64_t timeout,
                            const std::chrono::steady_clock::time_point &start);

  static bool CheckTimeout(const BufferReq &req);

  std::vector<llm::LlmMemPool*> npu_mem_pools_;
  uint64_t buffer_size_;

  aclrtContext aclrt_context_{nullptr};
  int32_t device_id_{-1};
  bool support_batch_copy_batch_ = true;

  std::thread buffer_req_processor_;
  std::thread buffer_resp_processor_;
  std::thread buffer_second_step_processor_;
  std::thread ctrl_msg_processor_;
  std::atomic<bool> stop_signal_{false};

  std::mutex buffer_req_mutex_;
  std::queue<std::pair<ChannelPtr, BufferReq>> buffer_req_queue_;
  std::condition_variable buffer_req_cv_;

  std::mutex buffer_resp_mutex_;
  std::queue<std::pair<ChannelPtr, BufferResp>> buffer_resp_queue_;
  std::condition_variable buffer_resp_cv_;

  std::mutex buffer_second_step_mutex_;
  std::queue<std::pair<ChannelPtr, BufferReq>> buffer_second_step_queue_;
  std::condition_variable buffer_second_step_cv_;

  std::mutex buffer_ctrl_msg_mutex_;
  std::queue<std::pair<ChannelPtr, BufferReq>> buffer_ctrl_msg_queue_;
  std::condition_variable ctrl_msg_cv_;

  std::mutex req_id_mutex_;
  std::map<uint64_t, std::set<void *>> req_id_buffers_;
  std::atomic<uint64_t> next_req_id_{0};

  std::mutex buff_idle_mutex_;
  std::mutex server_buff_idle_mutex_;
  std::vector<std::map<void *, bool>> buff_addr_idles_;

  std::map<TransferType, TransferType> reverse_transfer_type_;
};
}  // namespace adxl

#endif  // CANN_GRAPH_ENGINE_BUFFER_TRANSFER_SERVICE_H
