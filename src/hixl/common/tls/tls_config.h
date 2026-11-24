/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_COMMON_TLS_TLS_CONFIG_H_
#define CANN_HIXL_SRC_HIXL_COMMON_TLS_TLS_CONFIG_H_

#include <string>

#include "hixl/hixl_types.h"
#include "nlohmann/json.hpp"

namespace hixl {

struct TlsConfig {
  bool configured = false;
  std::string cert_file;
  std::string ca_file;
  std::string ciphers;
  std::string key_pem;
  // SHA-256 of key_pem. It is not sensitive and lets TlsManager compare configurations after wiping the key.
  std::string key_fingerprint;

  TlsConfig() = default;
  TlsConfig(const TlsConfig &other);
  TlsConfig &operator=(const TlsConfig &other);
  ~TlsConfig();

  bool Enabled() const {
    return configured;
  }

  // The private key is only held in memory; wipe it as soon as the owning TLS resources are released.
  void WipeKey();
  void UpdateKeyFingerprint();
};

Status ParseTlsConfig(const nlohmann::json &json, TlsConfig &config);
Status ValidateTlsConfig(const TlsConfig &config);

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_COMMON_TLS_TLS_CONFIG_H_
