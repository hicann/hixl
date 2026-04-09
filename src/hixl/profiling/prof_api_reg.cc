/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <mutex>
#include <unordered_set>
#include <map>
#include "mmpa/mmpa_api.h"
#include "acl/acl_prof.h"
#include "prof_api_reg.h"
#include "common/hixl_checker.h"
#include "common/hixl_log.h"


namespace {
bool g_prof_run = false;
std::mutex g_prof_mutex;
std::unordered_set<uint32_t> g_device_list;
constexpr uint32_t kStartProfiling = 1U;
constexpr uint32_t kStopProfiling = 2U;

const std::map<hixl::HixlProfType, std::string> kProfTypeToNames = {
  {hixl::HixlProfType::HixlOpBatchRead,                       "hixlOpBatchRead"},
  {hixl::HixlProfType::HixlOpBatchWrite,                      "hixlOpBatchWrite"},
};

aclError RegisterProfType() {
  for (const auto &iter: kProfTypeToNames) {
    uint32_t type_id = static_cast<uint32_t>(iter.first);
    const auto ret = MsprofRegTypeInfo(MSPROF_REPORT_ACL_LEVEL, type_id, iter.second.c_str());
    if (ret != MSPROF_ERROR_NONE) {
      HIXL_LOGE(ACL_ERROR_PROFILING_FAILURE, "[Hixl Profiling] Profiling registered api type [%u] failed = %d", type_id, ret);
      return ACL_ERROR_PROFILING_FAILURE;
    }
  }
  return ACL_SUCCESS;
}

aclError AddDeviceList(const uint32_t *const device_id_list, const uint32_t device_nums) {
  if (device_id_list == nullptr) {
    HIXL_LOGE(ACL_ERROR_INVALID_PARAM,"[Hixl Profiling] Device_id_list is null, please check");
    return ACL_ERROR_INVALID_PARAM;
  }
  for (size_t idx = 0U; idx < device_nums; idx++) {
    if (g_device_list.count(*(device_id_list + idx)) == 0U) {
      (void)g_device_list.insert(*(device_id_list + idx));
      HIXL_LOGI("[Hixl Profiling] Device id %u is successfully added in hixl profiling", *(device_id_list + idx));
    }
  }
  return ACL_SUCCESS;
}

aclError RemoveDeviceList(const uint32_t *const device_id_list, const uint32_t device_nums) {
  if (device_id_list == nullptr) {
    HIXL_LOGE(ACL_ERROR_INVALID_PARAM, "[Hixl Profiling] Device_id_list is null, please check");
    return ACL_ERROR_INVALID_PARAM;
  }
  for (size_t idx = 0U; idx < device_nums; idx++) {
    const auto iter = g_device_list.find(*(device_id_list + idx));
    if (iter != g_device_list.end()) {
      (void)g_device_list.erase(iter);
      HIXL_LOGI("[Hixl Profiling] Device id %u is successfully deleted from acl profiling", *(device_id_list + idx));
    }
  }
  return ACL_SUCCESS;
}

aclError ProfInnerStart(const MsprofCommandHandle *const profiler_config) {
  HIXL_LOGI("[Hixl Profiling] Start to execute profInnerStart");
  if (!g_prof_run) {
    auto reg_ret = RegisterProfType();
    if (reg_ret == ACL_SUCCESS) {
      g_prof_run = true;
    } else {
      HIXL_LOGE(ACL_ERROR_PROFILING_FAILURE, "[Hixl Profiling] Register prof type failed.");
      return ACL_ERROR_PROFILING_FAILURE;
    }
  }
  auto add_ret = AddDeviceList(profiler_config->devIdList, profiler_config->devNums);
  if (add_ret != ACL_SUCCESS) {
    HIXL_LOGE(ACL_ERROR_INVALID_PARAM, "[Hixl Profiling] Add device list failed.");
    return ACL_ERROR_INVALID_PARAM;
  }
  HIXL_LOGI("[Hixl Profiling] Successfully execute ProfInnerStart");
  return ACL_SUCCESS;
}

aclError ProfInnerStop(const MsprofCommandHandle *const profiler_config) {
  HIXL_LOGI("[Hixl Profiling] Start to execute profInnerStop");
  auto remove_ret = RemoveDeviceList(profiler_config->devIdList, profiler_config->devNums);
  if (remove_ret != ACL_SUCCESS) {
    HIXL_LOGE(ACL_ERROR_INVALID_PARAM, "[Hixl Profiling] Remove device list failed.");
    return ACL_ERROR_INVALID_PARAM;
  }
  if (g_device_list.empty() && g_prof_run) {
    g_prof_run = false;
  }
  HIXL_LOGI("[Hixl Profiling] Successfully execute ProfInnerStop");
  return ACL_SUCCESS;
}

aclError ProcessProfData(void *const data, const uint32_t len) {
  HIXL_LOGI("[Hixl Profiling] Start to execute ProcessProfData");
  const std::lock_guard<std::mutex> lk(g_prof_mutex);
  if (data == nullptr) {
    HIXL_LOGE(ACL_ERROR_INVALID_PARAM, "[Hixl Profiling] Data is null, please check");
    return ACL_ERROR_INVALID_PARAM;
  }
  constexpr size_t command_len = sizeof(MsprofCommandHandle);
  if (len < command_len) {
    HIXL_LOGE(ACL_ERROR_INVALID_PARAM, "[Hixl Profiling] [Check][Len]len[%u] is invalid, it should not be smaller than %zu", len, command_len);
    return ACL_ERROR_INVALID_PARAM;
  }
  MsprofCommandHandle *const profiler_config = static_cast<MsprofCommandHandle *>(data);
  aclError ret = ACL_SUCCESS;
  const uint64_t prof_switch = profiler_config->profSwitch;
  const uint32_t type = profiler_config->type;
  if (((prof_switch & ACL_PROF_ACL_API) != 0U) && (type == kStartProfiling)) {
    ret = ProfInnerStart(profiler_config);
  }
  if (((prof_switch & ACL_PROF_ACL_API) != 0U) && (type == kStopProfiling)) {
    ret = ProfInnerStop(profiler_config);
  }
  return ret;
}

aclError HixlProfCtrlHandle(uint32_t data_type, void *data, uint32_t data_len) {
  if (data == nullptr) {
    HIXL_LOGE(ACL_ERROR_INVALID_PARAM, "[Hixl Profiling] Data is null, please check");
    return ACL_ERROR_INVALID_PARAM;
  }
  if (data_type == PROF_CTRL_SWITCH) {
    const aclError ret = ProcessProfData(data, data_len);
    if (ret != ACL_SUCCESS) {
      HIXL_LOGE(ret, "[Hixl Profiling] [Process][ProfSwitch]failed to call ProcessProfData, result is %u", ret);
      return ret;
    }
    return ACL_SUCCESS;
  }
  HIXL_LOGI("[Hixl Profiling] get unsupported data_type %u while processing profiling data", data_type);
  return ACL_SUCCESS;
}

class HixlRegProfCallback {
public:
  HixlRegProfCallback() {
    const auto prof_ret = MsprofRegisterCallback(HIXL_MODULE_ID, &HixlProfCtrlHandle);
    if (prof_ret != 0) {
      HIXL_LOGE(ACL_ERROR_PROFILING_FAILURE, "[Hixl Profiling] Can not register Callback, prof result = %d", prof_ret);
    }
  }
  ~HixlRegProfCallback() {}
};
static HixlRegProfCallback prof_cb_reg;
}

namespace hixl {
  HixlProfilingReporter::HixlProfilingReporter(const HixlProfType api_id) : hixl_api_(api_id) {
    const std::lock_guard<std::mutex> lk(g_prof_mutex);
    if (g_prof_run) {
      start_time_ = MsprofSysCycleTime();
    }
  }

  HixlProfilingReporter::HixlProfilingReporter(const HixlProfType api_id, uint64_t start_time) 
      : start_time_(start_time), hixl_api_(api_id) {}

  uint64_t HixlProfilingReporter::GetSysCycleTime() {
    uint64_t sys_cycle_time = 0;
    const std::lock_guard<std::mutex> lk(g_prof_mutex);
    if (g_prof_run) {
      HIXL_LOGI("[Hixl Profiling] Profiling is enabled. Get start time.");
      sys_cycle_time  = MsprofSysCycleTime();
    }
    return sys_cycle_time;
  }

  HixlProfilingReporter::~HixlProfilingReporter() noexcept {
    const std::lock_guard<std::mutex> lk(g_prof_mutex);
    if (g_prof_run && (start_time_ != 0UL)) {
      const uint64_t end_time = MsprofSysCycleTime();
      MsprofApi api{};
      api.beginTime = start_time_;
      api.endTime = end_time;
      api.threadId = static_cast<uint32_t>(mmGetTid());
      api.level = MSPROF_REPORT_ACL_LEVEL;
      api.type = static_cast<uint32_t>(hixl_api_);
      (void)MsprofReportApi(true, &api);
    }
  }
}