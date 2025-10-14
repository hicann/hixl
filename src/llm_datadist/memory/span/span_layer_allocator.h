/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef H91ACA5ED_F7C8_4915_98CE_83796B352D22
#define H91ACA5ED_F7C8_4915_98CE_83796B352D22

#include "memory/util/object_allocator.h"
#include "memory/span/span_layer.h"

namespace llm {
class SpanLayerAllocator : public ObjectAllocator<SpanLayer> {
 public:
  explicit SpanLayerAllocator(size_t capacity) : ObjectAllocator(capacity) {}
};
}

#endif
