/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "state_manager.h"
#include "idle_state.h"
#include "receive_state.h"
#include "send_state.h"

namespace llm {
StateManager &StateManager::GetInstance() {
  static StateManager manager;
  return manager;
}

StateManager::StateManager() {
  static IdleState idle_state;
  RegisterState(FsmState::FSM_IDLE_STATE, &idle_state, "FSM_IDLE_STATE");
  static ReceiveState receive_state;
  RegisterState(FsmState::FSM_RECEIVE_STATE, &receive_state, "FSM_RECEIVE_STATE");
  static SendState send_state;
  RegisterState(FsmState::FSM_SEND_STATE, &send_state, "FSM_SEND_STATE");
}

void StateManager::RegisterState(llm::FsmState id, llm::BaseState *state, const char *desc) {
  state_[static_cast<size_t>(id)] = state;
  stateDesc_[static_cast<size_t>(id)] = desc;
}

BaseState *StateManager::GetState(llm::FsmState id) const {
  return state_[static_cast<size_t>(id)];
}

const std::string &StateManager::GetStateDesc(llm::FsmState id) const {
  return stateDesc_[static_cast<size_t>(id)];
}
}  // namespace llm