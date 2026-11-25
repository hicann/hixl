/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "cs/host_roce/host_roce_flush_cache.h"

#include <cstdlib>
#include <exception>
#include <vector>
#include "acl/acl_rt.h"
#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/hixl_utils.h"
#include "cs/host_roce/host_roce_verbs.h"

namespace hixl {
namespace {
Status ReleaseFlush(const HostRoceFlushResources &r) {
  if (r.verbs == nullptr) {
    return FAILED;
  }
  const auto tx = r.tx == nullptr ? SUCCESS : r.verbs->Stop(*r.tx);
  const auto rx = r.rx == nullptr ? SUCCESS : r.verbs->Stop(*r.rx);
  if (tx != SUCCESS || rx != SUCCESS) {
    return FAILED;
  }
  for (auto *mr : {r.local_mr.get(), r.remote_mr.get()}) {
    if (mr == nullptr) {
      continue;
    }
    HIXL_CHK_STATUS_RET(r.verbs->Deregister(*mr));
  }
  return SUCCESS;
}
}  // namespace

HostRoceFlushResources::HostRoceFlushResources() = default;

HostRoceFlushResources::~HostRoceFlushResources() {
  tx.reset();
  rx.reset();
  local_mr.reset();
  remote_mr.reset();
  {
    TemporaryRtContext guard(static_cast<aclrtContext>(context));
    if (local != nullptr && vendor_opcode) {
      (void)aclrtFree(local);
    }
    if (local != nullptr && !vendor_opcode) {
      std::free(local);
    }
    if (remote != nullptr) {
      (void)aclrtFree(remote);
    }
  }
  if (owns_context && context != nullptr) {
    (void)aclrtDestroyContext(static_cast<aclrtContext>(context));
  }
}

HostRoceNativeFlush::HostRoceNativeFlush(HostRoceVerbs *verbs) : resources(std::make_unique<HostRoceFlushResources>()) {
  resources->verbs = verbs;
}

HostRoceNativeFlush::~HostRoceNativeFlush() {
  try {
    (void)Close();
  } catch (const std::exception &e) {
    HIXL_LOGE(FAILED, "[HostRoce] Close flush resources caught exception:%s", e.what());
  }
}

Status HostRoceNativeFlush::Close() {
  if (resources == nullptr) {
    return SUCCESS;
  }
  const auto ret = ReleaseFlush(*resources);
  if (ret == SUCCESS) {
    resources.reset();
    return SUCCESS;
  }
  // A failed teardown must retain both registered buffers as well as their QPs/PD.
  static std::mutex quarantine_mutex;
  static auto *quarantine = new std::vector<std::unique_ptr<HostRoceFlushResources>>();
  std::lock_guard<std::mutex> lock(quarantine_mutex);
  quarantine->push_back(std::move(resources));
  HIXL_LOGE(ret, "[HostRoce] Retaining loopback flush resources after QP/MR teardown failure");
  return ret;
}

Status ResolveHostRoceFlushContext(uint32_t memory_device, void *&context, bool &owns_context) {
  context = nullptr;
  owns_context = false;
  HIXL_CHK_BOOL_RET_STATUS(memory_device <= static_cast<uint32_t>(INT32_MAX), PARAM_INVALID,
                           "[HostRoce] Invalid user device ID=%u for flush", memory_device);
  const auto user_device = static_cast<int32_t>(memory_device);
  (void)aclrtGetCurrentContext(&context);
  int32_t current_device = -1;
  if (context == nullptr || aclrtGetDevice(&current_device) != ACL_SUCCESS || current_device != user_device) {
    aclrtContext created = nullptr;
    HIXL_CHK_ACL_RET(aclrtCreateContext(&created, user_device),
                     "[HostRoce] Create flush allocation context failed, user_device=%d", user_device);
    const aclError set_ret = aclrtSetCurrentContext(created);
    if (set_ret != ACL_SUCCESS) {
      (void)aclrtDestroyContext(created);
      HIXL_LOGE(FAILED, "[HostRoce] Set flush allocation context failed, ret=%d, user_device=%d",
                static_cast<int32_t>(set_ret), user_device);
      return FAILED;
    }
    context = created;
    owns_context = true;
  }
  HIXL_CHK_BOOL_RET_STATUS(context != nullptr, FAILED, "[HostRoce] Local HBM flush requires an ACL context");
  return SUCCESS;
}

void HostRoceFlushCache::OnChannelConnected() {
  std::lock_guard<std::mutex> lock(mutex_);
  ++connected_channels_;
}

Status HostRoceFlushCache::OnChannelDisconnected() {
  std::map<uint32_t, Entry> released;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (connected_channels_ == 0U) {
      return SUCCESS;
    }
    if (--connected_channels_ == 0U) {
      // The cached QPs and MRs own references to the RoCE device.
      released.swap(entries_);
    }
  }
  Status first_error = SUCCESS;
  for (const auto &entry : released) {
    if (entry.second.flush != nullptr) {
      const auto ret = entry.second.flush->Close();
      if (first_error == SUCCESS && ret != SUCCESS) {
        first_error = ret;
      }
    }
  }
  return first_error;
}

void HostRoceFlushCache::PollActive(HostRoceVerbs &verbs, Entry &entry, std::unique_ptr<HostRoceFlush> &failed) {
  auto &active = *entry.active;
  active.status = verbs.PollFlush(*entry.flush, active.done);
  if (active.status != SUCCESS) {
    active.done = true;
    active.teardown_failure = entry.flush->Close();
    failed = std::move(entry.flush);
  }
  if (active.done) {
    entry.active = nullptr;
  }
}

Status HostRoceFlushCache::Start(HostRoceVerbs &verbs, const std::shared_ptr<HostRoceDevice> &device,
                                 const std::shared_ptr<HostRoceMr> &target, const HostRoceConfig &config, Entry &entry,
                                 HostRoceFlushRequest &request) {
  if (entry.flush == nullptr) {
    const auto ret = verbs.CreateFlush(device, target, config, entry.flush);
    if (ret != SUCCESS || entry.flush == nullptr) {
      return ret == SUCCESS ? FAILED : ret;
    }
  }
  const auto ret = verbs.PostFlush(*entry.flush);
  if (ret == SUCCESS) {
    entry.active = &request;
  }
  return ret;
}

Status HostRoceFlushCache::Advance(HostRoceVerbs &verbs, const std::shared_ptr<HostRoceDevice> &device,
                                   const std::shared_ptr<HostRoceMr> &target, const HostRoceConfig &config,
                                   HostRoceFlushRequest &request, bool &done) {
  done = false;
  HIXL_CHECK_NOTNULL(device);
  HIXL_CHECK_NOTNULL(target);
  std::unique_ptr<HostRoceFlush> failed;
  bool close_failed_for_request = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!request.done) {
      auto &entry = entries_[target->memory_device];
      // A caller querying another channel can retire the previous flush, even if its owner is idle.
      if (entry.active != nullptr) {
        PollActive(verbs, entry, failed);
      }
      if (entry.active == nullptr && !request.done && failed == nullptr) {
        request.status = Start(verbs, device, target, config, entry, request);
        if (request.status != SUCCESS) {
          request.done = true;
          if (entry.flush != nullptr) {
            failed = std::move(entry.flush);
            close_failed_for_request = true;
          }
        }
      }
    }
    done = request.done;
  }
  if (close_failed_for_request && failed != nullptr) {
    request.teardown_failure = failed->Close();
  }
  return request.status;
}

Status HostRoceFlushCache::Discard(uint32_t memory_device, const HostRoceFlushRequest &request) {
  std::unique_ptr<HostRoceFlush> discarded;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (request.teardown_failure != SUCCESS) {
      return request.teardown_failure;
    }
    const auto it = entries_.find(memory_device);
    if (it != entries_.end() && it->second.active == &request) {
      discarded = std::move(it->second.flush);
      entries_.erase(it);
    }
  }
  return discarded == nullptr ? SUCCESS : discarded->Close();
}

}  // namespace hixl
