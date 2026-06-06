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
#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <limits>
#include <securec.h>
#include <thread>
#include "acl/acl.h"
#include "nlohmann/json.hpp"
#include "hixl/hixl_types.h"
#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/hixl_utils.h"
#include "common/llm_utils.h"
#include "common/scope_guard.h"
#include "common/ctrl_msg_plugin.h"
#include "conn_msg_handler.h"
#include "host_register_proxy.h"
#include "load_kernel.h"
#include "mem_msg_handler.h"
#include "proxy/hcomm_proxy.h"
#include "proxy/hccp_proxy.h"
#include "rt_external.h"

namespace hixl {
namespace {
std::atomic<uint32_t> g_next_channel_index{0U};
constexpr uint32_t kDeviceTransferPoolSize = 128U;
constexpr uint32_t kDeviceCompleteMagic = 0x55425548U;
constexpr uint32_t kRoceCompleteMagic = 0x524F4345U;
constexpr const char *kTransFlagNameHost = "_hixl_builtin_host_trans_flag";
constexpr const char *kTransFlagNameDevice = "_hixl_builtin_dev_trans_flag";
constexpr uint64_t kDeviceFlagDoneValue = 1ULL;
constexpr uint64_t kDeviceFlagInitValue = 0ULL;
constexpr const char *kDeviceFuncGet = "HixlBatchGet";
constexpr const char *kDeviceFuncPut = "HixlBatchPut";
constexpr uint32_t kFlagSizeBytes = 8;
constexpr uint64_t kFlagDoneValue = 1ULL;
constexpr uint64_t kFlagResetValue = 0ULL;
constexpr uint32_t kCustomTimeoutMs = 1800;
constexpr uint32_t kMaxKernelBatchSize = 128U;
// notifywait默认1836ms等待时长，通过异步接口提供给用户使用，由用户感知超时主动退出，不使用notify的超时时间
constexpr uint16_t kNotifyDefaultWaitTimeMs = 27 * 68;
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
    const HcclResult ret = HcommProxy::MemUnimport(ep_handle, b.export_desc, b.export_len);
    if (ret != HCCL_SUCCESS) {
      HIXL_LOGW("[HixlClient] HcommMemUnimport failed. addr=%p size=%" PRIu64 " ret=0x%X", b.mem.addr, b.mem.size,
                static_cast<uint32_t>(ret));
    }
  }
}

void UnrecordAddrs(hixl::HixlMemStore &store, std::vector<void *> &addrs) {
  for (auto *addr : addrs) {
    if (addr == nullptr) {
      continue;
    }
    const hixl::Status ret = store.UnrecordMemory(true, addr);
    if (ret != hixl::SUCCESS) {
      HIXL_LOGW("[HixlClient] UnrecordMemory failed. addr=%p ret=%u", addr, static_cast<uint32_t>(ret));
    }
  }
  addrs.clear();
}

hixl::Status ImportOneDesc(hixl::ImportCtx &ctx, uint32_t idx, hixl::HixlMemDesc &desc) {
  CommMem buf{};
  hixl::Status ret = ctx.ep->MemImport(desc.export_desc, desc.export_len, buf);
  const char *safe_tag = desc.tag.empty() ? "<empty>" : desc.tag.c_str();
  if (ret != hixl::SUCCESS) {
    HIXL_LOGE(ret, "[HixlClient] MemImport failed, idx=%u, tag=%s", idx, safe_tag);
    return ret;
  }
  ctx.imported.emplace_back(buf);
  desc.is_imported = true;
  CommMem mem{};
  mem.type = desc.mem.type;
  mem.addr = desc.mem.addr;
  mem.size = desc.mem.size;
  bool is_host_mem = desc.mem.type == COMM_MEM_TYPE_HOST;
  HIXL_LOGI("[HixlClient] ImportOneDesc desc.tag=%s mem.addr=%p", safe_tag, mem.addr);
  ctx.mems.emplace_back(mem);
  if (!desc.tag.empty()) {
    ctx.tag_mem_map[desc.tag] = mem;
  }
  HIXL_LOGD("[HixlClient] Imported mem[%u]: tag='%s', addr=%p, size=%llu", idx, safe_tag, mem.addr, mem.size);
  ret = ctx.store->RecordMemory(true, mem.addr, static_cast<size_t>(mem.size), is_host_mem, desc.registered_dev_mem);
  if (ret == hixl::SUCCESS) {
    ctx.recorded_addrs.emplace_back(mem.addr);
  } else {
    HIXL_LOGE(ret,
              "[HixlClient] RecordMemory(server) failed! This memory may have been registered. idx=%u, tag=%s, "
              "addr=%p, size=%llu",
              idx, safe_tag, mem.addr, mem.size);
    return ret;
  }
  if (!desc.tag.empty()) {
    return AppendTagStorage(ctx.tag_storage, desc.tag);
  }
  return hixl::SUCCESS;
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

bool HixlCSClient::IsDeviceEndpoint(const EndpointDesc &ep) {
  return (ep.loc.locType == ENDPOINT_LOC_TYPE_DEVICE);
}

Status HixlCSClient::ResolveNotifyDeviceAddress(aclrtNotify notify, uint64_t &notify_addr, uint32_t &notify_len) {
  HIXL_CHECK_NOTNULL(notify);
  const EndpointDesc &ep = local_endpoint_->GetEndpoint();
  if (ep.protocol == COMM_PROTOCOL_HCCS) {
    return SUCCESS;
  } else if (ep.protocol == COMM_PROTOCOL_ROCE) {
    HIXL_LOGI("[HixlClient] ResolveNotifyDeviceAddress for ROCE");
    return HccpProxy::RaGetNotifyAddrLen(device_id_, notify, notify_addr, notify_len);
  }
  constexpr rtDevResProcType_t kNotifyDevResProcType = RT_PROCESS_HCCP;
  constexpr rtDevResType_t kNotifyDevResType = RT_RES_TYPE_STARS_NOTIFY_RECORD;
  uint32_t notify_id = 0U;
  HIXL_CHK_ACL_RET(aclrtGetNotifyId(notify, &notify_id), "[HixlClient] aclrtGetNotifyId failed");
  rtDevResInfo res_info{};
  res_info.dieId = 0U;
  res_info.procType = kNotifyDevResProcType;
  res_info.resType = kNotifyDevResType;
  res_info.resId = notify_id;
  res_info.flag = 0U;
  rtDevResAddrInfo addr_info{};
  addr_info.resAddress = &notify_addr;
  addr_info.len = &notify_len;
  HIXL_CHK_ACL_RET(rtGetDevResAddress(&res_info, &addr_info), "[HixlClient] rtGetDevResAddress failed");
  return SUCCESS;
}

Status HixlCSClient::RegisterNotifyMemForAllSlots(const std::vector<TransferPool::SlotHandle> &slots) {
  notify_mem_handles_.clear();
  notify_mem_handles_.resize(slots.size());
  for (size_t i = 0U; i < slots.size(); ++i) {
    uint64_t notify_addr = 0U;
    uint32_t notify_len = 0U;
    HIXL_CHK_STATUS_RET(ResolveNotifyDeviceAddress(slots[i].notify, notify_addr, notify_len),
                        "[HixlClient] ResolveNotifyDeviceAddress failed for slot %zu", i);
    CommMem mem{};
    mem.type = COMM_MEM_TYPE_DEVICE;
    mem.addr = reinterpret_cast<void *>(static_cast<uintptr_t>(notify_addr));
    mem.size = notify_len;
    HIXL_CHK_STATUS_RET(local_endpoint_->RegisterMem(nullptr, mem, notify_mem_handles_[i]),
                        "[HixlClient] register notify mem failed for slot %zu", i);
  }
  return SUCCESS;
}

HixlCSClient::HixlCSClient() : mem_store_() {
  for (size_t i = 0U; i < kFlagQueueSize; ++i) {
    available_indices_[i] = i;
    live_handles_[i] = nullptr;
  }
}

HixlCSClient::~HixlCSClient() {
  (void)Destroy();
  if (flag_queue_ != nullptr) {
    free(flag_queue_);
    flag_queue_ = nullptr;
  }
  for (size_t i = 0; i < kFlagQueueSize; ++i) {
    if (live_handles_[i] != nullptr) {
      delete live_handles_[i];
      live_handles_[i] = nullptr;
    }
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
  CommMem mem{};
  mem.type = COMM_MEM_TYPE_HOST;
  mem.addr = flag_queue_;
  mem.size = kFlagQueueSize * sizeof(uint64_t);
  MemHandle flag_handle = nullptr;
  HIXL_CHK_STATUS_RET(RegMem(kTransFlagNameHost, &mem, &flag_handle),
                      "Failed to reg HOST trans finished flag, mem.addr: %p, mem.size: %lu.", mem.addr, mem.size);
  HIXL_DISMISS_GUARD(free_flag_mem);
  return SUCCESS;
}

Status HixlCSClient::InitBaseClient(const HixlClientDesc *client_desc) {
  server_ip_ = client_desc->server_ip;
  server_port_ = client_desc->server_port;
  EndpointDesc local_endpoint = *(client_desc->local_endpoint);
  local_endpoint_ = MakeShared<Endpoint>(local_endpoint);
  tc_ = client_desc->tc;
  sl_ = client_desc->sl;
  HIXL_CHECK_NOTNULL(local_endpoint_);
  Status ret = local_endpoint_->Initialize();
  HIXL_CHK_STATUS_RET(ret,
                      "[HixlClient] Failed to initialize src endpoint. "
                      "Check Config: [Loc:%d, protocol:%d, AddrVal:0x%x]",
                      local_endpoint.loc.locType, local_endpoint.protocol, local_endpoint.commAddr.id);
  HIXL_LOGI("[HixlClient] local_endpoint initialized. ep_handle=%p", local_endpoint_->GetHandle());
  remote_endpoint_ = *(client_desc->remote_endpoint);
  CtrlMsgPlugin::Initialize();
  HIXL_LOGD("[HixlClient] CtrlMsgPlugin initialized");
  if (local_endpoint_->GetEndpoint().loc.locType == ENDPOINT_LOC_TYPE_HOST) {
    Status init_ret = InitFlagQueue();
    HIXL_CHK_STATUS_RET(init_ret, "[HixlClient] Failed to initialize flag queue.");
  }
  return SUCCESS;
}

Status HixlCSClient::InitDeviceResource() {
  const EndpointDesc &ep = local_endpoint_->GetEndpoint();
  if (!IsDeviceEndpoint(ep)) {
    device_id_ = -1;
    return SUCCESS;
  }
  HIXL_CHK_ACL_RET(aclrtGetDevice(&device_id_), "[HixlClient] aclrtGetDevice failed");
  HIXL_LOGI("[HixlClient] device_id=%d", device_id_);
  hixl::TemporaryRtContext with_context(nullptr);  // 创建context会切换当前context, 因此需要在析构时恢复原用户context
  auto *pool = TransferPool::GetInstance(device_id_);
  HIXL_CHECK_NOTNULL(pool);
  Status pret = pool->Initialize(kDeviceTransferPoolSize);
  HIXL_CHK_STATUS_RET(pret, "[HixlClient] TransferPool Initialize failed. devId=%d", device_id_);
  std::vector<TransferPool::SlotHandle> all_slots;
  HIXL_CHK_STATUS_RET(pool->GetAllSlots(all_slots),
                      "[HixlClient] TransferPool GetAllSlots failed. devId=%d", device_id_);

  // 预先解析所有 slot 的 notify 地址，避免传输时重新获取
  slot_notify_addrs_.clear();
  slot_notify_addrs_.resize(all_slots.size());
  for (size_t i = 0U; i < all_slots.size(); ++i) {
    uint64_t notify_addr = 0U;
    uint32_t notify_len = 0U;
    HIXL_CHK_STATUS_RET(ResolveNotifyDeviceAddress(all_slots[i].notify, notify_addr, notify_len),
                        "[HixlClient] ResolveNotifyDeviceAddress failed for slot %zu", i);
    slot_notify_addrs_[i] = notify_addr;
    if (i == 0U) {
      notify_len_ = notify_len;
    }
  }

  if (ep.protocol != COMM_PROTOCOL_ROCE && ep.protocol != COMM_PROTOCOL_HCCS) {
    HIXL_CHK_STATUS_RET(RegisterNotifyMemForAllSlots(all_slots),
                        "[HixlClient] RegisterNotifyMemForAllSlots failed. devId=%d", device_id_);
  }

  // 提前加载 kernel，避免传输时引入耗时
  {
    std::lock_guard<std::mutex> lock(device_mu_);
    HIXL_CHK_STATUS_RET(EnsureDeviceKernelLoadedLocked(), "[HixlClient] EnsureDeviceKernelLoadedLocked failed");
  }
  return SUCCESS;
}

Status HixlCSClient::Create(const HixlClientDesc *client_desc, const HixlClientConfig *config) {
  HIXL_CHECK_NOTNULL(client_desc->server_ip);
  HIXL_CHECK_NOTNULL(client_desc->local_endpoint);
  HIXL_CHECK_NOTNULL(client_desc->remote_endpoint);
  HIXL_CHECK_NOTNULL(config);
  HIXL_CHK_STATUS_RET(GlobalConfig::Parse(config->global_resource_config, global_config_),
                      "[HixlClient] Failed to parse global_resource_config");
  HIXL_EVENT(
      "[HixlClient] Create begin. Server=%s:%u. "
      "SrcEndpoint[Loc:%d, protocol:%d, commAddr.Type:%d, commAddr.id:0x%x], "
      "DstEndpoint[Loc:%d, protocol:%d, commAddr.Type:%d, commAddr.id:0x%x]",
      client_desc->server_ip, client_desc->server_port, client_desc->local_endpoint->loc.locType,
      client_desc->local_endpoint->protocol, client_desc->local_endpoint->commAddr.type,
      client_desc->local_endpoint->commAddr.id, client_desc->remote_endpoint->loc.locType,
      client_desc->remote_endpoint->protocol, client_desc->remote_endpoint->commAddr.type,
      client_desc->remote_endpoint->commAddr.id);
  std::lock_guard<std::mutex> lock(mutex_);
  HIXL_CHK_STATUS_RET(InitBaseClient(client_desc), "[HixlClient] InitBaseClient failed");
  EndpointHandle endpoint_handle = local_endpoint_->GetHandle();
  HIXL_EVENT("[HixlClient] Create success. server=%s:%u, src_ep_handle=%p", server_ip_.c_str(), server_port_,
             endpoint_handle);
  HIXL_CHK_STATUS_RET(InitDeviceResource(), "[HixlClient] InitDeviceResource failed");
  return SUCCESS;
}

// 注册client的endpoint的内存信息到内存注册表中。mem是一个结构体，其中记录了内存类型、地址和大小。
Status HixlCSClient::RegMem(const char *mem_tag, const CommMem *mem, MemHandle *mem_handle) {
  auto ctx_guard = GetContextGuard();
  (void)ctx_guard;
  HIXL_CHECK_NOTNULL(mem);
  auto check_result = mem_store_.CheckMemoryForRegister(false, mem->addr, mem->size);
  if (check_result) {
    HIXL_LOGE(PARAM_INVALID,
              "[HixlClient] Memory registration failed. This memory may overlap with the already recorded memory. "
              "Please check Mem, mem_addr: %p, mem_size: %u.",
              mem->addr, mem->size);
    return PARAM_INVALID;
  }
  MemHandle ep_mem_handle = nullptr;
  HIXL_CHK_STATUS_RET(local_endpoint_->RegisterMem(mem_tag, *mem, ep_mem_handle),
                      "[HixlClient] Failed to register client endpoint mem.");
  *mem_handle = ep_mem_handle;
  bool is_host_mem = mem->type == COMM_MEM_TYPE_HOST;
  void *register_dev_addr = nullptr;
  const auto &local_endpoint_desc = local_endpoint_->GetEndpoint();
  if (is_host_mem && (local_endpoint_desc.protocol == COMM_PROTOCOL_UBOE)) {
    HIXL_CHK_STATUS_RET(HostRegisterProxy::GetRegisteredDeviceAddrByDev(local_endpoint_desc.loc.device.devPhyId,
                                                                        mem->addr, register_dev_addr),
                        "Failed to get registered device addr, devPhyId=%d, addr=%p",
                        local_endpoint_desc.loc.device.devPhyId, mem->addr);
  }
  // 记录client侧给endpoint分配的内存信息
  Status ret = mem_store_.RecordMemory(false, mem->addr, mem->size, is_host_mem, register_dev_addr);
  if (ret != SUCCESS) {
    HIXL_LOGE(FAILED, "[HixlClient] Client record memory failed. mem_addr = %p, mem_size = %u", mem->addr, mem->size);
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

// 释放flag索引
void HixlCSClient::ReleaseFlagIndex(int32_t flag_index) {
  std::lock_guard<std::mutex> lock(indices_mutex_);
  if (top_index_ < kFlagQueueSize) {
    available_indices_[top_index_] = flag_index;
    flag_queue_[flag_index] = kFlagResetValue;  // 将flag重置为0
    ++top_index_;
  }
}

Status HixlCSClient::ReleaseCompleteHandle(CompleteHandleInfo *query_handle) {
  HIXL_CHECK_NOTNULL(query_handle);
  if (top_index_ < kFlagQueueSize) {
    ReleaseFlagIndex(query_handle->flag_index);
    live_handles_[query_handle->flag_index] = nullptr;
  }
  delete query_handle;
  return SUCCESS;
}

Status HixlCSClient::ValidateAddress(uint32_t list_num, const HixlOneSideOpDesc *desc_list) {
  HIXL_CHK_STATUS_RET(mem_store_.BatchValidateMemoryAccess(list_num, desc_list), "Validate address failed, list_num=%u",
                      list_num);
  return SUCCESS;
}

Status HixlCSClient::TransferWithRetry(bool is_get, uint64_t channel_handle, void *dst_buf, const void *src_buf,
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
      HIXL_LOGE(FAILED,
                "[HixlClient] Transfer failed, is_get=%d, channel_handle=%lu, dst_addr=%p, src_addr=%p, "
                "mem_len=%lu, hccl_ret=%d.",
                is_get, channel_handle, dst_buf, src_buf, len, hccl_ret);
      return FAILED;
    }

    // 检查超时
    auto current_time = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - start_time).count();
    if (elapsed_ms >= kRetryTimeoutMs) {
      HIXL_LOGE(FAILED,
                "[HixlClient] Transfer timeout after %ld ms, is_get=%d, channel_handle=%lu, dst_addr=%p, "
                "src_addr=%p, mem_len=%lu, hccl_ret=%d.",
                elapsed_ms, is_get, channel_handle, dst_buf, src_buf, len, hccl_ret);
      return FAILED;
    }

    HIXL_LOGW("[HixlClient] Transfer ret=%d, retrying. elapsed_ms=%ld, dst_addr=%p, src_addr=%p, len=%lu, is_get=%d.",
              hccl_ret, elapsed_ms, dst_buf, src_buf, len, is_get);

    // 执行 Fence 后重试，执行Fence后通常极少会出现再次重试的问题
    hccl_ret = HcommProxy::ChannelFenceOnThread(static_cast<ThreadHandle>(0), channel_handle);
    if (hccl_ret != HCCL_SUCCESS) {
      HIXL_LOGE(FAILED, "[HixlClient] HcommChannelFenceOnThread failed, channel_handle=%lu, hccl_ret=%d.",
                channel_handle, hccl_ret);
      return FAILED;
    }
  }
}

Status HixlCSClient::BatchTransferTask(bool is_get, uint32_t list_num, const HixlOneSideOpDesc *desc_list) {
  for (uint32_t i = 0; i < list_num; i++) {
    void *dst = is_get ? desc_list[i].local_buf : desc_list[i].remote_buf;
    const void *src = is_get ? desc_list[i].remote_buf : desc_list[i].local_buf;
    auto ret = TransferWithRetry(is_get, client_channel_handle_, dst, src, desc_list[i].len);
    if (ret != SUCCESS) {
      return FAILED;
    }
  }
  int32_t hccl_ret = HcommProxy::ChannelFenceOnThread(static_cast<ThreadHandle>(0), client_channel_handle_);
  if (hccl_ret != SUCCESS) {
    HIXL_LOGE(FAILED, "[HixlClient] HcommChannelFenceOnThread failed, client_channel_handle_ is %lu, hccl_ret is %d.",
              client_channel_handle_, hccl_ret);
    return FAILED;
  }
  return SUCCESS;
}
Status HixlCSClient::BatchTransferHostAsync(bool is_get, uint32_t list_num, const HixlOneSideOpDesc *desc_list,
                                            void **query_handle) {
  uint32_t num_chunks = (list_num + kMaxKernelBatchSize - 1U) / kMaxKernelBatchSize;
  for (uint32_t chunk_idx = 0U; chunk_idx < num_chunks; ++chunk_idx) {
    uint32_t chunk_offset = chunk_idx * kMaxKernelBatchSize;
    uint32_t chunk_size = std::min(kMaxKernelBatchSize, list_num - chunk_offset);
    HIXL_CHK_STATUS_RET(BatchTransferTask(is_get, chunk_size, desc_list + chunk_offset),
                        "[HixlClient] BatchTransferTask failed for chunk %u/%u", chunk_idx, num_chunks);
  }
  int32_t flag_index = AcquireFlagIndex();
  if (flag_index == -1) {
    HIXL_LOGE(RESOURCE_EXHAUSTED,
              "There are a large number of transfer tasks with no query results, making it impossible to create new "
              "transfer tasks.Please first call HixlCSClientQueryCompleteStatus to check whether the transfer tasks "
              "that have been created are completed, and then create new transfer tasks.");
    return RESOURCE_EXHAUSTED;
  }
  // 使用 scope_guard 自动管理 flag 资源的释放
  HIXL_DISMISSABLE_GUARD(flag_guard, ([this, flag_index]() { ReleaseFlagIndex(flag_index); }));
  uint64_t *flag_addr = &flag_queue_[flag_index];
  const char *kTransFlagName = nullptr;
  if (remote_endpoint_.loc.locType == ENDPOINT_LOC_TYPE_HOST) {
    kTransFlagName = kTransFlagNameHost;
  } else {
    kTransFlagName = kTransFlagNameDevice;
  }
  int32_t hccl_ret = HcommProxy::ReadNbiOnThread(static_cast<ThreadHandle>(0), client_channel_handle_, flag_addr,
                                                 tag_mem_descs_[kTransFlagName].addr, kFlagSizeBytes);
  if (hccl_ret != 0) {  // ret值为0时表示执行成功
    HIXL_LOGE(
        FAILED,
        "[HixlClient] HcommReadNbiOnThread failed, client_channel_handle_ is %lu, dst_addr is %p, src_addr is %p, "
        "mem_len is %lu, hccl_ret is %d.",
        client_channel_handle_, flag_addr, tag_mem_descs_[kTransFlagName].addr, kFlagSizeBytes, hccl_ret);
    return FAILED;
  }
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

Status HixlCSClient::EnsureDeviceRemoteFlagInitedLocked() {
  if (device_remote_flag_inited_) {
    return SUCCESS;
  }
  const char *kTransFlagName = nullptr;
  if (remote_endpoint_.loc.locType == ENDPOINT_LOC_TYPE_HOST) {
    kTransFlagName = kTransFlagNameHost;
  } else {
    kTransFlagName = kTransFlagNameDevice;
  }
  const auto it = tag_mem_descs_.find(kTransFlagName);
  // tag 不存在时，不报错，保持未初始化状态
  // 实际传输时 PrepareDeviceRemoteFlagAndKernel 会检查并报错
  if (it == tag_mem_descs_.end()) {
    HIXL_LOGD("[HixlClient] builtin remote_flag tag not found: %s, skip initialization", kTransFlagName);
    return SUCCESS;
  }

  const CommMem &mem = it->second;
  if (mem.addr == nullptr) {
    HIXL_LOGD("[HixlClient] builtin remote_flag addr is null, skip initialization");
    return SUCCESS;
  }

  if (mem.size < static_cast<uint64_t>(sizeof(uint64_t))) {
    HIXL_LOGD("[HixlClient] builtin remote_flag size too small. size=%" PRIu64 ", skip initialization", mem.size);
    return SUCCESS;
  }

  device_remote_flag_addr_ = mem.addr;
  device_remote_flag_size_ = mem.size;
  device_remote_flag_inited_ = true;

  HIXL_LOGI("[HixlClient] builtin remote_flag ready. addr=%p u64=%p size=%" PRIu64, mem.addr, device_remote_flag_addr_,
            device_remote_flag_size_);
  return SUCCESS;
}

Status HixlCSClient::ReleaseDevCompleteHandle(DeviceCompleteHandle *handle) {
  if (handle == nullptr) {
    return SUCCESS;
  }
  HIXL_LOGI("[HixlCSClient] ReleaseDevCompleteHandle start");
  if (handle->magic != kDeviceCompleteMagic) {
    HIXL_LOGE(PARAM_INVALID, "[HixlCSClient] ReleaseDevCompleteHandle bad magic=0x%X", handle->magic);
    return PARAM_INVALID;
  }
  (void)pending_device_handles_.erase(handle);

  // Free independent host_flag (allocated for async transfers)
  if (handle->host_flag != nullptr) {
    HIXL_CHK_ACL(aclrtFreeHost(handle->host_flag));
    handle->host_flag = nullptr;
  }

  // Free device op desc buffer
  if (handle->dev_op_desc_buf != nullptr) {
    HIXL_CHK_ACL(aclrtFree(handle->dev_op_desc_buf));
    handle->dev_op_desc_buf = nullptr;
  }

  // Release shared slot reference
  std::shared_ptr<TransferPool::SlotHandle> slot_ref = std::move(handle->shared_slot);
  if (slot_ref != nullptr) {
    ReleaseSharedSlotRef(slot_ref);
  }

  handle->magic = 0U;
  delete handle;
  HIXL_LOGI("[HixlCSClient] ReleaseDevCompleteHandle end");
  return SUCCESS;
}

Status HixlCSClient::AcquireSharedSlot(std::shared_ptr<TransferPool::SlotHandle> &slot_out) {
  std::lock_guard<std::mutex> lock(active_slot_mu_);

  // If active slot exists (pending transfer), reuse it
  if (active_slot_ != nullptr && active_slot_.use_count() > 0) {
    const long ref_before = active_slot_.use_count();
    slot_out = active_slot_;  // Share existing slot (increases ref_count)
    HIXL_LOGI("[HixlClient] Reusing active slot. slot_index=%u ref_before=%ld ref_after=%ld", active_slot_->slot_index,
              ref_before, active_slot_.use_count());
    return SUCCESS;
  }

  // No active slot - acquire new from pool
  TransferPool::SlotHandle new_slot{};
  auto *pool = TransferPool::GetInstance(device_id_);
  HIXL_CHECK_NOTNULL(pool);
  HIXL_CHK_STATUS_RET(pool->Acquire(&new_slot),
                      "[HixlClient] Acquire slot from pool failed");

  active_slot_ = std::make_shared<TransferPool::SlotHandle>(new_slot);
  slot_out = active_slot_;
  HIXL_LOGI("[HixlClient] Acquired new slot. slot_index=%u ref_count=%ld", new_slot.slot_index,
            active_slot_.use_count());
  return SUCCESS;
}

void HixlCSClient::ReleaseSharedSlotRef(std::shared_ptr<TransferPool::SlotHandle> &slot_ref) {
  std::lock_guard<std::mutex> lock(active_slot_mu_);

  if (slot_ref == nullptr) {
    return;
  }
  if (active_slot_ == nullptr || active_slot_ != slot_ref) {
    slot_ref.reset();
    return;
  }

  const uint32_t slot_index = active_slot_->slot_index;
  const long ref_before = active_slot_.use_count();
  slot_ref.reset();
  HIXL_LOGI("[HixlClient] ReleaseSharedSlotRef. slot_index=%u ref_before=%ld ref_after=%ld", slot_index, ref_before,
            active_slot_.use_count());

  if (active_slot_.use_count() == 1) {
    auto *rel_pool = TransferPool::GetInstance(active_slot_->device_id);
    if (rel_pool != nullptr) {
      rel_pool->Release(*active_slot_);
    }
    active_slot_.reset();
    HIXL_LOGI("[HixlClient] Released slot to pool. slot_index=%u", slot_index);
  }
}

void HixlCSClient::CleanupActiveSlot() {
  std::lock_guard<std::mutex> lock(active_slot_mu_);
  if (active_slot_ != nullptr) {
    auto *abort_pool = TransferPool::GetInstance(active_slot_->device_id);
    if (abort_pool != nullptr) {
      abort_pool->Abort(*active_slot_);
    }
    HIXL_LOGI("[HixlClient] Aborted active slot. slot_index=%u", active_slot_->slot_index);
    active_slot_.reset();
  }
}

Status HixlCSClient::EnsureDeviceKernelLoadedLocked() {
  if (device_kernel_loaded_) {
    return SUCCESS;
  }
  HIXL_LOGI("[HixlClient] EnsureDeviceKernelLoadedLocked start. Loading UB kernels...");
  HIXL_CHK_BOOL_RET_STATUS(device_id_ >= 0, FAILED, "[HixlClient] Invalid device_id_: %d", device_id_);
  hixl::DeviceFuncHandles func_handles{};
  Status ret = hixl::LoadDeviceKernelAndGetHandles(kDeviceFuncGet, kDeviceFuncPut, device_kernel_handle_, func_handles);
  if (ret != SUCCESS) {
    HIXL_LOGE(ret, "[HixlClient] LoadDeviceKernelAndGetHandles failed. dev=%d", device_id_);
    return ret;
  }
  HIXL_CHECK_NOTNULL(func_handles.batch_get, "[HixlClient] batchGet stub is null");
  HIXL_CHECK_NOTNULL(func_handles.batch_put, "[HixlClient] batchPut stub is null");
  device_func_get_ = func_handles.batch_get;
  device_func_put_ = func_handles.batch_put;
  device_kernel_loaded_ = true;
  HIXL_LOGI("[HixlClient] UB Kernels loaded successfully. dev=%d handle=%p get=%p put=%p", device_id_,
            device_kernel_handle_, device_func_get_, device_func_put_);
  return SUCCESS;
}

void *HixlCSClient::GetDeviceKernelFunc(bool is_get) {
  return is_get ? device_func_get_ : device_func_put_;
}

Status HixlCSClient::ValidateDeviceInputs(uint32_t list_num, const HixlOneSideOpDesc *desc_list,
                                          void *&query_handle) const {
  (void)query_handle;
  query_handle = nullptr;
  HIXL_CHK_BOOL_RET_STATUS(list_num > 0U, PARAM_INVALID, "[HixlClient] list_num must be > 0");
  HIXL_CHECK_NOTNULL(desc_list);
  return SUCCESS;
}

Status HixlCSClient::PrepareDeviceRemoteFlagAndKernel(void *&remote_flag) const {
  HIXL_LOGI("[HixlClient] PrepareDeviceRemoteFlagAndKernel start");
  remote_flag = device_remote_flag_addr_;
  HIXL_CHECK_NOTNULL(remote_flag, "[HixlClient] remote_flag is nullptr");
  HIXL_LOGI("[HixlClient] PrepareDeviceRemoteFlagAndKernel end, remote_flag=%p", remote_flag);
  return SUCCESS;
}

Status HixlCSClient::AllocateHostFlag(void *&host_flag) const {
  host_flag = nullptr;
  HIXL_CHK_ACL_RET(aclrtMallocHost(&host_flag, sizeof(uint64_t)), "[HixlClient] aclrtMallocHost host_flag failed");
  *(static_cast<uint64_t *>(host_flag)) = kDeviceFlagInitValue;
  return SUCCESS;
}

Status HixlCSClient::LaunchDeviceChunkedKernels(bool is_get, DeviceCompleteHandle &handle, uint32_t list_num) {
  uint32_t num_chunks = (list_num + kMaxKernelBatchSize - 1U) / kMaxKernelBatchSize;
  for (uint32_t chunk_idx = 0U; chunk_idx < num_chunks; ++chunk_idx) {
    uint32_t chunk_offset = chunk_idx * kMaxKernelBatchSize;
    uint32_t chunk_list_num = std::min(kMaxKernelBatchSize, list_num - chunk_offset);
    bool is_last_chunk = (chunk_idx == num_chunks - 1U);
    HixlOneSideOpParam param{};
    HIXL_CHK_STATUS_RET(BuildDeviceChunkParam(handle, chunk_offset, chunk_list_num, is_last_chunk, param),
                        "BuildDeviceChunkParam failed for chunk %u/%u", chunk_idx, num_chunks);
    HIXL_CHK_STATUS_RET(LaunchDeviceKernel(is_get, handle, param, is_last_chunk),
                        "LaunchDeviceKernel failed for chunk %u/%u", chunk_idx, num_chunks);
  }
  return SUCCESS;
}

Status HixlCSClient::AllocateDeviceDescBuf(DeviceCompleteHandle &handle, uint32_t total_list_num,
                                            const HixlOneSideOpDesc *desc_list) {
  size_t desc_buf_size = total_list_num * sizeof(HixlOneSideOpDesc);
  HIXL_CHK_ACL_RET(aclrtMalloc(&handle.dev_op_desc_buf, desc_buf_size, ACL_MEM_MALLOC_HUGE_ONLY),
                   "[HixlClient] aclrtMalloc op_desc_buf failed");
  HIXL_CHK_ACL_RET(aclrtMemcpy(handle.dev_op_desc_buf, desc_buf_size, desc_list, desc_buf_size, ACL_MEMCPY_HOST_TO_DEVICE),
                   "[HixlClient] aclrtMemcpy op_desc_buf failed");
  return SUCCESS;
}

Status HixlCSClient::BuildDeviceChunkParam(DeviceCompleteHandle &handle, uint32_t chunk_offset,
                                            uint32_t chunk_list_num, bool is_last_chunk,
                                            HixlOneSideOpParam &param) {
  HIXL_CHK_BOOL_RET_STATUS(handle.shared_slot->slot_index < slot_notify_addrs_.size(), PARAM_INVALID,
                           "[HixlClient] slot_index %u out of range %zu",
                           handle.shared_slot->slot_index, slot_notify_addrs_.size());
  param.thread = handle.shared_slot->thread;
  param.channel = static_cast<uint64_t>(client_channel_handle_);
  param.list_num = chunk_list_num;
  auto *chunk_base = static_cast<uint8_t *>(handle.dev_op_desc_buf) + chunk_offset * sizeof(HixlOneSideOpDesc);
  param.op_desc_list_addr = reinterpret_cast<uint64_t>(chunk_base);
  if (is_last_chunk) {
    void *remote_flag = nullptr;
    HIXL_CHK_STATUS_RET(PrepareDeviceRemoteFlagAndKernel(remote_flag), "PrepareDeviceRemoteFlagAndKernel failed");
    param.remote_flag_addr = reinterpret_cast<uint64_t>(remote_flag);
    param.local_flag_addr = slot_notify_addrs_[handle.shared_slot->slot_index];
    param.flag_size = notify_len_;
    param.notify_id = handle.shared_slot->notify_id;
  } else {
    param.remote_flag_addr = 0;
    param.local_flag_addr = 0;
    param.flag_size = 0;
    param.notify_id = 0;
  }
  if (local_endpoint_->GetEndpoint().protocol == COMM_PROTOCOL_HCCS) {
    param.use_notify_record = 1;
  }
  HIXL_LOGI("[HixlClient] protocol=%u, use_notify_record=%u",
    local_endpoint_->GetEndpoint().protocol, param.use_notify_record);
  return SUCCESS;
}

std::unique_ptr<hixl::TemporaryRtContext> HixlCSClient::GetContextGuard() const {
  if (device_id_ >= 0) {
    auto *pool = TransferPool::GetInstance(device_id_);
    if (pool == nullptr) {
      return nullptr;
    }
    auto ctx = pool->GetContext();
    if (ctx != nullptr) {
      return MakeUnique<hixl::TemporaryRtContext>(ctx);
    }
  }
  return nullptr;  // 不切换 context
}

Status HixlCSClient::LaunchDeviceKernel(bool is_get, DeviceCompleteHandle &handle,
                                        const HixlOneSideOpParam &param,
                                        bool wait_notify) {
  const char *kernel_name = is_get ? kDeviceFuncGet : kDeviceFuncPut;
  HIXL_LOGI("[HixlClient] LaunchDeviceKernel start. kernel=%s wait_notify=%d", kernel_name, wait_notify);
  void *func = GetDeviceKernelFunc(is_get);
  HIXL_CHECK_NOTNULL(func, "[HixlClient] func is null for %s", kernel_name);
  constexpr uint32_t block_dim = 1U;

  aclrtFuncHandle funcHandle = func;
  aclrtArgsHandle argsHandle = nullptr;
  HIXL_CHK_ACL_RET(aclrtKernelArgsInit(funcHandle, &argsHandle), "[HixlClient] aclrtKernelArgsInit failed. kernel=%s",
                   kernel_name);
  aclrtParamHandle paraHandle;
  HIXL_CHK_ACL_RET(aclrtKernelArgsAppend(argsHandle, const_cast<HixlOneSideOpParam *>(&param),
                                         sizeof(HixlOneSideOpParam), &paraHandle),
                   "[HixlClient] aclrtKernelArgsAppend param failed, kernel = %s", kernel_name);
  HIXL_CHK_ACL_RET(aclrtKernelArgsFinalize(argsHandle), "[HixlClient] aclrtKernelArgsFinalize failed, kernel = %s",
                   kernel_name);

  aclrtLaunchKernelCfg cfg;
  aclrtLaunchKernelAttr attr;
  attr.id = ACL_RT_LAUNCH_KERNEL_ATTR_TIMEOUT;
  attr.value.timeout = kNotifyDefaultWaitTimeMs;
  cfg.numAttrs = 1;
  cfg.attrs = &attr;

  HIXL_CHECK_NOTNULL(handle.shared_slot.get(), "[HixlClient] LaunchDeviceKernel shared_slot is null");
  HIXL_CHK_ACL_RET(
      aclrtLaunchKernelWithConfig(funcHandle, block_dim, handle.shared_slot->stream, &cfg, argsHandle, nullptr),
      "[HixlClient] aclrtLaunchKernelWithConfig failed");
  if (wait_notify) {
    HIXL_CHK_ACL_RET(aclrtWaitAndResetNotify(handle.shared_slot->notify, handle.shared_slot->stream, kCustomTimeoutMs),
                     "[HixlClient] aclrtWaitAndResetNotify failed");
  }
  HIXL_LOGI("[HixlClient] LaunchDeviceKernel end. kernel=%s", kernel_name);
  return SUCCESS;
}

Status HixlCSClient::BatchTransferDeviceAsync(bool is_get, uint32_t list_num, const HixlOneSideOpDesc *desc_list,
                                              void **query_handle) {
  void *handle_ptr = nullptr;
  HIXL_CHK_STATUS_RET(ValidateDeviceInputs(list_num, desc_list, handle_ptr), "ValidateDeviceInputs failed");

  // Serialize the entire device transfer lifecycle to prevent concurrent threads from
  // sharing the same stream or triggering Abort on it.
  std::lock_guard<std::mutex> launch_lock(device_launch_mu_);
  std::shared_ptr<TransferPool::SlotHandle> slot;
  HIXL_CHK_STATUS_RET(AcquireSharedSlot(slot), "[HixlClient] AcquireSharedSlot failed");

  HIXL_CHECK_NOTNULL(slot->notify, "[HixlClient] slot->notify is null");

  void *host_flag = nullptr;
  HIXL_CHK_STATUS_RET(AllocateHostFlag(host_flag), "[HixlClient] AllocateHostFlag failed");
  HIXL_DISMISSABLE_GUARD(flag_guard, [&host_flag]() {
    if (host_flag != nullptr) {
      aclrtFreeHost(host_flag);
    }
  });

  auto *handle = new (std::nothrow) DeviceCompleteHandle();
  if (handle == nullptr) {
    HIXL_LOGE(FAILED, "[HixlClient] new DeviceCompleteHandle failed");
    ReleaseSharedSlotRef(slot);
    return FAILED;
  }
  HIXL_DISMISSABLE_GUARD(handle_guard, ([this, handle]() {
                           std::lock_guard<std::mutex> lock(mutex_);
                           (void)ReleaseDevCompleteHandle(handle);
                         }));
  handle->magic = kDeviceCompleteMagic;
  handle->reserved = 0U;
  handle->shared_slot = std::move(slot);
  handle->host_flag = host_flag;
  handle->dev_op_desc_buf = nullptr;
  HIXL_DISMISS_GUARD(flag_guard);

  HIXL_CHK_STATUS_RET(AllocateDeviceDescBuf(*handle, list_num, desc_list), "AllocateDeviceDescBuf failed");

  HIXL_LOGI("[HixlClient] BatchTransferDeviceAsync. is_get=%d list_num=%u slot=%u magic=%u",
            static_cast<int32_t>(is_get), list_num, handle->shared_slot->slot_index, handle->magic);

  {
    hixl::TemporaryRtContext ctx_guard(handle->shared_slot->ctx);
    HIXL_CHK_STATUS_RET(LaunchDeviceChunkedKernels(is_get, *handle, list_num), "LaunchDeviceChunkedKernels failed");
    HIXL_CHK_ACL_RET(aclrtMemcpyAsync(handle->host_flag, sizeof(uint64_t), handle->shared_slot->dev_const_one,
                                      sizeof(uint64_t), ACL_MEMCPY_DEVICE_TO_HOST, handle->shared_slot->stream),
                     "[HixlClient] aclrtMemcpyAsync (Flag D2H) failed");
  }

  *query_handle = static_cast<void *>(handle);
  HIXL_DISMISS_GUARD(handle_guard);
  HIXL_LOGI("[HixlClient] BatchTransfer submitted. is_get=%d list_num=%u slot=%u", static_cast<int32_t>(is_get),
            list_num, handle->shared_slot->slot_index);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_device_handles_.insert(handle);
  }
  return SUCCESS;
}

Status HixlCSClient::BatchTransferDeviceSync(bool is_get, uint32_t list_num, const HixlOneSideOpDesc *desc_list,
                                             uint32_t timeout_ms) {
  void *handle_ptr = nullptr;
  HIXL_CHK_STATUS_RET(ValidateDeviceInputs(list_num, desc_list, handle_ptr), "ValidateDeviceInputs failed");

  // Serialize the entire device transfer lifecycle to prevent concurrent threads from
  // sharing the same stream or triggering Abort on it.
  std::lock_guard<std::mutex> launch_lock(device_launch_mu_);
  std::shared_ptr<TransferPool::SlotHandle> slot;
  HIXL_CHK_STATUS_RET(AcquireSharedSlot(slot), "[HixlClient] AcquireSharedSlot failed");
  HIXL_CHECK_NOTNULL(slot->notify, "[HixlClient] slot->notify is null");

  auto *handle = new (std::nothrow) DeviceCompleteHandle();
  if (handle == nullptr) {
    HIXL_LOGE(FAILED, "[HixlClient] new DeviceCompleteHandle failed");
    ReleaseSharedSlotRef(slot);
    return FAILED;
  }
  HIXL_MAKE_GUARD(handle_guard, ([this, handle]() {
                    std::lock_guard<std::mutex> lock(mutex_);
                    (void)ReleaseDevCompleteHandle(handle);
                  }));
  handle->magic = kDeviceCompleteMagic;
  handle->reserved = 0U;
  handle->shared_slot = std::move(slot);
  handle->host_flag = nullptr;
  handle->dev_op_desc_buf = nullptr;

  HIXL_CHK_STATUS_RET(AllocateDeviceDescBuf(*handle, list_num, desc_list), "AllocateDeviceDescBuf failed");

  HIXL_LOGI("[HixlClient] BatchTransferDeviceSync. is_get=%d list_num=%u slot=%u", static_cast<int32_t>(is_get),
            list_num, handle->shared_slot->slot_index);

  {
    hixl::TemporaryRtContext ctx_guard(handle->shared_slot->ctx);
    HIXL_CHK_STATUS_RET(LaunchDeviceChunkedKernels(is_get, *handle, list_num), "LaunchDeviceChunkedKernels failed");
    const aclError sync_ret = aclrtSynchronizeStreamWithTimeout(handle->shared_slot->stream, timeout_ms);
    if (sync_ret != ACL_SUCCESS && handle->shared_slot != nullptr) {
      auto *pool = TransferPool::GetInstance(handle->shared_slot->device_id);
      if (pool != nullptr) {
        pool->Abort(*handle->shared_slot);
      }
    }
    HIXL_CHK_ACL_RET(sync_ret, "[HixlClient] aclrtSynchronizeStreamWithTimeout failed, kernel=%s, ret=0x%X",
                     is_get ? kDeviceFuncGet : kDeviceFuncPut, static_cast<uint32_t>(sync_ret));
  }

  HIXL_LOGI("[HixlClient] BatchTransferDeviceSync done. is_get=%d list_num=%u", static_cast<int32_t>(is_get), list_num);
  return SUCCESS;
}

Status HixlCSClient::BatchTransferHostSync(bool is_get, uint32_t list_num, const HixlOneSideOpDesc *desc_list,
                                           uint32_t timeout_ms) {
  void *raw_handle = nullptr;
  HIXL_CHK_STATUS_RET(BatchTransferHostAsync(is_get, list_num, desc_list, &raw_handle),
                      "[HixlClient] BatchTransferHostAsync failed");
  HIXL_CHECK_NOTNULL(raw_handle);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (true) {
    if (std::chrono::steady_clock::now() >= deadline) {
      std::lock_guard<std::mutex> lock(mutex_);
      (void)ReleaseCompleteHandle(static_cast<CompleteHandleInfo *>(raw_handle));
      HIXL_LOGE(TIMEOUT, "[HixlClient] BatchTransferHostSync timeout after %u ms", timeout_ms);
      return TIMEOUT;
    }
    HixlCompleteStatus st = HixlCompleteStatus::HIXL_COMPLETE_STATUS_WAITING;
    const Status cs = CheckStatus(raw_handle, &st);
    if (cs != SUCCESS) {
      std::lock_guard<std::mutex> lock(mutex_);
      (void)ReleaseCompleteHandle(static_cast<CompleteHandleInfo *>(raw_handle));
      return cs;
    }
    if (st == HixlCompleteStatus::HIXL_COMPLETE_STATUS_COMPLETED) {
      return SUCCESS;
    }
    std::this_thread::sleep_for(std::chrono::microseconds(10));
  }
}

Status HixlCSClient::BatchTransferSync(bool is_get, uint32_t list_num, const HixlOneSideOpDesc *desc_list,
                                       uint32_t timeout_ms) {
  auto ctx_guard = GetContextGuard();
  (void)ctx_guard;
  HIXL_CHK_STATUS_RET(ValidateAddress(list_num, desc_list), "[HixlClient] ValidateAddress failed.");
  HIXL_CHECK_NOTNULL(local_endpoint_);
  const EndpointDesc endpoint = local_endpoint_->GetEndpoint();
  if (IsDeviceEndpoint(endpoint)) {
    if (endpoint.protocol == COMM_PROTOCOL_UBOE) {
      std::vector<HixlOneSideOpDesc> mutable_descs(desc_list, desc_list + list_num);
      HIXL_CHK_STATUS_RET(ConvertUboeDescs(list_num, mutable_descs.data()),
                          "[HixlClient] convert uboe descs failed.");
      return BatchTransferDeviceSync(is_get, list_num, mutable_descs.data(), timeout_ms);
    }
    return BatchTransferDeviceSync(is_get, list_num, desc_list, timeout_ms);
  }
  if (endpoint.loc.locType == ENDPOINT_LOC_TYPE_HOST) {
    return BatchTransferHostSync(is_get, list_num, desc_list, timeout_ms);
  }
  HIXL_LOGE(PARAM_INVALID, "[HixlClient] Invalid endpoint location: %d", endpoint.loc.locType);
  return PARAM_INVALID;
}

Status HixlCSClient::ConvertUboeDescs(uint32_t list_num, HixlOneSideOpDesc *desc_list) {
  return mem_store_.BatchConvertHostAddr(list_num, desc_list);
}

Status HixlCSClient::BatchTransferAsync(bool is_get, uint32_t list_num, const HixlOneSideOpDesc *desc_list,
                                        void **query_handle) {
  auto ctx_guard = GetContextGuard();
  (void)ctx_guard;
  HIXL_CHK_STATUS_RET(ValidateAddress(list_num, desc_list), "[HixlClient] ValidateAddress failed.");
  HIXL_CHECK_NOTNULL(local_endpoint_);
  const EndpointDesc ep = local_endpoint_->GetEndpoint();
  if (IsDeviceEndpoint(ep)) {
    if (ep.protocol == COMM_PROTOCOL_UBOE) {
      std::vector<HixlOneSideOpDesc> mutable_descs(desc_list, desc_list + list_num);
      HIXL_CHK_STATUS_RET(ConvertUboeDescs(list_num, mutable_descs.data()),
                          "[HixlClient] convert uboe descs failed.");
      return BatchTransferDeviceAsync(is_get, list_num, mutable_descs.data(), query_handle);
    }
    return BatchTransferDeviceAsync(is_get, list_num, desc_list, query_handle);
  } else if (ep.loc.locType == ENDPOINT_LOC_TYPE_HOST) {
    return BatchTransferHostAsync(is_get, list_num, desc_list, query_handle);
  } else {
    HIXL_LOGE(PARAM_INVALID, "[HixlClient] Invalid endpoint location: %d", ep.loc.locType);
    return PARAM_INVALID;
  }
  return PARAM_INVALID;
}

Status HixlCSClient::CheckStatusHost(CompleteHandleInfo &query_handle, HixlCompleteStatus &status) {
  // 检验query_handle中的序号是否合规
  if (query_handle.flag_index < 0 || query_handle.flag_index >= static_cast<int32_t>(kFlagQueueSize)) {
    HIXL_LOGE(PARAM_INVALID,
              "The value of query_handle->flag_index is outside the valid verification range; please check the "
              "query_handle. query_handle->flag_index：%d",
              query_handle.flag_index);
    return PARAM_INVALID;
  }
  // 通过读取query_handle中地址的值，来判断任务的完成状态
  uint64_t *atomic_flag = query_handle.flag_address;
  HIXL_CHECK_NOTNULL(atomic_flag);
  // 查到flag变成1之后，就把其重置为0，之后告知用户读写任务已经完成。
  if (*atomic_flag == kFlagDoneValue) {
    status = HixlCompleteStatus::HIXL_COMPLETE_STATUS_COMPLETED;
    HIXL_LOGI("The current transmission task has been completed.");
    return ReleaseCompleteHandle(&query_handle);  // 释放内存并回收索引
  }
  status = HixlCompleteStatus::HIXL_COMPLETE_STATUS_WAITING;
  HIXL_LOGI("The current transmission task has not been completed.");
  return SUCCESS;
}

Status HixlCSClient::CheckStatusDevice(DeviceCompleteHandle &query_handle, HixlCompleteStatus &status) {
  if (query_handle.magic != kDeviceCompleteMagic) {
    HIXL_LOGE(PARAM_INVALID, "[HixlClient] CheckStatusUb bad magic=0x%X", query_handle.magic);
    return PARAM_INVALID;
  }
  HIXL_CHECK_NOTNULL(query_handle.shared_slot.get(), "[HixlClient] CheckStatusDevice shared_slot is null");
  HIXL_CHECK_NOTNULL(query_handle.host_flag, "[HixlClient] CheckStatusDevice host_flag is null");

  void *host_flag = query_handle.host_flag;
  volatile uint64_t *flag_ptr = static_cast<uint64_t *>(host_flag);
  const uint64_t flag_val = *flag_ptr;
  HIXL_LOGI("[HixlCSClient] CheckStatusDevice flag_val=%lu", flag_val);
  if (flag_val == kDeviceFlagDoneValue) {
    status = HixlCompleteStatus::HIXL_COMPLETE_STATUS_COMPLETED;

    HIXL_LOGI("[HixlClient] Batch completed. slot=%u", query_handle.shared_slot->slot_index);
    return ReleaseDevCompleteHandle(&query_handle);
  }

  status = HixlCompleteStatus::HIXL_COMPLETE_STATUS_WAITING;
  return SUCCESS;
}

// 通过已经建立好的channel，检查批量读写的状态。
Status HixlCSClient::CheckStatus(void *query_handle, HixlCompleteStatus *status) {
  auto ctx_guard = GetContextGuard();
  (void)ctx_guard;
  HIXL_CHECK_NOTNULL(query_handle);
  HIXL_CHECK_NOTNULL(status);

  uint32_t head = 0U;
  errno_t rc = memcpy_s(&head, sizeof(head), query_handle, sizeof(head));
  if (rc != EOK) {
    HIXL_LOGE(FAILED, "[HixlClient] CheckStatus memcpy_s failed, rc=%d", static_cast<int32_t>(rc));
    return FAILED;
  }

  if (head == kDeviceCompleteMagic) {
    std::lock_guard<std::mutex> lock(mutex_);
    DeviceCompleteHandle *device_handle = static_cast<DeviceCompleteHandle *>(query_handle);
    return CheckStatusDevice(*device_handle, *status);
  }

  if (head == kRoceCompleteMagic) {
    CompleteHandleInfo *legacy = static_cast<CompleteHandleInfo *>(query_handle);
    return CheckStatusHost(*legacy, *status);
  }

  HIXL_LOGE(PARAM_INVALID, "[HixlClient] CheckStatus bad magic=0x%X", head);
  return PARAM_INVALID;
}

// 注销client的endpoint的内存信息。
Status HixlCSClient::UnRegMem(MemHandle mem_handle) {
  auto ctx_guard = GetContextGuard();
  (void)ctx_guard;
  HIXL_CHECK_NOTNULL(mem_handle);
  HixlMemDesc desc;
  Status query_status = local_endpoint_->GetMemDesc(mem_handle, desc);
  if (query_status != SUCCESS) {
    return PARAM_INVALID;
  }
  Status result = local_endpoint_->DeregisterMem(mem_handle);
  if (result == SUCCESS) {
    Status ret = mem_store_.UnrecordMemory(false, desc.mem.addr);  // 删掉记录中client侧给endpoint分配的内存信息
    if (ret != SUCCESS) {
      HIXL_LOGE(FAILED, "[HixlClient] Client record memory failed. mem_addr = %p", desc.mem.addr);
      return FAILED;
    }
    return SUCCESS;
  }
  return PARAM_INVALID;
}

Status HixlCSClient::Connect(uint32_t timeout_ms) {
  auto ctx_guard = GetContextGuard();
  (void)ctx_guard;
  std::lock_guard<std::mutex> lock(mutex_);
  HIXL_CHECK_NOTNULL(local_endpoint_);
  HIXL_CHK_BOOL_RET_STATUS(remote_endpoint_.protocol != COMM_PROTOCOL_RESERVED, PARAM_INVALID,
                           "[HixlClient] Connect called but remote_endpoint is not set in Create");
  HIXL_EVENT("[HixlClient] Connect start. Target=%s:%u, timeout=%u ms", server_ip_.c_str(), server_port_, timeout_ms);
  HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Connect(server_ip_, server_port_, socket_, timeout_ms),
                      "[HixlClient] Connect socket to %s:%u failed", server_ip_.c_str(), server_port_);
  HIXL_LOGI("[HixlClient] Socket connected (TCP ready). fd=%d", socket_);
  HIXL_CHK_STATUS_RET(ExchangeEndpointAndCreateChannelLocked(timeout_ms),
                      "[HixlClient] Exchange endpoint info failed. fd=%d, Target=%s:%u", socket_, server_ip_.c_str(),
                      server_port_);
  HIXL_EVENT("[HixlClient] Connect success. target=%s:%u, fd=%d, remote_ep_handle=%" PRIu64 ", ch=%p",
             server_ip_.c_str(), server_port_, socket_, remote_endpoint_handle_, client_channel_handle_);
  return SUCCESS;
}

Status HixlCSClient::ExchangeEndpointAndCreateChannelLocked(uint32_t timeout_ms) {
  const EndpointDesc &src_ep = local_endpoint_->GetEndpoint();
  HIXL_LOGD(
      "[HixlClient] MatchEndpoint then CreateChannel. socket: %d, timeout: %u ms, "
      "Src[protocol:%u, type:%u, id:%u], Dst[protocol:%u, type:%u, id:%u]",
      socket_, timeout_ms, src_ep.protocol, src_ep.commAddr.type, src_ep.commAddr.id, remote_endpoint_.protocol,
      remote_endpoint_.commAddr.type, remote_endpoint_.commAddr.id);
  Status ret = ConnMsgHandler::SendMatchEndpointRequest(socket_, remote_endpoint_,
                                                        global_config_.ListenPort().value_or(0));
  HIXL_CHK_STATUS_RET(ret, "[HixlClient] SendMatchEndpointRequest failed. fd=%d", socket_);
  uint32_t remote_listen_port = 0;
  ret = ConnMsgHandler::RecvMatchEndpointResponse(socket_, remote_endpoint_handle_, remote_listen_port, timeout_ms);
  HIXL_CHK_STATUS_RET(ret, "[HixlClient] RecvMatchEndpointResponse failed. fd=%d, timeout=%u ms", socket_, timeout_ms);
  local_endpoint_->SetPort(remote_listen_port);
  CommMem *prefetch_mems = nullptr;
  char **prefetch_tags = nullptr;
  uint32_t prefetch_num = 0U;
  HIXL_LOGI("[HixlClient] Connect: prefetch remote mem before CreateChannel");
  ret = GetRemoteMemLocked(timeout_ms, &prefetch_mems, &prefetch_tags, &prefetch_num);
  HIXL_CHK_STATUS_RET(ret, "[HixlClient] Connect prefetch GetRemoteMem/Import failed. fd=%d, timeout=%u ms", socket_,
                      timeout_ms);
  const uint32_t channel_index = g_next_channel_index.fetch_add(1U, std::memory_order_relaxed);
  CreateChannelReq create_body{};
  create_body.src = src_ep;
  create_body.dst_ep_handle = remote_endpoint_handle_;
  create_body.tc = tc_;
  create_body.sl = sl_;
  create_body.channel_index = channel_index;
  ret = ConnMsgHandler::SendCreateChannelRequest(socket_, create_body);
  HIXL_CHK_STATUS_RET(ret, "[HixlClient] SendCreateChannelRequest failed. fd=%d", socket_);
  ChannelHandle channel_handle = 0UL;
  ChannelDesc channel_desc{};
  channel_desc.remote_endpoint = remote_endpoint_;
  channel_desc.tc = tc_;
  channel_desc.sl = sl_;
  channel_desc.channel_type = ChannelType::kClient;
  channel_desc.channel_index = channel_index;
  ret = local_endpoint_->CreateChannel(channel_desc, channel_handle);
  HIXL_CHK_STATUS_RET(ret, "[HixlClient] Endpoint CreateChannel failed. Dst[id:0x%x]", remote_endpoint_.commAddr.id);
  ret = ConnMsgHandler::RecvCreateChannelResponse(socket_, timeout_ms);
  HIXL_CHK_STATUS_RET(ret, "[HixlClient] RecvCreateChannelResponse failed. fd=%d, timeout=%u ms", socket_, timeout_ms);
  HIXL_LOGI("[HixlClient] Connect: remote endpoint handle = %" PRIu64, remote_endpoint_handle_);
  client_channel_handle_ = channel_handle;
  HIXL_LOGI("[HixlClient] Channel Ready. client_channel_handle_=%p", client_channel_handle_);
  return SUCCESS;
}

Status HixlCSClient::GetRemoteMemLocked(uint32_t timeout_ms, CommMem **remote_mem_list, char ***mem_tag_list,
                                        uint32_t *list_num) {
  HIXL_CHECK_NOTNULL(local_endpoint_);
  Status ret = MemMsgHandler::SendGetRemoteMemRequest(socket_, remote_endpoint_handle_, timeout_ms);
  HIXL_CHK_STATUS_RET(ret, "[HixlClient] SendGetRemoteMemRequest failed. fd=%d, remote_ep_handle=%" PRIu64, socket_,
                      remote_endpoint_handle_);
  std::vector<HixlMemDesc> mem_descs;
  ret = MemMsgHandler::RecvGetRemoteMemResponse(socket_, mem_descs, timeout_ms);
  HIXL_CHK_STATUS_RET(ret, "[HixlClient] RecvGetRemoteMemResponse failed. fd=%d, timeout=%u ms", socket_, timeout_ms);
  HIXL_LOGD("[HixlClient] Recv remote mem descs success. Count=%zu", mem_descs.size());
  ret = ImportRemoteMem(mem_descs, remote_mem_list, mem_tag_list, list_num);
  HIXL_CHK_STATUS_RET(ret, "[HixlClient] ImportRemoteMem failed. desc_count=%zu", mem_descs.size());

  // 提前初始化 remote flag，避免传输时引入耗时
  if (device_id_ >= 0) {
    std::lock_guard<std::mutex> lock(device_mu_);
    HIXL_CHK_STATUS_RET(EnsureDeviceRemoteFlagInitedLocked(), "[HixlClient] EnsureDeviceRemoteFlagInitedLocked failed");
  }
  return SUCCESS;
}

Status HixlCSClient::GetRemoteMem(CommMem **remote_mem_list, char ***mem_tag_list, uint32_t *list_num,
                                  uint32_t timeout_ms) {
  auto ctx_guard = GetContextGuard();
  (void)ctx_guard;
  HIXL_EVENT("[HixlClient] GetRemoteMem begin. fd=%d, remote_ep_handle=%" PRIu64 ", timeout=%u ms", socket_,
             remote_endpoint_handle_, timeout_ms);
  HIXL_CHECK_NOTNULL(remote_mem_list);
  HIXL_CHECK_NOTNULL(mem_tag_list);
  HIXL_CHECK_NOTNULL(list_num);
  *remote_mem_list = nullptr;
  *mem_tag_list = nullptr;
  std::lock_guard<std::mutex> lock(mutex_);
  HIXL_CHECK_NOTNULL(local_endpoint_);
  Status ret = GetRemoteMemLocked(timeout_ms, remote_mem_list, mem_tag_list, list_num);
  HIXL_CHK_STATUS_RET(ret, "[HixlClient] GetRemoteMemLocked failed");
  HIXL_EVENT("[HixlClient] GetRemoteMem success. fd=%d, remote_ep_handle=%" PRIu64 ", imported=%u", socket_,
             remote_endpoint_handle_, *list_num);
  return SUCCESS;
}

void HixlCSClient::FillOutputParams(ImportCtx &ctx, CommMem **remote_mem_list, char ***mem_tag_list,
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

Status HixlCSClient::ImportRemoteMem(std::vector<HixlMemDesc> &desc_list, CommMem **remote_mem_list,
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
  HIXL_CHECK_NOTNULL(local_endpoint_);
  EndpointHandle ep_handle = local_endpoint_->GetHandle();
  HIXL_CHECK_NOTNULL(ep_handle, "[HixlClient] ImportRemoteMem: endpoint handle is null");
  ImportCtx ctx;
  ctx.ep = local_endpoint_.get();
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

Status HixlCSClient::ClearRemoteMemInfo() {
  EndpointHandle ep_handle = (local_endpoint_ != nullptr) ? local_endpoint_->GetHandle() : nullptr;
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
  {
    std::lock_guard<std::mutex> lk(device_mu_);
    device_remote_flag_inited_ = false;
    device_remote_flag_addr_ = nullptr;
    device_remote_flag_size_ = 0ULL;
  }
  return SUCCESS;
}

void HixlCSClient::ReleaseLegacyHandlesLocked() {
  std::lock_guard<std::mutex> lk(indices_mutex_);
  uint32_t live_cnt = 0U;
  for (size_t i = 0U; i < kFlagQueueSize; ++i) {
    if (live_handles_[i] != nullptr) {
      live_cnt += 1U;
    }
  }
  if (live_cnt > 0U) {
    HIXL_LOGW("[HixlClient] Destroy: %u legacy complete_handle still live. Force releasing them.", live_cnt);
    for (size_t i = 0U; i < kFlagQueueSize; ++i) {
      if (live_handles_[i] != nullptr) {
        delete live_handles_[i];
        live_handles_[i] = nullptr;
      }
    }
    for (size_t i = 0U; i < kFlagQueueSize; ++i) {
      available_indices_[i] = static_cast<int32_t>(i);
    }
    top_index_ = kFlagQueueSize;
  }
}

void HixlCSClient::AbortAllPendingDeviceHandlesLocked() {
  if (pending_device_handles_.empty()) {
    return;
  }
  std::vector<DeviceCompleteHandle *> pending(pending_device_handles_.begin(), pending_device_handles_.end());
  pending_device_handles_.clear();
  for (DeviceCompleteHandle *h : pending) {
    if (h == nullptr) {
      continue;
    }
    if (h->shared_slot != nullptr) {
      auto *pool = TransferPool::GetInstance(h->shared_slot->device_id);
      if (pool != nullptr) {
        pool->Abort(*h->shared_slot);
      }
    }
    (void)ReleaseDevCompleteHandle(h);
  }
}

void HixlCSClient::ReleaseDeviceResourcesLocked() {
  for (size_t i = 0U; i < notify_mem_handles_.size(); ++i) {
    if (notify_mem_handles_[i] != nullptr) {
      if (local_endpoint_ != nullptr) {
        local_endpoint_->DeregisterMem(notify_mem_handles_[i]);
      }
      notify_mem_handles_[i] = nullptr;
    }
  }
  notify_mem_handles_.clear();
  // Cleanup active slot before finalizing TransferPool
  CleanupActiveSlot();
  if (device_kernel_loaded_) {
    if (device_kernel_handle_ != nullptr) {
      aclrtBinaryUnLoad(device_kernel_handle_);
    }
    device_kernel_handle_ = nullptr;
    device_func_get_ = nullptr;
    device_func_put_ = nullptr;
    device_kernel_loaded_ = false;
  }
}

Status HixlCSClient::Destroy() {
  Status first_error = SUCCESS;
  {
    auto ctx_guard = GetContextGuard();
    (void)ctx_guard;
    HIXL_EVENT("[HixlClient] Destroy start. fd=%d, imported_bufs=%zu, recorded_addrs=%zu", socket_,
               imported_remote_bufs_.size(), recorded_remote_addrs_.size());
    std::lock_guard<std::mutex> lock(mutex_);
    ReleaseLegacyHandlesLocked();
    AbortAllPendingDeviceHandlesLocked();
    ReleaseDeviceResourcesLocked();
    Status ret = ClearRemoteMemInfo();
    if (ret != SUCCESS) {
      HIXL_LOGW("[HixlClient] ClearRemoteMemInfo failed. fd=%d, ret=%u", socket_, static_cast<uint32_t>(ret));
      first_error = (first_error == SUCCESS) ? ret : first_error;
    }
    if (socket_ != -1) {
      HIXL_LOGI("[HixlClient] Closing socket. fd=%d", socket_);
      close(socket_);
      socket_ = -1;
    }
    if (local_endpoint_ != nullptr) {
      ret = local_endpoint_->Finalize();
      if (ret != SUCCESS) {
        HIXL_LOGW("[HixlClient] Finalize endpoint failed in Destroy. ep_handle=%p, ret=%u",
                  local_endpoint_->GetHandle(), static_cast<uint32_t>(ret));
        first_error = (first_error == SUCCESS) ? ret : first_error;
      }
      local_endpoint_.reset();
    }
  }
  // ctx_guard 已析构，TransferPool 放到最后销毁
  if (device_id_ >= 0) {
    auto *pool = TransferPool::GetInstance(device_id_);
    if (pool != nullptr) {
      pool->Finalize();
    }
    device_id_ = -1;
  }
  HIXL_EVENT("[HixlClient] Destroy done. first_error=%u", static_cast<uint32_t>(first_error));
  return first_error;
}
}  // namespace hixl
