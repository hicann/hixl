/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "idle_state.h"
namespace llm {
ge::Status IdleState::Preprocess(CommEntity &entity) {
  LLMLOGD("Enter idle state");
  return Process(entity);
}

ge::Status IdleState::Process(CommEntity &entity) {
  return Postprocess(entity);
}

ge::Status IdleState::Postprocess(CommEntity &entity) {
  LLMLOGD("Finish idle state");
  return entity.ChangeState(FsmState::FSM_RECEIVE_STATE);
}
}  // namespace llm