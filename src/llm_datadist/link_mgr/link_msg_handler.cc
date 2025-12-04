/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "link_msg_handler.h"
#include <fstream>
#include <chrono>
#include "nlohmann/json.hpp"
#include "llm_datadist/llm_datadist.h"
#include "common/rank_table_generator.h"
#include "common/llm_utils.h"
#include "common/llm_checker.h"
#include "common/llm_scope_guard.h"

namespace llm {
namespace {
constexpr uint32_t kMaxLinkNum = 512;
constexpr const char kOptionRdmaTrafficClass[] = "llm.RdmaTrafficClass";
constexpr const char kOptionRdmaServiceLevel[] = "llm.RdmaServiceLevel";
constexpr uint64_t kDefaultReqBufferSize = 112U * 1024U;
constexpr uint64_t kDefaultRespBufferSize = 16U * 1024U;
}

static void from_json(const nlohmann::json &j, LLMExchangeInfo &l) {
  j.at("cache_table_addr").get_to(l.cache_table_addr);
  j.at("cache_table_size").get_to(l.cache_table_size);
  j.at("req_addr").get_to(l.req_addr);
  j.at("req_size").get_to(l.req_size);
  j.at("resp_addr").get_to(l.resp_addr);
  j.at("resp_size").get_to(l.resp_size);
  j.at("cluster_id").get_to(l.cluster_id);
  j.at("comm_res").get_to(l.comm_res);
  j.at("timeout").get_to(l.timeout);
  j.at("force_link").get_to(l.force_link);
}

static void to_json(nlohmann::json &j, const LLMExchangeInfo &l) {
  j = nlohmann::json{};
  j["cache_table_addr"] = l.cache_table_addr;
  j["cache_table_size"] = l.cache_table_size;
  j["req_addr"] = l.req_addr;
  j["req_size"] = l.req_size;
  j["resp_addr"] = l.resp_addr;
  j["resp_size"] = l.resp_size;
  j["cluster_id"] = l.cluster_id;
  j["comm_res"] = l.comm_res;
  j["timeout"] = l.timeout;
  j["force_link"] = l.force_link;
}

static void from_json(const nlohmann::json &j, LLMLinkStatus &l) {
  j.at("error_code").get_to(l.error_code);
  j.at("error_message").get_to(l.error_message);
}

static void to_json(nlohmann::json &j, const LLMLinkStatus &l) {
  j = nlohmann::json{};
  j["error_code"] = l.error_code;
  j["error_message"] = l.error_message;
}

static void from_json(const nlohmann::json &j, LLMDisconnectInfo &l) {
  j.at("cluster_id").get_to(l.cluster_id);
  j.at("timeout").get_to(l.timeout);
}

static void to_json(nlohmann::json &j, const LLMDisconnectInfo &l) {
  j = nlohmann::json{};
  j["cluster_id"] = l.cluster_id;
  j["timeout"] = l.timeout;
}

template<typename T>
ge::Status LinkMsgHandler::Serialize(const T &msg, std::string &msg_str) {
   try {
    nlohmann::json j = msg;
    msg_str = j.dump();
  } catch (const nlohmann::json::exception &e) {
    LLMLOGE(ge::LLM_PARAM_INVALID, "Failed to dump msg to str, exception:%s", e.what());
    return ge::LLM_PARAM_INVALID;
  }
  return ge::SUCCESS;
}

template<typename T>
ge::Status LinkMsgHandler::Deserialize(const std::vector<char> &msg_str, T &msg) {
   try {
    auto j = nlohmann::json::parse(&msg_str[0]);
    msg = j.get<T>();
  } catch (const nlohmann::json::exception &e) {
    LLMLOGE(ge::LLM_PARAM_INVALID, "Failed to load msg, exception:%s", e.what());
    return ge::LLM_PARAM_INVALID;
  }
  return ge::SUCCESS;
}

ge::Status LinkMsgHandler::Initialize(const std::map<ge::AscendString, ge::AscendString> &options) {
  LLM_CHECK_NOTNULL(comm_entity_manager_);
  LLM_CHECK_NOTNULL(comm_mem_manager_);
  LLM_CHECK_NOTNULL(cache_manager_);
  LLM_CHK_STATUS_RET(LLMUtils::ParseFlag(kLlmOptionEnableRemoteCacheAccessible,
                                        options,
                                        remote_cache_accessible_),
                    "Failed to parse option %s", kLlmOptionEnableRemoteCacheAccessible);
  const auto &ip_iter = options.find(kLlmOptionListenIp);
  if (ip_iter != options.cend()) {
    local_ip_ = ip_iter->second.GetString();
  }
  const auto &it = options.find(llm_datadist::OPTION_LOCAL_COMM_RES);
  if (it != options.cend()) {
    local_comm_res_ = it->second.GetString();
  }
  if (local_comm_res_.empty() && (!local_ip_.empty())) {
    LLM_CHK_STATUS_RET(LocalCommResGenerator::Generate(local_ip_, device_id_, local_comm_res_),
                      "Failed to generate local comm res, local_ip:%s, device_id:%d",
                      local_ip_.c_str(), device_id_);
  }
  HcclAdapter::GetInstance().HcclCommConfigInit(&comm_config_);
  const auto &traffic_it = options.find(kOptionRdmaTrafficClass);
  if (traffic_it != options.cend()) {
    uint32_t traffic_class = 0U;
    LLM_CHK_STATUS_RET(LLMUtils::ToNumber(traffic_it->second.GetString(), traffic_class),
                      "llm.RdmaTrafficClass is invalid, value = %s",
                      traffic_it->second.GetString());
    comm_config_.hcclRdmaTrafficClass = traffic_class;
    LLMLOGI("set rdma traffic class to %u.", traffic_class);
  }
  const auto &service_it = options.find(kOptionRdmaServiceLevel);
  if (service_it != options.cend()) {
    uint32_t service_level = 0U;
    LLM_CHK_STATUS_RET(LLMUtils::ToNumber(service_it->second.GetString(), service_level),
                      "llm.RdmaServiceLevel is invalid, value = %s", service_it->second.GetString());
    comm_config_.hcclRdmaServiceLevel = service_level;
    LLMLOGI("set rdma service level to %u.", service_level);
  }
  LLM_ASSERT_RT_OK(rtCtxGetCurrent(&rt_context_));
  const auto &buffer_and_size = cache_manager_->GetCacheTableBufferAndSize();
  LLM_CHK_STATUS_RET(comm_mem_manager_->RegisterCommMemAddr(buffer_and_size.first,
                                                          buffer_and_size.second,
                                                          HcclMemType::HCCL_MEM_TYPE_DEVICE),
                    "Failed to register cache table addr");
  handler_plugin_.Initialize();
  return ge::SUCCESS;
}

void LinkMsgHandler::Finalize() {
  handler_plugin_.Finalize();
  comm_entity_manager_ = nullptr;
  comm_mem_manager_ = nullptr;
  cache_manager_ = nullptr;
}

ge::Status LinkMsgHandler::StartDaemon(uint32_t listen_port) {
  handler_plugin_.RegisterConnectedProcess([this](int32_t fd, bool &keep_fd) {
    (void) ConnectedProcess(fd, keep_fd);
  });
  return handler_plugin_.StartDaemon(listen_port);
}

ge::Status LinkMsgHandler::StopDaemon() {
  handler_plugin_.Finalize();
  return ge::SUCCESS;
}

template<typename T>
ge::Status LinkMsgHandler::SendMsg(int32_t fd, LinkMsgType msg_type, const T &msg) {
  std::string msg_str;
  LLM_CHK_STATUS_RET(LinkMsgHandler::Serialize(msg, msg_str), "Failed to serialize msg");
  LLM_CHK_STATUS_RET(MsgHandlerPlugin::SendMsg(fd, static_cast<int32_t>(msg_type), msg_str),
                    "Failed to send msg");
  return ge::SUCCESS;
}

template<typename T>
ge::Status LinkMsgHandler::RecvMsg(int32_t fd, LinkMsgType msg_type, T &msg) {
  std::vector<char> msg_str;
  int32_t type = 0;
  LLM_CHK_STATUS_RET(MsgHandlerPlugin::RecvMsg(fd, type, msg_str),
                    "Failed to recv msg");
  LLM_CHK_BOOL_RET_STATUS(msg_type == static_cast<LinkMsgType>(type),
                         ge::FAILED, "Failed to check recv msg type:%d, expect type:%d",
                         type, static_cast<int32_t>(msg_type));
  LLM_CHK_STATUS_RET(LinkMsgHandler::Deserialize(msg_str, msg), "Failed to deserialize msg");
  return ge::SUCCESS;
}

ge::Status LinkMsgHandler::ProcessConnectRequest(int32_t fd, const std::vector<char> &msg) {
  EntityMemInfoPtr mem_info_ptr = nullptr;
  LLM_CHK_STATUS_RET(CreateEntityMemInfo(mem_info_ptr), "Failed to create entity mem info");
  LLMExchangeInfo exchange_info = {};
  const auto &buffer_and_size = cache_manager_->GetCacheTableBufferAndSize();
  exchange_info.cache_table_addr = PtrToValue(buffer_and_size.first);
  exchange_info.cache_table_size = static_cast<uint64_t>(buffer_and_size.second);
  exchange_info.comm_res = local_comm_res_;
  exchange_info.cluster_id = cluster_id_;
  exchange_info.req_addr = PtrToValue(mem_info_ptr->req_);
  exchange_info.req_size = kDefaultReqBufferSize;
  exchange_info.resp_addr = PtrToValue(mem_info_ptr->resp_);
  exchange_info.resp_size = kDefaultRespBufferSize;
  LLM_CHK_STATUS_RET(SendMsg(fd, LinkMsgType::kConnect, exchange_info), "Failed to send connect msg");

  auto ret = ge::SUCCESS;
  LLM_MAKE_GUARD(send_status, ([fd, &ret]() {
    LLMLinkStatus status{};
    status.error_code = ret;
    LinkMsgHandler::SendMsg(fd, LinkMsgType::kStatus, status);
  }));

  LLMExchangeInfo peer_exchange_info{};
  LLM_CHK_STATUS_RET(LinkMsgHandler::Deserialize(msg, peer_exchange_info), "Failed to deserialize connect msg");
  LLMLOGI("Start to process link cluster, local cluster_id:%lu, remote cluster_id:%lu, timeout:%d ms.",
         cluster_id_, peer_exchange_info.cluster_id, peer_exchange_info.timeout);
  peer_exchange_info.comm_name = std::to_string(peer_exchange_info.cluster_id) + "_" + std::to_string(cluster_id_);
  ret = ExchangeInfoProcess(peer_exchange_info, peer_exchange_info.timeout, peer_exchange_info.force_link,
                            mem_info_ptr);
  if (ret == ge::SUCCESS) {
    LLMLOGI("Success to process link cluster, local cluster_id:%lu, remote cluster_id:%lu.",
           cluster_id_, peer_exchange_info.cluster_id);
  }
  LLM_CHK_STATUS(ret, "Failed to process peer exchange info, timeout:%d ms", peer_exchange_info.timeout);
  return ret;
}

ge::Status LinkMsgHandler::ProcessDisconnectRequest(int32_t fd, const std::vector<char> &msg) const {
  auto ret = ge::SUCCESS;
  LLM_MAKE_GUARD(send_status, ([fd, &ret]() {
    LLMLinkStatus status{};
    status.error_code = ret;
    LinkMsgHandler::SendMsg(fd, LinkMsgType::kStatus, status);
  }));

  LLMDisconnectInfo peer_disconnect_info{};
  LLM_CHK_STATUS_RET(LinkMsgHandler::Deserialize(msg, peer_disconnect_info), "Failed to deserialize disconnect msg");
  LLMLOGI("Start to process disconnect cluster, local cluster_id:%lu, remote cluster_id:%lu.",
         cluster_id_, peer_disconnect_info.cluster_id);
  ret = DisconnectInfoProcess(peer_disconnect_info);
  if (ret == ge::SUCCESS) {
    LLMLOGI("Success to process disconnect cluster, local cluster_id:%lu, remote cluster_id:%lu.",
           cluster_id_, peer_disconnect_info.cluster_id);
  }
  LLM_CHK_STATUS(ret, "Failed to process disconnect info, local cluster_id:%lu, remote cluster_id:%lu",
                cluster_id_, peer_disconnect_info.cluster_id);
  return ret;
}

ge::Status LinkMsgHandler::ConnectedProcess(int32_t fd, bool &keep_fd) {
  keep_fd = false;
  int32_t msg_type = 0;
  std::vector<char> msg;
  LLM_CHK_STATUS_RET(MsgHandlerPlugin::RecvMsg(fd, msg_type, msg), "Failed to recv msg");
  LLM_CHK_BOOL_RET_STATUS(static_cast<LinkMsgType>(msg_type) == LinkMsgType::kConnect ||
                         static_cast<LinkMsgType>(msg_type) == LinkMsgType::kDisconnect,
                         ge::LLM_PARAM_INVALID,
                         "Failed to check msg type:%d", msg_type);

  if (static_cast<LinkMsgType>(msg_type) == LinkMsgType::kConnect) {
    LLM_CHK_STATUS_RET(ProcessConnectRequest(fd, msg), "Failed to process connect request");
  } else {
    LLM_CHK_STATUS_RET(ProcessDisconnectRequest(fd, msg), "Failed to process disconnect request");
  }
  return ge::SUCCESS;
}

ge::Status LinkMsgHandler::SetEntityMemInfo(const LLMExchangeInfo &peer_exchange_info,
                                            EntityPtr entity, EntityMemInfoPtr &mem_info_ptr) const {
  // prepare mem
  entity->SetEntityMemInfo(mem_info_ptr);
  auto &remote_mems = entity->GetRemoteMems();
  HcclMem remote_mem{};
  remote_mem.type = HcclMemType::HCCL_MEM_TYPE_DEVICE;
  remote_mem.addr = ValueToPtr(peer_exchange_info.cache_table_addr);
  remote_mem.size = peer_exchange_info.cache_table_size;
  remote_mems.emplace_back(remote_mem);
  remote_mem.type = HcclMemType::HCCL_MEM_TYPE_HOST;
  remote_mem.addr = ValueToPtr(peer_exchange_info.req_addr);
  remote_mem.size = peer_exchange_info.req_size;
  remote_mems.emplace_back(remote_mem);
  remote_mem.type = HcclMemType::HCCL_MEM_TYPE_HOST;
  remote_mem.addr = ValueToPtr(peer_exchange_info.resp_addr);
  remote_mem.size = peer_exchange_info.resp_size;
  remote_mems.emplace_back(remote_mem);
  LLM_CHK_STATUS_RET(entity->SetInfo(), "Failed to set entity mem info");
  entity->MarkEntityIdle();
  return ge::SUCCESS;
}

ge::Status LinkMsgHandler::ExchangeInfoProcess(const LLMExchangeInfo &peer_exchange_info, int32_t timeout,
                                               bool force_link, EntityMemInfoPtr &mem_info_ptr) {
  auto rank_table_generator = RankTableGeneratorFactory::Create(local_comm_res_, peer_exchange_info.comm_res);
  LLM_CHK_BOOL_RET_STATUS(rank_table_generator != nullptr, ge::LLM_PARAM_INVALID,
                         "Failed to create rank table generator.");
  std::string rank_table;
  LLM_CHK_STATUS_RET(rank_table_generator->Generate(device_id_, rank_table), "Failed to generate rank table");
  auto local_rank_id = rank_table_generator->GetLocalRankId();
  LLM_CHK_BOOL_RET_STATUS(local_rank_id >= 0, ge::LLM_PARAM_INVALID,
                         "Failed to get local rank id, please check rank table.");
  auto peer_rank_id = rank_table_generator->GetPeerRankId();
  if (force_link) {
    LLM_CHK_STATUS_RET(comm_entity_manager_->DestroyEntity(peer_exchange_info.cluster_id),
                      "Failed to destroy previous entity, peer cluster id:%lu.",
                      peer_exchange_info.cluster_id);
  }
  CommEntityParams entity_param{};
  entity_param.comm_id = UINT64_MAX;  // do not use comm_id
  entity_param.peer_cluster_id = peer_exchange_info.cluster_id;
  entity_param.peer_rank_id = peer_rank_id;
  entity_param.local_cluster_id = cluster_id_;
  entity_param.local_rank_id = local_rank_id;
  entity_param.remote_cache_accessible = remote_cache_accessible_;
  EntityCommInfo::CommParams comm_params{};
  comm_params.rank_id = local_rank_id;
  comm_params.comm_config = comm_config_;
  LLM_ASSERT_EOK(strcpy_s(comm_params.comm_config.hcclCommName, COMM_NAME_MAX_LENGTH,
                         peer_exchange_info.comm_name.c_str()));
  comm_params.rank_table = rank_table;
  comm_params.mem_handles = comm_mem_manager_->GetAllRegisterMemHandles();
  constexpr uint32_t kTimeInSec = 1000;
  auto left_time = timeout % kTimeInSec == 0 ? 0 : 1;
  comm_params.timeout = timeout / kTimeInSec + left_time;
  EntityPtr entity = nullptr;
  LLM_CHK_STATUS_RET(comm_entity_manager_->CreateEntity(entity_param, comm_params, entity),
                    "Failed to create entity");
  LLMLOGI("Success to create comm entity:%s", entity->GetDesc().c_str());
  entity->SetContext(rt_context_);
  entity->SetCacheManager(cache_manager_);
  LLM_DISMISSABLE_GUARD(fail_guard, ([this, &peer_exchange_info]() {
    (void) comm_entity_manager_->DestroyEntity(peer_exchange_info.cluster_id);
  }));
  LLM_CHK_STATUS_RET(SetEntityMemInfo(peer_exchange_info, entity, mem_info_ptr), "Failed ti set entity mem info");
  LLM_DISMISS_GUARD(fail_guard);
  return ge::SUCCESS;
}

ge::Status LinkMsgHandler::GenerateLocalCommRes(const ClusterInfo &cluster) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!local_comm_res_.empty()) {
    return ge::SUCCESS;
  }
  LLM_CHK_BOOL_RET_STATUS(cluster.local_ip_infos.size() == 1U, ge::LLM_PARAM_INVALID,
                         "In scenarios where local comm res is not configured, "
                         "local_ip_infos must be specified and only supports a size of 1.");
  LLM_CHK_STATUS_RET(LLMUtils::IntToIp(cluster.local_ip_infos[0].ip, local_ip_), "Failed to covert local ip.");
  LLM_CHK_STATUS_RET(LocalCommResGenerator::Generate(local_ip_, device_id_, local_comm_res_),
                    "Failed to generate local comm res, local_ip:%s, device_id:%d",
                    local_ip_.c_str(), device_id_);
  return ge::SUCCESS;
}

ge::Status LinkMsgHandler::DisconnectInfoProcess(const LLMDisconnectInfo &peer_disconnect_info) const {
  return comm_entity_manager_->DestroyEntity(peer_disconnect_info.cluster_id);
}

ge::Status LinkMsgHandler::CreateEntityMemInfo(EntityMemInfoPtr &mem_info_ptr) {
  mem_info_ptr = MakeUnique<EntityMemInfo>(remote_cache_accessible_,
                                           comm_entity_manager_->GetHostRegPool(),
                                           comm_entity_manager_->GetDeviceRegPool());
  LLM_CHECK_NOTNULL(mem_info_ptr);
  LLM_CHK_STATUS_RET(mem_info_ptr->Initialize(), "Failed to init mem info");
  return ge::SUCCESS;
}

ge::Status LinkMsgHandler::LinkCluster(const ClusterInfo &cluster, int32_t timeout) {
  LLM_CHK_BOOL_RET_STATUS(cluster.remote_ip_infos.size() == 1U, ge::LLM_PARAM_INVALID,
                         "remote_ip_infos size != 1 is unsupported.");
  std::string remote_ip_str;
  LLM_CHK_STATUS_RET(LLMUtils::IntToIp(cluster.remote_ip_infos[0].ip, remote_ip_str), "Failed to covert remote ip.");
  LLM_CHK_STATUS_RET(GenerateLocalCommRes(cluster), "Failed to generate local comm res");
  int32_t conn_fd = 0;
  uint32_t remote_port = static_cast<uint32_t>(cluster.remote_ip_infos[0].port);
  LLMLOGI("Start to link cluster, local cluster_id:%lu, remote info %s:%u, timeout:%d ms.",
         cluster_id_, remote_ip_str.c_str(), remote_port, timeout);
  LLM_CHK_STATUS_RET(MsgHandlerPlugin::Connect(remote_ip_str, remote_port, conn_fd, timeout, ge::LLM_LINK_FAILED),
                    "Failed to connect remote addr %s:%u, timeout=%d ms.",
                    remote_ip_str.c_str(), remote_port, timeout);
  LLMLOGI("connect to remote addr %s:%u success", remote_ip_str.c_str(), remote_port);

  LLM_MAKE_GUARD(close_fd, ([conn_fd]() { MsgHandlerPlugin::Disconnect(conn_fd); }));
  EntityMemInfoPtr mem_info_ptr = nullptr;
  LLM_CHK_STATUS_RET(CreateEntityMemInfo(mem_info_ptr), "Failed to create entity mem info");
  LLMExchangeInfo exchange_info = {};
  const auto &buffer_and_size = cache_manager_->GetCacheTableBufferAndSize();
  exchange_info.cache_table_addr = PtrToValue(buffer_and_size.first);
  exchange_info.cache_table_size = static_cast<uint64_t>(buffer_and_size.second);
  exchange_info.comm_res = local_comm_res_;
  exchange_info.cluster_id = cluster_id_;
  exchange_info.timeout = timeout;
  // The local comm does not exist; force the peer link if the peer comm exists.
  exchange_info.force_link = comm_entity_manager_->GetEntityByRemoteClusterId(cluster.remote_cluster_id) == nullptr;
  exchange_info.req_addr = PtrToValue(mem_info_ptr->req_);
  exchange_info.req_size = kDefaultReqBufferSize;
  exchange_info.resp_addr = PtrToValue(mem_info_ptr->resp_);
  exchange_info.resp_size = kDefaultRespBufferSize;

  LLM_CHK_STATUS_RET(SendMsg(conn_fd, LinkMsgType::kConnect, exchange_info), "Failed to send connect msg");
  LLMExchangeInfo peer_exchange_info = {};
  LLM_CHK_STATUS_RET(RecvMsg(conn_fd, LinkMsgType::kConnect, peer_exchange_info), "Failed to recv connect msg");

  peer_exchange_info.comm_name = std::to_string(cluster_id_) + "_" + std::to_string(peer_exchange_info.cluster_id);
  auto ret = ExchangeInfoProcess(peer_exchange_info, timeout, false, mem_info_ptr);
  LLMLinkStatus status{};
  LLM_CHK_STATUS_RET(RecvMsg(conn_fd, LinkMsgType::kStatus, status), "Failed to recv status msg");
  LLM_CHK_STATUS_RET(status.error_code, "Failed to check peer process ret status, error code[%u], err msg[%s]",
                    status.error_code, status.error_message.c_str());
  LLM_CHK_STATUS_RET(ret, "Failed to process peer exchange info, timeout:%d", timeout);
  LLMLOGI("Link cluster success, local cluster_id:%lu, remote cluster_id:%lu, remote ip:%s, remote port:%u",
         cluster_id_, peer_exchange_info.cluster_id, remote_ip_str.c_str(), remote_port);
  return ge::SUCCESS;
}

ge::Status LinkMsgHandler::UnlinkCluster(const ClusterInfo &cluster, int32_t timeout, bool force_flag) const {
  LLMLOGI("Start to unlink cluster, local cluster_id:%lu, remote cluster_id:%lu, timeout:%d ms, force_flag:%d",
         cluster_id_, cluster.remote_cluster_id, timeout, static_cast<int32_t>(force_flag));
  LLM_CHK_BOOL_RET_STATUS(force_flag || cluster.remote_ip_infos.size() == 1U, ge::LLM_PARAM_INVALID,
                         "When force_flag=false, remote_ip_infos size != 1 is unsupported.");
  int32_t conn_fd = -1;
  LLM_MAKE_GUARD(close_fd, ([&conn_fd]() {
    if (conn_fd != -1) {
      MsgHandlerPlugin::Disconnect(conn_fd);
    }
  }));

  if (!force_flag) {
    std::string remote_ip_str;
    LLM_CHK_STATUS_RET(LLMUtils::IntToIp(cluster.remote_ip_infos[0].ip, remote_ip_str), "Failed to covert remote ip.");
    uint32_t remote_port = static_cast<uint32_t>(cluster.remote_ip_infos[0].port);
    LLM_CHK_STATUS_RET(MsgHandlerPlugin::Connect(remote_ip_str, remote_port, conn_fd, timeout, ge::LLM_UNLINK_FAILED),
                      "Failed to connect remote addr %s:%u, timeout=%d ms.",
                      remote_ip_str.c_str(), remote_port, timeout);
    LLMLOGI("connect to remote addr %s:%u success", remote_ip_str.c_str(), remote_port);

    LLMDisconnectInfo disconnect_info = {};
    disconnect_info.cluster_id = cluster_id_;
    disconnect_info.timeout = timeout;
    LLM_CHK_STATUS_RET(SendMsg(conn_fd, LinkMsgType::kDisconnect, disconnect_info), "Failed to send disconnect msg");
  }
  LLMDisconnectInfo peer_disconnect_info = {};
  peer_disconnect_info.cluster_id = cluster.remote_cluster_id;
  peer_disconnect_info.timeout = timeout;

  auto ret = DisconnectInfoProcess(peer_disconnect_info);
  if (!force_flag) {
    LLMLinkStatus status{};
    LLM_CHK_STATUS_RET(RecvMsg(conn_fd, LinkMsgType::kStatus, status), "Failed to recv status msg");
    LLM_CHK_STATUS_RET(status.error_code, "Failed to check peer process ret status, error code[%u], err msg[%s]",
                      status.error_code, status.error_message.c_str());
  }
  LLM_CHK_STATUS_RET(ret,  "Failed to unlink cluster:%lu", cluster.remote_cluster_id);
  LLMLOGI("Unlink cluster success, local cluster_id:%lu, remote cluster_id:%lu, force_flag:%d",
         cluster_id_, cluster.remote_cluster_id, force_flag);
  return ge::SUCCESS;
}

size_t LinkMsgHandler::GetLinkSize() const {
  return comm_entity_manager_->GetEntitySize();
}
}  // namespace llm
