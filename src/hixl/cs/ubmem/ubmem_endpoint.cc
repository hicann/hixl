/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "cs/ubmem/ubmem_endpoint.h"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/hixl_utils.h"
#include "cs/ubmem/ubmem_virtual_memory_manager.h"
#include "nlohmann/json.hpp"
#include "securec.h"

namespace hixl {
namespace {
// export_desc wire codec: one JSON object per ShareHandleInfo, the fabric share handle as a byte array.
constexpr size_t kShareHandleDataSize = sizeof(aclrtMemFabricHandle{}.data);
constexpr int32_t kShareHandleArrayCheckErrCode = 401;

MemType ToUbMemType(CommMemType type) {
  return type == COMM_MEM_TYPE_HOST ? MEM_HOST : MEM_DEVICE;
}

// Locates the imported segment covering old_addr and reports the mapped address plus the length left in that
// segment. Entries at or below old_addr are probed from the closest peer address downwards.
bool FindMappedAddrPrefix(uintptr_t old_addr, const UbMemRemoteIndex &index, uintptr_t &new_addr,
                          size_t &available_len) {
  auto it = index.upper_bound(old_addr);
  while (it != index.begin()) {
    --it;
    const auto &info = it->second;
    const uintptr_t offset = old_addr - it->first;
    if (offset >= info.len || info.va_addr > std::numeric_limits<uintptr_t>::max() - offset) {
      continue;
    }
    new_addr = info.va_addr + offset;
    available_len = info.len - offset;
    return true;
  }
  return false;
}

nlohmann::json ShareHandleToArray(const aclrtMemFabricHandle &share_handle) {
  auto share_array = nlohmann::json::array();
  for (size_t i = 0; i < kShareHandleDataSize; ++i) {
    share_array.push_back(share_handle.data[i]);
  }
  return share_array;
}

void to_json(nlohmann::json &j, const ShareHandleInfo &info) {
  j = nlohmann::json{
      {"va_addr", info.va_addr}, {"len", info.len}, {"share_handle", ShareHandleToArray(info.share_handle)}};
}

void from_json(const nlohmann::json &j, ShareHandleInfo &info) {
  j.at("va_addr").get_to(info.va_addr);
  j.at("len").get_to(info.len);
  const auto &share_array = j.at("share_handle");
  if (!share_array.is_array() || share_array.size() != kShareHandleDataSize) {
    throw nlohmann::json::out_of_range::create(kShareHandleArrayCheckErrCode,
                                               "share_handle size must be " + std::to_string(kShareHandleDataSize), &j);
  }
  for (size_t i = 0; i < kShareHandleDataSize; ++i) {
    info.share_handle.data[i] = share_array.at(i).get<uint8_t>();
  }
}
}  // namespace

HcclResult ToHccl(Status status) {
  if (status == SUCCESS) {
    return HCCL_SUCCESS;
  }
  if (status == PARAM_INVALID) {
    return HCCL_E_PARA;
  }
  return HCCL_E_INTERNAL;
}

Status UbMemEndpoint::EncodeShareHandles(const std::vector<ShareHandleInfo> &handles, void *&export_desc,
                                         uint32_t &export_len) {
  export_desc = nullptr;
  export_len = 0U;
  nlohmann::json items = nlohmann::json::array();
  try {
    for (const auto &handle : handles) {
      nlohmann::json item;
      to_json(item, handle);
      item["mem_type"] = handle.mem_type;
      items.push_back(std::move(item));
    }
    const std::string payload = items.dump();
    HIXL_CHK_BOOL_RET_STATUS(payload.size() <= std::numeric_limits<uint32_t>::max(), PARAM_INVALID,
                             "[UbMemEndpoint] export_desc size %zu exceeds uint32 max", payload.size());
    void *buf = malloc(payload.size());
    HIXL_CHK_BOOL_RET_STATUS(buf != nullptr, FAILED, "[UbMemEndpoint] Call api:malloc failed, size:%zu bytes",
                             payload.size());
    const errno_t memcpy_ret = memcpy_s(buf, payload.size(), payload.data(), payload.size());
    if (memcpy_ret != EOK) {
      free(buf);
      HIXL_LOGE(FAILED, "[UbMemEndpoint] Call api:memcpy_s failed, ret:%d, size:%zu bytes", memcpy_ret, payload.size());
      return FAILED;
    }
    export_desc = buf;
    export_len = static_cast<uint32_t>(payload.size());
    return SUCCESS;
  } catch (const nlohmann::json::exception &e) {
    HIXL_LOGE(PARAM_INVALID, "[UbMemEndpoint] Failed to encode share handles, exception:%s", e.what());
    return PARAM_INVALID;
  }
}

Status UbMemEndpoint::DecodeShareHandles(const void *export_desc, uint32_t export_len,
                                         std::vector<ShareHandleInfo> &handles) {
  HIXL_CHECK_NOTNULL(export_desc);
  HIXL_CHK_BOOL_RET_STATUS(export_len > 0U, PARAM_INVALID, "[UbMemEndpoint] empty export_desc");
  handles.clear();
  try {
    const std::string payload(static_cast<const char *>(export_desc), export_len);
    const nlohmann::json items = nlohmann::json::parse(payload);
    HIXL_CHK_BOOL_RET_STATUS(items.is_array(), PARAM_INVALID, "[UbMemEndpoint] export_desc is not a JSON array");
    for (const auto &item : items) {
      ShareHandleInfo info{};
      from_json(item, info);
      if (item.contains("mem_type")) {
        info.mem_type = item.at("mem_type").get<MemType>();
      }
      handles.emplace_back(info);
    }
  } catch (const nlohmann::json::exception &e) {
    HIXL_LOGE(PARAM_INVALID, "[UbMemEndpoint] Failed to decode share handles, exception:%s", e.what());
    return PARAM_INVALID;
  }
  HIXL_CHK_BOOL_RET_STATUS(!handles.empty(), PARAM_INVALID, "[UbMemEndpoint] decoded share handle list is empty");
  return SUCCESS;
}

void UbMemEndpoint::FreeExportDesc(void *&export_desc) {
  if (export_desc != nullptr) {
    free(export_desc);
    export_desc = nullptr;
  }
}

Status UbMemEndpoint::TranslateRemoteOpDescs(const UbMemRemoteIndex &index, uint32_t list_num,
                                             const HixlOneSideOpDesc *src, std::vector<HixlOneSideOpDesc> &dst) {
  HIXL_CHECK_NOTNULL(src);
  dst.clear();
  for (uint32_t i = 0U; i < list_num; ++i) {
    const auto &op = src[i];
    HIXL_CHK_BOOL_RET_STATUS(op.len > 0U, PARAM_INVALID, "[UbMemEndpoint] transfer size must be non-zero");
    size_t offset = 0U;
    const uintptr_t old_remote = reinterpret_cast<uintptr_t>(op.remote_buf);
    const uintptr_t old_local = reinterpret_cast<uintptr_t>(op.local_buf);
    HIXL_CHK_BOOL_RET_STATUS(
        old_remote <= std::numeric_limits<uintptr_t>::max() - op.len &&
            old_local <= std::numeric_limits<uintptr_t>::max() - op.len,
        PARAM_INVALID, "[UbMemEndpoint] transfer address range overflows, local_addr:%lu, remote_addr:%lu, len:%zu",
        static_cast<unsigned long>(old_local), static_cast<unsigned long>(old_remote), op.len);
    while (offset < static_cast<size_t>(op.len)) {
      uintptr_t new_remote = 0;
      size_t available_len = 0;
      HIXL_CHK_BOOL_RET_STATUS(FindMappedAddrPrefix(old_remote + offset, index, new_remote, available_len),
                               PARAM_INVALID, "[UbMemEndpoint] remote addr:%lu is neither imported nor locally bound",
                               static_cast<unsigned long>(old_remote + offset));
      const size_t chunk_len = std::min(static_cast<size_t>(op.len) - offset, available_len);
      HixlOneSideOpDesc out = op;
      out.remote_buf = reinterpret_cast<void *>(new_remote);
      out.local_buf = reinterpret_cast<void *>(old_local + offset);
      out.len = chunk_len;
      dst.emplace_back(out);
      offset += chunk_len;
    }
  }
  return SUCCESS;
}

Status UbMemEndpoint::InitializeProcessWideVaPool() {
  auto &vmm = VirtualMemoryManager::GetInstance();
  const UbMemoryConfig &ub_memory = global_config_.UbMemory();
  if (ub_memory.max_capacity.has_value()) {
    HIXL_CHK_STATUS_RET(vmm.SetVirtualMemoryCapacity(*ub_memory.max_capacity),
                        "[UbMemEndpoint] Failed to set virtual memory capacity, capacity:%zu TB.",
                        *ub_memory.max_capacity);
  }
  if (ub_memory.start_address.has_value()) {
    HIXL_CHK_STATUS_RET(vmm.SetGlobalStartAddress(*ub_memory.start_address),
                        "[UbMemEndpoint] Failed to set virtual memory start address, start_addr:%zu TB.",
                        *ub_memory.start_address);
  }
  return vmm.Initialize();
}

HcclResult UbMemEndpoint::EndpointCreate(EndpointHandle &handle) {
  const Status vmm_ret = InitializeProcessWideVaPool();
  if (vmm_ret != SUCCESS) {
    HIXL_LOGE(vmm_ret, "[UbMemEndpoint] Failed to initialize virtual memory manager.");
    return ToHccl(vmm_ret);
  }
  // The endpoint is its own transport state, so the handle just names this object. It stays valid for
  // as long as the Endpoint does and must not be freed by EndpointDestroy.
  handle = this;
  HIXL_LOGI("UbMemEndpointCreate success, handle:%p", handle);
  return HCCL_SUCCESS;
}

UbMemEndpoint::~UbMemEndpoint() {
  for (auto &item : exports_) {
    FreeExportDesc(item.second.desc);
  }
  exports_.clear();
}

HcclResult UbMemEndpoint::EndpointDestroy() {
  for (auto &item : exports_) {
    FreeExportDesc(item.second.desc);
  }
  exports_.clear();
  memory_.Finalize();
  HIXL_LOGI("UbMemEndpointDestroy success, handle:%p", GetHandle());
  return HCCL_SUCCESS;
}

Status UbMemEndpoint::EncodeRegExport(MemHandle mem_handle, ExportEntry &entry) {
  if (entry.desc != nullptr) {
    return SUCCESS;
  }
  std::vector<ShareHandleInfo> handles;
  HIXL_CHK_STATUS_RET(memory_.GetShareHandles(mem_handle, handles),
                      "[UbMemEndpoint] GetShareHandles failed, mem_handle=%p", mem_handle);
  return EncodeShareHandles(handles, entry.desc, entry.len);
}

HcclResult UbMemEndpoint::MemReg(const char *mem_tag, const CommMem *mem, HcommMemHandle *mem_handle) {
  (void)mem_tag;
  if (mem == nullptr || mem_handle == nullptr) {
    return HCCL_E_PARA;
  }
  MemDesc desc{};
  desc.addr = reinterpret_cast<uintptr_t>(mem->addr);
  desc.len = mem->size;
  MemHandle out = nullptr;
  const Status reg_ret = memory_.RegisterMem(desc, ToUbMemType(mem->type), out);
  if (reg_ret != SUCCESS) {
    return ToHccl(reg_ret);
  }
  const auto existing_it = exports_.find(out);
  if (existing_it != exports_.end()) {
    *mem_handle = out;
    return HCCL_SUCCESS;
  }
  ExportEntry entry{};
  const Status enc_ret = EncodeRegExport(out, entry);
  if (enc_ret != SUCCESS) {
    (void)memory_.DeregisterMem(out);
    return ToHccl(enc_ret);
  }
  exports_[out] = entry;
  *mem_handle = out;
  return HCCL_SUCCESS;
}

HcclResult UbMemEndpoint::MemUnreg(HcommMemHandle mem_handle) {
  auto it = exports_.find(mem_handle);
  if (it != exports_.end()) {
    FreeExportDesc(it->second.desc);
    exports_.erase(it);
  }
  return ToHccl(memory_.DeregisterMem(mem_handle));
}

HcclResult UbMemEndpoint::MemExport(HcommMemHandle mem_handle, void **mem_desc, uint32_t *mem_desc_len) {
  if (mem_desc == nullptr || mem_desc_len == nullptr) {
    return HCCL_E_PARA;
  }
  auto it = exports_.find(mem_handle);
  if (it == exports_.end()) {
    return HCCL_E_PARA;
  }
  *mem_desc = it->second.desc;
  *mem_desc_len = it->second.len;
  return HCCL_SUCCESS;
}

HcclResult UbMemEndpoint::MemImport(const void *mem_desc, uint32_t desc_len, CommMem *out_mem) {
  if (mem_desc == nullptr || out_mem == nullptr) {
    return HCCL_E_PARA;
  }
  std::vector<ShareHandleInfo> handles;
  const Status dec_ret = DecodeShareHandles(mem_desc, desc_len, handles);
  if (dec_ret != SUCCESS) {
    return ToHccl(dec_ret);
  }
  const Status import_ret = memory_.Import(handles);
  if (import_ret != SUCCESS) {
    return ToHccl(import_ret);
  }
  out_mem->type = handles[0].mem_type == MEM_HOST ? COMM_MEM_TYPE_HOST : COMM_MEM_TYPE_DEVICE;
  out_mem->addr = reinterpret_cast<void *>(handles[0].va_addr);
  out_mem->size = handles[0].len;
  return HCCL_SUCCESS;
}

HcclResult UbMemEndpoint::MemUnimport(const void *mem_desc, uint32_t desc_len) {
  if (mem_desc == nullptr || desc_len == 0U) {
    return HCCL_E_PARA;
  }
  std::vector<ShareHandleInfo> handles;
  const Status dec_ret = DecodeShareHandles(mem_desc, desc_len, handles);
  if (dec_ret != SUCCESS) {
    return ToHccl(dec_ret);
  }
  // Matching happens inside Unimport on the share handle itself, so there is no need to re-run the
  // ownership split: a handle that was identity-bound at import time is dropped without any release,
  // even if the local registration it matched has been deregistered since.
  return ToHccl(memory_.Unimport(handles));
}

Status UbMemEndpoint::TranslateRemoteDescs(uint32_t list_num, const HixlOneSideOpDesc *src,
                                           std::vector<HixlOneSideOpDesc> &dst) {
  auto index = memory_.GetTranslationIndex();
  HIXL_CHECK_NOTNULL(index);
  return TranslateRemoteOpDescs(*index, list_num, src, dst);
}

}  // namespace hixl
