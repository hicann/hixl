/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_COMMON_TLS_TLS_MANAGER_H_
#define CANN_HIXL_SRC_HIXL_COMMON_TLS_TLS_MANAGER_H_

#include <cstddef>
#include <memory>
#include <mutex>
#include <shared_mutex>

#include "hixl/hixl_types.h"
#include "common/tls/tls_config.h"
#include "common/tls/tls_context.h"

namespace hixl {

class TlsManager {
 public:
  static TlsManager &GetInstance();

  // Each successful owner initialization must be paired with exactly one Finalize.
  Status Initialize(const TlsConfig &config);
  // Reuses the process TLS context when config has no explicit TLS fields and TLS is already enabled.
  Status InitializeOrRetain(const TlsConfig &config);
  void Finalize();
  // Reads the process mode without acquiring an owner reference; an owner must remain alive during connection use.
  Status GetTlsEnabled(bool &enabled) const;
  bool IsInitialized() const;
  std::shared_ptr<TlsContext> GetContext() const;
  TlsConfig GetConfig() const;

 private:
  TlsManager() = default;
  ~TlsManager();
  TlsManager(const TlsManager &) = delete;
  TlsManager &operator=(const TlsManager &) = delete;
  Status EnableTlsLocked(const TlsConfig &config);
  Status InitializeLocked(const TlsConfig &config);

  // Serializes initialization and finalization.
  std::mutex lifecycle_mutex_;
  mutable std::shared_mutex mutex_;
  std::shared_ptr<TlsContext> ctx_;
  TlsConfig config_;
  // References held by engines and standalone CS/listener owners, never by connections.
  size_t users_ = 0U;
};
}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_COMMON_TLS_TLS_MANAGER_H_
