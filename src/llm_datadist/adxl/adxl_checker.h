/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_ADXL_CHECKER_H
#define CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_ADXL_CHECKER_H

#include "adxl/adxl_types.h"
#include "hixl/hixl_types.h"
#include "adxl_utils.h"
#include "base/err_msg.h"
#include "common/llm_log.h"

// If expr is not SUCCESS, print the log and return the same value
#define ADXL_CHK_STATUS_RET(expr, ...)        \
  do {                                        \
    const adxl::Status _chk_status = (expr);  \
    if (_chk_status != adxl::SUCCESS) {       \
      if (adxl::NeedErrorLog(_chk_status)) {         \
        LLMLOGE((_chk_status), __VA_ARGS__);  \
      }                                     \
      return _chk_status;                     \
    }                                         \
  } while (false)

// If expr is not SUCCESS, print the log and do not execute return
#define ADXL_CHK_STATUS(expr, ...)            \
  do {                                        \
    const adxl::Status _chk_status = (expr);  \
    if (_chk_status != adxl::SUCCESS) {       \
      LLMLOGE(_chk_status, __VA_ARGS__);       \
    }                                         \
  } while (false)

// If expr is not true, print the log and return the specified status
#define ADXL_CHK_BOOL_RET_STATUS(expr, _status, ...) \
  do {                                               \
    const bool b = (expr);                           \
    if (!b) {                                        \
      REPORT_INNER_ERR_MSG("E19999", __VA_ARGS__);     \
      LLMLOGE((_status), __VA_ARGS__);                \
      return (_status);                              \
    }                                                \
  } while (false)

// If expr is true, print info log and return the specified status
#define ADXL_CHK_BOOL_RET_SPECIAL_STATUS(expr, _status, ...) \
  do {                                                       \
    const bool b = (expr);                                   \
    if (b) {                                                 \
      LLMLOGI(__VA_ARGS__);                                   \
      return (_status);                                      \
    }                                                        \
  } while (false)

// Check if the parameter is null. If yes, return PARAM_INVALID and record the error
#define ADXL_CHECK_NOTNULL(val, ...)                                                          \
  do {                                                                                        \
    if ((val) == nullptr) {                                                                   \
      REPORT_INNER_ERR_MSG("E19999", "Param:" #val " is nullptr, check invalid" __VA_ARGS__);   \
      LLMLOGE(adxl::PARAM_INVALID, "[Check][Param:" #val "]null is invalid" __VA_ARGS__);      \
      return adxl::PARAM_INVALID;                                                             \
    }                                                                                         \
  } while (false)

// If expr is not SUCCESS, print the log and return the adxl ret
#define ADXL_CHK_LLM_RET(expr, ...)                             \
  do {                                                          \
    const ge::Status _ret = (expr);                             \
    if (_ret != ge::SUCCESS) {                                  \
      REPORT_INNER_ERR_MSG("E19999", "Call %s fail, ret: 0x%X", #expr, static_cast<uint32_t>(_ret));  \
      const auto _adxl_ret = adxl::LLMError2AdxlStatus(_ret);   \
      LLMLOGE((_adxl_ret), __VA_ARGS__);                         \
      return _adxl_ret;                                         \
    }                                                           \
  } while (false)

// If expr is not 0, print the log and return
#define ADXL_CHK_HCCL_RET(expr)                                                                   \
  do {                                                                                            \
    const HcclResult _ret = (expr);                                                               \
    if (_ret != HCCL_SUCCESS) {                                                                   \
      REPORT_INNER_ERR_MSG("E19999", "Call %s fail, ret: 0x%X", #expr, static_cast<uint32_t>(_ret));  \
      const auto _adxl_ret = adxl::HcclError2AdxlStatus(_ret);                                    \
      LLMLOGE(_adxl_ret, "Call hccl api failed, ret: 0x%X", static_cast<uint32_t>(_ret));           \
      return _adxl_ret;                                                                           \
    }                                                                                             \
  } while (false)

// If expr is not 0, print the log and return
#define ADXL_CHK_ACL_RET(expr)                                                                    \
  do {                                                                                            \
    const aclError _ret = (expr);                                                                 \
    if (_ret != ACL_ERROR_NONE) {                                                                 \
      REPORT_INNER_ERR_MSG("E19999", "Call %s fail, ret: 0x%X", #expr, static_cast<uint32_t>(_ret));  \
      const auto _adxl_ret = adxl::AclError2AdxlStatus(_ret);                                     \
      LLMLOGE(_adxl_ret, "Call aclrt api failed, ret: 0x%X", static_cast<uint32_t>(_ret));          \
      return _adxl_ret;                                                                           \
    }                                                                                             \
  } while (false)

#endif  // CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_ADXL_CHECKER_H
