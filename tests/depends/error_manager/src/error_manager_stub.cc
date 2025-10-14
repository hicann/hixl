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
#include "error_manager.h"
#include <mutex>
#include <map>
#include "base/err_msg.h"

using namespace error_message;
namespace {
std::unique_ptr<error_message::char_t[]> CreateUniquePtrFromString(const std::string &str) {
  const size_t buf_size = str.empty() ? 1 : (str.size() + 1);
  auto uni_ptr = std::unique_ptr<error_message::char_t[]>(new (std::nothrow) error_message::char_t[buf_size]);
  if (uni_ptr == nullptr) {
    return nullptr;
  }

  if (str.empty()) {
    uni_ptr[0U] = '\0';
  } else {
    // 当src size < dst size时，strncpy_s会在末尾str.size()位置添加'\0'
    if (strncpy_s(uni_ptr.get(), str.size() + 1, str.c_str(), str.size()) != EOK) {
      return nullptr;
    }
  }
  return uni_ptr;
}
}  // namespace
thread_local Context ErrorManager::error_context_ = {0, "", "", ""};

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

void ReportInnerError(const char_t *file_name, const char_t *func, uint32_t line, const std::string error_code,
                      const char_t *format, ...) {
  ErrorManager::GetInstance().ReportInterErrMessage(error_code, format);
  return;
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

int32_t ErrMgrInit(ErrorMessageMode error_mode) {
  std::lock_guard<std::mutex> lock(stub_mutex_);
  stub_error_message_process_.clear();
  return 0;
}
static ErrorManagerContext error_manager_context_;

ErrorManagerContext GetErrMgrContext() {
  return error_manager_context_;
}

void SetErrMgrContext(ErrorManagerContext error_context) {
  error_manager_context_ = error_context;
}

unique_const_char_array GetErrMgrErrorMessage() {
  std::lock_guard<std::mutex> lock(stub_mutex_);
  if (!stub_error_message_process_.empty()) {
    return CreateUniquePtrFromString(stub_error_message_process_.front().error_message);
  } else {
    return CreateUniquePtrFromString("");
  }
}

unique_const_char_array GetErrMgrWarningMessage() {
  return CreateUniquePtrFromString("");
}
}  // namespace error_message

ErrorManager &ErrorManager::GetInstance() {
  static ErrorManager instance;
  return instance;
}

int ErrorManager::Init() {
  std::lock_guard<std::mutex> lock(mutex_);
  error_message_process_.clear();
  return 0;
}

/// @brief init
/// @param [in] path: current so path
/// @return int 0(success) -1(fail)
int ErrorManager::Init(std::string path) {
  return 0;
}

/// @brief Report error message
/// @param [in] error_code: error code
/// @param [in] args_map: parameter map
/// @return int 0(success) -1(fail)
int ErrorManager::ReportErrMessage(std::string error_code, const std::map<std::string, std::string> &args_map) {
  return 0;
}

std::string ErrorManager::GetErrorMessage() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!error_message_process_.empty()) {
    return error_message_process_.front().error_message;
  } else {
    return std::string();
  }
}

std::vector<ErrorItem> ErrorManager::GetRawErrorMessages() {
  return {};
}

std::string ErrorManager::GetWarningMessage() {
  return std::string();
}

int ErrorManager::ReportInterErrMessage(std::string error_code, const std::string &error_msg) {
  std::lock_guard<std::mutex> lock(mutex_);
  ErrorItem item = {error_code, "", error_msg};
  error_message_process_.emplace_back(item);
  return 0;
}

/// @brief output error message
/// @param [in] handle: print handle
/// @return int 0(success) -1(fail)
int ErrorManager::OutputErrMessage(int handle) {
  return 0;
}

/// @brief output  message
/// @param [in] handle: print handle
/// @return int 0(success) -1(fail)
int ErrorManager::OutputMessage(int handle) {
  return 0;
}

/// @brief Report error message
/// @param [in] key: vector parameter key
/// @param [in] value: vector parameter value
void ErrorManager::ATCReportErrMessage(std::string error_code, const std::vector<std::string> &key,
                                       const std::vector<std::string> &value) {}

/// @brief report graph compile failed message such as error code and op_name in mstune case
/// @param [in] msg: failed message map, key is error code, value is op_name
/// @return int 0(success) -1(fail)
int ErrorManager::ReportMstuneCompileFailedMsg(const std::string &root_graph_name,
                                               const std::map<std::string, std::string> &msg) {
  return 0;
}

/// @brief get graph compile failed message in mstune case
/// @param [in] graph_name: graph name
/// @param [out] msg_map: failed message map, key is error code, value is op_name list
/// @return int 0(success) -1(fail)
int ErrorManager::GetMstuneCompileFailedMsg(const std::string &graph_name,
                                            std::map<std::string, std::vector<std::string>> &msg_map) {
  return 0;
}

void ErrorManager::GenWorkStreamIdDefault() {}

void ErrorManager::GenWorkStreamIdBySessionGraph(uint64_t session_id, uint64_t graph_id) {}

const std::string &ErrorManager::GetLogHeader() {
  return error_context_.log_header;
}

struct error_message::Context &ErrorManager::GetErrorManagerContext() {
  static struct error_message::Context error_context;
  return error_context;
}

void ErrorManager::SetErrorContext(struct error_message::Context error_context) {}

void ErrorManager::SetStage(const std::string &first_stage, const std::string &second_stage) {}
