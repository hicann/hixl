/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hixl_client.h"
#include <array>
#include <cerrno>
#include <unistd.h>
#include <sys/poll.h>
#include "securec.h"
#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/hixl_utils.h"
#include "common/ctrl_msg.h"
#include "common/ctrl_msg_plugin.h"
#include "common/scope_guard.h"
#include "engine/client_handler_factory.h"
#include "engine/endpoint_generator/endpoint_generator.h"
#include "engine/endpoint_matcher.h"
#include "profiling/prof_reporter.h"
#include "nlohmann/json.hpp"

namespace hixl {
namespace {
constexpr uint64_t kMaxRecvRespBodySize = static_cast<uint64_t>(4ULL * 1024ULL * 1024ULL);

std::string SerializeNotifyMsg(const NotifyMsg &msg) {
  nlohmann::json j{{"name", msg.name}, {"notify_msg", msg.notify_msg}};
  return j.dump();
}

Status ParseNotifyAckResult(const std::string &json_str) {
  Status result = PARAM_INVALID;
  auto j = nlohmann::json::parse(json_str);
  if (j.contains("result")) {
    j.at("result").get_to(result);
  }
  return result;
}

bool IsSocketDisconnectedErrno(int32_t err_no) {
  return err_no == EPIPE || err_no == EBADF || err_no == ECONNRESET || err_no == ENOTCONN || err_no == ESHUTDOWN ||
         err_no == ETIMEDOUT;
}
}  // namespace

Status HixlClient::Initialize(const std::vector<EndpointConfig> &local_endpoint_list, uint32_t timeout_ms,
                              bool is_lazy) {
  std::lock_guard<std::mutex> lock(mutex_);
  HIXL_CHK_BOOL_RET_STATUS(!local_endpoint_list.empty(), PARAM_INVALID, "The input local_endpoint_list is empty");
  connection_ = MakeShared<CtrlMsgPlugin>();
  HIXL_CHECK_NOTNULL(connection_);
  HIXL_DISMISSABLE_GUARD(close_ctrl_socket, [this] { CloseCtrlSocket(); });
  HIXL_CHK_STATUS_RET(connection_->Initialize(), "Failed to initialize control connection");
  std::vector<EndpointConfig> remote_endpoint_list;
  HIXL_CHK_STATUS_RET(connection_->Connect(server_ip_, server_port_, timeout_ms), "Connect socket failed");
  HIXL_CHK_STATUS_RET(SendEndpointInfoReq(connection_, CtrlMsgType::kGetEndpointInfoReq, timeout_ms),
                      "HixlClient send GetEndpointInfoReq failed, connection:%s", connection_->Desc().c_str());
  HIXL_CHK_STATUS_RET(RecvEndpointInfoResp(connection_, remote_endpoint_list, timeout_ms),
                      "HixlClient receive GetEndpointInfoResp failed, connection:%s", connection_->Desc().c_str());
  HIXL_CHK_BOOL_RET_STATUS(!remote_endpoint_list.empty(), FAILED, "HixlClient received empty remote_endpoint_list");
  std::vector<HandlerCreateArgs::EndpointPair> matched_pairs;
  HandlerCreateArgs::HandlerType handler_type;
  HIXL_CHK_STATUS_RET(EndpointMatcher::MatchEndpoints(local_endpoint_list, remote_endpoint_list, matched_pairs,
                                                      handler_type, global_config_),
                      "EndpointMatcher::MatchEndpoints failed");
  link_pairs_ = matched_pairs;
  HIXL_EVENT("[HixlClient] link selected, local_engine:%s, remote_engine:%s, handler:%s, pair_count:%zu",
             local_engine_.c_str(), remote_engine_.c_str(), EndpointMatcher::HandlerTypeToString(handler_type),
             matched_pairs.size());
  for (size_t i = 0; i < matched_pairs.size(); ++i) {
    const auto &pair = matched_pairs[i];
    HIXL_EVENT(
        "[HixlClient] link pair[%zu], local_engine:%s, remote_engine:%s, comm_type:%s, "
        "local_endpoint:{%s}, remote_endpoint:{%s}",
        i, local_engine_.c_str(), remote_engine_.c_str(), CommTypeToString(pair.type), pair.local.ToString().c_str(),
        pair.remote.ToString().c_str());
  }
  HandlerCreateArgs args{};
  args.server_ip = server_ip_;
  args.server_port = server_port_;
  args.rdma_tc = rdma_tc_;
  args.rdma_sl = rdma_sl_;
  args.handler_type = handler_type;
  args.matched_pairs = std::move(matched_pairs);
  args.global_config = global_config_;
  args.is_lazy = is_lazy;
  args.timeout_ms = timeout_ms;
  args.connection = connection_;
  args.local_engine = local_engine_;
  args.remote_engine = remote_engine_;
  HIXL_CHK_STATUS_RET(ClientHandlerFactory::Create(args, client_handler_),
                      "ClientHandlerFactory create handler failed");
  HIXL_CHECK_NOTNULL(client_handler_, "ClientHandlerFactory create handler failed");
  HIXL_DISMISS_GUARD(close_ctrl_socket);
  return SUCCESS;
}

Status HixlClient::SendEndpointInfoReq(const CtrlMsgPluginPtr &connection, CtrlMsgType msg_type,
                                       uint32_t timeout_ms) const {
  CtrlMsgHeader header{};
  header.magic = kMagicNumber;
  header.body_size = static_cast<uint64_t>(sizeof(CtrlMsgType));
  HIXL_CHK_STATUS_RET(connection->Send(&header, static_cast<uint64_t>(sizeof(header)), timeout_ms),
                      "HixlClient send header failed, connection:%s", connection->Desc().c_str());
  HIXL_CHK_STATUS_RET(connection->Send(&msg_type, static_cast<uint64_t>(sizeof(msg_type)), timeout_ms),
                      "HixlClient send msg_type failed, connection:%s", connection->Desc().c_str());
  return SUCCESS;
}

Status HixlClient::RecvEndpointInfoResp(const CtrlMsgPluginPtr &connection,
                                        std::vector<EndpointConfig> &remote_endpoint_list, uint32_t timeout_ms) const {
  CtrlMsgHeader header{};
  HIXL_CHK_STATUS_RET(connection->Recv(&header, static_cast<uint32_t>(sizeof(header)), timeout_ms),
                      "HixlClient receive header failed, connection:%s", connection->Desc().c_str());
  HIXL_CHK_BOOL_RET_STATUS(header.magic == kMagicNumber, PARAM_INVALID,
                           "Invalid magic for HixlClient RecvEndpointInfoResp, expect:0x%X, actual:0x%X", kMagicNumber,
                           header.magic);
  HIXL_CHK_BOOL_RET_STATUS(
      header.body_size > sizeof(CtrlMsgType) && header.body_size <= kMaxRecvRespBodySize, PARAM_INVALID,
      "Invalid body_size in HixlClient RecvEndpointInfoResp, body_size=%" PRIu64 ", must be in (%zu, %" PRIu64 "]",
      header.body_size, sizeof(CtrlMsgType), kMaxRecvRespBodySize);

  const uint64_t body_size = header.body_size;
  std::vector<uint8_t> body(body_size);
  HIXL_EVENT("[HixlClient] RecvEndpointInfoResp: receiving remote_endpoint_list body (%" PRIu64
             " bytes) from connection=%s begin",
             body_size, connection->Desc().c_str());
  HIXL_CHK_STATUS_RET(connection->Recv(body.data(), static_cast<uint32_t>(body_size), timeout_ms));
  HIXL_EVENT("[HixlClient] RecvEndpointInfoResp: receiving remote_endpoint_list body (%" PRIu64
             " bytes) from connection=%s success",
             body_size, connection->Desc().c_str());

  CtrlMsgType msg_type{};
  const void *src = static_cast<const void *>(body.data());
  errno_t rc = memcpy_s(&msg_type, sizeof(msg_type), src, sizeof(msg_type));
  HIXL_CHK_BOOL_RET_STATUS(rc == EOK, FAILED, "memcpy_s msg_type failed, rc=%d", static_cast<int32_t>(rc));
  HIXL_CHK_BOOL_RET_STATUS(msg_type == CtrlMsgType::kGetEndpointInfoResp, PARAM_INVALID,
                           "Unexpected msg_type=%d in RecvEndpointInfoResp, expect=%d", static_cast<int32_t>(msg_type),
                           static_cast<int32_t>(CtrlMsgType::kGetEndpointInfoResp));

  const size_t json_len = static_cast<size_t>(body_size - sizeof(CtrlMsgType));
  std::string json_str(reinterpret_cast<const char *>(body.data() + sizeof(msg_type)), json_len);
  return EndpointGenerator::DeserializeEndpointConfigList(json_str, remote_endpoint_list);
}

Status HixlClient::RegisterMem(const std::vector<MemHandleInfo> &mem_info_list) {
  std::lock_guard<std::mutex> lock(mutex_);
  HIXL_CHK_BOOL_RET_STATUS(client_handler_ != nullptr, FAILED, "HixlClient is not initialized");
  for (const auto &mi : mem_info_list) {
    HIXL_CHK_STATUS_RET(client_handler_->RegisterMem(mi));
  }
  return SUCCESS;
}

Status HixlClient::DeregisterMem(MemHandle mem_handle) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (client_handler_ == nullptr) {
    return SUCCESS;
  }
  HIXL_CHK_STATUS_RET(client_handler_->DeregisterMem(mem_handle), "HixlClient deregister memory failed, mem_handle:%p",
                      mem_handle);
  return SUCCESS;
}

Status HixlClient::Connect(uint32_t timeout_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  HIXL_CHK_BOOL_RET_STATUS(client_handler_ != nullptr, FAILED, "HixlClient is not initialized");
  HIXL_EVENT("[HixlClient] connect link start, local_engine:%s, remote_engine:%s, timeout_ms:%u", local_engine_.c_str(),
             remote_engine_.c_str(), timeout_ms);
  LogLinkPairs("connect link start");
  HIXL_DISMISSABLE_GUARD(dump_guard, [this]() { client_handler_->Dump("connect failed", DumpLogLevel::ERROR); });
  Status ret = client_handler_->Connect(timeout_ms);
  if (ret == ALREADY_CONNECTED) {
    HIXL_DISMISS_GUARD(dump_guard);
  } else if (ret != SUCCESS) {
    CheckAliveAndLog("connect");
  }
  HIXL_CHK_STATUS_RET(ret, "HixlClient Connect failed");
  HIXL_DISMISS_GUARD(dump_guard);
  is_connected_ = true;
  HIXL_EVENT("[HixlClient] connect link success, local_engine:%s, remote_engine:%s, timeout_ms:%u",
             local_engine_.c_str(), remote_engine_.c_str(), timeout_ms);
  LogLinkPairs("connect link success");
  return SUCCESS;
}

Status HixlClient::TransferSync(const std::vector<TransferOpDesc> &op_descs, TransferOp operation,
                                uint32_t timeout_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  HIXL_CHK_BOOL_RET_STATUS(!op_descs.empty(), PARAM_INVALID, "HixlClient TransferSync failed, op_descs is empty");
  HIXL_CHK_BOOL_RET_STATUS(client_handler_ != nullptr, FAILED, "HixlClient is not initialized");
  HIXL_CHK_BOOL_RET_STATUS(is_connected_, NOT_CONNECTED, "HixlClient is not connected");
  HIXL_CHK_BOOL_RET_STATUS(!is_finalized_, FAILED, "HixlClient TransferSync rejected, client is finalized");
  HIXL_DISMISSABLE_GUARD(dump_guard, [this]() { client_handler_->Dump("transfer sync failed", DumpLogLevel::ERROR); });
  Status ret = client_handler_->TransferSync(op_descs, operation, timeout_ms);
  if (ret == SUCCESS) {
    HIXL_DISMISS_GUARD(dump_guard);
  } else {
    CheckAliveAndLog("transfer sync");
  }
  return ret;
}

Status HixlClient::TransferAsync(const std::vector<TransferOpDesc> &op_descs, TransferOp operation,
                                 const TransferArgs &optional_args, TransferReq &req) {
  (void)optional_args;
  std::lock_guard<std::mutex> lock(mutex_);
  HIXL_CHK_BOOL_RET_STATUS(!op_descs.empty(), PARAM_INVALID, "HixlClient TransferAsync failed, op_descs is empty");
  HIXL_CHK_BOOL_RET_STATUS(is_connected_, NOT_CONNECTED, "HixlClient is not connected");
  HIXL_CHK_BOOL_RET_STATUS(client_handler_ != nullptr, FAILED, "HixlClient is not initialized");
  HIXL_DISMISSABLE_GUARD(dump_guard, [this]() { client_handler_->Dump("transfer async failed", DumpLogLevel::ERROR); });
  HixlProfType prof_type = (operation == READ ? HixlProfType::HixlOpBatchRead : HixlProfType::HixlOpBatchWrite);
  TransferInfo transfer_info = {GetProfStart(prof_type), operation, AscendString()};
  Status ret = client_handler_->TransferAsync(op_descs, operation, req);
  if (ret != SUCCESS) {
    CheckAliveAndLog("transfer async");
  }
  HIXL_CHK_STATUS_RET(ret, "HixlClient TransferAsync failed");
  HIXL_DISMISS_GUARD(dump_guard);
  req_map_[req] = transfer_info;
  return SUCCESS;
}

Status HixlClient::GetTransferStatus(const TransferReq &req, TransferStatus &status) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (client_handler_ == nullptr) {
    HIXL_LOGE(FAILED, "HixlClient is not initialized");
    status = TransferStatus::FAILED;
    return FAILED;
  }
  TransferInfo transfer_info{};
  auto it = req_map_.find(req);
  if (it == req_map_.end()) {
    HIXL_LOGE(PARAM_INVALID, "HixlClient GetTransferStatus failed, request not found, req:%p", req);
    status = TransferStatus::FAILED;
    return PARAM_INVALID;
  }
  transfer_info = it->second;

  HIXL_DISMISSABLE_GUARD(dump_guard,
                         [this]() { client_handler_->Dump("get transfer status failed", DumpLogLevel::ERROR); });
  Status ret = client_handler_->GetTransferStatus(req, status);
  if (ret != SUCCESS) {
    RemoveTransferReq(req);
    return ret;
  }
  if (status == TransferStatus::COMPLETED) {
    HIXL_API_PROFILING_WITH_PROF_START(transfer_info.prof_start);
    RemoveTransferReq(req);
  } else if (status == TransferStatus::FAILED) {
    RemoveTransferReq(req);
    return SUCCESS;
  }
  HIXL_DISMISS_GUARD(dump_guard);
  return SUCCESS;
}

bool HixlClient::HasTransferReq(const TransferReq &req) const {
  return req_map_.find(req) != req_map_.end();
}

void HixlClient::ClearTransferReqs() {
  req_map_.clear();
}

void HixlClient::RemoveTransferReq(const TransferReq &req) {
  req_map_.erase(req);
}

void HixlClient::AcquireUse() {
  use_count_.fetch_add(1U, std::memory_order_acq_rel);
}

void HixlClient::ReleaseUse() {
  use_count_.fetch_sub(1U, std::memory_order_acq_rel);
}

bool HixlClient::IsBusy() const {
  // 加一发生在 ClientManager 锁内。这里用 acquire，淘汰线程再次进这把锁后能看见本次传输已占用。
  if (use_count_.load(std::memory_order_acquire) > 0U) {
    return true;
  }
  // A locked mutex_ means another thread is running a transfer on this link, so do not wait for it here.
  std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
  if (!lock.owns_lock()) {
    return true;
  }
  return !req_map_.empty();
}

const std::string &HixlClient::GetRemoteEngine() const {
  return remote_engine_;
}

void HixlClient::CloseCtrlSocket() {
  if (connection_ != nullptr) {
    HIXL_LOGI("HixlClient close ctrl socket start, connection:%s", connection_->Desc().c_str());
    (void)connection_->Close();
    HIXL_LOGI("HixlClient close ctrl socket end, connection:%s", connection_->Desc().c_str());
    connection_.reset();
  }
}

Status HixlClient::Finalize() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (is_finalized_) {
    return SUCCESS;
  }
  is_finalized_ = true;
  ClearTransferReqs();
  HIXL_EVENT("[HixlClient] disconnect link start, local_engine:%s, remote_engine:%s", local_engine_.c_str(),
             remote_engine_.c_str());
  LogLinkPairs("disconnect link start");
  HIXL_DISMISSABLE_GUARD(dump_guard, [this]() {
    if (client_handler_ != nullptr) {
      client_handler_->Dump("disconnect failed", DumpLogLevel::ERROR);
    }
  });
  CloseCtrlSocket();
  Status ret = (client_handler_ != nullptr) ? client_handler_->Finalize() : SUCCESS;
  if (ret == SUCCESS) {
    HIXL_DISMISS_GUARD(dump_guard);
    HIXL_EVENT("[HixlClient] disconnect link success, local_engine:%s, remote_engine:%s", local_engine_.c_str(),
               remote_engine_.c_str());
    LogLinkPairs("disconnect link success");
  }
  is_connected_ = false;
  return ret;
}

void HixlClient::LogLinkPairs(const char *phase) const {
  for (size_t i = 0; i < link_pairs_.size(); ++i) {
    const auto &pair = link_pairs_[i];
    HIXL_EVENT(
        "[HixlClient] %s pair[%zu], local_engine:%s, remote_engine:%s, comm_type:%s, "
        "local_endpoint:{%s}, remote_endpoint:{%s}",
        phase, i, local_engine_.c_str(), remote_engine_.c_str(), CommTypeToString(pair.type),
        pair.local.ToString().c_str(), pair.remote.ToString().c_str());
  }
}

Status HixlClient::RecvNotifyAck(const CtrlMsgPluginPtr &connection, int32_t timeout_ms) const {
  CtrlMsgHeader header{};
  HIXL_CHK_STATUS_RET(connection->Recv(&header, static_cast<uint32_t>(sizeof(header)), timeout_ms),
                      "HixlClient receive NotifyAck header failed, connection:%s", connection->Desc().c_str());
  HIXL_CHK_BOOL_RET_STATUS(header.magic == kMagicNumber, PARAM_INVALID,
                           "Invalid magic for NotifyAck, expect:0x%X, actual:0x%X", kMagicNumber, header.magic);
  HIXL_CHK_BOOL_RET_STATUS(header.body_size > sizeof(CtrlMsgType) && header.body_size <= kMaxRecvRespBodySize,
                           PARAM_INVALID, "Invalid body_size for NotifyAck, body_size:%lu", header.body_size);

  const uint64_t body_size = header.body_size;
  std::vector<uint8_t> body(body_size);
  HIXL_CHK_STATUS_RET(connection->Recv(body.data(), static_cast<uint32_t>(body_size), timeout_ms),
                      "HixlClient receive NotifyAck body failed, connection:%s", connection->Desc().c_str());

  CtrlMsgType msg_type{};
  const void *src = static_cast<const void *>(body.data());
  errno_t rc = memcpy_s(&msg_type, sizeof(msg_type), src, sizeof(msg_type));
  HIXL_CHK_BOOL_RET_STATUS(rc == EOK, FAILED, "memcpy_s msg_type failed, rc:%d", static_cast<int32_t>(rc));
  HIXL_CHK_BOOL_RET_STATUS(msg_type == CtrlMsgType::kNotifyAck, PARAM_INVALID,
                           "Unexpected msg_type=%d, expect kNotifyAck=%d", static_cast<int32_t>(msg_type),
                           static_cast<int32_t>(CtrlMsgType::kNotifyAck));

  const size_t json_len = static_cast<size_t>(body_size - sizeof(CtrlMsgType));
  std::string json_str(reinterpret_cast<const char *>(body.data() + sizeof(msg_type)), json_len);
  Status result = SUCCESS;
  try {
    result = ParseNotifyAckResult(json_str);
  } catch (const nlohmann::json::exception &e) {
    HIXL_LOGE(PARAM_INVALID, "Failed to parse NotifyAck, exception:%s", e.what());
    return PARAM_INVALID;
  }
  if (result != SUCCESS) {
    HIXL_LOGE(result, "NotifyAck result failed, result:%u", result);
    return result;
  }
  HIXL_LOGI("HixlClient received NotifyAck success");
  return SUCCESS;
}

Status HixlClient::CheckAlive() {
  std::lock_guard<std::mutex> lock(mutex_);
  return CheckAliveLocked();
}

bool HixlClient::IsCtrlSocketWritable() const {
  struct pollfd pfd = {};
  pfd.fd = connection_->Fd();
  pfd.events = POLLOUT;
  return poll(&pfd, 1, 0) > 0;
}

Status HixlClient::CheckAliveLocked() {
  HIXL_CHK_BOOL_RET_STATUS(connection_ != nullptr, FAILED,
                           "HixlClient CheckAlive failed, peer_ip:%s, peer_port:%u, control connection is null",
                           server_ip_.c_str(), server_port_);
  if (!connection_->IsUsable()) {
    HIXL_LOGE(FAILED, "HixlClient control connection is unusable, peer_ip:%s, peer_port:%u, fd:%d", server_ip_.c_str(),
              server_port_, connection_->Fd());
    CloseCtrlSocket();
    return FAILED;
  }
  CtrlMsgHeader header{};
  header.magic = kMagicNumber;
  header.body_size = sizeof(CtrlMsgType);
  CtrlMsgType msg_type = CtrlMsgType::kHeartBeat;
  std::array<uint8_t, sizeof(CtrlMsgHeader) + sizeof(CtrlMsgType)> heartbeat_msg{};
  errno_t rc = memcpy_s(heartbeat_msg.data(), heartbeat_msg.size(), &header, sizeof(header));
  HIXL_CHK_BOOL_RET_STATUS(rc == EOK, FAILED, "memcpy_s heartbeat header failed, rc=%d", static_cast<int32_t>(rc));
  rc = memcpy_s(heartbeat_msg.data() + sizeof(header), heartbeat_msg.size() - sizeof(header), &msg_type,
                sizeof(msg_type));
  HIXL_CHK_BOOL_RET_STATUS(rc == EOK, FAILED, "memcpy_s heartbeat msg_type failed, rc=%d", static_cast<int32_t>(rc));

  if (!IsCtrlSocketWritable()) {
    HIXL_LOGW(
        "HixlClient CheckAlive skipped heartbeat, ctrl socket is not writable, peer_ip:%s, peer_port:%u, connection:%s",
        server_ip_.c_str(), server_port_, connection_->Desc().c_str());
    return SUCCESS;
  }

  int32_t err_no = 0;
  Status ret =
      connection_->Send(heartbeat_msg.data(), heartbeat_msg.size(), CtrlMsgPlugin::kDefaultCtrlTimeoutMs, err_no);
  if (ret != SUCCESS) {
    if (!connection_->IsUsable() || IsSocketDisconnectedErrno(err_no)) {
      HIXL_LOGE(FAILED,
                "HixlClient CheckAlive send heartbeat failed, peer_ip:%s, peer_port:%u, connection=%s, errno=%d",
                server_ip_.c_str(), server_port_, connection_->Desc().c_str(), err_no);
      CloseCtrlSocket();
      return FAILED;
    }
    HIXL_LOGW("HixlClient CheckAlive send heartbeat failed, peer_ip:%s, peer_port:%u, connection=%s, errno=%d",
              server_ip_.c_str(), server_port_, connection_->Desc().c_str(), err_no);
    return SUCCESS;
  }
  return SUCCESS;
}

void HixlClient::CheckAliveAndLog(const char *operation) {
  Status alive_ret = CheckAliveLocked();
  if (alive_ret != SUCCESS) {
    HIXL_LOGE(alive_ret,
              "HixlClient link alive check after %s failure, ctrl link is dead, local_engine:%s, remote_engine:%s, "
              "peer_ip:%s, peer_port:%u, check_alive_ret:%u",
              operation, local_engine_.c_str(), remote_engine_.c_str(), server_ip_.c_str(), server_port_,
              static_cast<uint32_t>(alive_ret));
  }
}

Status HixlClient::SendNotify(const NotifyDesc &notify, int32_t timeout_ms) const {
  std::lock_guard<std::mutex> lock(mutex_);
  NotifyMsg notify_msg{notify.name.GetString(), notify.notify_msg.GetString()};

  HIXL_CHK_BOOL_RET_STATUS(notify_msg.name.size() <= kMaxNotifyNameLen, PARAM_INVALID,
                           "Notify name length invalid, size:%zu, max:%zu", notify_msg.name.size(), kMaxNotifyNameLen);
  HIXL_CHK_BOOL_RET_STATUS(notify_msg.notify_msg.size() <= kMaxNotifyMsgLen, PARAM_INVALID,
                           "Notify message too long, size:%zu, max:%zu", notify_msg.notify_msg.size(),
                           kMaxNotifyMsgLen);

  std::string msg_str;
  try {
    msg_str = SerializeNotifyMsg(notify_msg);
  } catch (const nlohmann::json::exception &e) {
    HIXL_LOGE(PARAM_INVALID, "Failed to serialize NotifyMsg, exception:%s", e.what());
    return PARAM_INVALID;
  }

  CtrlMsgHeader header{};
  header.magic = kMagicNumber;
  header.body_size = static_cast<uint64_t>(sizeof(CtrlMsgType) + msg_str.size());
  CtrlMsgType msg_type = CtrlMsgType::kNotify;

  HIXL_CHK_BOOL_RET_STATUS(connection_ != nullptr, FAILED,
                           "HixlClient SendNotify failed, control connection is null, peer_ip:%s, peer_port:%u",
                           server_ip_.c_str(), server_port_);

  HIXL_CHK_STATUS_RET(
      connection_->Send(&header, static_cast<uint64_t>(sizeof(header)), static_cast<uint32_t>(timeout_ms)),
      "HixlClient send NotifyMsg header failed, connection:%s", connection_->Desc().c_str());
  HIXL_CHK_STATUS_RET(
      connection_->Send(&msg_type, static_cast<uint64_t>(sizeof(msg_type)), static_cast<uint32_t>(timeout_ms)),
      "HixlClient send NotifyMsg msg_type failed, connection:%s", connection_->Desc().c_str());
  HIXL_CHK_STATUS_RET(
      connection_->Send(msg_str.c_str(), static_cast<uint64_t>(msg_str.size()), static_cast<uint32_t>(timeout_ms)),
      "HixlClient send NotifyMsg body failed, connection:%s", connection_->Desc().c_str());

  HIXL_LOGI("HixlClient sent NotifyMsg, name:%s, connection:%s", notify_msg.name.c_str(), connection_->Desc().c_str());
  HIXL_CHK_STATUS_RET(RecvNotifyAck(connection_, timeout_ms),
                      "HixlClient receive NotifyAck failed, timeout:%d ms, connection:%s", timeout_ms,
                      connection_->Desc().c_str());
  return SUCCESS;
}
}  // namespace hixl
