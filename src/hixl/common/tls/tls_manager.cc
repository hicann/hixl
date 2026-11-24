/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "tls_manager.h"

#include <openssl/ssl.h>

#include "common/hixl_checker.h"
#include "common/scope_guard.h"

namespace hixl {
namespace {
bool SameConfig(const TlsConfig &lhs, const TlsConfig &rhs) {
  return lhs.configured == rhs.configured && lhs.cert_file == rhs.cert_file && lhs.ca_file == rhs.ca_file &&
         lhs.ciphers == rhs.ciphers && lhs.key_fingerprint == rhs.key_fingerprint;
}
}  // namespace

TlsManager &TlsManager::GetInstance() {
  static TlsManager instance;
  return instance;
}

TlsManager::~TlsManager() = default;

Status TlsManager::EnableTlsLocked(const TlsConfig &config) {
  const int ret = OPENSSL_init_ssl(0, nullptr);
  HIXL_CHK_BOOL_RET_STATUS(ret == 1, FAILED, "Call api:OPENSSL_init_ssl failed, ret:%d", ret);
  std::shared_ptr<TlsContext> ctx;
  HIXL_CHK_STATUS_RET(TlsContext::Create(config, ctx), "[TlsManager] Failed to create TLS context");
  ctx_ = std::move(ctx);
  return SUCCESS;
}

Status TlsManager::Initialize(const TlsConfig &config) {
  std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
  std::unique_lock<std::shared_mutex> lock(mutex_);
  return InitializeLocked(config);
}

Status TlsManager::InitializeOrRetain(const TlsConfig &config) {
  std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
  std::unique_lock<std::shared_mutex> lock(mutex_);
  if (!config.Enabled() && users_ != 0U && config_.Enabled() && ctx_ != nullptr) {
    ++users_;
    return SUCCESS;
  }
  return InitializeLocked(config);
}

Status TlsManager::InitializeLocked(const TlsConfig &config) {
  HIXL_CHK_STATUS_RET(ValidateTlsConfig(config), "[TlsManager] Invalid TLS configuration");
  TlsConfig incoming = config;
  HIXL_MAKE_GUARD(wipe_incoming, ([&incoming]() { incoming.WipeKey(); }));
  incoming.UpdateKeyFingerprint();
  if (users_ == 0U && incoming.Enabled() && incoming.key_pem.empty()) {
    HIXL_LOGE(PARAM_INVALID, "[TlsManager] key_pem is required to enable process TLS");
    return PARAM_INVALID;
  }
  if (users_ != 0U) {
    HIXL_CHK_BOOL_RET_STATUS(SameConfig(config_, incoming), PARAM_INVALID,
                             "[TlsManager] TLS configuration conflicts with active process configuration");
    ++users_;
    return SUCCESS;
  }
  if (incoming.Enabled()) {
    HIXL_CHK_STATUS_RET(EnableTlsLocked(incoming), "[TlsManager] Failed to initialize TLS resources");
  }
  config_ = incoming;
  config_.WipeKey();
  users_ = 1U;
  return SUCCESS;
}

void TlsManager::Finalize() {
  std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
  std::unique_lock<std::shared_mutex> lock(mutex_);
  if (users_ == 0U) {
    return;
  }
  --users_;
  if (users_ != 0U) {
    return;
  }
  ctx_.reset();
  config_.WipeKey();
  config_ = TlsConfig{};
}

Status TlsManager::GetTlsEnabled(bool &enabled) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  enabled = false;
  HIXL_CHK_BOOL_RET_STATUS(users_ != 0U, FAILED, "[TlsManager] TLS manager must be initialized by its owner first");
  enabled = config_.Enabled();
  return SUCCESS;
}

bool TlsManager::IsInitialized() const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  return users_ != 0U;
}

std::shared_ptr<TlsContext> TlsManager::GetContext() const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  return ctx_;
}

TlsConfig TlsManager::GetConfig() const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  return config_;
}
}  // namespace hixl
