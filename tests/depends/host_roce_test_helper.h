/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HIXL_TESTS_DEPENDS_HOST_ROCE_TEST_HELPER_H_
#define HIXL_TESTS_DEPENDS_HOST_ROCE_TEST_HELPER_H_
#include <arpa/inet.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <deque>
#include <functional>
#include <thread>
#include <utility>
#include "cs/host_roce/host_roce_verbs.h"
namespace hixl::test {
class FakeHostRoceVerbs : public HostRoceVerbs {
 public:
  struct PostRecord {
    uint64_t local;
    uint64_t remote;
    uint32_t size;
    uint64_t id;
    bool fence;
    bool signaled;
    bool read;
    size_t pending_before;
  };
  struct Qp : HostRoceQp {
    std::deque<HostRoceCompletion> completions;
    std::deque<PostRecord> pending;
    bool stopped{false};
    uint32_t round{0};
  };
  Status Open(const std::string &ip, std::shared_ptr<HostRoceDevice> &out) override {
    std::array<uint8_t, 16> address{};
    if (inet_pton(AF_INET, ip.c_str(), address.data()) != 1 && inet_pton(AF_INET6, ip.c_str(), address.data()) != 1)
      return PARAM_INVALID;
    std::lock_guard<std::mutex> lock(mutex);
    auto &device = devices[ip];
    if (!device) {
      device = std::make_shared<HostRoceDevice>();
      device->local.mtu = mtu.load();
      device->local.read_depth = 8;
      device->local.plane_count = planes.load();
      device->local.gid[15] = static_cast<uint8_t>(devices.size());
      device->max_wr = 2;
      device->max_cqe = 2;
      device->max_message_size = 4;
    }
    out = device;
    return SUCCESS;
  }
  Status Register(const std::shared_ptr<HostRoceDevice> &, uint64_t base, uint64_t size, bool device_memory,
                  uint32_t memory_device, std::shared_ptr<HostRoceMr> &out) override {
    auto mr = std::make_shared<HostRoceMr>();
    mr->base = base;
    mr->size = size;
    mr->lkey = ++registrations;
    mr->rkey = mr->lkey + 100;
    last_registered_device_memory = device_memory;
    last_registered_memory_device = memory_device;
    mr->memory_device =
        device_memory ? (memory_device != UINT32_MAX ? memory_device : registered_memory_device.load()) : UINT32_MAX;
    out = mr;
    return SUCCESS;
  }
  Status Deregister(HostRoceMr &) override {
    return deregister_result.load();
  }
  struct NegoInput {
    uint32_t local_qpn{0};
    uint8_t feature1{0};
  };
  Status CreateQp(const std::shared_ptr<HostRoceDevice> &d, const HostRoceConfig &config,
                  std::unique_ptr<HostRoceQp> &out) override {
    auto q = std::make_unique<Qp>();
    q->capacity = qp_capacity.load();
    q->local = d->local;
    q->local.qpn = ++created;
    q->local.psn = q->local.qpn;
    q->local.has_feature = more_rounds.load() != 0;
    q->local.feature[1] = 0xA5;
    {
      std::lock_guard<std::mutex> lock(mutex);
      created_configs.push_back(config);
    }
    out = std::move(q);
    return create_qp_result.load();
  }
  Status Negotiate(HostRoceQp &q, const HostRocePeer &peer, HostRocePeer &out) override {
    std::lock_guard<std::mutex> lock(mutex);
    auto &qp = static_cast<Qp &>(q);
    nego_inputs.push_back({q.local.qpn, peer.feature[1]});
    out = q.local;
    out.more = ++qp.round <= more_rounds.load();
    // Distinguish a supported feature from a nego output. The peer must consume the former first.
    out.feature[1] = static_cast<uint8_t>(qp.round);
    return SUCCESS;
  }
  Status Connect(HostRoceQp &q, const HostRocePeer &peer, const HostRoceConfig &) override {
    std::lock_guard<std::mutex> lock(mutex);
    connected_planes.push_back(q.local.plane);
    connected_mtus.emplace_back(q.local.mtu, peer.mtu);
    return SUCCESS;
  }
  Status PostBatch(HostRoceQp &base, bool read, const std::vector<HostRoceWorkRequest> &wrs, size_t &posted) override {
    if (!read && before_write_post != nullptr) {
      before_write_post();
    }
    uint32_t post_delay = 0;
    Status result = SUCCESS;
    {
      std::lock_guard<std::mutex> lock(mutex);
      auto &q = static_cast<Qp &>(base);
      posted = 0;
      batch_sizes.push_back(wrs.size());
      for (const auto &wr : wrs) {
        const auto ret = PostOne(q, read, wr, post_delay);
        if (ret != SUCCESS) {
          batch_posted.push_back(posted);
          result = ret;
          break;
        }
        ++posted;
      }
      if (result == SUCCESS) {
        batch_posted.push_back(posted);
      }
    }
    if (post_delay != 0U) {
      std::this_thread::sleep_for(std::chrono::milliseconds(post_delay));
    }
    return result;
  }
  Status PostOne(Qp &q, bool read, const HostRoceWorkRequest &wr, uint32_t &post_delay) {
    if (q.stopped) return FAILED;
    if (q.pending.size() >= q.capacity) return RESOURCE_EXHAUSTED;
    uint64_t expected_fail = wr.id;
    if (wr.id != 0 && fail_id_once.compare_exchange_strong(expected_fail, 0)) {
      return FAILED;
    }
    bool expected_reject = true;
    uint64_t expected_reject_id = wr.id;
    if (reject_once.compare_exchange_strong(expected_reject, false) ||
        (wr.id != 0 && reject_id_once.compare_exchange_strong(expected_reject_id, 0))) {
      return RESOURCE_EXHAUSTED;
    }
    const PostRecord record{wr.local, wr.remote, wr.size, wr.id, wr.fence, wr.signaled, read, q.pending.size()};
    if (!defer_copies.load() && !model_read_pipeline.load()) Copy(record);
    posts.push_back(record);
    q.pending.push_back(record);
    const auto read_err = read_completion_error.load();
    const auto error = read && read_err != 0 ? read_err : completion_error.load();
    if (!model_read_pipeline.load() && (wr.signaled || error != 0)) {
      q.completions.push_back({wr.id, error, 17});
      ++generated_completions;
    }
    if (read) {
      const auto delay = read_post_delay_ms.exchange(0);
      if (delay != 0) {
        post_delay = delay;
      }
    }
    return SUCCESS;
  }
  void RunReadWave(Qp &q) {
    if (q.pending.empty()) return;
    const auto limit = q.pending.front().read ? std::max(1U, q.local.read_depth) : 1U;
    std::vector<PostRecord> wave;
    while (!q.pending.empty() && wave.size() < limit) {
      const auto &next = q.pending.front();
      if (!wave.empty() && (next.fence || next.read != wave.front().read)) break;
      wave.push_back(next);
      q.pending.pop_front();
    }
    if (wave.front().read) {
      ++read_waves;
      size_t prev = max_inflight_reads.load();
      while (prev < wave.size() && !max_inflight_reads.compare_exchange_weak(prev, wave.size())) {
      }
    }
    // Unfenced reads may finish out of order. Reverse completion exposes missing alias barriers.
    for (auto it = wave.rbegin(); it != wave.rend(); ++it) {
      const auto read_err = read_completion_error.load();
      const auto error = it->read && read_err != 0 ? read_err : completion_error.load();
      if (error == 0) Copy(*it);
      if (it->signaled || error != 0) {
        q.completions.push_back({it->id, error, 17});
        ++generated_completions;
      }
    }
  }
  Status Poll(HostRoceQp &base, std::vector<HostRoceCompletion> &out) override {
    const auto delay = delay_poll_ms.exchange(0);
    if (delay != 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    }
    std::lock_guard<std::mutex> lock(mutex);
    auto &q = static_cast<Qp &>(base);
    out.clear();
    if (hold_completions.load()) return SUCCESS;
    if (model_read_pipeline.load()) RunReadWave(q);
    const auto limit = poll_limit.load();
    while (!q.completions.empty() && out.size() < limit) {
      const auto completion = q.completions.front();
      q.completions.pop_front();
      while (!q.pending.empty() && q.pending.front().id <= completion.id) {
        if (defer_copies.load() && completion.status == 0) Copy(q.pending.front());
        q.pending.pop_front();
      }
      out.push_back(completion);
      ++polled_completions;
    }
    return SUCCESS;
  }
  struct Flush : HostRoceFlush {
    FakeHostRoceVerbs *verbs{nullptr};
    uint32_t memory_device{0};
    bool posted{false};
    std::chrono::steady_clock::time_point ready_at{};
    Status Close() override {
      return verbs->flush_close_result.load();
    }
    ~Flush() override {
      if (verbs != nullptr) {
        ++verbs->flush_destroyed;
      }
    }
  };
  Status CreateFlush(const std::shared_ptr<HostRoceDevice> &device, const std::shared_ptr<HostRoceMr> &target,
                     const HostRoceConfig &config, std::unique_ptr<HostRoceFlush> &out) override {
    ++flush_created;
    auto flush = std::make_unique<Flush>();
    flush->verbs = this;
    flush->memory_device = target->memory_device;
    {
      std::lock_guard<std::mutex> lock(mutex);
      flush_vendor_modes.push_back(device->local.plane_count != 0U);
      flush_configs.push_back(config);
    }
    out = std::move(flush);
    return create_flush_result.load();
  }
  Status PostFlush(HostRoceFlush &base) override {
    std::function<void()> callback;
    {
      std::lock_guard<std::mutex> lock(mutex);
      auto &flush = static_cast<Flush &>(base);
      flushed_devices.push_back(flush.memory_device);
      flush_post_counts.push_back(posts.size());
      callback = check_flush;
      const Status post_ret = flush_post_result.load();
      if (post_ret != SUCCESS) {
        return post_ret;
      }
      flush.posted = true;
      const uint32_t delay = flush_delay_ms.load();
      flush.ready_at = delay == 0U ? std::chrono::steady_clock::time_point::min()
                                   : std::chrono::steady_clock::now() + std::chrono::milliseconds(delay);
    }
    if (callback) {
      callback();
    }
    ++flush_posted;
    return SUCCESS;
  }
  Status PollFlush(HostRoceFlush &base, bool &done) override {
    done = false;
    std::lock_guard<std::mutex> lock(mutex);
    auto &flush = static_cast<Flush &>(base);
    if (!flush.posted || hold_flush_completions.load()) {
      return SUCCESS;
    }
    if (flush.ready_at != std::chrono::steady_clock::time_point::min() &&
        std::chrono::steady_clock::now() < flush.ready_at) {
      return SUCCESS;
    }
    flush.posted = false;
    const Status ret = flush_result.load();
    if (ret != SUCCESS) {
      return ret;
    }
    done = true;
    ++flush_completed;
    return SUCCESS;
  }
  Status Stop(HostRoceQp &base) override {
    std::lock_guard<std::mutex> lock(mutex);
    const auto ret = stop_result.load();
    if (ret != SUCCESS) return ret;
    auto &q = static_cast<Qp &>(base);
    if (!q.stopped) ++stopped;
    q.stopped = true;
    q.completions.clear();
    q.pending.clear();
    return SUCCESS;
  }
  void Copy(const PostRecord &r) {
    if (!copy_data.load()) return;
    if (r.read)
      std::memcpy(reinterpret_cast<void *>(r.local), reinterpret_cast<void *>(r.remote), r.size);
    else
      std::memcpy(reinterpret_cast<void *>(r.remote), reinterpret_cast<void *>(r.local), r.size);
  }
  size_t PostCount() {
    std::lock_guard<std::mutex> lock(mutex);
    return posts.size();
  }
  std::vector<PostRecord> PostsSnapshot() {
    std::lock_guard<std::mutex> lock(mutex);
    return posts;
  }
  std::mutex mutex;
  std::map<std::string, std::shared_ptr<HostRoceDevice>> devices;
  std::vector<PostRecord> posts;
  std::vector<size_t> batch_sizes;
  std::vector<size_t> batch_posted;
  std::atomic<uint32_t> generated_completions{0};
  std::atomic<uint32_t> polled_completions{0};
  std::atomic<size_t> max_inflight_reads{0};
  std::atomic<uint32_t> read_waves{0};
  std::vector<NegoInput> nego_inputs;
  std::vector<uint32_t> connected_planes;
  std::vector<std::pair<uint32_t, uint32_t>> connected_mtus;
  std::vector<HostRoceConfig> created_configs;
  std::atomic<uint32_t> created{0};
  std::atomic<uint32_t> stopped{0};
  std::atomic<Status> create_qp_result{SUCCESS};
  std::atomic<Status> stop_result{SUCCESS};
  std::atomic<uint32_t> registrations{0};
  std::atomic<Status> deregister_result{SUCCESS};
  std::atomic<uint32_t> planes{4};
  std::atomic<uint32_t> mtu{3};
  std::atomic<uint32_t> completion_error{0};
  std::atomic<uint32_t> read_completion_error{0};
  std::atomic<uint32_t> read_post_delay_ms{0};
  std::atomic<uint32_t> more_rounds{0};
  std::atomic<uint32_t> qp_capacity{2};
  std::atomic<uint32_t> poll_limit{32};
  std::atomic<bool> defer_copies{false};
  std::atomic<bool> model_read_pipeline{false};
  std::atomic<bool> hold_completions{false};
  std::atomic<uint32_t> delay_poll_ms{0};
  std::atomic<bool> copy_data{true};
  std::atomic<bool> reject_once{false};
  std::atomic<uint64_t> reject_id_once{0};
  std::atomic<uint64_t> fail_id_once{0};
  std::atomic<uint32_t> registered_memory_device{0};
  std::atomic<bool> last_registered_device_memory{false};
  std::atomic<uint32_t> last_registered_memory_device{UINT32_MAX};
  std::atomic<uint32_t> flush_created{0};
  std::atomic<uint32_t> flush_posted{0};
  std::atomic<uint32_t> flush_completed{0};
  std::atomic<uint32_t> flush_destroyed{0};
  std::vector<uint32_t> flushed_devices;
  std::vector<bool> flush_vendor_modes;
  std::vector<HostRoceConfig> flush_configs;
  std::vector<size_t> flush_post_counts;
  std::function<void()> before_write_post;
  std::function<void()> check_flush;
  std::atomic<Status> create_flush_result{SUCCESS};
  std::atomic<Status> flush_post_result{SUCCESS};
  std::atomic<Status> flush_result{SUCCESS};
  std::atomic<Status> flush_close_result{SUCCESS};
  std::atomic<bool> hold_flush_completions{false};
  std::atomic<uint32_t> flush_delay_ms{0};
};
}  // namespace hixl::test
#endif
