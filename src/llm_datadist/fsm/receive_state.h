/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_RECEIVE_STATE_H_
#define CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_RECEIVE_STATE_H_

#include <vector>
#include "ge_common/ge_api_types.h"
#include "common/llm_inner_types.h"
#include "common/common.h"
#include "fsm/base_state.h"
#include "link_mgr/comm_entity.h"

namespace llm {
class ReceiveState : public BaseState {
 public:
  ReceiveState() noexcept = default;
  ~ReceiveState() override = default;
  ge::Status Preprocess(CommEntity &entity) override;
  ge::Status Process(CommEntity &entity) override;
  ge::Status Postprocess(CommEntity &entity) override;

  ReceiveState(const ReceiveState &) = delete;
  ReceiveState(const ReceiveState &&) = delete;
  ReceiveState &operator=(const ReceiveState &) = delete;
  ReceiveState &operator=(const ReceiveState &&) = delete;
};
}  // namespace llm

#endif  // CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_RECEIVE_STATE_H_
