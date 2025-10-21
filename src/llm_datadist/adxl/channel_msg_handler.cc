/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "channel_msg_handler.h"
#include "nlohmann/json.hpp"
#include "adxl/adxl_types.h"
#include "common/rank_table_generator.h"
#include "common/msg_handler_plugin.h"
#include "hccl/hccl_adapter.h"
#include "common/llm_utils.h"
#include "channel.h"
#include "common/llm_checker.h"
#include "common/llm_scope_guard.h"

namespace adxl {

static inline void from_json(const nlohmann::json &j, AddrInfo &op_desc) {
  j.at("mem_type").get_to(op_desc.mem_type);
  j.at("start_addr").get_to(op_desc.start_addr);
  j.at("end_addr").get_to(op_desc.end_addr);
}

static inline void to_json(nlohmann::json &j, const AddrInfo &op_desc) {
  j = nlohmann::json{
      {"mem_type", op_desc.mem_type}, {"start_addr", op_desc.start_addr}, {"end_addr", op_desc.end_addr}};
}

static void from_json(const nlohmann::json &j, ChannelConnectInfo &c) {
  j.at("channel_id").get_to(c.channel_id);
  j.at("comm_res").get_to(c.comm_res);
  j.at("timeout").get_to(c.timeout);
  j.at("addrs").get_to(c.addrs);
}

static void to_json(nlohmann::json &j, const ChannelConnectInfo &c) {
  j = nlohmann::json{};
  j["channel_id"] = c.channel_id;
  j["comm_res"] = c.comm_res;
  j["timeout"] = c.timeout;
  j["addrs"] = c.addrs;
}

static void from_json(const nlohmann::json &j, ChannelStatus &c) {
  j.at("error_code").get_to(c.error_code);
  j.at("error_message").get_to(c.error_message);
}

static void to_json(nlohmann::json &j, const ChannelStatus &c) {
  j = nlohmann::json{};
  j["error_code"] = c.error_code;
  j["error_message"] = c.error_message;
}

static void from_json(const nlohmann::json &j, ChannelDisconnectInfo &c) {
  j.at("channel_id").get_to(c.channel_id);
}

static void to_json(nlohmann::json &j, const ChannelDisconnectInfo &c) {
  j = nlohmann::json{};
  j["channel_id"] = c.channel_id;
}

template<typename T>
Status ChannelMsgHandler::Serialize(const T &msg, std::string &msg_str) {
   try {
    nlohmann::json j = msg;
    msg_str = j.dump();
  } catch (const nlohmann::json::exception &e) {
    LLMLOGE(PARAM_INVALID, "Failed to dump msg to str, exception:%s", e.what());
    return PARAM_INVALID;
  }
  return SUCCESS;
}

template<typename T>
Status ChannelMsgHandler::Deserialize(const std::vector<char> &msg_str, T &msg) {
   try {
    auto j = nlohmann::json::parse(&msg_str[0]);
    msg = j.get<T>();
  } catch (const nlohmann::json::exception &e) {
    LLMLOGE(PARAM_INVALID, "Failed to load msg, exception:%s", e.what());
    return PARAM_INVALID;
  }
  return SUCCESS;
}

template<typename T>
Status ChannelMsgHandler::SendMsg(int32_t fd, ChannelMsgType msg_type, const T &msg) {
  std::string msg_str;
  ADXL_CHK_STATUS_RET(ChannelMsgHandler::Serialize(msg, msg_str), "Failed to serialize msg");
  ADXL_CHK_LLM_RET(llm::MsgHandlerPlugin::SendMsg(fd, static_cast<int32_t>(msg_type), msg_str),
                   "Failed to send msg");
  return SUCCESS;
}

template<typename T>
Status ChannelMsgHandler::RecvMsg(int32_t fd, ChannelMsgType msg_type, T &msg) {
  std::vector<char> msg_str;
  int32_t type = 0;
  ADXL_CHK_LLM_RET(llm::MsgHandlerPlugin::RecvMsg(fd, type, msg_str),
                   "Failed to recv msg");
  ADXL_CHK_BOOL_RET_STATUS(msg_type == static_cast<ChannelMsgType>(type),
                           FAILED, "Failed to check recv msg type:%d, expect type:%d",
                           type, static_cast<int32_t>(msg_type));
  ADXL_CHK_STATUS_RET(ChannelMsgHandler::Deserialize(msg_str, msg), "Failed to deserialize msg");
  return SUCCESS;
}

Status ChannelMsgHandler::ParseTrafficClass(const std::map<AscendString, AscendString> &options) {
  std::string traffic_class_str;
  const auto &traffic_it = options.find(hixl::OPTION_RDMA_TRAFFIC_CLASS);
  if (traffic_it != options.cend()) {
    traffic_class_str = traffic_it->second.GetString();
  } else {
    const auto &traffic_it2 = options.find(adxl::OPTION_RDMA_TRAFFIC_CLASS);
    if (traffic_it2 != options.cend()) {
      traffic_class_str = traffic_it2->second.GetString();
    }
  }

  if (!traffic_class_str.empty()) {
    uint32_t traffic_class = 0U;
    ADXL_CHK_LLM_RET(llm::LLMUtils::ToNumber(traffic_class_str, traffic_class),
                     "%s is invalid, value = %s",
                     hixl::OPTION_RDMA_TRAFFIC_CLASS, traffic_class_str.c_str());
    comm_config_.hcclRdmaTrafficClass = traffic_class;
    LLMLOGI("set rdma traffic class to %u.", traffic_class);
  }
  return SUCCESS;
}

Status ChannelMsgHandler::ParseServiceLevel(const std::map<AscendString, AscendString> &options) {
  std::string service_level_str;
  const auto &service_it = options.find(hixl::OPTION_RDMA_SERVICE_LEVEL);
  if (service_it != options.cend()) {
    service_level_str = service_it->second.GetString();
  } else {
    const auto &service_it2 = options.find(adxl::OPTION_RDMA_SERVICE_LEVEL);
    if (service_it2 != options.cend()) {
      service_level_str = service_it2->second.GetString();
    }
  }

  if (!service_level_str.empty()) {
    uint32_t service_level = 0U;
    ADXL_CHK_LLM_RET(llm::LLMUtils::ToNumber(service_level_str, service_level),
                     "%s is invalid, value = %s",
                     hixl::OPTION_RDMA_SERVICE_LEVEL, service_level_str.c_str());
    comm_config_.hcclRdmaServiceLevel = service_level;
    LLMLOGI("set rdma service level to %u.", service_level);
  }
  return SUCCESS;
}

Status ChannelMsgHandler::Initialize(const std::map<AscendString, AscendString> &options, SegmentTable *segment_table) {
  ADXL_CHECK_NOTNULL(channel_manager_);
  ADXL_CHK_ACL_RET(rtGetDevice(&device_id_));
  ADXL_CHK_STATUS_RET(ParseListenInfo(listen_info_, local_ip_, listen_port_), "Failed to parse listen info");
  ADXL_CHK_LLM_RET(llm::LocalCommResGenerator::Generate(local_ip_, device_id_, local_comm_res_),
                   "Failed to generate local comm res, local_ip:%s, device_id:%d",
                   local_ip_.c_str(), device_id_);
  llm::HcclAdapter::GetInstance().HcclCommConfigInit(&comm_config_);
  ADXL_CHK_STATUS_RET(ParseTrafficClass(options), "Failed to parse traffic class");
  ADXL_CHK_STATUS_RET(ParseServiceLevel(options), "Failed to parse service level");
  if (listen_port_ > 0) {
    ADXL_CHK_STATUS_RET(StartDaemon(listen_port_), "Failed to start listen deamon, port = %u", listen_port_);
    LLMEVENT("start daemon success, listen on port:%u", listen_port_);
  }
  segment_table_ = segment_table;
  return SUCCESS;
}

void ChannelMsgHandler::Finalize() {
  StopDaemon();
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto &it : handle_to_addr_) {
    auto handle = it.first;
    (void) llm::HcclAdapter::GetInstance().HcclDeregisterGlobalMem(handle);
  }
  handle_to_addr_.clear();
}

Status ChannelMsgHandler::ParseListenInfo(const std::string &listen_info,
                                          std::string &listen_ip, int32_t &listen_port) {
  const auto listen_infos = llm::LLMUtils::Split(listen_info, ':');
  ADXL_CHK_BOOL_RET_STATUS(listen_infos.size() >= 1U, PARAM_INVALID,
                           "listen info is invalid: %s, expect ${ip}:${port} or ${ip}", listen_info.c_str());
  listen_ip = listen_infos[0];
  ADXL_CHK_LLM_RET(llm::LLMUtils::CheckIp(listen_ip), "IP is invalid: %s, listen info = %s",
                   listen_ip.c_str(), listen_info.c_str());
  if (listen_infos.size() > 1U) {
    ADXL_CHK_LLM_RET(llm::LLMUtils::ToNumber(listen_infos[1], listen_port), "Port:%s is invalid.",
                     listen_infos[1].c_str());
  }
  return SUCCESS;
}

Status ChannelMsgHandler::RegisterMem(const MemDesc &mem, MemType type, MemHandle &mem_handle) {
  HcclMem hccl_mem = {};
  hccl_mem.type = type == MEM_DEVICE ? HCCL_MEM_TYPE_DEVICE : HCCL_MEM_TYPE_HOST;
  hccl_mem.addr = reinterpret_cast<void *>(mem.addr);
  hccl_mem.size = mem.len;
  ADXL_CHK_HCCL_RET(llm::HcclAdapter::GetInstance().HcclRegisterGlobalMem(&hccl_mem, &mem_handle));
  {
    std::lock_guard<std::mutex> lock(addr_info_mutex_);
    addr_infos_.emplace_back(AddrInfo{mem.addr, mem.addr + mem.len, type});
  }
  LLMLOGI("Add local mem range start:%lu, end:%lu, type:%d.", mem.addr, mem.addr + mem.len, type);
  segment_table_->AddRange(listen_info_, mem.addr, mem.addr + mem.len, type);
  std::lock_guard<std::mutex> lock(mutex_);
  handle_to_addr_[mem_handle] = reinterpret_cast<void *>(mem.addr);
  return SUCCESS;
}

Status ChannelMsgHandler::DeregisterMem(MemHandle mem_handle) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = handle_to_addr_.find(mem_handle);
  if (it == handle_to_addr_.end()) {
    LLMLOGW("handle:%p is not registered.", mem_handle);
    return SUCCESS; 
  }
  ADXL_CHK_HCCL_RET(llm::HcclAdapter::GetInstance().HcclDeregisterGlobalMem(mem_handle));
  handle_to_addr_.erase(it);
  return SUCCESS;
}

Status ChannelMsgHandler::StartDaemon(uint32_t listen_port) {
  handler_plugin_.RegisterConnectedProcess([this](int32_t fd, bool &keep_fd) {
    (void) ConnectedProcess(fd, keep_fd);
  });
  ADXL_CHK_LLM_RET(handler_plugin_.StartDaemon(listen_port), "Failed to start daemon.");
  return SUCCESS;
}

Status ChannelMsgHandler::StopDaemon() {
  handler_plugin_.Finalize();
  return SUCCESS;
}

Status ChannelMsgHandler::CreateChannel(const ChannelInfo &channel_info, bool is_client,
                                        const std::vector<AddrInfo> &remote_addrs) {
  LLMLOGI("Start to create channel, channel_id:%s, local_rank:%u, peer_rank:%u, is_client:%d",
         channel_info.channel_id.c_str(), channel_info.local_rank_id,
         channel_info.peer_rank_id, static_cast<int32_t>(is_client));
  if (!is_client &&
      channel_manager_->GetChannel(channel_info.channel_type, channel_info.channel_id) != nullptr) {
    LLMEVENT("channel:%s exist, begin to destroy, local_rank:%u, peer_rank:%u.",
            channel_info.channel_id.c_str(), channel_info.local_rank_id, channel_info.peer_rank_id);
    ADXL_CHK_STATUS_RET(channel_manager_->DestroyChannel(channel_info.channel_type, channel_info.channel_id),
                        "Failed to destroy previous channel, channel id:%s.",
                        channel_info.channel_id.c_str());
  }
  ChannelPtr channel = nullptr;
  ADXL_CHK_STATUS_RET(channel_manager_->CreateChannel(channel_info, channel));
  // add remote addr to segment table
  for (const auto &remote_addr : remote_addrs) {
    LLMLOGI("Add remote mem range start:%lu, end:%lu, type:%d.", remote_addr.start_addr, remote_addr.end_addr,
           remote_addr.mem_type);
    segment_table_->AddRange(channel->GetChannelId(), remote_addr.start_addr, remote_addr.end_addr,
                             remote_addr.mem_type);
  }
  LLMLOGI("Success to create channel, channel_id:%s, local_rank:%u, peer_rank:%u, is_client:%d",
         channel_info.channel_id.c_str(), channel_info.local_rank_id,
         channel_info.peer_rank_id, static_cast<int32_t>(is_client));
  return SUCCESS;
}


Status ChannelMsgHandler::ConnectInfoProcess(const ChannelConnectInfo &peer_channel_info,
                                             int32_t timeout, bool is_client) {
  auto rank_table_generator = llm::RankTableGeneratorFactory::Create(local_comm_res_, peer_channel_info.comm_res);
  ADXL_CHK_BOOL_RET_STATUS(rank_table_generator != nullptr, PARAM_INVALID,
                           "Failed to create rank table generator.");
  std::string rank_table;
  ADXL_CHK_STATUS_RET(rank_table_generator->Generate(device_id_, rank_table), "Failed to generate rank table");
  auto local_rank_id = rank_table_generator->GetLocalRankId();
  ADXL_CHK_BOOL_RET_STATUS(local_rank_id >= 0, PARAM_INVALID,
                           "Failed to get local rank id, please check rank table.");
  auto peer_rank_id = rank_table_generator->GetPeerRankId();
  ADXL_CHK_BOOL_RET_STATUS(peer_rank_id >= 0, PARAM_INVALID,
                           "Failed to get peer rank id, please check rank table, "
                           "not support connect with self device.");
  ChannelInfo channel_info{};
  channel_info.channel_type = is_client ? ChannelType::kClient : ChannelType::kServer;
  channel_info.channel_id = peer_channel_info.channel_id;
  channel_info.peer_rank_id = peer_rank_id;
  channel_info.local_rank_id = local_rank_id;
  channel_info.comm_config = comm_config_;
  auto ret = strcpy_s(channel_info.comm_config.hcclCommName, COMM_NAME_MAX_LENGTH,
                      peer_channel_info.comm_name.c_str());
  ADXL_CHK_BOOL_RET_STATUS(ret == EOK, FAILED, "Failed to copy comm name.");
  channel_info.rank_table = rank_table;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    channel_info.registered_mems = handle_to_addr_;
  }
  constexpr uint32_t kTimeInSec = 1000;
  auto left_time = timeout % kTimeInSec == 0 ? 0 : 1;
  channel_info.timeout_sec = timeout / kTimeInSec + left_time;
  ADXL_CHK_STATUS_RET(CreateChannel(channel_info, is_client, peer_channel_info.addrs), "Failed to create channel");
  return SUCCESS;
}

Status ChannelMsgHandler::ProcessConnectRequest(int32_t fd, const std::vector<char> &msg, bool &keep_fd) {
  ChannelConnectInfo channel_connect_info = {};
  channel_connect_info.channel_id = listen_info_;
  channel_connect_info.comm_res = local_comm_res_;
  {
    std::lock_guard<std::mutex> lock(addr_info_mutex_);
    channel_connect_info.addrs = addr_infos_;
  }
  ADXL_CHK_STATUS_RET(SendMsg(fd, ChannelMsgType::kConnect, channel_connect_info),
                      "Failed to send connect msg");

  auto ret = FAILED;
  ChannelConnectInfo peer_connect_info{};
  if (ChannelMsgHandler::Deserialize(msg, peer_connect_info) == SUCCESS) {
    LLMLOGI("Start to process connect info, local engine:%s, remote engine:%s, timeout:%d ms.",
           listen_info_.c_str(), peer_connect_info.channel_id.c_str(), peer_connect_info.timeout);
    peer_connect_info.comm_name = peer_connect_info.channel_id + "_" + listen_info_;
    ret = ConnectInfoProcess(peer_connect_info, peer_connect_info.timeout, false);
  }
  if (ret == SUCCESS) {
    LLMLOGI("Success to process connect info, local engine:%s, remote engine:%s, timeout:%d ms.",
           listen_info_.c_str(), peer_connect_info.channel_id.c_str(), peer_connect_info.timeout);
  }
  ADXL_CHK_STATUS(ret, "Failed to process connect info, local engine:%s, remote engine:%s, timeout:%d ms.",
                  listen_info_.c_str(), peer_connect_info.channel_id.c_str(), peer_connect_info.timeout);
  if (ret == SUCCESS) {
    auto channel = channel_manager_->GetChannel(ChannelType::kServer, peer_connect_info.channel_id);
    ADXL_CHK_BOOL_RET_STATUS(channel != nullptr, FAILED,
                             "Faield to get channel, local engine:%s, remote engine:%s.",
                             listen_info_.c_str(), peer_connect_info.channel_id.c_str());
    ADXL_CHK_STATUS_RET(channel->SetSocketNonBlocking(fd),
                        "Failed to start heartbeat, local engine:%s, remote engine:%s.",
                        listen_info_.c_str(), peer_connect_info.channel_id.c_str());
    ADXL_CHK_STATUS_RET(channel_manager_->AddSocketToEpoll(fd, channel),
                        "Failed to add fd to epoll, local engine:%s, remote engine:%s.",
                        listen_info_.c_str(), peer_connect_info.channel_id.c_str());
    keep_fd = true;
  }
  ChannelStatus status{};
  status.error_code = ret;
  ChannelMsgHandler::SendMsg(fd, ChannelMsgType::kStatus, status);
  return ret;
}

Status ChannelMsgHandler::DisconnectInfoProcess(ChannelType channel_type,
                                                const ChannelDisconnectInfo &peer_disconnect_info) {
  LLMLOGI("Destroy channel in disconnect process.");
  return channel_manager_->DestroyChannel(channel_type, peer_disconnect_info.channel_id);
}

Status ChannelMsgHandler::ProcessDisconnectRequest(int32_t fd, const std::vector<char> &msg) {
  auto ret = SUCCESS;
  LLM_MAKE_GUARD(send_status, ([fd, &ret]() {
    ChannelStatus status{};
    status.error_code = ret;
    ChannelMsgHandler::SendMsg(fd, ChannelMsgType::kStatus, status);
  }));

  ChannelDisconnectInfo peer_disconnect_info{};
  ADXL_CHK_STATUS_RET(ChannelMsgHandler::Deserialize(msg, peer_disconnect_info),
                      "Failed to deserialize disconnect msg");

  LLMLOGI("Start to process disconnect info, local engine:%s, remote engine:%s.",
         listen_info_.c_str(), peer_disconnect_info.channel_id.c_str());
  ret = DisconnectInfoProcess(ChannelType::kServer, peer_disconnect_info);
  if (ret == SUCCESS) {
    LLMLOGI("Success to process disconnect info, local engine:%s, remote engine:%s.",
           listen_info_.c_str(), peer_disconnect_info.channel_id.c_str());
  }
  ADXL_CHK_STATUS(ret, "Failed to process disconnect info, local engine:%s, remote engine:%s.",
                  listen_info_.c_str(), peer_disconnect_info.channel_id.c_str());
  return ret;
}

Status ChannelMsgHandler::ConnectedProcess(int32_t fd, bool &keep_fd) {
  keep_fd = false;
  int32_t msg_type = 0;
  std::vector<char> msg;
  ADXL_CHK_LLM_RET(llm::MsgHandlerPlugin::RecvMsg(fd, msg_type, msg), "Failed to recv msg");
  ADXL_CHK_BOOL_RET_STATUS(static_cast<ChannelMsgType>(msg_type) == ChannelMsgType::kConnect ||
                           static_cast<ChannelMsgType>(msg_type) == ChannelMsgType::kDisconnect,
                           PARAM_INVALID,
                           "Failed to check msg type:%d", msg_type);

  if (static_cast<ChannelMsgType>(msg_type) == ChannelMsgType::kConnect) {
    ADXL_CHK_STATUS_RET(ProcessConnectRequest(fd, msg, keep_fd), "Failed to process connect request");
  } else {
    ADXL_CHK_STATUS_RET(ProcessDisconnectRequest(fd, msg), "Failed to process disconnect request");
  }
  return SUCCESS;
}

Status ChannelMsgHandler::Connect(const std::string &remote_engine, int32_t timeout_in_millis) {
  auto channel = channel_manager_->GetChannel(ChannelType::kClient, remote_engine);
  ADXL_CHK_BOOL_RET_STATUS(channel == nullptr, ALREADY_CONNECTED,
                           "remote_engine:%s is already connected.", remote_engine.c_str());
  std::string remote_ip;
  int32_t remote_port = -1;
  ADXL_CHK_STATUS_RET(ParseListenInfo(remote_engine, remote_ip, remote_port), "Failed to parse listen info");
  int32_t conn_fd = 0;
  ADXL_CHK_LLM_RET(llm::MsgHandlerPlugin::Connect(remote_ip, static_cast<uint32_t>(remote_port),
                                                  conn_fd, timeout_in_millis, FAILED),
                   "Failed to connect, local engine:%s, remote engine:%s, timeout:%d ms.",
                   listen_info_.c_str(), remote_engine.c_str(), timeout_in_millis);

  LLM_DISMISSABLE_GUARD(close_fd, ([conn_fd, this, &remote_engine]() {
    llm::MsgHandlerPlugin::Disconnect(conn_fd);
    if (channel_manager_->GetChannel(ChannelType::kClient, remote_engine) != nullptr) {
      (void)channel_manager_->DestroyChannel(ChannelType::kClient, remote_engine);
    }
  }));
  ChannelConnectInfo connect_info = {};
  connect_info.channel_id = listen_info_;
  connect_info.comm_res = local_comm_res_;
  connect_info.timeout = timeout_in_millis;
  ADXL_CHK_STATUS_RET(SendMsg(conn_fd, ChannelMsgType::kConnect, connect_info), "Failed to send connect msg");
  ChannelConnectInfo peer_connect_info = {};
  ADXL_CHK_STATUS_RET(RecvMsg(conn_fd, ChannelMsgType::kConnect, peer_connect_info), "Failed to recv connect msg");
  peer_connect_info.comm_name = listen_info_ + "_" + peer_connect_info.channel_id;
  auto ret = ConnectInfoProcess(peer_connect_info, timeout_in_millis, true);
  ChannelStatus status{};
  ADXL_CHK_STATUS_RET(RecvMsg(conn_fd, ChannelMsgType::kStatus, status),
                      "Failed to recv status msg, local engine:%s, remote engine:%s, timeout:%d ms.",
                      listen_info_.c_str(), remote_engine.c_str(), timeout_in_millis);
  ADXL_CHK_STATUS_RET(status.error_code, "Failed to check peer process ret status, error code[%u], err msg[%s], "
                      "local engine:%s, remote engine:%s, timeout:%d ms.",
                      status.error_code, status.error_message.c_str(),
                      listen_info_.c_str(), remote_engine.c_str(), timeout_in_millis);
  ADXL_CHK_STATUS_RET(ret, "Failed to process connect info, timeout:%d", timeout_in_millis);
  channel = channel_manager_->GetChannel(ChannelType::kClient, remote_engine);
  ADXL_CHK_BOOL_RET_STATUS(channel != nullptr, FAILED,
                           "Faield to get channel, local engine:%s, remote engine:%s, timeout:%d ms.",
                           listen_info_.c_str(), remote_engine.c_str(), timeout_in_millis);
  ADXL_CHK_STATUS_RET(channel->SetSocketNonBlocking(conn_fd), "Failed to start heartbeat, remote_engine:%s.",
                      remote_engine.c_str());
  ADXL_CHK_STATUS_RET(channel_manager_->AddSocketToEpoll(conn_fd, channel),
                      "Failed to add fd to epoll, local engine:%s, remote engine:%s.",
                      listen_info_.c_str(), peer_connect_info.channel_id.c_str());
  LLM_DISMISS_GUARD(close_fd);
  LLMEVENT("Connect success, local engine:%s, remote engine:%s, timeout:%d ms.",
          listen_info_.c_str(), remote_engine.c_str(), timeout_in_millis);
  return SUCCESS;
}

Status ChannelMsgHandler::Disconnect(const std::string &remote_engine, int32_t timeout_in_millis) {
  std::string remote_ip;
  int32_t remote_port = -1;
  ADXL_CHK_STATUS_RET(ParseListenInfo(remote_engine, remote_ip, remote_port), "Failed to parse listen info");
  LLMEVENT("Start to disconnect, local engine:%s, remote engine:%s, timeout:%d ms.",
          listen_info_.c_str(), remote_engine.c_str(), timeout_in_millis);

  int32_t conn_fd = -1;
  LLM_MAKE_GUARD(close_fd, ([&conn_fd]() {
    if (conn_fd != -1) {
      llm::MsgHandlerPlugin::Disconnect(conn_fd);
    }
  }));

  auto channel = channel_manager_->GetChannel(ChannelType::kClient, remote_engine);
  ADXL_CHK_BOOL_RET_STATUS(channel != nullptr, NOT_CONNECTED,
                           "Failed to get channel, channel_id:%s", remote_engine.c_str());
  std::lock_guard<std::mutex> transfer_lock(channel->GetTransferMutex());
  channel->StopHeartbeat();
  // if connect failed, then release client and server auto release channel
  ADXL_CHK_LLM_RET(llm::MsgHandlerPlugin::Connect(remote_ip, static_cast<uint32_t>(remote_port),
                                                  conn_fd, timeout_in_millis, SUCCESS),
                   "Failed to connect remote addr %s:%d, timeout=%d ms.",
                   remote_ip.c_str(), remote_port, timeout_in_millis);
  Status send_status = FAILED;
  if (conn_fd > 0) {
    ChannelDisconnectInfo disconnect_info = {};
    disconnect_info.channel_id = listen_info_;
    send_status = SendMsg(conn_fd, ChannelMsgType::kDisconnect, disconnect_info);
  }

  ChannelDisconnectInfo local_disconnect_info = {};
  local_disconnect_info.channel_id = remote_engine;
  auto ret = DisconnectInfoProcess(ChannelType::kClient, local_disconnect_info);
  if (send_status == SUCCESS) {
    ChannelStatus status{};
    ADXL_CHK_STATUS_RET(RecvMsg(conn_fd, ChannelMsgType::kStatus, status),
                        "Failed to recv status msg");
    ADXL_CHK_STATUS_RET(status.error_code, "Failed to check peer process ret status, error code[%u], err msg[%s]",
                        status.error_code, status.error_message.c_str());
  }
  ADXL_CHK_STATUS_RET(ret, "Failed to disconnect, local engine:%s, remote engine:%s, timeout:%d ms.",
                      listen_info_.c_str(), remote_engine.c_str(), timeout_in_millis);
  LLMEVENT("Success to disconnect, local engine:%s, remote engine:%s, timeout:%d ms.",
          listen_info_.c_str(), remote_engine.c_str(), timeout_in_millis);
  return SUCCESS;
}
}  // namespace adxl
