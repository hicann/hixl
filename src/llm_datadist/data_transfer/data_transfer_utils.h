/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_GRAPH_ENGINE_RUNTIME_LLM_ENGINE_DATA_TRANSFER_DATA_TRANSFER_UTILS_H_
#define CANN_GRAPH_ENGINE_RUNTIME_LLM_ENGINE_DATA_TRANSFER_DATA_TRANSFER_UTILS_H_

#include "ge_common/ge_api_types.h"
#include "link_mgr/comm_entity.h"

namespace llm {
class DataTransferUtils {
 public:
  static ge::Status QueryEventStatus(const aclrtEvent &event, aclrtEventRecordedStatus &status);
  static ge::Status SendCache(const aclrtStream stream, CommEntity &comm_entity,
                              std::list<HcclOneSideOpDesc> &transfer_tasks, aclrtEvent &event);
  static ge::Status SendCache(const aclrtStream stream, CommEntity &comm_entity,
                              std::list<HcclOneSideOpDesc> &transfer_tasks);
  static ge::Status SendBatchCache(const aclrtStream stream, const std::vector<HcclOneSideOpDesc> &desces,
                                   CommEntity &comm_entity);
};
}  // namespace llm
#endif  // CANN_GRAPH_ENGINE_RUNTIME_LLM_ENGINE_DATA_TRANSFER_DATA_TRANSFER_UTILS_H_
