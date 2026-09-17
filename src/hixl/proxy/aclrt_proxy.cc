/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "aclrt_proxy.h"

#include "common/hixl_log.h"

extern "C" {
// Prototype for CANN < 9.2.0 headers that do not declare this symbol. Identical to CANN 9.2.0 acl_rt.h.
ACL_FUNC_VISIBILITY aclError aclrtGetPhyDevIdByUserDevId(const int32_t userDevId, int32_t *const phyDevId);
#pragma weak aclrtGetPhyDevIdByUserDevId
}

namespace hixl {

aclError AclrtProxy::GetPhyDevIdByUserDevId(int32_t user_dev_id, int32_t *phy_dev_id) {
  if (aclrtGetPhyDevIdByUserDevId != nullptr) {
    return aclrtGetPhyDevIdByUserDevId(user_dev_id, phy_dev_id);
  }
  static bool logged = false;
  if (!logged) {
    logged = true;
    HIXL_LOGI("[AclrtProxy] aclrtGetPhyDevIdByUserDevId is unavailable, fallback to aclrtGetPhyDevIdByLogicDevId");
  }
  return aclrtGetPhyDevIdByLogicDevId(user_dev_id, phy_dev_id);
}

}  // namespace hixl
