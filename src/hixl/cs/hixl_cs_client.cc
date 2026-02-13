/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hixl_cs_client.h"
#include <chrono>
#include <thread>
#include <cstring>
#include <algorithm>
#include <securec.h>
#include "acl/acl.h"
#include "hixl/hixl_types.h"
#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/hixl_utils.h"
#include "common/scope_guard.h"
#include "common/ctrl_msg_plugin.h"
#include "conn_msg_handler.h"
#include "mem_msg_handler.h"
#include <common/llm_utils.h>


namespace {

constexpr uint32_t kFlagSizeBytes = 8;
constexpr uint64_t kFlagDoneValue = 1ULL;
constexpr uint64_t kFlagResetValue = 0ULL;
constexpr const char *kTransFlagNameHost = "_hixl_builtin_host_trans_flag";
constexpr const char *kTransFlagNameDevice = "_hixl_builtin_dev_trans_flag";

void FreeExportDesc(std::vector<hixl::HixlMemDesc> &desc_list) {
  for (auto &d : desc_list) {
    if (d.export_desc != nullptr && d.export_len > 0U) {
      std::free(d.export_desc);
      d.export_desc = nullptr;
      d.export_len = 0U;
    }
  }
  desc_list.clear();
}

hixl::Status ValidateExportDescList(const std::vector<hixl::HixlMemDesc> &desc_list) {
  for (const auto &d : desc_list) {
    if (d.export_desc == nullptr || d.export_len == 0U) {
      HIXL_LOGE(hixl::PARAM_INVALID,
                "[HixlClient] ValidateExportDescList failed! Invalid export_desc at"
                "ptr=%p, len=%u, total_count=%zu",
                d.export_desc, d.export_len, desc_list.size());
      return hixl::PARAM_INVALID;
    }
  }
  return hixl::SUCCESS;
}

hixl::Status AppendTagStorage(std::vector<std::vector<char>> &storage, const std::string &tag) {
  std::vector<char> buf(tag.size() + 1U, '\0');
  if (!tag.empty()) {
    errno_t rc = memcpy_s(buf.data(), buf.size(), tag.data(), tag.size());
    HIXL_CHK_BOOL_RET_STATUS(
        rc == EOK, hixl::FAILED,
        "[HixlClient] AppendTagStorage failed! memcpy_s error.tag: '%s', tag_len: %zu, dest_buf_size: %zu, rc: %d",
        tag.c_str(), tag.size(), buf.size(), static_cast<int32_t>(rc));
  }
  storage.emplace_back(std::move(buf));
  HIXL_LOGD("[HixlClient] AppendTagStorage success. tag: '%s', current_storage_size: %zu", tag.c_str(), storage.size());
  return hixl::SUCCESS;
}

void BuildTagPtrs(std::vector<std::vector<char>> &storage, std::vector<char *> &ptrs) {
  ptrs.clear();
  ptrs.reserve(storage.size());
  for (auto &s : storage) {
    ptrs.emplace_back(s.empty() ? nullptr : s.data());
  }
}

void CloseImportedBufs(EndpointHandle ep_handle, std::vector<hixl::HixlMemDesc> &bufs) {
  if (ep_handle == nullptr) {
    return;
  }
  for (const auto &b : bufs) {
    if (!b.is_imported) {
      continue;
    }
    const HcclResult ret = HcommMemUnimport(ep_handle, b.export_desc, b.export_len);
    if (ret != HCCL_SUCCESS) {
      HIXL_LOGW("[HixlClient] HcommMemUnimport failed. addr=%p size=%" PRIu64 " ret=0x%X", b.mem.addr, b.mem.size,
                static_cast<uint32_t>(ret));
    }
  }
}

hixl::Status ImportOneDesc(hixl::ImportCtx &ctx, uint32_t idx, hixl::HixlMemDesc &desc) {
  HcommMem buf{};
  hixl::Status ret = ctx.ep->MemImport(desc.export_desc, desc.export_len, buf);
  if (ret != hixl::SUCCESS) {
    HIXL_LOGE(ret, "[HixlClient] MemImport failed, idx=%u, tag=%s", idx,
              desc.tag.empty() ? "(empty)" : desc.tag.c_str());
    return ret;
  }
  ctx.imported.emplace_back(buf);
  desc.is_imported = true;
  HcommMem mem{};
  mem.type = desc.mem.type;
  mem.addr = desc.mem.addr;
  mem.size = desc.mem.size;
  HIXL_LOGI("[HixlClient] ImportOneDesc desc.tag=%s mem.addr=%p", desc.tag.empty() ? "(empty)" : desc.tag.c_str(),
            mem.addr);
  ctx.mems.emplace_back(mem);
  ctx.tag_mem_map[desc.tag] = mem;
  HIXL_LOGD("[HixlClient] Imported mem[%u]: tag='%s', addr=%p, size=%llu", idx,
            desc.tag.empty() ? "(empty)" : desc.tag.c_str(), mem.addr, mem.size);
  ret = ctx.store->RecordMemory(true, mem.addr, static_cast<size_t>(mem.size));
  if (ret == hixl::SUCCESS) {
    ctx.recorded_addrs.emplace_back(mem.addr);
  } else {
    HIXL_LOGE(ret,
              "[HixlClient] RecordMemory(server) failed! This memory may have been registered. idx=%u, tag=%s, "
              "addr=%p, size=%llu",
              idx, desc.tag.empty() ? "(empty)" : desc.tag.c_str(), mem.addr, mem.size);
    return ret;
  }
  return AppendTagStorage(ctx.tag_storage, desc.tag);
}

hixl::Status ImportAllDescs(hixl::ImportCtx &ctx, std::vector<hixl::HixlMemDesc> &desc_list) {
  for (uint32_t i = 0; i < ctx.num; ++i) {
    hixl::Status ret = ImportOneDesc(ctx, i, desc_list[i]);
    if (ret != hixl::SUCCESS) {
      return ret;
    }
  }
  return hixl::SUCCESS;
}
}  // namespace

namespace hixl {

HixlCSClient::HixlCSClient() : mem_store_() {
  for (size_t i = 0U; i < kFlagQueueSize; ++i) {
    available_indices_[i] = i;
    live_handles_[i] = nullptr;
  }
}

Status HixlCSClient::InitFlagQueue() noexcept {
  if (flag_queue_ != nullptr) {
    return SUCCESS;  // 已初始化
  }
  void *tmp = nullptr;
  tmp = malloc(kFlagQueueSize * sizeof(uint64_t));
  HIXL_DISMISSABLE_GUARD(free_flag_mem, [&tmp]() {
    if (tmp != nullptr) {
      free(tmp);
      tmp = nullptr;
    }
  });
  if (tmp == nullptr) {
    HIXL_LOGE(FAILED, "flag_addr malloc failed.");
    return FAILED;
  }
  flag_queue_ = static_cast<uint64_t *>(tmp);
  for (size_t i = 0; i < kFlagQueueSize; ++i) {
    flag_queue_[i] = 0;
  }
  top_index_ = kFlagQueueSize;  // 初始化成功后可用
  HcommMem mem{};
  mem.type = HCCL_MEM_TYPE_HOST;
  mem.addr = flag_queue_;
  mem.size = kFlagQueueSize * sizeof(uint64_t);
  MemHandle flag_handle = nullptr;
  HIXL_CHK_STATUS_RET(RegMem(kTransFlagNameHost, &mem, &flag_handle),
                      "Failed to reg HOST trans finished flag, mem.addr: %p, mem.size: %lu.", mem.addr, mem.size);
  HIXL_DISMISS_GUARD(free_flag_mem);
  return SUCCESS;
}

HixlCSClient::~HixlCSClient() {
  if (flag_queue_ != nullptr) {
    free(flag_queue_);
    flag_queue_ = nullptr;
    HIXL_LOGI("flag_queue_ has been free.");
  }
  for (size_t i = 0; i < kFlagQueueSize; ++i){
    if (live_handles_[i] != nullptr) {
      delete live_handles_[i];
      live_handles_[i] = nullptr;
    }
  }
}

Status HixlCSClient::Create(const char *server_ip, uint32_t server_port, const EndpointDesc *src_endpoint,
                            const EndpointDesc *dst_endpoint, const HixlClientConfig *config) {
  HIXL_CHECK_NOTNULL(server_ip);
  HIXL_CHECK_NOTNULL(src_endpoint);
  HIXL_CHECK_NOTNULL(dst_endpoint);
  HIXL_CHECK_NOTNULL(config);
  HIXL_EVENT(
      "[HixlClient] Create begin. Server=%s:%u. "
      "SrcEndpoint[Loc:%d, protocol:%d, commAddr.Type:%d, commAddr.id:0x%x], "
      "DstEndpoint[Loc:%d, protocol:%d, commAddr.Type:%d, commAddr.id:0x%x]",
      server_ip, server_port, src_endpoint->loc.locType, src_endpoint->protocol, src_endpoint->commAddr.type,
      src_endpoint->commAddr.id, dst_endpoint->loc.locType, dst_endpoint->protocol, dst_endpoint->commAddr.type,
      dst_endpoint->commAddr.id);
  std::lock_guard<std::mutex> lock(mutex_);
  server_ip_ = server_ip;
  server_port_ = server_port;
  src_endpoint_ = MakeShared<Endpoint>(*src_endpoint);
  HIXL_CHECK_NOTNULL(src_endpoint_);
  Status ret = src_endpoint_->Initialize();
  HIXL_CHK_STATUS_RET(ret,
                      "[HixlClient] Failed to initialize src endpoint. "
                      "Check Config: [Loc:%d, protocol:%d, AddrVal:0x%x]",
                      src_endpoint->loc.locType, src_endpoint->protocol, src_endpoint->commAddr.id);
  HIXL_LOGI("[HixlClient] src_endpoint initialized. ep_handle=%p", src_endpoint_->GetHandle());
  dst_endpoint_ = *dst_endpoint;
  CtrlMsgPlugin::Initialize();
  HIXL_LOGD("[HixlClient] CtrlMsgPlugin initialized");
  EndpointHandle endpoint_handle = src_endpoint_->GetHandle();
  HIXL_EVENT("[HixlClient] Create success. server=%s:%u, src_ep_handle=%p", server_ip_.c_str(), server_port_,
             endpoint_handle);
  Status init_ret = InitFlagQueue();
  if (init_ret != SUCCESS) {
    HIXL_LOGE(init_ret, "[HixlClient] Failed to initialize flag queue.");
    return init_ret;
  }
  return SUCCESS;
}

// 注册client的endpoint的内存信息到内存注册表中。mem是一个结构体，其中记录了内存类型、地址和大小。
Status HixlCSClient::RegMem(const char *mem_tag, const HcommMem *mem, MemHandle *mem_handle) {
  HIXL_CHECK_NOTNULL(mem);
  auto check_result = mem_store_.CheckMemoryForRegister(false, mem->addr, mem->size);
  if (check_result) {
    HIXL_LOGE(PARAM_INVALID,
              "[HixlClient] Memory registration failed. This memory may overlap with the already recorded memory. "
              "Please check Mem, mem_adrr: %p, mem_size: %u.",
              mem->addr, mem->size);
    return PARAM_INVALID;
  }
  MemHandle ep_mem_handle = nullptr;
  HIXL_CHK_STATUS_RET(src_endpoint_->RegisterMem(mem_tag, *mem, ep_mem_handle),
                      "[HixlClient] Failed to register client endpoint mem.");
  *mem_handle = ep_mem_handle;
  Status ret = mem_store_.RecordMemory(false, mem->addr, mem->size);  // 记录client侧给endpoint分配的内存信息
  if (ret != SUCCESS) {
    HIXL_LOGE(FAILED,
              "[HixlClient] Client record memory failed. mem_addr = %p, mem_size = %u",mem->addr, mem->size);
    return FAILED;
  }
  HIXL_LOGI("[HixlClient] Memory register success. ");
  return SUCCESS;
}

// 获取列表中有效的flag，考虑多线程调用，加上线程锁
int32_t HixlCSClient::AcquireFlagIndex() {
  std::lock_guard<std::mutex> lock(indices_mutex_);
  if (top_index_ == 0U) {
    return -1;
  }
  --top_index_;
  return available_indices_[top_index_];
}

Status HixlCSClient::ReleaseCompleteHandle(CompleteHandle *query_handle) {
  HIXL_CHECK_NOTNULL(query_handle);
  std::lock_guard<std::mutex> lock(indices_mutex_);
  if (top_index_ < kFlagQueueSize) {
    ++top_index_;
    available_indices_[top_index_] = query_handle->flag_index;  // 回收索引
    live_handles_[query_handle->flag_index] = nullptr;
  }
  delete query_handle;
  return SUCCESS;
}

Status HixlCSClient::ValidateAddress(bool is_get, const CommunicateMem &communicate_mem_param) {
  // 先校验用户提供的地址的有效性
  for (uint32_t i = 0; i < communicate_mem_param.list_num; i++) {
    Buffers buffer = is_get ? Buffers{communicate_mem_param.src_buf_list[i], communicate_mem_param.dst_buf_list[i]}
                            : Buffers{communicate_mem_param.dst_buf_list[i], communicate_mem_param.src_buf_list[i]};
    Status check_result =
        mem_store_.ValidateMemoryAccess(buffer.remote, communicate_mem_param.len_list[i], buffer.local);
    if (check_result != SUCCESS) {
      HIXL_LOGE(PARAM_INVALID,
                "This memory is not registered and cannot be read from or written to. "
                "Please check remote_buf:%p, local_buf:%p, buf_len:%u",
                buffer.remote, buffer.local, communicate_mem_param.len_list[i]);
      return check_result;
    }
  }
  return SUCCESS;
}

Status HixlCSClient::BatchTransferTask(bool is_get, const CommunicateMem &communicate_mem_param) {
  int32_t hccl_ret = 0;  // hccl_ret值为0时表示hccl接口执行成功
  if (is_get) {
    // 批量提交读任务
    for (uint32_t i = 0; i < communicate_mem_param.list_num; i++) {
      hccl_ret =
          HcommReadNbi(client_channel_handle_, communicate_mem_param.dst_buf_list[i],
                       const_cast<void *>(communicate_mem_param.src_buf_list[i]), communicate_mem_param.len_list[i]);
      if (hccl_ret != 0) {
        HIXL_LOGE(FAILED,
                  "[HixlClient] HcommReadNbi failed, client_channel_handle_ is %lu, dst_addr is %p, src_addr is %p, "
                  "mem_len is %lu, hccl_ret is %d.",
                  client_channel_handle_, communicate_mem_param.dst_buf_list[i],
                  const_cast<void *>(communicate_mem_param.src_buf_list[i]), communicate_mem_param.len_list[i],
                  hccl_ret);
        return FAILED;
      }
    }
  } else {
    // 批量提交写任务
    for (uint32_t i = 0; i < communicate_mem_param.list_num; i++) {
      hccl_ret =
          HcommWriteNbi(client_channel_handle_, communicate_mem_param.dst_buf_list[i],
                        const_cast<void *>(communicate_mem_param.src_buf_list[i]), communicate_mem_param.len_list[i]);
      if (hccl_ret != 0) {  // ret值为0时表示执行成功
        HIXL_LOGE(FAILED,
                  "[HixlClient] HcommWriteNbi failed, client_channel_handle_ is %lu, dst_addr is %p, src_addr is %p, "
                  "mem_len is %lu, hccl_ret is %d.",
                  client_channel_handle_, communicate_mem_param.dst_buf_list[i],
                  const_cast<void *>(communicate_mem_param.src_buf_list[i]), communicate_mem_param.len_list[i],
                  hccl_ret);
        return FAILED;
      }
    }
  }
  // 创建内存隔断，等到通道上所有的读任务执行结束后才会接着执行之后创建的读写任务
  hccl_ret = HcommChannelFence(client_channel_handle_);
  if (hccl_ret != 0) {  // ret值为0时表示执行成功
    HIXL_LOGE(FAILED, "[HixlClient] HcommChannelFence failed，client_channel_handle_ is %lu, hccl_ret is %d.",
              client_channel_handle_, hccl_ret);
    return FAILED;
  }
  return SUCCESS;
}

// 通过已经建立好的channel，从用户提取的地址列表中，批量读取server内存地址中的内容
Status HixlCSClient::BatchTransfer(bool is_get, const CommunicateMem &communicate_mem_param, void **query_handle) {
  HIXL_CHK_STATUS_RET(ValidateAddress(is_get, communicate_mem_param), "[HixlClient] ValidateAddress failed.");
  HIXL_CHK_STATUS_RET(BatchTransferTask(is_get, communicate_mem_param), "[HixlClient] BatchTransferTask failed.");
  int32_t flag_index = AcquireFlagIndex();
  if (flag_index == -1) {
    HIXL_LOGE(RESOURCE_EXHAUSTED,
              "There are a large number of transfer tasks with no query results, making it impossible to create new "
              "transfer tasks.Please first call HixlCSClientQueryCompleteStatus to check whether the transfer tasks "
              "that have been created are completed, and then create new transfer tasks.");
    return RESOURCE_EXHAUSTED;
  }
  uint64_t *flag_addr = &flag_queue_[flag_index];
  EndpointDesc endpoint = src_endpoint_->GetEndpoint();
  const char *kTransFlagName = nullptr;
  if (endpoint.loc.locType == ENDPOINT_LOC_TYPE_HOST) {
    kTransFlagName = kTransFlagNameHost;
  } else {
    kTransFlagName = kTransFlagNameDevice;
  }
  int32_t hccl_ret =
      HcommReadNbi(client_channel_handle_, flag_addr, tag_mem_descs_[kTransFlagName].addr, kFlagSizeBytes);
  if (hccl_ret != 0) {  // ret值为0时表示执行成功
    HIXL_LOGE(FAILED,
              "[HixlClient] HcommReadNbi failed, client_channel_handle_ is %lu, dst_addr is %p, src_addr is %p, "
              "mem_len is %lu, hccl_ret is %d.",
              client_channel_handle_, flag_addr, tag_mem_descs_[kTransFlagName].addr, kFlagSizeBytes, hccl_ret);
    return FAILED;
  }
  CompleteHandle *query_mem_handle = new (std::nothrow) CompleteHandle();
  if (query_mem_handle != nullptr) {
    query_mem_handle->flag_index = flag_index;
    query_mem_handle->flag_address = flag_addr;
    // 需要先创建query_handle实体，之后再传给指针。
    *query_handle = query_mem_handle;
    live_handles_[flag_index] = query_mem_handle;
  } else {
    if (top_index_ < kFlagQueueSize) {
      ++top_index_;
      available_indices_[top_index_] = query_mem_handle->flag_index;  // 回收索引
      live_handles_[query_mem_handle->flag_index] = nullptr;
    }
    HIXL_LOGE(FAILED, "Memory allocate failed; unable to generate query handle.");
    return FAILED;
  }
  return SUCCESS;
}

// 通过已经建立好的channel，检查批量读写的状态。
Status HixlCSClient::CheckStatus(CompleteHandle *query_handle, HixlCompleteStatus *status) {
  HIXL_CHECK_NOTNULL(query_handle);
  // 检验query_handle中的序号是否合规
  if (query_handle->flag_index < 0 || query_handle->flag_index >= static_cast<int32_t>(kFlagQueueSize)) {
    HIXL_LOGE(PARAM_INVALID,
              "The value of query_handle->flag_index is outside the valid verification range; please check the "
              "query_handle. query_handle->flag_index：%d",
              query_handle->flag_index);
    return PARAM_INVALID;
  }
  // 通过读取query_handle中地址的值，来判断任务的完成状态
  uint64_t *atomic_flag = query_handle->flag_address;
  HIXL_CHECK_NOTNULL(atomic_flag);
  // 查到flag变成1之后，就把其重置为0，之后告知用户读写任务已经完成。
  if (*atomic_flag == kFlagDoneValue) {
    *atomic_flag = kFlagResetValue;
    *status = HixlCompleteStatus::HIXL_COMPLETE_STATUS_COMPLETED;
    HIXL_LOGI("The current transmission task has been completed.");
    return ReleaseCompleteHandle(query_handle);  // 释放内存并回收索引
  }
  *status = HixlCompleteStatus::HIXL_COMPLETE_STATUS_WAITING;
  HIXL_LOGI("The current transmission task has not been completed.");
  return SUCCESS;
}

// 注销client的endpoint的内存信息。
Status HixlCSClient::UnRegMem(MemHandle mem_handle) {
  HIXL_CHECK_NOTNULL(mem_handle);
  HixlMemDesc desc;
  Status query_status = src_endpoint_->GetMemDesc(mem_handle, desc);
  if (query_status != SUCCESS) {
    return PARAM_INVALID;
  }
  Status result = src_endpoint_->DeregisterMem(mem_handle);
  if (result == SUCCESS) {
    Status ret = mem_store_.UnrecordMemory(false, desc.mem.addr);  // 删掉记录中client侧给endpoint分配的内存信息
    if (ret != SUCCESS) {
      HIXL_LOGE(FAILED,
                "[HixlClient] Client record memory failed. mem_addr = %p",desc.mem.addr);
      return FAILED;
    }
    return SUCCESS;
  }
  return PARAM_INVALID;
}

Status HixlCSClient::Connect(uint32_t timeout_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  HIXL_CHECK_NOTNULL(src_endpoint_);
  HIXL_CHK_BOOL_RET_STATUS(dst_endpoint_.protocol != COMM_PROTOCOL_RESERVED, PARAM_INVALID,
                           "[HixlClient] Connect called but dst_endpoint is not set in Create");
  HIXL_EVENT("[HixlClient] Connect start. Target=%s:%u, timeout=%u ms", server_ip_.c_str(), server_port_, timeout_ms);
  HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Connect(server_ip_, server_port_, socket_, timeout_ms),
                      "[HixlClient] Connect socket to %s:%u failed", server_ip_.c_str(), server_port_);
  HIXL_LOGI("[HixlClient] Socket connected (TCP ready). fd=%d", socket_);
  HIXL_CHK_STATUS_RET(ExchangeEndpointAndCreateChannelLocked(timeout_ms),
                      "[HixlClient] Exchange endpoint info failed. fd=%d, Target=%s:%u", socket_, server_ip_.c_str(),
                      server_port_);
  HIXL_EVENT("[HixlClient] Connect success. target=%s:%u, fd=%d, remote_ep_handle=%" PRIu64 ", ch=%p",
             server_ip_.c_str(), server_port_, socket_, dst_endpoint_handle_, client_channel_handle_);
  return SUCCESS;
}

Status HixlCSClient::ExchangeEndpointAndCreateChannelLocked(uint32_t timeout_ms) {
  const EndpointDesc &src_ep = src_endpoint_->GetEndpoint();
  HIXL_LOGD(
      "[HixlClient] Sending CreateChannelReq. socket: %d, timeout: %u ms, "
      "Src[protocol:%u, type:%u, id:%u], Dst[protocol:%u, type:%u, id:%u]",
      socket_, timeout_ms, src_ep.protocol, src_ep.commAddr.type, src_ep.commAddr.id, dst_endpoint_.protocol,
      dst_endpoint_.commAddr.type, dst_endpoint_.commAddr.id);
  Status ret = ConnMsgHandler::SendCreateChannelRequest(socket_, src_ep, dst_endpoint_);
  HIXL_CHK_STATUS_RET(ret, "[HixlClient] SendCreateChannelRequest failed. fd=%d", socket_);
  ChannelHandle channel_handle = 0UL;
  ret = src_endpoint_->CreateChannel(dst_endpoint_, channel_handle);
  HIXL_CHK_STATUS_RET(ret, "[HixlClient] Endpoint CreateChannel failed. Dst[id:0x%x]", dst_endpoint_.commAddr.id);
  ret = ConnMsgHandler::RecvCreateChannelResponse(socket_, dst_endpoint_handle_, timeout_ms);
  HIXL_CHK_STATUS_RET(ret, "[HixlClient] RecvCreateChannelResponse failed. fd=%d, timeout=%u ms", socket_, timeout_ms);
  HIXL_LOGI("[HixlClient] Connect: remote endpoint handle = %" PRIu64, dst_endpoint_handle_);
  client_channel_handle_ = channel_handle;
  HIXL_LOGI("[HixlClient] Channel Ready. client_channel_handle_=%p", client_channel_handle_);
  return SUCCESS;
}

Status HixlCSClient::GetRemoteMem(HcommMem **remote_mem_list, char ***mem_tag_list, uint32_t *list_num,
                                  uint32_t timeout_ms) {
  HIXL_EVENT("[HixlClient] GetRemoteMem begin. fd=%d, remote_ep_handle=%" PRIu64 ", timeout=%u ms", socket_,
             dst_endpoint_handle_, timeout_ms);
  HIXL_CHECK_NOTNULL(remote_mem_list);
  HIXL_CHECK_NOTNULL(mem_tag_list);
  HIXL_CHECK_NOTNULL(list_num);
  *remote_mem_list = nullptr;
  *mem_tag_list = nullptr;
  std::lock_guard<std::mutex> lock(mutex_);
  HIXL_CHECK_NOTNULL(src_endpoint_);
  Status ret = MemMsgHandler::SendGetRemoteMemRequest(socket_, dst_endpoint_handle_, timeout_ms);
  HIXL_CHK_STATUS_RET(ret, "[HixlClient] SendGetRemoteMemRequest failed. fd=%d, remote_ep_handle=%" PRIu64, socket_,
                      dst_endpoint_handle_);
  std::vector<HixlMemDesc> mem_descs;
  ret = MemMsgHandler::RecvGetRemoteMemResponse(socket_, mem_descs, timeout_ms);
  HIXL_CHK_STATUS_RET(ret, "[HixlClient] RecvGetRemoteMemResponse failed. fd=%d, timeout=%u ms", socket_, timeout_ms);
  HIXL_LOGD("[HixlClient] Recv remote mem descs success. Count=%zu", mem_descs.size());
  ret = ImportRemoteMem(mem_descs, remote_mem_list, mem_tag_list, list_num);
  HIXL_CHK_STATUS_RET(ret, "[HixlClient] ImportRemoteMem failed. desc_count=%zu", mem_descs.size());
  HIXL_EVENT("[HixlClient] GetRemoteMem success. fd=%d, remote_ep_handle=%" PRIu64 ", imported=%u", socket_,
             dst_endpoint_handle_, *list_num);
  return SUCCESS;
}

void HixlCSClient::FillOutputParams(ImportCtx &ctx, HcommMem **remote_mem_list, char ***mem_tag_list,
                                    uint32_t *list_num) {
  imported_remote_bufs_ = std::move(ctx.imported);
  recorded_remote_addrs_ = std::move(ctx.recorded_addrs);
  tag_mem_descs_ = std::move(ctx.tag_mem_map);
  remote_mems_out_ = std::move(ctx.mems);
  remote_tag_storage_.clear();
  remote_tag_ptrs_.clear();
  remote_tag_storage_ = std::move(ctx.tag_storage);
  BuildTagPtrs(remote_tag_storage_, remote_tag_ptrs_);
  *mem_tag_list = remote_tag_ptrs_.empty() ? nullptr : remote_tag_ptrs_.data();
  *remote_mem_list = remote_mems_out_.empty() ? nullptr : remote_mems_out_.data();
  *list_num = static_cast<uint32_t>(remote_mems_out_.size());
}

Status HixlCSClient::ImportRemoteMem(std::vector<HixlMemDesc> &desc_list, HcommMem **remote_mem_list,
                                     char ***mem_tag_list, uint32_t *list_num) {
  HIXL_DISMISSABLE_GUARD(free_export_desc, [&desc_list]() { FreeExportDesc(desc_list); });
  *list_num = static_cast<uint32_t>(desc_list.size());
  Status ret = ClearRemoteMemInfo();
  HIXL_CHK_STATUS_RET(ret, "[HixlClient] ClearRemoteMemInfo before ImportRemoteMem failed");
  if (*list_num == 0U) {
    HIXL_LOGI("[HixlClient] Remote mem list is empty, nothing to import.");
    return SUCCESS;
  }
  ret = ValidateExportDescList(desc_list);
  HIXL_CHK_STATUS_RET(ret, "[HixlClient] ValidateExportDescList failed");
  HIXL_CHECK_NOTNULL(src_endpoint_);
  EndpointHandle ep_handle = src_endpoint_->GetHandle();
  HIXL_CHK_BOOL_RET_STATUS(ep_handle != nullptr, FAILED, "[HixlClient] ImportRemoteMem: endpoint handle is null");
  ImportCtx ctx;
  ctx.ep = src_endpoint_.get();
  ctx.ep_handle = ep_handle;
  ctx.store = &mem_store_;
  ctx.num = *list_num;
  ctx.imported.reserve(ctx.num);
  ctx.recorded_addrs.reserve(ctx.num);
  ctx.mems.reserve(ctx.num);
  ctx.tag_storage.reserve(ctx.num);
  ret = ImportAllDescs(ctx, desc_list);
  if (ret != SUCCESS) {
    HIXL_LOGW("[HixlClient] RollbackImport triggered. Cleaning up %zu imported bufs.", desc_list.size());
    CloseImportedBufs(ctx.ep_handle, desc_list);
    return ret;
  }
  desc_list_ = std::move(desc_list);
  FillOutputParams(ctx, remote_mem_list, mem_tag_list, list_num);
  HIXL_DISMISS_GUARD(free_export_desc);
  return SUCCESS;
}

void UnrecordAddrs(HixlMemStore &store, std::vector<void*> &addrs) {
  for (auto *addr : addrs) {
    if (addr == nullptr) {
      continue;
    }

    const Status ret = store.UnrecordMemory(true, addr);
    if (ret != SUCCESS) {
      HIXL_LOGW("[HixlClient] UnrecordMemory failed. addr=%p ret=%u", addr, static_cast<uint32_t>(ret));
    }
  }
  addrs.clear();
}

Status HixlCSClient::ClearRemoteMemInfo() {
  EndpointHandle ep_handle = (src_endpoint_ != nullptr) ? src_endpoint_->GetHandle() : nullptr;
  const size_t buf_cnt = imported_remote_bufs_.size();
  const size_t addr_cnt = recorded_remote_addrs_.size();
  if (buf_cnt > 0U || addr_cnt > 0U) {
    HIXL_LOGI("[HixlClient] Cleaning up remote mem info. Bufs=%zu, Addrs=%zu", buf_cnt, addr_cnt);
  }
  if (!desc_list_.empty()) {
    if (ep_handle != nullptr) {
      CloseImportedBufs(ep_handle, desc_list_);
    } else {
      HIXL_LOGW("[HixlClient] ClearRemoteMemInfo: endpoint handle null, skip MemClose for %zu bufs", desc_list_.size());
    }
    for (auto &desc : desc_list_) {
      if (desc.export_desc != nullptr) {
        std::free(desc.export_desc);
        desc.export_desc = nullptr;
      }
    }
    desc_list_.clear();
  }
  if (!recorded_remote_addrs_.empty()) {
    UnrecordAddrs(mem_store_, recorded_remote_addrs_);
  }
  tag_mem_descs_.clear();
  remote_mems_out_.clear();
  remote_tag_ptrs_.clear();
  remote_tag_storage_.clear();
  return SUCCESS;
}


Status HixlCSClient::Destroy() {
  HIXL_EVENT("[HixlClient] Destroy start. fd=%d, imported_bufs=%zu, recorded_addrs=%zu", socket_,
             imported_remote_bufs_.size(), recorded_remote_addrs_.size());
  std::lock_guard<std::mutex> lock(mutex_);
  Status first_error = SUCCESS;
  Status ret = ClearRemoteMemInfo();
  if (ret != SUCCESS) {
    HIXL_LOGW("[HixlClient] ClearRemoteMemInfo failed. fd=%d, ret=%u", socket_, static_cast<uint32_t>(ret));
    if (first_error == SUCCESS) {
      first_error = ret;
    }
  }
  if (socket_ != -1) {
    HIXL_LOGI("[HixlClient] Closing socket. fd=%d", socket_);
    close(socket_);
    socket_ = -1;
  }
  if (src_endpoint_ != nullptr) {
    ret = src_endpoint_->Finalize();
    if (ret != SUCCESS) {
      HIXL_LOGW("[HixlClient] Finalize endpoint failed in Destroy. ep_handle=%p, ret=%u", src_endpoint_->GetHandle(),
                static_cast<uint32_t>(ret));
      if (first_error == SUCCESS) {
        first_error = ret;
      }
    }
    src_endpoint_.reset();
  }
  HIXL_EVENT("[HixlClient] Destroy done. first_error=%u", static_cast<uint32_t>(first_error));
  return first_error;
}
}  // namespace hixl
