/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_COMMON_HIXL_LOG_H_
#define CANN_HIXL_SRC_HIXL_COMMON_HIXL_LOG_H_

#include <cinttypes>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include "dlog_pub.h"
#include "base/err_msg.h"
#ifdef __GNUC__
#include <unistd.h>
#include <sys/syscall.h>
#else
#include "mmpa/mmpa_api.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif
#define HIXL_MODULE_NAME static_cast<int32_t>(GE)
#define HIXL_GET_ERRORNO_STR(value) ge::StatusFactory::Instance()->GetErrDesc(value)
#define HIXL_GET_ERROR_LOG_HEADER "[HIXL]"

class HixlLog {
 public:
  static uint64_t GetTid() {
#ifdef __GNUC__
    auto tid = static_cast<uint64_t>(syscall(__NR_gettid));
#else
    auto tid = static_cast<uint64_t>(GetCurrentThreadId());
#endif
    return tid;
  }
};

inline bool HixlLogPrintStdout() {
  static int32_t stdout_flag = -1;
  if (stdout_flag == -1) {
    const char *env_ret = getenv("ASCEND_SLOG_PRINT_TO_STDOUT");
    if (env_ret != nullptr) {
      std::string env_str = env_ret;
      stdout_flag = env_str == "1" ? 1 : 0;
    }
  }
  return (stdout_flag == 1);
}

#define HIXL_LOGE(ERROR_CODE, fmt, ...)                                                                    \
  do {                                                                                                     \
    dlog_error(HIXL_MODULE_NAME, "%" PRIu64 " %s: ErrorNo: %" PRIuLEAST8 "(%s) %s" fmt, HixlLog::GetTid(), \
               &__FUNCTION__[0U], (ERROR_CODE), ((HIXL_GET_ERRORNO_STR(ERROR_CODE)).c_str()),              \
               HIXL_GET_ERROR_LOG_HEADER, ##__VA_ARGS__);                                                  \
  } while (false)

#define HIXL_LOGW(fmt, ...)                                                                                  \
  do {                                                                                                       \
    dlog_warn(HIXL_MODULE_NAME, "%" PRIu64 " %s:" fmt, HixlLog::GetTid(), &__FUNCTION__[0U], ##__VA_ARGS__); \
  } while (false)

#define HIXL_LOGI(fmt, ...)                                                                                  \
  do {                                                                                                       \
    dlog_info(HIXL_MODULE_NAME, "%" PRIu64 " %s:" fmt, HixlLog::GetTid(), &__FUNCTION__[0U], ##__VA_ARGS__); \
  } while (false)

#define HIXL_LOGD(fmt, ...)                                                                                   \
  do {                                                                                                        \
    dlog_debug(HIXL_MODULE_NAME, "%" PRIu64 " %s:" fmt, HixlLog::GetTid(), &__FUNCTION__[0U], ##__VA_ARGS__); \
  } while (false)

#define HIXL_EVENT(fmt, ...)                                                                                       \
  do {                                                                                                             \
    dlog_info(static_cast<int32_t>(static_cast<uint32_t>(RUN_LOG_MASK) | static_cast<uint32_t>(HIXL_MODULE_NAME)), \
              "%" PRIu64 " %s:" fmt, HixlLog::GetTid(), &__FUNCTION__[0U], ##__VA_ARGS__);                         \
    if (!HixlLogPrintStdout()) {                                                                                   \
      dlog_info(HIXL_MODULE_NAME, "%" PRIu64 " %s:" fmt, HixlLog::GetTid(), &__FUNCTION__[0U], ##__VA_ARGS__);     \
    }                                                                                                              \
  } while (false)

#ifdef __cplusplus
}
#endif

#endif  // CANN_HIXL_SRC_HIXL_COMMON_HIXL_LOG_H_
