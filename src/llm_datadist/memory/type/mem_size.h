/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef H57132A1F_3AE9_4FB2_A6CF_2C2503EB9E9E
#define H57132A1F_3AE9_4FB2_A6CF_2C2503EB9E9E

namespace llm {
using MemSize = unsigned long long;

constexpr MemSize MEM_SIZE_ALIGN = 512;
constexpr MemSize MEM_SIZE_KB = 1024;
constexpr MemSize MEM_SIZE_MB = 1024 * MEM_SIZE_KB;
constexpr MemSize MEM_SIZE_GB = 1024 * MEM_SIZE_MB;

constexpr MemSize operator "" _BYTE(MemSize size) noexcept {
  return size;
}

constexpr MemSize operator "" _KB(MemSize size) noexcept {
  return size * MEM_SIZE_KB;
}

constexpr MemSize operator "" _MB(MemSize size) noexcept {
  return size * MEM_SIZE_MB;
}

constexpr MemSize operator "" _GB(MemSize size) noexcept {
  return size * MEM_SIZE_GB;
}

constexpr MemSize MemSize_GetAlignedOf(MemSize size, MemSize alignSize) noexcept {
  return ((size + alignSize - 1) & ~(alignSize - 1));
}
}

#endif
