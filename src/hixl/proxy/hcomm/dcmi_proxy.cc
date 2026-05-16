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

#include "proxy/hcomm/dcmi_proxy.h"
#include <dlfcn.h>
#include <unistd.h>
#include "common/hixl_log.h"
#include "hixl/hixl_types.h"

namespace hixl {

namespace {

// DCMI 接口函数指针
static dcmi_init_func g_dcmi_init = nullptr;
static dcmi_get_urma_device_cnt_func g_dcmi_get_urma_device_cnt = nullptr;
static dcmi_get_eid_list_func g_dcmi_get_eid_list = nullptr;
static dcmi_get_mainboard_id_func g_dcmi_get_mainboard_id = nullptr;
static dcmi_get_logicid_from_phyid_func g_dcmi_get_logicid_from_phyid = nullptr;
static dcmi_get_device_info_func g_dcmi_get_device_info = nullptr;

void *g_dcmi_handle = nullptr;
volatile bool g_dcmi_loaded = false;
volatile int g_dcmi_init_status = -1;

int TryLoadDcmiSymbols() {
  g_dcmi_handle = dlopen("libdcmi.so", RTLD_LAZY);
  if (g_dcmi_handle == nullptr) {
    HIXL_LOGE(FAILED, "Failed to dlopen libdcmi.so: %s", dlerror());
    return -1;
  }

  g_dcmi_init = reinterpret_cast<dcmi_init_func>(dlsym(g_dcmi_handle, "dcmiv2_init"));
  g_dcmi_get_urma_device_cnt =
      reinterpret_cast<dcmi_get_urma_device_cnt_func>(dlsym(g_dcmi_handle, "dcmiv2_get_urma_device_cnt"));
  g_dcmi_get_eid_list =
      reinterpret_cast<dcmi_get_eid_list_func>(dlsym(g_dcmi_handle, "dcmiv2_get_eid_list_by_urma_dev_index"));
  g_dcmi_get_mainboard_id =
      reinterpret_cast<dcmi_get_mainboard_id_func>(dlsym(g_dcmi_handle, "dcmiv2_get_mainboard_id"));
  g_dcmi_get_logicid_from_phyid =
      reinterpret_cast<dcmi_get_logicid_from_phyid_func>(dlsym(g_dcmi_handle, "dcmiv2_get_dev_id_by_chip_phy_id"));

  if (g_dcmi_get_logicid_from_phyid == nullptr) {
    g_dcmi_get_logicid_from_phyid =
        reinterpret_cast<dcmi_get_logicid_from_phyid_func>(dlsym(g_dcmi_handle, "dcmiv2_get_dev_id_from_chip_phyid"));
  }

  g_dcmi_get_device_info = reinterpret_cast<dcmi_get_device_info_func>(dlsym(g_dcmi_handle, "dcmiv2_get_device_info"));

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

int InitDcmiWithRetry() {
  const int max_wait_time = 10;

  for (int i = 0; i < max_wait_time; ++i) {
    g_dcmi_init_status = g_dcmi_init();
    if (g_dcmi_init_status == 0) {
      break;
    }
    sleep(1);
  }

  if (g_dcmi_init_status != 0) {
    HIXL_LOGE(FAILED, "DCMI init failed after %d retries", max_wait_time);
    dlclose(g_dcmi_handle);
    g_dcmi_handle = nullptr;
    g_dcmi_init_status = -1;
    return g_dcmi_init_status;
  }

  return 0;
}

}  // anonymous namespace

int LoadDcmi() {
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

int32_t DcmiGetLogicIdFromPhyId(unsigned int phy_id, unsigned int *logic_id) {
  if (LoadDcmi() != 0) {
    return -1;
  }
  return g_dcmi_get_logicid_from_phyid(phy_id, logic_id);
}

int32_t DcmiGetUrmaDeviceCnt(unsigned int logic_id, unsigned int *dev_cnt) {
  if (LoadDcmi() != 0) {
    return -1;
  }
  return g_dcmi_get_urma_device_cnt(static_cast<int>(logic_id), dev_cnt);
}

int32_t DcmiGetEidList(unsigned int logic_id, int urma_dev_index,
                       dcmi_urma_eid_info_t *eid_list, int *eid_cnt) {
  if (LoadDcmi() != 0) {
    return -1;
  }
  return g_dcmi_get_eid_list(static_cast<int>(logic_id), urma_dev_index, eid_list, eid_cnt);
}

int32_t DcmiGetMainboardId(unsigned int logic_id, unsigned int *mainboard_id) {
  if (LoadDcmi() != 0) {
    return -1;
  }
  return g_dcmi_get_mainboard_id(static_cast<int>(logic_id), mainboard_id);
}

int32_t DcmiGetDeviceInfo(unsigned int logic_id, int main_cmd,
                          unsigned int sub_cmd, void *buf, unsigned int *size) {
  if (LoadDcmi() != 0) {
    return -1;
  }
  return g_dcmi_get_device_info(static_cast<int>(logic_id), main_cmd, sub_cmd, buf, size);
}

}  // namespace hixl
