/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "channel.h"

#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/hixl_utils.h"
#include "endpoint.h"
#include "hixl_mem_store.h"

namespace hixl {

Status Channel::InitContext(const ChannelContext &ctx) {
  ctx_ = ctx;
  HIXL_CHK_BOOL_RET_STATUS(ctx_.local_endpoint != nullptr, FAILED, "[Channel] Initialize with null local endpoint");
  HIXL_CHK_BOOL_RET_STATUS(ctx_.mem_store != nullptr, FAILED, "[Channel] Initialize with null mem store");
  HIXL_CHK_BOOL_RET_STATUS(ctx_.global_config != nullptr, FAILED, "[Channel] Initialize with null global config");
  initialized_ = true;
  return SUCCESS;
}

Status Channel::EnsureInitialized(const char *op) const {
  HIXL_CHK_BOOL_RET_STATUS(initialized_, FAILED,
                           "[Channel] %s rejected, channel is not initialized; call Initialize() first", op);
  return SUCCESS;
}

Status Channel::ValidateAddress(uint32_t list_num, const HixlOneSideOpDesc *desc_list) const {
  // hccs/ubmem:device链路不经hcomm注册本地内存，跳过本地内存校验
  const EndpointDesc &local_ep = ctx_.local_endpoint->GetEndpoint();
  const bool check_local_mem = !((local_ep.protocol == COMM_PROTOCOL_HCCS || IsUbMemProtocol(local_ep.protocol)) &&
                                 local_ep.loc.locType == ENDPOINT_LOC_TYPE_DEVICE);
  HIXL_CHK_STATUS_RET(ctx_.mem_store->BatchValidateMemoryAccess(list_num, desc_list, check_local_mem),
                      "Validate address failed, list_num=%u", list_num);
  return SUCCESS;
}

void Channel::LatchTransferFailure(Status ret) {
  if (transfer_failure_latched_) {
    return;
  }
  transfer_failure_latched_ = true;
  transfer_failure_status_ = ret;
  HIXL_LOGE(ret, "[Channel] Transfer failure latched, status=%u", static_cast<uint32_t>(ret));
}

Status Channel::LatchTransferFailureIfNeeded(Status ret) {
  if (ret != SUCCESS && ret != RESOURCE_EXHAUSTED) {
    LatchTransferFailure(ret);
  }
  return ret;
}

Status Channel::BatchTransferSync(bool is_get, const char *op, const HixlOneSideOpDesc *desc_list, uint32_t list_num,
                                  uint32_t timeout_ms) {
  HIXL_CHK_STATUS_RET(EnsureInitialized(op), "[Channel] EnsureInitialized failed, op=%s", op);
  HIXL_CHK_BOOL_RET_STATUS(!transfer_failure_latched_, FAILED,
                           "[Channel] %s rejected after prior transfer failure. last_status=%u, list_num=%u", op,
                           static_cast<uint32_t>(transfer_failure_status_), list_num);
  HIXL_CHK_STATUS_RET(ValidateAddress(list_num, desc_list), "[Channel] ValidateAddress failed, op=%s", op);
  return LatchTransferFailureIfNeeded(DoBatchTransferSync(is_get, desc_list, list_num, timeout_ms));
}

Status Channel::BatchTransferAsync(bool is_get, const char *op, const HixlOneSideOpDesc *desc_list, uint32_t list_num,
                                   void **complete_handle) {
  HIXL_CHK_STATUS_RET(EnsureInitialized(op), "[Channel] EnsureInitialized failed, op=%s", op);
  HIXL_CHK_BOOL_RET_STATUS(!transfer_failure_latched_, FAILED,
                           "[Channel] %s rejected after prior transfer failure. last_status=%u, list_num=%u", op,
                           static_cast<uint32_t>(transfer_failure_status_), list_num);
  HIXL_CHK_STATUS_RET(ValidateAddress(list_num, desc_list), "[Channel] ValidateAddress failed, op=%s", op);
  return LatchTransferFailureIfNeeded(DoBatchTransferAsync(is_get, desc_list, list_num, complete_handle));
}

Status Channel::BatchPutSync(const HixlOneSideOpDesc *desc_list, uint32_t list_num, uint32_t timeout_ms) {
  return BatchTransferSync(false, "BatchPutSync", desc_list, list_num, timeout_ms);
}

Status Channel::BatchGetSync(const HixlOneSideOpDesc *desc_list, uint32_t list_num, uint32_t timeout_ms) {
  return BatchTransferSync(true, "BatchGetSync", desc_list, list_num, timeout_ms);
}

Status Channel::BatchPutAsync(const HixlOneSideOpDesc *desc_list, uint32_t list_num, void **complete_handle) {
  return BatchTransferAsync(false, "BatchPutAsync", desc_list, list_num, complete_handle);
}

Status Channel::BatchGetAsync(const HixlOneSideOpDesc *desc_list, uint32_t list_num, void **complete_handle) {
  return BatchTransferAsync(true, "BatchGetAsync", desc_list, list_num, complete_handle);
}

}  // namespace hixl
