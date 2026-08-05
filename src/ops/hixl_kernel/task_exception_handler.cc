/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#include "task_exception_handler.h"

#include "common/hixl_log.h"
#include "hixl/hixl_types.h"
#include "hcomm_proxy.h"
#include "transfer_context_manager.h"
#include "ascend_hal.h"
#include "aicpu_schedule/aicpu_context.h"
#include "tsch/aicpu_msg.h"
#include "tsch/task_scheduler_error.h"
#include "common/scope_guard.h"
#include <sstream>
#include <string>

namespace hixl {
namespace {
char g_hixl_ex_cb_user_data[] = "HIXL";

// notify record type: 1=event record, 2=notify record.
constexpr uint8_t kAicpuMsgNotifyRecord = 2U;

Status NotifyTsfwTaskException(uint32_t notify_id, uint32_t error_code) {
  if (halEschedSubmitEvent == nullptr) {
    HIXL_LOGE(FAILED, "[TaskExceptionHandler] halEschedSubmitEvent is null, API unavailable");
    return FAILED;
  }
  aicpu::aicpuContext_t ctx = {};
  if (aicpu::aicpuGetContext == nullptr) {
    HIXL_LOGE(FAILED, "[TaskExceptionHandler] aicpuGetContext is null, API unavailable");
    return FAILED;
  }
  aicpu::status_t ctx_ret = aicpu::aicpuGetContext(&ctx);
  if (ctx_ret != aicpu::AICPU_ERROR_NONE) {
    HIXL_LOGE(FAILED, "[TaskExceptionHandler] aicpuGetContext failed, ret=%d", ctx_ret);
    return FAILED;
  }
  // 透传 HComm 错误码会导致 runtime 打印错误码描述混乱，暂时统一上报 TS_ERROR_HCCL_OTHER_ERROR
  constexpr uint16_t report_err_code = static_cast<uint16_t>(TS_ERROR_HCCL_OTHER_ERROR);
  ts_aicpu_msg_info_t aicpu_msg = {};
  aicpu_msg.pid = static_cast<uint32_t>(ctx.hostPid);
  aicpu_msg.cmd_type = TS_AICPU_RECORD;
  aicpu_msg.vf_id = static_cast<uint8_t>(ctx.vfId);
  aicpu_msg.tid = 0U;
  aicpu_msg.ts_id = static_cast<uint8_t>(ctx.tsId);
  aicpu_msg.u.aicpu_record.record_type = kAicpuMsgNotifyRecord;
  aicpu_msg.u.aicpu_record.record_id = notify_id;
  aicpu_msg.u.aicpu_record.ret_code = report_err_code;
  event_summary event = {};
  event.dst_engine = TS_CPU;
  event.policy = ONLY;
  event.pid = 0;
  event.grp_id = 0;
  event.event_id = EVENT_TS_CTRL_MSG;
  event.subevent_id = 0U;
  event.msg_len = static_cast<uint32_t>(sizeof(ts_aicpu_msg_info_t));
  event.msg = reinterpret_cast<char *>(&aicpu_msg);

  drvError_t ret = halEschedSubmitEvent(ctx.deviceId, &event);
  if (ret != DRV_ERROR_NONE) {
    HIXL_LOGE(FAILED, "[TaskExceptionHandler] halEschedSubmitEvent failed, ret=%d, notifyId=%u", ret, notify_id);
    return FAILED;
  }
  HIXL_LOGI("[TaskExceptionHandler] Submit to TSFW success. deviceId=%u, notifyId=%u, errCode=%u, reportErrCode=%u",
            ctx.deviceId, notify_id, error_code, report_err_code);
  return SUCCESS;
}

const char *ExpandTypeToString(HcommExceptionExpandType type) {
  switch (type) {
    case HCOMM_EXCEPTION_STARS:
      return "STARS";
    case HCOMM_EXCEPTION_ROCE:
      return "ROCE";
    case HCOMM_EXCEPTION_URMA:
      return "URMA";
    default:
      return "INVALID";
  }
}

std::string HcommExceptionInfoToString(const HcommExceptionInfo *info) {
  std::ostringstream oss;
  oss << "HcommExceptionInfo{taskId=" << info->taskId << ", thread=" << info->thread << ", channel=" << info->channel
      << ", retCode=" << info->retCode << ", expandType=" << ExpandTypeToString(info->expandInfo.type);
  if (info->expandInfo.type == HCOMM_EXCEPTION_STARS) {
    const auto &s = info->expandInfo.detail.starsInfo;
    oss << ", sqeType=" << static_cast<uint32_t>(s.sqeType)
        << ", statusMerged=" << static_cast<uint32_t>(s.statusMerged) << ", starsErrcode=" << s.starsErrcode;
  }
  oss << "}";
  return oss.str();
}

void HixlTaskExceptionCallback(const HcommExceptionInfo *exception_info, void *user_data) {
  if (exception_info == nullptr) {
    HIXL_LOGE(FAILED, "[TaskExceptionHandler] Exception callback received null exception_info, user_data=%p",
              user_data);
    return;
  }
  uint64_t thread_handle = exception_info->thread;
  uint32_t error_code = exception_info->retCode;
  std::string info_str = HcommExceptionInfoToString(exception_info);

  auto ctx = TransferContextManager::Instance().Get(thread_handle);
  if (ctx == nullptr) {
    HIXL_LOGI("[TaskExceptionHandler] Thread %lu not found, maybe not HIXL task or thread is deleted. user_data=%p. %s",
              thread_handle, user_data, info_str.c_str());
    return;
  }

  HIXL_LOGE(FAILED, "[TaskExceptionHandler] Thread %lu task exception. user_data=%p. %s", thread_handle, user_data,
            info_str.c_str());

  uint32_t notify_id = 0;
  {
    ctx->lock();
    ScopeGuard ctx_lock([&ctx] { ctx->unlock(); });
    if (ctx->GetState() != TRANSFER_THREAD_STATE_INITIALIZED) {
      HIXL_LOGE(FAILED, "[TaskExceptionHandler] Thread %lu state is not INITIALIZED, state=%u, skip processing",
                thread_handle, static_cast<uint32_t>(ctx->GetState()));
      return;
    }
    ctx->WriteErrorFlag();
    notify_id = ctx->notify_id;
  }

  Status notify_ret = NotifyTsfwTaskException(notify_id, error_code);
  if (notify_ret != SUCCESS) {
    HIXL_LOGE(FAILED, "[TaskExceptionHandler] Thread %lu NotifyTsfwTaskException failed, ret=%u", thread_handle,
              notify_ret);
  }
}

}  // namespace

TaskExceptionHandler &TaskExceptionHandler::Instance() {
  static TaskExceptionHandler handler;
  return handler;
}

void TaskExceptionHandler::EnableExceptionCallback() {
  if (callback_registered_.load()) {
    return;
  }
  HIXL_LOGI("[TaskExceptionHandler] Registering callback with user_data=%p",
            static_cast<const void *>(g_hixl_ex_cb_user_data));
  const int32_t ret = HcommProxy::RegisterExceptionCallback(HixlTaskExceptionCallback, g_hixl_ex_cb_user_data);
  if (ret == 0) {
    callback_registered_.store(true);
    HIXL_LOGI("[TaskExceptionHandler] Register callback success");
    return;
  }
  if (ret == static_cast<int32_t>(HCCL_E_NOT_SUPPORT)) {
    HIXL_LOGI("[TaskExceptionHandler] HcommExceptionRegisterCallback unsupported, ret=%d", ret);
  } else {
    HIXL_LOGE(FAILED, "[TaskExceptionHandler] HcommExceptionRegisterCallback failed, ret=%d", ret);
  }
}

void TaskExceptionHandler::DisableExceptionCallback() {
  if (!callback_registered_.load()) {
    return;
  }
  HIXL_LOGI("[TaskExceptionHandler] Unregistering callback");
  int32_t ret = HcommProxy::UnregisterExceptionCallback(HixlTaskExceptionCallback);
  if (ret == 0) {
    callback_registered_.store(false);
    HIXL_LOGI("[TaskExceptionHandler] Unregister callback success");
    return;
  }
  if (ret == static_cast<int32_t>(HCCL_E_NOT_SUPPORT)) {
    HIXL_LOGI("[TaskExceptionHandler] HcommExceptionUnregisterCallback unsupported, ret=%d", ret);
  } else {
    HIXL_LOGE(FAILED, "[TaskExceptionHandler] HcommExceptionUnregisterCallback failed, ret=%d", ret);
  }
}

}  // namespace hixl
