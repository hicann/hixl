/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "dsmi_proxy.h"
#include <mutex>
#include "mmpa/mmpa_api.h"
#include "common/hixl_checker.h"
#include "common/hixl_log.h"

namespace hixl {
namespace {

constexpr const char *kLibDrvdsmiHostSo = "libdrvdsmi_host.so";

constexpr uint32_t kDsmiMainCmdChipInf = 12U;
constexpr uint32_t kDsmiChipInfSubCmdSpodInfo = 1U;

using DsmiGetBoardInfoFn = int (*)(int device_id, struct DsmiBoardInfoStru *pboard_info);
using DsmiGetDeviceInfoFn = int (*)(uint32_t device_id, uint32_t main_cmd, uint32_t sub_cmd, void *buf,
                                    uint32_t *buf_size);

struct LibDrvdsmiHostLoader {
  void *handle = nullptr;
  DsmiGetBoardInfoFn dsmi_get_board_info = nullptr;
  DsmiGetDeviceInfoFn dsmi_get_device_info = nullptr;
  std::mutex mu;

  ~LibDrvdsmiHostLoader() {
    std::lock_guard<std::mutex> lock(mu);
    if (handle != nullptr) {
      HIXL_LOGI("[DsmiProxy] LibDrvdsmiHostLoader destruct, mmDlclose %s", kLibDrvdsmiHostSo);
      (void)mmDlclose(handle);
      handle = nullptr;
      dsmi_get_board_info = nullptr;
      dsmi_get_device_info = nullptr;
    }
  }
};

LibDrvdsmiHostLoader &LibDrvdsmiHost() {
  static LibDrvdsmiHostLoader inst;
  return inst;
}

Status EnsureLibDrvdsmiHostLoaded() {
  LibDrvdsmiHostLoader &ldr = LibDrvdsmiHost();
  std::lock_guard<std::mutex> lock(ldr.mu);
  if (ldr.handle != nullptr) {
    return SUCCESS;
  }

  const int32_t dl_mode = MMPA_RTLD_NOW;
  void *dsmi_handle = mmDlopen(kLibDrvdsmiHostSo, dl_mode);
  if (dsmi_handle == nullptr) {
    HIXL_LOGE(FAILED, "[DsmiProxy] mmDlopen %s failed: %s", kLibDrvdsmiHostSo, mmDlerror());
    return FAILED;
  }

  auto *get_board_info_fn = reinterpret_cast<DsmiGetBoardInfoFn>(mmDlsym(dsmi_handle, "dsmi_get_board_info"));
  if (get_board_info_fn == nullptr) {
    HIXL_LOGE(FAILED, "[DsmiProxy] mmDlsym dsmi_get_board_info failed: %s", mmDlerror());
    (void)mmDlclose(dsmi_handle);
    return FAILED;
  }

  ldr.handle = dsmi_handle;
  ldr.dsmi_get_board_info = get_board_info_fn;
  ldr.dsmi_get_device_info = reinterpret_cast<DsmiGetDeviceInfoFn>(mmDlsym(dsmi_handle, "dsmi_get_device_info"));
  if (ldr.dsmi_get_device_info == nullptr) {
    HIXL_LOGW("[DsmiProxy] mmDlsym dsmi_get_device_info failed, InterconType unavailable: %s", mmDlerror());
  }
  return SUCCESS;
}

}  // namespace

Status DsmiProxy::GetDevSlotId(int32_t device_id, uint32_t &slot_id) {
  HIXL_CHK_STATUS_RET(EnsureLibDrvdsmiHostLoaded(), "[DsmiProxy] EnsureLibDrvdsmiHostLoaded failed");

  LibDrvdsmiHostLoader &ldr = LibDrvdsmiHost();
  std::lock_guard<std::mutex> lock(ldr.mu);

  struct DsmiBoardInfoStru board_info = {};
  const int ret = ldr.dsmi_get_board_info(device_id, &board_info);
  if (ret != 0) {
    HIXL_LOGE(FAILED, "[DsmiProxy] dsmi_get_board_info failed, ret=%d, device_id=%d", ret, device_id);
    return FAILED;
  }

  slot_id = board_info.slot_id;
  HIXL_LOGI("[DsmiProxy] GetDevSlotId success, device_id=%d, slot_id=%u", device_id, slot_id);
  return SUCCESS;
}

Status DsmiProxy::GetInterconType(int32_t device_id, uint32_t &intercon_type) {
  HIXL_CHK_STATUS_RET(EnsureLibDrvdsmiHostLoaded(), "[DsmiProxy] EnsureLibDrvdsmiHostLoaded failed");

  LibDrvdsmiHostLoader &ldr = LibDrvdsmiHost();
  std::lock_guard<std::mutex> lock(ldr.mu);
  if (ldr.dsmi_get_device_info == nullptr) {
    HIXL_LOGW("[DsmiProxy] dsmi_get_device_info symbol not available, cannot query InterconType");
    intercon_type = 0U;
    return FAILED;
  }

  DsmiSpodInfo spod_info{};
  uint32_t buf_size = static_cast<uint32_t>(sizeof(spod_info));
  const int ret = ldr.dsmi_get_device_info(static_cast<uint32_t>(device_id), kDsmiMainCmdChipInf,
                                           kDsmiChipInfSubCmdSpodInfo, &spod_info, &buf_size);
  if (ret != 0) {
    HIXL_LOGE(FAILED, "[DsmiProxy] dsmi_get_device_info failed, ret=%d, device_id=%d", ret, device_id);
    intercon_type = 0U;
    return FAILED;
  }
  intercon_type = spod_info.super_pod_intercon_type;
  HIXL_LOGI("[DsmiProxy] GetInterconType success, device_id=%d, intercon_type=%u", device_id, intercon_type);
  return SUCCESS;
}

bool DsmiProxy::IsInterconTypeSupported() {
  if (EnsureLibDrvdsmiHostLoaded() != SUCCESS) {
    return false;
  }
  LibDrvdsmiHostLoader &ldr = LibDrvdsmiHost();
  std::lock_guard<std::mutex> lock(ldr.mu);
  return ldr.dsmi_get_device_info != nullptr;
}

}  // namespace hixl
