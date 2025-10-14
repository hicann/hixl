/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HE31ABFDB_286F_4B25_95BB_B14889387A5D
#define HE31ABFDB_286F_4B25_95BB_B14889387A5D

#include <limits>
#include "memory/type/mem_size.h"
#include "memory/type/mem_addr.h"

namespace llm {
///////////////////////////////////////////////////////////
using PageLen = uint32_t;

constexpr PageLen PAGE_LENGTH_INVALID = std::numeric_limits<PageLen>::max();

constexpr PageLen PageLen_GetLenFromSize(MemSize size, size_t page_idem_num) noexcept {
  return size >> page_idem_num;
}

constexpr MemSize PageLen_GetMemSize(PageLen page_len, size_t page_idem_num) noexcept {
  return static_cast<MemSize>(page_len) << page_idem_num;
}

constexpr MemAddr PageLen_ForwardAddr(PageLen page_len, size_t page_idem_num, MemAddr addr) noexcept {
  return addr + PageLen_GetMemSize(page_len, page_idem_num);
}

///////////////////////////////////////////////////////////
using SpanLayerId = PageLen;

constexpr SpanLayerId SPAN_LAYER_ID_INVALID = std::numeric_limits<SpanLayerId>::max();

constexpr SpanLayerId SpanLayerId_GetIdFromSize(MemSize size, size_t page_idem_num) noexcept {
  return PageLen_GetLenFromSize(size, page_idem_num);
}

constexpr SpanLayerId SpanLayerId_GetMemSize(SpanLayerId layer_id, size_t page_idem_num) noexcept {
  return PageLen_GetMemSize(layer_id, page_idem_num);
}
}

#endif
