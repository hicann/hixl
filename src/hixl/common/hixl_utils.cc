/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hixl_utils.h"
#include <arpa/inet.h>
#include "securec.h"
#include "nlohmann/json.hpp"
#include "hixl_log.h"
#include "hixl_checker.h"

namespace hixl {
Status HcclError2Status(HcclResult ret) {
  static const std::map<HcclResult, Status> result2status = {
      {HCCL_SUCCESS, SUCCESS},
      {HCCL_E_PARA, PARAM_INVALID},
      {HCCL_E_TIMEOUT, TIMEOUT},
      {HCCL_E_NOT_SUPPORT, UNSUPPORTED},
  };
  const auto &it = result2status.find(ret);
  if (it != result2status.cend()) {
    return it->second;
  }
  return FAILED;
}

Status ParseIpAddress(const std::string &ip_str, CommAddr &addr) {
  struct in_addr ipv4_addr;
  (void)memset_s(&ipv4_addr, sizeof(ipv4_addr), 0, sizeof(ipv4_addr));
  if (inet_pton(AF_INET, ip_str.c_str(), &ipv4_addr) == 1) {
    addr.type = COMM_ADDR_TYPE_IP_V4;
    addr.addr = ipv4_addr;
    return SUCCESS;
  }

  struct in6_addr ipv6_addr;
  (void)memset_s(&ipv6_addr, sizeof(ipv6_addr), 0, sizeof(ipv6_addr));
  if (inet_pton(AF_INET6, ip_str.c_str(), &ipv6_addr) == 1) {
    addr.type = COMM_ADDR_TYPE_IP_V6;
    addr.addr6 = ipv6_addr;
    return SUCCESS;
  }

  HIXL_LOGE(PARAM_INVALID, "Invalid IP address: %s", ip_str.c_str());
  return PARAM_INVALID;
}


Status CheckIp(const std::string &ip) {
  struct in_addr addr;
  struct sockaddr_in6 ipv6_addr;
  HIXL_CHK_BOOL_RET_STATUS(
      inet_pton(AF_INET, ip.c_str(), &addr) == 1 || inet_pton(AF_INET6, ip.c_str(), &ipv6_addr.sin6_addr) == 1,
      hixl::PARAM_INVALID, "%s is not a valid ip address", ip.c_str());
  return hixl::SUCCESS;
}

std::vector<std::string, std::allocator<std::string>> Split(const std::string &str, const char delim) {
  std::vector<std::string, std::allocator<std::string>> elems;
  if (str.empty()) {
    (void)elems.emplace_back("");
    return elems;
  }

  std::stringstream ss(str);
  std::string item;
  while (getline(ss, item, delim)) {
    (void)elems.push_back(item);
  }

  const auto str_size = str.size();
  if ((str_size > 0U) && (str[str_size - 1U] == delim)) {
    (void)elems.emplace_back("");
  }
  return elems;
}

Status ParseListenInfo(const std::string &listen_info, std::string &listen_ip, int32_t &listen_port) {
  std::vector<std::string> listen_infos;
  size_t left = listen_info.find('[');
  size_t right = listen_info.find(']');
  if (left != std::string::npos && right != std::string::npos && left < right) {
    listen_infos.emplace_back(listen_info.substr(left + 1, right - left - 1));
    size_t colon = listen_info.find(':', right);
    if (colon != std::string::npos) {
      listen_infos.emplace_back(listen_info.substr(colon + 1));
    }
  } else {
    listen_infos = Split(listen_info, ':');
  }
  HIXL_CHK_BOOL_RET_STATUS(listen_infos.size() >= 1U, PARAM_INVALID,
                           "listen info is invalid: %s, expect ${ip}:${port} or ${ip}", listen_info.c_str());
  listen_ip = listen_infos[0];
  HIXL_CHK_STATUS_RET(CheckIp(listen_ip), "IP is invalid: %s, listen info = %s", listen_ip.c_str(),
                      listen_info.c_str());
  if (listen_infos.size() > 1U) {
    HIXL_CHK_STATUS_RET(ToNumber(listen_infos[1], listen_port), "Port:%s is invalid.", listen_infos[1].c_str());
  }
  return SUCCESS;
}
}  // namespace hixl
