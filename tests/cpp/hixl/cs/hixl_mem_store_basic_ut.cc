/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "gtest/gtest.h"
#include "hixl_mem_store.h"
#include "hixl/hixl_types.h"
namespace hixl {
TEST(HixlMemStoreBasicTest, RecordValidateAndUnrecord) {
  HixlMemStore store;
  uint32_t kServerDataAddr = 1;
  uint32_t kClientDataAddr = 2;
  void* saddr = &kServerDataAddr;
  void* caddr = &kClientDataAddr;
  constexpr uint32_t kBlockSizeBytes = 64;      // 远端数据块大小
  constexpr uint32_t kClientBufSizeBytes = 4096;  // 客户端缓冲区大小
  // 记录 server/client 内存
  EXPECT_EQ(store.RecordMemory(true, saddr, kClientBufSizeBytes), hixl::SUCCESS);
  EXPECT_EQ(store.RecordMemory(false, caddr, kClientBufSizeBytes), hixl::SUCCESS);

  // 校验访问
  EXPECT_EQ(store.ValidateMemoryAccess(saddr, kBlockSizeBytes, caddr), hixl::SUCCESS);

  // 重复记录同一server地址跳过记录
  EXPECT_EQ(store.RecordMemory(true, saddr, kClientBufSizeBytes), hixl::SUCCESS);
  // 重复记录同一client地址应返回参数错误
  EXPECT_EQ(store.RecordMemory(false, caddr, kClientBufSizeBytes), hixl::PARAM_INVALID);

  // 注销
  EXPECT_EQ(store.UnrecordMemory(true, saddr), hixl::SUCCESS);
  EXPECT_EQ(store.UnrecordMemory(false, caddr), hixl::SUCCESS);

  // 再次注销应参数错误
  EXPECT_EQ(store.UnrecordMemory(true, saddr), hixl::PARAM_INVALID);
}
}  // namespace hixl