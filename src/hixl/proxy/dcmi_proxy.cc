/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

/**
 * @file dcmi_proxy.cc
 * @brief DCMI 接口代理模块实现
 */

#include "dcmi_proxy.h"
#include <dlfcn.h>
#include <unistd.h>
#include "common/hixl_log.h"
#include "hixl/hixl_types.h"

namespace hixl {

namespace {

// DCMI 接口函数指针类型
using DcmiInitFunc = int32_t(*)();
using DcmiGetUrmaDeviceCntFunc = int32_t(*)(int32_t npu_id, uint32_t* dev_cnt);
using DcmiGetEidListFunc = int32_t(*)(int32_t npu_id, int32_t urma_dev_index,
                                    DcmiUrmaEidInfo* eid_list, int32_t* eid_cnt);
using DcmiGetMainboardIdFunc = int32_t(*)(int32_t npu_id, uint32_t* mainboard_id);
using DcmiGetLogicIdFromPhyIdFunc = int32_t(*)(uint32_t phy_id, uint32_t* logic_id);
using DcmiGetDeviceInfoFunc = int32_t(*)(int32_t npu_id, int32_t main_cmd,
                                      uint32_t sub_cmd, void* buf, uint32_t* size);

// DCMI 接口函数指针
DcmiInitFunc g_dcmi_init = nullptr;
DcmiGetUrmaDeviceCntFunc g_dcmi_get_urma_device_cnt = nullptr;
DcmiGetEidListFunc g_dcmi_get_eid_list = nullptr;
DcmiGetMainboardIdFunc g_dcmi_get_mainboard_id = nullptr;
DcmiGetLogicIdFromPhyIdFunc g_dcmi_get_logicid_from_phyid = nullptr;
DcmiGetDeviceInfoFunc g_dcmi_get_device_info = nullptr;

void *g_dcmi_handle = nullptr;
volatile bool g_dcmi_loaded = false;
volatile int32_t g_dcmi_init_status = -1;

int32_t TryLoadDcmiSymbols() {
  g_dcmi_handle = dlopen("libdcmi.so", RTLD_LAZY);
  if (g_dcmi_handle == nullptr) {
    HIXL_LOGE(FAILED, "Failed to dlopen libdcmi.so: %s", dlerror());
    return -1;
  }

  g_dcmi_init = reinterpret_cast<DcmiInitFunc>(dlsym(g_dcmi_handle, "dcmiv2_init"));
  g_dcmi_get_urma_device_cnt =
      reinterpret_cast<DcmiGetUrmaDeviceCntFunc>(dlsym(g_dcmi_handle, "dcmiv2_get_urma_device_cnt"));
  g_dcmi_get_eid_list =
      reinterpret_cast<DcmiGetEidListFunc>(dlsym(g_dcmi_handle, "dcmiv2_get_eid_list_by_urma_dev_index"));
  g_dcmi_get_mainboard_id =
      reinterpret_cast<DcmiGetMainboardIdFunc>(dlsym(g_dcmi_handle, "dcmiv2_get_mainboard_id"));
  g_dcmi_get_logicid_from_phyid =
      reinterpret_cast<DcmiGetLogicIdFromPhyIdFunc>(dlsym(g_dcmi_handle, "dcmiv2_get_dev_id_by_chip_phy_id"));
  if (g_dcmi_get_logicid_from_phyid == nullptr) {
    g_dcmi_get_logicid_from_phyid =
        reinterpret_cast<DcmiGetLogicIdFromPhyIdFunc>(dlsym(g_dcmi_handle, "dcmiv2_get_dev_id_from_chip_phyid"));
  }

  g_dcmi_get_device_info = reinterpret_cast<DcmiGetDeviceInfoFunc>(dlsym(g_dcmi_handle, "dcmiv2_get_device_info"));
  if (g_dcmi_init == nullptr || g_dcmi_get_urma_device_cnt == nullptr || g_dcmi_get_eid_list == nullptr ||
      g_dcmi_get_mainboard_id == nullptr || g_dcmi_get_logicid_from_phyid == nullptr ||
      g_dcmi_get_device_info == nullptr) {
    HIXL_LOGE(FAILED, "Failed to load DCMI function symbols");
    dlclose(g_dcmi_handle);
    g_dcmi_handle = nullptr;
    return -1;
  }

  return 0;
}

int32_t InitDcmiWithRetry() {
  constexpr int32_t kMaxWaitTime = 10;

  for (int32_t i = 0; i < kMaxWaitTime; ++i) {
    g_dcmi_init_status = g_dcmi_init();
    if (g_dcmi_init_status == 0) {
      break;
    }
    sleep(1);
  }

  if (g_dcmi_init_status != 0) {
    HIXL_LOGE(FAILED, "DCMI init failed after %d retries", kMaxWaitTime);
    dlclose(g_dcmi_handle);
    g_dcmi_handle = nullptr;
    g_dcmi_init_status = -1;
    return g_dcmi_init_status;
  }

  return 0;
}

}  // anonymous namespace

int32_t DcmiProxy::LoadDcmi() {
  if (g_dcmi_loaded) {
    return g_dcmi_init_status;
  }

  if (TryLoadDcmiSymbols() != 0) {
    g_dcmi_init_status = -1;
    g_dcmi_loaded = true;
    return g_dcmi_init_status;
  }

  if (InitDcmiWithRetry() != 0) {
    g_dcmi_init_status = -1;
    g_dcmi_loaded = true;
    return g_dcmi_init_status;
  }

  g_dcmi_loaded = true;
  return g_dcmi_init_status;
}

void DcmiProxy::UnloadDcmi() {
  if (!g_dcmi_loaded) {
    return;
  }
  if (g_dcmi_handle != nullptr) {
    dlclose(g_dcmi_handle);
    g_dcmi_handle = nullptr;
  }
  g_dcmi_init = nullptr;
  g_dcmi_get_urma_device_cnt = nullptr;
  g_dcmi_get_eid_list = nullptr;
  g_dcmi_get_mainboard_id = nullptr;
  g_dcmi_get_logicid_from_phyid = nullptr;
  g_dcmi_get_device_info = nullptr;
  g_dcmi_loaded = false;
  g_dcmi_init_status = -1;
}

int32_t DcmiProxy::GetLogicIdFromPhyId(uint32_t phy_id, uint32_t *logic_id) {
  if (LoadDcmi() != 0) {
    return -1;
  }
  return g_dcmi_get_logicid_from_phyid(phy_id, logic_id);
}

int32_t DcmiProxy::GetUrmaDeviceCnt(uint32_t logic_id, uint32_t *dev_cnt) {
  if (LoadDcmi() != 0) {
    return -1;
  }
  return g_dcmi_get_urma_device_cnt(static_cast<int32_t>(logic_id), dev_cnt);
}

int32_t DcmiProxy::GetEidList(uint32_t logic_id, int32_t urma_dev_index,
                               DcmiUrmaEidInfo *eid_list, int32_t *eid_cnt) {
  if (LoadDcmi() != 0) {
    return -1;
  }
  return g_dcmi_get_eid_list(static_cast<int32_t>(logic_id), urma_dev_index, eid_list, eid_cnt);
}

int32_t DcmiProxy::GetMainboardId(uint32_t logic_id, uint32_t *mainboard_id) {
  if (LoadDcmi() != 0) {
    return -1;
  }
  return g_dcmi_get_mainboard_id(static_cast<int32_t>(logic_id), mainboard_id);
}

int32_t DcmiProxy::GetDeviceInfo(uint32_t logic_id, int32_t main_cmd,
                                 uint32_t sub_cmd, void *buf, uint32_t *size) {
  if (LoadDcmi() != 0) {
    return -1;
  }
  return g_dcmi_get_device_info(static_cast<int32_t>(logic_id), main_cmd, sub_cmd, buf, size);
}

}  // namespace hixl
