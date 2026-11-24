/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <gtest/gtest.h>
#include <cstdint>
#include <thread>
#include <future>
#include <chrono>
#include <fcntl.h>
#include <sys/poll.h>
#include <openssl/ssl.h>

#include "depends/ctrl_msg_test_helper.h"

#include "cs/msg_handler.h"
#include "cs/msg_receiver.h"
#include "common/scope_guard.h"
#include "common/ctrl_msg_plugin.h"
#include "common/tls/tls_test_helper.h"
#include "cs/global_config.h"
#define private public
#include "engine/hixl_client.h"
#include "cs/hixl_cs_server.h"
#undef private

namespace hixl {
class TlsCtrlMsgPluginUt : public ::testing::Test {
 protected:
  Status InitializeManager(const TlsConfig &config) {
    const auto status = TlsManager::GetInstance().Initialize(config);
    if (status == SUCCESS) {
      ++manager_users_;
    }
    return status;
  }
  void FinalizeManager() {
    while (manager_users_ != 0U) {
      --manager_users_;
      TlsManager::GetInstance().Finalize();
    }
  }

  void TearDown() override {
    FinalizeManager();
    for (const auto &dir : dirs_) {
      test::RemovePath(dir + "/cert.pem");
      test::RemovePath(dir + "/key.pem");
      test::RemovePath(dir);
    }
  }
  TlsConfig MakeConfig() {
    const auto dir = test::MakeTempDir();
    dirs_.push_back(dir);
    EXPECT_TRUE(test::GenerateSelfSignedCert(dir));
    return test::MakeTlsConfig(dir);
  }
  uint16_t ListenPort(const CtrlMsgPluginPtr &listener_connection) {
    sockaddr_in addr{};
    socklen_t len = sizeof(addr);
    EXPECT_EQ(getsockname(listener_connection->Fd(), reinterpret_cast<sockaddr *>(&addr), &len), 0);
    return ntohs(addr.sin_port);
  }
  Status ConnectPair(const TlsConfig &server_config, CtrlMsgPluginPtr &server_connection,
                     CtrlMsgPluginPtr &client_connection) {
    if (InitializeManager(server_config) != SUCCESS) {
      return FAILED;
    }
    auto listener_connection = std::make_shared<CtrlMsgPlugin>();
    if (listener_connection->Initialize() != SUCCESS || listener_connection->Listen("127.0.0.1", 0, 16) != SUCCESS) {
      return FAILED;
    }
    const auto port = ListenPort(listener_connection);
    Status server_ret = FAILED;
    std::thread accept_thread([&]() {
      pollfd event{listener_connection->Fd(), POLLIN, 0};
      if (poll(&event, 1, 5000) > 0 && listener_connection->Accept(server_connection) == SUCCESS &&
          server_connection != nullptr) {
        server_ret = server_connection->HandshakeServer();
      }
    });
    client_connection = std::make_shared<CtrlMsgPlugin>();
    Status ret = client_connection->Initialize();
    if (ret == SUCCESS) {
      ret = client_connection->Connect("127.0.0.1", port, 3000);
    }
    accept_thread.join();
    return ret == SUCCESS ? server_ret : ret;
  }
  void Exchange(const CtrlMsgPluginPtr &server_connection, const CtrlMsgPluginPtr &client_connection) {
    const char payload[] = "isolated-tls";
    ASSERT_EQ(client_connection->Send(payload, sizeof(payload)), SUCCESS);
    char received[sizeof(payload)]{};
    ASSERT_EQ(server_connection->Recv(received, sizeof(received), 3000), SUCCESS);
    EXPECT_STREQ(received, payload);
  }
  void CheckShutdownWakesReceive(const TlsConfig &config) {
    CtrlMsgPluginPtr server, client;
    ASSERT_EQ(ConnectPair(config, server, client), SUCCESS);
    const int fd = server->Fd();
    auto received = std::async(std::launch::async, [server]() {
      char byte = 0;
      return server->Recv(&byte, 1U, 60000U);
    });
    HIXL_MAKE_GUARD(wake_peer, ([client]() { (void)shutdown(client->Fd(), SHUT_RDWR); }));
    bool receiving = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
      if (!server->receive_mutex_.try_lock()) {
        receiving = true;
        break;
      }
      server->receive_mutex_.unlock();
      std::this_thread::yield();
    }
    ASSERT_TRUE(receiving);
    EXPECT_EQ(server->ShutdownSocket(), SUCCESS);
    ASSERT_EQ(received.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    EXPECT_NE(received.get(), SUCCESS);
    EXPECT_EQ(server->Fd(), fd);
    EXPECT_EQ(server->Close(), SUCCESS);
    EXPECT_EQ(server->ShutdownSocket(), SUCCESS);
  }
  struct BlockedSend {
    std::promise<void> started, release;
    std::promise<Status> finished;
    std::shared_future<void> resume = release.get_future().share();
    Status Send(const CtrlMsgPluginPtr &connection) {
      started.set_value();
      resume.wait();
      const char byte = 'x';
      const auto status = connection->Send(&byte, 1);
      finished.set_value(status);
      return status;
    }
  };
  CtrlMsgPluginPtr ReuseFd(int fd, int &peer) {
    int sockets[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) != 0) {
      return nullptr;
    }
    peer = dup(sockets[1]);
    if (dup2(sockets[0], fd) != fd) {
      return nullptr;
    }
    if (sockets[0] != fd) {
      close(sockets[0]);
    }
    if (sockets[1] != fd) {
      close(sockets[1]);
    }
    return CtrlMsgPluginTestAccess::Own(fd);
  }
  size_t manager_users_ = 0U;
  std::vector<std::string> dirs_;
};

TEST_F(TlsCtrlMsgPluginUt, ClientConnectUsesCallerTimeoutForTlsHandshake) {
  auto listener = std::make_shared<CtrlMsgPlugin>();
  const auto config = MakeConfig();
  ASSERT_EQ(InitializeManager(config), SUCCESS);
  ASSERT_EQ(listener->Initialize(), SUCCESS);
  ASSERT_EQ(listener->Listen("127.0.0.1", 0, 1), SUCCESS);
  auto client = std::make_shared<CtrlMsgPlugin>();
  ASSERT_EQ(client->Initialize(), SUCCESS);
  const auto start = std::chrono::steady_clock::now();
  EXPECT_EQ(client->Connect("127.0.0.1", ListenPort(listener), 50), TIMEOUT);
  const auto elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
  EXPECT_GE(elapsed, 40);
  EXPECT_LT(elapsed, 5000);
  EXPECT_EQ(client->Fd(), -1);
}

TEST_F(TlsCtrlMsgPluginUt, SocketShutdownWakesPlainReceiveWithoutClosingDescriptor) {
  CheckShutdownWakesReceive({});
}

TEST_F(TlsCtrlMsgPluginUt, SocketShutdownWakesTlsReceiveWithoutClosingDescriptor) {
  CheckShutdownWakesReceive(MakeConfig());
}

TEST_F(TlsCtrlMsgPluginUt, SendPassesCallerTimeoutToTlsSession) {
  CtrlMsgPluginPtr server, client;
  ASSERT_EQ(ConnectPair(MakeConfig(), server, client), SUCCESS);
  const int send_buffer_size = 4096;
  ASSERT_EQ(setsockopt(client->Fd(), SOL_SOCKET, SO_SNDBUF, &send_buffer_size, sizeof(send_buffer_size)), 0);
  const std::vector<char> payload(1024U * 1024U, 'x');
  int32_t err_no = EIO;
  const auto start = std::chrono::steady_clock::now();
  EXPECT_EQ(client->Send(payload.data(), payload.size(), 100U, err_no), TIMEOUT);
  const auto elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
  EXPECT_GE(elapsed, 90);
  EXPECT_LT(elapsed, 1000);
  EXPECT_EQ(err_no, 0);
  EXPECT_GE(client->Fd(), 0);
  EXPECT_FALSE(client->IsUsable());
  const char different[] = "different-message";
  EXPECT_EQ(client->Send(different, sizeof(different), 1000U), FAILED);
  EXPECT_EQ(client->InvalidateTlsSession(), SUCCESS);
  EXPECT_EQ(client->Close(), SUCCESS);
  EXPECT_EQ(client->Close(), SUCCESS);
  CtrlMsgPluginPtr new_server, new_client;
  ASSERT_EQ(ConnectPair(TlsManager::GetInstance().GetConfig(), new_server, new_client), SUCCESS);
  ASSERT_NO_FATAL_FAILURE(Exchange(new_server, new_client));
}

TEST_F(TlsCtrlMsgPluginUt, HeartbeatRejectsInvalidSessionBeforeSocketWritableCheck) {
  CtrlMsgPluginPtr server, connection;
  ASSERT_EQ(ConnectPair(MakeConfig(), server, connection), SUCCESS);
  const int fd = connection->Fd();
  // Leave the kernel socket open; the heartbeat must inspect the TLS state independently of poll().
  CtrlMsgPluginTestAccess::Session(connection)->Invalidate();
  ClientConfig config{};
  HixlClient client("127.0.0.1", 0U, config);
  client.connection_ = connection;
  EXPECT_EQ(client.CheckAlive(), FAILED);
  EXPECT_EQ(client.connection_, nullptr);
  EXPECT_EQ(connection->Fd(), -1);
  EXPECT_EQ(fcntl(fd, F_GETFD), -1);
}

TEST_F(TlsCtrlMsgPluginUt, FailedResponseSchedulesServerConnectionCleanup) {
  CtrlMsgPluginPtr peer, connection;
  ASSERT_EQ(ConnectPair(MakeConfig(), peer, connection), SUCCESS);
  HixlCSServer server("127.0.0.1", 0U, GlobalConfig{});
  ASSERT_EQ(server.msg_handler_.Initialize(), SUCCESS);
  const int fd = connection->Fd();
  const int buffer_size = 4096;
  ASSERT_EQ(setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &buffer_size, sizeof(buffer_size)), 0);
  ASSERT_EQ(connection->AddToEpoll(server.epoll_fd_), SUCCESS);
  server.clients_[fd] = std::make_shared<MsgReceiver>(connection);
  const std::string response(1024U * 1024U, 'x');
  ASSERT_EQ(server.RegProc(CtrlMsgType::kNotify,
                           [&response](const CtrlMsgPluginPtr &conn, const char *, uint64_t) {
                             return conn->Send(response.data(), response.size(), 50U);
                           }),
            SUCCESS);
  auto msg = std::make_shared<CtrlMsg>();
  msg->msg_type = CtrlMsgType::kNotify;
  auto processor = server.msg_handler_.processors_.at(msg->msg_type);
  EXPECT_EQ(processor(connection, nullptr, 0U), TIMEOUT);
  EXPECT_EQ(server.Finalize(), SUCCESS);
  EXPECT_TRUE(server.clients_.empty());
  EXPECT_EQ(connection->Fd(), -1);
}

TEST_F(TlsCtrlMsgPluginUt, HeartbeatClosesSessionAfterFatalTlsWriteWithoutSocketErrno) {
  CtrlMsgPluginPtr peer, connection;
  ASSERT_EQ(ConnectPair(MakeConfig(), peer, connection), SUCCESS);
  const int buffer_size = 4096;
  ASSERT_EQ(setsockopt(connection->Fd(), SOL_SOCKET, SO_SNDBUF, &buffer_size, sizeof(buffer_size)), 0);
  const std::vector<char> payload(1024U * 1024U, 'x');
  ssize_t sent = 0;
  int ssl_error = SSL_ERROR_NONE;
  int32_t err_no = 0;
  ASSERT_EQ(
      CtrlMsgPluginTestAccess::Session(connection)->WriteOnce(payload.data(), payload.size(), sent, ssl_error, err_no),
      SUCCESS);
  ASSERT_EQ(ssl_error, SSL_ERROR_WANT_WRITE);
  ASSERT_EQ(sent, -1);
  ASSERT_TRUE(connection->IsUsable());
  // Drain ciphertext to make the socket writable without completing the pending SSL_write.
  char byte = 0;
  ssize_t received = 0;
  ASSERT_EQ(peer->Recv(&byte, 1U, received), SUCCESS);
  pollfd writable{connection->Fd(), POLLOUT, 0};
  ASSERT_EQ(poll(&writable, 1, 1000), 1);
  ClientConfig config{};
  HixlClient client("127.0.0.1", 0U, config);
  client.connection_ = connection;
  EXPECT_EQ(client.CheckAlive(), FAILED);
  EXPECT_EQ(client.connection_, nullptr);
  EXPECT_EQ(connection->Fd(), -1);
}

TEST_F(TlsCtrlMsgPluginUt, SharedContextAndIndependentSessions) {
  const auto config = MakeConfig();
  CtrlMsgPluginPtr server_a, client_a, server_b, client_b;
  ASSERT_EQ(ConnectPair(config, server_a, client_a), SUCCESS);
  const auto context = TlsManager::GetInstance().GetContext();
  ASSERT_EQ(ConnectPair(config, server_b, client_b), SUCCESS);
  EXPECT_EQ(TlsManager::GetInstance().GetContext(), context);
  EXPECT_NE(CtrlMsgPluginTestAccess::Session(server_a), CtrlMsgPluginTestAccess::Session(server_b));
  EXPECT_EQ(TlsManager::GetInstance().GetConfig().ca_file, config.ca_file);
  // ConnectPair already destroyed both listeners; the engine-level owner retains the context.
  Exchange(server_a, client_a);
  Exchange(server_b, client_b);
  ASSERT_EQ(client_a->Close(), SUCCESS);
  ASSERT_EQ(server_a->Close(), SUCCESS);
  EXPECT_FALSE(CtrlMsgPluginTestAccess::IsTlsEnabled(server_a));
  EXPECT_EQ(TlsManager::GetInstance().GetContext(), context);
  Exchange(server_b, client_b);
  ASSERT_EQ(client_b->Close(), SUCCESS);
  ASSERT_EQ(server_b->Close(), SUCCESS);
  EXPECT_EQ(TlsManager::GetInstance().GetContext(), context);
  FinalizeManager();
  EXPECT_EQ(TlsManager::GetInstance().GetContext(), nullptr);
}

TEST_F(TlsCtrlMsgPluginUt, ConflictingConfigurationDoesNotAffectActiveConnections) {
  const auto config = MakeConfig();
  CtrlMsgPluginPtr server, client;
  ASSERT_EQ(ConnectPair(config, server, client), SUCCESS);
  CtrlMsgPlugin conflict;
  EXPECT_EQ(InitializeManager(MakeConfig()), PARAM_INVALID);
  EXPECT_EQ(InitializeManager({}), PARAM_INVALID);
  EXPECT_EQ(conflict.Listen("127.0.0.1", 0, 1), FAILED);
  EXPECT_EQ(conflict.Close(), SUCCESS);
  EXPECT_EQ(client->Initialize(), FAILED);
  Exchange(server, client);
  ASSERT_EQ(client->Close(), SUCCESS);
  ASSERT_EQ(server->Close(), SUCCESS);
  FinalizeManager();
  EXPECT_EQ(TlsManager::GetInstance().GetContext(), nullptr);
  ASSERT_EQ(InitializeManager({}), SUCCESS);
  ASSERT_EQ(conflict.Initialize(), SUCCESS);
  EXPECT_EQ(conflict.Listen("127.0.0.1", 0, 1), SUCCESS);
}

TEST_F(TlsCtrlMsgPluginUt, TlsPluginRequiresOwnerInitializationAndNeverInitializesManager) {
  const auto config = MakeConfig();
  EXPECT_FALSE(TlsManager::GetInstance().IsInitialized());
  CtrlMsgPlugin connection;
  EXPECT_EQ(connection.Initialize(), FAILED);
  EXPECT_FALSE(TlsManager::GetInstance().IsInitialized());
  ASSERT_EQ(InitializeManager(config), SUCCESS);
  const auto context = TlsManager::GetInstance().GetContext();
  ASSERT_EQ(connection.Initialize(), SUCCESS);
  for (size_t i = 0; i < 3U; ++i) {
    auto other = std::make_shared<CtrlMsgPlugin>();
    ASSERT_EQ(other->Initialize(), SUCCESS);
    EXPECT_TRUE(CtrlMsgPluginTestAccess::IsTlsEnabled(other));
    ASSERT_EQ(other->Close(), SUCCESS);
    EXPECT_TRUE(TlsManager::GetInstance().IsInitialized());
  }
  ASSERT_EQ(connection.Close(), SUCCESS);
  EXPECT_EQ(TlsManager::GetInstance().GetContext(), context);
  FinalizeManager();
  EXPECT_FALSE(TlsManager::GetInstance().IsInitialized());
}

TEST_F(TlsCtrlMsgPluginUt, HasPendingTracksTlsSession) {
  const auto config = MakeConfig();
  CtrlMsgPluginPtr server_connection, client_connection;
  ASSERT_EQ(ConnectPair(config, server_connection, client_connection), SUCCESS);
  const std::string payload(64, 'b');
  ASSERT_EQ(client_connection->Send(payload.data(), payload.size()), SUCCESS);
  char half[32]{};
  ssize_t received = 0;
  ASSERT_EQ(server_connection->Recv(half, sizeof(half), received), SUCCESS);
  ASSERT_EQ(received, 32);
  EXPECT_TRUE(server_connection->HasPending());
  ASSERT_EQ(server_connection->Recv(half, sizeof(half), received), SUCCESS);
  EXPECT_EQ(received, 32);
  EXPECT_FALSE(server_connection->HasPending());
}

TEST_F(TlsCtrlMsgPluginUt, ReceiverPropagatesFatalTlsRecordError) {
  const auto config = MakeConfig();
  CtrlMsgPluginPtr server_connection, client_connection;
  ASSERT_EQ(ConnectPair(config, server_connection, client_connection), SUCCESS);
  const uint8_t invalid_record[] = {0x17, 0x03, 0x03, 0x00, 0x01, 0x00};
  ASSERT_EQ(send(client_connection->Fd(), invalid_record, sizeof(invalid_record), MSG_NOSIGNAL),
            static_cast<ssize_t>(sizeof(invalid_record)));
  pollfd event{server_connection->Fd(), POLLIN, 0};
  ASSERT_EQ(poll(&event, 1, 2000), 1);

  MsgReceiver receiver(server_connection);
  std::vector<CtrlMsgPtr> msgs;
  EXPECT_EQ(receiver.IRecv(msgs), FAILED);
  EXPECT_TRUE(msgs.empty());
}

TEST_F(TlsCtrlMsgPluginUt, MissingSessionDoesNotFallBackToPlaintext) {
  const auto config = MakeConfig();
  int sockets[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
  HIXL_MAKE_GUARD(close_sockets, ([&sockets]() {
                    (void)close(sockets[0]);
                    (void)close(sockets[1]);
                  }));
  ASSERT_EQ(InitializeManager(config), SUCCESS);
  auto server_connection = CtrlMsgPluginTestAccess::Own(dup(sockets[0]));
  ASSERT_NE(server_connection, nullptr);
  ASSERT_GE(server_connection->Fd(), 0);
  ASSERT_TRUE(CtrlMsgPluginTestAccess::IsTlsEnabled(server_connection));
  ASSERT_EQ(CtrlMsgPluginTestAccess::Session(server_connection), nullptr);
  char byte = 0;
  EXPECT_EQ(server_connection->Send(&byte, 1), FAILED);
  EXPECT_EQ(server_connection->Recv(&byte, 1, 1), FAILED);
  ssize_t received = 0;
  EXPECT_EQ(server_connection->Recv(&byte, 1, received), FAILED);
}

TEST_F(TlsCtrlMsgPluginUt, RepeatedTlsHandshakePreservesEstablishedConnection) {
  const auto config = MakeConfig();
  CtrlMsgPluginPtr server_connection, client_connection;
  ASSERT_EQ(ConnectPair(config, server_connection, client_connection), SUCCESS);
  const int fd = server_connection->Fd();
  const auto session = CtrlMsgPluginTestAccess::Session(server_connection);
  ASSERT_NE(session, nullptr);
  EXPECT_EQ(server_connection->HandshakeServer(), FAILED);
  EXPECT_EQ(server_connection->Fd(), fd);
  EXPECT_EQ(CtrlMsgPluginTestAccess::Session(server_connection), session);
  Exchange(server_connection, client_connection);
}

TEST_F(TlsCtrlMsgPluginUt, RepeatedSocketCreationPreservesExistingConnection) {
  CtrlMsgPluginPtr server_connection, client_connection;
  ASSERT_EQ(ConnectPair({}, server_connection, client_connection), SUCCESS);
  const int fd = client_connection->Fd();
  EXPECT_EQ(client_connection->Connect("127.0.0.1", 0, 100), FAILED);
  EXPECT_EQ(client_connection->Listen("127.0.0.1", 0, 1), FAILED);
  EXPECT_EQ(client_connection->Fd(), fd);
  Exchange(server_connection, client_connection);

  CtrlMsgPlugin listener;
  ASSERT_EQ(listener.Initialize(), SUCCESS);
  ASSERT_EQ(listener.Listen("127.0.0.1", 0, 1), SUCCESS);
  const int listener_fd = listener.Fd();
  EXPECT_EQ(listener.Listen("127.0.0.1", 0, 1), FAILED);
  EXPECT_EQ(listener.Connect("127.0.0.1", 0, 100), FAILED);
  EXPECT_EQ(listener.Fd(), listener_fd);
  EXPECT_NE(fcntl(listener_fd, F_GETFD), -1);
}

TEST_F(TlsCtrlMsgPluginUt, FailedServerHandshakeClosesSocket) {
  const auto config = MakeConfig();
  int sockets[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
  HIXL_MAKE_GUARD(close_sockets, ([&sockets]() {
                    (void)close(sockets[0]);
                    (void)close(sockets[1]);
                  }));
  ASSERT_EQ(InitializeManager(config), SUCCESS);
  auto connection = CtrlMsgPluginTestAccess::Own(dup(sockets[0]));
  ASSERT_NE(connection, nullptr);
  ASSERT_GE(connection->Fd(), 0);
  const int fd = connection->Fd();
  ASSERT_EQ(shutdown(sockets[1], SHUT_RDWR), 0);
  EXPECT_NE(connection->HandshakeServer(), SUCCESS);
  EXPECT_EQ(connection->Fd(), -1);
  EXPECT_EQ(fcntl(fd, F_GETFD), -1);
  EXPECT_FALSE(CtrlMsgPluginTestAccess::IsInitialized(connection));
  EXPECT_EQ(connection->Listen("127.0.0.1", 0, 1), FAILED);
}

TEST_F(TlsCtrlMsgPluginUt, ClosedConnectionCannotAffectReusedDescriptor) {
  ASSERT_EQ(InitializeManager({}), SUCCESS);
  int sockets[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
  auto old_connection = CtrlMsgPluginTestAccess::Own(sockets[0]);
  const int reused_fd = sockets[0];
  ASSERT_EQ(old_connection->Close(), SUCCESS);
  ASSERT_EQ(dup2(sockets[1], reused_fd), reused_fd);
  EXPECT_EQ(old_connection->Close(), SUCCESS);
  char byte = 'x';
  EXPECT_EQ(old_connection->Send(&byte, 1), FAILED);
  EXPECT_NE(fcntl(reused_fd, F_GETFD), -1);
  close(reused_fd);
  close(sockets[1]);
}

TEST_F(TlsCtrlMsgPluginUt, QueuedTaskRetainsOldConnectionAfterFdReuse) {
  ASSERT_EQ(InitializeManager({}), SUCCESS);
  int original[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, original), 0);
  auto old_connection = CtrlMsgPluginTestAccess::Own(original[0]);
  const int reused_fd = original[0];
  close(original[1]);
  BlockedSend send;
  auto ready = send.started.get_future();
  auto result = send.finished.get_future();
  MsgHandler handler;
  ASSERT_EQ(handler.Initialize(), SUCCESS);
  bool released = false;
  HIXL_MAKE_GUARD(unblock, ([&]() {
                    if (!released) {
                      send.release.set_value();
                    }
                    handler.Finalize();
                  }));
  ASSERT_EQ(handler.RegisterMsgProcessor(CtrlMsgType::kNotify, [&](const CtrlMsgPluginPtr &connection, const char *,
                                                                   uint64_t) { return send.Send(connection); }),
            SUCCESS);
  auto message = std::make_shared<CtrlMsg>();
  message->msg_type = CtrlMsgType::kNotify;
  handler.SubmitMsg(old_connection, message);
  ASSERT_EQ(ready.wait_for(std::chrono::seconds(5)), std::future_status::ready);
  ASSERT_EQ(old_connection->Close(), SUCCESS);
  int peer = -1;
  auto current_connection = ReuseFd(reused_fd, peer);
  ASSERT_NE(current_connection, nullptr);
  ASSERT_GE(peer, 0);
  HIXL_MAKE_GUARD(close_peer, ([peer]() { close(peer); }));
  EXPECT_EQ(old_connection->Fd(), -1);
  EXPECT_EQ(current_connection->Fd(), reused_fd);
  send.release.set_value();
  released = true;
  ASSERT_EQ(result.wait_for(std::chrono::seconds(5)), std::future_status::ready);
  EXPECT_EQ(result.get(), FAILED);
  const char byte = 'y';
  ASSERT_EQ(current_connection->Send(&byte, 1), SUCCESS);
  char received = 0;
  ASSERT_EQ(read(peer, &received, 1), 1);
  EXPECT_EQ(received, byte);
}

TEST_F(TlsCtrlMsgPluginUt, GlobalConfigRejectsUnpairedCert) {
  const char *json = R"({"tls_config.cert_file":"/tmp/a.pem"})";
  GlobalConfig cfg;
  EXPECT_EQ(GlobalConfig::Parse(json, cfg), PARAM_INVALID);
}
}  // namespace hixl
