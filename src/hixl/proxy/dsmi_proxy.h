/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#ifndef CANN_HIXL_SRC_HIXL_PROXY_DSMI_PROXY_H_
#define CANN_HIXL_SRC_HIXL_PROXY_DSMI_PROXY_H_

#include <cstdint>
#include "hixl/hixl_types.h"

namespace hixl {

/**
 * @brief Proxy class for DSMI (Device System Management Interface) operations.
 *        Provides interface to query device board information via libdrvdsmi_host.so.
 */
class DsmiProxy {
 public:
  DsmiProxy() = delete;

  /**
   * @brief Get device slot ID via dsmi_get_board_info.
   * @param device_id Logical device ID.
   * @param slot_id Output slot ID from board info.
   * @return SUCCESS or FAILED.
   */
  static Status GetDevSlotId(int32_t device_id, uint32_t &slot_id);
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_PROXY_DSMI_PROXY_H_