/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HIXL_SERVER_RUNNER_H
#define HIXL_SERVER_RUNNER_H

#include <cstdint>
#include <optional>

#include "benchmark_config.h"
#include "hixl/hixl.h"
#include "tcp_client_server.h"

namespace hixl_benchmark {

/// Server benchmark: `Init()` 绑定 device；`Run()` 执行业务；`Shutdown()` 统一回收并 `aclrtResetDevice`。
class ServerRunner {
 public:
  explicit ServerRunner(const BenchmarkConfig &cfg) : cfg_(cfg) {}
  ~ServerRunner();

  ServerRunner(const ServerRunner &) = delete;
  ServerRunner &operator=(const ServerRunner &) = delete;

  bool Init();
  int Run();
  void Shutdown();

 private:
  const BenchmarkConfig &cfg_;
  bool device_bound_ = false;
  int32_t device_id_ = 0;

  hixl::Hixl hixl_;
  void *buffer_ = nullptr;
  hixl::MemHandle mem_handle_ = nullptr;
  bool is_host_ = false;
  bool need_register_ = false;
  bool buffer_allocated_ = false;
  bool hixl_initialized_ = false;
  bool mem_registered_ = false;

  std::optional<TcpServerSession> tcp_session_;

  void ReleaseServerResources();
  bool AllocServerBufferForRun();
  bool InitHixlAndRegisterMem();
  int CompleteTcpHandshake(std::uintptr_t addr);
};

}  // namespace hixl_benchmark

#endif  // HIXL_SERVER_RUNNER_H
