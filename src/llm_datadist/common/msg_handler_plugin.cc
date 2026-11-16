/**
 * Copyright (c) 2025-2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "msg_handler_plugin.h"
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <sys/poll.h>
#include <sys/socket.h>
#include "common/llm_utils.h"
#include "common/llm_checker.h"
#include "common/llm_scope_guard.h"
#include "common/ctrl_msg_plugin.h"

namespace llm {
namespace {
constexpr int32_t kListenBacklog = 128;
constexpr int64_t kDefaultSleepTime = 1;
constexpr uint32_t kMagicNumber = 0xA4B3C2D1;
constexpr int32_t kAcceptPollTimeoutMs = 1000;

ge::Status ToGeStatus(hixl::Status status, ge::Status default_err) {
  if (status == hixl::SUCCESS) {
    return ge::SUCCESS;
  }
  if (status == hixl::TIMEOUT) {
    return ge::LLM_TIMEOUT;
  }
  if (status == hixl::PARAM_INVALID) {
    return ge::LLM_PARAM_INVALID;
  }
  return default_err;
}
}  // namespace

ge::Status MsgHandlerPlugin::Initialize() {
  listener_connection_ = MakeShared<hixl::CtrlMsgPlugin>();
  LLM_CHECK_NOTNULL(listener_connection_);
  return ToGeStatus(listener_connection_->Initialize(), ge::FAILED);
}

void MsgHandlerPlugin::ListenClose() {
  if (listener_connection_ != nullptr) {
    (void)listener_connection_->Close();
  }
}

void MsgHandlerPlugin::RegisterConnectedProcess(ConnectedProcess proc) {
  connected_process_ = proc;
}

ge::Status MsgHandlerPlugin::DoConnectedProcess(const hixl::CtrlMsgPluginPtr &connection) {
  LLM_DISMISSABLE_GUARD(close_connection, ([connection]() { connection->Close(); }));
  LLM_CHK_BOOL_RET_STATUS(aclrtSetCurrentContext(aclrt_context_) == ACL_SUCCESS, ge::LLM_PARAM_INVALID,
                          "Set aclrt context failed.");
  constexpr int32_t kMillisPerSecond = 1000;
  struct timeval timeout;
  timeout.tv_sec = kServerReadTimeoutMs / kMillisPerSecond;
  timeout.tv_usec = 0;
  auto socket_ret = setsockopt(connection->Fd(), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  LLM_CHK_BOOL_RET_STATUS(socket_ret == 0, ge::FAILED,
                          "Failed to setsockopt(SO_RCVTIMEO), socket_ret:%d, error msg:%s, errno:%d", socket_ret,
                          strerror(errno), errno);
  LLM_CHK_STATUS_RET(ToGeStatus(connection->SetTcpNoDelay(), ge::FAILED), "Failed to set TCP_NODELAY, connection:%s",
                     connection->Desc().c_str());
  LLM_CHK_STATUS_RET(ToGeStatus(connection->HandshakeServer(), ge::FAILED),
                     "Failed to finish TLS handshake, connection:%s", connection->Desc().c_str());
  bool keep_connection = false;
  connected_process_(connection, keep_connection);
  if (keep_connection) {
    LLM_DISMISS_GUARD(close_connection);
    return ge::SUCCESS;
  }
  socket_ret = shutdown(connection->Fd(), SHUT_RDWR);
  LLM_CHK_BOOL_RET_STATUS(socket_ret == 0, ge::FAILED,
                          "Failed to shutdown conn_fd, connection may be incomplete, "
                          "socket_ret:%d, error msg:%s, errno:%d",
                          socket_ret, strerror(errno), errno);
  return ge::SUCCESS;
}

ge::Status MsgHandlerPlugin::DoAccept() {
  if (!listener_running_) {
    return ge::SUCCESS;
  }
  hixl::CtrlMsgPluginPtr connection;
  const auto status = listener_connection_->Accept(connection);
  LLM_CHK_BOOL_RET_STATUS(status == hixl::SUCCESS, ge::FAILED, "Failed to accept, hixl_status:%u", status);
  if (connection == nullptr) {
    return ge::SUCCESS;
  }
  LLM_DISMISSABLE_GUARD(close_connection, ([connection]() { (void)connection->Close(); }));
  auto task = thread_pool_->commit([this, connection]() { (void)DoConnectedProcess(connection); });
  LLM_CHK_BOOL_RET_STATUS(task.valid(), ge::FAILED, "Failed to submit connection task");
  LLM_DISMISS_GUARD(close_connection);
  return ge::SUCCESS;
}

ge::Status MsgHandlerPlugin::StartDaemon(const std::string &ip, uint32_t listen_port) {
  LLM_CHK_BOOL_RET_STATUS(!listener_running_, ge::FAILED, "Control listener is already running");
  LLM_ASSERT_RT_OK(aclrtGetCurrentContext(&aclrt_context_));
  LLM_CHK_STATUS_RET(Initialize(), "Failed to initialize control listener");
  LLM_CHK_STATUS_RET(ToGeStatus(listener_connection_->Listen(ip, listen_port, kListenBacklog), ge::FAILED),
                     "Failed to listen on %s:%u", ip.c_str(), listen_port);
  LLM_DISMISSABLE_GUARD(fail_guard, ([this]() { ListenClose(); }));
  constexpr uint32_t kThreadPoolSize = 16U;
  thread_pool_ = MakeUnique<LLMThreadPool>("ge_llm_mhp", kThreadPoolSize);
  LLM_CHECK_NOTNULL(thread_pool_);
  listener_running_ = true;
  listener_ = std::thread([this]() {
    while (listener_running_) {
      struct pollfd pfd = {};
      pfd.fd = listener_connection_->Fd();
      pfd.events = POLLIN;
      const int pret = poll(&pfd, 1, kAcceptPollTimeoutMs);
      if (pret <= 0) {
        continue;
      }
      auto ret = DoAccept();
      if (ret != ge::SUCCESS && listener_running_) {
        std::this_thread::sleep_for(std::chrono::seconds(kDefaultSleepTime));
      }
    }
  });
  LLM_DISMISS_GUARD(fail_guard);
  return ge::SUCCESS;
}

ge::Status MsgHandlerPlugin::SendMsg(const hixl::CtrlMsgPluginPtr &connection, int32_t msg_type,
                                     const std::string &msg_str) {
  LLM_CHECK_NOTNULL(connection);
  auto ret = connection->Send(&kMagicNumber, sizeof(kMagicNumber));
  LLM_CHK_BOOL_RET_STATUS(ret == hixl::SUCCESS, ge::FAILED,
                          "Call api:CtrlMsgPlugin::Send magic number failed, hixl_status:%u, connection:%s", ret,
                          connection->Desc().c_str());
  uint64_t length = msg_str.size() + sizeof(msg_type);
  ret = connection->Send(&length, sizeof(length));
  LLM_CHK_BOOL_RET_STATUS(ret == hixl::SUCCESS, ge::FAILED,
                          "Call api:CtrlMsgPlugin::Send message length failed, hixl_status:%u, connection:%s", ret,
                          connection->Desc().c_str());
  ret = connection->Send(&msg_type, sizeof(msg_type));
  LLM_CHK_BOOL_RET_STATUS(ret == hixl::SUCCESS, ge::FAILED,
                          "Call api:CtrlMsgPlugin::Send message type failed, hixl_status:%u, connection:%s, type:%d",
                          ret, connection->Desc().c_str(), msg_type);
  ret = connection->Send(msg_str.data(), msg_str.size());
  LLM_CHK_BOOL_RET_STATUS(ret == hixl::SUCCESS, ge::FAILED,
                          "Call api:CtrlMsgPlugin::Send message body failed, hixl_status:%u, connection:%s, "
                          "size:%zu bytes",
                          ret, connection->Desc().c_str(), msg_str.size());
  return ge::SUCCESS;
}

ge::Status MsgHandlerPlugin::RecvMsg(const hixl::CtrlMsgPluginPtr &connection, int32_t &msg_type,
                                     std::vector<char> &msg, int32_t timeout_ms) {
  LLM_CHK_BOOL_RET_STATUS(connection != nullptr, ge::FAILED, "Cannot receive message: control connection is null");
  LLM_CHK_BOOL_RET_STATUS(timeout_ms > 0, ge::FAILED, "Invalid receive timeout:%d ms, must be positive", timeout_ms);
  uint32_t magic_number = 0U;
  auto ret = connection->Recv(&magic_number, sizeof(magic_number), static_cast<uint32_t>(timeout_ms));
  LLM_CHK_BOOL_RET_STATUS(ret == hixl::SUCCESS, ge::FAILED,
                          "Call api:CtrlMsgPlugin::Recv magic number failed, hixl_status:%u, connection:%s, "
                          "timeout:%d ms",
                          ret, connection->Desc().c_str(), timeout_ms);
  LLM_CHK_BOOL_RET_STATUS(magic_number == kMagicNumber, ge::FAILED, "Failed to check recv magic num:%u", magic_number);
  const static size_t kMaxLength = 1ULL << 20;
  uint64_t length = 0;
  ret = connection->Recv(&length, sizeof(length), static_cast<uint32_t>(timeout_ms));
  LLM_CHK_BOOL_RET_STATUS(ret == hixl::SUCCESS, ge::FAILED,
                          "Call api:CtrlMsgPlugin::Recv message length failed, hixl_status:%u, connection:%s, "
                          "timeout:%d ms",
                          ret, connection->Desc().c_str(), timeout_ms);
  LLM_CHK_BOOL_RET_STATUS(length <= kMaxLength && length >= sizeof(int32_t), ge::FAILED,
                          "Failed to check msg len:%lu, must in range: [%zu, %zu]", length, sizeof(int32_t),
                          kMaxLength);
  int32_t type = 0;
  ret = connection->Recv(&type, sizeof(type), static_cast<uint32_t>(timeout_ms));
  LLM_CHK_BOOL_RET_STATUS(ret == hixl::SUCCESS, ge::FAILED,
                          "Call api:CtrlMsgPlugin::Recv message type failed, hixl_status:%u, connection:%s, "
                          "timeout:%d ms",
                          ret, connection->Desc().c_str(), timeout_ms);
  msg_type = type;
  size_t msg_len = static_cast<size_t>(length) - sizeof(int32_t);
  msg.resize(msg_len + 1U);
  ret = connection->Recv(msg.data(), msg_len, static_cast<uint32_t>(timeout_ms));
  LLM_CHK_BOOL_RET_STATUS(ret == hixl::SUCCESS, ge::FAILED,
                          "Call api:CtrlMsgPlugin::Recv message body failed, hixl_status:%u, connection:%s, "
                          "size:%zu bytes, timeout:%d ms",
                          ret, connection->Desc().c_str(), msg_len, timeout_ms);
  msg[msg_len] = '\0';
  return ge::SUCCESS;
}

MsgHandlerPlugin::~MsgHandlerPlugin() {
  if (listener_running_) {
    Finalize();
  }
}

void MsgHandlerPlugin::Finalize() {
  if (listener_running_) {
    listener_running_ = false;
  }
  ListenClose();
  if (listener_.joinable()) {
    listener_.join();
  }
  if (thread_pool_ != nullptr) {
    thread_pool_->Destroy();
  }
}
}  // namespace llm
