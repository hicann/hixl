/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_COMMON_HIXL_UTILS_H_
#define CANN_HIXL_SRC_HIXL_COMMON_HIXL_UTILS_H_

#include <memory>
#include <utility>
#include <sstream>
#include "hixl_cs.h"
#include "hccl/hccl_types.h"
#include "hixl/hixl_types.h"
#include "hixl_checker.h"
#include "hixl_inner_types.h"

namespace hixl {
template <typename _Tp, typename... _Args>
static inline std::shared_ptr<_Tp> MakeShared(_Args &&... __args) {
  using _Tp_nc = typename std::remove_const<_Tp>::type;
  const std::shared_ptr<_Tp> ret(new (std::nothrow) _Tp_nc(std::forward<_Args>(__args)...));
  return ret;
}

template <typename T>
struct MakeUniq {
  using unique_obj = std::unique_ptr<T>;
};

template <typename T, typename... Args>
inline auto MakeUnique(Args &&...args) -> typename MakeUniq<T>::unique_obj {
  using T_nc = typename std::remove_const<T>::type;
  return std::unique_ptr<T>(new (std::nothrow) T_nc(std::forward<Args>(args)...));
}

template <typename T, typename... Args>
inline typename MakeUniq<T>::invalid_type MakeUnique(Args &&...) = delete;

template<typename T>
static hixl::Status ToNumber(const std::string &num_str, T &value) {
    std::stringstream ss(num_str);
    ss >> value;
    HIXL_CHK_BOOL_RET_STATUS(!ss.fail(), hixl::PARAM_INVALID, "Failed to convert [%s] to number", num_str.c_str());
    HIXL_CHK_BOOL_RET_STATUS(ss.eof(), hixl::PARAM_INVALID, "Failed to convert [%s] to number", num_str.c_str());
    return hixl::SUCCESS;
}

Status HcclError2Status(HcclResult ret);

Status ParseIpAddress(const std::string &ip_str, CommAddr &addr);

Status CheckIp(const std::string &ip);

std::vector<std::string, std::allocator<std::string>> Split(const std::string &str, const char delim);

Status ParseListenInfo(const std::string &listen_info, std::string &listen_ip, int32_t &listen_port);

Status ParseEidAddress(const std::string &eid_str, CommAddr &addr);

Status ConvertToEndpointInfo(const EndpointConfig &endpoint_config, EndpointDesc &endpoint, uint32_t devPhyId);

Status CheckAddrOverlap(const AddrInfo &cur_info, const std::map<MemHandle, AddrInfo> &addr_map, bool &is_duplicate,
                        MemHandle &existing_handle);

Status SerializeEndpointConfigList(const std::vector<EndpointConfig> &list, std::string &msg_str);

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_COMMON_HIXL_UTILS_H_
