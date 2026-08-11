/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#include "hixl_batch_transfer.h"
#include <cinttypes>
#include <mutex>
#include <string>
#include <vector>
#include "common/hixl_log.h"
#include "common/hixl_checker.h"
#include "common/scope_guard.h"
#include "proxy/hcomm_proxy.h"
#include "transfer_context_manager.h"
#include "hixl/hixl.h"

namespace hixl {
namespace {

Status ValidateBatchTransferParam(HixlOneSideOpParam *param) {
  HIXL_CHK_BOOL_RET_STATUS(param != nullptr, PARAM_INVALID, "[HixlBatchPutAndGet] param is nullptr");
  constexpr uint32_t kMaxBatchSize = 8192;
  HIXL_CHK_BOOL_RET_STATUS(param->list_num > 0U && param->list_num <= kMaxBatchSize, PARAM_INVALID,
                           "[HixlBatchPutAndGet] invalid list_num:%u, valid range:[1, %u]", param->list_num,
                           kMaxBatchSize);
  HIXL_CHK_BOOL_RET_STATUS(param->op_desc_list_addr != 0U, PARAM_INVALID,
                           "[HixlBatchPutAndGet] op_desc_list_addr is null");
  return SUCCESS;
}

int32_t TransferWithBatch(bool is_read, HixlOneSideOpParam *param) {
  auto *op_list = reinterpret_cast<HixlOneSideOpDesc *>(static_cast<uintptr_t>(param->op_desc_list_addr));
  std::vector<HcommBatchTransferDesc> descs(param->list_num);
  for (uint32_t i = 0; i < param->list_num; i++) {
    descs[i].transType = is_read ? HCOMM_TRANSFER_TYPE_READ : HCOMM_TRANSFER_TYPE_WRITE;
    if (is_read) {
      descs[i].transferInfo.read.len = op_list[i].len;
      descs[i].transferInfo.read.dst = op_list[i].local_buf;
      descs[i].transferInfo.read.src = op_list[i].remote_buf;
    } else {
      descs[i].transferInfo.write.len = op_list[i].len;
      descs[i].transferInfo.write.dst = op_list[i].remote_buf;
      descs[i].transferInfo.write.src = op_list[i].local_buf;
    }
  }

  int32_t ret = HcommProxy::BatchTransferOnThread(param->thread, param->channel, descs.data(), param->list_num);
  if (ret == HCCL_E_NOT_SUPPORT) {
    HIXL_LOGD("[HixlBatchTransfer] HcommBatchTransferOnThread not supported.");
    return HCCL_E_NOT_SUPPORT;
  }
  return ret;
}

Status TransferWithSingle(bool is_read, HixlOneSideOpParam *param) {
  auto *op_list = reinterpret_cast<HixlOneSideOpDesc *>(static_cast<uintptr_t>(param->op_desc_list_addr));
  if (is_read) {
    for (uint32_t i = 0; i < param->list_num; i++) {
      HIXL_LOGD("[HixlBatchPutAndGet] HcommReadOnThread start, list_num=%u, i=%u, thread=%" PRIu64 ", channel=%" PRIu64
                ", "
                "dst_buf_list[%u]=%p, src_buf_list[%u]=%p, len_list[%u]=%lu",
                param->list_num, i, param->thread, param->channel, i, op_list[i].local_buf, i, op_list[i].remote_buf, i,
                op_list[i].len);
      HIXL_CHK_HCCL_RET(
          static_cast<HcclResult>(HcommProxy::ReadOnThread(param->thread, param->channel, op_list[i].local_buf,
                                                           op_list[i].remote_buf, op_list[i].len)),
          "thread:%" PRIu64 ", channel:%" PRIu64 ", dst_buf:%p, src_buf:%p, size:%" PRIu64 " bytes", param->thread,
          param->channel, op_list[i].local_buf, op_list[i].remote_buf, op_list[i].len);
    }
  } else {
    for (uint32_t i = 0; i < param->list_num; i++) {
      HIXL_LOGD("[HixlBatchPutAndGet] HcommWriteOnThread start, list_num=%u, i=%u, thread=%" PRIu64 ", channel=%" PRIu64
                ", "
                "dst_buf_list[%u]=%p, src_buf_list[%u]=%p, len_list[%u]=%lu",
                param->list_num, i, param->thread, param->channel, i, op_list[i].remote_buf, i, op_list[i].local_buf, i,
                op_list[i].len);
      HIXL_CHK_HCCL_RET(
          static_cast<HcclResult>(HcommProxy::WriteOnThread(param->thread, param->channel, op_list[i].remote_buf,
                                                            op_list[i].local_buf, op_list[i].len)),
          "thread:%" PRIu64 ", channel:%" PRIu64 ", dst_buf:%p, src_buf:%p, size:%" PRIu64 " bytes", param->thread,
          param->channel, op_list[i].remote_buf, op_list[i].local_buf, op_list[i].len);
    }
  }
  return SUCCESS;
}

Status HixlBatchTransferTask(bool is_read, HixlOneSideOpParam *param) {
  int32_t batch_ret = TransferWithBatch(is_read, param);
  if (batch_ret == HCCL_E_NOT_SUPPORT) {
    HIXL_LOGD("[HixlBatchTransfer] HcommBatchTransferOnThread not supported, fallback to single calls");
    return TransferWithSingle(is_read, param);
  }
  HIXL_CHK_HCCL_RET(static_cast<HcclResult>(batch_ret),
                    "thread:%" PRIu64 ", channel:%" PRIu64 ", list_num:%u, is_read:%d", param->thread, param->channel,
                    param->list_num, static_cast<int32_t>(is_read));
  HIXL_LOGD("[HixlBatchTransfer] HcommBatchTransferOnThread success");
  return SUCCESS;
}

Status ReadRemoteFlag(HixlOneSideOpParam *param) {
  HIXL_LOGD(
      "[HixlBatchPutAndGet] HcommReadOnThread start to read remote flag, flag_size=%u, "
      "local_flag=%lu, remote_flag=%lu",
      param->flag_size, param->local_flag_addr, param->remote_flag_addr);
  HIXL_CHK_HCCL_RET(
      static_cast<HcclResult>(HcommProxy::ReadOnThread(
          param->thread, param->channel, reinterpret_cast<void *>(static_cast<uintptr_t>(param->local_flag_addr)),
          reinterpret_cast<void *>(static_cast<uintptr_t>(param->remote_flag_addr)), param->flag_size)),
      "thread:%" PRIu64 ", channel:%" PRIu64 ", dst_addr:%" PRIu64 ", src_addr:%" PRIu64 ", size:%u bytes",
      param->thread, param->channel, param->local_flag_addr, param->remote_flag_addr, param->flag_size);
  return SUCCESS;
}

Status RecordRemoteNotify(HixlOneSideOpParam *param) {
  HIXL_LOGD("[HixlBatchPutAndGet] aclrtNotifyRecordOnThread start to read remote flag, thread[%lu], notify_id[%u]",
            param->thread, param->notify_id);
  HIXL_CHK_HCCL_RET(static_cast<HcclResult>(HcommProxy::aclrtNotifyRecordOnThread(param->thread, param->notify_id)),
                    "thread:%" PRIu64 ", notify_id:%u", param->thread, param->notify_id);
  return SUCCESS;
}

Status HandleRemoteFlag(HixlOneSideOpParam *param) {
  HIXL_LOGD("[HixlBatchPutAndGet] HixlBatchTransfer use_notify_record=%u.", param->use_notify_record);
  if (param->remote_flag_addr == 0) {
    return SUCCESS;
  }
  return param->use_notify_record == 0 ? ReadRemoteFlag(param) : RecordRemoteNotify(param);
}

Status HixlBatchTransfer(bool is_read, HixlOneSideOpParam *param) {
  HIXL_LOGD("[HixlBatchPutAndGet] HixlBatchTransfer %s start.", is_read ? "read" : "write");
  HIXL_CHK_STATUS_RET(ValidateBatchTransferParam(param), "[HixlBatchPutAndGet] validate param failed");
  auto ctx = TransferContextManager::Instance().Get(param->thread);
  HIXL_CHK_BOOL_RET_STATUS(ctx != nullptr && ctx->GetState() == TRANSFER_THREAD_STATE_INITIALIZED, FAILED,
                           "[HixlBatchPutAndGet] transfer context unavailable, thread:%lu",
                           static_cast<uint64_t>(param->thread));
  std::lock_guard<TransferContext> transfer_lock(*ctx);
  HIXL_CHK_BOOL_RET_STATUS(ctx->GetState() == TRANSFER_THREAD_STATE_INITIALIZED, FAILED,
                           "[HixlBatchPutAndGet] transfer context deleting after lock, state:%u",
                           static_cast<uint32_t>(ctx->GetState()));

  HIXL_DISMISSABLE_GUARD(err_flag_guard, ([&ctx]() { ctx->WriteErrorFlag(); }));

  constexpr const char *kBatchTag = "HixlKernel";
  HIXL_CHK_HCCL_RET(static_cast<HcclResult>(HcommProxy::BatchModeStart(kBatchTag)), "batch_tag:%s", kBatchTag);
  HIXL_DISMISSABLE_GUARD(batch_mode, ([kBatchTag]() {
                           HIXL_CHK_HCCL(static_cast<HcclResult>(HcommProxy::BatchModeEnd(kBatchTag)), "batch_tag:%s",
                                         kBatchTag);
                         }));

  HIXL_CHK_STATUS_RET(HixlBatchTransferTask(is_read, param),
                      "[HixlBatchPutAndGet] HixlBatchTransferTask failed, is_read:%d", static_cast<int32_t>(is_read));

  HIXL_CHK_HCCL_RET(static_cast<HcclResult>(HcommProxy::ChannelFenceOnThread(param->thread, param->channel)),
                    "[HixlBatchPutAndGet] thread:%" PRIu64 ", channel:%" PRIu64, param->thread, param->channel);

  HIXL_CHK_STATUS_RET(HandleRemoteFlag(param), "[HixlBatchPutAndGet] handle remote flag failed");

  HIXL_DISMISS_GUARD(batch_mode);
  HIXL_CHK_HCCL_RET(static_cast<HcclResult>(HcommProxy::BatchModeEnd(kBatchTag)), "batch_tag:%s", kBatchTag);

  HIXL_DISMISS_GUARD(err_flag_guard);
  return SUCCESS;
}
}  // namespace
}  // namespace hixl
extern "C" {
uint32_t HixlBatchPut(HixlOneSideOpParam *param) {
  const uint64_t thread = (param == nullptr) ? 0ULL : static_cast<uint64_t>(param->thread);
  HIXL_CHK_STATUS_RET(hixl::HixlBatchTransfer(false, param), "[HixlBatchPut] failed, thread:%" PRIu64, thread);
  return hixl::SUCCESS;
}

uint32_t HixlBatchGet(HixlOneSideOpParam *param) {
  const uint64_t thread = (param == nullptr) ? 0ULL : static_cast<uint64_t>(param->thread);
  HIXL_CHK_STATUS_RET(hixl::HixlBatchTransfer(true, param), "[HixlBatchGet] failed, thread:%" PRIu64, thread);
  return hixl::SUCCESS;
}
}  // extern "C"
