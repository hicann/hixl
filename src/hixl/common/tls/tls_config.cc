/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "tls_config.h"

#include <openssl/crypto.h>
#include <openssl/sha.h>

#include <cstring>
#include <utility>

#include "common/hixl_checker.h"

namespace hixl {
namespace {
constexpr const char *kCertFile = "tls_config.cert_file";
constexpr const char *kCaFile = "tls_config.ca_file";
constexpr const char *kCiphers = "tls_config.ciphers";
constexpr const char *kKeyPem = "tls_config.key_pem";
constexpr size_t kMaxTlsPemSize = 64U * 1024U;

Status ParseStringField(const nlohmann::json &json, const char *key, std::string &out, bool &configured) {
  const auto it = json.find(key);
  if (it == json.end()) {
    return SUCCESS;
  }
  configured = true;
  HIXL_CHK_BOOL_RET_STATUS(it->is_string(), PARAM_INVALID, "[TlsConfig] %s must be a string", key);
  out = it->get<std::string>();
  return SUCCESS;
}

Status ParseKeyPemField(const nlohmann::json &json, std::string &out, bool &configured) {
  const auto it = json.find(kKeyPem);
  if (it == json.end()) {
    return SUCCESS;
  }
  configured = true;
  HIXL_CHK_BOOL_RET_STATUS(it->is_string(), PARAM_INVALID, "[TlsConfig] %s must be a string", kKeyPem);
  std::string value = it->get<std::string>();
  HIXL_CHK_BOOL_RET_STATUS(!value.empty(), PARAM_INVALID, "[TlsConfig] %s must not be empty", kKeyPem);
  HIXL_CHK_BOOL_RET_STATUS(value.size() <= kMaxTlsPemSize, PARAM_INVALID,
                           "[TlsConfig] %s exceeds max size:%zu, actual:%zu", kKeyPem, kMaxTlsPemSize, value.size());
  HIXL_CHK_BOOL_RET_STATUS(value.find("PRIVATE KEY-----") != std::string::npos, PARAM_INVALID,
                           "[TlsConfig] %s is not a valid PEM private key", kKeyPem);
  out = std::move(value);
  return SUCCESS;
}

std::string ComputeKeyFingerprint(const std::string &key_pem) {
  unsigned char digest[SHA256_DIGEST_LENGTH] = {};
  SHA256(reinterpret_cast<const unsigned char *>(key_pem.data()), key_pem.size(), digest);
  constexpr char kHex[] = "0123456789abcdef";
  std::string fingerprint;
  fingerprint.reserve(SHA256_DIGEST_LENGTH * 2U);
  for (const unsigned char byte : digest) {
    fingerprint.push_back(kHex[byte >> 4U]);
    fingerprint.push_back(kHex[byte & 0x0FU]);
  }
  return fingerprint;
}
}  // namespace

TlsConfig::TlsConfig(const TlsConfig &other) = default;

TlsConfig &TlsConfig::operator=(const TlsConfig &other) {
  if (this != &other) {
    WipeKey();
    configured = other.configured;
    cert_file = other.cert_file;
    ca_file = other.ca_file;
    ciphers = other.ciphers;
    key_pem = other.key_pem;
    key_fingerprint = other.key_fingerprint;
  }
  return *this;
}

TlsConfig::~TlsConfig() {
  WipeKey();
}

void TlsConfig::WipeKey() {
  if (!key_pem.empty()) {
    OPENSSL_cleanse(key_pem.data(), key_pem.size());
    key_pem.clear();
  }
}

void TlsConfig::UpdateKeyFingerprint() {
  if (!key_pem.empty()) {
    key_fingerprint = ComputeKeyFingerprint(key_pem);
  }
}

Status ValidateTlsConfig(const TlsConfig &config) {
  if (!config.configured) {
    return SUCCESS;
  }
  HIXL_CHK_BOOL_RET_STATUS(!config.cert_file.empty(), PARAM_INVALID, "[TlsConfig] cert_file is required");
  HIXL_CHK_BOOL_RET_STATUS(!config.key_pem.empty() || !config.key_fingerprint.empty(), PARAM_INVALID,
                           "[TlsConfig] key_pem is required");
  HIXL_CHK_BOOL_RET_STATUS(!config.ca_file.empty(), PARAM_INVALID,
                           "[TlsConfig] ca_file is required because peer verification is always enabled");
  return SUCCESS;
}

Status ParseTlsConfig(const nlohmann::json &json, TlsConfig &config) {
  TlsConfig parsed;
  HIXL_CHK_STATUS_RET(ParseStringField(json, kCertFile, parsed.cert_file, parsed.configured),
                      "[TlsConfig] Failed to parse cert_file");
  HIXL_CHK_STATUS_RET(ParseStringField(json, kCaFile, parsed.ca_file, parsed.configured),
                      "[TlsConfig] Failed to parse ca_file");
  HIXL_CHK_STATUS_RET(ParseStringField(json, kCiphers, parsed.ciphers, parsed.configured),
                      "[TlsConfig] Failed to parse ciphers");
  HIXL_CHK_STATUS_RET(ParseKeyPemField(json, parsed.key_pem, parsed.configured), "[TlsConfig] Failed to parse key_pem");
  HIXL_CHK_STATUS_RET(ValidateTlsConfig(parsed), "[TlsConfig] Invalid tls_config");
  parsed.UpdateKeyFingerprint();
  HIXL_LOGI("[TlsConfig] Parsed configuration, enabled:%s, custom_ciphers:%s, verify_peer:true",
            parsed.Enabled() ? "true" : "false", parsed.ciphers.empty() ? "false" : "true");
  config = std::move(parsed);
  return SUCCESS;
}
}  // namespace hixl
