/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef AIR_CXX_TESTS_DEPENDS_SLOG_SRC_SLOG_STUB_H_
#define AIR_CXX_TESTS_DEPENDS_SLOG_SRC_SLOG_STUB_H_
#include <memory>
#include <string>
#include <map>
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
}  // namespace llm
#endif  // AIR_CXX_TESTS_DEPENDS_SLOG_SRC_SLOG_STUB_H_
