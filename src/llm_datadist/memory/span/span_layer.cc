/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "memory/span/span_layer.h"
#include "memory/span/span_allocator.h"
#include "common/llm_log.h"

namespace llm {
void SpanLayer::Release(SpanAllocator &span_allocator) {
  while (!free_link_.empty()) {
    auto span = free_link_.pop_front();
    if (span == nullptr) {
      continue;
    }
    if (span->HasSplited()) {
      LLMLOGE(ge::FAILED, "[SpanLayer]: releasing splited span [addr : %p, len : %u]",
             span->GetBlockAddr(), span->GetPageLen());
    }
    span_allocator.Free(*span);
  }
}
}

