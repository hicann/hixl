/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_CHECKER_H_
#define CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_CHECKER_H_
#include <securec.h>
#include <sstream>
#include "common/llm_log.h"

struct LlmErrorResult {
  operator bool() const {
    return false;
  }
  operator ge::graphStatus() const {
    return ge::LLM_PARAM_INVALID;
  }
  template<typename T>
  operator std::unique_ptr<T>() const {
    return nullptr;
  }
  template<typename T>
  operator std::shared_ptr<T>() const {
    return nullptr;
  }
  template<typename T>
  operator T *() const {
    return nullptr;
  }
  template<typename T>
  operator std::vector<std::shared_ptr<T>>() const {
    return {};
  }
  template<typename T>
  operator std::vector<T>() const {
    return {};
  }
  operator std::string() const {
    return "";
  }
  template<typename T>
  operator T() const {
    return T();
  }
};

inline std::vector<char> LlmCreateErrorMsg(const char *format, ...) {
  // no safe function can get the real length, so pre define as MSG_LENGTH , which defineed in toolchain/log_types.h
  std::vector<char> msg(MSG_LENGTH + 1U, '\0');
  va_list args;
  va_start(args, format);
  const auto ret = vsnprintf_truncated_s(msg.data(), MSG_LENGTH + 1U, format, args);
  va_end(args);
  return (ret > 0) ? msg : std::vector<char>{};
}

inline std::vector<char> LlmCreateErrorMsg() {
  return {};
}

#define LLM_ASSERT_EQ(x, y)                                                                                             \
  do {                                                                                                                 \
    const auto &xv = (x);                                                                                              \
    const auto &yv = (y);                                                                                              \
    if (xv != yv) {                                                                                                    \
      std::stringstream ss;                                                                                            \
      ss << "Assert (" << #x << " == " << #y << ") failed, expect " << yv << " actual " << xv;                         \
      REPORT_INNER_ERR_MSG("E19999", "%s", ss.str().c_str());                                                            \
      LLMLOGE(ge::FAILED, "%s", ss.str().c_str());                                                                      \
      return ::LlmErrorResult();                                                                                          \
    }                                                                                                                  \
  } while (false)

#define LLM_ASSERT(exp, ...)                                                                                            \
  do {                                                                                                                 \
    if (!(exp)) {                                                                                                      \
      auto msg = LlmCreateErrorMsg(__VA_ARGS__);                                                                          \
      if (msg.empty()) {                                                                                               \
        REPORT_INNER_ERR_MSG("E19999", "Assert %s failed", #exp);                                                        \
        LLMLOGE(ge::FAILED, "Assert %s failed", #exp);                                                                  \
      } else {                                                                                                         \
        REPORT_INNER_ERR_MSG("E19999", "%s", msg.data());                                                                \
        LLMLOGE(ge::FAILED, "%s", msg.data());                                                                          \
      }                                                                                                                \
      return ::LlmErrorResult();                                                                                          \
    }                                                                                                                  \
  } while (false)

#define LLM_ASSERT_NOTNULL(v, ...) LLM_ASSERT(((v) != nullptr), __VA_ARGS__)
#define LLM_ASSERT_SUCCESS(v, ...) LLM_ASSERT(((v) == ge::SUCCESS), __VA_ARGS__)
#define LLM_ASSERT_RT_OK(v, ...) LLM_ASSERT(((v) == 0), __VA_ARGS__)
#define LLM_ASSERT_EOK(v, ...) LLM_ASSERT(((v) == EOK), __VA_ARGS__)
#define LLM_ASSERT_TRUE(v, ...) LLM_ASSERT((v), __VA_ARGS__)

#endif  // CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_CHECKER_H_
