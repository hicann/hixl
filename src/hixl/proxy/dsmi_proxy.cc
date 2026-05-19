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

struct dsmi_board_info_stru {
  unsigned int board_id;
  unsigned int pcb_id;
  unsigned int bom_id;
  unsigned int slot_id;
};

using DsmiGetBoardInfoFn = int (*)(int device_id, struct dsmi_board_info_stru *pboard_info);

struct LibDrvdsmiHostLoader {
  void *handle = nullptr;
  DsmiGetBoardInfoFn dsmi_get_board_info = nullptr;
  std::mutex mu;

  ~LibDrvdsmiHostLoader() {
    std::lock_guard<std::mutex> lock(mu);
    if (handle != nullptr) {
      HIXL_LOGI("[DsmiProxy] LibDrvdsmiHostLoader destruct, mmDlclose %s", kLibDrvdsmiHostSo);
      (void)mmDlclose(handle);
      handle = nullptr;
      dsmi_get_board_info = nullptr;
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
  return SUCCESS;
}

}  // namespace

Status DsmiProxy::GetDevSlotId(int32_t device_id, uint32_t &slot_id) {
  HIXL_CHK_STATUS_RET(EnsureLibDrvdsmiHostLoaded(), "[DsmiProxy] EnsureLibDrvdsmiHostLoaded failed");

  LibDrvdsmiHostLoader &ldr = LibDrvdsmiHost();
  std::lock_guard<std::mutex> lock(ldr.mu);

  struct dsmi_board_info_stru board_info = {};
  const int ret = ldr.dsmi_get_board_info(device_id, &board_info);
  if (ret != 0) {
    HIXL_LOGE(FAILED, "[DsmiProxy] dsmi_get_board_info failed, ret=%d, device_id=%d", ret, device_id);
    return FAILED;
  }

  slot_id = board_info.slot_id;
  HIXL_LOGI("[DsmiProxy] GetDevSlotId success, device_id=%d, slot_id=%u", device_id, slot_id);
  return SUCCESS;
}

}  // namespace hixl