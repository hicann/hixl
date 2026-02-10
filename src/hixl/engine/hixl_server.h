/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_ENGINE_HIXL_SERVER_H
#define CANN_HIXL_SRC_HIXL_ENGINE_HIXL_SERVER_H

#include <vector>
#include <mutex>
#include <map>
#include "hixl/hixl_types.h"
#include "common/hixl_inner_types.h"

namespace hixl {

class HixlServer {
 public:
  HixlServer() = default;
  ~HixlServer() = default;
  /**
   * @brief 初始化HixlServer
   * @param [in] ip 服务端ip
   * @param [in] port 服务端监听的端口
   * @param [in] data_end_point_list 服务端支持的传输协议
   * @return 成功:SUCCESS, 失败:其它.
   */
  Status Initialize(const std::string &ip, int32_t port, const std::vector<EndpointConfig> &data_end_point_list);

  /**
   * @brief 注册内存
   * @param [in] mem 需要注册的内存的描述信息
   * @param [in] type 需要注册的内存的类型
   * @param [out] mem_handle 注册成功返回的内存handle, 可用于内存解注册
   * @return 成功:SUCCESS, 失败:其它.
   */
  Status RegisterMem(const MemDesc &mem, MemType type, MemHandle &mem_handle);

  /**
   * @brief 解注册内存
   * @param [in] mem_handle 注册内存返回的内存handle
   * @return 成功:SUCCESS, 失败:其它.
   */
  Status DeregisterMem(MemHandle &mem_handle);

  /**
   * @brief 销毁server
   */
  Status Finalize();

 private:
  void *server_handle_ = nullptr;
  std::vector<EndpointConfig> data_endpoint_config_list_;
  std::mutex mtx_;
  std::map<MemHandle, AddrInfo> handle_to_addr_;
};
}  // namespace hixl
#endif  // #ifndef CANN_HIXL_SRC_HIXL_ENGINE_HIXL_SERVER_H