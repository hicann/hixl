/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hixl_entity.h"
#include "common/llm_scope_guard.h"
#include "common/ctrl_msg.h"
#include "common/ctrl_msg_plugin.h"

namespace llm {
namespace {
constexpr int32_t kDisconnectTimeoutMs = 30000; // default 30s
}

ge::Status HixlEntity::Initialize(int32_t timeout_ms) {
  LLMLOGI("Hixl entity init begin, timeout:%d ms", timeout_ms);
  auto &cache_access_table = GetCacheAccessTable();
  LLM_CHK_STATUS_RET(cache_access_table.Initialize(true), "Failed to init cache_access_table");
  remote_engine_ = remote_ip_ + ":" + std::to_string(remote_port_);
  LLM_CHK_BOOL_RET_STATUS(engine_->Connect(remote_engine_.c_str(), timeout_ms) == hixl::SUCCESS, ge::FAILED,
                          "Failed to connect to remote engine, remote_engine:%s, timeout:%d ms.",
                          remote_engine_.c_str(), timeout_ms);

  int32_t client_fd = -1;
  LLM_CHK_BOOL_RET_STATUS(hixl::CtrlMsgPlugin::Connect(remote_ip_, remote_port_,
                                                       client_fd, timeout_ms) == hixl::SUCCESS,
                          ge::FAILED,
                          "Connect server %s failed", remote_engine_.c_str());
  ScopeGuard socket_guard([client_fd]() {
    close(client_fd);
  });
  hixl::CtrlMsgHeader header{};
  header.magic = hixl::kMagicNumber;
  header.body_size = static_cast<uint64_t>(sizeof(hixl::CtrlMsgType));
  auto msg_type = hixl::CtrlMsgType::kGetCacheTableReq;
  LLM_CHK_BOOL_RET_STATUS(
      hixl::CtrlMsgPlugin::Send(client_fd, &header, static_cast<uint64_t>(sizeof(header))) == hixl::SUCCESS, ge::FAILED,
      "Failed to send cache table header");
  LLM_CHK_BOOL_RET_STATUS(
      hixl::CtrlMsgPlugin::Send(client_fd, &msg_type, static_cast<uint64_t>(sizeof(msg_type))) == hixl::SUCCESS, ge::FAILED,
      "Failed to send cache table type");
  CacheTableInfo info{};
  LLM_CHK_STATUS_RET(RecvCacheTableResp(client_fd, info, timeout_ms), "Failed to recv cache table info");
  auto &remote_mems = GetRemoteMems();
  HcclMem remote_mem{};
  remote_mem.type = HcclMemType::HCCL_MEM_TYPE_DEVICE;
  remote_mem.addr = ValueToPtr(info.cache_table_addr);
  remote_mem.size = info.cache_table_size;
  remote_mems.emplace_back(remote_mem);
  return ge::SUCCESS;
}

ge::Status HixlEntity::RecvCacheTableResp(int32_t fd, CacheTableInfo &cache_table_info, int32_t timeout_ms) {
  hixl::CtrlMsgHeader header{};
  LLM_CHK_BOOL_RET_STATUS(
      hixl::CtrlMsgPlugin::Recv(fd, &header, static_cast<uint32_t>(sizeof(header)), timeout_ms) == hixl::SUCCESS,
      ge::FAILED, "Failed to recv cache table header, timeout:%d", timeout_ms);
  LLM_CHK_BOOL_RET_STATUS(header.magic == hixl::kMagicNumber, ge::LLM_PARAM_INVALID,
                          "Invalid magic:0x%X for cache table resp", header.magic);
  LLM_CHK_BOOL_RET_STATUS(
      header.body_size == sizeof(hixl::CtrlMsgType) + sizeof(CacheTableInfo), ge::LLM_PARAM_INVALID,
      "Invalid body_size in RecvCacheTableInfoResp, body_size=%" PRIu64 ", must be equal %zu",
      header.body_size, sizeof(hixl::CtrlMsgType) + sizeof(CacheTableInfo));

  hixl::CtrlMsgType msg_type{};
  LLM_CHK_BOOL_RET_STATUS(
      hixl::CtrlMsgPlugin::Recv(fd, &msg_type, static_cast<uint32_t>(sizeof(msg_type)), timeout_ms) == hixl::SUCCESS,
      ge::FAILED, "Failed to recv cache table msg type");
  LLM_CHK_BOOL_RET_STATUS(msg_type == hixl::CtrlMsgType::kGetCacheTableResp, ge::LLM_PARAM_INVALID,
                          "Unexpected msg type in RecvCacheTableInfoResp: %d", static_cast<int32_t>(msg_type));

  LLM_CHK_BOOL_RET_STATUS(
      hixl::CtrlMsgPlugin::Recv(fd, &cache_table_info, static_cast<uint32_t>(sizeof(CacheTableInfo)), timeout_ms) ==
          hixl::SUCCESS,
      ge::FAILED, "Failed to recv cache table body");
  return ge::SUCCESS;
}

ge::Status HixlEntity::Finalize(bool force) {
  if (!force) {
    LLM_CHK_BOOL_RET_STATUS(engine_->Disconnect(remote_engine_.c_str(), kDisconnectTimeoutMs) == hixl::SUCCESS,
                            ge::FAILED,
                            "Failed to disconnect to remote engine, remote_engine:%s.", remote_engine_.c_str());
  }
  return ge::SUCCESS;
}

ge::Status HixlEntity::BatchTransfer(std::list<HcclOneSideOpDesc> &tasks, bool is_put, bool reversed, int32_t timeout_ms) {
  std::vector<hixl::TransferOpDesc> op_descs;
  LLMLOGI("task num = %zu", tasks.size());
  while (!tasks.empty()) {
    auto op_desc = tasks.front();
    tasks.pop_front();
    hixl::TransferOpDesc desc{};
    if (reversed) {
      std::swap(op_desc.localAddr, op_desc.remoteAddr);
    }
    desc.local_addr = reinterpret_cast<uintptr_t>(op_desc.localAddr);
    desc.remote_addr = reinterpret_cast<uintptr_t>(op_desc.remoteAddr);
    desc.len = op_desc.count;
    op_descs.emplace_back(desc);
  }
  LLM_CHK_BOOL_RET_STATUS(engine_->TransferSync(
      remote_engine_.c_str(), is_put ? hixl::WRITE : hixl::READ, op_descs, timeout_ms) == hixl::SUCCESS,
      ge::FAILED,
      "Failed to batch transfer, remote_engine:%s.", remote_engine_.c_str());
  return ge::SUCCESS;
}

}  // namespace llm
