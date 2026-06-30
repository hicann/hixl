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
#include <unistd.h>
#include "securec.h"
#include "common/ctrl_msg.h"
#include "common/ctrl_msg_plugin.h"
#include "common/hixl_checker.h"
#include "common/json_utils.h"
#include "nlohmann/json.hpp"
#include "common/hixl_log.h"
#include "common/hixl_utils.h"
#include "common/optional_aclrt_context.h"
#include "common/scope_guard.h"
#include "common/thread_pool.h"
#include "engine/client_handler_config_helper.h"
#include "engine/endpoint_generator.h"

namespace hixl {
namespace {
constexpr uint64_t kMaxRecvMemInfoBodySize = static_cast<uint64_t>(4ULL * 1024ULL * 1024ULL);  // 4MB

Status DeserializeMemInfoList(const std::string &json_str, std::vector<MemInfo> &mem_info_list) {
  nlohmann::json j;
  try {
    j = nlohmann::json::parse(json_str);
  } catch (const nlohmann::json::exception &e) {
    HIXL_LOGE(PARAM_INVALID, "Failed to parse mem info json, exception:%s", e.what());
    return PARAM_INVALID;
  }
  if (!j.is_array()) {
    HIXL_LOGE(PARAM_INVALID, "Invalid mem info json format, expect array");
    return PARAM_INVALID;
  }

  mem_info_list.clear();
  for (const auto &item : j) {
    MemInfo info{};
    HIXL_CHK_STATUS_RET(ParseJsonField(item, "type", info.type), "Failed to parse mem type");
    HIXL_CHK_STATUS_RET(ParseJsonField(item, "addr", info.addr), "Failed to parse mem addr");
    HIXL_CHK_STATUS_RET(ParseJsonField(item, "size", info.size), "Failed to parse mem size");
    mem_info_list.push_back(info);
  }
  return SUCCESS;
}

Status FetchRemoteMemInfo(int32_t sock, uint32_t timeout_ms, std::vector<MemInfo> &mem_info) {
  CtrlMsgHeader header{};
  header.magic = kMagicNumber;
  header.body_size = static_cast<uint64_t>(sizeof(CtrlMsgType));
  CtrlMsgType req_type = CtrlMsgType::kGetMemInfoReq;
  HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Send(sock, &header, sizeof(header)),
                      "[UbClientHandler] Failed to send mem info header, fd:%d", sock);
  HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Send(sock, &req_type, sizeof(req_type)),
                      "[UbClientHandler] Failed to send GetMemInfoReq, fd:%d", sock);

  CtrlMsgHeader resp_header{};
  HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Recv(sock, &resp_header, sizeof(resp_header), timeout_ms),
                      "[UbClientHandler] Failed to recv mem info header, fd:%d", sock);
  HIXL_CHK_BOOL_RET_STATUS(resp_header.magic == kMagicNumber, PARAM_INVALID,
                           "[UbClientHandler] Invalid magic in mem info resp, fd:%d: 0x%X", sock, resp_header.magic);
  HIXL_CHK_BOOL_RET_STATUS(
      resp_header.body_size > sizeof(CtrlMsgType) && resp_header.body_size <= kMaxRecvMemInfoBodySize, PARAM_INVALID,
      "[UbClientHandler] Invalid body_size=%" PRIu64 " in mem info resp, fd:%d", resp_header.body_size, sock);

  std::vector<uint8_t> body(resp_header.body_size);
  HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Recv(sock, body.data(), resp_header.body_size, timeout_ms),
                      "[UbClientHandler] Failed to recv mem info body, fd:%d", sock);

  CtrlMsgType resp_type{};
  errno_t rc = memcpy_s(&resp_type, sizeof(resp_type), body.data(), sizeof(resp_type));
  HIXL_CHK_BOOL_RET_STATUS(rc == EOK, FAILED, "[UbClientHandler] memcpy_s failed, rc=%d", static_cast<int32_t>(rc));
  HIXL_CHK_BOOL_RET_STATUS(resp_type == CtrlMsgType::kGetMemInfoResp, PARAM_INVALID,
                           "[UbClientHandler] Unexpected msg_type=%d in mem info resp, expect=%d",
                           static_cast<int32_t>(resp_type), static_cast<int32_t>(CtrlMsgType::kGetMemInfoResp));

  std::string json_str(reinterpret_cast<const char *>(body.data() + sizeof(resp_type)),
                       resp_header.body_size - sizeof(CtrlMsgType));
  return DeserializeMemInfoList(json_str, mem_info);
}

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
  const std::string global_resource_config = ClientHandlerConfigHelper::BuildGlobalResourceConfig(args);
  for (const auto &pair : args.matched_pairs) {
    EndpointDesc le{};
    EndpointDesc re{};
    HIXL_CHK_STATUS_RET(EndpointGenerator::ConvertToEndpointDesc(pair.local, le));
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

  // 通过复用 ctrl_socket 向 server 获取对端内存信息，提前构建 remote_segments_
  out->lazy_mode_ = args.is_lazy;
  if (args.is_lazy) {
    out->connect_timeout_ms_ = args.timeout_ms;
  }
  std::vector<MemInfo> remote_mem_info;
  HIXL_CHK_STATUS_RET(FetchRemoteMemInfo(args.ctrl_socket, args.timeout_ms, remote_mem_info),
                      "Failed to fetch remote mem info");
  HIXL_CHK_STATUS_RET(out->BuildRemoteSegmentsFromMemInfo(remote_mem_info), "Failed to build remote segments");
  return SUCCESS;
}

Status UbClientHandler::Connect(uint32_t timeout_ms) {
  std::lock_guard<std::mutex> lock(handle_mutex_);
  if (handles_.empty()) {
    return FAILED;
  }

  // 非 lazy 模式：直接连接全部链路
  if (!lazy_mode_) {
    HIXL_CHK_BOOL_RET_STATUS(connected_types_.empty(), ALREADY_CONNECTED,
                             "[UbClientHandler] All links already connected");
    return ConnectHandles(handles_, timeout_ms);
  }

  // lazy 模式首次调用：仅保存超时，延迟到 transfer 时按需建链
  if (!connect_triggered_) {
    connect_triggered_ = true;
    HIXL_LOGI("[UbClientHandler] Auto-connect mode, deferring link connection to transfer time");
    return SUCCESS;
  }

  // lazy 模式显式调用hixl的Connect：补齐 transfer 未覆盖的剩余链路
  std::map<CommType, HixlClientHandle> pending;
  for (const auto &[type, handle] : handles_) {
    if (connected_types_.count(type) == 0U) {
      pending.emplace(type, handle);
    }
  }
  if (pending.empty()) {
    HIXL_LOGE(ALREADY_CONNECTED, "[UbClientHandler] All links already connected");
    return ALREADY_CONNECTED;
  }
  HIXL_LOGI("[UbClientHandler] Auto-connect mode, connecting %zu remaining link(s)", pending.size());
  return ConnectHandles(pending, timeout_ms);
}

Status UbClientHandler::EnsureLinksConnected(const std::vector<CommType> &types, uint32_t timeout_ms) {
  std::lock_guard<std::mutex> lock(handle_mutex_);

  std::map<CommType, HixlClientHandle> pending;
  for (auto type : types) {
    if (connected_types_.count(type) != 0U) {
      continue;
    }
    auto it = handles_.find(type);
    if (it == handles_.end()) {
      HIXL_LOGE(FAILED, "[UbClientHandler] No handle for type:%s", CommTypeToString(type));
      return FAILED;
    }
    pending.emplace(type, it->second);
  }
  if (pending.empty()) {
    return SUCCESS;
  }
  return ConnectHandles(pending, timeout_ms);
}

Status UbClientHandler::ConnectHandles(const std::map<CommType, HixlClientHandle> &handles, uint32_t timeout_ms) {
  ThreadPool thread_pool("ub_connect", handles.size());
  std::vector<std::future<Status>> futures;
  OptionalAclrtContext context;
  HIXL_CHK_STATUS_RET(context.GetCurrentContext(), "GetCurrentContext failed");

  for (const auto &[type, handle] : handles) {
    futures.emplace_back(thread_pool.commit([handle, timeout_ms, type, &context]() -> Status {
      HIXL_CHK_STATUS_RET(context.SetCurrentContext(), "SetCurrentContext failed");
      HIXL_CHK_STATUS_RET(HixlCSClientConnect(handle, timeout_ms), "UbClientHandler Connect failed for type:%s",
                          CommTypeToString(type));
      HIXL_LOGI("[UbClientHandler] Connected type:%s successfully", CommTypeToString(type));
      return SUCCESS;
    }));
  }
  for (auto &f : futures) {
    HIXL_CHK_STATUS_RET(f.get(), "[UbClientHandler] ConnectHandles failed");
  }

  for (const auto &pair : handles) {
    connected_types_.insert(pair.first);
  }
  return SUCCESS;
}

Status UbClientHandler::BuildRemoteSegmentsFromMemInfo(const std::vector<MemInfo> &mem_info_list) {
  std::lock_guard<std::mutex> seg_lock(remote_seg_mutex_);
  remote_segments_.clear();
  SegmentPtr device_seg;
  SegmentPtr host_seg;
  for (const auto &info : mem_info_list) {
    auto &seg = (info.type == MEM_DEVICE) ? device_seg : host_seg;
    if (seg == nullptr) {
      seg = MakeShared<Segment>(info.type);
      HIXL_CHK_BOOL_RET_STATUS(seg != nullptr, FAILED, "Failed to create segment");
    }
    HIXL_CHK_STATUS_RET(seg->AddRange(info.addr, info.size), "AddRange failed");
  }
  if (device_seg != nullptr) {
    remote_segments_.push_back(device_seg);
  }
  if (host_seg != nullptr) {
    remote_segments_.push_back(host_seg);
  }
  HIXL_LOGI("[UbClientHandler] Built remote segments from %zu exchanged mem info entries", mem_info_list.size());
  return SUCCESS;
}

Status UbClientHandler::RegisterMem(const MemHandleInfo &mem_info) {
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

  if (lazy_mode_) {
    std::vector<CommType> needed_types;
    for (const auto &pair : table) {
      needed_types.push_back(pair.first);
    }
    HIXL_CHK_STATUS_RET(EnsureLinksConnected(needed_types, connect_timeout_ms_));
  }

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

  if (lazy_mode_) {
    std::vector<CommType> needed_types;
    for (const auto &pair : table) {
      needed_types.push_back(pair.first);
    }
    uint32_t remaining_ms = 0;
    HIXL_CHK_STATUS_RET(ComputeRemainingMs(sync_start, timeout_ms, remaining_ms));
    HIXL_CHK_STATUS_RET(EnsureLinksConnected(needed_types, remaining_ms));
  }

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
    connected_types_.clear();
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
