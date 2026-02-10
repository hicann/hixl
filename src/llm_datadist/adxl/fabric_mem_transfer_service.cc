/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "fabric_mem_transfer_service.h"
#include <chrono>
#include <cstddef>
#include <map>
#include <mutex>
#include <vector>
#include "adxl/adxl_checker.h"
#include "common/def_types.h"
#include "common/llm_scope_guard.h"
#include "common/llm_log.h"
#include "statistic_manager.h"
#include "virtual_memory_manager.h"

namespace adxl {
namespace {
constexpr uint64_t kMillisToMicros = 1000;
}  // namespace
Status FabricMemTransferService::Initialize(size_t max_stream_num, size_t task_stream_num) {
  task_stream_num_ = task_stream_num;
  // async transfer need one plus stream to submit event record for better performance
  async_task_stream_num_ = task_stream_num_ + 1U;
  ADXL_CHK_ACL_RET(aclrtGetDevice(&device_id_));
  LLMLOGI("Get device id:%d", device_id_);
  max_stream_num_ = max_stream_num;
  return SUCCESS;
}

void FabricMemTransferService::Finalize() {
  {
    std::lock_guard<std::mutex> async_req_lock(async_req_mutex_);
    for (auto &req_2_record : req_2_async_record_) {
      DestroyAsyncResources(req_2_record.second.async_resources);
    }
    req_2_async_record_.clear();
  }
  {
    std::lock_guard<std::mutex> async_req_lock(channel_2_req_mutex_);
    channel_2_req_.clear();
  }
  {
    std::lock_guard<std::mutex> lock(stream_pool_mutex_);
    for (auto &stream_stat : stream_pool_) {
      if (stream_stat.first != nullptr) {
        LLM_CHK_ACL(aclrtDestroyStream(stream_stat.first));
      }
    }
    stream_pool_.clear();
  }
  {
    std::lock_guard<std::mutex> lock(local_va_map_mutex_);
    // unmap and free all imported pa handle
    for (auto &it : local_va_to_old_va_) {
      LLM_CHK_ACL(aclrtUnmapMem(llm::ValueToPtr(it.first)));
      (void)VirtualMemoryManager::GetInstance().ReleaseMemory(it.first);
    }
    local_va_to_old_va_.clear();
    for (auto it = mem_handle_to_import_info_.begin(); it != mem_handle_to_import_info_.end(); it++) {
      LLM_CHK_ACL(aclrtFreePhysical(it->second.first));
    }
    mem_handle_to_import_info_.clear();
  }
  {
    // free all retained pa handle
    std::lock_guard<std::mutex> lock(share_handle_mutex_);
    for (auto &share_handle : share_handles_) {
      LLM_CHK_ACL(aclrtFreePhysical(share_handle.first));
    }
    share_handles_.clear();
  }
}

Status FabricMemTransferService::RegisterMem(const MemDesc &mem, MemType type, MemHandle &mem_handle) {
  aclrtMemFabricHandle share_handle = {};
  auto va = llm::ValueToPtr(mem.addr);
  aclrtDrvMemHandle pa_handle = nullptr;
  {
    std::lock_guard<std::mutex> lock(share_handle_mutex_);
    ADXL_CHK_ACL_RET(aclrtMemRetainAllocationHandle(va, &pa_handle));
    LLM_DISMISSABLE_GUARD(pa_guard, ([&pa_handle]() {
                            if (pa_handle != nullptr) {
                              LLM_CHK_ACL(aclrtFreePhysical(pa_handle));
                            }
                          }));
    ADXL_CHK_ACL_RET(aclrtMemExportToShareableHandleV2(pa_handle, ACL_RT_VMM_EXPORT_FLAG_DISABLE_PID_VALIDATION,
                                                       ACL_MEM_SHARE_HANDLE_TYPE_FABRIC, &share_handle));
    share_handles_[pa_handle] = ShareHandleInfo{mem.addr, mem.len, share_handle};
    mem_handle = pa_handle;
    LLMLOGI("Export suc, mem type:%d, mem addr:%lu.", type, mem.addr);
    LLM_DISMISS_GUARD(pa_guard);
  }
  if (type == MEM_HOST) {
    uintptr_t local_va_addr = 0;
    aclrtDrvMemHandle local_pa_handle = nullptr;
    ADXL_CHK_STATUS_RET(VirtualMemoryManager::GetInstance().ReserveMemory(mem.len, local_va_addr));
    LLM_DISMISSABLE_GUARD(fail_guard, ([&local_va_addr, &local_pa_handle]() {
                            if (local_va_addr != 0) {
                              (void)VirtualMemoryManager::GetInstance().ReleaseMemory(local_va_addr);
                            }
                            if (local_pa_handle != nullptr) {
                              LLM_CHK_ACL(aclrtFreePhysical(local_pa_handle));
                            }
                          }));
    ADXL_CHK_ACL_RET(aclrtMemImportFromShareableHandleV2(&share_handle, ACL_MEM_SHARE_HANDLE_TYPE_FABRIC, 0U,
                                                      &local_pa_handle));
    void *local_va_ = llm::ValueToPtr(local_va_addr);
    ADXL_CHK_ACL_RET(aclrtMapMem(local_va_, mem.len, 0, local_pa_handle, 0));
    std::lock_guard<std::mutex> lock(local_va_map_mutex_);
    mem_handle_to_import_info_[pa_handle] = std::make_pair(local_pa_handle, local_va_addr);
    local_va_to_old_va_[local_va_addr] = ShareHandleInfo{mem.addr, mem.len, share_handle};
    LLMLOGI("Imported mem from share handle, va:%lu, new mapped va addr:%lu, len:%zu.", mem.addr, local_va_addr,
            mem.len);
    LLM_DISMISS_GUARD(fail_guard);
  }
  return SUCCESS;
}

Status FabricMemTransferService::DeregisterMem(MemHandle mem_handle) {
  {
    std::lock_guard<std::mutex> lock(local_va_map_mutex_);
    auto it = mem_handle_to_import_info_.find(mem_handle);
    if (it != mem_handle_to_import_info_.end()) {
      auto import_info = it->second;
      auto va_map_it = local_va_to_old_va_.find(import_info.second);
      if (va_map_it != local_va_to_old_va_.end()) {
        LLM_CHK_ACL(aclrtUnmapMem(llm::ValueToPtr(va_map_it->first)));
        (void)VirtualMemoryManager::GetInstance().ReleaseMemory(va_map_it->first);
        local_va_to_old_va_.erase(va_map_it);
      }
      // free imported pa handle
      LLM_CHK_ACL(aclrtFreePhysical(import_info.first));
      mem_handle_to_import_info_.erase(it);
    }
  }
  {
    std::lock_guard<std::mutex> lock(share_handle_mutex_);
    auto it = share_handles_.find(mem_handle);
    if (it != share_handles_.end()) {
      // free retained pa handle
      LLM_CHK_ACL(aclrtFreePhysical(it->first));
      share_handles_.erase(it);
    }
  }
  return SUCCESS;
}

Status FabricMemTransferService::Transfer(const ChannelPtr &channel, TransferOp operation,
                                          const std::vector<TransferOpDesc> &op_descs, int32_t timeout_in_millis) {
  const auto start = std::chrono::steady_clock::now();
  uint64_t timeout = static_cast<uint64_t>(timeout_in_millis) * kMillisToMicros;
  std::vector<aclrtStream> streams;
  streams.reserve(task_stream_num_);
  ADXL_CHK_STATUS_RET(TryGetStream(streams, timeout), "Failed to get stream.");
  LLM_DISMISSABLE_GUARD(fail_guard, ([this, &streams]() -> void {
                          std::lock_guard<std::mutex> lock(stream_pool_mutex_);
                          for (auto &stream : streams) {
                            LLM_CHK_ACL(aclrtDestroyStream(stream));
                            auto it = stream_pool_.find(stream);
                            if (it != stream_pool_.end()) {
                              stream_pool_.erase(it);
                            }
                          }
                        }));
  auto real_copy_start = std::chrono::steady_clock::now();
  ADXL_CHK_STATUS_RET(DoTransfer(streams, channel, operation, op_descs, real_copy_start), "Failed to transfer.");
  for (auto &stream : streams) {
    uint64_t time_cost =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
    ADXL_CHK_BOOL_RET_STATUS(time_cost < timeout, TIMEOUT, "Transfer timeout.");
    uint64_t stream_timeout = (timeout - time_cost) / kMillisToMicros;
    ADXL_CHK_BOOL_RET_STATUS(stream_timeout > 0, TIMEOUT, "Transfer timeout.");
    ADXL_CHK_ACL_RET(aclrtSynchronizeStreamWithTimeout(stream, stream_timeout));
  }
  uint64_t real_copy_cost =
      std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - real_copy_start).count();
  StatisticManager::GetInstance().UpdateFabricMemRealCopyCost(channel->GetChannelId(), real_copy_cost);
  LLM_DISMISS_GUARD(fail_guard);
  ReleaseStreams(streams);
  uint64_t transfer_cost =
      std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
  StatisticManager::GetInstance().UpdateFabricMemTransferCost(channel->GetChannelId(), transfer_cost);
  LLMLOGI("Transfer time cost:%lu us, real cost:%lu us.", transfer_cost, real_copy_cost);
  return SUCCESS;
}

Status FabricMemTransferService::TransferAsync(const ChannelPtr &channel, TransferOp operation,
                                               const std::vector<TransferOpDesc> &op_descs, TransferReq &req) {
  auto start = std::chrono::steady_clock::now();
  std::vector<aclrtStream> streams;
  streams.reserve(async_task_stream_num_);
  ADXL_CHK_STATUS_RET(TryGetStreamOnce(streams, async_task_stream_num_), "Failed to get stream.");
  auto real_copy_start = std::chrono::steady_clock::now();
  std::vector<aclrtStream> copy_streams(task_stream_num_, nullptr);
  for (size_t i = 0U; i < task_stream_num_; ++i) {
    copy_streams[i] = streams[i + 1U];
  }
  // TryGetStreamOnce make sure streams is not empty.
  aclrtStream record_stream = streams[0U];
  std::vector<AsyncResource> async_resources;
  LLM_DISMISSABLE_GUARD(fail_guard, ([this, &async_resources, &streams]() {
                          for (auto &async_resource : async_resources) {
                            if (async_resource.second != nullptr) {
                              LLM_CHK_ACL(aclrtDestroyEvent(async_resource.second));
                            }
                          }
                          std::lock_guard<std::mutex> lock(stream_pool_mutex_);
                          for (auto &stream : streams) {
                            LLM_CHK_ACL(aclrtDestroyStream(stream));
                            auto it = stream_pool_.find(stream);
                            if (it != stream_pool_.end()) {
                              stream_pool_.erase(it);
                            }
                          }
                        }));
  ADXL_CHK_STATUS_RET(DoTransfer(copy_streams, channel, operation, op_descs, real_copy_start), "Failed to transfer.");
  async_resources.reserve(streams.size());
  async_resources.emplace_back(record_stream, nullptr);
  for (auto &stream : copy_streams) {
    aclrtEvent event = nullptr;
    ADXL_CHK_ACL_RET(aclrtCreateEvent(&event));
    async_resources.emplace_back(stream, event);
    ADXL_CHK_ACL_RET(aclrtRecordEvent(event, record_stream));
    ADXL_CHK_ACL_RET(aclrtStreamWaitEvent(stream, event));
  }
  {
    std::lock_guard<std::mutex> lock(channel_2_req_mutex_);
    channel_2_req_[channel->GetChannelId()].emplace(llm::PtrToValue(req));
  }
  {
    std::lock_guard<std::mutex> lock(async_req_mutex_);
    req_2_async_record_[llm::PtrToValue(req)] = AsyncRecord{std::move(async_resources), real_copy_start};
  }
  auto cost = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
  LLMLOGI("Transfer async call end, channel:%s, req:%lu, time cost:%lu us.", channel->GetChannelId().c_str(),
          llm::PtrToValue(req), cost);
  LLM_DISMISS_GUARD(fail_guard);
  return SUCCESS;
}

Status FabricMemTransferService::IsTransferDone(const std::vector<AsyncResource> &async_resources, uint64_t req_id,
                                                TransferStatus &status, bool &completed) {
  status = TransferStatus::WAITING;
  for (auto &async_resource : async_resources) {
    if (async_resource.second != nullptr) {
      aclrtEventRecordedStatus event_status{};
      auto ret = aclrtQueryEventStatus(async_resource.second, &event_status);
      if (ret != ACL_ERROR_NONE) {
        LLMLOGE(FAILED, "Call aclrtEventRecordedStatus failed for req:%llu, ret:%d.", req_id, ret);
        status = TransferStatus::FAILED;
        return FAILED;
      }
      completed = completed && (event_status == ACL_EVENT_RECORDED_STATUS_COMPLETE);
    }
  }
  return SUCCESS;
}

Status FabricMemTransferService::GetTransferStatus(const ChannelPtr &channel, const TransferReq &req,
                                                   TransferStatus &status) {
  AsyncRecord async_record;
  auto req_id = llm::PtrToValue(req);
  {
    std::lock_guard<std::mutex> lock(async_req_mutex_);
    auto async_record_it = req_2_async_record_.find(req_id);
    ADXL_CHK_BOOL_RET_STATUS(async_record_it != req_2_async_record_.end(), FAILED, "Request:%lu not found.", req_id);
    // copy in case map rehash
    async_record = async_record_it->second;
  }
  const auto &async_resources = async_record.async_resources;
  LLM_DISMISSABLE_GUARD(fail_guard, ([this, &channel, &async_resources, req_id]() {
                          DestroyAsyncResources(async_resources);
                          RemoveChannelReqRelation(channel->GetChannelId(), req_id);
                          {
                            std::lock_guard<std::mutex> lock(async_req_mutex_);
                            req_2_async_record_.erase(req_id);
                          }
                        }));
  bool completed = true;
  ADXL_CHK_STATUS_RET(IsTransferDone(async_resources, req_id, status, completed), "Failed to get transfer status.");
  if (completed) {
    SynchronizeStream(async_resources, req_id, status);
    ADXL_CHK_BOOL_RET_SPECIAL_STATUS(status == TransferStatus::FAILED, FAILED, "Synchronize stream failed.");
    // free event.
    for (auto &async_resource : async_resources) {
      if (async_resource.second != nullptr) {
        LLM_CHK_ACL(aclrtDestroyEvent(async_resource.second));
      }
    }
    // release streams
    std::vector<aclrtStream> streams;
    streams.reserve(async_resources.size());
    for (auto &async_resource : async_resources) {
      streams.emplace_back(async_resource.first);
    }
    ReleaseStreams(streams);
    RemoveChannelReqRelation(channel->GetChannelId(), req_id);
    {
      std::lock_guard<std::mutex> lock(async_req_mutex_);
      req_2_async_record_.erase(req_id);
    }
    auto end = std::chrono::steady_clock::now();
    uint64_t cost = std::chrono::duration_cast<std::chrono::microseconds>(end - async_record.real_start).count();
    StatisticManager::GetInstance().UpdateFabricMemTransferCost(channel->GetChannelId(), cost);
    LLMLOGI("Transfer async request completed, channel:%s, req:%lu, cost:%lu us.", channel->GetChannelId().c_str(),
            req_id, cost);
  }
  LLM_DISMISS_GUARD(fail_guard);
  return SUCCESS;
}

void FabricMemTransferService::SynchronizeStream(const std::vector<AsyncResource> &async_resources, uint64_t req_id,
                                                 TransferStatus &status) {
  // call sync in case error happens
  status = TransferStatus::COMPLETED;
  for (auto &async_resource : async_resources) {
    if (async_resource.second != nullptr) {
      auto ret = aclrtSynchronizeStream(async_resource.first);
      if (ret != ACL_ERROR_NONE) {
        LLMLOGE(FAILED, "Call aclrtSynchronizeStream failed for req:%lu, ret:%d.", req_id, ret);
        status = TransferStatus::FAILED;
        continue;
      }
    }
  }
}

void FabricMemTransferService::RemoveChannel(const std::string &channel_id) {
  std::lock_guard<std::mutex> lock(channel_2_req_mutex_);
  auto it = channel_2_req_.find(channel_id);
  if (it == channel_2_req_.end()) {
    return;
  }
  std::lock_guard<std::mutex> async_req_lock(async_req_mutex_);
  // destroy all async resources related to this channel
  for (auto &req_id : it->second) {
    LLMLOGI("Destroy async resources, channel:%s, req:%lu.", channel_id.c_str(), req_id);
    auto async_record_it = req_2_async_record_.find(req_id);
    if (async_record_it == req_2_async_record_.end()) {
      continue;
    }
    DestroyAsyncResources(async_record_it->second.async_resources);
    // remove async record
    req_2_async_record_.erase(async_record_it);
  }
  // remove all relations of channel
  channel_2_req_.erase(it);
}

void FabricMemTransferService::RemoveChannelReqRelation(const std::string &channel_id, uint64_t req_id) {
  std::lock_guard<std::mutex> channel_2_req_lock(channel_2_req_mutex_);
  auto channel_2_req_it = channel_2_req_.find(channel_id);
  if (channel_2_req_it != channel_2_req_.end()) {
    auto req_id_it = std::find(channel_2_req_it->second.begin(), channel_2_req_it->second.end(), req_id);
    if (req_id_it != channel_2_req_it->second.end()) {
      channel_2_req_it->second.erase(req_id_it);
    }
  }
}

void FabricMemTransferService::DestroyAsyncResources(const std::vector<AsyncResource> &async_resources) {
  std::lock_guard<std::mutex> stream_lock(stream_pool_mutex_);
  for (auto &async_resource : async_resources) {
    if (async_resource.second != nullptr) {
      LLM_CHK_ACL(aclrtDestroyEvent(async_resource.second));
    }
    auto &stream = async_resource.first;
    LLM_CHK_ACL(aclrtDestroyStream(stream));
    auto stream_it = stream_pool_.find(stream);
    if (stream_it != stream_pool_.end()) {
      stream_pool_.erase(stream_it);
    }
  }
}

Status FabricMemTransferService::TryGetStream(std::vector<aclrtStream> &streams, uint64_t timeout) {
  auto start = std::chrono::steady_clock::now();
  streams.reserve(task_stream_num_);
  while (true) {
    ADXL_CHK_BOOL_RET_SPECIAL_STATUS(TryGetStreamOnce(streams, task_stream_num_) == SUCCESS, SUCCESS, "Success to get stream.");
    uint64_t time_cost =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
    ADXL_CHK_BOOL_RET_STATUS(time_cost < timeout, TIMEOUT, "Get stream timeout.");
  }
}

Status FabricMemTransferService::TryGetStreamOnce(std::vector<aclrtStream> &streams, size_t stream_num) {
  std::lock_guard<std::mutex> lock(stream_pool_mutex_);
  for (auto &stream_stat : stream_pool_) {
    if (stream_stat.second) {
      stream_stat.second = false;
      streams.emplace_back(stream_stat.first);
      if (streams.size() >= stream_num) {
        return SUCCESS;
      }
    }
  }
  while (streams.size() < stream_num) {
    if (stream_pool_.size() >= max_stream_num_) {
      LLMEVENT("Stream pool is full, current stream pool size:%zu.", stream_pool_.size());
      for (auto &stream : streams) {
        LLM_CHK_ACL(aclrtDestroyStream(stream));
      }
      return FAILED;
    }
    aclrtStream stream = nullptr;
    ADXL_CHK_ACL_RET(
        aclrtCreateStreamWithConfig(&stream, 0, ACL_STREAM_FAST_LAUNCH | ACL_STREAM_FAST_SYNC));
    streams.emplace_back(stream);
    LLMEVENT("Create new stream, current stream pool size:%zu.", stream_pool_.size());
  }
  for (const auto &stream : streams) {
    stream_pool_[stream] = false;
  }
  return SUCCESS;
}

void FabricMemTransferService::ReleaseStreams(std::vector<aclrtStream> &streams) {
  std::lock_guard<std::mutex> lock(stream_pool_mutex_);
  for (auto &stream : streams) {
    auto it = stream_pool_.find(stream);
    if (it != stream_pool_.end()) {
      it->second = true;
    }
  }
}

Status FabricMemTransferService::DoTransfer(const std::vector<aclrtStream> &streams, const ChannelPtr &channel,
                                            TransferOp operation, const std::vector<TransferOpDesc> &op_descs,
                                            std::chrono::steady_clock::time_point &start) {
  std::vector<TransferOpDesc> new_op_descs;
  new_op_descs.reserve(op_descs.size());
  // Get imported memory info from channel
  auto remote_va_to_old_va = channel->GetNewVaToOldVa();
  bool need_trans_local_addr = false;
  for (size_t i = 0; i < op_descs.size(); ++i) {
    const auto &op = op_descs[i];
    if (i == 0) {
      aclrtPtrAttributes attributes;
      ADXL_CHK_ACL_RET(aclrtPointerGetAttributes(llm::ValueToPtr(op.local_addr), &attributes));
      if (attributes.location.type == ACL_MEM_LOCATION_TYPE_HOST) {
        need_trans_local_addr = true;
      }
    }
    uintptr_t new_local_addr = op.local_addr;
    if (need_trans_local_addr) {
      std::lock_guard<std::mutex> lock(local_va_map_mutex_);
      ADXL_CHK_STATUS_RET(TransOpAddr(op.local_addr, op.len, local_va_to_old_va_, new_local_addr),
                          "Failed to transfer local addr");
    }
    uintptr_t new_remote_addr;
    ADXL_CHK_STATUS_RET(TransOpAddr(op.remote_addr, op.len, remote_va_to_old_va, new_remote_addr),
                        "Failed to transfer remote addr");
    TransferOpDesc new_op = op;
    new_op.local_addr = new_local_addr;
    new_op.remote_addr = new_remote_addr;
    LLMLOGD("Old local addr:%lu, New local addr:%lu, Old remote_addr: %lu, new remote_addr: %lu, len: %lu.",
            op.local_addr, new_local_addr, op.remote_addr, new_remote_addr, op.len);
    new_op_descs.push_back(new_op);
  }
  start = std::chrono::steady_clock::now();
  ADXL_CHK_STATUS_RET(ProcessCopyWithAsync(streams, operation, new_op_descs), "Failed to copy.");
  return SUCCESS;
}

Status FabricMemTransferService::TransOpAddr(uintptr_t old_addr, size_t len,
                                             std::unordered_map<uintptr_t, ShareHandleInfo> &new_va_to_old_va,
                                             uintptr_t &new_addr) {
  bool found = false;
  for (const auto &new_va_to_info : new_va_to_old_va) {
    auto &info = new_va_to_info.second;
    auto registered_old_va_start = info.va_addr;
    auto registered_old_va_end = info.va_addr + info.len;
    if ((old_addr >= registered_old_va_start) && ((old_addr + len) <= registered_old_va_end)) {
      uintptr_t offset = old_addr - registered_old_va_start;
      new_addr = new_va_to_info.first + offset;
      found = true;
      break;
    }
  }
  if (!found) {
    LLMLOGE(PARAM_INVALID, "Address:%lu not found in registered segments.", old_addr);
    return PARAM_INVALID;
  }
  return SUCCESS;
}

Status FabricMemTransferService::ProcessCopyWithAsync(const std::vector<aclrtStream> &streams, TransferOp operation,
                                                      const std::vector<TransferOpDesc> &op_descs) {
  for (size_t i = 0; i < op_descs.size(); ++i) {
    const auto &op = op_descs[i];
    auto kind = ACL_MEMCPY_DEVICE_TO_DEVICE;
    auto &stream = streams[i % (streams.size())];
    if (operation == TransferOp::WRITE) {
      ADXL_CHK_ACL_RET(
          aclrtMemcpyAsync(llm::ValueToPtr(op.remote_addr), op.len, llm::ValueToPtr(op.local_addr), op.len, kind, stream));
    } else if (operation == TransferOp::READ) {
      ADXL_CHK_ACL_RET(
          aclrtMemcpyAsync(llm::ValueToPtr(op.local_addr), op.len, llm::ValueToPtr(op.remote_addr), op.len, kind, stream));
    }
  }
  return SUCCESS;
}

std::vector<ShareHandleInfo> FabricMemTransferService::GetShareHandles() {
  std::lock_guard<std::mutex> lock(share_handle_mutex_);
  std::vector<ShareHandleInfo> share_handles;
  share_handles.reserve(share_handles_.size());
  for (auto &share_handle : share_handles_) {
    share_handles.push_back(share_handle.second);
  }
  return share_handles;
}

Status FabricMemTransferService::ImportMem(const ChannelPtr &channel,
                                           const std::vector<ShareHandleInfo> &remote_share_handles) const {
  return channel->ImportMem(remote_share_handles, device_id_);
}

}  // namespace adxl
