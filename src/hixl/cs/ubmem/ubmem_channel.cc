/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "cs/ubmem/ubmem_channel.h"

#include <atomic>

#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/hixl_utils.h"
#include "common/soc_type.h"
#include "cs/global_config.h"
#include "cs/ubmem/ubmem_engine.h"
#include "cs/ubmem/ubmem_aicpu_types.h"

namespace hixl {
namespace {
std::atomic<uint64_t> g_next_ubmem_channel_id{1UL};

// Gen1/Gen2 SoCs submit stars v1 SQEs from the UB_MEM AICPU kernel; A5 (and any newer device that
// reports an unknown SoC name) uses stars v2.
UbMemStarsSqeFormat SqeFormatFromSocType(SocType soc_type) {
  return (soc_type == SocType::kGen1 || soc_type == SocType::kGen2) ? UbMemStarsSqeFormat::kStarsV1
                                                                    : UbMemStarsSqeFormat::kStarsV2;
}
}  // namespace

UbMemChannel::UbMemChannel(Endpoint &local_endpoint) : local_endpoint_(local_endpoint) {}

UbMemChannel::~UbMemChannel() {
  (void)Destroy();
}

Status UbMemChannel::Initialize(const ChannelContext &ctx) {
  HIXL_CHK_STATUS_RET(InitContext(ctx), "[UbMemChannel] InitContext failed");
  if (ctx_.global_config->MaxTransferCountPerBatch() > kMaxFixedQueueTransferCountPerBatch) {
    HIXL_LOGE(PARAM_INVALID, "[UbMemChannel] max_transfer_count_per_batch=%u exceeds UB_MEM range [1, %u]",
              ctx_.global_config->MaxTransferCountPerBatch(), kMaxFixedQueueTransferCountPerBatch);
    return PARAM_INVALID;
  }
  SocType soc_type = SocType::kDefault;
  HIXL_CHK_STATUS_RET(SocTypeUtil::GetSocType(soc_type), "[UbMemChannel] GetSocType for ubmem failed");
  const UbMemStarsSqeFormat sqe_format = SqeFormatFromSocType(soc_type);
  HIXL_LOGI("[UbMemChannel] ubmem AICPU SQE format resolved, sqe_format=%u", static_cast<uint32_t>(sqe_format));
  engine_ = MakeUnique<UbMemEngine>(ctx_.device_id, *ctx_.global_config, local_endpoint_, sqe_format);
  HIXL_CHECK_NOTNULL(engine_);
  return SUCCESS;
}

Status UbMemChannel::Connect(const ChannelDesc &channel_desc, uint32_t timeout_ms) {
  (void)channel_desc;
  (void)timeout_ms;
  channel_handle_ = g_next_ubmem_channel_id.fetch_add(1U, std::memory_order_relaxed);
  HIXL_LOGI("[UbMemChannel] Connect success, channel=%lu", channel_handle_);
  return SUCCESS;
}

Status UbMemChannel::Disconnect() {
  channel_handle_ = 0UL;
  return SUCCESS;
}

Status UbMemChannel::Destroy() {
  if (engine_ != nullptr) {
    engine_->Finalize();
    engine_.reset();
  }
  channel_handle_ = 0UL;
  initialized_ = false;
  return SUCCESS;
}

Status UbMemChannel::RegMem(const char *mem_tag, const CommMem *mem, MemHandle *mem_handle) {
  HIXL_CHK_STATUS_RET(EnsureInitialized("RegMem"), "[UbMemChannel] EnsureInitialized failed");
  (void)mem_tag;
  HIXL_CHECK_NOTNULL(mem);
  HIXL_CHECK_NOTNULL(mem_handle);
  HIXL_CHK_BOOL_RET_STATUS(mem->addr != nullptr && mem->size > 0U, PARAM_INVALID,
                           "[UbMemChannel] Invalid memory registration, mem_addr:%p, mem_size:%lu bytes", mem->addr,
                           mem->size);
  // On the ubmem path the server and the client may register the same physical memory, while ACL allows a
  // PA handle to be exported only once. The server side already exported it, so skip the client side
  // registration and hand back the address as the handle to keep the returned handle non-null.
  HIXL_LOGI("[UbMemChannel] Skip client mem registration for ubmem, mem_addr:%p, mem_size:%lu bytes", mem->addr,
            mem->size);
  *mem_handle = mem->addr;
  return SUCCESS;
}

Status UbMemChannel::UnregMem(MemHandle mem_handle) {
  HIXL_CHK_STATUS_RET(EnsureInitialized("UnregMem"), "[UbMemChannel] EnsureInitialized failed");
  // The ubmem path skips client side registration, so its deregistration is skipped as well.
  HIXL_LOGI("[UbMemChannel] Skip client mem deregistration for ubmem, mem_handle:%p", mem_handle);
  return SUCCESS;
}

Status UbMemChannel::SetRemoteMemInfo(const CommMem &trans_flag) {
  HIXL_CHK_STATUS_RET(EnsureInitialized("SetRemoteMemInfo"), "[UbMemChannel] EnsureInitialized failed");
  (void)trans_flag;
  return SUCCESS;
}

Status UbMemChannel::DoBatchTransferSync(bool is_get, const HixlOneSideOpDesc *desc_list, uint32_t list_num,
                                         uint32_t timeout_ms) {
  HIXL_CHECK_NOTNULL(engine_);
  return engine_->SubmitSync(is_get, list_num, desc_list, timeout_ms);
}

Status UbMemChannel::DoBatchTransferAsync(bool is_get, const HixlOneSideOpDesc *desc_list, uint32_t list_num,
                                          void **complete_handle) {
  HIXL_CHECK_NOTNULL(engine_);
  return engine_->SubmitAsync(is_get, list_num, desc_list, complete_handle);
}

Status UbMemChannel::QueryCompleteStatus(void *complete_handle, HixlCompleteStatus *status) {
  HIXL_CHECK_NOTNULL(complete_handle);
  HIXL_CHECK_NOTNULL(status);
  HIXL_CHECK_NOTNULL(engine_);
  const Status ret = engine_->CheckStatus(complete_handle, *status);
  if ((ret == SUCCESS || ret == FAILED) && *status == HixlCompleteStatus::HIXL_COMPLETE_STATUS_FAILED) {
    LatchTransferFailure(FAILED);
  }
  return ret;
}

}  // namespace hixl
