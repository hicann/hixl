/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "slog_stub.h"
#include "dlog_pub.h"
#include "plog.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <iostream>

namespace llm {
int ge_log_level = DLOG_ERROR;
auto ins = llm::SlogStub::GetInstance();  // 让log提前初始化
class DefaultSlogStub : public SlogStub {
 public:
  DefaultSlogStub() : SlogStub() {
    auto log_level = getenv("ASCEND_GLOBAL_LOG_LEVEL");
    if (log_level != nullptr) {
      SetLevel(atoi(log_level));
    } else {
      SetLevel(DLOG_ERROR);
    }
    auto log_event_level = getenv("ASCEND_GLOBAL_EVENT_ENABLE");
    if (log_event_level != nullptr) {
      SetEventLevel(atoi(log_event_level));
    }
  }

  void Log(int module_id, int level, const char *fmt, va_list args) override {
    if ((!log_init) || (level < GetLevel())) {
      return;
    }
    char fmt_buff[1536] = {0};
    if (Format(fmt_buff, sizeof(fmt_buff), module_id, level, fmt, args) > 0) {
      printf("%s \n", fmt_buff);
    }
  }
};

SlogStub::~SlogStub() {
  log_init = false;
  ge_log_level = DLOG_ERROR;
}

void SlogStub::SetEventLevel(int event_level) {
  event_log_level_ = event_level;
}
void SlogStub::SetLevel(int level) {
  log_level_ = level;
  ge_log_level = level;
}

std::shared_ptr<SlogStub> stub_ins = nullptr;
SlogStub *SlogStub::GetInstance() {
  static DefaultSlogStub stub;
  if (stub_ins != nullptr) {
    return stub_ins.get();
  }
  return &stub;
}
void SlogStub::SetInstance(std::shared_ptr<SlogStub> stub) {
  stub_ins = std::move(stub);
}
int EraseFolderFromPath(char *buff, int len) {
  int i = 0;
  // 跳过第一组[LogLevel]，下面一组方框就是[/path/to/file]了
  while (i < len && buff[i] != ' ') {
    ++i;
  }

  int first_pos = -1;
  int last_pos = -1;
  for (; i < len; ++i) {
    switch (buff[i]) {
      case '[':
        first_pos = i;
        break;
      case '/':
        last_pos = i;
        break;
      case ']':
        i += len;  // 跳出循环了
        break;
      default:
        break;
    }
  }

  if (first_pos < 0 || last_pos < 0 || last_pos < first_pos) {
    return len;
  }
  len -= (last_pos - first_pos);
  // memcpy不支持overlap，因此这里不可以使用memcpy
  while (buff[last_pos] != '\0') {
    buff[++first_pos] = buff[++last_pos];
  }
  return len;
}
int SlogStub::Format(char *buff, size_t buff_len, int module_id, int level, const char *fmt, va_list args) {
  struct timeval ts;
  gettimeofday(&ts, 0);
  time_t t = time(NULL);
  struct tm *lt = localtime(&t);

  auto pos =
      snprintf(buff, buff_len, "%s %s(%lu,ut):%04d-%02d-%02d-%02d:%02d:%02d.%03d.%03d ", GetLevelStr(level),
               GetModuleIdStr(module_id), getpid(), (lt->tm_year + 1900), (lt->tm_mon + 1), lt->tm_mday, lt->tm_hour,
               lt->tm_min, lt->tm_sec, static_cast<int>(ts.tv_usec / 1000), static_cast<int>(ts.tv_usec % 1000));
  if (pos < 0) {
    return pos;
  }

  auto len = vsnprintf(buff + pos, buff_len - pos, fmt, args);
  if (len < 0) {
    return len;
  }
  pos += len;
  // 按照原来的实现，这里有一个裁掉目录，仅保存文件名的步骤，原地打印后，没法使用原来的机制了，所以重写一个。。。
  return EraseFolderFromPath(buff, pos);
}

LogCaptureStub::LogCaptureStub() {}

LogCaptureStub::~LogCaptureStub() {}

void LogCaptureStub::Log(int module_id, int level, const char *fmt, va_list args) {
  if (!log_init) {
    return;
  }
  char buff[2048] = {0};
  if (Format(buff, sizeof(buff), module_id, level, fmt, args) > 0) {
    std::string log_msg(buff);
    bool any_pattern_captured = false;
    {
      std::lock_guard<std::mutex> lock(log_mutex_);
      captured_logs_.push_back(log_msg);
      // 检查是否匹配任何捕获模式
      for (size_t i = 0; i < capture_patterns_.size(); ++i) {
        if (log_msg.find(capture_patterns_[i]) != std::string::npos) {
          pattern_captured_[i] = true;
          any_pattern_captured = true;
          std::cout << log_msg << std::endl;
        }
      }
    }
    // 如果捕获到新的模式，通知等待的线程
    if (any_pattern_captured) {
      log_cv_.notify_all();
    }
  }
}

void LogCaptureStub::AddCapturePattern(const std::string &pattern) {
  std::lock_guard<std::mutex> lock(log_mutex_);
  capture_patterns_.push_back(pattern);
  pattern_captured_.push_back(false);
}

bool LogCaptureStub::IsPatternCaptured(const std::string &pattern) const {
  std::lock_guard<std::mutex> lock(log_mutex_);
  if (pattern.empty()) {
    // 检查是否有任何模式被捕获
    for (bool captured : pattern_captured_) {
      if (captured) {
        return true;
      }
    }
    return false;
  } else {
    // 检查指定模式是否被捕获
    for (size_t i = 0; i < capture_patterns_.size(); ++i) {
      if (capture_patterns_[i] == pattern && pattern_captured_[i]) {
        return true;
      }
    }
    return false;
  }
}

const std::vector<std::string> &LogCaptureStub::GetCapturedLogs() const {
  std::lock_guard<std::mutex> lock(log_mutex_);
  return captured_logs_;
}

void LogCaptureStub::Reset() {
  std::lock_guard<std::mutex> lock(log_mutex_);
  captured_logs_.clear();
  pattern_captured_.assign(capture_patterns_.size(), false);
}

bool LogCaptureStub::WaitForAllPatternsCaptured(int timeout_ms) {
  std::unique_lock<std::mutex> lock(log_mutex_);
  auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

  // 检查是否所有模式都已被捕获
  auto all_captured = [this]() {
    for (bool captured : pattern_captured_) {
      if (!captured) {
        return false;
      }
    }
    return true;
  };

  // 等待所有模式被捕获或超时
  while (!all_captured()) {
    if (log_cv_.wait_until(lock, deadline) == std::cv_status::timeout) {
      return false;  // 超时
    }
  }
  return true;  // 所有模式都已被捕获
}
}  // namespace llm

namespace {
constexpr int32_t kAclLogModuleIdMax = 0xffff;
llm::LogCallStats g_log_call_stats;
std::mutex g_log_call_stats_mutex;

void SaveLogCall(int32_t module_id, int32_t level, bool is_acl) {
  std::lock_guard<std::mutex> lock(g_log_call_stats_mutex);
  if (is_acl) {
    ++g_log_call_stats.acl_record_count;
  } else {
    ++g_log_call_stats.slog_record_count;
  }
  g_log_call_stats.last_module_id = module_id;
  g_log_call_stats.last_level = level;
}

void SaveRunLogCheck() {
  std::lock_guard<std::mutex> lock(g_log_call_stats_mutex);
  ++g_log_call_stats.run_check_count;
}
}  // namespace

namespace llm {
void ResetLogCallStats() {
  std::lock_guard<std::mutex> lock(g_log_call_stats_mutex);
  g_log_call_stats = {};
}

LogCallStats GetLogCallStats() {
  std::lock_guard<std::mutex> lock(g_log_call_stats_mutex);
  return g_log_call_stats;
}
}  // namespace llm

void dav_log(int module_id, const char *fmt, ...) {}

void DlogRecord(int moduleId, int level, const char *fmt, ...) {
  SaveLogCall(moduleId, level, false);
  va_list valist;
  va_start(valist, fmt);
  if (moduleId & RUN_LOG_MASK) {
    llm::SlogStub::GetInstance()->Log(moduleId & (~RUN_LOG_MASK), DLOG_INFO, fmt, valist);
  } else {
    llm::SlogStub::GetInstance()->Log(moduleId, level, fmt, valist);
  }
  va_end(valist);
}

void acllogRecord(int32_t module_id, int32_t level, const char *fmt, ...) {
  const bool is_run_log = (static_cast<uint32_t>(module_id) & static_cast<uint32_t>(RUN_LOG_MASK)) != 0U;
  if ((module_id < 0) || ((!is_run_log) && (module_id > kAclLogModuleIdMax))) {
    return;
  }
  SaveLogCall(module_id, level, true);
  va_list valist;
  va_start(valist, fmt);
  if (module_id & RUN_LOG_MASK) {
    llm::SlogStub::GetInstance()->Log(module_id & (~RUN_LOG_MASK), DLOG_INFO, fmt, valist);
  } else {
    llm::SlogStub::GetInstance()->Log(module_id, level, fmt, valist);
  }
  va_end(valist);
}

int32_t aclsysGetVersionNum(char *pkg_name, int32_t *version_num) {
  if ((pkg_name == nullptr) || (version_num == nullptr) || (strcmp(pkg_name, "runtime") != 0)) {
    return -1;
  }
  *version_num = 90300001;
  return 0;
}

int32_t acllogCheckDebugLevel(int32_t module_id, int32_t log_level) {
  return CheckLogLevel(module_id, log_level);
}

void DlogErrorInner(int module_id, const char *fmt, ...) {
  SaveLogCall(module_id, DLOG_ERROR, false);
  va_list valist;
  va_start(valist, fmt);
  llm::SlogStub::GetInstance()->Log(module_id, DLOG_ERROR, fmt, valist);
  va_end(valist);
}

void DlogWarnInner(int module_id, const char *fmt, ...) {
  SaveLogCall(module_id, DLOG_WARN, false);
  va_list valist;
  va_start(valist, fmt);
  llm::SlogStub::GetInstance()->Log(module_id, DLOG_WARN, fmt, valist);
  va_end(valist);
}

void DlogInfoInner(int module_id, const char *fmt, ...) {
  SaveLogCall(module_id, DLOG_INFO, false);
  va_list valist;
  va_start(valist, fmt);
  if (module_id & RUN_LOG_MASK) {
    llm::SlogStub::GetInstance()->Log(module_id & (~RUN_LOG_MASK), DLOG_INFO, fmt, valist);
  } else {
    llm::SlogStub::GetInstance()->Log(module_id, DLOG_INFO, fmt, valist);
  }
  va_end(valist);
}

void DlogDebugInner(int module_id, const char *fmt, ...) {
  SaveLogCall(module_id, DLOG_DEBUG, false);
  va_list valist;
  va_start(valist, fmt);
  llm::SlogStub::GetInstance()->Log(module_id, DLOG_DEBUG, fmt, valist);
  va_end(valist);
}

void DlogEventInner(int module_id, const char *fmt, ...) {
  va_list valist;
  va_start(valist, fmt);
  llm::SlogStub::GetInstance()->Log((module_id & (~RUN_LOG_MASK)), DLOG_INFO, fmt, valist);
  va_end(valist);
}

void DlogInner(int module_id, int level, const char *fmt, ...) {
  dav_log(module_id, fmt);
}

int dlog_setlevel(int module_id, int level, int enable_event) {
  auto log_level = getenv("ASCEND_GLOBAL_LOG_LEVEL");
  // 设置环境变量时忽略用例代码里的设置
  if (log_level == nullptr) {
    llm::SlogStub::GetInstance()->SetLevel(level);
    llm::SlogStub::GetInstance()->SetEventLevel(enable_event);
    if (module_id == GE) {
      llm::ge_log_level = level;
    }
  }
  return 0;
}

int dlog_getlevel(int module_id, int *enable_event) {
  return llm::SlogStub::GetInstance()->GetLevel();
}

int CheckLogLevel(int moduleId, int log_level_check) {
  if (moduleId & RUN_LOG_MASK) {
    SaveRunLogCheck();
    return llm::SlogStub::GetInstance()->GetEventLevel();
  }
  if (moduleId == GE) {
    return log_level_check >= llm::ge_log_level;
  }
  return log_level_check >= dlog_getlevel(moduleId, nullptr);
}

/**
 * @ingroup plog
 * @brief DlogReportInitialize: init log in service process before all device setting.
 * @return: 0: SUCCEED, others: FAILED
 */
int DlogReportInitialize() {
  return 0;
}

/**
 * @ingroup plog
 * @brief DlogReportFinalize: release log resource in service process after all device reset.
 * @return: 0: SUCCEED, others: FAILED
 */
int DlogReportFinalize() {
  return 0;
}

int DlogSetAttr(LogAttr logAttr) {
  return 0;
}

void DlogReportStop(int devId) {}

int DlogReportStart(int devId, int mode) {
  return 0;
}
