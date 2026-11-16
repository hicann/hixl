/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "tls_session.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <limits>
#include <sys/poll.h>

#include <openssl/err.h>
#include <openssl/ssl.h>

#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/tls/tls_openssl_utils.h"
#include "common/scope_guard.h"

namespace hixl {
namespace {
bool IsWantIo(int ssl_err) {
  return ssl_err == SSL_ERROR_WANT_READ || ssl_err == SSL_ERROR_WANT_WRITE;
}

int64_t RemainingTimeoutMs(std::chrono::steady_clock::time_point start, uint32_t timeout_ms) {
  return static_cast<int64_t>(timeout_ms) -
         std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
}

// errno is only meaningful for socket-level failures (ssl_err == SSL_ERROR_SYSCALL).
// For pure OpenSSL failures errno stays 0 and strerror(0) prints "Success", which is
// misleading in error logs, so use "none" instead.
const char *ErrnoMessage(int32_t err_no) {
  return err_no != 0 ? strerror(err_no) : "none";
}

}  // namespace

TlsSession::TlsSession(SSL *ssl, std::shared_ptr<TlsContext> ctx, int32_t fd)
    : ssl_(ssl), ctx_(std::move(ctx)), fd_(fd) {}

TlsSession::~TlsSession() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (ssl_ != nullptr) {
    SSL_free(ssl_);
    ssl_ = nullptr;
  }
}

Status TlsSession::PollFd(short events, int32_t timeout_ms) const {
  struct pollfd pfd = {};
  pfd.fd = fd_;
  pfd.events = events;
  const int ret = poll(&pfd, 1, timeout_ms);
  if (ret == 0) {
    HIXL_CHK_BOOL_RET_STATUS(false, TIMEOUT, "Call api:poll timed out, ret:%d, fd:%d, events:0x%x, timeout:%d ms", ret,
                             fd_, static_cast<unsigned int>(events), timeout_ms);
  }
  if (ret < 0) {
    const int32_t saved_errno = errno;
    if (saved_errno == EINTR) {
      return SUCCESS;
    }
    HIXL_CHK_BOOL_RET_STATUS(false, FAILED,
                             "Call api:poll failed, ret:%d, fd:%d, timeout_ms:%d, errno:%d, error_msg:%s", ret, fd_,
                             timeout_ms, saved_errno, ErrnoMessage(saved_errno));
  }
  return SUCCESS;
}

Status TlsSession::HandshakeStep(int (*handshake_fn)(SSL *), TlsHandshakeState &state) {
  std::lock_guard<std::mutex> lock(mutex_);
  state = TlsHandshakeState::kComplete;
  HIXL_CHK_BOOL_RET_STATUS(ssl_ != nullptr && !failed_, FAILED, "TLS session is not usable for handshake, fd:%d", fd_);
  if (handshake_complete_) {
    return SUCCESS;
  }
  ERR_clear_error();
  errno = 0;
  const int rc = handshake_fn(ssl_);
  const int saved_errno = errno;
  if (rc == 1) {
    handshake_complete_ = true;
    HIXL_LOGI("[TlsSession] Handshake completed, api:%s, fd:%d, protocol:%s, cipher:%s",
              handshake_fn == SSL_accept ? "SSL_accept" : "SSL_connect", fd_, SSL_get_version(ssl_),
              SSL_get_cipher_name(ssl_));
    return SUCCESS;
  }
  const int ssl_err = SSL_get_error(ssl_, rc);
  if (IsWantIo(ssl_err)) {
    state = ssl_err == SSL_ERROR_WANT_READ ? TlsHandshakeState::kWantRead : TlsHandshakeState::kWantWrite;
    return SUCCESS;
  }
  const std::string error = OpensslErrorString();
  HIXL_CHK_BOOL_RET_STATUS(false, FAILED,
                           "Call api:%s failed, ret:%d, ssl_err:%d, fd:%d, errno:%d, error_msg:%s, openssl_error:%s",
                           handshake_fn == SSL_accept ? "SSL_accept" : "SSL_connect", rc, ssl_err, fd_, saved_errno,
                           ErrnoMessage(saved_errno), error.c_str());
}

Status TlsSession::DoHandshake(int (*handshake_fn)(SSL *), int32_t timeout_ms) {
  HIXL_CHK_BOOL_RET_STATUS(timeout_ms > 0, PARAM_INVALID,
                           "[TlsSession] Invalid handshake timeout, fd:%d, timeout:%d ms", fd_, timeout_ms);
  const auto start = std::chrono::steady_clock::now();
  const char *api = handshake_fn == SSL_accept ? "SSL_accept" : "SSL_connect";
  HIXL_LOGI("[TlsSession] Handshake started, api:%s, fd:%d, timeout:%d ms", api, fd_, timeout_ms);
  while (true) {
    TlsHandshakeState state;
    HIXL_CHK_STATUS_RET(HandshakeStep(handshake_fn, state), "[TlsSession] Handshake failed, api:%s, fd:%d", api, fd_);
    if (state == TlsHandshakeState::kComplete) {
      const auto elapsed =
          std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
      HIXL_LOGI("[TlsSession] Handshake succeeded, api:%s, fd:%d, elapsed:%ld ms", api, fd_, elapsed);
      return SUCCESS;
    }
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    HIXL_CHK_BOOL_RET_STATUS(elapsed < static_cast<int64_t>(timeout_ms), TIMEOUT,
                             "[TlsSession] Handshake timed out, api:%s, fd:%d, elapsed:%ld ms, timeout:%d ms", api, fd_,
                             elapsed, timeout_ms);
    const short events = state == TlsHandshakeState::kWantRead ? POLLIN : POLLOUT;
    const int32_t remain = timeout_ms - static_cast<int32_t>(elapsed);
    HIXL_CHK_STATUS_RET(PollFd(events, remain),
                        "[TlsSession] Handshake poll failed, api:%s, fd:%d, remaining_timeout:%d ms", api, fd_, remain);
  }
}

Status TlsSession::HandshakeAsServer(int32_t timeout_ms) {
  return DoHandshake(SSL_accept, timeout_ms);
}

Status TlsSession::HandshakeAsServerStep(TlsHandshakeState &state) {
  return HandshakeStep(SSL_accept, state);
}

bool TlsSession::IsHandshakeComplete() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return handshake_complete_;
}

bool TlsSession::IsUsable() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return ssl_ != nullptr && handshake_complete_ && !failed_;
}

void TlsSession::Invalidate() {
  std::lock_guard<std::mutex> lock(mutex_);
  failed_ = true;
}

Status TlsSession::HandshakeAsClient(int32_t timeout_ms) {
  return DoHandshake(SSL_connect, timeout_ms);
}

Status TlsSession::Send(const void *buf, size_t len, uint32_t timeout_ms, int32_t &err_no) {
  const auto start = std::chrono::steady_clock::now();
  err_no = 0;
  HIXL_CHK_BOOL_RET_STATUS(IsUsable(), FAILED, "TLS session is not usable for send, fd:%d", fd_);
  HIXL_CHK_BOOL_RET_STATUS(len <= static_cast<size_t>(std::numeric_limits<int>::max()), PARAM_INVALID,
                           "TLS write length exceeds int max, fd:%d, size:%zu bytes", fd_, len);
  bool write_started = false;
  HIXL_DISMISSABLE_GUARD(abandon_write, ([this, &write_started]() {
                           if (write_started) {
                             Invalidate();
                           }
                         }));
  const char *pos = static_cast<const char *>(buf);
  auto nbytes = static_cast<int>(len);
  while (nbytes > 0) {
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    HIXL_CHK_BOOL_RET_STATUS(elapsed < static_cast<int64_t>(timeout_ms), TIMEOUT,
                             "Call api:SSL_write timed out, fd:%d, size:%zu bytes, remaining_size:%d bytes, "
                             "elapsed:%ld ms, timeout:%u ms",
                             fd_, len, nbytes, elapsed, timeout_ms);
    ssize_t rc = 0;
    int ssl_err = SSL_ERROR_NONE;
    write_started = true;
    HIXL_CHK_STATUS_RET(WriteOnce(pos, static_cast<size_t>(nbytes), rc, ssl_err, err_no), "TLS write failed, fd:%d",
                        fd_);
    if (rc > 0) {
      pos += rc;
      nbytes -= static_cast<int>(rc);
      continue;
    }
    const short events = (ssl_err == SSL_ERROR_WANT_READ) ? POLLIN : POLLOUT;
    const auto remaining_ms =
        static_cast<int64_t>(timeout_ms) -
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    if (remaining_ms <= 0) {
      continue;
    }
    const auto remaining = static_cast<int32_t>(std::min<int64_t>(remaining_ms, std::numeric_limits<int32_t>::max()));
    HIXL_CHK_STATUS_RET(PollFd(events, remaining), "Call api:poll failed during SSL_write, fd:%d", fd_);
  }
  HIXL_DISMISS_GUARD(abandon_write);
  return SUCCESS;
}

Status TlsSession::Recv(void *buf, size_t len, uint32_t timeout_ms) {
  HIXL_CHK_BOOL_RET_STATUS(IsUsable(), FAILED, "TLS session is not usable for receive, fd:%d", fd_);
  HIXL_CHK_BOOL_RET_STATUS(len <= static_cast<size_t>(std::numeric_limits<int>::max()), PARAM_INVALID,
                           "TLS read length exceeds int max, fd:%d, size:%zu bytes", fd_, len);
  auto *pos = static_cast<uint8_t *>(buf);
  auto nbytes = static_cast<int>(len);
  const auto start = std::chrono::steady_clock::now();
  while (nbytes > 0) {
    auto remaining_ms = RemainingTimeoutMs(start, timeout_ms);
    HIXL_CHK_BOOL_RET_STATUS(remaining_ms > 0, TIMEOUT,
                             "Call api:SSL_read timed out, fd:%d, size:%zu bytes, remaining_size:%d bytes, "
                             "elapsed:%ld ms, timeout:%u ms",
                             fd_, len, nbytes, static_cast<int64_t>(timeout_ms) - remaining_ms, timeout_ms);
    int rc = 0;
    int ssl_err = SSL_ERROR_NONE;
    int saved_errno = 0;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      HIXL_CHK_BOOL_RET_STATUS(!failed_, FAILED, "TLS session failed during receive, fd:%d", fd_);
      ERR_clear_error();
      errno = 0;
      rc = SSL_read(ssl_, pos, nbytes);
      saved_errno = errno;
      if (rc > 0) {
        pos += rc;
        nbytes -= rc;
        continue;
      }
      ssl_err = SSL_get_error(ssl_, rc);
      if (!IsWantIo(ssl_err)) {
        failed_ = true;
      }
    }
    if (!IsWantIo(ssl_err)) {
      const std::string error = OpensslErrorString();
      HIXL_CHK_BOOL_RET_STATUS(false, FAILED,
                               "Call api:SSL_read failed, ret:%d, ssl_err:%d, fd:%d, remaining_size:%d bytes, "
                               "errno:%d, error_msg:%s, openssl_error:%s",
                               rc, ssl_err, fd_, nbytes, saved_errno, ErrnoMessage(saved_errno), error.c_str());
    }
    remaining_ms = RemainingTimeoutMs(start, timeout_ms);
    if (remaining_ms <= 0) {
      continue;
    }
    const auto remaining = static_cast<int32_t>(std::min<int64_t>(remaining_ms, std::numeric_limits<int32_t>::max()));
    const short events = (ssl_err == SSL_ERROR_WANT_READ) ? POLLIN : POLLOUT;
    HIXL_CHK_STATUS_RET(PollFd(events, remaining), "Call api:poll failed during SSL_read, fd:%d", fd_);
  }
  return SUCCESS;
}

Status TlsSession::RecvOnce(void *buf, size_t len, ssize_t &n) {
  std::lock_guard<std::mutex> lock(mutex_);
  n = -1;
  HIXL_CHK_BOOL_RET_STATUS(ssl_ != nullptr && handshake_complete_ && !failed_, FAILED,
                           "TLS session is not usable for receive, fd:%d", fd_);
  HIXL_CHK_BOOL_RET_STATUS(len <= static_cast<size_t>(std::numeric_limits<int>::max()), PARAM_INVALID,
                           "TLS read length exceeds int max, fd:%d, size:%zu bytes", fd_, len);
  ERR_clear_error();
  errno = 0;
  const int rc = SSL_read(ssl_, buf, static_cast<int>(len));
  const int saved_errno = errno;
  if (rc > 0) {
    n = rc;
    return SUCCESS;
  }
  const int ssl_err = SSL_get_error(ssl_, rc);
  if (IsWantIo(ssl_err)) {
    errno = EAGAIN;
    return SUCCESS;
  }
  if (ssl_err == SSL_ERROR_ZERO_RETURN || rc == 0) {
    n = 0;
    return SUCCESS;
  }
  failed_ = true;
  const std::string error = OpensslErrorString();
  HIXL_CHK_BOOL_RET_STATUS(false, FAILED,
                           "Call api:SSL_read failed, ret:%d, ssl_err:%d, fd:%d, size:%zu bytes, errno:%d, "
                           "error_msg:%s, openssl_error:%s",
                           rc, ssl_err, fd_, len, saved_errno, ErrnoMessage(saved_errno), error.c_str());
}

Status TlsSession::WriteOnce(const void *buf, size_t len, ssize_t &n, int &ssl_error, int32_t &err_no) {
  std::lock_guard<std::mutex> lock(mutex_);
  n = -1;
  HIXL_CHK_BOOL_RET_STATUS(ssl_ != nullptr && handshake_complete_ && !failed_, FAILED,
                           "TLS session is not usable for send, fd:%d", fd_);
  HIXL_CHK_BOOL_RET_STATUS(len <= static_cast<size_t>(std::numeric_limits<int>::max()), PARAM_INVALID,
                           "TLS write length exceeds int max, fd:%d, size:%zu bytes", fd_, len);
  ERR_clear_error();
  errno = 0;
  const int rc = SSL_write(ssl_, buf, static_cast<int>(len));
  const int saved_errno = errno;
  if (rc > 0) {
    n = rc;
    return SUCCESS;
  }
  ssl_error = SSL_get_error(ssl_, rc);
  if (IsWantIo(ssl_error)) {
    return SUCCESS;
  }
  failed_ = true;
  err_no = saved_errno;
  const std::string error = OpensslErrorString();
  HIXL_CHK_BOOL_RET_STATUS(false, FAILED,
                           "Call api:SSL_write failed, ret:%d, ssl_err:%d, fd:%d, size:%zu bytes, errno:%d, "
                           "error_msg:%s, openssl_error:%s",
                           rc, ssl_error, fd_, len, saved_errno, ErrnoMessage(saved_errno), error.c_str());
}

bool TlsSession::HasPending() {
  std::lock_guard<std::mutex> lock(mutex_);
  return ssl_ != nullptr && !failed_ && SSL_pending(ssl_) > 0;
}

void TlsSession::Shutdown() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (ssl_ != nullptr && handshake_complete_ && !failed_) {
    (void)SSL_shutdown(ssl_);
  }
}
}  // namespace hixl
