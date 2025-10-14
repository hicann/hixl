/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "channel.h"
#include <mutex>
#include <fcntl.h>
#include <unistd.h>
#include "adxl/adxl_utils.h"
#include "common/llm_inner_types.h"
#include "common/llm_scope_guard.h"

#include <base/err_msg.h>

namespace adxl {
namespace {
// hccl HcclCommInitClusterInfoMemConfig not support parallel call, so use mutex to protect it
std::mutex g_mutex_;
constexpr uint32_t kMaxOpDescNum = 256U;
constexpr int64_t kHeartbeatTimeoutInMillis = 120000;
constexpr int32_t kMillisToMicros = 1000;
}

int64_t Channel::timeout_in_millis_ = kHeartbeatTimeoutInMillis;

Status Channel::Initialize() {
  LLMLOGI("HcclCommInitClusterInfoMemConfig begin, comm_name=%s, local rank_id=%u, rank_table=%s",
         channel_info_.comm_config.hcclCommName, channel_info_.local_rank_id, channel_info_.rank_table.c_str());
  {
    std::lock_guard<std::mutex> lock(g_mutex_);
    ADXL_CHK_HCCL_RET(llm::HcclAdapter::GetInstance().HcclCommInitClusterInfoMemConfig(
        channel_info_.rank_table.c_str(),
        channel_info_.local_rank_id,
        &channel_info_.comm_config,
        &channel_info_.comm));
  }
  std::vector<void *> bind_handles;
  LLM_DISMISSABLE_GUARD(fail_guard, ([this, &bind_handles]() {
    for (auto bind_handle : bind_handles) {
      (void) llm::HcclAdapter::GetInstance().HcclCommUnbindMem(channel_info_.comm, bind_handle);
    }
    (void) llm::HcclAdapter::GetInstance().HcclCommDestroy(channel_info_.comm);
  }));

  for (const auto &reg_handle_it : channel_info_.registered_mems) {
    auto reg_handle = reg_handle_it.first;
    ADXL_CHK_HCCL_RET(llm::HcclAdapter::GetInstance().HcclCommBindMem(channel_info_.comm, reg_handle));
    bind_handles.emplace_back(reg_handle);
  }

  const auto start = std::chrono::steady_clock::now();
  HcclPrepareConfig prepareConfig{};
  ADXL_CHK_HCCL_RET(llm::HcclAdapter::GetInstance().HcclCommPrepare(channel_info_.comm, &prepareConfig,
                                                                    channel_info_.timeout_sec));
  auto cost = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
  LLMLOGI("HcclCommPrepare success, cost=%ld ms.", cost);
  ADXL_CHK_ACL_RET(rtStreamCreateWithFlags(&stream_, RT_STREAM_PRIORITY_DEFAULT,
                   RT_STREAM_FAST_LAUNCH | RT_STREAM_FAST_SYNC));
  LLM_DISMISS_GUARD(fail_guard);
  return SUCCESS;
}

std::string Channel::GetChannelId() const {
  return channel_info_.channel_id;
}

Status Channel::Finalize() {
  auto ret = SUCCESS;
  if (stream_ != nullptr) {
    auto rt_ret = rtStreamAbort(stream_);
    LLMLOGI("Call rtStreamAbort ret:%d.", ret);
    ret = rt_ret != RT_ERROR_NONE ? FAILED : ret;
  }

  for (const auto &reg_handle_it : channel_info_.registered_mems) {
    auto reg_handle = reg_handle_it.first;
    auto hccl_ret = llm::HcclAdapter::GetInstance().HcclCommUnbindMem(channel_info_.comm, reg_handle);
    ret = hccl_ret != HcclResult::HCCL_SUCCESS ? FAILED : ret;
  }

  auto hccl_ret = llm::HcclAdapter::GetInstance().HcclCommDestroy(channel_info_.comm);
  ret = hccl_ret != HcclResult::HCCL_SUCCESS ? FAILED : ret;
  if (stream_ != nullptr) {
    auto rt_ret = rtStreamDestroy(stream_);
    LLMLOGI("Call rtStreamDestroy ret:%d.", ret);
    ret = rt_ret != RT_ERROR_NONE ? FAILED : ret;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  if (fd_ > 0) {
    close(fd_);
    fd_ = -1;
  }
  with_heartbeat_ = false;
  return ret;
}

Status Channel::TransferAsync(TransferOp operation, const std::vector<TransferOpDesc> &op_descs,
                              rtStream_t stream) {
  auto trans_func = [this, operation, &stream](HcclOneSideOpDesc *descs, uint32_t desc_num) -> Status {
    HcclResult ret = HCCL_SUCCESS;
    if (operation == READ) {
      ret = llm::HcclAdapter::GetInstance().HcclBatchGet(channel_info_.comm, channel_info_.peer_rank_id,
                                                         descs, desc_num, stream);
    } else {
      ret = llm::HcclAdapter::GetInstance().HcclBatchPut(channel_info_.comm, channel_info_.peer_rank_id,
                                                         descs, desc_num, stream);
    }
    ADXL_CHK_BOOL_RET_STATUS(ret == HCCL_SUCCESS,
                             HcclError2AdxlStatus(ret),
                             "Failed to invoke %s, hccl_result = %d",
                             operation == READ ? "HcclBatchGet" : "HcclBatchPut", static_cast<int32_t>(ret));
    return SUCCESS;
  };
  BufferedTransfer transfer(trans_func);
  ADXL_CHK_STATUS_RET(transfer.Put(op_descs), "Failed to batch transfer");
  return SUCCESS;
}

Status Channel::TransferSync(TransferOp operation,
                             const std::vector<TransferOpDesc> &op_descs,
                             int32_t timeout_in_millis) {
  const auto start = std::chrono::steady_clock::now();
  ADXL_CHK_STATUS_RET(TransferAsync(operation, op_descs, stream_), "Transfer failed.");

  ADXL_CHK_ACL_RET(rtStreamSynchronizeWithTimeout(stream_, timeout_in_millis));
  const auto end = std::chrono::steady_clock::now();
  const auto cost = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
  LLMLOGI("%s success, num = %zu, cost = %ld us.",
         operation == READ ? "HcclBatchGet" : "HcclBatchPut", op_descs.size(), cost);
  return SUCCESS;
}

Status Channel::SetSocketNonBlocking(int32_t fd) {
  std::lock_guard<std::mutex> lock(mutex_);
  int flags = fcntl(fd, F_GETFL, 0);
  ADXL_CHK_BOOL_RET_STATUS(flags != -1, FAILED, "Failed to get fd flags: %s", strerror(errno));
  ADXL_CHK_BOOL_RET_STATUS(fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1,
                           FAILED, "Failed to set fd to non-blocking: %s", strerror(errno));

  with_heartbeat_ = true;
  fd_ = fd;
  last_heartbeat_time_ = std::chrono::steady_clock::now();
  return SUCCESS;
}

void Channel::StopHeartbeat() {
  std::lock_guard<std::mutex> lock(mutex_);
  with_heartbeat_ = false;
}

Status Channel::SendControlMsg(const std::function<Status(int32_t)> &func) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (fd_ < 0) {
    return FAILED;
  }
  return func(fd_);
}

void Channel::SetHeartbeatTimeout(int64_t timeout_in_millis) {
  timeout_in_millis_ = timeout_in_millis;
}

void Channel::UpdateHeartbeatTime() {
  last_heartbeat_time_ = std::chrono::steady_clock::now();
}

bool Channel::IsHeartbeatTimeout() const {
  auto now = std::chrono::steady_clock::now();
  const auto cost = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_heartbeat_time_).count();
  if (cost >= timeout_in_millis_) {
    LLMLOGW("Channel heartbeat timeout detected, cost:%ld ms, channel_id:%s", cost, channel_info_.channel_id.c_str());
    return true;
  }
  return false;
}

BufferedTransfer::BufferedTransfer(
    std::function<Status(HcclOneSideOpDesc *descs, uint32_t desc_num)> trans_func) : trans_func_(trans_func) {
  op_descs_.reserve(kMaxOpDescNum);
}

Status BufferedTransfer::Put(const std::vector<TransferOpDesc> &op_descs) {
  size_t index = 0U;
  for (const auto &desc : op_descs) {
    HcclOneSideOpDesc hccl_op_desc{};
    hccl_op_desc.localAddr = reinterpret_cast<void *>(desc.local_addr);
    hccl_op_desc.remoteAddr = reinterpret_cast<void *>(desc.remote_addr);
    hccl_op_desc.count = desc.len;
    hccl_op_desc.dataType = HCCL_DATA_TYPE_UINT8;
    op_descs_.emplace_back(hccl_op_desc);
    LLMLOGI("Batch transfer sync, index:%zu, local addr:%p, remote addr:%p, len:%zu.",
           index, desc.local_addr, desc.remote_addr, desc.len);
    if (op_descs_.size() == op_descs_.capacity()) {
      ADXL_CHK_STATUS_RET(Flush(), "Failed to batch transfer.");
    }
  }
  if (!op_descs_.empty()) {
    ADXL_CHK_STATUS_RET(Flush(), "Failed to batch transfer.");
  }
  return SUCCESS;
}

Status BufferedTransfer::Flush() {
  if (!op_descs_.empty()) {
    ADXL_CHK_STATUS_RET(trans_func_(op_descs_.data(), static_cast<uint32_t>(op_descs_.size())),
                      "Failed to batch transfer.");
    op_descs_.clear();
  }
  return SUCCESS;
}

}  // namespace adxl
