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

#include <cerrno>
#include <memory>
#include <string>
#include <thread>
#include <chrono>
#include <cstring>
#include <vector>
#include <limits>
#include <sys/socket.h>
#include <unistd.h>

#include <openssl/err.h>
#include <openssl/ssl.h>

#include "common/tls/tls_context.h"
#include "common/tls/tls_session.h"
#include "common/tls/tls_openssl_utils.h"
#include "common/scope_guard.h"
#include "common/tls/tls_test_helper.h"
#include "hixl/hixl_types.h"

namespace hixl {

class TlsSessionUt : public ::testing::Test {
 protected:
  void SetUp() override {
    dir_ = test::MakeTempDir();
    ASSERT_FALSE(dir_.empty());
    if (!test::GenerateSelfSignedCert(dir_)) {
      GTEST_SKIP() << "openssl is not available to generate test certificates";
    }
    ASSERT_EQ(TlsContext::Create(test::MakeTlsConfig(dir_), ctx_), SUCCESS);
  }

  void TearDown() override {
    ERR_clear_error();
    ctx_.reset();
    test::RemovePath(dir_ + "/cert.pem");
    test::RemovePath(dir_ + "/key.pem");
    test::RemovePath(dir_);
  }

  void ConnectSessions(int (&fds)[2], std::shared_ptr<TlsSession> &server_session,
                       std::shared_ptr<TlsSession> &client_session) {
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
    ASSERT_EQ(TlsContext::CreateSession(ctx_, fds[0], true, server_session), SUCCESS);
    ASSERT_EQ(TlsContext::CreateSession(ctx_, fds[1], false, client_session), SUCCESS);
    Status server_ret = FAILED;
    std::thread server([&]() { server_ret = server_session->HandshakeAsServer(2000); });
    const auto client_ret = client_session->HandshakeAsClient(2000);
    server.join();
    ASSERT_EQ(client_ret, SUCCESS);
    ASSERT_EQ(server_ret, SUCCESS);
  }

  void CheckRecvBudgetAfterReadDelay(std::chrono::milliseconds delay) {
    int fds[2] = {-1, -1};
    HIXL_MAKE_GUARD(close_fds, ([&]() {
                      (void)close(fds[0]);
                      (void)close(fds[1]);
                    }));
    std::shared_ptr<TlsSession> server;
    std::shared_ptr<TlsSession> client;
    ASSERT_NO_FATAL_FAILURE(ConnectSessions(fds, server, client));
    struct ReadDelay {
      std::chrono::milliseconds duration;
      std::chrono::steady_clock::time_point finished;
      bool applied = false;
    } read_delay{delay, {}};
    BIO *bio = SSL_get_rbio(server->ssl_);
    ASSERT_NE(bio, nullptr);
    BIO_set_callback_arg(bio, reinterpret_cast<char *>(&read_delay));
    BIO_set_callback_ex(bio, [](BIO *b, int operation, const char *, size_t, int, long, int result, size_t *) -> long {
      auto &state = *reinterpret_cast<ReadDelay *>(BIO_get_callback_arg(b));
      if (operation == (BIO_CB_READ | BIO_CB_RETURN) && result <= 0 && !state.applied) {
        std::this_thread::sleep_for(state.duration);
        state.finished = std::chrono::steady_clock::now();
        state.applied = true;
      }
      return result;
    });
    HIXL_MAKE_GUARD(clear_callback, ([bio]() {
                      BIO_set_callback_ex(bio, nullptr);
                      BIO_set_callback_arg(bio, nullptr);
                    }));
    constexpr uint32_t kTimeoutMs = 1000U;
    constexpr int64_t kMaxWaitAfterReadMs = 700;
    char byte = 0;
    const auto start = std::chrono::steady_clock::now();
    EXPECT_EQ(server->Recv(&byte, sizeof(byte), kTimeoutMs), TIMEOUT);
    const auto finished = std::chrono::steady_clock::now();
    ASSERT_TRUE(read_delay.applied);
    EXPECT_GE(finished - start, std::chrono::milliseconds(kTimeoutMs - 10U));
    EXPECT_LT(finished - read_delay.finished, std::chrono::milliseconds(kMaxWaitAfterReadMs));
    EXPECT_TRUE(server->IsUsable());
  }

  std::string dir_;
  std::shared_ptr<TlsContext> ctx_;
};

TEST_F(TlsSessionUt, HandshakeAndEcho) {
  int fds[2] = {-1, -1};
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  std::shared_ptr<TlsSession> server_session;
  std::shared_ptr<TlsSession> client_session;
  ASSERT_EQ(TlsContext::CreateSession(ctx_, fds[0], true, server_session), SUCCESS);
  ASSERT_EQ(TlsContext::CreateSession(ctx_, fds[1], false, client_session), SUCCESS);

  Status server_ret = FAILED;
  std::thread server_th([&]() { server_ret = server_session->HandshakeAsServer(2000); });
  EXPECT_EQ(client_session->HandshakeAsClient(2000), SUCCESS);
  server_th.join();
  EXPECT_EQ(server_ret, SUCCESS);

  const char payload[] = "hixl-tls";
  int32_t err_no = 0;
  EXPECT_EQ(client_session->Send(payload, sizeof(payload), 1000U, err_no), SUCCESS);
  char recv_buf[sizeof(payload)] = {};
  EXPECT_EQ(server_session->Recv(recv_buf, sizeof(payload), 2000), SUCCESS);
  EXPECT_STREQ(recv_buf, payload);

  close(fds[0]);
  close(fds[1]);
}

TEST_F(TlsSessionUt, HandshakeUsesCallerTimeout) {
  for (const bool is_server : {false, true}) {
    SCOPED_TRACE(is_server);
    int fds[2] = {-1, -1};
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
    HIXL_MAKE_GUARD(close_fds, ([&]() {
                      (void)close(fds[0]);
                      (void)close(fds[1]);
                    }));
    std::shared_ptr<TlsSession> session;
    ASSERT_EQ(TlsContext::CreateSession(ctx_, fds[0], is_server, session), SUCCESS);
    ERR_raise(ERR_LIB_USER, 1);
    errno = EIO;
    const auto start = std::chrono::steady_clock::now();
    EXPECT_EQ(is_server ? session->HandshakeAsServer(50) : session->HandshakeAsClient(50), TIMEOUT);
    EXPECT_EQ(ERR_peek_error(), 0UL);
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    EXPECT_GE(elapsed, 40);
    EXPECT_LT(elapsed, 5000);
  }
}

TEST_F(TlsSessionUt, RejectsNonPositiveHandshakeTimeout) {
  int fds[2] = {-1, -1};
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  HIXL_MAKE_GUARD(close_fds, ([&]() {
                    (void)close(fds[0]);
                    (void)close(fds[1]);
                  }));
  std::shared_ptr<TlsSession> session;
  ASSERT_EQ(TlsContext::CreateSession(ctx_, fds[0], false, session), SUCCESS);
  EXPECT_EQ(session->HandshakeAsClient(0), PARAM_INVALID);
  EXPECT_EQ(session->HandshakeAsClient(-1), PARAM_INVALID);
}

TEST_F(TlsSessionUt, ServerHandshakeStepYieldsForSilentPeer) {
  int fds[2] = {-1, -1};
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  HIXL_MAKE_GUARD(close_fds, ([&]() {
                    (void)close(fds[0]);
                    (void)close(fds[1]);
                  }));
  std::shared_ptr<TlsSession> session;
  ASSERT_EQ(TlsContext::CreateSession(ctx_, fds[0], true, session), SUCCESS);
  TlsHandshakeState state;
  const auto start = std::chrono::steady_clock::now();
  ASSERT_EQ(session->HandshakeAsServerStep(state), SUCCESS);
  EXPECT_EQ(state, TlsHandshakeState::kWantRead);
  EXPECT_FALSE(session->IsHandshakeComplete());
  EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(1));
  ASSERT_EQ(session->HandshakeAsServerStep(state), SUCCESS);
  EXPECT_EQ(state, TlsHandshakeState::kWantRead);
}

TEST_F(TlsSessionUt, ServerHandshakeStepReportsWriteBackpressure) {
  SSL_CTX *ctx = SSL_CTX_new(TLS_method());
  ASSERT_NE(ctx, nullptr);
  HIXL_MAKE_GUARD(free_ctx, ([ctx]() { SSL_CTX_free(ctx); }));
  ASSERT_EQ(SSL_CTX_use_certificate_chain_file(ctx, (dir_ + "/cert.pem").c_str()), 1);
  ASSERT_EQ(SSL_CTX_use_PrivateKey_file(ctx, (dir_ + "/key.pem").c_str(), SSL_FILETYPE_PEM), 1);
  SSL *server = SSL_new(ctx);
  ASSERT_NE(server, nullptr);
  TlsSession session(server, nullptr, -1);
  BIO *server_bio = nullptr;
  BIO *peer_bio = nullptr;
  ASSERT_EQ(BIO_new_bio_pair(&server_bio, 1, &peer_bio, 16384), 1);
  SSL_set_bio(server, server_bio, server_bio);
  HIXL_MAKE_GUARD(free_peer, ([peer_bio]() { BIO_free(peer_bio); }));
  SSL *client = SSL_new(ctx);
  ASSERT_NE(client, nullptr);
  HIXL_MAKE_GUARD(free_client, ([client]() { SSL_free(client); }));
  SSL_set_bio(client, BIO_new(BIO_s_mem()), BIO_new(BIO_s_mem()));
  EXPECT_LE(SSL_connect(client), 0);
  char hello[16384];
  const int size = BIO_read(SSL_get_wbio(client), hello, sizeof(hello));
  ASSERT_GT(size, 0);
  ASSERT_EQ(BIO_write(peer_bio, hello, size), size);
  TlsHandshakeState state;
  ASSERT_EQ(session.HandshakeAsServerStep(state), SUCCESS);
  EXPECT_EQ(state, TlsHandshakeState::kWantWrite);
  EXPECT_FALSE(session.IsHandshakeComplete());
}

TEST_F(TlsSessionUt, SendUsesCallerTimeoutWhenPeerDoesNotRead) {
  int fds[2] = {-1, -1};
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  HIXL_MAKE_GUARD(close_fds, ([&]() {
                    (void)close(fds[0]);
                    (void)close(fds[1]);
                  }));
  std::shared_ptr<TlsSession> server_session;
  std::shared_ptr<TlsSession> client_session;
  ASSERT_EQ(TlsContext::CreateSession(ctx_, fds[0], true, server_session), SUCCESS);
  ASSERT_EQ(TlsContext::CreateSession(ctx_, fds[1], false, client_session), SUCCESS);
  Status server_ret = FAILED;
  std::thread server([&]() { server_ret = server_session->HandshakeAsServer(2000); });
  const auto client_ret = client_session->HandshakeAsClient(2000);
  server.join();
  ASSERT_EQ(client_ret, SUCCESS);
  ASSERT_EQ(server_ret, SUCCESS);
  const int send_buffer_size = 4096;
  ASSERT_EQ(setsockopt(fds[1], SOL_SOCKET, SO_SNDBUF, &send_buffer_size, sizeof(send_buffer_size)), 0);
  const std::vector<char> payload(1024U * 1024U, 'x');
  int32_t err_no = 0;
  const auto start = std::chrono::steady_clock::now();
  ERR_raise(ERR_LIB_USER, 1);
  errno = EIO;
  EXPECT_EQ(client_session->Send(payload.data(), payload.size(), 100U, err_no), TIMEOUT);
  EXPECT_EQ(ERR_peek_error(), 0UL);
  EXPECT_EQ(err_no, 0);
  const auto elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
  EXPECT_GE(elapsed, 90);
  EXPECT_LT(elapsed, 1000);
  EXPECT_TRUE(client_session->IsHandshakeComplete());
  EXPECT_FALSE(client_session->IsUsable());
  const char different[] = "next-message";
  const auto retry_start = std::chrono::steady_clock::now();
  EXPECT_EQ(client_session->Send(different, sizeof(different), 1000U, err_no), FAILED);
  EXPECT_LT(std::chrono::steady_clock::now() - retry_start, std::chrono::milliseconds(100));
  ssize_t n = 0;
  char byte = 0;
  int ssl_error = SSL_ERROR_NONE;
  EXPECT_EQ(client_session->WriteOnce(different, sizeof(different), n, ssl_error, err_no), FAILED);
  EXPECT_EQ(n, -1);
  EXPECT_EQ(client_session->RecvOnce(&byte, 1U, n), FAILED);
  EXPECT_EQ(client_session->Recv(&byte, 1U, 1000U), FAILED);
  EXPECT_FALSE(client_session->HasPending());
}

TEST_F(TlsSessionUt, ZeroSendTimeoutDoesNotWrite) {
  int fds[2] = {-1, -1};
  HIXL_MAKE_GUARD(close_fds, ([&]() {
                    (void)close(fds[0]);
                    (void)close(fds[1]);
                  }));
  std::shared_ptr<TlsSession> server_session, client_session;
  ASSERT_NO_FATAL_FAILURE(ConnectSessions(fds, server_session, client_session));
  char byte = 'x';
  int32_t err_no = EIO;
  EXPECT_EQ(client_session->Send(&byte, 1, 0U, err_no), TIMEOUT);
  EXPECT_EQ(err_no, 0);
  EXPECT_EQ(client_session->Send(nullptr, 0, 0U, err_no), SUCCESS);
  EXPECT_EQ(server_session->Recv(&byte, 1, 30U), TIMEOUT);
  EXPECT_TRUE(client_session->IsUsable());
  EXPECT_EQ(client_session->Send(&byte, 1U, 1000U, err_no), SUCCESS);
  EXPECT_EQ(server_session->Recv(&byte, 1U, 1000U), SUCCESS);
}

TEST_F(TlsSessionUt, WantWriteCanResumeWithSameBufferWithinDeadline) {
  int fds[2] = {-1, -1};
  HIXL_MAKE_GUARD(close_fds, ([&]() {
                    (void)close(fds[0]);
                    (void)close(fds[1]);
                  }));
  std::shared_ptr<TlsSession> server, client;
  ASSERT_NO_FATAL_FAILURE(ConnectSessions(fds, server, client));
  const int send_buffer_size = 4096;
  ASSERT_EQ(setsockopt(fds[1], SOL_SOCKET, SO_SNDBUF, &send_buffer_size, sizeof(send_buffer_size)), 0);
  const std::vector<char> payload(1024U * 1024U, 'x');
  ssize_t sent = 0;
  int ssl_error = SSL_ERROR_NONE;
  int32_t err_no = 0;
  ASSERT_EQ(client->WriteOnce(payload.data(), payload.size(), sent, ssl_error, err_no), SUCCESS);
  ASSERT_EQ(sent, -1);
  ASSERT_EQ(ssl_error, SSL_ERROR_WANT_WRITE);
  EXPECT_TRUE(client->IsUsable());
  std::vector<char> received(payload.size());
  Status receive_status = FAILED;
  std::thread reader([&]() { receive_status = server->Recv(received.data(), received.size(), 3000U); });
  const Status send_status = client->Send(payload.data(), payload.size(), 3000U, err_no);
  reader.join();
  EXPECT_EQ(send_status, SUCCESS);
  EXPECT_EQ(receive_status, SUCCESS);
  EXPECT_EQ(received, payload);
  EXPECT_TRUE(client->IsUsable());
}

TEST_F(TlsSessionUt, OversizedWriteIsRejectedBeforeCallingOpenSsl) {
  int fds[2] = {-1, -1};
  HIXL_MAKE_GUARD(close_fds, ([&]() {
                    (void)close(fds[0]);
                    (void)close(fds[1]);
                  }));
  std::shared_ptr<TlsSession> server, client;
  ASSERT_NO_FATAL_FAILURE(ConnectSessions(fds, server, client));
  const char byte = 'x';
  const size_t size = static_cast<size_t>(std::numeric_limits<int>::max()) + 1U;
  int32_t err_no = 0;
  EXPECT_EQ(client->Send(&byte, size, 1000U, err_no), PARAM_INVALID);
  ssize_t sent = 0;
  int ssl_error = SSL_ERROR_NONE;
  EXPECT_EQ(client->WriteOnce(&byte, size, sent, ssl_error, err_no), PARAM_INVALID);
  EXPECT_EQ(sent, -1);
  EXPECT_TRUE(client->IsUsable());
}

TEST_F(TlsSessionUt, OversizedReadIsRejectedBeforeCallingOpenSsl) {
  int fds[2] = {-1, -1};
  HIXL_MAKE_GUARD(close_fds, ([&]() {
                    (void)close(fds[0]);
                    (void)close(fds[1]);
                  }));
  std::shared_ptr<TlsSession> server, client;
  ASSERT_NO_FATAL_FAILURE(ConnectSessions(fds, server, client));
  char byte = 'x';
  const size_t size = static_cast<size_t>(std::numeric_limits<int>::max()) + 1U;
  EXPECT_EQ(server->Recv(&byte, size, 1000U), PARAM_INVALID);
  ssize_t received = 0;
  EXPECT_EQ(server->RecvOnce(&byte, size, received), PARAM_INVALID);
  EXPECT_EQ(received, -1);
  EXPECT_TRUE(server->IsUsable());
}

TEST_F(TlsSessionUt, RecvTimeout) {
  int fds[2] = {-1, -1};
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  std::shared_ptr<TlsSession> server_session;
  std::shared_ptr<TlsSession> client_session;
  ASSERT_EQ(TlsContext::CreateSession(ctx_, fds[0], true, server_session), SUCCESS);
  ASSERT_EQ(TlsContext::CreateSession(ctx_, fds[1], false, client_session), SUCCESS);
  Status server_ret = FAILED;
  std::thread server_th([&]() { server_ret = server_session->HandshakeAsServer(2000); });
  ASSERT_EQ(client_session->HandshakeAsClient(2000), SUCCESS);
  server_th.join();
  ASSERT_EQ(server_ret, SUCCESS);

  char buf[8] = {};
  ERR_raise(ERR_LIB_USER, 1);
  errno = EIO;
  EXPECT_EQ(server_session->Recv(buf, sizeof(buf), 50), TIMEOUT);
  EXPECT_EQ(ERR_peek_error(), 0UL);
  close(fds[0]);
  close(fds[1]);
}

TEST_F(TlsSessionUt, RecvDeductsSslReadTimeFromPollBudget) {
  CheckRecvBudgetAfterReadDelay(std::chrono::milliseconds(600));
}

TEST_F(TlsSessionUt, RecvDoesNotPollAfterSslReadExhaustsBudget) {
  CheckRecvBudgetAfterReadDelay(std::chrono::milliseconds(1200));
}

TEST_F(TlsSessionUt, HasPendingReflectsSslBuffer) {
  int fds[2] = {-1, -1};
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  std::shared_ptr<TlsSession> server_session;
  std::shared_ptr<TlsSession> client_session;
  ASSERT_EQ(TlsContext::CreateSession(ctx_, fds[0], true, server_session), SUCCESS);
  ASSERT_EQ(TlsContext::CreateSession(ctx_, fds[1], false, client_session), SUCCESS);
  Status server_hs = FAILED;
  std::thread server_th([&]() { server_hs = server_session->HandshakeAsServer(2000); });
  ASSERT_EQ(client_session->HandshakeAsClient(2000), SUCCESS);
  server_th.join();
  ASSERT_EQ(server_hs, SUCCESS);

  EXPECT_FALSE(server_session->HasPending());
  char payload[64] = {};
  (void)memset(payload, 'a', sizeof(payload));
  int32_t err_no = 0;
  ASSERT_EQ(client_session->Send(payload, sizeof(payload), 1000U, err_no), SUCCESS);

  char half[32] = {};
  ssize_t n = 0;
  ASSERT_EQ(server_session->RecvOnce(half, sizeof(half), n), SUCCESS);
  ASSERT_EQ(n, static_cast<ssize_t>(sizeof(half)));
  EXPECT_TRUE(server_session->HasPending());

  char rest[32] = {};
  ASSERT_EQ(server_session->RecvOnce(rest, sizeof(rest), n), SUCCESS);
  ASSERT_EQ(n, static_cast<ssize_t>(sizeof(rest)));
  EXPECT_FALSE(server_session->HasPending());
  EXPECT_EQ(memcmp(half, payload, sizeof(half)), 0);
  EXPECT_EQ(memcmp(rest, payload + sizeof(half), sizeof(rest)), 0);

  close(fds[0]);
  close(fds[1]);
}

TEST_F(TlsSessionUt, RecvOnceIgnoresStaleErrorQueue) {
  int fds[2] = {-1, -1};
  HIXL_MAKE_GUARD(close_fds, ([&]() {
                    (void)close(fds[0]);
                    (void)close(fds[1]);
                  }));
  std::shared_ptr<TlsSession> server_session, client_session;
  ASSERT_NO_FATAL_FAILURE(ConnectSessions(fds, server_session, client_session));
  ERR_raise(ERR_LIB_USER, 1);
  errno = EIO;
  char byte = 0;
  ssize_t received = 0;
  const auto status = server_session->RecvOnce(&byte, sizeof(byte), received);
  const int receive_errno = errno;
  EXPECT_EQ(status, SUCCESS);
  EXPECT_EQ(received, -1);
  EXPECT_EQ(receive_errno, EAGAIN);
  EXPECT_EQ(ERR_peek_error(), 0UL);
}

TEST_F(TlsSessionUt, WriteOnceIgnoresStaleErrorQueueUnderBackpressure) {
  int fds[2] = {-1, -1};
  HIXL_MAKE_GUARD(close_fds, ([&]() {
                    (void)close(fds[0]);
                    (void)close(fds[1]);
                  }));
  std::shared_ptr<TlsSession> server_session, client_session;
  ASSERT_NO_FATAL_FAILURE(ConnectSessions(fds, server_session, client_session));
  const int send_buffer_size = 4096;
  ASSERT_EQ(setsockopt(fds[1], SOL_SOCKET, SO_SNDBUF, &send_buffer_size, sizeof(send_buffer_size)), 0);
  const std::vector<char> payload(1024U * 1024U, 'x');
  ERR_raise(ERR_LIB_USER, 1);
  errno = EIO;
  ssize_t sent = 0;
  int ssl_error = SSL_ERROR_NONE;
  int32_t err_no = 0;
  const auto status = client_session->WriteOnce(payload.data(), payload.size(), sent, ssl_error, err_no);
  EXPECT_EQ(status, SUCCESS);
  EXPECT_EQ(sent, -1);
  EXPECT_EQ(ssl_error, SSL_ERROR_WANT_WRITE);
  EXPECT_EQ(err_no, 0);
  EXPECT_EQ(ERR_peek_error(), 0UL);
}

TEST_F(TlsSessionUt, PeerCertificateFingerprintIsStableSha256) {
  // The server only receives a client certificate when peer verification is enabled.
  ASSERT_EQ(TlsContext::Create(test::MakeTlsConfig(dir_), ctx_), SUCCESS);
  int fds[2] = {-1, -1};
  HIXL_MAKE_GUARD(close_fds, ([&]() {
                    (void)close(fds[0]);
                    (void)close(fds[1]);
                  }));
  std::shared_ptr<TlsSession> server;
  std::shared_ptr<TlsSession> client;
  ASSERT_NO_FATAL_FAILURE(ConnectSessions(fds, server, client));

  std::string server_fingerprint;
  std::string client_fingerprint;
  EXPECT_EQ(server->GetPeerCertificateFingerprint(server_fingerprint), SUCCESS);
  EXPECT_EQ(client->GetPeerCertificateFingerprint(client_fingerprint), SUCCESS);
  EXPECT_EQ(server_fingerprint.size(), 64U);
  // Both ends use the same self-signed certificate in the fixture.
  EXPECT_EQ(server_fingerprint, client_fingerprint);

  std::string repeated;
  EXPECT_EQ(server->GetPeerCertificateFingerprint(repeated), SUCCESS);
  EXPECT_EQ(repeated, server_fingerprint);
}

TEST(TlsOpensslUtilsTest, CollectsAndDrainsErrorQueue) {
  ERR_clear_error();
  EXPECT_EQ(OpensslErrorString(), "none");
  ERR_raise(ERR_LIB_USER, 1);
  ERR_raise(ERR_LIB_USER, 2);
  const auto errors = OpensslErrorString();
  EXPECT_NE(errors, "none");
  EXPECT_NE(errors.find("; "), std::string::npos);
  EXPECT_EQ(ERR_peek_error(), 0UL);
  EXPECT_EQ(OpensslErrorString(), "none");
}

TEST(TlsOpensslUtilsTest, DrainsErrorsBeyondDiagnosticLengthLimit) {
  ERR_clear_error();
  for (int i = 0; i < 16; ++i) {
    ERR_raise(ERR_LIB_USER, i + 1);
  }
  const auto errors = OpensslErrorString();
  EXPECT_FALSE(errors.empty());
  EXPECT_LE(errors.size(), 512U);
  EXPECT_EQ(ERR_peek_error(), 0UL);
}
}  // namespace hixl
