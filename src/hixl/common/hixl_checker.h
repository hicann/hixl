/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_COMMON_HIXL_CHECKER_H_
#define CANN_HIXL_SRC_HIXL_COMMON_HIXL_CHECKER_H_

#include "hixl/hixl_types.h"
#include "base/err_msg.h"
#include "acl/acl.h"
#include "hixl_log.h"

inline hixl::Status ConvertAclRetToStatus(int32_t acl_ret) {
  if (acl_ret == ACL_SUCCESS) {
    return hixl::SUCCESS;
  }

  switch (acl_ret) {
    case ACL_ERROR_INVALID_PARAM:
    case ACL_ERROR_INVALID_FILE:
    case ACL_ERROR_INVALID_RESOURCE_HANDLE:
    case ACL_ERROR_INVALID_DEVICE:
    case ACL_ERROR_RESOURCE_NOT_MATCH:
      return hixl::PARAM_INVALID;

    case ACL_ERROR_BAD_ALLOC:
    case ACL_ERROR_STORAGE_OVER_LIMIT:
      return hixl::RESOURCE_EXHAUSTED;

    case ACL_ERROR_WAIT_CALLBACK_TIMEOUT:
    case 507011:  // ACL_ERROR_RT_STREAM_SYNC_TIMEOUT (常见值，视具体版本而定)
      return hixl::TIMEOUT;

    case ACL_ERROR_API_NOT_SUPPORT:
    case ACL_ERROR_FEATURE_UNSUPPORTED:
      return hixl::UNSUPPORTED;

    default:
      return hixl::FAILED;
  }
}

// If expr is not SUCCESS, print the log and return the same value
#define HIXL_CHK_STATUS_RET(expr, ...)       \
  do {                                       \
    const hixl::Status _chk_status = (expr); \
    if (_chk_status != hixl::SUCCESS) {      \
      REPORT_INNER_ERR_MSG("E19999", "Call " #expr " fail. " __VA_ARGS__); \
      HIXL_LOGE((_chk_status), __VA_ARGS__); \
      return _chk_status;                    \
    }                                        \
  } while (false)

// If expr is not SUCCESS, print the log and do not execute return
#define HIXL_CHK_STATUS(expr, ...)           \
  do {                                       \
    const hixl::Status _chk_status = (expr); \
    if (_chk_status != hixl::SUCCESS) {      \
      REPORT_INNER_ERR_MSG("E19999", "Call " #expr " fail. " __VA_ARGS__); \
      HIXL_LOGE(_chk_status, __VA_ARGS__);   \
    }                                        \
  } while (false)

// If expr is not true, print the log and return the specified status
#define HIXL_CHK_BOOL_RET_STATUS(expr, _status, ...) \
  do {                                               \
    const bool b = (expr);                           \
    if (!b) {                                        \
      REPORT_INNER_ERR_MSG("E19999", __VA_ARGS__);   \
      HIXL_LOGE((_status), __VA_ARGS__);             \
      return (_status);                              \
    }                                                \
  } while (false)

// If expr is true, print info log and return the specified status
#define HIXL_CHK_BOOL_RET_SPECIAL_STATUS(expr, _status, ...) \
  do {                                             \
    const bool b = (expr);                         \
    if (b) {                                      \
      HIXL_LOGI(__VA_ARGS__);              \
      return (_status);                            \
    }                                              \
  } while (false)

// Check if the parameter is null. If yes, return PARAM_INVALID and record the error
#define HIXL_CHECK_NOTNULL(val, ...)                                                          \
  do {                                                                                        \
    if ((val) == nullptr) {                                                                   \
      REPORT_INNER_ERR_MSG("E19999", "Param:" #val " is nullptr, check invalid" __VA_ARGS__); \
      HIXL_LOGE(hixl::PARAM_INVALID, "[Check][Param:" #val "]null is invalid" __VA_ARGS__);   \
      return hixl::PARAM_INVALID;                                                             \
    }                                                                                         \
  } while (false)

// If expr is not 0, print the log and return
#define HIXL_CHK_HCCL_RET(expr)                                                                      \
  do {                                                                                               \
    const HcclResult _ret = (expr);                                                                  \
    if (_ret != HCCL_SUCCESS) {                                                                      \
      REPORT_INNER_ERR_MSG("E19999", "Call %s fail, ret: 0x%X", #expr, static_cast<uint32_t>(_ret)); \
      const auto _hixl_ret = hixl::HcclError2Status(_ret);                                           \
      HIXL_LOGE(_hixl_ret, "Call hccl api:%s failed, ret: 0x%X", #expr, static_cast<uint32_t>(_ret));          \
      return _hixl_ret;                                                                              \
    }                                                                                                \
  } while (false)

#define HIXL_CHK_HCCL(expr)                                                                          \
  do {                                                                                               \
    const HcclResult _ret = (expr);                                                                  \
    if (_ret != HCCL_SUCCESS) {                                                                      \
      REPORT_INNER_ERR_MSG("E19999", "Call %s fail, ret: 0x%X", #expr, static_cast<uint32_t>(_ret)); \
      const auto _hixl_ret = hixl::HcclError2Status(_ret);                                           \
      HIXL_LOGE(_hixl_ret, "Call hccl api:%s failed, ret: 0x%X", #expr, static_cast<uint32_t>(_ret));          \
    }                                                                                                \
  } while (false)

// If expr != ACL_SUCCESS, log and return the converted Hixl Status
#define HIXL_CHK_ACL_RET(expr, ...)                                                                      \
  do {                                                                                                   \
    const aclError _acl_ret = (expr);                                                                    \
    if (_acl_ret != ACL_SUCCESS) {                                                                       \
      hixl::Status _hixl_status = ConvertAclRetToStatus(static_cast<int32_t>(_acl_ret));                 \
      REPORT_INNER_ERR_MSG("E19999", "Call %s fail, ret: 0x%X", #expr, static_cast<uint32_t>(_acl_ret)); \
      HIXL_LOGE(_hixl_status, "Call acl api:%s failed, ret: 0x%X. " __VA_ARGS__, #expr,                  \
                static_cast<uint32_t>(_acl_ret));                                                        \
      return _hixl_status;                                                                               \
    }                                                                                                    \
  } while (false)

// If expr != ACL_SUCCESS, print the log and do not return
#define HIXL_CHK_ACL(expr, ...)                                                                       \
  do {                                                                                                \
    const aclError _ret = (expr);                                                                     \
    if (_ret != ACL_SUCCESS) {                                                                        \
      REPORT_INNER_ERR_MSG("E19999", "Call %s fail, ret: 0x%X", #expr, static_cast<uint32_t>(_ret));  \
      HIXL_LOGE(FAILED, "Call acl api failed, ret: 0x%X. " __VA_ARGS__, static_cast<uint32_t>(_ret)); \
    }                                                                                                 \
  } while (false)


#endif  // CANN_HIXL_SRC_HIXL_COMMON_HIXL_CHECKER_H_
