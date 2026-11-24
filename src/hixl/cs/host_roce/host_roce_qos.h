/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_CS_HOST_ROCE_QOS_H_
#define CANN_HIXL_SRC_HIXL_CS_HOST_ROCE_QOS_H_

#include <cstdint>
#include <string>
#include "hixl/hixl_types.h"

namespace hixl {
// Same sentinel as HCCL_COMM_QOS_CONFIG_NOT_SET. A configured business qos is in [0, 7].
constexpr uint32_t kHostRoceQosNotSet = 0xFFFFFFFFU;
// HCCN fallback when qos_dscp is missing or does not contain the requested qos. DSCP 33 << 2 == TC 132.
constexpr uint8_t kHostRoceDefaultDscp = 33U;
constexpr const char *kHostRoceQosCfgPath = "/etc/hcomm.cfg";

// Read qos_dscp_{phy_id} from cfg_path ("qos:dscp,...") and write the TC/SL used on the wire.
// Missing file, missing key, empty value, or an absent qos uses the default DSCP. A malformed value fails.
Status ResolveHostRoceQos(uint32_t qos, uint32_t phy_id, const std::string &cfg_path, uint8_t &tc, uint8_t &sl);

// When qos is set, replace tc/sl from this host's /etc/hcomm.cfg. Unset qos leaves tc/sl unchanged.
Status ApplyHostRoceQos(uint32_t qos, uint8_t &tc, uint8_t &sl);

#ifdef HIXL_ENABLE_TEST_HOOKS
void SetHostRoceQosCfgPathForTest(const std::string &path);
#endif
}  // namespace hixl
#endif
