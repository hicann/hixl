/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "engine/direct_client_handler.h"
#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/hixl_utils.h"
#include "engine/endpoint_generator.h"

namespace hixl {

DirectClientHandler::DirectClientHandler(HixlClientHandle handle) : handle_(handle) {}

Status DirectClientHandler::Create(const HandlerCreateArgs &args, std::unique_ptr<DirectClientHandler> &out) {
  const auto &pair = args.matched_pairs[0];
  int32_t dev_logic_id = 0;
  int32_t dev_phy_id = 0;
  HIXL_CHK_ACL_RET(aclrtGetDevice(&dev_logic_id));
  HIXL_CHK_ACL_RET(aclrtGetPhyDevIdByLogicDevId(dev_logic_id, &dev_phy_id));
  EndpointDesc local_endpoint{};
  EndpointDesc remote_endpoint{};
  HIXL_CHK_STATUS_RET(EndpointGenerator::ConvertToEndpointDesc(pair.local, local_endpoint, static_cast<uint32_t>(dev_phy_id)));
  HIXL_CHK_STATUS_RET(EndpointGenerator::ConvertToEndpointDesc(pair.remote, remote_endpoint));
  HixlClientDesc desc{};
  desc.server_ip = args.server_ip.c_str();
  desc.server_port = args.server_port;
  desc.local_endpoint = &local_endpoint;
  desc.remote_endpoint = &remote_endpoint;
  desc.tc = args.rdma_tc;
  desc.sl = args.rdma_sl;
  HixlClientHandle handle = nullptr;
  const HixlClientConfig config{};
  HIXL_CHK_STATUS_RET(HixlCSClientCreate(&desc, &config, &handle), "HixlCSClientCreate failed for type %s",
                      CommTypeToString(pair.type));
  out = MakeUnique<DirectClientHandler>(handle);
  HIXL_CHECK_NOTNULL(out, "DirectClientHandler create failed");
  return SUCCESS;
}

Status DirectClientHandler::Connect(uint32_t timeout_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  return static_cast<Status>(HixlCSClientConnect(handle_, timeout_ms));
}

Status DirectClientHandler::RegisterMem(const MemInfo &mem_info) {
  CommMem hccl_mem{};
  hccl_mem.type = (mem_info.type == MemType::MEM_DEVICE) ? COMM_MEM_TYPE_DEVICE : COMM_MEM_TYPE_HOST;
  hccl_mem.addr = reinterpret_cast<void *>(mem_info.mem.addr);
  hccl_mem.size = mem_info.mem.len;

  std::lock_guard<std::mutex> lock(mutex_);
  MemHandle mem_handle = nullptr;
  HIXL_CHK_STATUS_RET(HixlCSClientRegMem(handle_, nullptr, &hccl_mem, &mem_handle),
                      "DirectClientHandler register memory failed, addr: 0x%lx", mem_info.mem.addr);
  mem_handles_.push_back(mem_handle);
  return SUCCESS;
}

Status DirectClientHandler::TransferAsync(const std::vector<TransferOpDesc> &op_descs, TransferOp operation,
                                           TransferReq &req) {
  std::lock_guard<std::mutex> lock(mutex_);
  uint32_t list_num = static_cast<uint32_t>(op_descs.size());
  std::vector<HixlOneSideOpDesc> hixl_descs(list_num);
  for (size_t i = 0; i < list_num; i++) {
    hixl_descs[i].remote_buf = reinterpret_cast<void *>(op_descs[i].remote_addr);
    hixl_descs[i].local_buf = reinterpret_cast<void *>(op_descs[i].local_addr);
    hixl_descs[i].len = op_descs[i].len;
  }
  CompleteHandle complete_handle = nullptr;
  if (operation == WRITE) {
    HIXL_CHK_STATUS_RET(HixlCSClientBatchPutAsync(handle_, list_num, hixl_descs.data(), &complete_handle));
  } else {
    HIXL_CHK_STATUS_RET(HixlCSClientBatchGetAsync(handle_, list_num, hixl_descs.data(), &complete_handle));
  }
  req = static_cast<TransferReq>(complete_handle);
  std::lock_guard<std::mutex> ch_lock(complete_handles_mutex_);
  complete_handles_[req] = complete_handle;
  return SUCCESS;
}

Status DirectClientHandler::TransferSync(const std::vector<TransferOpDesc> &op_descs, TransferOp operation,
                                         uint32_t timeout_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  uint32_t list_num = static_cast<uint32_t>(op_descs.size());
  std::vector<HixlOneSideOpDesc> hixl_descs(list_num);
  for (size_t i = 0; i < list_num; i++) {
    hixl_descs[i].remote_buf = reinterpret_cast<void *>(op_descs[i].remote_addr);
    hixl_descs[i].local_buf = reinterpret_cast<void *>(op_descs[i].local_addr);
    hixl_descs[i].len = op_descs[i].len;
  }
  if (operation == WRITE) {
    return static_cast<Status>(HixlCSClientBatchPutSync(handle_, list_num, hixl_descs.data(), timeout_ms));
  }
  return static_cast<Status>(HixlCSClientBatchGetSync(handle_, list_num, hixl_descs.data(), timeout_ms));
}

Status DirectClientHandler::GetTransferStatus(const TransferReq &req, TransferStatus &status) {
  std::scoped_lock lock(mutex_, complete_handles_mutex_);
  if (complete_handles_.empty()) {
    HIXL_LOGE(FAILED, "DirectClientHandler GetTransferStatus failed, no transfer tasks in progress");
    status = TransferStatus::FAILED;
    return FAILED;
  }
  auto it = complete_handles_.find(req);
  if (it == complete_handles_.end()) {
    HIXL_LOGE(PARAM_INVALID, "DirectClientHandler GetTransferStatus failed, invalid req");
    status = TransferStatus::FAILED;
    return PARAM_INVALID;
  }
  HixlCompleteStatus cs = HIXL_COMPLETE_STATUS_WAITING;
  Status ret = static_cast<Status>(HixlCSClientQueryCompleteStatus(handle_, it->second, &cs));
  if (ret != SUCCESS) {
    status = TransferStatus::FAILED;
    complete_handles_.erase(req);
    return ret;
  }
  if (cs == HIXL_COMPLETE_STATUS_WAITING) {
    status = TransferStatus::WAITING;
  } else {
    status = TransferStatus::COMPLETED;
    complete_handles_.erase(req);
  }
  return SUCCESS;
}

Status DirectClientHandler::Finalize() {
  {
    std::lock_guard<std::mutex> lock(complete_handles_mutex_);
    complete_handles_.clear();
  }
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto &mh : mem_handles_) {
    if (mh != nullptr) {
      HixlCSClientUnregMem(handle_, mh);
    }
  }
  mem_handles_.clear();
  if (handle_ != nullptr) {
    HixlCSClientDestroy(handle_);
    handle_ = nullptr;
  }
  return SUCCESS;
}

}  // namespace hixl
