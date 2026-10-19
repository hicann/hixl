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

#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/soc_type.h"
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
}  // namespace

namespace hixl {

Status NotifyAddrResolver::Resolve(int32_t device_id, aclrtNotify notify, uint64_t &notify_addr, uint32_t &notify_len) {
  HIXL_CHECK_NOTNULL(notify);
  notify_addr = 0U;
  notify_len = 0U;
  SocType soc_type = SocType::kDefault;
  HIXL_CHK_STATUS_RET(SocTypeUtil::GetSocType(soc_type), "[NotifyResolver] GetSocType failed. device_id=%d", device_id);
  if (soc_type == SocType::kGen1 || soc_type == SocType::kGen2) {
    HIXL_LOGI("[NotifyResolver] Resolve notify address by HCCP. device_id=%d", device_id);
    return HccpProxy::RaGetNotifyAddrLen(device_id, notify, notify_addr, notify_len);
  }
  HIXL_LOGI("[NotifyResolver] Resolve notify address by runtime. device_id=%d", device_id);
  return ResolveNotifyDeviceAddressByRuntime(notify, notify_addr, notify_len);
}

}  // namespace hixl
