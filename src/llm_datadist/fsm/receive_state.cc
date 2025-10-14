/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "receive_state.h"
namespace llm {
ge::Status ReceiveState::Preprocess(CommEntity &entity) {
  LLMLOGD("Enter receive state");
  return Process(entity);
}

ge::Status ReceiveState::Process(CommEntity &entity) {
  volatile int8_t *volatile flag = entity.GetCacheInfoFlag();
  if (*flag == 1) {
    auto &recv_statistic_info = entity.GetRecvStatisticInfo();
    recv_statistic_info.sync_flag_get_times++;
    entity.ClearReqFlag();
    return Postprocess(entity);
  }
  return ge::SUCCESS;
}

ge::Status ReceiveState::Postprocess(CommEntity &entity) {
  LLMLOGD("Finish receive state");
  return entity.ChangeState(FsmState::FSM_SEND_STATE);
}
}  // namespace llm