/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "tls_context.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

#include <openssl/ssl.h>

#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/tls/tls_openssl_utils.h"

namespace hixl {
namespace {
constexpr int kVerifyDepth = 4;
}

Status TlsContext::Create(const TlsConfig &config, std::shared_ptr<TlsContext> &ctx) {
  auto created = std::shared_ptr<TlsContext>(new (std::nothrow) TlsContext());
  HIXL_CHK_BOOL_RET_STATUS(created != nullptr, FAILED, "[TlsContext] Failed to allocate TlsContext");
  HIXL_CHK_STATUS_RET(created->Init(config), "[TlsContext] Failed to init SSL_CTX");
  ctx = std::move(created);
  return SUCCESS;
}

TlsContext::~TlsContext() {
  if (server_ctx_ != nullptr) {
    SSL_CTX_free(server_ctx_);
    server_ctx_ = nullptr;
  }
  if (client_ctx_ != nullptr) {
    SSL_CTX_free(client_ctx_);
    client_ctx_ = nullptr;
  }
}

Status TlsContext::Init(const TlsConfig &config) {
  config_ = config;
  HIXL_LOGI("[TlsContext] Initializing contexts, verify_peer:%s, cert_refresh:%s",
            config_.verify_peer ? "true" : "false", config_.enable_cert_refresh ? "true" : "false");
  HIXL_CHK_STATUS_RET(CreateSslCtx(true, &server_ctx_), "[TlsContext] Failed to create server SSL_CTX");
  HIXL_CHK_STATUS_RET(CreateSslCtx(false, &client_ctx_), "[TlsContext] Failed to create client SSL_CTX");
  HIXL_CHK_STATUS_RET(LoadCertAndKey(server_ctx_), "[TlsContext] Failed to load server cert/key");
  HIXL_CHK_STATUS_RET(LoadCertAndKey(client_ctx_), "[TlsContext] Failed to load client cert/key");
  HIXL_CHK_STATUS_RET(ApplyVerifyAndCiphers(server_ctx_), "[TlsContext] Failed to apply server verify/ciphers");
  HIXL_CHK_STATUS_RET(ApplyVerifyAndCiphers(client_ctx_), "[TlsContext] Failed to apply client verify/ciphers");
  HIXL_LOGI("[TlsContext] Server and client contexts initialized, minimum_protocol:TLSv1.2");
  return SUCCESS;
}

Status TlsContext::CreateSslCtx(bool is_server, SSL_CTX **out_ctx) const {
  SSL_CTX *ctx = SSL_CTX_new(is_server ? TLS_server_method() : TLS_client_method());
  // CHECK macros evaluate log arguments twice; capture the consuming OpenSSL error lookup once.
  const std::string ctx_error = ctx != nullptr ? "" : OpensslErrorString();
  HIXL_CHK_BOOL_RET_STATUS(ctx != nullptr, FAILED, "Call api:SSL_CTX_new failed, openssl_error:%s, is_server:%s",
                           ctx_error.c_str(), is_server ? "true" : "false");
  const int proto_ret = SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
  if (proto_ret != 1) {
    const std::string error = OpensslErrorString();
    SSL_CTX_free(ctx);
    HIXL_CHK_BOOL_RET_STATUS(false, FAILED,
                             "Call api:SSL_CTX_set_min_proto_version failed, ret:%d, is_server:%s, openssl_error:%s",
                             proto_ret, is_server ? "true" : "false", error.c_str());
  }
  *out_ctx = ctx;
  return SUCCESS;
}

Status TlsContext::LoadCertAndKey(SSL_CTX *ctx) const {
  const int cert_ret = SSL_CTX_use_certificate_chain_file(ctx, config_.cert_file.c_str());
  const std::string cert_error = cert_ret == 1 ? "" : OpensslErrorString();
  HIXL_CHK_BOOL_RET_STATUS(cert_ret == 1, FAILED,
                           "Call api:SSL_CTX_use_certificate_chain_file failed, ret:%d, path:%s, openssl_error:%s",
                           cert_ret, config_.cert_file.c_str(), cert_error.c_str());
  const int key_ret = SSL_CTX_use_PrivateKey_file(ctx, config_.key_file.c_str(), SSL_FILETYPE_PEM);
  const std::string key_error = key_ret == 1 ? "" : OpensslErrorString();
  HIXL_CHK_BOOL_RET_STATUS(key_ret == 1, FAILED,
                           "Call api:SSL_CTX_use_PrivateKey_file failed, ret:%d, path:%s, openssl_error:%s", key_ret,
                           config_.key_file.c_str(), key_error.c_str());
  const int check_ret = SSL_CTX_check_private_key(ctx);
  const std::string check_error = check_ret == 1 ? "" : OpensslErrorString();
  HIXL_CHK_BOOL_RET_STATUS(check_ret == 1, FAILED,
                           "Call api:SSL_CTX_check_private_key failed, ret:%d, openssl_error:%s", check_ret,
                           check_error.c_str());
  return SUCCESS;
}

Status TlsContext::ApplyVerifyAndCiphers(SSL_CTX *ctx) const {
  if (!config_.ca_file.empty()) {
    const int ca_ret = SSL_CTX_load_verify_locations(ctx, config_.ca_file.c_str(), nullptr);
    const std::string ca_error = ca_ret == 1 ? "" : OpensslErrorString();
    HIXL_CHK_BOOL_RET_STATUS(ca_ret == 1, FAILED,
                             "Call api:SSL_CTX_load_verify_locations failed, ret:%d, path:%s, openssl_error:%s", ca_ret,
                             config_.ca_file.c_str(), ca_error.c_str());
  }
  if (config_.verify_peer) {
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, nullptr);
    SSL_CTX_set_verify_depth(ctx, kVerifyDepth);
  } else {
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);
  }
  if (config_.ciphers.empty()) {
    return SUCCESS;
  }
  const int cipher_ret = SSL_CTX_set_cipher_list(ctx, config_.ciphers.c_str());
  const std::string cipher_error = cipher_ret == 1 ? "" : OpensslErrorString();
  HIXL_CHK_BOOL_RET_STATUS(cipher_ret == 1, PARAM_INVALID,
                           "Call api:SSL_CTX_set_cipher_list failed, ret:%d, ciphers:%s, openssl_error:%s", cipher_ret,
                           config_.ciphers.c_str(), cipher_error.c_str());
  return SUCCESS;
}

Status TlsContext::CreateSession(const std::shared_ptr<TlsContext> &context, int32_t fd, bool is_server,
                                 std::shared_ptr<TlsSession> &session) {
  HIXL_CHK_BOOL_RET_STATUS(context != nullptr, PARAM_INVALID, "[TlsContext] Context is null, fd:%d", fd);
  HIXL_CHK_BOOL_RET_STATUS(fd >= 0, PARAM_INVALID, "[TlsContext] Invalid fd:%d", fd);
  SSL_CTX *ctx = is_server ? context->server_ctx_ : context->client_ctx_;
  HIXL_CHK_BOOL_RET_STATUS(ctx != nullptr, FAILED, "[TlsContext] SSL_CTX is null, is_server:%d",
                           static_cast<int32_t>(is_server));
  SSL *ssl = SSL_new(ctx);
  const std::string ssl_error = ssl != nullptr ? "" : OpensslErrorString();
  HIXL_CHK_BOOL_RET_STATUS(ssl != nullptr, FAILED, "Call api:SSL_new failed, openssl_error:%s, fd:%d",
                           ssl_error.c_str(), fd);
  const int fd_ret = SSL_set_fd(ssl, fd);
  if (fd_ret != 1) {
    const std::string error = OpensslErrorString();
    SSL_free(ssl);
    HIXL_CHK_BOOL_RET_STATUS(false, FAILED, "Call api:SSL_set_fd failed, ret:%d, openssl_error:%s, fd:%d", fd_ret,
                             error.c_str(), fd);
  }
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0 || fcntl(fd, F_SETFL, static_cast<uint32_t>(flags) | static_cast<uint32_t>(O_NONBLOCK)) != 0) {
    const int saved_errno = errno;
    SSL_free(ssl);
    HIXL_CHK_BOOL_RET_STATUS(false, FAILED, "Call api:fcntl failed, fd:%d, errno:%d, error_msg:%s", fd, saved_errno,
                             strerror(saved_errno));
  }
  session.reset(new (std::nothrow) TlsSession(ssl, context, fd));
  if (session == nullptr) {
    SSL_free(ssl);
    HIXL_CHK_BOOL_RET_STATUS(false, FAILED, "[TlsContext] Failed to allocate TlsSession, fd:%d", fd);
  }
  HIXL_LOGD("[TlsContext] Created SSL session, fd:%d, role:%s, nonblocking:true", fd, is_server ? "server" : "client");
  return SUCCESS;
}
}  // namespace hixl
