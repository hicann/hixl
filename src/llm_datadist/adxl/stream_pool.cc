/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "stream_pool.h"
#include <mutex>
#include "runtime/rt.h"
#include "adxl_checker.h"

namespace adxl {
Status StreamPool::TryAllocStream(rtStream_t &stream) {
  std::lock_guard<std::mutex> lock(pool_mutex_);
  for (auto &item : pool_) {
    if (item.second) {
      item.second = false;
      stream = item.first;
      return SUCCESS;
    }
  }  
  if (pool_.size() < max_stream_num_) {
    rtStream_t new_stream = nullptr;
    ADXL_CHK_ACL_RET(rtStreamCreateWithFlags(&new_stream, RT_STREAM_PRIORITY_DEFAULT,
                     RT_STREAM_FAST_LAUNCH | RT_STREAM_FAST_SYNC));
    pool_[new_stream] = false;
    stream = new_stream;
    LLMLOGI("Create new stream, current stream pool size: %zu", pool_.size());
    return SUCCESS;
  }  
  LLMLOGW("Stream Pool capacity limit reached, current stream pool size: %zu", pool_.size());
  return RESOURCE_EXHAUSTED;
}

void StreamPool::FreeStream(rtStream_t &stream) {
  std::lock_guard<std::mutex> lock(pool_mutex_);
  auto it = pool_.find(stream);
  if (it != pool_.end()) {
    it->second = true;
  }
}

void StreamPool::Finalize() {
  std::lock_guard<std::mutex> lock(pool_mutex_);
  for (auto &it : pool_) {
    if (it.first != nullptr) {
      (void)rtStreamDestroy(it.first);
    }
  }
  pool_.clear();
}
void StreamPool::DestroyStream(rtStream_t &stream) {
  std::lock_guard<std::mutex> lock(pool_mutex_);
  auto it = pool_.find(stream);
  if (it != pool_.end()) {
    auto rt_abort = rtStreamAbort(stream);
    if (rt_abort != RT_ERROR_NONE) {
      LLMLOGE(FAILED, "Call rtStreamAbort ret:%d.", rt_abort);
    }
    auto rt_destroy = rtStreamDestroy(stream);
    if (rt_destroy != RT_ERROR_NONE) {
      LLMLOGE(FAILED, "Call rtStreamDestroy ret:%d.", rt_destroy);
    }
    pool_.erase(it);
  }
}
}// namespace adxl