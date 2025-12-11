/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HIXL_SRC_LLMDATADIST_ADXL_STREAM_POOL_H
#define HIXL_SRC_LLMDATADIST_ADXL_STREAM_POOL_H

#include <mutex>
#include <map>
#include "runtime/rt.h"
#include "adxl/adxl_types.h"

namespace adxl {

class StreamPool { 
public:
  explicit StreamPool(size_t max_stream_num) : max_stream_num_(max_stream_num) {}
  ~StreamPool() = default;
  void Finalize();
  Status TryAllocStream(rtStream_t &stream);
  void FreeStream(rtStream_t &stream);
  void DestroyStream(rtStream_t &stream);

private:
  std::mutex pool_mutex_;
  std::map<rtStream_t, bool> pool_;
  size_t max_stream_num_;
};
}// namespace adxl

#endif  // HIXL_SRC_LLMDATADIST_ADXL_STREAM_POOL_H