/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <securec.h>
#include <mutex>
#include <map>
#include <string>
#include "base/err_msg.h"

using namespace error_message;

namespace error_message {

#ifdef __GNUC__
std::string TrimPath(const std::string &str) {
  if (str.find_last_of('/') != std::string::npos) {
    return str.substr(str.find_last_of('/') + 1U);
  }
  return str;
}
#else
std::string TrimPath(const std::string &str) {
  if (str.find_last_of('\\') != std::string::npos) {
    return str.substr(str.find_last_of('\\') + 1U);
  }
  return str;
}
#endif

int FormatErrorMessage(char *str_dst, size_t dst_max, const char *format, ...) {
  return 1;
}

int32_t RegisterFormatErrorMessage(const char_t *error_msg, size_t error_msg_len) {
  (void)error_msg_len;
  (void)error_msg;
  return 0;
}

struct StubErrorItem {
  std::string error_id;
  std::string error_title;
  std::string error_message;
  std::string possible_cause;
  std::string solution;
  std::map<std::string, std::string> args_map;
  std::string report_time;

  friend bool operator==(const StubErrorItem &lhs, const StubErrorItem &rhs) noexcept {
    return (lhs.error_id == rhs.error_id) && (lhs.error_message == rhs.error_message) &&
           (lhs.possible_cause == rhs.possible_cause) && (lhs.solution == rhs.solution);
  }
};

std::mutex stub_mutex_;
static std::vector<StubErrorItem> stub_error_message_process_;

int32_t ReportInnerErrMsg(const char *file_name, const char *func, uint32_t line, const char *error_code,
                          const char *format, ...) {
  (void)file_name;
  (void)func;
  (void)line;
  std::lock_guard<std::mutex> lock(stub_mutex_);
  StubErrorItem item = {error_code, "", format};
  stub_error_message_process_.emplace_back(item);
  return 0;
}

int32_t ReportUserDefinedErrMsg(const char *error_code, const char *format, ...) {
  (void)error_code;
  (void)format;
  return 0;
}

int32_t ReportPredefinedErrMsg(const char *error_code, const std::vector<const char *> &key,
                               const std::vector<const char *> &value) {
  (void)error_code;
  (void)key;
  (void)value;
  return 0;
}

int32_t ReportPredefinedErrMsg(const char *error_code) {
  (void)error_code;
  return 0;
}
}  // namespace error_message
