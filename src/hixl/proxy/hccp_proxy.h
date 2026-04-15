/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#ifndef CANN_HIXL_SRC_HIXL_PROXY_HCCP_PROXY_H_
#define CANN_HIXL_SRC_HIXL_PROXY_HCCP_PROXY_H_

#include <cstdint>
#include "acl/acl.h"
#include "common/hixl_checker.h"

namespace hixl {

/**
 * @brief Resolve Stars notify record device VA and byte length via RA (libra.so: RaRdevGetHandle,
 *        RaGetNotifyBaseAddr) and runtime API rtNotifyGetAddrOffset (runtime/rt_external_event.h), aligned with
 *        HCCL CreateRdmaSignal.
 * @note Physical device id uses aclrtGetPhyDevIdByLogicDevId (same as RankTableGeneratorV2::GenerateLocalCommRes).
 *       This path assumes 910B/910_93-class notify record size (4 bytes).
 */
class HccpProxy {
 public:
  HccpProxy() = delete;
  static Status RaGetNotifyAddrLen(int32_t device_id, aclrtNotify notify, uint64_t &notify_addr,
                                   uint32_t &notify_len);
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_PROXY_HCCP_PROXY_H_
