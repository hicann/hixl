/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_LLM_LOG_H
#define CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_LLM_LOG_H

#include <cstdint>
#include <map>

// LLM_ERROR_CODES has been defined in metadef, that will cause can't find the info in llm_error_codes.h
#include "llm_datadist/llm_error_codes.h"
#include "base/err_msg.h"
#include "acl/acl.h"
#include "hixl/hixl_types.h"
#include "dlog_pub.h"
#include "../../hixl/common/hixl_log.h"

#ifdef __cplusplus
extern "C" {
#endif
#define LLM_MODULE_NAME HIXL_MODULE_NAME
#define LLM_MODULE_NAME_U16 static_cast<int32_t>(GE)

using LlmLog = HixlLog;

inline bool LlmIsLogEnable(const int32_t module_name, const int32_t log_level) {
  return HixlCheckLogLevel(module_name, log_level);
}

#define LLM_RECORD(MODULE, LEVEL, fmt, ...) HIXL_RECORD((MODULE), (LEVEL), fmt, ##__VA_ARGS__)
#define LLMLOGE(ERROR_CODE, fmt, ...) HIXL_LOGE((ERROR_CODE), fmt, ##__VA_ARGS__)
#define LLMLOGW(fmt, ...) HIXL_LOGW(fmt, ##__VA_ARGS__)
#define LLMLOGI(fmt, ...) HIXL_LOGI(fmt, ##__VA_ARGS__)
#define LLMLOGD(fmt, ...) HIXL_LOGD(fmt, ##__VA_ARGS__)
#define LLMEVENT(fmt, ...) HIXL_EVENT(fmt, ##__VA_ARGS__)

#define LLM_LOGE_IF(condition, ...)     \
  if ((condition)) {                    \
    LLMLOGE((ge::FAILED), __VA_ARGS__); \
  }

// If expr is not SUCCESS, print the log and return the same value
#define LLM_CHK_STATUS_RET(expr, ...)                                      \
  do {                                                                     \
    const ge::Status _chk_status = (expr);                                 \
    if (_chk_status != ge::SUCCESS) {                                      \
      REPORT_INNER_ERR_MSG("E19999", "Call " #expr " fail. " __VA_ARGS__); \
      LLMLOGE((ge::FAILED), __VA_ARGS__);                                  \
      return _chk_status;                                                  \
    }                                                                      \
  } while (false)

// If expr is not SUCCESS, print the log and do not execute return
#define LLM_CHK_STATUS(expr, ...)                                          \
  do {                                                                     \
    const ge::Status _chk_status = (expr);                                 \
    if (_chk_status != ge::SUCCESS) {                                      \
      REPORT_INNER_ERR_MSG("E19999", "Call " #expr " fail. " __VA_ARGS__); \
      LLMLOGE(_chk_status, __VA_ARGS__);                                   \
    }                                                                      \
  } while (false)

// If expr is not SUCCESS, return the same value
#define LLM_CHK_STATUS_RET_NOLOG(expr)     \
  do {                                     \
    const ge::Status _chk_status = (expr); \
    if (_chk_status != ge::SUCCESS) {      \
      return _chk_status;                  \
    }                                      \
  } while (false)

// If expr is not true, print the log and return the specified status
#define LLM_CHK_BOOL_RET_STATUS(expr, _status, ...) \
  do {                                              \
    const bool b = (expr);                          \
    if (!b) {                                       \
      REPORT_INNER_ERR_MSG("E19999", __VA_ARGS__);  \
      LLMLOGE((_status), __VA_ARGS__);              \
      return (_status);                             \
    }                                               \
  } while (false)

// If expr is true, print info log and return the specified status
#define LLM_CHK_BOOL_RET_SPECIAL_STATUS(expr, _status, ...) \
  do {                                                      \
    const bool b = (expr);                                  \
    if (b) {                                                \
      LLMLOGI(__VA_ARGS__);                                 \
      return (_status);                                     \
    }                                                       \
  } while (false)

// If expr is not true, print the log and return the specified status
#define LLM_CHK_BOOL_RET_STATUS_NOLOG(expr, _status, ...) \
  do {                                                    \
    const bool b = (expr);                                \
    if (!b) {                                             \
      return (_status);                                   \
    }                                                     \
  } while (false)

// If expr is not true, print the log and execute a custom statement
#define LLM_CHK_BOOL_EXEC(expr, exec_expr, ...) \
  {                                             \
    const bool b = (expr);                      \
    if (!b) {                                   \
      LLMLOGE(ge::FAILED, __VA_ARGS__);         \
      exec_expr;                                \
    }                                           \
  }

// Check if the parameter is null. If yes, return PARAM_INVALID and record the error
#define LLM_CHECK_NOTNULL(val, ...)                                                           \
  do {                                                                                        \
    if ((val) == nullptr) {                                                                   \
      REPORT_INNER_ERR_MSG("E19999", "Param:" #val " is nullptr, check invalid" __VA_ARGS__); \
      LLMLOGE(ge::FAILED, "[Check][Param:" #val "]null is invalid" __VA_ARGS__);              \
      return ge::LLM_PARAM_INVALID;                                                           \
    }                                                                                         \
  } while (false)

// Check if the value on the left is greater than or equal to the value on the right
#define LLM_CHECK_GE(lhs, rhs)                                                                           \
  do {                                                                                                   \
    if ((lhs) < (rhs)) {                                                                                 \
      LLMLOGE(ge::FAILED, "param[%s][%ld] is less than[%s][%ld]", #lhs, static_cast<int64_t>(lhs), #rhs, \
              static_cast<int64_t>(rhs));                                                                \
      return ge::LLM_PARAM_INVALID;                                                                      \
    }                                                                                                    \
  } while (false)

// Check if the value on the left is less than or equal to the value on the right
#define LLM_CHECK_LE(lhs, rhs)                                                                              \
  do {                                                                                                      \
    if ((lhs) > (rhs)) {                                                                                    \
      LLMLOGE(ge::FAILED, "param[%s][%ld] is greater than[%s][%ld]", #lhs, static_cast<int64_t>(lhs), #rhs, \
              static_cast<int64_t>(rhs));                                                                   \
      return ge::LLM_PARAM_INVALID;                                                                         \
    }                                                                                                       \
  } while (false)

#ifdef __cplusplus
}
#endif

namespace llm {
inline ge::Status ConvertAclError2Ge(int32_t ret) {
  const static std::map<int32_t, ge::Status> acl_to_ge_status = {
      {static_cast<int32_t>(ACL_ERROR_RT_STREAM_SYNC_TIMEOUT), ge::LLM_TIMEOUT}};
  const auto &it = acl_to_ge_status.find(ret);
  if (it != acl_to_ge_status.cend()) {
    return it->second;
  }
  return static_cast<ge::Status>(ret);
}

inline ge::Status ConvertHixlError2Ge(uint32_t ret) {
  const static std::map<uint32_t, ge::Status> hixl_to_ge_status = {{hixl::PARAM_INVALID, ge::LLM_PARAM_INVALID},
                                                                   {hixl::TIMEOUT, ge::LLM_TIMEOUT},
                                                                   {hixl::NOT_CONNECTED, ge::LLM_NOT_YET_LINK},
                                                                   {hixl::ALREADY_CONNECTED, ge::LLM_ALREADY_LINK},
                                                                   {hixl::FAILED, ge::FAILED},
                                                                   {hixl::UNSUPPORTED, ge::LLM_FEATURE_NOT_ENABLED}};
  const auto &it = hixl_to_ge_status.find(ret);
  if (it != hixl_to_ge_status.cend()) {
    return it->second;
  }
  return ge::FAILED;
}

// If expr is not 0, print the log and return
#define LLM_CHK_ACL_RET(expr)                                                                        \
  do {                                                                                               \
    const aclError _ret = (expr);                                                                    \
    if (_ret != ACL_ERROR_NONE) {                                                                    \
      REPORT_INNER_ERR_MSG("E19999", "Call %s fail, ret: 0x%X", #expr, static_cast<uint32_t>(_ret)); \
      LLMLOGE(ge::FAILED, "Call aclrt api failed, ret: 0x%X", static_cast<uint32_t>(_ret));          \
      return llm::ConvertAclError2Ge(static_cast<int32_t>(_ret));                                    \
    }                                                                                                \
  } while (false)
}  // namespace llm

#define LLM_CHK_ACL(expr)                                                                               \
  do {                                                                                                  \
    const aclError _rt_err = (expr);                                                                    \
    if (_rt_err != ACL_ERROR_NONE) {                                                                    \
      REPORT_INNER_ERR_MSG("E19999", "Call %s fail, ret: 0x%X", #expr, static_cast<uint32_t>(_rt_err)); \
      LLMLOGE(ge::FAILED, "Call aclrt api failed, ret: 0x%X", static_cast<uint32_t>(_rt_err));          \
    }                                                                                                   \
  } while (false)

// If expr is not 0, print the log and return
#define LLM_CHK_HIXL_RET(expr, ...)                                         \
  do {                                                                      \
    const auto _ret = (expr);                                               \
    if (_ret != hixl::SUCCESS) {                                            \
      REPORT_INNER_ERR_MSG("E19999", "Call %s fail, ret:%u.", #expr, _ret); \
      auto _status = llm::ConvertHixlError2Ge(static_cast<int32_t>(_ret));  \
      LLMLOGE(_status, __VA_ARGS__);                                        \
      return _status;                                                       \
    }                                                                       \
  } while (false)

#define LLM_RT_ERROR_TO_GE_STATUS(RT_ERROR) static_cast<ge::Status>(RT_ERROR)
// If expr is not ACL_ERROR_NONE, print the log and return
#define LLM_CHK_RT_RET(expr)                                                                            \
  do {                                                                                                  \
    const aclError _rt_ret = (expr);                                                                    \
    if (_rt_ret != ACL_ERROR_NONE) {                                                                    \
      REPORT_INNER_ERR_MSG("E19999", "Call %s fail, ret: 0x%X", #expr, static_cast<uint32_t>(_rt_ret)); \
      LLMLOGE(ge::FAILED, "Call aclrt api failed, ret: 0x%X", static_cast<uint32_t>(_rt_ret));          \
      return LLM_RT_ERROR_TO_GE_STATUS(_rt_ret);                                                        \
    }                                                                                                   \
  } while (false)

#endif  // CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_LLM_LOG_H
