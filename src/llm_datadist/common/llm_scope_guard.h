/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.

 * The code snippet comes from Huawei's open-source Mindspore project.
 * Copyright 2019-2020 Huawei Technologies Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 */

#ifndef CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_SCOPE_GUARD_H_
#define CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_SCOPE_GUARD_H_

#include <functional>

/// Usage:
/// Acquire Resource 1
/// MAKE_GUARD([&] { Release Resource 1 })
/// Acquire Resource 2
// MAKE_GUARD([&] { Release Resource 2 })
#define LLM_MAKE_GUARD(var, callback) const ::llm::ScopeGuard const_guard_##var(callback)

#define LLM_DISMISSABLE_GUARD(var, callback) ::llm::ScopeGuard make_guard_##var(callback)
#define LLM_DISMISS_GUARD(var) make_guard_##var.Dismiss()

namespace llm {
class GE_FUNC_VISIBILITY ScopeGuard {
 public:
  // Noncopyable
  ScopeGuard(ScopeGuard const &) = delete;
  ScopeGuard &operator=(ScopeGuard const &) & = delete;

  explicit ScopeGuard(const std::function<void()> &on_exit_scope) : on_exit_scope_(on_exit_scope), dismissed_(false) {}

  ~ScopeGuard() {
    if (!dismissed_) {
      if (on_exit_scope_ != nullptr) {
        try {
          on_exit_scope_();
        } catch (std::bad_function_call &) { }
          catch (...) { }
      }
    }
  }

  void Dismiss() { dismissed_ = true; }

 private:
  std::function<void()> on_exit_scope_;
  bool dismissed_ ;
};
}  // namespace llm

#endif  // CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_SCOPE_GUARD_H_
