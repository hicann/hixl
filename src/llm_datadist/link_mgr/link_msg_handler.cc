/**
 * Copyright (c) 2025-2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "link_msg_handler.h"
#include "nlohmann/json.hpp"
#include "llm_datadist/llm_datadist.h"
#include "common/def_types.h"
#include "common/rank_table_generator.h"
#include "common/hixl_utils.h"
#include "common/tls/tls_manager.h"
#include "common/llm_utils.h"
#include "common/llm_checker.h"
#include "common/llm_scope_guard.h"
#include "cache_mgr/comm_mem_manager.h"

namespace llm {
namespace {
constexpr uint32_t kMaxLinkNum = 512;
constexpr const char kOptionRdmaTrafficClass[] = "llm.RdmaTrafficClass";
constexpr const char kOptionRdmaServiceLevel[] = "llm.RdmaServiceLevel";
constexpr uint64_t kDefaultReqBufferSize = 112U * 1024U;
constexpr uint64_t kDefaultRespBufferSize = 16U * 1024U;

ge::Status ParseControlTlsConfig(const std::string &text, hixl::TlsConfig &config) {
  try {
    const auto status = hixl::ParseTlsConfig(nlohmann::json::parse(text), config);
    LLM_CHK_BOOL_RET_STATUS(status == hixl::SUCCESS, ge::LLM_PARAM_INVALID,
                            "Failed to parse TLS configuration, hixl_status:%u", status);
  } catch (const nlohmann::json::parse_error &error) {
    LLM_CHK_BOOL_RET_STATUS(false, ge::LLM_PARAM_INVALID,
                            "Failed to parse global resource configuration, parse_error_id:%d, byte:%zu", error.id,
                            error.byte);
  } catch (const nlohmann::json::exception &error) {
    LLM_CHK_BOOL_RET_STATUS(false, ge::LLM_PARAM_INVALID, "Failed to parse global resource configuration, error_id:%d",
                            error.id);
  }
  return ge::SUCCESS;
}

bool IsVersionOnlyCommRes(const std::string &comm_res) {
  if (comm_res.empty()) {
    return false;
  }
  try {
    auto json = nlohmann::json::parse(comm_res);
    if (!json.contains("version") || json.size() != 1) {
      return false;
    }
    const auto &ver = json["version"];
    return ver == "1.0" || ver == "1.2";
  } catch (const nlohmann::json::exception &e) {
    LLMLOGW("Failed to parse LocalCommRes as json: %s", e.what());
    return false;
  }
}

// A destroy request (force_link reconnect or kDisconnect) may only remove the entity of the cluster id it
// declares when the requester presents the same TLS peer certificate that created that entity, so a foreign
// peer cannot tear down someone else's link by claiming its cluster id. Entities created without peer TLS
// keep an empty fingerprint and are rejected under TLS peer verification instead of being destroyed blindly.
ge::Status ValidateEntityOwnerByCert(CommEntityManager *mgr, uint64_t claimed_cluster_id,
                                     const std::string &requester_fingerprint) {
  auto entity = mgr->GetEntityByRemoteClusterId(claimed_cluster_id);
  if (entity == nullptr) {
    return ge::SUCCESS;
  }
  const std::string &recorded_fingerprint = entity->GetPeerCertFingerprint();
  if (recorded_fingerprint.empty()) {
    LLMLOGW("Reject entity destroy: claimed cluster id:%lu was not created with a peer certificate.",
            claimed_cluster_id);
    return ge::LLM_PARAM_INVALID;
  }
  if (recorded_fingerprint != requester_fingerprint) {
    LLMLOGW(
        "Reject entity destroy: claimed cluster id:%lu certificate fingerprint mismatch, recorded:%s, "
        "requester:%s.",
        claimed_cluster_id, recorded_fingerprint.c_str(), requester_fingerprint.c_str());
    return ge::LLM_PARAM_INVALID;
  }
  return ge::SUCCESS;
}
}  // namespace

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

template <typename T>
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

template <typename T>
ge::Status LinkMsgHandler::Deserialize(const std::vector<char> &msg_str, T &msg) {
  try {
    auto j = nlohmann::json::parse(msg_str.data(), msg_str.data() + msg_str.size());
    msg = j.get<T>();
  } catch (const nlohmann::json::exception &e) {
    LLMLOGE(ge::LLM_PARAM_INVALID, "Failed to load msg, exception:%s", e.what());
    return ge::LLM_PARAM_INVALID;
  }
  return ge::SUCCESS;
}

LinkMsgHandler::~LinkMsgHandler() {
  Finalize();
}

ge::Status LinkMsgHandler::Initialize(const std::map<ge::AscendString, ge::AscendString> &options) {
  LLM_CHECK_NOTNULL(comm_entity_manager_);
  LLM_CHECK_NOTNULL(cache_manager_);
  LLM_CHK_STATUS_RET(LLMUtils::ParseFlag(kLlmOptionEnableRemoteCacheAccessible, options, remote_cache_accessible_),
                     "Failed to parse option %s", kLlmOptionEnableRemoteCacheAccessible);
  const auto &ip_iter = options.find(kLlmOptionListenIp);
  if (ip_iter != options.cend()) {
    local_ip_ = ip_iter->second.GetString();
  }
  const auto &it = options.find(llm_datadist::OPTION_LOCAL_COMM_RES);
  if (it != options.cend()) {
    local_comm_res_ = it->second.GetString();
  }
  if (IsVersionOnlyCommRes(local_comm_res_)) {
    LLMEVENT("LocalCommRes only contains version, will auto-generate.");
    local_comm_res_.clear();
  }
  if (local_comm_res_.empty() && (!local_ip_.empty())) {
    LLM_CHK_STATUS_RET(LocalCommResGenerator::Generate(local_ip_, device_id_, local_comm_res_),
                       "Failed to generate local comm res, local_ip:%s, device_id:%d", local_ip_.c_str(), device_id_);
  }
  CommAdapter::GetInstance().DlHcclCommConfigInit(&comm_config_);
  const auto &traffic_it = options.find(kOptionRdmaTrafficClass);
  if (traffic_it != options.cend()) {
    uint32_t traffic_class = 0U;
    LLM_CHK_STATUS_RET(LLMUtils::ToNumber(traffic_it->second.GetString(), traffic_class),
                       "llm.RdmaTrafficClass is invalid, value = %s", traffic_it->second.GetString());
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
  LLM_ASSERT_RT_OK(aclrtGetCurrentContext(&aclrt_context_));
  return InitializeControlListener(options);
}

ge::Status LinkMsgHandler::InitializeControlListener(const std::map<ge::AscendString, ge::AscendString> &options) {
  LLM_CHK_BOOL_RET_STATUS(!tls_initialized_, ge::FAILED, "TLS is already initialized");
  tls_config_ = hixl::TlsConfig{};
  LLM_MAKE_GUARD(wipe_tls_key, ([this]() { tls_config_.WipeKey(); }));
  const auto config_it = options.find(llm_datadist::OPTION_GLOBAL_RESOURCE_CONFIG);
  if (config_it != options.end()) {
    LLM_CHK_STATUS_RET(ParseControlTlsConfig(config_it->second.GetString(), tls_config_),
                       "Failed to parse TLS configuration");
  }
  LLM_CHK_HIXL_RET(hixl::TlsManager::GetInstance().Initialize(tls_config_), "Failed to initialize TLS");
  LLM_DISMISSABLE_GUARD(tls_guard, ([this]() {
                          handler_plugin_.Finalize();
                          hixl::TlsManager::GetInstance().Finalize();
                        }));
  LLM_CHK_STATUS_RET(handler_plugin_.Initialize(), "Failed to initialize control listener");
  LLM_DISMISS_GUARD(tls_guard);
  tls_initialized_ = true;
  return ge::SUCCESS;
}

void LinkMsgHandler::Finalize() {
  handler_plugin_.Finalize();
  if (tls_initialized_) {
    tls_initialized_ = false;
    hixl::TlsManager::GetInstance().Finalize();
  }
  comm_entity_manager_ = nullptr;
  cache_manager_ = nullptr;
}

ge::Status LinkMsgHandler::StartDaemon(const std::string &ip, uint32_t listen_port) {
  handler_plugin_.RegisterConnectedProcess([this](const hixl::CtrlMsgPluginPtr &connection, bool &keep_connection) {
    (void)ConnectedProcess(connection, keep_connection);
  });
  return handler_plugin_.StartDaemon(ip, listen_port);
}

ge::Status LinkMsgHandler::StopDaemon() {
  handler_plugin_.Finalize();
  return ge::SUCCESS;
}

template <typename T>
ge::Status LinkMsgHandler::SendMsg(const hixl::CtrlMsgPluginPtr &connection, LinkMsgType msg_type, const T &msg) {
  std::string msg_str;
  LLM_CHK_STATUS_RET(LinkMsgHandler::Serialize(msg, msg_str), "Failed to serialize msg");
  LLM_CHK_STATUS_RET(MsgHandlerPlugin::SendMsg(connection, static_cast<int32_t>(msg_type), msg_str),
                     "Failed to send msg");
  return ge::SUCCESS;
}

template <typename T>
ge::Status LinkMsgHandler::RecvMsg(const hixl::CtrlMsgPluginPtr &connection, LinkMsgType msg_type, T &msg,
                                   int32_t timeout_ms) {
  std::vector<char> msg_str;
  int32_t type = 0;
  LLM_CHK_STATUS_RET(MsgHandlerPlugin::RecvMsg(connection, type, msg_str, timeout_ms), "Failed to recv msg");
  LLM_CHK_BOOL_RET_STATUS(msg_type == static_cast<LinkMsgType>(type), ge::FAILED,
                          "Failed to check recv msg type:%d, expect type:%d", type, static_cast<int32_t>(msg_type));
  LLM_CHK_STATUS_RET(LinkMsgHandler::Deserialize(msg_str, msg), "Failed to deserialize msg");
  return ge::SUCCESS;
}

ge::Status LinkMsgHandler::GetPeerCertFingerprint(const hixl::CtrlMsgPluginPtr &connection,
                                                  std::string &fingerprint) const {
  fingerprint.clear();
  if (!tls_config_.Enabled()) {
    return ge::SUCCESS;
  }
  const auto ret = connection->GetPeerCertificateFingerprint(fingerprint);
  if (ret != hixl::SUCCESS || fingerprint.empty()) {
    LLMLOGE(ge::LLM_PARAM_INVALID, "Failed to get peer certificate fingerprint, ret:%u, connection:%s.",
            static_cast<uint32_t>(ret), connection->Desc().c_str());
    return ge::LLM_PARAM_INVALID;
  }
  return ge::SUCCESS;
}

ge::Status LinkMsgHandler::ProcessConnectRequest(const hixl::CtrlMsgPluginPtr &connection,
                                                 const std::vector<char> &msg) {
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
  LLM_CHK_STATUS_RET(SendMsg(connection, LinkMsgType::kConnect, exchange_info), "Failed to send connect msg");

  auto ret = ge::FAILED;
  LLM_MAKE_GUARD(send_status, ([connection, &ret]() {
                   LLMLinkStatus status{};
                   status.error_code = ret;
                   LinkMsgHandler::SendMsg(connection, LinkMsgType::kStatus, status);
                 }));

  LLMExchangeInfo peer_exchange_info{};
  LLM_CHK_STATUS_RET(LinkMsgHandler::Deserialize(msg, peer_exchange_info), "Failed to deserialize connect msg");
  std::string requester_fingerprint;
  ret = GetPeerCertFingerprint(connection, requester_fingerprint);
  if (ret != ge::SUCCESS) {
    return ret;
  }
  if (peer_exchange_info.force_link && !requester_fingerprint.empty()) {
    // force_link destroys the entity of the claimed cluster id; bind the claim to the peer certificate that
    // created it so a foreign peer cannot tear down someone else's link. Assign ret before returning: the
    // guard reports it.
    ret = ValidateEntityOwnerByCert(comm_entity_manager_, peer_exchange_info.cluster_id, requester_fingerprint);
    LLM_CHK_STATUS(ret,
                   "Failed to validate entity owner, local cluster id:%lu, claimed cluster id:%lu, "
                   "requester fingerprint:%s.",
                   cluster_id_, peer_exchange_info.cluster_id, requester_fingerprint.c_str());
    if (ret != ge::SUCCESS) {
      return ret;
    }
  }
  LLMLOGI("Start to process link cluster, local cluster_id:%lu, remote cluster_id:%lu, timeout:%d ms.", cluster_id_,
          peer_exchange_info.cluster_id, peer_exchange_info.timeout);
  peer_exchange_info.comm_name =
      "llm_datadist_" + std::to_string(peer_exchange_info.cluster_id) + "_" + std::to_string(cluster_id_);
  ret = ExchangeInfoProcess(peer_exchange_info, peer_exchange_info.timeout, peer_exchange_info.force_link,
                            requester_fingerprint, mem_info_ptr);
  if (ret == ge::SUCCESS) {
    LLMLOGI("Success to process link cluster, local cluster_id:%lu, remote cluster_id:%lu.", cluster_id_,
            peer_exchange_info.cluster_id);
  }
  LLM_CHK_STATUS(ret, "Failed to process peer exchange info, timeout:%d ms", peer_exchange_info.timeout);
  return ret;
}

ge::Status LinkMsgHandler::ProcessDisconnectRequest(const hixl::CtrlMsgPluginPtr &connection,
                                                    const std::vector<char> &msg) const {
  auto ret = ge::FAILED;
  LLM_MAKE_GUARD(send_status, ([connection, &ret]() {
                   LLMLinkStatus status{};
                   status.error_code = ret;
                   LinkMsgHandler::SendMsg(connection, LinkMsgType::kStatus, status);
                 }));

  LLMDisconnectInfo peer_disconnect_info{};
  LLM_CHK_STATUS_RET(LinkMsgHandler::Deserialize(msg, peer_disconnect_info), "Failed to deserialize disconnect msg");
  LLMLOGI("Start to process disconnect cluster, local cluster_id:%lu, remote cluster_id:%lu.", cluster_id_,
          peer_disconnect_info.cluster_id);
  std::string requester_fingerprint;
  ret = GetPeerCertFingerprint(connection, requester_fingerprint);
  if (ret != ge::SUCCESS) {
    return ret;
  }
  if (!requester_fingerprint.empty()) {
    // A disconnect request only declares a cluster id; bind the claim to the peer certificate that created
    // the entity so a foreign peer cannot tear down someone else's link. Assign ret before returning: the
    // guard reports it.
    ret = ValidateEntityOwnerByCert(comm_entity_manager_, peer_disconnect_info.cluster_id, requester_fingerprint);
    LLM_CHK_STATUS(ret,
                   "Failed to validate entity owner, local cluster id:%lu, claimed cluster id:%lu, "
                   "requester fingerprint:%s.",
                   cluster_id_, peer_disconnect_info.cluster_id, requester_fingerprint.c_str());
    if (ret != ge::SUCCESS) {
      return ret;
    }
  }
  ret = DisconnectInfoProcess(peer_disconnect_info);
  if (ret == ge::SUCCESS) {
    LLMLOGI("Success to process disconnect cluster, local cluster_id:%lu, remote cluster_id:%lu.", cluster_id_,
            peer_disconnect_info.cluster_id);
  }
  LLM_CHK_STATUS(ret, "Failed to process disconnect info, local cluster_id:%lu, remote cluster_id:%lu", cluster_id_,
                 peer_disconnect_info.cluster_id);
  return ret;
}

ge::Status LinkMsgHandler::ConnectedProcess(const hixl::CtrlMsgPluginPtr &connection, bool &keep_connection) {
  keep_connection = false;
  int32_t msg_type = 0;
  std::vector<char> msg;
  LLM_CHK_STATUS_RET(MsgHandlerPlugin::RecvMsg(connection, msg_type, msg, kServerReadTimeoutMs), "Failed to recv msg");
  LLM_CHK_BOOL_RET_STATUS(static_cast<LinkMsgType>(msg_type) == LinkMsgType::kConnect ||
                              static_cast<LinkMsgType>(msg_type) == LinkMsgType::kDisconnect,
                          ge::LLM_PARAM_INVALID, "Failed to check msg type:%d", msg_type);

  if (static_cast<LinkMsgType>(msg_type) == LinkMsgType::kConnect) {
    LLM_CHK_STATUS_RET(ProcessConnectRequest(connection, msg), "Failed to process connect request");
  } else {
    LLM_CHK_STATUS_RET(ProcessDisconnectRequest(connection, msg), "Failed to process disconnect request");
  }
  return ge::SUCCESS;
}

ge::Status LinkMsgHandler::SetEntityMemInfo(const LLMExchangeInfo &peer_exchange_info, EntityPtr entity,
                                            EntityMemInfoPtr &mem_info_ptr) const {
  // prepare mem
  entity->SetEntityMemInfo(mem_info_ptr);
  auto &remote_mems = entity->GetRemoteMems();
  CommMem remote_mem{};
  remote_mem.type = CommMemType::COMM_MEM_TYPE_DEVICE;
  remote_mem.addr = ValueToPtr(peer_exchange_info.cache_table_addr);
  remote_mem.size = peer_exchange_info.cache_table_size;
  remote_mems.emplace_back(remote_mem);
  remote_mem.type = CommMemType::COMM_MEM_TYPE_HOST;
  remote_mem.addr = ValueToPtr(peer_exchange_info.req_addr);
  remote_mem.size = peer_exchange_info.req_size;
  remote_mems.emplace_back(remote_mem);
  remote_mem.type = CommMemType::COMM_MEM_TYPE_HOST;
  remote_mem.addr = ValueToPtr(peer_exchange_info.resp_addr);
  remote_mem.size = peer_exchange_info.resp_size;
  remote_mems.emplace_back(remote_mem);
  LLM_CHK_STATUS_RET(entity->SetInfo(), "Failed to set entity mem info");
  entity->MarkEntityIdle();
  return ge::SUCCESS;
}

ge::Status LinkMsgHandler::GenerateRankInfo(const std::string &peer_comm_res, std::string &rank_table,
                                            int32_t &local_rank_id, int32_t &peer_rank_id) const {
  auto rank_table_generator = RankTableGeneratorFactory::Create(local_comm_res_, peer_comm_res);
  LLM_CHK_BOOL_RET_STATUS(rank_table_generator != nullptr, ge::LLM_PARAM_INVALID,
                          "Failed to create rank table generator.");
  LLM_CHK_STATUS_RET(rank_table_generator->Generate(device_id_, rank_table), "Failed to generate rank table");
  local_rank_id = rank_table_generator->GetLocalRankId();
  LLM_CHK_BOOL_RET_STATUS(local_rank_id >= 0, ge::LLM_PARAM_INVALID,
                          "Failed to get local rank id, please check rank table.");
  peer_rank_id = rank_table_generator->GetPeerRankId();
  return ge::SUCCESS;
}

ge::Status LinkMsgHandler::ExchangeInfoProcess(const LLMExchangeInfo &peer_exchange_info, int32_t timeout,
                                               bool force_link, const std::string &peer_cert_fingerprint,
                                               EntityMemInfoPtr &mem_info_ptr) const {
  LLM_CHK_BOOL_RET_STATUS(peer_exchange_info.cache_table_size <= kCacheAccessTableBufferSize, ge::LLM_PARAM_INVALID,
                          "Remote cache_table_size %lu exceeds max %lu.", peer_exchange_info.cache_table_size,
                          kCacheAccessTableBufferSize);
  int32_t local_rank_id = -1;
  int32_t peer_rank_id = -1;
  std::string rank_table;
  LLM_CHK_STATUS_RET(GenerateRankInfo(peer_exchange_info.comm_res, rank_table, local_rank_id, peer_rank_id),
                     "Failed to generate rank info");
  if (force_link) {
    LLM_CHK_STATUS_RET(comm_entity_manager_->DestroyEntity(peer_exchange_info.cluster_id),
                       "Failed to destroy previous entity, peer cluster id:%lu.", peer_exchange_info.cluster_id);
  }

  auto entity = comm_entity_manager_->GetEntityByRemoteClusterId(peer_exchange_info.cluster_id);
  LLM_CHK_BOOL_RET_STATUS(entity == nullptr, ge::LLM_ALREADY_LINK, "Link already exists, peer cluster id:%lu.",
                          peer_exchange_info.cluster_id);

  entity = MakeShared<CommEntity>(UINT64_MAX, peer_exchange_info.cluster_id, peer_rank_id, cluster_id_, local_rank_id);
  LLM_CHECK_NOTNULL(entity);
  // Record the peer certificate so later destroy requests claiming this cluster id can be authenticated.
  entity->SetPeerCertFingerprint(peer_cert_fingerprint);
  LLM_CHK_STATUS_RET(entity->Initialize(remote_cache_accessible_), "Failed to init entity");
  ScopeGuard entity_guard([entity]() { entity->Finalize(); });
  EntityCommInfo::CommParams comm_params{};
  comm_params.rank_id = local_rank_id;
  comm_params.comm_config = comm_config_;
  LLM_ASSERT_EOK(
      strcpy_s(comm_params.comm_config.hcclCommName, COMM_NAME_MAX_LENGTH, peer_exchange_info.comm_name.c_str()));
  comm_params.rank_table = rank_table;
  comm_params.mem_handles = mem_handles_callback_();
  constexpr uint32_t kTimeInSec = 1000;
  auto left_time = timeout % kTimeInSec == 0 ? 0 : 1;
  comm_params.timeout = timeout / kTimeInSec + left_time;
  auto comm_info_ptr = MakeShared<EntityCommInfo>(comm_params);
  LLM_CHECK_NOTNULL(comm_info_ptr);
  LLM_CHK_STATUS_RET(comm_info_ptr->Initialize(), "Failed to init communication.");
  entity->SetEntityCommInfo(comm_info_ptr);
  entity->SetContext(aclrt_context_);
  entity->SetCacheManager(cache_manager_);
  LLM_CHK_STATUS_RET(SetEntityMemInfo(peer_exchange_info, entity, mem_info_ptr), "Failed to set entity mem info");
  LLMLOGI("Success to create comm entity:%s", entity->GetDesc().c_str());
  LLM_CHK_STATUS_RET(comm_entity_manager_->AddEntity(peer_exchange_info.cluster_id, entity), "Failed to add entity");
  entity_guard.Dismiss();
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
  LLM_CHK_STATUS_RET(LLMUtils::IntToIp(cluster.local_ip_infos[0].ip, local_ip_), "Failed to convert local ip.");
  LLM_CHK_STATUS_RET(LocalCommResGenerator::Generate(local_ip_, device_id_, local_comm_res_),
                     "Failed to generate local comm res, local_ip:%s, device_id:%d", local_ip_.c_str(), device_id_);
  return ge::SUCCESS;
}

ge::Status LinkMsgHandler::DisconnectInfoProcess(const LLMDisconnectInfo &peer_disconnect_info) const {
  return comm_entity_manager_->DestroyEntity(peer_disconnect_info.cluster_id);
}

ge::Status LinkMsgHandler::CreateEntityMemInfo(EntityMemInfoPtr &mem_info_ptr) const {
  mem_info_ptr = MakeUnique<EntityMemInfo>(remote_cache_accessible_, comm_entity_manager_->GetHostRegPool(),
                                           comm_entity_manager_->GetDeviceRegPool());
  LLM_CHECK_NOTNULL(mem_info_ptr);
  LLM_CHK_STATUS_RET(mem_info_ptr->Initialize(), "Failed to init mem info");
  return ge::SUCCESS;
}

LLMExchangeInfo LinkMsgHandler::BuildExchangeInfo(const ClusterInfo &cluster, int32_t timeout,
                                                  const EntityMemInfoPtr &mem_info_ptr) const {
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
  return exchange_info;
}

ge::Status LinkMsgHandler::LinkCluster(const ClusterInfo &cluster, int32_t timeout) {
  LLM_CHK_BOOL_RET_STATUS(cluster.remote_ip_infos.size() == 1U, ge::LLM_PARAM_INVALID,
                          "remote_ip_infos size != 1 is unsupported.");
  std::string remote_ip_str;
  LLM_CHK_STATUS_RET(LLMUtils::IntToIp(cluster.remote_ip_infos[0].ip, remote_ip_str), "Failed to convert remote ip.");
  LLM_CHK_STATUS_RET(GenerateLocalCommRes(cluster), "Failed to generate local comm res");
  auto connection = MakeShared<hixl::CtrlMsgPlugin>();
  LLM_CHECK_NOTNULL(connection);
  uint32_t remote_port = static_cast<uint32_t>(cluster.remote_ip_infos[0].port);
  LLMLOGI("Start to link cluster, local cluster_id:%lu, remote info %s:%u, timeout:%d ms.", cluster_id_,
          remote_ip_str.c_str(), remote_port, timeout);
  const auto init_ret = connection->Initialize();
  LLM_CHK_BOOL_RET_STATUS(init_ret == hixl::SUCCESS, ge::LLM_LINK_FAILED,
                          "Call api:CtrlMsgPlugin::Initialize failed, peer:%s:%u, ret:%u", remote_ip_str.c_str(),
                          remote_port, init_ret);
  const auto connect_ret = connection->Connect(remote_ip_str, remote_port, timeout);
  LLM_CHK_BOOL_RET_STATUS(connect_ret == hixl::SUCCESS,
                          (connect_ret == hixl::PARAM_INVALID || connect_ret == hixl::TIMEOUT)
                              ? ConvertHixlError2Ge(connect_ret)
                              : ge::LLM_LINK_FAILED,
                          "Call api:CtrlMsgPlugin::Connect failed, peer:%s:%u, timeout:%d ms, ret:%u",
                          remote_ip_str.c_str(), remote_port, timeout, connect_ret);
  LLMLOGI("connect to remote addr %s:%u success", remote_ip_str.c_str(), remote_port);

  LLM_MAKE_GUARD(close_connection, ([connection]() { (void)connection->Close(); }));
  EntityMemInfoPtr mem_info_ptr = nullptr;
  LLM_CHK_STATUS_RET(CreateEntityMemInfo(mem_info_ptr), "Failed to create entity mem info");
  const auto exchange_info = BuildExchangeInfo(cluster, timeout, mem_info_ptr);

  LLM_CHK_STATUS_RET(SendMsg(connection, LinkMsgType::kConnect, exchange_info), "Failed to send connect msg");
  LLMExchangeInfo peer_exchange_info = {};
  LLM_CHK_STATUS_RET(RecvMsg(connection, LinkMsgType::kConnect, peer_exchange_info, timeout),
                     "Failed to recv connect msg");

  peer_exchange_info.comm_name =
      "llm_datadist_" + std::to_string(cluster_id_) + "_" + std::to_string(peer_exchange_info.cluster_id);
  // 使用 exchange_info.force_link 而非硬编码 false：自连接场景下，服务端 daemon 线程可能先于
  // 客户端完成 AddEntity，导致客户端 ExchangeInfoProcess 命中 ALREADY_LINK。传 true 可在本地
  // 已存在 entity 时先 destroy 再重建，规避竞态；首次建链本地无 entity 时 DestroyEntity
  // 返回 SUCCESS，行为不变。
  // 本端主动建链（client）创建的 entity 不记录对端指纹：非强制断链由 client 发起、server 只做本地
  // force unlink，因此 client 侧 entity 不会收到 daemon 的 kDisconnect/force_link；指纹校验只发生在
  // daemon 创建、带指纹的 server 侧 entity 上。
  auto ret = ExchangeInfoProcess(peer_exchange_info, timeout, exchange_info.force_link, "", mem_info_ptr);
  LLMLinkStatus status{};
  LLM_CHK_STATUS_RET(RecvMsg(connection, LinkMsgType::kStatus, status, timeout), "Failed to recv status msg");
  LLM_CHK_STATUS_RET(status.error_code, "Failed to check peer process ret status, error code[%u], err msg[%s]",
                     status.error_code, status.error_message.c_str());
  LLM_CHK_STATUS_RET(ret, "Failed to process peer exchange info, timeout:%d ms", timeout);
  LLMLOGI("Link cluster success, local cluster_id:%lu, remote cluster_id:%lu, remote ip:%s, remote port:%u",
          cluster_id_, peer_exchange_info.cluster_id, remote_ip_str.c_str(), remote_port);
  return ge::SUCCESS;
}

ge::Status LinkMsgHandler::UnlinkCluster(const ClusterInfo &cluster, int32_t timeout, bool force_flag) const {
  LLMLOGI("Start to unlink cluster, local cluster_id:%lu, remote cluster_id:%lu, timeout:%d ms, force_flag:%d",
          cluster_id_, cluster.remote_cluster_id, timeout, static_cast<int32_t>(force_flag));
  LLM_CHK_BOOL_RET_STATUS(force_flag || cluster.remote_ip_infos.size() == 1U, ge::LLM_PARAM_INVALID,
                          "When force_flag=false, remote_ip_infos size != 1 is unsupported.");
  hixl::CtrlMsgPluginPtr connection;
  LLM_MAKE_GUARD(close_connection, ([&connection]() {
                   if (connection != nullptr) {
                     (void)connection->Close();
                   }
                 }));
  if (!force_flag) {
    std::string remote_ip_str;
    LLM_CHK_STATUS_RET(LLMUtils::IntToIp(cluster.remote_ip_infos[0].ip, remote_ip_str), "Failed to convert remote ip.");
    uint32_t remote_port = static_cast<uint32_t>(cluster.remote_ip_infos[0].port);
    connection = MakeShared<hixl::CtrlMsgPlugin>();
    LLM_CHECK_NOTNULL(connection);
    const auto init_ret = connection->Initialize();
    LLM_CHK_BOOL_RET_STATUS(init_ret == hixl::SUCCESS, ge::LLM_UNLINK_FAILED,
                            "Call api:CtrlMsgPlugin::Initialize failed, peer:%s:%u, ret:%u", remote_ip_str.c_str(),
                            remote_port, init_ret);
    const auto connect_ret = connection->Connect(remote_ip_str, remote_port, timeout);
    LLM_CHK_BOOL_RET_STATUS(connect_ret == hixl::SUCCESS,
                            (connect_ret == hixl::PARAM_INVALID || connect_ret == hixl::TIMEOUT)
                                ? ConvertHixlError2Ge(connect_ret)
                                : ge::LLM_UNLINK_FAILED,
                            "Call api:CtrlMsgPlugin::Connect failed, peer:%s:%u, timeout:%d ms, ret:%u",
                            remote_ip_str.c_str(), remote_port, timeout, connect_ret);
    LLMLOGI("connect to remote addr %s:%u success", remote_ip_str.c_str(), remote_port);
    LLMDisconnectInfo disconnect_info = {};
    disconnect_info.cluster_id = cluster_id_;
    disconnect_info.timeout = timeout;
    LLM_CHK_STATUS_RET(SendMsg(connection, LinkMsgType::kDisconnect, disconnect_info), "Failed to send disconnect msg");
  }
  LLMDisconnectInfo peer_disconnect_info = {};
  peer_disconnect_info.cluster_id = cluster.remote_cluster_id;
  peer_disconnect_info.timeout = timeout;

  auto ret = DisconnectInfoProcess(peer_disconnect_info);
  if (!force_flag) {
    LLMLinkStatus status{};
    LLM_CHK_STATUS_RET(RecvMsg(connection, LinkMsgType::kStatus, status, timeout), "Failed to recv status msg");
    LLM_CHK_STATUS_RET(status.error_code, "Failed to check peer process ret status, error code[%u], err msg[%s]",
                       status.error_code, status.error_message.c_str());
  }
  LLM_CHK_STATUS_RET(ret, "Failed to unlink cluster:%lu", cluster.remote_cluster_id);
  LLMLOGI("Unlink cluster success, local cluster_id:%lu, remote cluster_id:%lu, force_flag:%d", cluster_id_,
          cluster.remote_cluster_id, force_flag);
  return ge::SUCCESS;
}

size_t LinkMsgHandler::GetLinkSize() const {
  return comm_entity_manager_->GetEntitySize();
}

void LinkMsgHandler::SetMemHandlesCallback(GetMemHandles callback) {
  mem_handles_callback_ = callback;
}
}  // namespace llm
