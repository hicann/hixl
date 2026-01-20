/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "channel_msg_handler.h"
#include <algorithm>
#include <sstream>
#include "nlohmann/json.hpp"
#include "adxl/adxl_types.h"
#include "common/rank_table_generator.h"
#include "common/msg_handler_plugin.h"
#include "hccl/hccl_adapter.h"
#include "common/llm_utils.h"
#include "channel.h"
#include "common/llm_checker.h"
#include "common/llm_scope_guard.h"
#include "common/def_types.h"
#include "adxl_utils.h"
#include "control_msg_handler.h"

namespace adxl {

namespace {
constexpr int32_t kWaitRespTime = 20;
constexpr int32_t kCheckDisconnetPeriod = 10;
std::string GetDebugStr(rtDrvMemFabricHandle share_handle) {
  std::stringstream ss;
  for (auto &i : share_handle.share_info) {
    ss << i;
  }
  return ss.str();
}
}  // namespace
static inline void from_json(const nlohmann::json &j, AddrInfo &op_desc) {
  j.at("mem_type").get_to(op_desc.mem_type);
  j.at("start_addr").get_to(op_desc.start_addr);
  j.at("end_addr").get_to(op_desc.end_addr);
}

static inline void to_json(nlohmann::json &j, const AddrInfo &op_desc) {
  j = nlohmann::json{
      {"mem_type", op_desc.mem_type}, {"start_addr", op_desc.start_addr}, {"end_addr", op_desc.end_addr}};
}

static void from_json(const nlohmann::json &j, ShareHandleInfo &c) {
  j.at("va_addr").get_to(c.va_addr);
  j.at("len").get_to(c.len);
  const auto& share_array = j["share_handle"];
  for (size_t i = 0; i < share_array.size(); ++i) {
    c.share_handle.share_info[i] = share_array[i].get<uint8_t>();
  }
}

static void to_json(nlohmann::json &j, const ShareHandleInfo &c) {
  j = nlohmann::json{};
  j["va_addr"] = c.va_addr;
  j["len"] = c.len;
  auto share_array = nlohmann::json::array();
  for (size_t i = 0; i < sizeof(c.share_handle.share_info); ++i) {
    share_array.push_back(c.share_handle.share_info[i]);
  }
  j["share_handle"] = share_array;
}

static void from_json(const nlohmann::json &j, ChannelConnectInfo &c) {
  j.at("channel_id").get_to(c.channel_id);
  j.at("comm_res").get_to(c.comm_res);
  j.at("timeout").get_to(c.timeout);
  j.at("addrs").get_to(c.addrs);
  j.at("share_handles").get_to(c.share_handles);
}

static void to_json(nlohmann::json &j, const ChannelConnectInfo &c) {
  j = nlohmann::json{};
  j["channel_id"] = c.channel_id;
  j["comm_res"] = c.comm_res;
  j["timeout"] = c.timeout;
  j["addrs"] = c.addrs;
  j["share_handles"] = c.share_handles;
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

Status ChannelMsgHandler::Initialize(const std::map<AscendString, AscendString> &options, SegmentTable *segment_table,
                                     FabricMemTransferService *fabric_mem_transfer_service) {
  ADXL_CHECK_NOTNULL(channel_manager_);
  ADXL_CHK_ACL_RET(rtGetDevice(&device_id_));
  ADXL_CHK_STATUS_RET(ParseListenInfo(listen_info_, local_ip_, listen_port_), "Failed to parse listen info");
  ADXL_CHK_LLM_RET(llm::LocalCommResGenerator::Generate(local_ip_, device_id_, local_comm_res_),
                   "Failed to generate local comm res, local_ip:%s, device_id:%d",
                   local_ip_.c_str(), device_id_);
  llm::HcclAdapter::GetInstance().HcclCommConfigInit(&comm_config_);
  ADXL_CHK_STATUS_RET(ParseTrafficClass(options), "Failed to parse traffic class");
  ADXL_CHK_STATUS_RET(ParseServiceLevel(options), "Failed to parse service level");
  handler_plugin_.Initialize();
  if (listen_port_ > 0) {
    ADXL_CHK_STATUS_RET(StartDaemon(local_ip_, listen_port_), "Failed to start listen deamon, ip:%s, port:%u",
                        local_ip_.c_str(), listen_port_);
    LLMEVENT("start daemon success, listen on %s:%u", local_ip_.c_str(), listen_port_);
  }
  segment_table_ = segment_table;
  enable_use_fabric_mem_ = (fabric_mem_transfer_service != nullptr);
  fabric_mem_transfer_service_ = fabric_mem_transfer_service;
  if (user_config_channel_pool_) {
    ADXL_CHK_STATUS_RET(InitChannelPool(), "Failed to Init ChannelPool.");
  }
  return SUCCESS;
}

void ChannelMsgHandler::Finalize() {
  if (eviction_thread_.joinable()) {
    stop_eviction_ = true;
    evict_cv_.notify_all();
    eviction_thread_.join();
  }

  StopDaemon();
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto &it : handle_to_addr_) {
    auto handle = it.first;
    if (!enable_use_fabric_mem_) {
      (void)llm::HcclAdapter::GetInstance().HcclDeregisterGlobalMem(handle);
    }
  }
  handle_to_addr_.clear();
}

Status ChannelMsgHandler::ParseListenInfo(const std::string &listen_info,
                                          std::string &listen_ip, int32_t &listen_port) {
  std::vector<std::string> listen_infos;
  size_t left = listen_info.find('[');
  size_t right = listen_info.find(']');
  if (left != std::string::npos && right != std::string::npos && left < right) {
    listen_infos.emplace_back(listen_info.substr(left + 1, right - left - 1));
    size_t colon = listen_info.find(':', right);
    if (colon != std::string::npos) {
      listen_infos.emplace_back(listen_info.substr(colon + 1));
    }
  } else {
    listen_infos = llm::LLMUtils::Split(listen_info, ':');
  }
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
  if (enable_use_fabric_mem_) {
    ADXL_CHK_STATUS_RET(fabric_mem_transfer_service_->RegisterMem(mem, type, mem_handle), "Failed to register mem.");
  } else {
    HcclMem hccl_mem = {};
    hccl_mem.type = type == MEM_DEVICE ? HCCL_MEM_TYPE_DEVICE : HCCL_MEM_TYPE_HOST;
    hccl_mem.addr = reinterpret_cast<void *>(mem.addr);
    hccl_mem.size = mem.len;
    ADXL_CHK_HCCL_RET(llm::HcclAdapter::GetInstance().HcclRegisterGlobalMem(&hccl_mem, &mem_handle));
  }
  LLMLOGI("Add local mem range start:%lu, end:%lu, type:%d.", mem.addr, mem.addr + mem.len, type);
  // keep same lock order with DeregisterMem
  std::lock_guard<std::mutex> lock(mutex_);
  ADXL_CHK_BOOL_RET_STATUS(segment_table_ != nullptr, FAILED, "Segment table is null.");
  segment_table_->AddRange(listen_info_, mem.addr, mem.addr + mem.len, type);
  handle_to_addr_[mem_handle] = AddrInfo{mem.addr, mem.addr + mem.len, type};
  return SUCCESS;
}

Status ChannelMsgHandler::DeregisterMem(MemHandle mem_handle) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = handle_to_addr_.find(mem_handle);
  if (it == handle_to_addr_.end()) {
    LLMLOGW("handle:%p is not registered.", mem_handle);
    return SUCCESS; 
  }
  auto &addr_info = it->second;
  ADXL_CHK_BOOL_RET_STATUS(segment_table_ != nullptr, FAILED, "Segment table is null.");
  segment_table_->RemoveRange(listen_info_, addr_info.start_addr, addr_info.end_addr, addr_info.mem_type);
  if (enable_use_fabric_mem_) {
    ADXL_CHK_STATUS_RET(fabric_mem_transfer_service_->DeregisterMem(mem_handle), "Failed to Deregister mem.");
  } else {
    ADXL_CHK_HCCL_RET(llm::HcclAdapter::GetInstance().HcclDeregisterGlobalMem(mem_handle));
  }
  handle_to_addr_.erase(it);
  return SUCCESS;
}

Status ChannelMsgHandler::StartDaemon(const std::string &ip, uint32_t listen_port) {
  handler_plugin_.RegisterConnectedProcess([this](int32_t fd, bool &keep_fd) {
    (void) ConnectedProcess(fd, keep_fd);
  });
  ADXL_CHK_LLM_RET(handler_plugin_.StartDaemon(ip, listen_port), "Failed to start daemon.");
  return SUCCESS;
}

Status ChannelMsgHandler::StopDaemon() {
  handler_plugin_.Finalize();
  return SUCCESS;
}

Status ChannelMsgHandler::CreateChannel(const ChannelInfo &channel_info, bool is_client,
                                        const ChannelConnectInfo &peer_channel_info) {
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
  ADXL_CHK_STATUS_RET(channel_manager_->CreateChannel(channel_info, channel, enable_use_fabric_mem_));
  // add remote addr to segment table
  for (const auto &remote_addr : peer_channel_info.addrs) {
    LLMLOGI("Add remote mem range start:%lu, end:%lu, type:%d.", remote_addr.start_addr, remote_addr.end_addr,
            remote_addr.mem_type);
    ADXL_CHK_BOOL_RET_STATUS(segment_table_ != nullptr, FAILED, "Segment table is null.");
    segment_table_->AddRange(channel->GetChannelId(), remote_addr.start_addr, remote_addr.end_addr,
                             remote_addr.mem_type);
  }
  if (enable_use_fabric_mem_) {
    ADXL_CHK_STATUS_RET(fabric_mem_transfer_service_->ImportMem(channel, peer_channel_info.share_handles), "Failed to import mem.");
  }
  LLMLOGI("Success to create channel, channel_id:%s, local_rank:%u, peer_rank:%u, is_client:%d",
         channel_info.channel_id.c_str(), channel_info.local_rank_id,
         channel_info.peer_rank_id, static_cast<int32_t>(is_client));
  return SUCCESS;
}


Status ChannelMsgHandler::ConnectInfoProcess(const ChannelConnectInfo &peer_channel_info,
                                             int32_t timeout, bool is_client) {
  if (user_config_channel_pool_) {
    auto start_time = std::chrono::steady_clock::now();
    NotifyEviction();
    while(GetTotalChannelCount() >= max_channel_) {
      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time).count();
      if (elapsed >= timeout) {
        LLMLOGE(RESOURCE_EXHAUSTED, 
                "Failed to Connect %s after %d ms, channel resourse exhausted, adjust channel pool config to avoid", 
                peer_channel_info.channel_id.c_str(), timeout);
        return RESOURCE_EXHAUSTED;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(kCheckDisconnetPeriod));
    }
  }
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
    for (const auto &addr_info : handle_to_addr_) {
      channel_info.registered_mems[addr_info.first] = reinterpret_cast<void *>(addr_info.second.start_addr);
    }
  }
  constexpr uint32_t kTimeInSec = 1000;
  auto left_time = timeout % kTimeInSec == 0 ? 0 : 1;
  channel_info.timeout_sec = timeout / kTimeInSec + left_time;
  ADXL_CHK_STATUS_RET(CreateChannel(channel_info, is_client, peer_channel_info), "Failed to create channel");
  return SUCCESS;
}

Status ChannelMsgHandler::ProcessConnectRequest(int32_t fd, const std::vector<char> &msg, bool &keep_fd) {
  // deserialize peer connect info first.
  ChannelConnectInfo peer_connect_info{};
  ADXL_CHK_STATUS_RET(ChannelMsgHandler::Deserialize(msg, peer_connect_info), "Failed to deserialize connect msg");
  ChannelConnectInfo channel_connect_info = {};
  channel_connect_info.channel_id = listen_info_;
  channel_connect_info.comm_res = local_comm_res_;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto &addr_info : handle_to_addr_) {
      channel_connect_info.addrs.emplace_back(addr_info.second);
    }
  }
  if (enable_use_fabric_mem_) {
    channel_connect_info.share_handles = fabric_mem_transfer_service_->GetShareHandles();
    for (const auto &share_handle : channel_connect_info.share_handles) {
      LLMLOGD("Share handle: va_addr:%lu, len:%lu, share_handle:%s", share_handle.va_addr, share_handle.len,
              GetDebugStr(share_handle.share_handle).c_str());
    }
  }
  ADXL_CHK_STATUS_RET(SendMsg(fd, ChannelMsgType::kConnect, channel_connect_info),
                      "Failed to send connect msg");

  LLMLOGI("Start to process connect info, local engine:%s, remote engine:%s, timeout:%d ms.", listen_info_.c_str(),
          peer_connect_info.channel_id.c_str(), peer_connect_info.timeout);
  peer_connect_info.comm_name = peer_connect_info.channel_id + "_" + listen_info_;
  auto ret = ConnectInfoProcess(peer_connect_info, peer_connect_info.timeout, false);
  if (ret == SUCCESS) {
    LLMEVENT("Success to process connect info, local engine:%s, remote engine:%s, timeout:%d ms.", listen_info_.c_str(),
             peer_connect_info.channel_id.c_str(), peer_connect_info.timeout);
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
  if (enable_use_fabric_mem_) {
    fabric_mem_transfer_service_->RemoveChannel(peer_disconnect_info.channel_id);
  }
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
  ADXL_CHK_BOOL_RET_STATUS(channel == nullptr, ALREADY_CONNECTED, "remote_engine:%s is already connected.",
                           remote_engine.c_str());
  const auto start = std::chrono::steady_clock::now();
  ADXL_CHK_STATUS_RET(DoConnect(remote_engine, timeout_in_millis),
                      "Failed to connect, local engine:%s, remote engine:%s, timeout:%d ms.", listen_info_.c_str(),
                      remote_engine.c_str(), timeout_in_millis);
  const auto end = std::chrono::steady_clock::now();
  const auto cost = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
  LLMEVENT("Connect success, local engine:%s, remote engine:%s, time cost:%lu us.", listen_info_.c_str(),
           remote_engine.c_str(), cost);
  return SUCCESS;
}

Status ChannelMsgHandler::DoConnect(const std::string &remote_engine, int32_t timeout_in_millis) {
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
  for (const auto &share_handle : peer_connect_info.share_handles) {
    LLMLOGD("Peer share handle: va_addr:%lu, len:%lu, share_handle:%s", share_handle.va_addr, share_handle.len,
            GetDebugStr(share_handle.share_handle).c_str());
  }
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
  auto channel = channel_manager_->GetChannel(ChannelType::kClient, remote_engine);
  ADXL_CHK_BOOL_RET_STATUS(channel != nullptr, FAILED,
                           "Faield to get channel, local engine:%s, remote engine:%s, timeout:%d ms.",
                           listen_info_.c_str(), remote_engine.c_str(), timeout_in_millis);
  ADXL_CHK_STATUS_RET(channel->SetSocketNonBlocking(conn_fd), "Failed to start heartbeat, remote_engine:%s.",
                      remote_engine.c_str());
  ADXL_CHK_STATUS_RET(channel_manager_->AddSocketToEpoll(conn_fd, channel),
                      "Failed to add fd to epoll, local engine:%s, remote engine:%s.",
                      listen_info_.c_str(), peer_connect_info.channel_id.c_str());
  LLM_DISMISS_GUARD(close_fd);
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
  channel->StopHeartbeat();
  // if connect failed, then release client and server auto release channel
  ADXL_CHK_STATUS(llm::MsgHandlerPlugin::Connect(remote_ip, static_cast<uint32_t>(remote_port),
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

Status ChannelMsgHandler::InitChannelPool() {
  LLMLOGI("Waterline config: max_channel=%d, high_mark=%d, low_mark=%d",
          max_channel_, high_waterline_, low_waterline_);
  ADXL_CHK_STATUS_RET(StartEvictionThread(), "Failed to start eviction thread");
  ADXL_CHK_STATUS_RET(SetupChannelManagerCallbacks(), "Failed to setup channel manager callbacks");
  return SUCCESS;
}

Status ChannelMsgHandler::StartEvictionThread() {
  if (high_waterline_ > 0 && low_waterline_ > 0) {
    stop_eviction_ = false;
    eviction_thread_ = std::thread(
      [this]() { 
      EvictionLoop(); 
    });
    LLMLOGI("Eviction thread started with: max=%d, high_mark=%d, low_mark=%d", 
        max_channel_, high_waterline_, low_waterline_);
  }
  return SUCCESS;
}

Status ChannelMsgHandler::SetupChannelManagerCallbacks() {
  channel_manager_->SetDisconnectCallback(
    [this](const std::string& channel_id, int32_t timeout_ms) {
    EvictItem item;
    item.channel_id = channel_id;
    auto client_channel = channel_manager_->GetChannel(ChannelType::kClient, channel_id);
    if (client_channel != nullptr) {
      item.channel_type = ChannelType::kClient;
    } else {
      LLMLOGI("Channel %s not found for disconnect", channel_id.c_str());
      return NOT_CONNECTED;
    }
    item.timeout_ms = timeout_ms;
    {
      std::lock_guard<std::mutex> lock(evict_mutex_);
      evict_queue_.push(item);
    }
    evict_cv_.notify_one();
    return SUCCESS;
  });

  channel_manager_->SetDisconnectResponseCallback(
    [this](const RequestDisconnectResp& resp) {
    std::lock_guard<std::mutex> lock(pending_req_mutex_);
    auto it = pending_disconnect_requests_.find(resp.req_id);
    if (it != pending_disconnect_requests_.end()) {
      it->second->resp = resp;
      it->second->received = true;
      it->second->cv.notify_one();
    }
  });
  return SUCCESS;
}

int32_t ChannelMsgHandler::GetTotalChannelCount() const {
  auto client_channels = channel_manager_->GetAllClientChannel();
  auto server_channels = channel_manager_->GetAllServerChannel();
  return static_cast<int32_t>(client_channels.size() + server_channels.size());
}

bool ChannelMsgHandler::ShouldTriggerEviction() const {
  bool should_evict = GetTotalChannelCount() >= high_waterline_;
  if (should_evict) {
    LLMLOGI("Eviction triggered: current_channel_count(%d) >= high_mark(%d)", 
            GetTotalChannelCount(), high_waterline_);
  }
  return should_evict;
}

Status ChannelMsgHandler::NotifyEviction() {
  if (!ShouldTriggerEviction()) {
    return SUCCESS;
  }

  int32_t current_count = GetTotalChannelCount();
  int32_t need_expire = current_count - low_waterline_;
  if (need_expire <= 0) {
    LLMLOGI("No need to evict channels: current_channel_count(%d) <= low_mark(%d)", 
            current_count, low_waterline_);
    return SUCCESS;
  }

  std::vector<EvictItem> candidates = SelectEvictionCandidates(need_expire);
  LLMLOGI("Select %zu eviction candidates from %d total channels", 
          candidates.size(), current_count);
  std::lock_guard<std::mutex> lock(evict_mutex_);
  for (const auto& item : candidates) {
    evict_queue_.push(item);
  }
  evict_cv_.notify_one();
  return SUCCESS;
}

std::vector<EvictItem> ChannelMsgHandler::SelectEvictionCandidates(int32_t need_expire) {
  auto client_channels = channel_manager_->GetAllClientChannel();
  auto server_channels = channel_manager_->GetAllServerChannel();
  
  LLMLOGI("SelectEvictionCandidates: need_expire=%d, client_channels=%zu, server_channels=%zu", 
          need_expire, client_channels.size(), server_channels.size());
  
  std::sort(client_channels.begin(), client_channels.end(), 
      [](const ChannelPtr& a, const ChannelPtr& b) {
        if (a->GetHasTransferred() != b->GetHasTransferred()) {
          return !a->GetHasTransferred(); 
        }
        return false;
      });
  std::vector<EvictItem> target_items;
  target_items.reserve(need_expire);
  auto client_it = client_channels.begin();
  auto server_it = server_channels.begin();
  auto client_end = client_channels.end();
  auto server_end = server_channels.end();
  int32_t diff = std::abs(static_cast<int32_t>(client_channels.size() - server_channels.size()));
  int32_t pick_extra = std::min(diff, need_expire);
  bool pick_client_first = (client_channels.size() > server_channels.size());

  for(int32_t i = 0; i < pick_extra && need_expire > 0; i++) {
    if (pick_client_first && client_it != client_end) {
      target_items.push_back(EvictItem{(*client_it)->GetChannelId(), ChannelType::kClient});
      (*client_it)->SetDisconnecting(true);
      ++client_it;
    } else if (server_it != server_end) {
      target_items.push_back(EvictItem{(*server_it)->GetChannelId(), ChannelType::kServer});
      (*server_it)->SetDisconnecting(true);
      ++server_it;
    }
    need_expire--;
  }
  bool pick_client = true;
  while(need_expire > 0 && (client_it != client_end || server_it != server_end)) {
    if (pick_client && client_it != client_end) {
      target_items.push_back(EvictItem{(*client_it)->GetChannelId(), ChannelType::kClient});
      (*client_it)->SetDisconnecting(true);
      ++client_it;
    } else if (!pick_client && server_it != server_end) {
      target_items.push_back(EvictItem{(*server_it)->GetChannelId(), ChannelType::kServer});
      (*server_it)->SetDisconnecting(true);
      ++server_it;
    }
    need_expire--;
    pick_client = !pick_client;
  }
  
  return target_items;
}

void ChannelMsgHandler::EvictionLoop() {
  while (true) {
    std::unique_lock<std::mutex> lock(evict_mutex_);
    evict_cv_.wait(
      lock, 
      [this] { 
      return stop_eviction_ || !evict_queue_.empty(); 
    });
    if (stop_eviction_) {
      break;
    }
    
    while (!evict_queue_.empty()) {
      EvictItem item = evict_queue_.front();
      evict_queue_.pop();
      lock.unlock();
      ProcessEviction(item);
      lock.lock();
    }
    ResetAllTransferFlags();
  }
}

Status ChannelMsgHandler::ProcessEviction(const EvictItem& item) {
  auto channel = channel_manager_->GetChannel(item.channel_type, item.channel_id);
  if (channel == nullptr) {
    LLMLOGI("Skip eviction: channel %s (type:%d) not found", 
            item.channel_id.c_str(), static_cast<int>(item.channel_type));
    return SUCCESS;
  }
  
  if (channel->GetTransferCount() > 0) {
    LLMLOGI("Skip eviction: channel %s has unfinished transfers", item.channel_id.c_str());
    return SUCCESS;
  }
  if (item.channel_type == ChannelType::kServer) {
    return ProcessServerEviction(item.channel_id, channel);
  } else {
    return ProcessClientEviction(item.channel_id, item.timeout_ms);
  }
}

Status ChannelMsgHandler::ProcessServerEviction(const std::string& channel_id, ChannelPtr channel) {
  int32_t fd = channel->GetFd();
  if (fd < 0) {
    LLMLOGW("Channel %s has invalid fd, cannot send request disconnect", channel_id.c_str());
    return SUCCESS;
  }
  uint64_t req_id = next_req_id_.fetch_add(1ULL, std::memory_order_acq_rel);
  auto pending_req = std::make_shared<PendingDisconnectRequest>();
  {
    std::lock_guard<std::mutex> lock(pending_req_mutex_);
    pending_disconnect_requests_[req_id] = pending_req;
  }
  RequestDisconnectMsg req_msg;
  req_msg.channel_id = listen_info_;
  req_msg.req_id = req_id;
  Status ret = channel->SendControlMsg([&req_msg](int32_t fd) {
    return ControlMsgHandler::SendMsg(fd, ControlMsgType::kRequestDisconnect, req_msg, req_msg.timeout);
  });
  if (ret != SUCCESS) {
    LLMLOGW("Failed to send request disconnect for channel: %s, ret=%d", channel_id.c_str(), ret);
    std::lock_guard<std::mutex> lock(pending_req_mutex_);
    pending_disconnect_requests_.erase(req_id);
    return ret;
  }
  LLMLOGI("Sent request disconnect to client for channel: %s, req_id=%lu", channel_id.c_str(), req_id);
  std::unique_lock<std::mutex> lock(pending_req_mutex_);
  bool received = pending_req->cv.wait_for(lock, std::chrono::milliseconds(kWaitRespTime), [&pending_req] {
    return pending_req->received;
  });
  if (!received) {
    pending_disconnect_requests_.erase(req_id);
    return SUCCESS;
  } else {
    RequestDisconnectResp resp = pending_req->resp;
    pending_disconnect_requests_.erase(req_id);
    lock.unlock();
    LLMLOGI("Client refused or failed to disconnect channel %s, error_code=%u, error_message=%s", 
      channel_id.c_str(), resp.error_code, resp.error_message.c_str());
    return SUCCESS;
  }
}

Status ChannelMsgHandler::ProcessClientEviction(const std::string& channel_id, int32_t timeout_ms) {
  Status ret = Disconnect(channel_id, timeout_ms);
  if (ret == SUCCESS) {
    LLMLOGI("Evicted client channel: %s", channel_id.c_str());
  } else {
    LLMLOGE(ret, "Failed to evict client channel: %s, ret=%d", channel_id.c_str(), ret);
  }
  return ret;
}

Status ChannelMsgHandler::ResetAllTransferFlags() {
  auto client_channels = channel_manager_->GetAllClientChannel();
  auto server_channels = channel_manager_->GetAllServerChannel();
  for (auto& channel : client_channels) {
    if (channel->GetTransferCount() == 0) {
      channel->SetHasTransferred(false);
    }
  }
  for (auto& channel : server_channels) {
    if (channel->GetTransferCount() == 0) {
      channel->SetHasTransferred(false);
    }
  }
  LLMLOGI("Reset transfer flags, client=%zu, server=%zu", 
    client_channels.size(), server_channels.size());
  return SUCCESS;
}
}  // namespace adxl
