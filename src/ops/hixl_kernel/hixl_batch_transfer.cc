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
#include <string>
#include <vector>
#include "common/hixl_log.h"
#include "common/hixl_checker.h"
#include "proxy/hcomm_proxy.h"
#include "hixl/hixl.h"

namespace hixl {
namespace {
constexpr uint32_t kMaxBatchSize = 1000;

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

  int32_t ret = 0;
  uint32_t offset = 0;
  while (offset < param->list_num) {
    uint32_t batch_size = std::min(kMaxBatchSize, param->list_num - offset);
    ret = HcommProxy::BatchTransferOnThread(param->thread, param->channel, descs.data() + offset, batch_size);
    if (ret != 0) {
      if (ret == HCCL_E_NOT_SUPPORT) {
        HIXL_LOGI("[HixlBatchTransfer] HcommBatchTransferOnThread not supported.");
        return HCCL_E_NOT_SUPPORT;
      }
      HIXL_LOGE(FAILED, "[HixlBatchTransfer] BatchTransferOnThread failed at offset %u, batch_size %u, ret=%d",
                offset, batch_size, ret);
      return ret;
    }
    offset += batch_size;
  }
  return ret;
}

uint32_t TransferWithSingle(bool is_read, HixlOneSideOpParam *param) {
  auto *op_list = reinterpret_cast<HixlOneSideOpDesc *>(static_cast<uintptr_t>(param->op_desc_list_addr));
  if (is_read) {
    for (uint32_t i = 0; i < param->list_num; i++) {
      HIXL_LOGI(
          "[HixlBatchPutAndGet] HcommReadOnThread start, list_num=%u, i=%u, thread=%u, channel=%u, "
          "dst_buf_list[%u]=%p, src_buf_list[%u]=%p, len_list[%u]=%lu",
          param->list_num, i, param->thread, param->channel, i, op_list[i].local_buf, i,
          op_list[i].remote_buf, i, op_list[i].len);
      int32_t ret = HcommProxy::ReadOnThread(param->thread, param->channel, op_list[i].local_buf,
                                            op_list[i].remote_buf, op_list[i].len);
      if (ret != 0) {
        HIXL_LOGE(FAILED,
                  "HcommReadOnThread failed. The address information is as follows:dst_buf:%p, scr_buf:%p, buf_len:%u, "
                  "ret is %d.",
                  op_list[i].local_buf, op_list[i].remote_buf, op_list[i].len, ret);
        return FAILED;
      }
    }
  } else {
    for (uint32_t i = 0; i < param->list_num; i++) {
      HIXL_LOGI(
          "[HixlBatchPutAndGet] HcommWriteOnThread start, list_num=%u, i=%u, thread=%u, channel=%u, "
          "dst_buf_list[%u]=%p, src_buf_list[%u]=%p, len_list[%u]=%lu",
          param->list_num, i, param->thread, param->channel, i, op_list[i].remote_buf, i,
          op_list[i].local_buf, i, op_list[i].len);
      int32_t ret = HcommProxy::WriteOnThread(param->thread, param->channel, op_list[i].remote_buf,
                                              op_list[i].local_buf, op_list[i].len);
      if (ret != 0) {
        HIXL_LOGE(FAILED,
                  "HcommWriteOnThread failed. The address information is as follows:dst_buf:%p, scr_buf:%p, "
                  "buf_len:%u, ret is %d.",
                  op_list[i].remote_buf, op_list[i].local_buf, op_list[i].len, ret);
        return FAILED;
      }
    }
  }
  return SUCCESS;
}

uint32_t HixlBatchTransferTask(bool is_read, HixlOneSideOpParam *param) {
  int32_t batch_ret = TransferWithBatch(is_read, param);
  if (batch_ret == HCCL_E_NOT_SUPPORT) {
    HIXL_LOGI("[HixlBatchTransfer] HcommBatchTransferOnThread not supported, fallback to single calls");
    return TransferWithSingle(is_read, param);
  }
  if (batch_ret != 0) {
    HIXL_LOGE(FAILED, "HcommBatchTransferOnThread failed, ret=%d", batch_ret);
    return FAILED;
  }
  HIXL_LOGI("[HixlBatchTransfer] HcommBatchTransferOnThread success");
  return SUCCESS;
}

uint32_t HixlBatchTransfer(bool is_read, HixlOneSideOpParam *param) {
  HIXL_LOGI("[HixlBatchPutAndGet] HixlBatchTransfer %s start.", is_read ? "read" : "write");
  if (param == nullptr) {
    HIXL_LOGE(PARAM_INVALID, "[HixlBatchPutAndGet] param is nullptr");
    return PARAM_INVALID;
  }

  constexpr const char *kBatchTag = "HixlKernel";
  int32_t ret = HcommProxy::BatchModeStart(kBatchTag);
  HIXL_CHK_BOOL_RET_STATUS(ret == 0, FAILED, "[HixlBatchPutAndGet] HcommBatchModeStart failed, ret is %d", ret);

  ret = HixlBatchTransferTask(is_read, param);
  HIXL_CHK_BOOL_RET_STATUS(ret == 0, FAILED, "[HixlBatchPutAndGet] HixlBatchTransferTask failed, ret is %d", ret);

  ret = HcommProxy::ChannelFenceOnThread(param->thread, param->channel);
  HIXL_CHK_BOOL_RET_STATUS(ret == 0, FAILED, "[HixlBatchPutAndGet] HcommChannelFenceOnThread failed, ret is %d", ret);
  if (param->remote_flag_addr != 0) {
    if (param->protocol != COMM_PROTOCOL_HCCS) {
      HIXL_LOGI("[HixlBatchPutAndGet] HcommReadOnThread start to read remote flag, flag_size=%u, "
                "local_flag=%lu, remote_flag=%lu",
                param->flag_size, param->local_flag_addr, param->remote_flag_addr);
      ret = HcommProxy::ReadOnThread(
          param->thread, param->channel, reinterpret_cast<void *>(static_cast<uintptr_t>(param->local_flag_addr)),
          reinterpret_cast<void *>(static_cast<uintptr_t>(param->remote_flag_addr)), param->flag_size);
      HIXL_CHK_BOOL_RET_STATUS(ret == 0, FAILED,
          "[HixlBatchPutAndGet] Remote flag read failed. dst:%lu, src:%lu, len:%u, ret=%d.",
          param->local_flag_addr, param->remote_flag_addr, param->flag_size, ret);
    } else {
      HIXL_LOGI(
          "[HixlBatchPutAndGet] aclrtNotifyRecordOnThread start to read remote flag, thread[%lu], notify_id[%u]",
          param->thread, param->notify_id);
      ret = HcommProxy::aclrtNotifyRecordOnThread(param->thread, param->notify_id);
      HIXL_CHK_BOOL_RET_STATUS(
          ret == 0, FAILED,
          "[HixlBatchPutAndGet] aclrtNotifyRecordOnThread start to read remote flag failed, thread[%lu], notify_id[%u]",
          param->thread, param->notify_id);
    }
  }


  ret = HcommProxy::BatchModeEnd(kBatchTag);
  HIXL_CHK_BOOL_RET_STATUS(ret == 0, FAILED, "[HixlBatchPutAndGet] HcommBatchModeEnd failed, ret is %d", ret);

  return SUCCESS;
}
}  // namespace
}  // namespace hixl
extern "C" {
uint32_t HixlBatchPut(HixlOneSideOpParam *param) {
  uint32_t ret = hixl::HixlBatchTransfer(false, param);
  if (ret != 0) {
    HIXL_LOGE(hixl::FAILED, "[HixlBatchPut] HixlBatchPut failed, ret is %u", ret);
    return hixl::FAILED;
  }
  return ret;
}

uint32_t HixlBatchGet(HixlOneSideOpParam *param) {
  uint32_t ret = hixl::HixlBatchTransfer(true, param);
  if (ret != 0) {
    HIXL_LOGE(hixl::FAILED, "[HixlBatchGet] HixlBatchGet failed, ret is %u", ret);
    return hixl::FAILED;
  }
  return ret;
}
}  // extern "C"
