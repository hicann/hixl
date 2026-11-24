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

#include <sys/socket.h>
#include <unistd.h>

#include "common/tls/tls_context.h"
#include "common/tls/tls_test_helper.h"
#include "hixl/hixl_types.h"

namespace hixl {

class TlsContextUt : public ::testing::Test {
 protected:
  void SetUp() override {
    dir_ = test::MakeTempDir();
    ASSERT_FALSE(dir_.empty());
    if (!test::GenerateSelfSignedCert(dir_)) {
      GTEST_SKIP() << "openssl is not available to generate test certificates";
    }
  }

  void TearDown() override {
    test::RemovePath(dir_ + "/cert.pem");
    test::RemovePath(dir_ + "/key.pem");
    test::RemovePath(dir_ + "/cert_pkcs1.pem");
    test::RemovePath(dir_ + "/key_pkcs1.pem");
    test::RemovePath(dir_ + "/cert_ec.pem");
    test::RemovePath(dir_ + "/key_ec.pem");
    test::RemovePath(dir_);
  }

  std::string dir_;
};

TEST_F(TlsContextUt, CreateServerAndClientContextSuccess) {
  std::shared_ptr<TlsContext> ctx;
  EXPECT_EQ(TlsContext::Create(test::MakeTlsConfig(dir_), ctx), SUCCESS);
  ASSERT_NE(ctx, nullptr);
  std::shared_ptr<TlsSession> session;
  int fds[2] = {-1, -1};
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  EXPECT_EQ(TlsContext::CreateSession(ctx, fds[0], true, session), SUCCESS);
  ASSERT_NE(session, nullptr);
  const std::weak_ptr<TlsContext> weak_ctx = ctx;
  ctx.reset();
  EXPECT_FALSE(weak_ctx.expired());
  session.reset();
  EXPECT_TRUE(weak_ctx.expired());
  close(fds[0]);
  close(fds[1]);
}

TEST(TlsContextTest, CreateSessionRejectsNullContext) {
  std::shared_ptr<TlsSession> session;
  EXPECT_EQ(TlsContext::CreateSession(nullptr, 0, true, session), PARAM_INVALID);
  EXPECT_EQ(session, nullptr);
}

TEST_F(TlsContextUt, InvalidCertPath) {
  TlsConfig cfg = test::MakeTlsConfig(dir_);
  cfg.cert_file = dir_ + "/missing.pem";
  std::shared_ptr<TlsContext> ctx;
  EXPECT_EQ(TlsContext::Create(cfg, ctx), FAILED);
  EXPECT_EQ(ctx, nullptr);
}

TEST_F(TlsContextUt, InvalidKeyPem) {
  TlsConfig cfg = test::MakeTlsConfig(dir_);
  cfg.key_pem = "not-a-key";
  std::shared_ptr<TlsContext> ctx;
  EXPECT_EQ(TlsContext::Create(cfg, ctx), FAILED);
  EXPECT_EQ(ctx, nullptr);
}

TEST_F(TlsContextUt, CreatesContextWithoutKeyFile) {
  const auto config = test::MakeTlsConfig(dir_);
  test::RemovePath(dir_ + "/key.pem");
  std::shared_ptr<TlsContext> ctx;
  EXPECT_EQ(TlsContext::Create(config, ctx), SUCCESS);
  EXPECT_NE(ctx, nullptr);
}

TEST_F(TlsContextUt, AcceptsPkcs1PrivateKey) {
  if (!test::GeneratePkcs1Cert(dir_)) {
    GTEST_SKIP() << "openssl cannot generate a PKCS#1 private key";
  }
  std::shared_ptr<TlsContext> ctx;
  EXPECT_EQ(TlsContext::Create(test::MakeTlsConfigFromFiles(dir_, "cert_pkcs1.pem", "key_pkcs1.pem"), ctx), SUCCESS);
  EXPECT_NE(ctx, nullptr);
}

TEST_F(TlsContextUt, AcceptsEcPrivateKey) {
  if (!test::GenerateEcCert(dir_)) {
    GTEST_SKIP() << "openssl cannot generate an EC private key";
  }
  std::shared_ptr<TlsContext> ctx;
  EXPECT_EQ(TlsContext::Create(test::MakeTlsConfigFromFiles(dir_, "cert_ec.pem", "key_ec.pem"), ctx), SUCCESS);
  EXPECT_NE(ctx, nullptr);
}

TEST_F(TlsContextUt, EncryptedPrivateKeyRejected) {
  const std::string encrypted_key = dir_ + "/encrypted_key.pem";
  const std::string cmd =
      "openssl rsa -in " + dir_ + "/key.pem -aes256 -passout pass:test -out " + encrypted_key + " >/dev/null 2>&1";
  if (!test::RunOpenssl(cmd)) {
    GTEST_SKIP() << "openssl cannot create an encrypted private key";
  }
  TlsConfig cfg = test::MakeTlsConfig(dir_);
  cfg.key_pem = test::ReadFileToString(encrypted_key);
  std::shared_ptr<TlsContext> ctx;
  EXPECT_EQ(TlsContext::Create(cfg, ctx), FAILED);
  EXPECT_EQ(ctx, nullptr);
  test::RemovePath(encrypted_key);
}

TEST_F(TlsContextUt, InvalidCaPath) {
  TlsConfig cfg = test::MakeTlsConfig(dir_);
  cfg.ca_file = dir_ + "/missing-ca.pem";
  std::shared_ptr<TlsContext> ctx;
  EXPECT_EQ(TlsContext::Create(cfg, ctx), FAILED);
  EXPECT_EQ(ctx, nullptr);
}

TEST_F(TlsContextUt, CertKeyMismatch) {
  ASSERT_TRUE(test::GenerateSelfSignedCert(dir_, "other.pem", "other_key.pem"));
  TlsConfig cfg = test::MakeTlsConfig(dir_);
  cfg.key_pem = test::ReadFileToString(dir_ + "/other_key.pem");
  std::shared_ptr<TlsContext> ctx;
  EXPECT_EQ(TlsContext::Create(cfg, ctx), FAILED);
  test::RemovePath(dir_ + "/other.pem");
  test::RemovePath(dir_ + "/other_key.pem");
}

TEST_F(TlsContextUt, InvalidCiphersRejected) {
  TlsConfig cfg = test::MakeTlsConfig(dir_);
  cfg.ciphers = "NOT-A-REAL-CIPHER";
  std::shared_ptr<TlsContext> ctx;
  EXPECT_EQ(TlsContext::Create(cfg, ctx), PARAM_INVALID);
}

}  // namespace hixl
