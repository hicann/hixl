/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <cstdint>
#include <map>
#include <mutex>
#include <unordered_set>
#include <utility>
#include <unistd.h>
#include <sys/syscall.h>
#include "acl/acl.h"
#include "aprof_pub.h"
#include "common/hixl_log.h"
#include "prof_proxy.h"

namespace {
bool g_prof_run = false;
std::mutex g_prof_mutex;
std::unordered_set<uint32_t> g_device_list;
std::map<uint64_t, std::pair<uint64_t, hixl::HixlProfType>> g_msprof_ranges;
uint64_t g_next_msprof_range_id = 1UL;
constexpr uint32_t kProfCtrlSwitch = static_cast<uint32_t>(PROF_CTRL_SWITCH);

constexpr uint32_t kStartProfiling = 1U;
constexpr uint32_t kStopProfiling = 2U;
constexpr uint32_t kHixlModuleId = 12U;
constexpr uint32_t kHixlProfTypeStartOffset = 0x009000U;
// Keep this value aligned with ACL_PROF_ACL_API from acl/acl_prof.h.
constexpr uint64_t kAclProfAclApiSwitch = 0x0001ULL;

uint32_t GetMsprofTypeId(const hixl::HixlProfType api_id) {
  return MSPROF_REPORT_ACL_OTHERS_BASE_TYPE + kHixlProfTypeStartOffset + 1U + static_cast<uint32_t>(api_id);
}

void ReportMsprofApi(const uint64_t begin_time, const hixl::HixlProfType api_id) {
  MsprofApi api{};
  api.beginTime = begin_time;
  api.endTime = MsprofSysCycleTime();
  api.threadId = static_cast<uint32_t>(syscall(SYS_gettid));
  api.level = MSPROF_REPORT_ACL_LEVEL;
  api.type = GetMsprofTypeId(api_id);
  (void)MsprofReportApi(true, &api);
}

uint64_t AllocateMsprofRangeId() {
  if (g_next_msprof_range_id == hixl::kInvalidProfRange) {
    g_next_msprof_range_id = 1UL;
  }
  return g_next_msprof_range_id++;
}

uint64_t StartMsprofRange(const hixl::HixlProfType api_id) {
  const std::lock_guard<std::mutex> lk(g_prof_mutex);
  if (!g_prof_run) {
    return hixl::kInvalidProfRange;
  }
  const uint64_t range_id = AllocateMsprofRangeId();
  g_msprof_ranges.emplace(range_id, std::make_pair(MsprofSysCycleTime(), api_id));
  return range_id;
}

void StopMsprofRange(const uint64_t range_id) {
  uint64_t begin_time = 0UL;
  hixl::HixlProfType api_id = hixl::HixlProfType::HixlOpBatchRead;
  {
    const std::lock_guard<std::mutex> lk(g_prof_mutex);
    const auto iter = g_msprof_ranges.find(range_id);
    if (iter == g_msprof_ranges.end()) {
      return;
    }
    begin_time = iter->second.first;
    api_id = iter->second.second;
    g_msprof_ranges.erase(iter);
    if (!g_prof_run || (begin_time == 0UL)) {
      return;
    }
  }
  ReportMsprofApi(begin_time, api_id);
}

void DestroyMsprofRange(const uint64_t range_id) {
  const std::lock_guard<std::mutex> lk(g_prof_mutex);
  g_msprof_ranges.erase(range_id);
}

aclError RegisterProfType() {
  const hixl::HixlProfType types[] = {hixl::HixlProfType::HixlOpBatchRead, hixl::HixlProfType::HixlOpBatchWrite};
  for (const hixl::HixlProfType api_id : types) {
    const uint32_t type_id = GetMsprofTypeId(api_id);
    const auto ret = MsprofRegTypeInfo(MSPROF_REPORT_ACL_LEVEL, type_id, hixl::GetProfName(api_id));
    if (ret != MSPROF_ERROR_NONE) {
      HIXL_LOGE(ACL_ERROR_PROFILING_FAILURE,
                "[Hixl Profiling] Call api:MsprofRegTypeInfo failed, ret:%d, type_id:%u, name:%s", ret, type_id,
                hixl::GetProfName(api_id));
      return ACL_ERROR_PROFILING_FAILURE;
    }
  }
  return ACL_SUCCESS;
}

aclError UpdateDeviceList(const uint32_t *const device_id_list, const uint32_t device_nums, const bool add_device) {
  if (device_id_list == nullptr) {
    HIXL_LOGE(ACL_ERROR_INVALID_PARAM, "[Hixl Profiling] Device_id_list is null, please check");
    return ACL_ERROR_INVALID_PARAM;
  }
  for (size_t idx = 0U; idx < device_nums; idx++) {
    const uint32_t device_id = *(device_id_list + idx);
    if (add_device) {
      (void)g_device_list.insert(device_id);
      HIXL_LOGI("[Hixl Profiling] Device id %u is successfully added in hixl profiling", device_id);
      continue;
    }
    (void)g_device_list.erase(device_id);
    HIXL_LOGI("[Hixl Profiling] Device id %u is successfully deleted from hixl profiling", device_id);
  }
  return ACL_SUCCESS;
}

aclError ProfInnerStart(const MsprofCommandHandle *const profiler_config) {
  HIXL_LOGI("[Hixl Profiling] Start to execute profInnerStart");
  if (!g_prof_run) {
    const aclError reg_ret = RegisterProfType();
    if (reg_ret != ACL_SUCCESS) {
      HIXL_LOGE(ACL_ERROR_PROFILING_FAILURE, "[Hixl Profiling] Register prof type failed, ret:%d", reg_ret);
      return ACL_ERROR_PROFILING_FAILURE;
    }
    g_prof_run = true;
  }
  return UpdateDeviceList(profiler_config->devIdList, profiler_config->devNums, true);
}

aclError ProfInnerStop(const MsprofCommandHandle *const profiler_config) {
  HIXL_LOGI("[Hixl Profiling] Start to execute profInnerStop");
  const aclError remove_ret = UpdateDeviceList(profiler_config->devIdList, profiler_config->devNums, false);
  if (remove_ret != ACL_SUCCESS) {
    return remove_ret;
  }
  if (g_device_list.empty() && g_prof_run) {
    g_prof_run = false;
  }
  HIXL_LOGI("[Hixl Profiling] Successfully execute ProfInnerStop");
  return ACL_SUCCESS;
}

aclError HandleProfSwitch(const MsprofCommandHandle *const profiler_config) {
  const uint64_t prof_switch = profiler_config->profSwitch;
  const uint32_t type = profiler_config->type;
  if (((prof_switch & kAclProfAclApiSwitch) != 0U) && (type == kStartProfiling)) {
    return ProfInnerStart(profiler_config);
  }
  if (((prof_switch & kAclProfAclApiSwitch) != 0U) && (type == kStopProfiling)) {
    return ProfInnerStop(profiler_config);
  }
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
    HIXL_LOGE(ACL_ERROR_INVALID_PARAM,
              "[Hixl Profiling] [Check][Len]len[%u] is invalid, it should not be smaller than %zu", len, command_len);
    return ACL_ERROR_INVALID_PARAM;
  }
  return HandleProfSwitch(static_cast<MsprofCommandHandle *>(data));
}

aclError HixlProfCtrlHandle(uint32_t data_type, void *data, uint32_t data_len) {
  if (data == nullptr) {
    HIXL_LOGE(ACL_ERROR_INVALID_PARAM, "[Hixl Profiling] Data is null, please check");
    return ACL_ERROR_INVALID_PARAM;
  }
  if (data_type != kProfCtrlSwitch) {
    HIXL_LOGI("[Hixl Profiling] get unsupported data_type %u while processing profiling data", data_type);
    return ACL_SUCCESS;
  }
  const aclError ret = ProcessProfData(data, data_len);
  if (ret != ACL_SUCCESS) {
    HIXL_LOGE(ret, "[Hixl Profiling] [Process][ProfSwitch] Call api:ProcessProfData failed, result is %u", ret);
  }
  return ret;
}

class HixlRegProfCallback {
 public:
  HixlRegProfCallback() {
    const auto prof_ret = MsprofRegisterCallback(kHixlModuleId, &HixlProfCtrlHandle);
    if (prof_ret != 0) {
      HIXL_LOGE(ACL_ERROR_PROFILING_FAILURE,
                "[Hixl Profiling] Call api:MsprofRegisterCallback failed, prof result = %d, module_id:%u", prof_ret,
                kHixlModuleId);
    }
  }
};
static HixlRegProfCallback prof_cb_reg;

}  // namespace

namespace hixl {
uint64_t MsprofStartRange(const HixlProfType prof_type) {
  return StartMsprofRange(prof_type);
}

void MsprofStopRange(const uint64_t range_id) {
  StopMsprofRange(range_id);
}

void MsprofDestroyRange(const uint64_t range_id) {
  DestroyMsprofRange(range_id);
}
}  // namespace hixl
