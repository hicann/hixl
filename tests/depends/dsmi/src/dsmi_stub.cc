/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "dsmi_stub.h"

#include <cstdint>
#include <cstring>

struct dsmi_board_info_stru {
  uint32_t board_id;
  uint32_t pcb_id;
  uint32_t bom_id;
  uint32_t slot_id;
};

static const uint32_t kDsmiMainCmdChipInf = 12U;
static const uint32_t kDsmiChipInfSubCmdSpodInfo = 1U;

static uint32_t g_slot_id = 0U;
static int g_board_info_ret = 0;

static uint32_t g_intercon_type = 4U;
static uint32_t g_super_pod_id = 0U;
static int g_device_info_ret = 0;

#ifdef __cplusplus
extern "C" {
#endif

int dsmi_get_board_info(int device_id, struct dsmi_board_info_stru *pboard_info) {
  (void)device_id;
  if (pboard_info == nullptr) {
    return -1;
  }
  if (g_board_info_ret != 0) {
    return g_board_info_ret;
  }
  pboard_info->board_id = 0;
  pboard_info->pcb_id = 0;
  pboard_info->bom_id = 0;
  pboard_info->slot_id = g_slot_id;
  return 0;
}

int dsmi_get_device_info(uint32_t device_id, uint32_t main_cmd, uint32_t sub_cmd, void *buf, uint32_t *buf_size) {
  (void)device_id;
  if (buf == nullptr || buf_size == nullptr) {
    return -1;
  }
  if (g_device_info_ret != 0) {
    return g_device_info_ret;
  }
  if (main_cmd != kDsmiMainCmdChipInf || sub_cmd != kDsmiChipInfSubCmdSpodInfo) {
    return -1;
  }
  static const uint32_t kSpodInfoFields = 12U;
  if (*buf_size < kSpodInfoFields * sizeof(uint32_t)) {
    return -1;
  }
  auto *fields = static_cast<uint32_t *>(buf);
  for (uint32_t i = 0; i < kSpodInfoFields; ++i) {
    fields[i] = 0;
  }
  fields[2] = g_super_pod_id;
  fields[6] = g_intercon_type;
  *buf_size = kSpodInfoFields * sizeof(uint32_t);
  return 0;
}

void DsmiStubSetInterconType(uint32_t type) {
  g_intercon_type = type;
}

void DsmiStubSetSuperPodId(uint32_t id) {
  g_super_pod_id = id;
}

void DsmiStubSetSlotId(uint32_t slot_id, int ret) {
  g_slot_id = slot_id;
  g_board_info_ret = ret;
}

void DsmiStubSetDeviceInfoRet(int ret) {
  g_device_info_ret = ret;
}

#ifdef __cplusplus
}
#endif
