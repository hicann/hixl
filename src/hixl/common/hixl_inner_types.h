/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_COMMON_HIXL_INNER_TYPES_H_
#define CANN_HIXL_SRC_HIXL_COMMON_HIXL_INNER_TYPES_H_

#include <string>
#include <sstream>
#include "hixl/hixl_types.h"

namespace hixl {
constexpr const char *kProtocolRoce = "roce";
constexpr const char *kProtocolUbCtp = "ub_ctp";
constexpr const char *kProtocolUbTp = "ub_tp";
constexpr const char *kPlacementDevice = "device";
constexpr const char *kPlacementHost = "host";

struct AddrInfo {
  uintptr_t start_addr{0};
  uintptr_t end_addr{0};
  MemType mem_type{MemType::MEM_DEVICE};
};

struct EndpointConfig {
  std::string protocol;
  std::string comm_id;
  std::string placement;
  std::string plane;
  std::string dst_eid;
  std::string net_instance_id;

  std::string ToString() const {
    std::ostringstream oss;
    oss << "EndpointConfig{";
    oss << "protocol: " << protocol << ", ";
    oss << "comm_id: " << comm_id << ", ";
    oss << "placement: " << placement << ", ";
    oss << "plane: " << plane << ", ";
    oss << "dst_eid: " << dst_eid << ", ";
    oss << "net_instance_id: " << net_instance_id;
    oss << "}";
    return oss.str();
  }
};

struct MemInfo {
  MemHandle mem_handle;
  MemDesc mem;
  MemType type;
};
}  // namespace hixl
#endif  // CANN_HIXL_SRC_HIXL_COMMON_HIXL_INNER_TYPES_H_