/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#ifndef CANN_HIXL_SRC_HIXL_PROXY_ACLRT_PROXY_H_
#define CANN_HIXL_SRC_HIXL_PROXY_ACLRT_PROXY_H_

#include <cstdint>
#include "acl/acl.h"

namespace hixl {

/**
 * @brief ACL runtime compatibility proxy.
 *
 * aclrtGetPhyDevIdByUserDevId is available from CANN 9.2.0. On 9.1.0 the symbol is absent,
 * so this proxy uses a weak reference and falls back to aclrtGetPhyDevIdByLogicDevId.
 */
class AclrtProxy {
 public:
  AclrtProxy() = delete;

  /**
   * @brief Map a user device id to a physical device id.
   * @param user_dev_id User (visible) device id.
   * @param phy_dev_id Output physical device id.
   * @return ACL_SUCCESS on success, otherwise the ACL error from the underlying API.
   */
  static aclError GetPhyDevIdByUserDevId(int32_t user_dev_id, int32_t *phy_dev_id);
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_PROXY_ACLRT_PROXY_H_
