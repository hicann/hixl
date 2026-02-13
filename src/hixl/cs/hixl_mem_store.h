/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_CS_HIXL_MEM_STORE_H_
#define CANN_HIXL_SRC_HIXL_CS_HIXL_MEM_STORE_H_
#include <cstdint>
#include <mutex>
#include "hixl/hixl_types.h"

namespace hixl {
struct MemoryRegion {
  const void *addr = nullptr;  // 内存起始地址
  size_t size = 0U;            // 内存区域大小

  MemoryRegion() = default;
  MemoryRegion(const void *a, size_t s) noexcept : addr(a), size(s) {}
};

/**
 * @brief 内存存储管理类
 *
 * 负责管理Server和Client端注册的内存区域，提供内存访问验证功能，与client绑定，作为client的成员变量，一个Client有一个memstore对象，用于记录client侧的endpoint分配的内存地址和sever侧分配的内存地址。channel销毁后，销毁memstore
 */
class HixlMemStore {
 public:
  HixlMemStore() = default;
  ~HixlMemStore() = default;

  /**
   * @brief 给client的endpoint分配内存时，登记Client端分配的内存区域
   * @param is_server 判断是哪一侧注册内存
   * @param addr 要注册的内存起始地址
   * @param size 要注册的内存区域大小
   * @return 操作结果
   */
  Status RecordMemory(bool is_server, const void *addr, size_t size);

  /**
   * @brief channel销毁后，注销Server端已经注册的内存区域
   */
  Status UnrecordMemory(bool is_server, const void *addr);

  /**
   * @brief 验证Client对Server的内存访问请求是否在注册范围内
   * @param server_addr 请求访问的Server端内存地址
   * @param mem_size 请求访问的Server端内存大小
   * @param client_addr 发起请求的Client端内存地址
   * @return 验证结果
   */
  Status ValidateMemoryAccess(const void *server_addr, size_t mem_size, const void *client_addr);
  bool CheckMemoryForRegister(bool is_server, const void *check_addr, size_t check_size);


 private:
  bool CheckMemoryForAccess(bool is_server, const void *check_addr, size_t check_size);
  // 内存区域信息结构体
  std::map<const void *, MemoryRegion> server_regions_;
  std::map<const void *, MemoryRegion> client_regions_;
  std::mutex mutex_;

  HixlMemStore(const HixlMemStore &) = delete;
  HixlMemStore &operator=(const HixlMemStore &) = delete;
};
}  // namespace hixl
#endif  // CANN_HIXL_SRC_HIXL_CS_HIXL_MEM_STORE_H_
