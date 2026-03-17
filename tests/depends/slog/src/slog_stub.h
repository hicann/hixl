/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef AIR_CXX_TESTS_DEPENDS_SLOG_SRC_SLOG_STUB_H_
#define AIR_CXX_TESTS_DEPENDS_SLOG_SRC_SLOG_STUB_H_
#include <memory>
#include <string>
#include <map>
#include <vector>
#include <stdarg.h>
#include "dlog_pub.h"
namespace llm {
class SlogStub {
 public:
  SlogStub() {
    log_init = true;
  }
  static SlogStub *GetInstance();
  static void SetInstance(std::shared_ptr<SlogStub> stub);
  virtual ~SlogStub();

  virtual void Log(int module_id, int level, const char *fmt, va_list args) = 0;
  int Format(char *buff, size_t buff_len, int module_id, int level, const char *fmt, va_list args);
  const char *GetLevelStr(int level) {
    if (!log_init) {
      return "[UNKNOWN]";
    }
    auto it = level_str.find(level);
    if (it == level_str.end()) {
      return "[UNKNOWN]";
    }
    return it->second.c_str();
  }

  const char *GetModuleIdStr(int module_id) {
    if (!log_init) {
      return "[UNKNOWN]";
    }
    auto mask = ~RUN_LOG_MASK;
    auto it = module_id_str.find(module_id & mask);
    if (it == module_id_str.end()) {
      return "UNKNOWN";
    }
    return it->second.c_str();
  }

  int GetLevel() const {
    return log_level_;
  }

  int GetEventLevel() const {
    return event_log_level_;
  }
  void SetLevel(int level);
  void SetEventLevel(int event_level);
  void SetLevelDebug() {
    SetLevel(DLOG_DEBUG);
  }
  void SetLevelInfo() {
    SetLevel(DLOG_INFO);
  }

 protected:
  bool log_init = false;

 private:
  int log_level_ = DLOG_ERROR;
  int event_log_level_ = 0;
  std::map<int, std::string> level_str = {
      {DLOG_DEBUG, "[DEBUG]"},
      {DLOG_INFO, "[INFO]"},
      {DLOG_WARN, "[WARNING]"},
      {DLOG_ERROR, "[ERROR]"},
      {DLOG_DEBUG, "[TRACE]"}
  };
  std::map<int, std::string> module_id_str = {
      {GE, "GE"},
      {FE, "FE"},
      {HCCL, "HCCL"},
      {RUNTIME, "RUNTIME"}
  };
};

/**
 * @class LogCaptureStub
 * @brief 通用日志捕获Stub，用于测试中捕获特定模式的日志
 */
class LogCaptureStub : public SlogStub {
 public:
  /**
   * @brief 构造函数
   */
  LogCaptureStub();
 
  /**
   * @brief 析构函数
   */
  virtual ~LogCaptureStub();
 
  /**
   * @brief 日志处理函数
   * @param module_id 模块ID
   * @param level 日志级别
   * @param fmt 日志格式
   * @param args 日志参数
   */
  void Log(int module_id, int level, const char *fmt, va_list args) override;
 
  /**
   * @brief 添加要捕获的日志模式
   * @param pattern 日志模式字符串
   */
  void AddCapturePattern(const std::string &pattern);
 
  /**
   * @brief 检查是否捕获到指定模式的日志
   * @param pattern 日志模式字符串，如果为空则检查是否捕获到任何模式的日志
   * @return 是否捕获到指定模式的日志
   */
  bool IsPatternCaptured(const std::string &pattern = "") const;
 
  /**
   * @brief 获取捕获到的日志
   * @return 捕获到的日志列表
   */
  const std::vector<std::string> &GetCapturedLogs() const;
 
  /**
   * @brief 重置捕获状态
   */
  void Reset();
 
 private:
  std::vector<std::string> capture_patterns_;  // 要捕获的日志模式
  std::vector<std::string> captured_logs_;     // 捕获到的日志
  std::vector<bool> pattern_captured_;         // 每个模式的捕获状态
};
}  // namespace llm
#endif  // AIR_CXX_TESTS_DEPENDS_SLOG_SRC_SLOG_STUB_H_
