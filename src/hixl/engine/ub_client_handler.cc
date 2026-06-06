/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "engine/ub_client_handler.h"
#include "cs/hixl_cs_client.h"
#include <algorithm>
#include <chrono>
#include <thread>
#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/hixl_utils.h"
#include "common/thread_pool.h"
#include "engine/client_handler_config_helper.h"
#include "engine/endpoint_generator.h"

namespace hixl {
namespace {

Status ComputeRemainingMs(const std::chrono::steady_clock::time_point &start, uint32_t timeout_ms,
                          uint32_t &remaining_ms) {
  auto elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
  if (elapsed >= static_cast<int64_t>(timeout_ms)) {
    return TIMEOUT;
  }
  remaining_ms = static_cast<uint32_t>(static_cast<int64_t>(timeout_ms) - elapsed);
  return SUCCESS;
}

}  // namespace

UbClientHandler::UbClientHandler(std::map<CommType, HixlClientHandle> handles) : handles_(std::move(handles)) {}

Status UbClientHandler::Create(const HandlerCreateArgs &args, std::unique_ptr<UbClientHandler> &out) {
  std::map<CommType, HixlClientHandle> handles;
  const std::string global_resource_config = ClientHandlerConfigHelper::BuildGlobalResourceConfig(
      args.local_listen_port);
  for (const auto &pair : args.matched_pairs) {
    int32_t dev_logic_id = 0;
    int32_t dev_phy_id = 0;
    HIXL_CHK_ACL_RET(aclrtGetDevice(&dev_logic_id));
    HIXL_CHK_ACL_RET(aclrtGetPhyDevIdByLogicDevId(dev_logic_id, &dev_phy_id));
    EndpointDesc le{};
    EndpointDesc re{};
    HIXL_CHK_STATUS_RET(EndpointGenerator::ConvertToEndpointDesc(pair.local, le, static_cast<uint32_t>(dev_phy_id)));
    HIXL_CHK_STATUS_RET(EndpointGenerator::ConvertToEndpointDesc(pair.remote, re));
    HixlClientDesc desc{};
    desc.server_ip = args.server_ip.c_str();
    desc.server_port = args.server_port;
    desc.local_endpoint = &le;
    desc.remote_endpoint = &re;
    desc.tc = args.rdma_tc;
    desc.sl = args.rdma_sl;
    HixlClientHandle handle = nullptr;
    HixlClientConfig config{};
    if (!global_resource_config.empty()) {
      config.global_resource_config = global_resource_config.c_str();
    }
    HIXL_CHK_STATUS_RET(HixlCSClientCreate(&desc, &config, &handle), "HixlCSClientCreate failed for type %s",
                        CommTypeToString(pair.type));
    handles[pair.type] = handle;
  }
  out = MakeUnique<UbClientHandler>(std::move(handles));
  HIXL_CHECK_NOTNULL(out, "UbClientHandler create failed");
  return SUCCESS;
}

Status UbClientHandler::Connect(uint32_t timeout_ms) {
  std::lock_guard<std::mutex> lock(handle_mutex_);
  if (handles_.empty()) {
    return FAILED;
  }

  ThreadPool thread_pool("ub_connect", handles_.size());
  std::vector<std::future<Status>> futures;
  aclrtContext context = nullptr;
  HIXL_CHK_ACL_RET(aclrtGetCurrentContext(&context));

  for (const auto &[type, handle] : handles_) {
    futures.emplace_back(thread_pool.commit([handle, timeout_ms, type, context]() -> Status {
      HIXL_CHK_ACL_RET(aclrtSetCurrentContext(context));
      HIXL_CHK_STATUS_RET(HixlCSClientConnect(handle, timeout_ms), "UbClientHandler Connect failed for type:%s",
                          CommTypeToString(type));
      return SUCCESS;
    }));
  }
  for (auto &f : futures) {
    HIXL_CHK_STATUS_RET(f.get(), "UbClientHandler Connect failed");
  }

  // 获取远端内存
  for (const auto &pair : handles_) {
    auto handle = pair.second;
    CommMem *remote_mem_list = nullptr;
    char **mem_tag_list = nullptr;
    uint32_t list_num = 0;
    HIXL_CHK_STATUS_RET(HixlCSClientGetRemoteMem(handle, &remote_mem_list, &mem_tag_list, &list_num, timeout_ms));

    std::lock_guard<std::mutex> seg_lock(remote_seg_mutex_);
    for (uint32_t i = 0; i < list_num; i++) {
      MemType type = (remote_mem_list[i].type == COMM_MEM_TYPE_DEVICE) ? MEM_DEVICE : MEM_HOST;
      auto it = std::find_if(remote_segments_.begin(), remote_segments_.end(),
                             [type](const SegmentPtr &seg) { return seg->GetMemType() == type; });
      if (it != remote_segments_.end()) {
        HIXL_CHK_STATUS_RET(
            (*it)->AddRange(reinterpret_cast<uintptr_t>(remote_mem_list[i].addr), remote_mem_list[i].size));
      } else {
        auto seg = MakeShared<Segment>(type);
        HIXL_CHK_BOOL_RET_STATUS(seg != nullptr, FAILED, "Failed to create segment");
        HIXL_CHK_STATUS_RET(
            seg->AddRange(reinterpret_cast<uintptr_t>(remote_mem_list[i].addr), remote_mem_list[i].size));
        remote_segments_.push_back(seg);
      }
    }
  }
  return SUCCESS;
}

Status UbClientHandler::RegisterMem(const MemInfo &mem_info) {
  const auto &mem = mem_info.mem;
  const auto type = mem_info.type;

  {
    std::lock_guard<std::mutex> lock(local_seg_mutex_);
    auto it = std::find_if(local_segments_.begin(), local_segments_.end(),
                           [type](const SegmentPtr &seg) { return seg->GetMemType() == type; });
    if (it != local_segments_.end()) {
      HIXL_CHK_STATUS_RET((*it)->AddRange(mem.addr, mem.len));
    } else {
      auto seg = MakeShared<Segment>(type);
      HIXL_CHK_BOOL_RET_STATUS(seg != nullptr, FAILED, "Failed to create segment");
      HIXL_CHK_STATUS_RET(seg->AddRange(mem.addr, mem.len));
      local_segments_.push_back(seg);
    }
  }

  CommMem hccl_mem{};
  hccl_mem.type = (type == MemType::MEM_DEVICE) ? COMM_MEM_TYPE_DEVICE : COMM_MEM_TYPE_HOST;
  hccl_mem.addr = reinterpret_cast<void *>(mem.addr);
  hccl_mem.size = mem.len;

  std::vector<CommType> comm_types;
  if (type == MemType::MEM_DEVICE) {
    comm_types = {CommType::COMM_TYPE_UB_D2H, CommType::COMM_TYPE_UB_D2D};
  } else {
    comm_types = {CommType::COMM_TYPE_UB_H2D, CommType::COMM_TYPE_UB_H2H};
  }

  std::scoped_lock lock(handle_mutex_, mem_handle_mutex_);
  for (auto ct : comm_types) {
    auto h_it = handles_.find(ct);
    if (h_it == handles_.end()) {
      continue;
    }
    MemHandle mh = nullptr;
    HIXL_CHK_STATUS_RET(HixlCSClientRegMem(h_it->second, nullptr, &hccl_mem, &mh));
    mem_handles_[ct].push_back(mh);
  }
  return SUCCESS;
}

Status UbClientHandler::TransferAsync(const std::vector<TransferOpDesc> &op_descs, TransferOp operation,
                                       TransferReq &req) {
  std::map<CommType, std::vector<TransferOpDesc>> table;
  HIXL_CHK_STATUS_RET(ClassifyTransfers(op_descs, table));

  std::vector<BatchHandle> batch_handles;
  std::lock_guard<std::mutex> lock(handle_mutex_);
  for (const auto &[type, descs] : table) {
    auto it = handles_.find(type);
    if (it == handles_.end()) {
      return FAILED;
    }
    auto handle = it->second;

    uint32_t list_num = static_cast<uint32_t>(descs.size());
    std::vector<HixlOneSideOpDesc> hixl_descs(list_num);
    for (size_t i = 0; i < list_num; i++) {
      hixl_descs[i].remote_buf = reinterpret_cast<void *>(descs[i].remote_addr);
      hixl_descs[i].local_buf = reinterpret_cast<void *>(descs[i].local_addr);
      hixl_descs[i].len = descs[i].len;
    }
    CompleteHandle complete_handle = nullptr;
    if (operation == WRITE) {
      HIXL_CHK_STATUS_RET(HixlCSClientBatchPutAsync(handle, list_num, hixl_descs.data(), &complete_handle));
    } else {
      HIXL_CHK_STATUS_RET(HixlCSClientBatchGetAsync(handle, list_num, hixl_descs.data(), &complete_handle));
    }
    batch_handles.push_back({type, complete_handle});
  }
  req = static_cast<TransferReq>(batch_handles[0].handle);
  std::lock_guard<std::mutex> ch_lock(complete_handles_mutex_);
  complete_handles_[req] = std::move(batch_handles);
  return SUCCESS;
}

Status UbClientHandler::TransferSync(const std::vector<TransferOpDesc> &op_descs, TransferOp operation,
                                     uint32_t timeout_ms) {
  std::map<CommType, std::vector<TransferOpDesc>> table;
  HIXL_CHK_STATUS_RET(ClassifyTransfers(op_descs, table));

  const auto sync_start = std::chrono::steady_clock::now();
  for (const auto &[type, descs] : table) {
    uint32_t remaining_ms = 0;
    HIXL_CHK_STATUS_RET(ComputeRemainingMs(sync_start, timeout_ms, remaining_ms));

    HixlClientHandle handle = nullptr;
    {
      std::lock_guard<std::mutex> lock(handle_mutex_);
      auto it = handles_.find(type);
      if (it == handles_.end()) {
        return FAILED;
      }
      handle = it->second;
    }

    uint32_t list_num = static_cast<uint32_t>(descs.size());
    std::vector<HixlOneSideOpDesc> op_descs(list_num);
    for (size_t i = 0; i < list_num; i++) {
      op_descs[i].remote_buf = reinterpret_cast<void *>(descs[i].remote_addr);
      op_descs[i].local_buf = reinterpret_cast<void *>(descs[i].local_addr);
      op_descs[i].len = descs[i].len;
    }
    auto *cs = static_cast<HixlCSClient *>(handle);
    bool is_get = (operation != WRITE);
    HIXL_CHK_STATUS_RET(cs->BatchTransferSync(is_get, list_num, op_descs.data(), remaining_ms));
  }
  return SUCCESS;
}

Status UbClientHandler::GetTransferStatus(const TransferReq &req, TransferStatus &status) {
  std::lock_guard<std::mutex> ch_lock(complete_handles_mutex_);
  if (complete_handles_.empty()) {
    HIXL_LOGE(FAILED, "UbClientHandler GetTransferStatus failed, no transfer tasks in progress, req:%p", req);
    status = TransferStatus::FAILED;
    return FAILED;
  }
  auto it = complete_handles_.find(req);
  if (it == complete_handles_.end()) {
    HIXL_LOGE(PARAM_INVALID, "UbClientHandler GetTransferStatus failed, invalid req");
    status = TransferStatus::FAILED;
    return PARAM_INVALID;
  }

  bool all_complete = true;
  for (const auto &bh : it->second) {
    std::lock_guard<std::mutex> lock(handle_mutex_);
    auto h_it = handles_.find(bh.type);
    if (h_it == handles_.end()) {
      status = TransferStatus::FAILED;
      complete_handles_.erase(req);
      return FAILED;
    }
    HixlCompleteStatus cs = HIXL_COMPLETE_STATUS_WAITING;
    Status ret = static_cast<Status>(HixlCSClientQueryCompleteStatus(h_it->second, bh.handle, &cs));
    if (ret != SUCCESS) {
      status = TransferStatus::FAILED;
      complete_handles_.erase(req);
      return ret;
    }
    if (cs == HIXL_COMPLETE_STATUS_WAITING) {
      all_complete = false;
    }
  }

  status = all_complete ? TransferStatus::COMPLETED : TransferStatus::WAITING;
  if (all_complete) {
    HIXL_LOGI("UbClientHandler GetTransferStatus completed, req:%p", req);
    complete_handles_.erase(req);
  } else {
    HIXL_LOGI("UbClientHandler GetTransferStatus waiting, req:%p", req);
  }
  return SUCCESS;
}

Status UbClientHandler::Finalize() {
  {
    std::lock_guard<std::mutex> lock(complete_handles_mutex_);
    complete_handles_.clear();
  }
  {
    std::scoped_lock lock(mem_handle_mutex_, handle_mutex_);
    for (auto &[type, mem_list] : mem_handles_) {
      auto it = handles_.find(type);
      if (it == handles_.end()) {
        continue;
      }
      for (auto &mh : mem_list) {
        if (mh != nullptr) {
          HixlCSClientUnregMem(it->second, mh);
        }
      }
    }
    mem_handles_.clear();
  }
  {
    std::lock_guard<std::mutex> lock(handle_mutex_);
    for (auto &pair : handles_) {
      if (pair.second != nullptr) {
        HixlCSClientDestroy(pair.second);
      }
    }
    handles_.clear();
  }
  {
    std::lock_guard<std::mutex> lock(local_seg_mutex_);
    local_segments_.clear();
  }
  {
    std::lock_guard<std::mutex> lock(remote_seg_mutex_);
    remote_segments_.clear();
  }
  return SUCCESS;
}

Status UbClientHandler::ClassifyTransfers(const std::vector<TransferOpDesc> &op_descs,
                                          std::map<CommType, std::vector<TransferOpDesc>> &table) {
  for (const auto &op : op_descs) {
    MemType local_mem_type;
    {
      std::lock_guard<std::mutex> lock(local_seg_mutex_);
      if (GetMemType(local_segments_, op.local_addr, op.len, local_mem_type) != SUCCESS) {
        HIXL_LOGE(PARAM_INVALID, "Local memory range does not register before connection: start:0x%lx, end:0x%lx",
                  op.local_addr, op.local_addr + op.len);
        return PARAM_INVALID;
      }
    }
    MemType remote_mem_type;
    {
      std::lock_guard<std::mutex> lock(remote_seg_mutex_);
      if (GetMemType(remote_segments_, op.remote_addr, op.len, remote_mem_type) != SUCCESS) {
        HIXL_LOGE(PARAM_INVALID, "Remote memory range does not register before connection: start:0x%lx, end:0x%lx",
                  op.remote_addr, op.remote_addr + op.len);
        return PARAM_INVALID;
      }
    }
    CommType ct = (local_mem_type == MEM_DEVICE)
                      ? ((remote_mem_type == MEM_DEVICE) ? CommType::COMM_TYPE_UB_D2D : CommType::COMM_TYPE_UB_D2H)
                      : ((remote_mem_type == MEM_DEVICE) ? CommType::COMM_TYPE_UB_H2D : CommType::COMM_TYPE_UB_H2H);
    table[ct].push_back(op);
  }
  return SUCCESS;
}

Status UbClientHandler::GetMemType(const std::vector<SegmentPtr> &segments, uintptr_t addr, size_t len,
                                   MemType &mem_type) const {
  for (const auto &seg : segments) {
    if (seg->Contains(addr, addr + len)) {
      mem_type = seg->GetMemType();
      return SUCCESS;
    }
  }
  return PARAM_INVALID;
}

}  // namespace hixl
