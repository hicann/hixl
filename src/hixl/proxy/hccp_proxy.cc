/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hccp_proxy.h"
#include <chrono>
#include <cstdint>
#include <mutex>
#include <thread>
#include "acl/acl.h"
#include "mmpa/mmpa_api.h"
#include "runtime/rt_external_event.h"
#include "common/hixl_checker.h"
#include "common/hixl_log.h"

namespace hixl {
namespace {
constexpr int32_t kRoceEagain = 128101;
constexpr const char *kLibRaSo = "libra.so";
// RoCE/HCCS RA notify path is only used for 910B / 910_93-class notify record layout (see HCCL GetNotifySize).
constexpr uint32_t kNotifyRecordByteLengthRa = 4U;

using RdmaHandle = void *;
using RaRdevGetHandleFn = int (*)(unsigned int phy_id, RdmaHandle *out_handle);
using RaGetNotifyBaseAddrFn = int (*)(RdmaHandle handle, unsigned long long *va, unsigned long long *size);

struct LibRaLoader {
  void *handle = nullptr;
  RaRdevGetHandleFn ra_rdev_get_handle = nullptr;
  RaGetNotifyBaseAddrFn ra_get_notify_base_addr = nullptr;
  std::mutex mu;

  ~LibRaLoader() {
    std::lock_guard<std::mutex> lock(mu);
    if (handle != nullptr) {
      HIXL_LOGI("[HccpProxy] LibRaLoader destruct, mmDlclose %s", kLibRaSo);
      (void)mmDlclose(handle);
      handle = nullptr;
      ra_rdev_get_handle = nullptr;
      ra_get_notify_base_addr = nullptr;
    }
  }
};

LibRaLoader &LibRa() {
  static LibRaLoader inst;
  return inst;
}

Status EnsureLibRaLoaded() {
  LibRaLoader &lr = LibRa();
  std::lock_guard<std::mutex> lock(lr.mu);
  if (lr.handle != nullptr) {
    return SUCCESS;
  }
  const int32_t dl_mode = static_cast<int32_t>(static_cast<uint32_t>(MMPA_RTLD_NOW) |
                                                 static_cast<uint32_t>(MMPA_RTLD_GLOBAL));
  void *h = mmDlopen(kLibRaSo, dl_mode);
  if (h == nullptr) {
    HIXL_LOGE(FAILED, "[HccpProxy] mmDlopen %s failed: %s", kLibRaSo, mmDlerror());
    return FAILED;
  }
  auto *get_handle = reinterpret_cast<RaRdevGetHandleFn>(mmDlsym(h, "RaRdevGetHandle"));
  auto *get_ba = reinterpret_cast<RaGetNotifyBaseAddrFn>(mmDlsym(h, "RaGetNotifyBaseAddr"));
  if (get_handle == nullptr || get_ba == nullptr) {
    HIXL_LOGE(FAILED, "[HccpProxy] mmDlsym RaRdevGetHandle/RaGetNotifyBaseAddr failed: %s", mmDlerror());
    (void)mmDlclose(h);
    return FAILED;
  }
  lr.handle = h;
  lr.ra_rdev_get_handle = get_handle;
  lr.ra_get_notify_base_addr = get_ba;
  return SUCCESS;
}

Status RaGetNotifyBaseAddrWithRetry(RaGetNotifyBaseAddrFn fn, RdmaHandle rdma_handle,
                                    unsigned long long *va, unsigned long long *total_size) {
  const auto t_start = std::chrono::steady_clock::now();
  const auto deadline = t_start + std::chrono::seconds(30);
  while (true) {
    const int ret = fn(rdma_handle, va, total_size);
    const auto t_now = std::chrono::steady_clock::now();
    const int64_t elapsed_us =
        std::chrono::duration_cast<std::chrono::microseconds>(t_now - t_start).count();
    if (ret == 0) {
      HIXL_LOGI("[HccpProxy] RaGetNotifyBaseAddrWithRetry ok, ret=%d, elapsed_us=%lld", ret,
                static_cast<long long>(elapsed_us));
      return SUCCESS;
    }
    if (ret != kRoceEagain) {
      HIXL_LOGE(FAILED, "[HccpProxy] RaGetNotifyBaseAddr failed, ret=%d, elapsed_us=%lld", ret,
                static_cast<long long>(elapsed_us));
      return FAILED;
    }
    if (t_now >= deadline) {
      HIXL_LOGE(FAILED, "[HccpProxy] RaGetNotifyBaseAddr timed out, elapsed_us=%lld",
                static_cast<long long>(elapsed_us));
      return FAILED;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

Status ResolveRaNotifyPhyId(int32_t device_id, unsigned int &phy_id) {
  if (device_id < 0) {
    HIXL_LOGE(PARAM_INVALID, "[HccpProxy] Ra path requires valid device_id, got %d", device_id);
    return PARAM_INVALID;
  }
  int32_t phy_device_id = 0;
  HIXL_CHK_ACL_RET(aclrtGetPhyDevIdByLogicDevId(device_id, &phy_device_id),
                   "[HccpProxy] aclrtGetPhyDevIdByLogicDevId failed");
  if (phy_device_id < 0) {
    HIXL_LOGE(PARAM_INVALID, "[HccpProxy] invalid phy_device_id=%d", phy_device_id);
    return PARAM_INVALID;
  }
  phy_id = static_cast<unsigned int>(phy_device_id);
  return SUCCESS;
}

void LibRaCopyFnPtrs(RaRdevGetHandleFn *get_handle_fn, RaGetNotifyBaseAddrFn *get_ba_fn) {
  LibRaLoader &lr = LibRa();
  std::lock_guard<std::mutex> lock(lr.mu);
  *get_handle_fn = lr.ra_rdev_get_handle;
  *get_ba_fn = lr.ra_get_notify_base_addr;
}

Status RaOpenRdev(unsigned int phy_id, RaRdevGetHandleFn get_handle_fn, RdmaHandle *rdma_handle) {
  const int gh_ret = get_handle_fn(phy_id, rdma_handle);
  if (gh_ret != 0 || *rdma_handle == nullptr) {
    HIXL_LOGE(FAILED, "[HccpProxy] RaRdevGetHandle failed, ret=%d phy_id=%u", gh_ret, phy_id);
    return FAILED;
  }
  return SUCCESS;
}

Status CombineNotifyDeviceVa(unsigned int phy_id, unsigned long long base_va, aclrtNotify notify,
                             uint64_t &notify_addr) {
  uint64_t offset = 0ULL;
  const rtError_t rt_ret = rtNotifyGetAddrOffset(reinterpret_cast<rtNotify_t>(notify), &offset);
  if (rt_ret != RT_ERROR_NONE) {
    REPORT_INNER_ERR_MSG("E19999", "Call rtNotifyGetAddrOffset fail, ret: 0x%X",
                         static_cast<uint32_t>(rt_ret));
    HIXL_LOGE(FAILED, "[HccpProxy] rtNotifyGetAddrOffset failed, ret: 0x%X",
              static_cast<uint32_t>(rt_ret));
    return FAILED;
  }
  if (base_va > (UINT64_MAX - offset)) {
    HIXL_LOGE(FAILED, "[HccpProxy] notify VA overflow base=0x%llx offset=0x%llx",
              static_cast<unsigned long long>(base_va), static_cast<unsigned long long>(offset));
    return FAILED;
  }
  notify_addr = static_cast<uint64_t>(base_va) + offset;
  HIXL_LOGI("[HccpProxy] Ra notify: phy_id=%u base=0x%llx offset=0x%llx va=0x%llx len=%u", phy_id,
            static_cast<unsigned long long>(base_va), static_cast<unsigned long long>(offset),
            static_cast<unsigned long long>(notify_addr), kNotifyRecordByteLengthRa);
  return SUCCESS;
}
}  // namespace

Status HccpProxy::RaGetNotifyAddrLen(int32_t device_id, aclrtNotify notify, uint64_t &notify_addr,
                                     uint32_t &notify_len) {
  unsigned int phy_id = 0U;
  HIXL_CHK_STATUS_RET(ResolveRaNotifyPhyId(device_id, phy_id), "[HccpProxy] ResolveRaNotifyPhyId failed");
  HIXL_CHK_STATUS_RET(EnsureLibRaLoaded(), "[HccpProxy] EnsureLibRaLoaded failed");
  RaRdevGetHandleFn get_handle_fn = nullptr;
  RaGetNotifyBaseAddrFn get_ba_fn = nullptr;
  LibRaCopyFnPtrs(&get_handle_fn, &get_ba_fn);
  RdmaHandle rdma_handle = nullptr;
  HIXL_CHK_STATUS_RET(RaOpenRdev(phy_id, get_handle_fn, &rdma_handle), "[HccpProxy] RaOpenRdev failed");
  unsigned long long base_va = 0ULL;
  unsigned long long total_size = 0ULL;
  HIXL_CHK_STATUS_RET(RaGetNotifyBaseAddrWithRetry(get_ba_fn, rdma_handle, &base_va, &total_size),
                      "[HccpProxy] RaGetNotifyBaseAddrWithRetry failed");
  (void)total_size;
  HIXL_CHK_STATUS_RET(CombineNotifyDeviceVa(phy_id, base_va, notify, notify_addr),
                      "[HccpProxy] CombineNotifyDeviceVa failed");
  notify_len = kNotifyRecordByteLengthRa;
  return SUCCESS;
}

}  // namespace hixl
