/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_COMMON_TLS_TLS_SESSION_H_
#define CANN_HIXL_SRC_HIXL_COMMON_TLS_TLS_SESSION_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unistd.h>

#include "hixl/hixl_types.h"

using SSL = struct ssl_st;

namespace hixl {
class TlsContext;

enum class TlsHandshakeState { kWantRead, kWantWrite, kComplete };

class TlsSession {
 public:
  TlsSession(SSL *ssl, std::shared_ptr<TlsContext> ctx, int32_t fd);
  ~TlsSession();
  TlsSession(const TlsSession &) = delete;
  TlsSession &operator=(const TlsSession &) = delete;

  Status HandshakeAsServer(int32_t timeout_ms);
  Status HandshakeAsServerStep(TlsHandshakeState &state);
  Status HandshakeAsClient(int32_t timeout_ms);
  bool IsHandshakeComplete() const;
  // An abandoned write makes the session unusable even after a completed handshake.
  bool IsUsable() const;
  // SHA-256 fingerprint (lowercase hex) of the peer certificate presented during the handshake.
  Status GetPeerCertificateFingerprint(std::string &fingerprint) const;
  void Invalidate();
  Status Send(const void *buf, size_t len, uint32_t timeout_ms, int32_t &err_no);
  Status Recv(void *buf, size_t len, uint32_t timeout_ms);
  Status RecvOnce(void *buf, size_t len, ssize_t &n);
  bool HasPending();
  void Shutdown();
  int32_t Fd() const {
    return fd_;
  }

 private:
  Status DoHandshake(int (*handshake_fn)(SSL *), int32_t timeout_ms);
  Status HandshakeStep(int (*handshake_fn)(SSL *), TlsHandshakeState &state);
  Status PollFd(short events, int32_t timeout_ms) const;
  Status WriteOnce(const void *buf, size_t len, ssize_t &n, int &ssl_error, int32_t &err_no);

  mutable std::mutex mutex_;
  bool handshake_complete_ = false;
  bool failed_ = false;  // Terminal I/O failure, protected by mutex_.
  SSL *ssl_ = nullptr;
  std::shared_ptr<TlsContext> ctx_;
  int32_t fd_ = -1;
};
}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_COMMON_TLS_TLS_SESSION_H_
