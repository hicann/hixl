/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_CS_HOST_ROCE_FLUSH_CACHE_H_
#define CANN_HIXL_SRC_HIXL_CS_HOST_ROCE_FLUSH_CACHE_H_

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include "hixl/hixl_types.h"

namespace hixl {

class HostRoceVerbs;
struct HostRoceConfig;
struct HostRoceDevice;
struct HostRoceMr;
struct HostRoceQp;

struct HostRoceFlush {
  HostRoceFlush() = default;
  HostRoceFlush(const HostRoceFlush &) = delete;
  HostRoceFlush &operator=(const HostRoceFlush &) = delete;
  HostRoceFlush(HostRoceFlush &&) = delete;
  HostRoceFlush &operator=(HostRoceFlush &&) = delete;
  virtual ~HostRoceFlush() = default;
  virtual Status Close() {
    return SUCCESS;
  }
};

struct HostRoceFlushRequest {
  bool done{false};
  Status status{SUCCESS};
  Status teardown_failure{SUCCESS};
};

struct HostRoceFlushResources {
  HostRoceFlushResources();
  HostRoceFlushResources(const HostRoceFlushResources &) = delete;
  HostRoceFlushResources &operator=(const HostRoceFlushResources &) = delete;
  HostRoceFlushResources(HostRoceFlushResources &&) = delete;
  HostRoceFlushResources &operator=(HostRoceFlushResources &&) = delete;
  ~HostRoceFlushResources();
  HostRoceVerbs *verbs{nullptr};
  std::unique_ptr<HostRoceQp> tx;
  std::unique_ptr<HostRoceQp> rx;
  std::shared_ptr<HostRoceMr> local_mr;
  std::shared_ptr<HostRoceMr> remote_mr;
  void *context{nullptr};
  bool owns_context{false};
  void *local{nullptr};
  void *remote{nullptr};
  bool vendor_opcode{false};
};

struct HostRoceNativeFlush final : HostRoceFlush {
  explicit HostRoceNativeFlush(HostRoceVerbs *verbs);
  ~HostRoceNativeFlush() override;
  Status Close() override;
  std::unique_ptr<HostRoceFlushResources> resources;
};

// Share one loopback QP pair per HBM device across channels using this RoCE device.
class HostRoceFlushCache {
 public:
  HostRoceFlushCache() = default;
  HostRoceFlushCache(const HostRoceFlushCache &) = delete;
  HostRoceFlushCache &operator=(const HostRoceFlushCache &) = delete;
  HostRoceFlushCache(HostRoceFlushCache &&) = delete;
  HostRoceFlushCache &operator=(HostRoceFlushCache &&) = delete;
  void OnChannelConnected();
  Status OnChannelDisconnected();
  Status Advance(HostRoceVerbs &verbs, const std::shared_ptr<HostRoceDevice> &device,
                 const std::shared_ptr<HostRoceMr> &target, const HostRoceConfig &config, HostRoceFlushRequest &request,
                 bool &done);
  Status Discard(uint32_t memory_device, const HostRoceFlushRequest &request);

 private:
  struct Entry {
    std::unique_ptr<HostRoceFlush> flush;
    HostRoceFlushRequest *active{nullptr};
  };

  static void PollActive(HostRoceVerbs &verbs, Entry &entry, std::unique_ptr<HostRoceFlush> &failed);
  static Status Start(HostRoceVerbs &verbs, const std::shared_ptr<HostRoceDevice> &device,
                      const std::shared_ptr<HostRoceMr> &target, const HostRoceConfig &config, Entry &entry,
                      HostRoceFlushRequest &request);

  std::mutex mutex_;
  size_t connected_channels_{0};
  std::map<uint32_t, Entry> entries_;
};

Status ResolveHostRoceFlushContext(uint32_t memory_device, void *&context, bool &owns_context);

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_CS_HOST_ROCE_FLUSH_CACHE_H_
