/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "notify_addr_resolver.h"

#include <set>
#include <string>

#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "proxy/hccp_proxy.h"
#include "rt_external.h"

namespace {
constexpr rtDevResProcType_t kNotifyDevResProcType = RT_PROCESS_HCCP;
constexpr rtDevResType_t kNotifyDevResType = RT_RES_TYPE_STARS_NOTIFY_RECORD;

hixl::Status ResolveNotifyDeviceAddressByRuntime(aclrtNotify notify, uint64_t &notify_addr, uint32_t &notify_len) {
  HIXL_CHECK_NOTNULL(notify);
  uint32_t notify_id = 0U;
  HIXL_CHK_ACL_RET(aclrtGetNotifyId(notify, &notify_id), "[NotifyResolver] aclrtGetNotifyId failed");
  rtDevResInfo res_info{};
  res_info.dieId = 0U;
  res_info.procType = kNotifyDevResProcType;
  res_info.resType = kNotifyDevResType;
  res_info.resId = notify_id;
  res_info.flag = 0U;
  rtDevResAddrInfo addr_info{};
  addr_info.resAddress = &notify_addr;
  addr_info.len = &notify_len;
  HIXL_CHK_ACL_RET(rtGetDevResAddress(&res_info, &addr_info), "[NotifyResolver] rtGetDevResAddress failed");
  return hixl::SUCCESS;
}

bool IsA2OrA3Soc(const char *soc_name) {
  if (soc_name == nullptr) {
    return false;
  }
  static const std::set<std::string> kSocA2 = {"Ascend910B1", "Ascend910B2",  "Ascend910B3",
                                               "Ascend910B4", "Ascend910B2C", "Ascend910B4-1"};
  static const std::set<std::string> kSocA3 = {"Ascend910_9391", "Ascend910_9381", "Ascend910_9392",
                                               "Ascend910_9382", "Ascend910_9372", "Ascend910_9362"};
  const std::string soc(soc_name);
  return kSocA2.find(soc) != kSocA2.end() || kSocA3.find(soc) != kSocA3.end();
}
}  // namespace

namespace hixl {

Status NotifyAddrResolver::Resolve(int32_t device_id, aclrtNotify notify, uint64_t &notify_addr, uint32_t &notify_len) {
  HIXL_CHECK_NOTNULL(notify);
  notify_addr = 0U;
  notify_len = 0U;
  const char *soc_name = aclrtGetSocName();
  HIXL_CHK_BOOL_RET_STATUS(soc_name != nullptr, FAILED, "[NotifyResolver] aclrtGetSocName returned nullptr");
  if (IsA2OrA3Soc(soc_name)) {
    HIXL_LOGI("[NotifyResolver] Resolve notify address by HCCP. device_id=%d soc=%s", device_id, soc_name);
    return HccpProxy::RaGetNotifyAddrLen(device_id, notify, notify_addr, notify_len);
  }
  HIXL_LOGI("[NotifyResolver] Resolve notify address by runtime. device_id=%d soc=%s", device_id, soc_name);
  return ResolveNotifyDeviceAddressByRuntime(notify, notify_addr, notify_len);
}

}  // namespace hixl
