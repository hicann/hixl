/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hixl_server.h"

#include "common/hixl_cs.h"
#include "nlohmann/json.hpp"
#include "cs/hixl_cs_server.h"
#include "common/hixl_checker.h"
#include "common/ctrl_msg.h"
#include "common/ctrl_msg_plugin.h"
#include "common/hixl_inner_types.h"
#include "common/hixl_utils.h"

namespace hixl {
Status HixlServer::Initialize(const std::string &ip, int32_t port,
                              const std::vector<EndpointConfig> &data_endpoint_config_list) {
  data_endpoint_config_list_ = data_endpoint_config_list;
  std::vector<EndpointDesc> data_end_point_list;
  int32_t dev_logic_id = 0;
  int32_t dev_phy_id = 0;
  HIXL_CHK_ACL_RET(aclrtGetDevice(&dev_logic_id));
  HIXL_CHK_ACL_RET(aclrtGetPhyDevIdByLogicDevId(dev_logic_id, &dev_phy_id));
  for (const auto &it : data_endpoint_config_list) {
    EndpointDesc end_point_info{};
    HIXL_CHK_STATUS_RET(ConvertToEndpointInfo(it, end_point_info, static_cast<uint32_t>(dev_phy_id)),
                        "Failed to convert endpoint config to endpoint info.");
    data_end_point_list.emplace_back(end_point_info);
  }
  const EndpointDesc *endpoints = data_end_point_list.data();
  if (port < 0) {
    port = 0;
  }
  HixlServerConfig config{};
  HixlServerDesc server_desc{};
  server_desc.server_ip = ip.c_str();
  server_desc.server_port = static_cast<uint32_t>(port);
  server_desc.endpoint_list = endpoints;
  server_desc.endpoint_list_num = static_cast<uint32_t>(data_endpoint_config_list.size());
  HIXL_CHK_STATUS_RET(HixlCSServerCreate(&server_desc, &config, &server_handle_),
                      "Failed to create hixl server, ip:%s, port:%d.", ip.c_str(), port);
  // port > 0 初始化hixl server，否则作为hixl client注册内存用
  if (port > 0) {
    // 注册回调函数且监听端口
    MsgProcessor send_endpoint_cb = [this](int32_t fd, const char *msg, uint64_t msg_len) -> Status {
      (void)msg;
      (void)msg_len;
      std::string msg_str;
      HIXL_CHK_STATUS_RET(SerializeEndpointConfigList(data_endpoint_config_list_, msg_str),
                          "Failed to serialize endpoint config.");
      CtrlMsgHeader header{};
      header.magic = kMagicNumber;
      header.body_size = static_cast<uint64_t>(sizeof(CtrlMsgType) + msg_str.size());
      CtrlMsgType msg_type = CtrlMsgType::kGetEndPointInfoResp;
      HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Send(fd, &header, static_cast<uint64_t>(sizeof(header))));
      HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Send(fd, &msg_type, static_cast<uint64_t>(sizeof(msg_type))));
      HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Send(fd, msg_str.c_str(), static_cast<uint64_t>(msg_str.size())));
      return SUCCESS;
    };
    HIXL_CHK_STATUS_RET(HixlCSServerRegProc(server_handle_, CtrlMsgType::kGetEndPointInfoReq, send_endpoint_cb),
                        "Failed to register send endpoint info processor.");
    HIXL_CHK_STATUS_RET(HixlCSServerListen(server_handle_, static_cast<uint32_t>(port)),
                        "HixlServer listen failed, port:%d.", port);
  }
  return SUCCESS;
}

Status HixlServer::RegisterMem(const MemDesc &mem, MemType type, MemHandle &mem_handle) {
  HIXL_CHECK_NOTNULL(server_handle_);
  AddrInfo cur_info{};
  cur_info.start_addr = mem.addr;
  cur_info.end_addr = mem.addr + mem.len;
  cur_info.mem_type = type;
  std::lock_guard<std::mutex> lk(mtx_);

  bool is_duplicate = false;
  MemHandle existing_handle = nullptr;
  HIXL_CHK_STATUS_RET(CheckAddrOverlap(cur_info, handle_to_addr_, is_duplicate, existing_handle),
                      "Failed to check address overlap.");
  if (is_duplicate) {
    mem_handle = existing_handle;
    HIXL_LOGI("Memory already registered, returning existing handle:%p", mem_handle);
    return SUCCESS;
  }

  HcommMem hccl_mem{};
  hccl_mem.type = (type == MemType::MEM_DEVICE) ? HCCL_MEM_TYPE_DEVICE : HCCL_MEM_TYPE_HOST;
  hccl_mem.addr = reinterpret_cast<void *>(mem.addr);
  hccl_mem.size = mem.len;
  HIXL_CHK_STATUS_RET(HixlCSServerRegMem(server_handle_, nullptr, &hccl_mem, &mem_handle),
                      "Failed to register mem, addr:0x%lx, size:%lu, type:%d.", mem.addr, mem.len,
                      static_cast<int32_t>(type));
  handle_to_addr_[mem_handle] = cur_info;
  return SUCCESS;
}

Status HixlServer::DeregisterMem(MemHandle &mem_handle) {
  HIXL_CHECK_NOTNULL(server_handle_);
  // 判断mem_handle是否存在
  std::lock_guard<std::mutex> lk(mtx_);
  auto it = handle_to_addr_.find(mem_handle);
  if (it == handle_to_addr_.end()) {
    HIXL_LOGW("mem_handle:%p is not registered.", mem_handle);
    return SUCCESS;
  }
  HIXL_CHK_STATUS_RET(HixlCSServerUnregMem(server_handle_, mem_handle), "Failed to deregister mem, handle:%p.",
                      mem_handle);
  handle_to_addr_.erase(it);
  mem_handle = nullptr;
  return SUCCESS;
}

Status HixlServer::Finalize() {
  if (server_handle_ == nullptr) {
    return SUCCESS;
  }
  // 注销所有注册的内存
  std::lock_guard<std::mutex> lk(mtx_);
  for (const auto &handle : handle_to_addr_) {
    Status ret = HixlCSServerUnregMem(server_handle_, handle.first);
    if (ret != SUCCESS) {
      HIXL_LOGE(ret, "Failed to deregister mem, handle:%p.", handle);
    }
  }
  handle_to_addr_.clear();
  HIXL_CHK_STATUS_RET(HixlCSServerDestroy(server_handle_), "Failed to destroy hixl server.");
  server_handle_ = nullptr;
  return SUCCESS;
}

}  // namespace hixl