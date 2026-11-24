/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_CS_HOST_ROCE_VERBS_H_
#define CANN_HIXL_SRC_HIXL_CS_HOST_ROCE_VERBS_H_

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <tuple>
#include <vector>
#include "cs/host_roce/host_roce_flush_cache.h"
#include "hixl/hixl_types.h"

namespace hixl {
constexpr uint32_t kHostRoceNoPlane = UINT32_MAX;
constexpr size_t kHostRoceGidBytes = 16U;
constexpr size_t kHostRoceFeatureBytes = 64U;
constexpr uint32_t kDefaultHostRoceTc = 132U;
constexpr uint32_t kDefaultHostRoceSl = 4U;
constexpr uint32_t kDefaultHostRoceRetryCount = 7U;
constexpr uint32_t kDefaultHostRoceTimeout = 20U;
constexpr uint32_t kDefaultHostRoceQueueDepth = 1024U;
constexpr int kHostRocePollBatchSize = 32;
struct HostRocePeer {
  uint32_t qpn{0};
  uint32_t psn{0};
  uint32_t mtu{0};  // Provider enum rather than bytes; preserve values beyond the standard 1..5 range.
  uint32_t read_depth{0};
  uint32_t plane_count{0};
  uint32_t plane{kHostRoceNoPlane};
  std::array<uint8_t, kHostRoceGidBytes> gid{};
  std::array<uint8_t, kHostRoceFeatureBytes> feature{};
  bool has_feature{false};
  bool more{false};
};
struct HostRoceConfig {
  uint32_t tc{kDefaultHostRoceTc};
  uint32_t sl{kDefaultHostRoceSl};
  uint32_t retry_count{kDefaultHostRoceRetryCount};
  uint32_t timeout{kDefaultHostRoceTimeout};
  uint32_t queue_depth{kDefaultHostRoceQueueDepth};
};
struct HostRoceMr {
  HostRoceMr() = default;
  HostRoceMr(const HostRoceMr &) = delete;
  HostRoceMr &operator=(const HostRoceMr &) = delete;
  HostRoceMr(HostRoceMr &&) = delete;
  HostRoceMr &operator=(HostRoceMr &&) = delete;
  virtual ~HostRoceMr() = default;
  uint64_t base{0};
  uint64_t size{0};
  uint32_t lkey{0};
  uint32_t rkey{0};
  uint32_t memory_device{UINT32_MAX};
};
struct HostRoceDevice {
  HostRoceDevice() = default;
  HostRoceDevice(const HostRoceDevice &) = delete;
  HostRoceDevice &operator=(const HostRoceDevice &) = delete;
  HostRoceDevice(HostRoceDevice &&) = delete;
  HostRoceDevice &operator=(HostRoceDevice &&) = delete;
  virtual ~HostRoceDevice() = default;
  HostRocePeer local;
  uint32_t max_wr{0};
  uint32_t max_cqe{0};
  uint32_t max_message_size{0};
  bool resolve_device_memory{false};
  // Persist across endpoint/channel creation on the same NIC.
  std::shared_ptr<std::atomic<uint64_t>> next_plane{std::make_shared<std::atomic<uint64_t>>(0)};
  std::mutex memory_mutex;
  std::map<std::tuple<uint64_t, uint64_t, bool>, std::weak_ptr<HostRoceMr>> registrations;
  HostRoceFlushCache flush_cache;
};
struct HostRoceQp {
  HostRoceQp() = default;
  HostRoceQp(const HostRoceQp &) = delete;
  HostRoceQp &operator=(const HostRoceQp &) = delete;
  HostRoceQp(HostRoceQp &&) = delete;
  HostRoceQp &operator=(HostRoceQp &&) = delete;
  virtual ~HostRoceQp() = default;
  HostRocePeer local;
  uint32_t capacity{0};
};
struct HostRoceCompletion {
  uint64_t id{0};
  uint32_t status{0};
  uint32_t vendor_error{0};
};
struct HostRoceWorkRequest {
  uint64_t local{0};
  uint64_t remote{0};
  uint64_t id{0};
  uint32_t lkey{0};
  uint32_t rkey{0};
  uint32_t size{0};
  bool signaled{false};
  bool fence{false};
};
// The implementation includes real verbs headers; transport state and tests do not depend on provider ABI layouts.
class HostRoceVerbs {
 public:
  HostRoceVerbs() = default;
  HostRoceVerbs(const HostRoceVerbs &) = delete;
  HostRoceVerbs &operator=(const HostRoceVerbs &) = delete;
  HostRoceVerbs(HostRoceVerbs &&) = delete;
  HostRoceVerbs &operator=(HostRoceVerbs &&) = delete;
  virtual ~HostRoceVerbs() = default;
  // Accept numeric IPv4/IPv6 literals without the display prefixes used in endpoint logs.
  virtual Status Open(const std::string &ip, std::shared_ptr<HostRoceDevice> &device) = 0;
  virtual Status Register(const std::shared_ptr<HostRoceDevice> &device, uint64_t base, uint64_t size,
                          bool device_memory, uint32_t memory_device, std::shared_ptr<HostRoceMr> &mr) = 0;
  virtual Status Deregister(HostRoceMr &mr) = 0;
  // On failure a partially created QP may still require Stop().
  virtual Status CreateQp(const std::shared_ptr<HostRoceDevice> &device, const HostRoceConfig &config,
                          std::unique_ptr<HostRoceQp> &qp) = 0;
  virtual Status Negotiate(HostRoceQp &qp, const HostRocePeer &peer, HostRocePeer &local) = 0;
  virtual Status Connect(HostRoceQp &qp, const HostRocePeer &peer, const HostRoceConfig &config) = 0;
  // posted is the successfully enqueued prefix, including when the provider returns an error.
  virtual Status PostBatch(HostRoceQp &qp, bool read, const std::vector<HostRoceWorkRequest> &wrs, size_t &posted) = 0;
  virtual Status Poll(HostRoceQp &qp, std::vector<HostRoceCompletion> &completions) = 0;
  // On failure a partially created flush may still require Close().
  virtual Status CreateFlush(const std::shared_ptr<HostRoceDevice> &device, const std::shared_ptr<HostRoceMr> &target,
                             const HostRoceConfig &config, std::unique_ptr<HostRoceFlush> &flush) = 0;
  // Non-blocking local loopback flush for Device HBM GET visibility.
  virtual Status PostFlush(HostRoceFlush &flush) = 0;
  virtual Status PollFlush(HostRoceFlush &flush, bool &done) = 0;
  // Destroy the QP before releasing any buffers; return only after the provider can no longer DMA through it.
  virtual Status Stop(HostRoceQp &qp) = 0;
};
std::shared_ptr<HostRoceVerbs> GetHostRoceVerbs();
#ifdef HIXL_ENABLE_TEST_HOOKS
void SetHostRoceVerbsForTest(std::shared_ptr<HostRoceVerbs> verbs);
#endif
}  // namespace hixl
#endif
