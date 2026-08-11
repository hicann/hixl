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
#include <dlfcn.h>
#include "common/hixl_checker.h"
#include "common/hixl_log.h"

namespace hixl {
namespace {

constexpr const char *kLibDrvdsmiHostSo = "libdrvdsmi_host.so";

constexpr uint32_t kDsmiMainCmdChipInf = 12U;
constexpr uint32_t kDsmiChipInfSubCmdSpodInfo = 1U;

// DSMI_MAIN_CMD_UB and DSMI_UB_INFO_SUB_CMD_URMA_DEV_NAME are not available in
// CANN 9.1.0 headers. Values confirmed by probing on real hardware.
constexpr uint32_t kDsmiMainCmdUb = 62U;
constexpr uint32_t kDsmiUbInfoSubCmdUrmaDevName = 3U;

// Max length for UB device name (e.g. "udmac1d1e2" is 10 chars + NUL)
constexpr size_t kUbDevNameMaxLen = 32U;

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
      HIXL_LOGI("[DsmiProxy] LibDrvdsmiHostLoader destruct, dlclose %s", kLibDrvdsmiHostSo);
      (void)dlclose(handle);
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

  const int32_t dl_mode = RTLD_NOW;
  void *dsmi_handle = dlopen(kLibDrvdsmiHostSo, dl_mode);
  if (dsmi_handle == nullptr) {
    const char *err = dlerror();
    HIXL_LOGE(FAILED, "[DsmiProxy] dlopen %s failed: %s", kLibDrvdsmiHostSo, err != nullptr ? err : "unknown error");
    return FAILED;
  }

  auto *get_board_info_fn = reinterpret_cast<DsmiGetBoardInfoFn>(dlsym(dsmi_handle, "dsmi_get_board_info"));
  if (get_board_info_fn == nullptr) {
    const char *err = dlerror();
    HIXL_LOGE(FAILED, "[DsmiProxy] dlsym dsmi_get_board_info failed: %s", err != nullptr ? err : "unknown error");
    (void)dlclose(dsmi_handle);
    return FAILED;
  }

  ldr.handle = dsmi_handle;
  ldr.dsmi_get_board_info = get_board_info_fn;
  ldr.dsmi_get_device_info = reinterpret_cast<DsmiGetDeviceInfoFn>(dlsym(dsmi_handle, "dsmi_get_device_info"));
  if (ldr.dsmi_get_device_info == nullptr) {
    const char *err = dlerror();
    HIXL_LOGW("[DsmiProxy] dlsym dsmi_get_device_info failed, InterconType unavailable: %s",
              err != nullptr ? err : "unknown error");
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
  HIXL_CHK_BOOL_RET_STATUS(ret == 0, FAILED, "[DsmiProxy] Call api:dsmi_get_board_info failed, ret=%d, device_id=%d",
                           ret, device_id);

  slot_id = board_info.slot_id;
  HIXL_LOGI("[DsmiProxy] GetDevSlotId success, device_id=%d, slot_id=%u", device_id, slot_id);
  return SUCCESS;
}

Status DsmiProxy::GetInterconType(int32_t device_id, uint32_t &intercon_type) {
  HIXL_CHK_STATUS_RET(EnsureLibDrvdsmiHostLoaded(), "[DsmiProxy] EnsureLibDrvdsmiHostLoaded failed");

  LibDrvdsmiHostLoader &ldr = LibDrvdsmiHost();
  std::lock_guard<std::mutex> lock(ldr.mu);
  HIXL_CHK_BOOL_RET_STATUS(ldr.dsmi_get_device_info != nullptr, FAILED,
                           "[DsmiProxy] dsmi_get_device_info symbol not available, cannot query InterconType");

  DsmiSpodInfo spod_info{};
  uint32_t buf_size = static_cast<uint32_t>(sizeof(spod_info));
  const int ret = ldr.dsmi_get_device_info(static_cast<uint32_t>(device_id), kDsmiMainCmdChipInf,
                                           kDsmiChipInfSubCmdSpodInfo, &spod_info, &buf_size);
  HIXL_CHK_BOOL_RET_STATUS(ret == 0, FAILED, "[DsmiProxy] Call api:dsmi_get_device_info failed, ret=%d, device_id=%d",
                           ret, device_id);
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

Status DsmiProxy::GetUbDevName(int32_t device_id, std::string &ub_dev_name) {
  HIXL_CHK_STATUS_RET(EnsureLibDrvdsmiHostLoaded(), "[DsmiProxy] EnsureLibDrvdsmiHostLoaded failed");

  LibDrvdsmiHostLoader &ldr = LibDrvdsmiHost();
  std::lock_guard<std::mutex> lock(ldr.mu);
  HIXL_CHK_BOOL_RET_STATUS(ldr.dsmi_get_device_info != nullptr, FAILED,
                           "[DsmiProxy] dsmi_get_device_info symbol not available, cannot query UB dev name");

  ub_dev_name.resize(kUbDevNameMaxLen);
  uint32_t buf_size = static_cast<uint32_t>(kUbDevNameMaxLen);
  int ret = ldr.dsmi_get_device_info(static_cast<uint32_t>(device_id), kDsmiMainCmdUb, kDsmiUbInfoSubCmdUrmaDevName,
                                     &ub_dev_name[0], &buf_size);
  HIXL_LOGI("[DsmiProxy] dsmi_get_device_info(UB, URMA_DEV_NAME) ret=%d, device_id=%d, buf_size=%u", ret, device_id,
            buf_size);
  HIXL_CHK_BOOL_RET_STATUS(ret == 0, FAILED,
                           "[DsmiProxy] Call api:dsmi_get_device_info(UB, URMA_DEV_NAME) failed, ret=%d, device_id=%d",
                           ret, device_id);

  // buf_size includes the NUL terminator from the API; resize to actual string length
  ub_dev_name.resize(strlen(ub_dev_name.c_str()));
  HIXL_LOGI("[DsmiProxy] GetUbDevName success, device_id=%d, ub_dev_name=%s", device_id, ub_dev_name.c_str());
  return SUCCESS;
}

}  // namespace hixl
