/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#ifndef CANN_HIXL_SRC_HIXL_PROXY_ASCEND_HAL_PROXY_H_
#define CANN_HIXL_SRC_HIXL_PROXY_ASCEND_HAL_PROXY_H_

#include <cstdint>
#include "hixl/hixl_types.h"

namespace hixl {

// Matches drvRegisterTpye in ascend_hal (values only; avoid linking ascend_hal headers).
constexpr uint32_t kDevSvmMapHost = 2U;
constexpr uint32_t kHostMemMapDevPcieTh = 3U;  // HOST_MEM_MAP_DEV_PCIE_TH / HOST_MEM_MAP_DEV_V2

/**
 * @brief Proxy for host/device memory register and UB segment APIs via libascend_hal.so (dlopen).
 */
class AscendHalProxy {
 public:
  AscendHalProxy() = delete;

  /**
   * @brief Register memory and obtain the mapped address on the peer side.
   * @param src Source VA (host for HOST_MEM_MAP_DEV_PCIE_TH, device for DEV_SVM_MAP_HOST).
   * @param size Bytes to register.
   * @param flag Register type (e.g. kHostMemMapDevPcieTh / kDevSvmMapHost).
   * @param logic_dev_id Logic device id for halHostRegister.
   * @param dst Output mapped VA.
   */
  static Status HostRegister(void *src, uint64_t size, uint32_t flag, uint32_t logic_dev_id, void **dst);

  /**
   * @brief Unregister a previously registered source VA.
   * @param src The same source pointer passed to HostRegister.
   * @param logic_dev_id Logic device id.
   */
  static Status HostUnregister(void *src, uint32_t logic_dev_id);

  /**
   * @brief Register a device SVM memory segment for UB direct access.
   * @param logic_dev_id Logic device id.
   * @param va Start virtual address to register.
   * @param size Byte size to register.
   */
  static Status MemRegUbSegment(uint32_t logic_dev_id, uint64_t va, uint64_t size);

  /**
   * @brief Unregister a previously registered UB segment.
   * @param logic_dev_id Logic device id.
   * @param va Start virtual address to unregister.
   * @param size Byte size to unregister.
   */
  static Status MemUnRegUbSegment(uint32_t logic_dev_id, uint64_t va, uint64_t size);

#ifdef HIXL_ENABLE_TEST_HOOKS
  // Test-only hook to clear the process-local dynamic loader cache between tests.
  static void ResetForTest();
#endif
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_PROXY_ASCEND_HAL_PROXY_H_
