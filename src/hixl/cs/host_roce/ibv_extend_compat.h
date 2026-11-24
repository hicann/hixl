/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_CS_HOST_ROCE_IBV_EXTEND_COMPAT_H_
#define CANN_HIXL_SRC_HIXL_CS_HOST_ROCE_IBV_EXTEND_COMPAT_H_

#include <cstdint>
struct ibv_context_extend;
struct ibv_qp;
namespace hixl {
// Public ibv_extend 4.3 ABI subset. Keep reserved fields: the provider reads the full attribute object.
// Source: driver/src/custom/nda/ibv_extend/ibv_extend.h. No HCOMM or provider-private ABI is used here.
struct HostRoceHyperFeature {
  uint8_t type{0U};
  uint8_t version{0U};
  uint8_t sack_enable{0U};
  uint8_t reserved[61]{};
};
struct HostRoceMpath {
  uint32_t flowlet{0U};
  uint32_t paths{0U};
  uint32_t interval{0U};
  uint32_t rr{0U};
  uint32_t reserved[32]{};
};
struct HostRoceAr {
  uint32_t rr{0U};
  uint32_t reserved[32]{};
};
struct HostRoceSack {
  uint32_t tx{0U};
  uint32_t rx{0U};
  uint32_t reserved[32]{};
};
struct HostRoceExtendedQpAttr {
  ibv_qp *qp{nullptr};
  uint32_t udp_source_port{0U};
  HostRoceHyperFeature feature{};
  uint32_t lb_mode{0U};
  HostRoceMpath mpath{};
  HostRoceAr ar{};
  HostRoceSack sack{};
  uint32_t reserved[47]{};
  uint32_t lag_port{0U};
};
static_assert(sizeof(HostRoceHyperFeature) == 64, "Hyper feature ABI size");
}  // namespace hixl
#endif
