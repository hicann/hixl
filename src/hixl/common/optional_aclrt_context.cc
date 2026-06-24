/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "common/optional_aclrt_context.h"

#include "acl/acl_rt.h"
#include "common/hixl_checker.h"
#include "common/hixl_log.h"

namespace hixl {
OptionalAclrtContext::~OptionalAclrtContext() {
  DestroyContext();
}

Status OptionalAclrtContext::GetCurrentContext() {
  if (has_context_) {
    return SUCCESS;
  }

  uint32_t device_count = 0;
  HIXL_CHK_ACL_RET(aclrtGetDeviceCount(&device_count), "aclrtGetDeviceCount failed");
  if (device_count == 0U) {
    ctx_ = nullptr;
    has_context_ = false;
    owns_context_ = false;
    HIXL_LOGI("aclrtGetDeviceCount returns 0, skip aclrtGetCurrentContext");
    return SUCCESS;
  }

  HIXL_CHK_ACL_RET(aclrtGetCurrentContext(&ctx_));
  has_context_ = true;
  owns_context_ = false;
  HIXL_LOGI("aclrtGetCurrentContext ctx=%p", ctx_);
  return SUCCESS;
}

Status OptionalAclrtContext::CreateContext() {
  DestroyContext();
  uint32_t device_count = 0;
  HIXL_CHK_ACL_RET(aclrtGetDeviceCount(&device_count), "aclrtGetDeviceCount failed");
  if (device_count == 0U) {
    HIXL_LOGI("aclrtGetDeviceCount returns 0, skip aclrtCreateContext");
    return SUCCESS;
  }

  int32_t device_id = -1;
  aclrtContext ctx = nullptr;
  HIXL_CHK_ACL_RET(aclrtGetDevice(&device_id), "aclrtGetDevice failed");
  HIXL_CHK_ACL_RET(aclrtCreateContext(&ctx, device_id), "aclrtCreateContext failed");
  ctx_ = ctx;
  has_context_ = true;
  owns_context_ = true;
  HIXL_LOGI("Created optional aclrt context:%p, device_id:%d", ctx_, device_id);
  return SUCCESS;
}

Status OptionalAclrtContext::SetCurrentContext() const {
  if (!has_context_) {
    return SUCCESS;
  }
  HIXL_CHK_ACL_RET(aclrtSetCurrentContext(ctx_));
  return SUCCESS;
}

std::unique_ptr<TemporaryRtContext> OptionalAclrtContext::GetContextGuard() const {
  if (!has_context_) {
    return nullptr;
  }
  return MakeUnique<TemporaryRtContext>(ctx_);
}

void OptionalAclrtContext::DestroyContext() {
  if (owns_context_ && ctx_ != nullptr) {
    HIXL_CHK_ACL(aclrtDestroyContext(ctx_), "aclrtDestroyContext failed");
  }
  ctx_ = nullptr;
  has_context_ = false;
  owns_context_ = false;
}
}  // namespace hixl
