/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_COMMON_TLS_TLS_CONTEXT_H_
#define CANN_HIXL_SRC_HIXL_COMMON_TLS_TLS_CONTEXT_H_

#include <cstdint>
#include <memory>
#include <string>

#include "hixl/hixl_types.h"
#include "common/tls/tls_config.h"
#include "common/tls/tls_session.h"

using SSL_CTX = struct ssl_ctx_st;

namespace hixl {

class TlsContext {
 public:
  static Status Create(const TlsConfig &config, std::shared_ptr<TlsContext> &ctx);
  ~TlsContext();
  TlsContext(const TlsContext &) = delete;
  TlsContext &operator=(const TlsContext &) = delete;

  static Status CreateSession(const std::shared_ptr<TlsContext> &context, int32_t fd, bool is_server,
                              std::shared_ptr<TlsSession> &session);
  const TlsConfig &Config() const {
    return config_;
  }

 private:
  TlsContext() = default;
  Status Init(const TlsConfig &config);
  Status CreateSslCtx(bool is_server, SSL_CTX **out_ctx) const;
  Status LoadCertAndKey(SSL_CTX *ctx) const;
  Status ApplyVerifyAndCiphers(SSL_CTX *ctx) const;

  TlsConfig config_;
  SSL_CTX *server_ctx_ = nullptr;
  SSL_CTX *client_ctx_ = nullptr;
};
}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_COMMON_TLS_TLS_CONTEXT_H_
