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

#include <array>
#include <atomic>
#include <thread>
#include <vector>

#include "common/tls/tls_manager.h"
#include "common/tls/tls_test_helper.h"

namespace hixl {
class TlsManagerUt : public ::testing::Test {
 protected:
  void SetUp() override {
    dir_ = test::MakeTempDir();
    ASSERT_FALSE(dir_.empty());
    if (!test::GenerateSelfSignedCert(dir_)) {
      GTEST_SKIP() << "openssl is not available to generate test certificates";
    }
  }
  void TearDown() override {
    while (initialized_ != 0U) {
      Finalize();
    }
    test::RemovePath(dir_ + "/cert.pem");
    test::RemovePath(dir_ + "/key.pem");
    test::RemovePath(dir_);
  }
  Status Initialize(const TlsConfig &config) {
    const auto ret = manager_.Initialize(config);
    if (ret == SUCCESS) {
      ++initialized_;
    }
    return ret;
  }
  void Finalize() {
    manager_.Finalize();
    --initialized_;
  }
  TlsManager &manager_ = TlsManager::GetInstance();
  size_t initialized_ = 0U;
  std::string dir_;
};

TEST_F(TlsManagerUt, PlainConfigurationIsSharedAndRejectsTlsUntilLastFinalize) {
  ASSERT_EQ(Initialize({}), SUCCESS);
  ASSERT_EQ(Initialize({}), SUCCESS);
  EXPECT_FALSE(manager_.GetConfig().Enabled());
  EXPECT_EQ(manager_.GetContext(), nullptr);
  EXPECT_EQ(Initialize(test::MakeTlsConfig(dir_)), PARAM_INVALID);
  Finalize();
  EXPECT_EQ(Initialize(test::MakeTlsConfig(dir_)), PARAM_INVALID);
  Finalize();
  ASSERT_EQ(Initialize(test::MakeTlsConfig(dir_)), SUCCESS);
  EXPECT_NE(manager_.GetContext(), nullptr);
}

TEST_F(TlsManagerUt, ReusesContextWithoutReadingFilesAndFinalizesLastUser) {
  const auto config = test::MakeTlsConfig(dir_);
  ASSERT_EQ(Initialize(config), SUCCESS);
  std::weak_ptr<TlsContext> context = manager_.GetContext();
  test::RemovePath(config.cert_file);
  ASSERT_EQ(Initialize(config), SUCCESS);
  EXPECT_EQ(manager_.GetContext(), context.lock());
  Finalize();
  EXPECT_EQ(manager_.GetContext(), context.lock());
  Finalize();
  EXPECT_TRUE(context.expired());
  EXPECT_EQ(manager_.GetContext(), nullptr);
  EXPECT_FALSE(manager_.GetConfig().Enabled());
  ASSERT_EQ(Initialize({}), SUCCESS);
}

TEST_F(TlsManagerUt, RejectsEveryConflictingFieldWithoutChangingActiveContext) {
  auto config = test::MakeTlsConfig(dir_);
  config.ca_file = config.cert_file;
  ASSERT_EQ(Initialize(config), SUCCESS);
  const auto context = manager_.GetContext();
  std::array<TlsConfig, 5> conflicts;
  conflicts.fill(config);
  conflicts[0].configured = false;
  conflicts[1].cert_file += ".other";
  conflicts[2].ca_file += ".other";
  conflicts[3].ciphers = "ECDHE-RSA-AES256-GCM-SHA384";
  conflicts[4].key_pem += "x";
  for (const auto &conflict : conflicts) {
    EXPECT_EQ(Initialize(conflict), PARAM_INVALID);
    EXPECT_EQ(manager_.GetContext(), context);
  }
  Finalize();
  EXPECT_EQ(manager_.GetContext(), nullptr);
}

TEST_F(TlsManagerUt, FailedInitializationCanBeRetriedWithDifferentConfiguration) {
  TlsConfig invalid;
  invalid.configured = true;
  EXPECT_EQ(Initialize(invalid), PARAM_INVALID);
  invalid = test::MakeTlsConfig(dir_);
  invalid.key_pem = "not-a-key";
  EXPECT_NE(Initialize(invalid), SUCCESS);
  EXPECT_EQ(manager_.GetContext(), nullptr);
  EXPECT_FALSE(manager_.GetConfig().Enabled());
  ASSERT_EQ(Initialize(test::MakeTlsConfig(dir_)), SUCCESS);
  Finalize();
  ASSERT_EQ(Initialize(test::MakeTlsConfig(dir_)), SUCCESS);
}

TEST_F(TlsManagerUt, ModeQueryRequiresOwnerAndDoesNotRetainResources) {
  bool enabled = true;
  EXPECT_EQ(manager_.GetTlsEnabled(enabled), FAILED);
  EXPECT_FALSE(enabled);
  EXPECT_FALSE(manager_.IsInitialized());
  ASSERT_EQ(Initialize({}), SUCCESS);
  enabled = true;
  EXPECT_EQ(manager_.GetTlsEnabled(enabled), SUCCESS);
  EXPECT_FALSE(enabled);
  Finalize();
  EXPECT_FALSE(manager_.IsInitialized());
  ASSERT_EQ(Initialize(test::MakeTlsConfig(dir_)), SUCCESS);
  EXPECT_EQ(manager_.GetTlsEnabled(enabled), SUCCESS);
  EXPECT_TRUE(enabled);
  Finalize();
  EXPECT_FALSE(manager_.IsInitialized());
  EXPECT_EQ(manager_.GetContext(), nullptr);
  EXPECT_EQ(manager_.GetTlsEnabled(enabled), FAILED);
  EXPECT_FALSE(enabled);
}

TEST_F(TlsManagerUt, ConcurrentInitializeFinalizePreservesActiveContext) {
  const auto config = test::MakeTlsConfig(dir_);
  ASSERT_EQ(Initialize(config), SUCCESS);
  const auto context = manager_.GetContext();
  std::vector<std::thread> threads;
  for (size_t i = 0; i < 4U; ++i) {
    threads.emplace_back([&]() {
      for (size_t j = 0; j < 100U; ++j) {
        ASSERT_EQ(manager_.Initialize(config), SUCCESS);
        EXPECT_EQ(manager_.GetContext(), context);
        EXPECT_EQ(manager_.GetConfig().cert_file, config.cert_file);
        manager_.Finalize();
      }
    });
  }
  for (auto &thread : threads) {
    thread.join();
  }
  Finalize();
  EXPECT_EQ(manager_.GetContext(), nullptr);
}

TEST_F(TlsManagerUt, ConcurrentLastFinalizeAndInitializeCanRestartTls) {
  const auto config = test::MakeTlsConfig(dir_);
  std::atomic<bool> start{false};
  std::vector<std::thread> threads;
  for (size_t i = 0; i < 4U; ++i) {
    threads.emplace_back([&]() {
      while (!start.load()) {
        std::this_thread::yield();
      }
      for (size_t j = 0; j < 10U; ++j) {
        ASSERT_EQ(manager_.Initialize(config), SUCCESS);
        EXPECT_NE(manager_.GetContext(), nullptr);
        manager_.Finalize();
      }
    });
  }
  start = true;
  for (auto &thread : threads) {
    thread.join();
  }
  EXPECT_EQ(manager_.GetContext(), nullptr);
  EXPECT_FALSE(manager_.GetConfig().Enabled());
  ASSERT_EQ(Initialize(test::MakeTlsConfig(dir_)), SUCCESS);
}

TEST_F(TlsManagerUt, WipesStoredKeyAndReusesByFingerprint) {
  const auto config = test::MakeTlsConfig(dir_);
  ASSERT_EQ(Initialize(config), SUCCESS);
  const auto stored = manager_.GetConfig();
  EXPECT_TRUE(stored.key_pem.empty());
  EXPECT_EQ(stored.key_fingerprint.size(), 64U);
  const auto context = manager_.GetContext();
  ASSERT_EQ(Initialize(stored), SUCCESS);
  EXPECT_EQ(manager_.GetContext(), context);
  auto changed = test::MakeTlsConfig(dir_);
  changed.key_pem += "x";
  EXPECT_EQ(Initialize(changed), PARAM_INVALID);
  EXPECT_EQ(manager_.GetContext(), context);
  Finalize();
  Finalize();
  EXPECT_EQ(manager_.GetContext(), nullptr);
}

TEST_F(TlsManagerUt, InitializeOrRetainReusesEnabledTls) {
  ASSERT_EQ(Initialize(test::MakeTlsConfig(dir_)), SUCCESS);
  const auto context = manager_.GetContext();
  ASSERT_EQ(manager_.InitializeOrRetain(TlsConfig{}), SUCCESS);
  ++initialized_;
  EXPECT_EQ(manager_.GetContext(), context);
  Finalize();
  EXPECT_EQ(manager_.GetContext(), context);
  Finalize();
  EXPECT_EQ(manager_.GetContext(), nullptr);
}

TEST_F(TlsManagerUt, InitializeOrRetainUsesPlainModeWhenUninitialized) {
  ASSERT_EQ(manager_.InitializeOrRetain(TlsConfig{}), SUCCESS);
  ++initialized_;
  EXPECT_FALSE(manager_.GetConfig().Enabled());
  EXPECT_EQ(manager_.GetContext(), nullptr);
}
}  // namespace hixl
