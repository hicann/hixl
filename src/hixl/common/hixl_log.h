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
#include <unistd.h>
#include <sys/syscall.h>
#include "dlog_pub.h"
#include "base/err_msg.h"

extern "C" {
__attribute__((weak)) int32_t acllogCheckDebugLevel(int32_t module_id, int32_t log_level);
__attribute__((weak)) void acllogRecord(int32_t module_id, int32_t level, const char *fmt, ...);
__attribute__((weak)) int32_t aclsysGetVersionNum(char *pkg_name, int32_t *version_num);
}

#ifdef __cplusplus
extern "C" {
#endif
#define HIXL_MODULE_NAME static_cast<int32_t>(GE)

class HixlLog {
 public:
  static uint64_t GetTid() {
    return static_cast<uint64_t>(syscall(__NR_gettid));
  }
};

inline bool HixlLogPrintStdout() {
  static const int32_t stdout_flag = []() {
    const char *env_ret = getenv("ASCEND_SLOG_PRINT_TO_STDOUT");
    return (env_ret != nullptr && std::string(env_ret) == "1") ? 1 : 0;
  }();
  return (stdout_flag == 1);
}

using HixlAclLogCheckLevelFunc = int32_t (*)(int32_t, int32_t);
using HixlAclLogRecordFunc = void (*)(int32_t, int32_t, const char *, ...);
using HixlAclGetVersionNumFunc = int32_t (*)(char *, int32_t *);

// 9.2.0 is encoded as 90200000 by aclsysGetVersionNum.
constexpr int32_t HIXL_ACL_RUN_LOG_VERSION_THRESHOLD = 90200000;

inline HixlAclLogCheckLevelFunc HixlGetAclLogCheckLevel() {
  return acllogCheckDebugLevel;
}

inline HixlAclLogRecordFunc HixlGetAclLogRecord() {
  return acllogRecord;
}

inline HixlAclLogCheckLevelFunc HixlResolveLogCheckLevel(HixlAclLogCheckLevelFunc acl_check_level) {
  return acl_check_level != nullptr ? acl_check_level : CheckLogLevel;
}

inline HixlAclLogCheckLevelFunc HixlGetLogCheckLevel() {
  static const HixlAclLogCheckLevelFunc check_level = HixlResolveLogCheckLevel(HixlGetAclLogCheckLevel());
  return check_level;
}

inline HixlAclLogRecordFunc HixlResolveLogRecord(HixlAclLogRecordFunc acl_log_record) {
  return acl_log_record != nullptr ? acl_log_record : DlogRecord;
}

inline HixlAclLogRecordFunc HixlGetLogRecord() {
  static const HixlAclLogRecordFunc log_record = HixlResolveLogRecord(HixlGetAclLogRecord());
  return log_record;
}

inline bool HixlIsAclRunLogSupported(HixlAclGetVersionNumFunc get_version_num) {
  if (get_version_num == nullptr) {
    return false;
  }
  char pkg_name[] = "runtime";
  int32_t version_num = 0;
  return (get_version_num(pkg_name, &version_num) == 0) && (version_num > HIXL_ACL_RUN_LOG_VERSION_THRESHOLD);
}

inline HixlAclLogRecordFunc HixlResolveRunLogRecord(HixlAclLogRecordFunc acl_log_record,
                                                    HixlAclGetVersionNumFunc get_version_num) {
  return ((acl_log_record != nullptr) && HixlIsAclRunLogSupported(get_version_num)) ? acl_log_record : DlogRecord;
}

inline HixlAclLogRecordFunc HixlGetRunLogRecord() {
  static const HixlAclLogRecordFunc run_log_record =
      HixlResolveRunLogRecord(HixlGetAclLogRecord(), aclsysGetVersionNum);
  return run_log_record;
}

inline bool HixlCheckLogLevel(const int32_t module_name, const int32_t log_level) {
  return HixlGetLogCheckLevel()(module_name, log_level) == 1;
}

#define HIXL_RECORD(MODULE, LEVEL, fmt, ...)                                                       \
  do {                                                                                             \
    HixlGetLogRecord()((MODULE), (LEVEL), "[%s:%d]" fmt, DLOG_FILE_NAME, __LINE__, ##__VA_ARGS__); \
  } while (false)

#define HIXL_LOGE(ERROR_CODE, fmt, ...)                                                               \
  do {                                                                                                \
    HIXL_RECORD(HIXL_MODULE_NAME, DLOG_ERROR, "[HIXL] %" PRIu64 " %s: ErrorNo: %" PRIuLEAST8 " " fmt, \
                HixlLog::GetTid(), &__FUNCTION__[0U], (ERROR_CODE), ##__VA_ARGS__);                   \
  } while (false)

#define HIXL_LOGW(fmt, ...)                                                                                        \
  do {                                                                                                             \
    if (HixlCheckLogLevel(HIXL_MODULE_NAME, DLOG_WARN)) {                                                          \
      HIXL_RECORD(HIXL_MODULE_NAME, DLOG_WARN, "[HIXL] %" PRIu64 " %s:" fmt, HixlLog::GetTid(), &__FUNCTION__[0U], \
                  ##__VA_ARGS__);                                                                                  \
    }                                                                                                              \
  } while (false)

#define HIXL_LOGI(fmt, ...)                                                                                        \
  do {                                                                                                             \
    if (HixlCheckLogLevel(HIXL_MODULE_NAME, DLOG_INFO)) {                                                          \
      HIXL_RECORD(HIXL_MODULE_NAME, DLOG_INFO, "[HIXL] %" PRIu64 " %s:" fmt, HixlLog::GetTid(), &__FUNCTION__[0U], \
                  ##__VA_ARGS__);                                                                                  \
    }                                                                                                              \
  } while (false)

#define HIXL_LOGD(fmt, ...)                                                                                         \
  do {                                                                                                              \
    if (HixlCheckLogLevel(HIXL_MODULE_NAME, DLOG_DEBUG)) {                                                          \
      HIXL_RECORD(HIXL_MODULE_NAME, DLOG_DEBUG, "[HIXL] %" PRIu64 " %s:" fmt, HixlLog::GetTid(), &__FUNCTION__[0U], \
                  ##__VA_ARGS__);                                                                                   \
    }                                                                                                               \
  } while (false)

#define HIXL_EVENT(fmt, ...)                                                                                       \
  do {                                                                                                             \
    const int32_t event_module =                                                                                   \
        static_cast<int32_t>(static_cast<uint32_t>(RUN_LOG_MASK) | static_cast<uint32_t>(HIXL_MODULE_NAME));       \
    HixlGetRunLogRecord()(event_module, DLOG_INFO, "[%s:%d][HIXL] %" PRIu64 " %s:" fmt, DLOG_FILE_NAME, __LINE__,  \
                          HixlLog::GetTid(), &__FUNCTION__[0U], ##__VA_ARGS__);                                    \
    if (!HixlLogPrintStdout() && HixlCheckLogLevel(HIXL_MODULE_NAME, DLOG_INFO)) {                                 \
      HIXL_RECORD(HIXL_MODULE_NAME, DLOG_INFO, "[HIXL] %" PRIu64 " %s:" fmt, HixlLog::GetTid(), &__FUNCTION__[0U], \
                  ##__VA_ARGS__);                                                                                  \
    }                                                                                                              \
  } while (false)

#ifdef __cplusplus
}
#endif

#endif  // CANN_HIXL_SRC_HIXL_COMMON_HIXL_LOG_H_
