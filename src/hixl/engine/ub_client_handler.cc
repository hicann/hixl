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
#include "utils/extern_math_util.h"
#include <algorithm>
#include <chrono>
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

namespace hixl {
namespace {

constexpr uint64_t kMaxRecvMemInfoBodySize = static_cast<uint64_t>(4ULL * 1024ULL * 1024ULL);  // 4MB

void LogUbDumpSummary(DumpLogLevel level, const char *reason, const std::string &local_engine,
                      const std::string &remote_engine, size_t handle_count, size_t connected_type_count,
                      bool lazy_mode, size_t complete_req_count, size_t local_segment_count,
                      size_t remote_segment_count) {
  if (level == DumpLogLevel::ERROR) {
    HIXL_LOGE(FAILED,
              "[UbClientHandler] dump, reason:%s, local_engine:%s, remote_engine:%s, handle_count:%zu, "
              "connected_type_count:%zu, lazy_mode:%d, complete_req_count:%zu, local_segment_count:%zu, "
              "remote_segment_count:%zu",
              reason, local_engine.c_str(), remote_engine.c_str(), handle_count, connected_type_count,
              static_cast<int32_t>(lazy_mode), complete_req_count, local_segment_count, remote_segment_count);
    return;
  }
  HIXL_EVENT(
      "[UbClientHandler] dump, reason:%s, local_engine:%s, remote_engine:%s, handle_count:%zu, "
      "connected_type_count:%zu, lazy_mode:%d, complete_req_count:%zu, local_segment_count:%zu, "
      "remote_segment_count:%zu",
      reason, local_engine.c_str(), remote_engine.c_str(), handle_count, connected_type_count,
      static_cast<int32_t>(lazy_mode), complete_req_count, local_segment_count, remote_segment_count);
}

void LogUbDumpLink(DumpLogLevel level, const char *reason, const std::string &local_engine,
                   const std::string &remote_engine, CommType type, HixlClientHandle handle, bool connected,
                   const std::string &local_endpoint, const std::string &remote_endpoint) {
  if (level == DumpLogLevel::ERROR) {
    HIXL_LOGE(FAILED,
              "[UbClientHandler] dump link, reason:%s, local_engine:%s, remote_engine:%s, comm_type:%s, "
              "handle:%p, connected:%d, local_endpoint:{%s}, remote_endpoint:{%s}",
              reason, local_engine.c_str(), remote_engine.c_str(), CommTypeToString(type), handle,
              static_cast<int32_t>(connected), local_endpoint.c_str(), remote_endpoint.c_str());
    return;
  }
  HIXL_EVENT(
      "[UbClientHandler] dump link, reason:%s, local_engine:%s, remote_engine:%s, comm_type:%s, "
      "handle:%p, connected:%d, local_endpoint:{%s}, remote_endpoint:{%s}",
      reason, local_engine.c_str(), remote_engine.c_str(), CommTypeToString(type), handle,
      static_cast<int32_t>(connected), local_endpoint.c_str(), remote_endpoint.c_str());
}

Status DeserializeMemInfoList(const std::string &json_str, std::vector<MemInfo> &mem_info_list) {
  nlohmann::json j;
  try {
    j = nlohmann::json::parse(json_str);
  } catch (const nlohmann::json::exception &e) {
    HIXL_LOGE(PARAM_INVALID, "Failed to parse mem info json, exception:%s", e.what());
    return PARAM_INVALID;
  }
  HIXL_CHK_BOOL_RET_STATUS(j.is_array(), PARAM_INVALID, "Invalid mem info json format, expect array");

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

std::string BuildAvailableCommTypes(const std::map<CommType, HixlClientHandle> &handles) {
  std::string available_types;
  for (const auto &[type, handle] : handles) {
    (void)handle;
    available_types += available_types.empty() ? "" : ",";
    available_types += CommTypeToString(type);
  }
  return available_types.empty() ? "none" : available_types;
}

std::string BuildAvailableCommTypes(const std::map<CommType, std::vector<HixlClientHandle>> &handles) {
  std::string available_types;
  for (const auto &[type, type_handles] : handles) {
    if (type_handles.empty()) {
      continue;
    }
    available_types += available_types.empty() ? "" : ",";
    available_types += CommTypeToString(type);
  }
  return available_types.empty() ? "none" : available_types;
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

Status UbClientHandler::InitRemoteState(const HandlerCreateArgs &args) {
  lazy_mode_ = args.is_lazy;
  if (args.is_lazy) {
    connect_timeout_ms_ = args.timeout_ms;
  }
  std::vector<MemInfo> remote_mem_info;
  HIXL_CHK_STATUS_RET(FetchRemoteMemInfo(args.ctrl_socket, args.timeout_ms, remote_mem_info),
                      "Failed to fetch remote mem info");
  // BuildRemoteSegmentsFromMemInfo 内部自持 remote_seg_mutex_，外层勿重复加锁
  return BuildRemoteSegmentsFromMemInfo(remote_mem_info);
}

void UbClientHandler::ClearTransferStateLocked() {
  async_workers_.clear();
}

void UbClientHandler::DestroyHandlesLocked() {
  for (auto &pair : handles_) {
    if (pair.second != nullptr) {
      HixlCSClientDestroy(pair.second);
    }
  }
  handles_.clear();
}

void UbClientHandler::UnregisterAllMemLocked() {
  for (auto &[key, record] : handle_to_mem_record_) {
    (void)key;
    for (const auto &[client, mh] : record.handles) {
      (void)UnregOneHandle(client, mh, record.addr);
    }
  }
  handle_to_mem_record_.clear();
  mem_handles_.clear();
}

std::vector<HixlClientHandle> UbClientHandler::GetHandlesForType(CommType type) const {
  auto it = handles_.find(type);
  if (it == handles_.end()) {
    return {};
  }
  return {it->second};
}

std::map<CommType, std::vector<HixlClientHandle>> UbClientHandler::GetAllLinkHandles() const {
  std::map<CommType, std::vector<HixlClientHandle>> all;
  for (const auto &[type, handle] : handles_) {
    all[type] = {handle};
  }
  return all;
}

UbClientHandler::UbClientHandler(std::map<CommType, HixlClientHandle> handles, const std::string &local_engine,
                                 const std::string &remote_engine,
                                 std::map<CommType, HandlerCreateArgs::EndpointPair> link_pairs)
    : handles_(std::move(handles)),
      local_engine_(local_engine),
      remote_engine_(remote_engine),
      link_pairs_(std::move(link_pairs)) {}

Status UbClientHandler::CreateHandlesForType(CommType type, const HixlClientDesc &desc, const HixlClientConfig &config,
                                             const HandlerCreateArgs &args) {
  (void)args;
  HixlClientHandle handle = nullptr;
  HIXL_CHK_STATUS_RET(HixlCSClientCreate(&desc, &config, &handle), "HixlCSClientCreate failed for type %s",
                      CommTypeToString(type));
  handles_[type] = handle;
  return SUCCESS;
}

Status UbClientHandler::InitLinkPairs(const HandlerCreateArgs &args) {
  const std::string global_resource_config = ClientHandlerConfigHelper::BuildGlobalResourceConfig(args);
  for (const auto &pair : args.matched_pairs) {
    EndpointDesc le{};
    EndpointDesc re{};
    HixlClientDesc desc{};
    HIXL_CHK_STATUS_RET(ClientHandlerConfigHelper::FillClientDesc(args, pair, le, re, desc),
                        "UbClientHandler fill client desc failed for type %s", CommTypeToString(pair.type));
    HixlClientConfig config{};
    if (!global_resource_config.empty()) {
      config.global_resource_config = global_resource_config.c_str();
    }
    HIXL_CHK_STATUS_RET(CreateHandlesForType(pair.type, desc, config, args),
                        "UbClientHandler create transport failed for type %s", CommTypeToString(pair.type));
    link_pairs_[pair.type] = pair;
  }
  return SUCCESS;
}

Status UbClientHandler::Create(const HandlerCreateArgs &args, std::unique_ptr<UbClientHandler> &out) {
  out = MakeUnique<UbClientHandler>(std::map<CommType, HixlClientHandle>{}, args.local_engine, args.remote_engine,
                                    std::map<CommType, HandlerCreateArgs::EndpointPair>{});
  HIXL_CHECK_NOTNULL(out, "UbClientHandler create failed");

  // 若后续步骤失败，通过 Finalize 释放已建资源，避免泄漏
  HIXL_DISMISSABLE_GUARD(finalize_guard, [&out]() {
    if (out != nullptr) {
      (void)out->Finalize();
    }
  });

  HIXL_CHK_STATUS_RET(out->InitLinkPairs(args), "UbClientHandler create links failed");
  HIXL_CHK_STATUS_RET(out->InitRemoteState(args), "UbClientHandler init remote state failed");
  HIXL_DISMISS_GUARD(finalize_guard);
  return SUCCESS;
}

Status UbClientHandler::Connect(uint32_t timeout_ms) {
  std::lock_guard<std::mutex> lock(handle_mutex_);
  const auto all_handles = GetAllLinkHandles();
  if (all_handles.empty()) {
    return FAILED;
  }

  // 非 lazy 模式：直接连接全部链路
  if (!lazy_mode_) {
    HIXL_CHK_BOOL_RET_STATUS(connected_types_.empty(), ALREADY_CONNECTED,
                             "[UbClientHandler] All links already connected");
    return ConnectHandles(all_handles, timeout_ms);
  }

  // lazy 模式首次调用：仅保存超时，延迟到 transfer 时按需建链
  if (!connect_triggered_) {
    connect_triggered_ = true;
    HIXL_LOGI("[UbClientHandler] Auto-connect mode, deferring link connection to transfer time");
    return SUCCESS;
  }

  HIXL_LOGE(ALREADY_CONNECTED, "[UbClientHandler] Auto-connect mode already triggered");
  return ALREADY_CONNECTED;
}

Status UbClientHandler::EnsureLinksConnected(const std::vector<CommType> &types, uint32_t timeout_ms) {
  std::lock_guard<std::mutex> lock(handle_mutex_);

  std::map<CommType, std::vector<HixlClientHandle>> pending;
  for (auto type : types) {
    if (connected_types_.count(type) != 0U) {
      continue;
    }
    // 宏参数仅在失败分支求值（含错误上报），热路径零额外分配
    std::vector<HixlClientHandle> type_handles = GetHandlesForType(type);
    HIXL_CHK_BOOL_RET_STATUS(
        !type_handles.empty(), FAILED,
        "[UbClientHandler] Requested communication type:%s is unavailable, available communication types:%s, "
        "local_engine:%s, remote_engine:%s",
        CommTypeToString(type), BuildAvailableCommTypes(GetAllLinkHandles()).c_str(), local_engine_.c_str(),
        remote_engine_.c_str());
    pending.emplace(type, std::move(type_handles));
  }
  if (pending.empty()) {
    return SUCCESS;
  }
  return ConnectHandles(pending, timeout_ms);
}

Status UbClientHandler::ConnectHandles(const std::map<CommType, std::vector<HixlClientHandle>> &handles,
                                       uint32_t timeout_ms) {
  std::vector<std::future<Status>> futures;
  std::vector<CommType> type_order;  // 与 futures 索引一一对应，worker 级并行提交
  std::map<CommType, size_t> worker_total;
  std::map<CommType, size_t> worker_done;
  size_t task_num = 0U;
  OptionalAclrtContext context;
  HIXL_CHK_STATUS_RET(context.GetCurrentContext(), "GetCurrentContext failed");

  for (const auto &[type, type_handles] : handles) {
    worker_total[type] = type_handles.size();
    task_num += type_handles.size();
  }
  ThreadPool thread_pool("ub_connect", task_num);
  for (const auto &[type, type_handles] : handles) {
    for (HixlClientHandle handle : type_handles) {
      type_order.push_back(type);
      futures.emplace_back(thread_pool.commit([type, handle, timeout_ms, &context]() -> Status {
        HIXL_CHK_STATUS_RET(context.SetCurrentContext(), "SetCurrentContext failed");
        HIXL_CHK_STATUS_RET(HixlCSClientConnect(handle, timeout_ms), "UbClientHandler Connect failed for type:%s",
                            CommTypeToString(type));
        return SUCCESS;
      }));
    }
  }
  for (size_t i = 0; i < futures.size(); ++i) {
    HIXL_CHK_STATUS_RET(futures[i].get(), "[UbClientHandler] ConnectHandles failed for type:%s",
                        CommTypeToString(type_order[i]));
    if (++worker_done[type_order[i]] == worker_total[type_order[i]]) {
      connected_types_.insert(type_order[i]);
      HIXL_LOGI("[UbClientHandler] Connected type:%s successfully", CommTypeToString(type_order[i]));
    }
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

Status UbClientHandler::AddLocalRange(uintptr_t addr, uint64_t len, MemType type) {
  auto it = std::find_if(local_segments_.begin(), local_segments_.end(),
                         [type](const SegmentPtr &seg) { return seg->GetMemType() == type; });
  if (it != local_segments_.end()) {
    return (*it)->AddRange(addr, len);
  }
  auto seg = MakeShared<Segment>(type);
  HIXL_CHK_BOOL_RET_STATUS(seg != nullptr, FAILED, "Failed to create segment");
  HIXL_CHK_STATUS_RET(seg->AddRange(addr, len));
  local_segments_.push_back(seg);
  return SUCCESS;
}

void UbClientHandler::RemoveLocalRange(uintptr_t addr, uint64_t len, MemType type) {
  uint64_t end = 0U;
  if (ge::AddOverflow(addr, len, end)) {
    HIXL_LOGW("Skip removing local range due to overflow, addr:0x%lx, len:%lu bytes", addr, len);
    return;
  }
  for (auto it = local_segments_.begin(); it != local_segments_.end();) {
    if ((*it)->GetMemType() != type) {
      ++it;
      continue;
    }
    (*it)->RemoveRange(addr, end);
    if ((*it)->IsEmpty()) {
      it = local_segments_.erase(it);
    } else {
      ++it;
    }
  }
}

Status UbClientHandler::RollbackRegisteredHandles(const std::map<HixlClientHandle, MemHandle> &addr_handles,
                                                  uintptr_t addr) {
  Status first_err = SUCCESS;
  for (const auto &[client, mh] : addr_handles) {
    const Status ret = UnregOneHandle(client, mh, addr);
    if (ret != SUCCESS) {
      if (first_err == SUCCESS) {
        first_err = ret;
      }
      continue;
    }
    EraseMemHandle(mh);
  }
  return first_err;
}

Status UbClientHandler::UnregOneHandle(HixlClientHandle client, MemHandle mh, uintptr_t addr) {
  if (mh == nullptr) {
    return SUCCESS;
  }
  HIXL_CHK_STATUS_RET(HixlCSClientUnregMem(client, mh), "Call api:HixlCSClientUnregMem failed, addr:0x%lx, handle:%p",
                      addr, mh);
  return SUCCESS;
}

void UbClientHandler::EraseMemHandle(MemHandle mh) {
  for (auto &[ct, handles] : mem_handles_) {
    (void)ct;
    auto mh_it = std::find(handles.begin(), handles.end(), mh);
    if (mh_it != handles.end()) {
      handles.erase(mh_it);
      return;
    }
  }
}

Status UbClientHandler::RegisterMem(const MemHandleInfo &mem_info) {
  const auto &mem = mem_info.mem;
  const auto type = mem_info.type;
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

  std::scoped_lock lock(handle_mutex_, mem_handle_mutex_, local_seg_mutex_);
  if (handle_to_mem_record_.find(mem_info.mem_handle) != handle_to_mem_record_.end()) {
    return SUCCESS;
  }
  HIXL_CHK_STATUS_RET(AddLocalRange(mem.addr, mem.len, type));
  std::map<HixlClientHandle, MemHandle> addr_handles;
  for (auto ct : comm_types) {
    for (HixlClientHandle client : GetHandlesForType(ct)) {
      MemHandle mh = nullptr;
      const Status ret = HixlCSClientRegMem(client, nullptr, &hccl_mem, &mh);
      if (ret != SUCCESS) {
        HIXL_CHK_STATUS(RollbackRegisteredHandles(addr_handles, mem.addr),
                        "Rollback after RegisterMem failed, addr:0x%lx, failed_comm_type:%s", mem.addr,
                        CommTypeToString(ct));
        RemoveLocalRange(mem.addr, mem.len, type);
        HIXL_CHK_STATUS_RET(ret, "Call api:HixlCSClientRegMem failed, addr:0x%lx, comm_type:%s", mem.addr,
                            CommTypeToString(ct));
      }
      mem_handles_[ct].push_back(mh);
      addr_handles[client] = mh;
    }
  }
  handle_to_mem_record_[mem_info.mem_handle] = AddrMemRecord{mem.addr, mem.len, type, std::move(addr_handles)};
  return SUCCESS;
}

Status UbClientHandler::DeregisterMem(MemHandle mem_handle) {
  std::scoped_lock lock(handle_mutex_, mem_handle_mutex_, local_seg_mutex_);
  auto handle_it = handle_to_mem_record_.find(mem_handle);
  if (handle_it == handle_to_mem_record_.end()) {
    return SUCCESS;
  }
  AddrMemRecord &record = handle_it->second;
  Status first_err = SUCCESS;
  std::vector<HixlClientHandle> done_clients;
  for (const auto &[client, mh] : record.handles) {
    const Status ret = UnregOneHandle(client, mh, record.addr);
    if (ret != SUCCESS) {
      if (first_err == SUCCESS) {
        first_err = ret;
      }
      continue;
    }
    EraseMemHandle(mh);
    done_clients.push_back(client);
  }
  for (auto client : done_clients) {
    record.handles.erase(client);
  }
  HIXL_CHK_BOOL_RET_STATUS(first_err == SUCCESS, first_err,
                           "[UbClientHandler] DeregisterMem partial failure, mem_handle:%p, addr:0x%lx, len:%lu bytes, "
                           "remaining_comm_types:%zu",
                           mem_handle, record.addr, record.len, record.handles.size());
  RemoveLocalRange(record.addr, record.len, record.type);
  handle_to_mem_record_.erase(handle_it);
  return SUCCESS;
}

Status UbClientHandler::SubmitDescsForType(CommType type, const std::vector<TransferOpDesc> &descs, bool is_get,
                                           uint32_t timeout_ms, bool is_async, TransferReq &req) {
  HixlClientHandle handle = nullptr;
  {
    std::lock_guard<std::mutex> lock(handle_mutex_);
    auto it = handles_.find(type);
    HIXL_CHK_BOOL_RET_STATUS(
        it != handles_.end(), FAILED,
        "[UbClientHandler] Requested communication type:%s is unavailable, available communication types:%s, "
        "local_engine:%s, remote_engine:%s",
        CommTypeToString(type), BuildAvailableCommTypes(handles_).c_str(), local_engine_.c_str(),
        remote_engine_.c_str());
    handle = it->second;
  }
  const uint32_t list_num = static_cast<uint32_t>(descs.size());
  std::vector<HixlOneSideOpDesc> hixl_descs(list_num);
  for (size_t i = 0; i < list_num; i++) {
    hixl_descs[i].remote_buf = reinterpret_cast<void *>(descs[i].remote_addr);
    hixl_descs[i].local_buf = reinterpret_cast<void *>(descs[i].local_addr);
    hixl_descs[i].len = descs[i].len;
  }
  if (is_async) {
    CompleteHandle complete_handle = nullptr;
    const Status ret = is_get ? HixlCSClientBatchGetAsync(handle, list_num, hixl_descs.data(), &complete_handle)
                              : HixlCSClientBatchPutAsync(handle, list_num, hixl_descs.data(), &complete_handle);
    if (ret != SUCCESS) {
      return ret;
    }
    req = static_cast<TransferReq>(complete_handle);
    return SUCCESS;
  }
  return is_get ? HixlCSClientBatchGetSync(handle, list_num, hixl_descs.data(), timeout_ms)
                : HixlCSClientBatchPutSync(handle, list_num, hixl_descs.data(), timeout_ms);
}

Status UbClientHandler::CheckSubReqStatus(const SubReq &sub, TransferStatus &sub_status) {
  HixlClientHandle handle = nullptr;
  {
    std::lock_guard<std::mutex> lock(handle_mutex_);
    auto it = handles_.find(sub.type);
    HIXL_CHK_BOOL_RET_STATUS(it != handles_.end(), FAILED,
                             "[UbClientHandler] Requested communication type:%s is unavailable, "
                             "local_engine:%s, remote_engine:%s",
                             CommTypeToString(sub.type), local_engine_.c_str(), remote_engine_.c_str());
    handle = it->second;
  }
  HixlCompleteStatus cs = HIXL_COMPLETE_STATUS_WAITING;
  HIXL_CHK_STATUS_RET(HixlCSClientQueryCompleteStatus(handle, sub.req, &cs),
                      "[UbClientHandler] query complete status failed, handle:%p", sub.req);
  sub_status = ToTransferStatus(cs);
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

  // 异步提交+记账与 GetTransferStatus/ClearTransferStateLocked 共用 complete_handles_mutex_
  std::lock_guard<std::mutex> lock(complete_handles_mutex_);
  // 提交失败即返回错误：传输不重试，调用方按契约 disconnect 重建，
  // dispatcher 记账由 Finalize/Reset 清理
  SubReqList sub_reqs;
  for (const auto &[type, descs] : table) {
    SubReq sub{};
    sub.type = type;
    HIXL_CHK_STATUS_RET(SubmitDescsForType(type, descs, (operation != WRITE), 0U, true, sub.req),
                        "[UbClientHandler] async submit failed for type:%s", CommTypeToString(type));
    sub_reqs.push_back(sub);
  }
  HIXL_CHK_BOOL_RET_STATUS(!sub_reqs.empty(), PARAM_INVALID,
                           "[UbClientHandler] SubmitAsync failed, no sub request "
                           "submitted");
  req = sub_reqs[0].req;
  async_workers_[req] = std::move(sub_reqs);
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

  // 同步传输独占 sync_submit_mutex_，与异步查询（complete_handles_mutex_）解耦
  std::lock_guard<std::mutex> lock(sync_submit_mutex_);
  for (const auto &[type, descs] : table) {
    uint32_t remaining_ms = 0;
    HIXL_CHK_STATUS_RET(ComputeRemainingMs(sync_start, timeout_ms, remaining_ms));
    TransferReq sub_req = nullptr;
    HIXL_CHK_STATUS_RET(SubmitDescsForType(type, descs, (operation != WRITE), remaining_ms, false, sub_req));
  }
  return SUCCESS;
}

Status UbClientHandler::GetTransferStatus(const TransferReq &req, TransferStatus &status) {
  std::lock_guard<std::mutex> ch_lock(complete_handles_mutex_);
  if (async_workers_.empty()) {
    HIXL_LOGE(FAILED, "[UbClientHandler] GetTransferStatus failed, no transfer tasks in progress, req:%p", req);
    status = TransferStatus::FAILED;
    return FAILED;
  }
  auto it = async_workers_.find(req);
  if (it == async_workers_.end()) {
    HIXL_LOGE(PARAM_INVALID, "[UbClientHandler] GetTransferStatus failed, invalid req:%p", req);
    status = TransferStatus::FAILED;
    return PARAM_INVALID;
  }
  // 终态缓存：已终态子请求不再下查（CS 层首查终态即释放句柄，
  // 重查将触碰已释放内存）
  bool all_complete = true;
  for (auto &sub : it->second) {
    if (sub.status == TransferStatus::COMPLETED) {
      continue;
    }
    TransferStatus sub_status = TransferStatus::FAILED;
    const Status ret = CheckSubReqStatus(sub, sub_status);
    if (ret != SUCCESS) {
      status = TransferStatus::FAILED;
      async_workers_.erase(it);
      return ret;
    }
    if (sub_status == TransferStatus::WAITING) {
      all_complete = false;
      continue;
    }
    if (sub_status != TransferStatus::COMPLETED) {
      status = sub_status;
      HIXL_LOGE(FAILED, "UbClientHandler GetTransferStatus failed, sub status:%d, req:%p",
                static_cast<int32_t>(sub_status), req);
      async_workers_.erase(it);
      return SUCCESS;
    }
    sub.status = TransferStatus::COMPLETED;
  }

  status = all_complete ? TransferStatus::COMPLETED : TransferStatus::WAITING;
  if (all_complete) {
    HIXL_LOGI("UbClientHandler GetTransferStatus completed, req:%p", req);
    async_workers_.erase(it);
  } else {
    HIXL_LOGI("UbClientHandler GetTransferStatus waiting, req:%p", req);
  }
  return SUCCESS;
}

Status UbClientHandler::Finalize() {
  {
    // 传输记账复位与并发 GetTransferStatus/TransferSync 同斥
    std::scoped_lock lock(complete_handles_mutex_, sync_submit_mutex_);
    ClearTransferStateLocked();
  }
  {
    std::scoped_lock lock(mem_handle_mutex_, handle_mutex_);
    // 须在销毁句柄前逐个注销内存，避免泄漏对端注册
    UnregisterAllMemLocked();
  }
  {
    std::lock_guard<std::mutex> lock(handle_mutex_);
    DestroyHandlesLocked();
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

void UbClientHandler::Dump(const char *reason, DumpLogLevel level) const {
  struct DumpLink {
    CommType type;
    HixlClientHandle handle;
    bool connected;
    std::string local_endpoint;
    std::string remote_endpoint;
  };
  std::vector<DumpLink> links;
  size_t handle_count = 0;
  size_t connected_type_count = 0;
  size_t complete_req_count = 0;
  size_t local_segment_count = 0;
  size_t remote_segment_count = 0;
  {
    std::lock_guard<std::mutex> lock(handle_mutex_);
    for (const auto &[type, type_handles] : GetAllLinkHandles()) {
      auto pair_it = link_pairs_.find(type);
      const bool connected = connected_types_.count(type) != 0U;
      const std::string local_endpoint = pair_it == link_pairs_.end() ? "" : pair_it->second.local.ToString();
      const std::string remote_endpoint = pair_it == link_pairs_.end() ? "" : pair_it->second.remote.ToString();
      handle_count += type_handles.size();
      for (HixlClientHandle handle : type_handles) {
        links.push_back({type, handle, connected, local_endpoint, remote_endpoint});
      }
    }
    connected_type_count = connected_types_.size();
  }
  {
    std::lock_guard<std::mutex> lock(complete_handles_mutex_);
    complete_req_count = async_workers_.size();
  }
  {
    std::lock_guard<std::mutex> lock(local_seg_mutex_);
    local_segment_count = local_segments_.size();
  }
  {
    std::lock_guard<std::mutex> lock(remote_seg_mutex_);
    remote_segment_count = remote_segments_.size();
  }
  LogUbDumpSummary(level, reason, local_engine_, remote_engine_, handle_count, connected_type_count, lazy_mode_,
                   complete_req_count, local_segment_count, remote_segment_count);
  for (const auto &link : links) {
    LogUbDumpLink(level, reason, local_engine_, remote_engine_, link.type, link.handle, link.connected,
                  link.local_endpoint, link.remote_endpoint);
  }
}

Status UbClientHandler::ClassifyTransfers(const std::vector<TransferOpDesc> &op_descs,
                                          std::map<CommType, std::vector<TransferOpDesc>> &table) const {
  for (const auto &op : op_descs) {
    uint64_t local_end = 0U;
    uint64_t remote_end = 0U;
    HIXL_CHK_BOOL_RET_STATUS(!ge::AddOverflow(op.local_addr, op.len, local_end), PARAM_INVALID,
                             "Local memory range overflow: start:0x%lx, len:0x%lx", op.local_addr, op.len);
    HIXL_CHK_BOOL_RET_STATUS(!ge::AddOverflow(op.remote_addr, op.len, remote_end), PARAM_INVALID,
                             "Remote memory range overflow: start:0x%lx, len:0x%lx", op.remote_addr, op.len);
    MemType local_mem_type;
    {
      std::lock_guard<std::mutex> lock(local_seg_mutex_);
      HIXL_CHK_BOOL_RET_STATUS(
          GetMemType(local_segments_, op.local_addr, op.len, local_mem_type) == SUCCESS, PARAM_INVALID,
          "Local memory range is not registered before connection, start:0x%lx, end:0x%lx", op.local_addr, local_end);
    }
    MemType remote_mem_type;
    {
      std::lock_guard<std::mutex> lock(remote_seg_mutex_);
      HIXL_CHK_BOOL_RET_STATUS(GetMemType(remote_segments_, op.remote_addr, op.len, remote_mem_type) == SUCCESS,
                               PARAM_INVALID,
                               "Remote memory range is not registered before connection, start:0x%lx, end:0x%lx",
                               op.remote_addr, remote_end);
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
