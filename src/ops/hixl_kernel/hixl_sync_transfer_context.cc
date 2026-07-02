/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#include "hixl_sync_transfer_context.h"

#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "transfer_context_manager.h"

namespace hixl {
namespace {

Status ValidateSyncTransferContextParam(const HixlTransferContextSyncParam *param) {
  if (param == nullptr) {
    HIXL_LOGE(PARAM_INVALID, "[HixlSyncTransferContext] param is nullptr");
    return PARAM_INVALID;
  }
  if (param->entry_num == 0U) {
    HIXL_LOGE(PARAM_INVALID, "[HixlSyncTransferContext] entry_num is 0");
    return PARAM_INVALID;
  }
  if (param->entry_list_addr == 0U) {
    HIXL_LOGE(PARAM_INVALID, "[HixlSyncTransferContext] entry_list_addr is 0");
    return PARAM_INVALID;
  }
  if (param->state_list_addr == 0U) {
    HIXL_LOGE(PARAM_INVALID, "[HixlSyncTransferContext] state_list_addr is 0");
    return PARAM_INVALID;
  }
  return SUCCESS;
}

uint32_t DoSyncTransferContext(HixlTransferContextSyncParam *param) {
  HIXL_CHK_STATUS_RET(ValidateSyncTransferContextParam(param), "[HixlSyncTransferContext] validate param failed");
  HIXL_LOGI("[HixlSyncTransferContext] device execute start. entry_num=%u", param->entry_num);
  auto *entries = reinterpret_cast<HixlTransferContextSyncEntry *>(static_cast<uintptr_t>(param->entry_list_addr));
  auto *states = reinterpret_cast<uint32_t *>(static_cast<uintptr_t>(param->state_list_addr));
  HixlTransferThreadState state = TRANSFER_THREAD_STATE_DELETED;
  for (uint32_t i = 0U; i < param->entry_num; ++i) {
    if (entries[i].op == TRANSFER_CONTEXT_OP_ADD) {
      state = TransferContextManager::Instance().Add(entries[i].thread, entries[i].user_stream_id, entries[i].notify_id,
                                                     entries[i].err_flag_dev_va);
    } else if (entries[i].op == TRANSFER_CONTEXT_OP_DELETE) {
      state = TransferContextManager::Instance().Delete(entries[i].thread);
    } else {
      HIXL_LOGE(PARAM_INVALID, "[HixlSyncTransferContext] invalid op=%u, index=%u", entries[i].op, i);
      return PARAM_INVALID;
    }
    states[i] = static_cast<uint32_t>(state);
  }
  HIXL_LOGI("[HixlSyncTransferContext] device execute end. entry_num=%u last_state=%u", param->entry_num,
            static_cast<uint32_t>(state));
  return SUCCESS;
}

}  // namespace
}  // namespace hixl

extern "C" {
uint32_t HixlSyncTransferContext(HixlTransferContextSyncParam *param) {
  uint32_t ret = hixl::DoSyncTransferContext(param);
  if (ret != 0U) {
    HIXL_LOGE(hixl::FAILED, "[HixlSyncTransferContext] failed, ret is %u", ret);
    return hixl::FAILED;
  }
  return ret;
}
}  // extern "C"
