/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <gtest/gtest.h>
#include "securec.h"

#include "depends/ctrl_msg_test_helper.h"
#include "common/ctrl_msg.h"
#include "common/tls/tls_test_helper.h"
#define private public
#include "cs/hixl_cs_server.h"
#undef private

namespace hixl {
class HixlCSServerHandshakeUt : public ::testing::Test {
 protected:
  void SetUp() override {
    dir_ = test::MakeTempDir();
    ASSERT_FALSE(dir_.empty());
    ASSERT_TRUE(test::GenerateSelfSignedCert(dir_));
    config_ = test::MakeTlsConfig(dir_);
    ASSERT_EQ(TlsManager::GetInstance().Initialize(config_), SUCCESS);
    manager_initialized_ = true;
    const nlohmann::json json{{"tls_config.cert_file", config_.cert_file},
                              {"tls_config.ca_file", config_.ca_file},
                              {"tls_config.key_pem", config_.key_pem}};
    GlobalConfig global;
    ASSERT_EQ(GlobalConfig::Parse(json.dump().c_str(), global), SUCCESS);
    server_ = std::make_unique<HixlCSServer>("127.0.0.1", 0, global);
    server_->listener_connection_ = std::make_shared<CtrlMsgPlugin>();
    ASSERT_EQ(server_->listener_connection_->Initialize(), SUCCESS);
    ASSERT_EQ(server_->listener_connection_->Listen("127.0.0.1", 0, 16), SUCCESS);
    ASSERT_EQ(server_->listener_connection_->AddToEpoll(server_->epoll_fd_), SUCCESS);
    ASSERT_EQ(server_->msg_handler_.Initialize(), SUCCESS);
    ASSERT_EQ(server_->RegProc(CtrlMsgType::kNotify,
                               [this](const CtrlMsgPluginPtr &connection, const char *msg, uint64_t size) {
                                 const auto status = connection->Send(msg, size);
                                 ++echo_count_;
                                 return status;
                               }),
              SUCCESS);
  }

  void TearDown() override {
    if (server_ != nullptr) {
      EXPECT_EQ(server_->Finalize(), SUCCESS);
    }
    for (const int fd : peers_) {
      (void)close(fd);
    }
    if (manager_initialized_) {
      TlsManager::GetInstance().Finalize();
    }
    test::RemovePath(dir_ + "/cert.pem");
    test::RemovePath(dir_ + "/key.pem");
    test::RemovePath(dir_);
  }

  int ConnectRaw() {
    sockaddr_in addr{};
    socklen_t size = sizeof(addr);
    if (getsockname(server_->listener_connection_->Fd(), reinterpret_cast<sockaddr *>(&addr), &size) != 0) {
      return -1;
    }
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
      return -1;
    }
    peers_.push_back(fd);
    return connect(fd, reinterpret_cast<sockaddr *>(&addr), size) == 0 ? fd : -1;
  }

  int AcceptSilent() {
    const int peer = ConnectRaw();
    EXPECT_GE(peer, 0);
    EXPECT_EQ(server_->AcceptClient(), SUCCESS);
    return peer;
  }

  void ConnectTls(CtrlMsgPluginPtr &client) {
    sockaddr_in addr{};
    socklen_t size = sizeof(addr);
    ASSERT_EQ(getsockname(server_->listener_connection_->Fd(), reinterpret_cast<sockaddr *>(&addr), &size), 0);
    client = std::make_shared<CtrlMsgPlugin>();
    ASSERT_EQ(client->Initialize(), SUCCESS);
    auto connected =
        std::async(std::launch::async, [&]() { return client->Connect("127.0.0.1", ntohs(addr.sin_port), 2000); });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    do {
      ASSERT_EQ(server_->DoWait(), SUCCESS);
      if (connected.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready &&
          server_->pending_handshakes_.size() == silent_count_) {
        break;
      }
    } while (std::chrono::steady_clock::now() < deadline);
    ASSERT_EQ(connected.get(), SUCCESS);
    ASSERT_EQ(server_->pending_handshakes_.size(), silent_count_);
  }

  void Echo(const CtrlMsgPluginPtr &client, const std::string &payload) {
    const auto expected = echo_count_.load() + 1U;
    const CtrlMsgHeader header{kMagicNumber, sizeof(CtrlMsgType) + payload.size()};
    const CtrlMsgType type = CtrlMsgType::kNotify;
    ASSERT_EQ(client->Send(&header, sizeof(header)), SUCCESS);
    ASSERT_EQ(client->Send(&type, sizeof(type)), SUCCESS);
    ASSERT_EQ(client->Send(payload.data(), payload.size()), SUCCESS);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (echo_count_.load() < expected && std::chrono::steady_clock::now() < deadline) {
      ASSERT_EQ(server_->DoWait(), SUCCESS);
    }
    ASSERT_EQ(echo_count_.load(), expected);
    std::string received(payload.size(), '\0');
    ASSERT_EQ(client->Recv(received.data(), received.size(), 1000), SUCCESS);
    EXPECT_EQ(received, payload);
  }

  bool manager_initialized_ = false;
  size_t silent_count_ = 0;
  std::atomic<size_t> echo_count_{0};
  std::string dir_;
  TlsConfig config_;
  std::unique_ptr<HixlCSServer> server_;
  std::vector<int> peers_;
};

TEST_F(HixlCSServerHandshakeUt, SilentClientDoesNotBlockNewOrEstablishedConnections) {
  CtrlMsgPluginPtr established;
  ConnectTls(established);
  ASSERT_GE(AcceptSilent(), 0);
  silent_count_ = 1;
  ASSERT_EQ(server_->pending_handshakes_.size(), 1U);
  CtrlMsgPluginPtr incoming;
  ConnectTls(incoming);
  EXPECT_EQ(server_->clients_.size(), 2U);
  Echo(established, "existing-connection");
  Echo(incoming, "new-connection");
  EXPECT_EQ(server_->pending_handshakes_.size(), 1U);
}

TEST_F(HixlCSServerHandshakeUt, IncompleteHandshakeRejectsApplicationIo) {
  ASSERT_GE(AcceptSilent(), 0);
  ASSERT_EQ(server_->pending_handshakes_.size(), 1U);
  auto connection = server_->pending_handshakes_.begin()->second.connection;
  char byte = 'x';
  ssize_t size = 0;
  EXPECT_EQ(connection->Send(&byte, 1), FAILED);
  EXPECT_EQ(connection->Recv(&byte, 1, 1), FAILED);
  EXPECT_EQ(connection->Recv(&byte, 1, size), FAILED);
  EXPECT_FALSE(connection->HasPending());
  EXPECT_EQ(server_->clients_.size(), 0U);
}

TEST_F(HixlCSServerHandshakeUt, PendingHandshakeUsesTenSecondTimeout) {
  ASSERT_GE(AcceptSilent(), 0);
  ASSERT_EQ(server_->pending_handshakes_.size(), 1U);
  const auto deadline = server_->pending_handshakes_.begin()->second.deadline;
  const auto remaining = deadline - std::chrono::steady_clock::now();
  EXPECT_EQ(CtrlMsgPlugin::kServerHandshakeTimeoutMs, 10000);
  EXPECT_GT(remaining, std::chrono::milliseconds(0));
  EXPECT_LE(remaining, std::chrono::milliseconds(CtrlMsgPlugin::kServerHandshakeTimeoutMs));
}

TEST_F(HixlCSServerHandshakeUt, SilentHandshakeExpiresWithoutAnotherSocketEvent) {
  const int peer = AcceptSilent();
  ASSERT_GE(peer, 0);
  auto &pending = server_->pending_handshakes_.begin()->second;
  auto connection = pending.connection;
  const int fd = connection->Fd();
  const auto original_deadline = pending.deadline;
  ASSERT_EQ(server_->AdvanceHandshake(fd), SUCCESS);
  EXPECT_EQ(pending.deadline, original_deadline);
  pending.deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(20);
  ASSERT_EQ(server_->DoWait(), SUCCESS);
  EXPECT_TRUE(server_->pending_handshakes_.empty());
  EXPECT_EQ(connection->Fd(), -1);
  EXPECT_EQ(fcntl(fd, F_GETFD), -1);
  char byte = 0;
  EXPECT_EQ(recv(peer, &byte, 1, MSG_DONTWAIT), 0);
}

TEST_F(HixlCSServerHandshakeUt, FailedHandshakeDoesNotPreventNextClient) {
  const int peer = AcceptSilent();
  ASSERT_GE(peer, 0);
  const std::string invalid = "GET / HTTP/1.0\r\n\r\n";
  ASSERT_EQ(send(peer, invalid.data(), invalid.size(), MSG_NOSIGNAL), static_cast<ssize_t>(invalid.size()));
  ASSERT_EQ(server_->DoWait(), SUCCESS);
  EXPECT_TRUE(server_->pending_handshakes_.empty());
  EXPECT_TRUE(server_->clients_.empty());
  CtrlMsgPluginPtr client;
  ConnectTls(client);
  Echo(client, "after-failed-handshake");
}

TEST_F(HixlCSServerHandshakeUt, PeerDisconnectRemovesOnlyItsPendingHandshake) {
  CtrlMsgPluginPtr client;
  ConnectTls(client);
  const int peer = AcceptSilent();
  ASSERT_GE(peer, 0);
  ASSERT_EQ(shutdown(peer, SHUT_RDWR), 0);
  ASSERT_EQ(server_->DoWait(), SUCCESS);
  EXPECT_TRUE(server_->pending_handshakes_.empty());
  EXPECT_EQ(server_->clients_.size(), 1U);
  Echo(client, "still-connected");
}

TEST_F(HixlCSServerHandshakeUt, EpollUpdateFailureClosesPendingConnection) {
  ASSERT_GE(AcceptSilent(), 0);
  auto connection = server_->pending_handshakes_.begin()->second.connection;
  const int epoll_fd = server_->epoll_fd_;
  server_->epoll_fd_ = -1;
  EXPECT_EQ(server_->AdvanceHandshake(connection->Fd()), FAILED);
  server_->epoll_fd_ = epoll_fd;
  EXPECT_TRUE(server_->pending_handshakes_.empty());
  EXPECT_EQ(connection->Fd(), -1);
  CtrlMsgPluginPtr client;
  ConnectTls(client);
  Echo(client, "after-epoll-update-failure");
}

TEST_F(HixlCSServerHandshakeUt, PendingHandshakeLimitRejectsOnlyNewConnection) {
  for (size_t i = 0; i < HixlCSServer::kMaxPendingHandshakes; ++i) {
    ASSERT_GE(AcceptSilent(), 0);
  }
  ASSERT_EQ(server_->pending_handshakes_.size(), HixlCSServer::kMaxPendingHandshakes);
  const int rejected = ConnectRaw();
  ASSERT_GE(rejected, 0);
  EXPECT_EQ(server_->AcceptClient(), RESOURCE_EXHAUSTED);
  EXPECT_EQ(server_->pending_handshakes_.size(), HixlCSServer::kMaxPendingHandshakes);
  char byte = 0;
  EXPECT_EQ(recv(rejected, &byte, 1, MSG_DONTWAIT), 0);
  server_->pending_handshakes_.begin()->second.deadline = std::chrono::steady_clock::now();
  server_->ExpireHandshakes();
  EXPECT_EQ(server_->pending_handshakes_.size(), HixlCSServer::kMaxPendingHandshakes - 1U);
  ASSERT_GE(AcceptSilent(), 0);
  EXPECT_EQ(server_->pending_handshakes_.size(), HixlCSServer::kMaxPendingHandshakes);
}

TEST_F(HixlCSServerHandshakeUt, FinalizeClosesPendingHandshakeWithoutWaitingForItsDeadline) {
  ASSERT_GE(AcceptSilent(), 0);
  ASSERT_EQ(server_->pending_handshakes_.size(), 1U);
  auto connection = server_->pending_handshakes_.begin()->second.connection;
  server_->listener_running_ = true;
  server_->listener_ = std::thread([this]() {
    while (server_->listener_running_) {
      (void)server_->DoWait();
    }
  });
  const auto start = std::chrono::steady_clock::now();
  EXPECT_EQ(server_->Finalize(), SUCCESS);
  EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(1));
  EXPECT_EQ(connection->Fd(), -1);
  EXPECT_TRUE(server_->pending_handshakes_.empty());
}

TEST_F(HixlCSServerHandshakeUt, PendingMessagesDoNotDelayTheNextEventLoopIteration) {
  CtrlMsgPluginPtr client;
  ConnectTls(client);
  const std::string payload(12288, 'x');
  const CtrlMsgHeader header{kMagicNumber, sizeof(CtrlMsgType) + payload.size()};
  const CtrlMsgType type = CtrlMsgType::kNotify;
  std::string wire(sizeof(header) + sizeof(type) + payload.size(), '\0');
  ASSERT_EQ(memcpy_s(wire.data(), wire.size(), &header, sizeof(header)), EOK);
  ASSERT_EQ(memcpy_s(wire.data() + sizeof(header), wire.size() - sizeof(header), &type, sizeof(type)), EOK);
  ASSERT_EQ(memcpy_s(wire.data() + sizeof(header) + sizeof(type), payload.size(), payload.data(), payload.size()), EOK);
  ASSERT_EQ(client->Send(wire.data(), wire.size()), SUCCESS);
  ASSERT_EQ(server_->DoWait(), SUCCESS);
  auto connection = server_->clients_.begin()->second->Connection();
  ASSERT_TRUE(connection->HasPending());
  const auto start = std::chrono::steady_clock::now();
  ASSERT_EQ(server_->DoWait(), SUCCESS);
  EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(80));
  EXPECT_TRUE(connection->HasPending());
  EXPECT_TRUE(server_->ProcessPendingMsg());
  EXPECT_TRUE(server_->ProcessPendingMsg());
  EXPECT_FALSE(connection->HasPending());
  std::string received(payload.size(), '\0');
  ASSERT_EQ(client->Recv(received.data(), received.size(), 1000), SUCCESS);
  EXPECT_EQ(received, payload);
}
}  // namespace hixl
