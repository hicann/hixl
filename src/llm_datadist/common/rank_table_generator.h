/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_RANK_TABLE_GENERATOR_H_
#define CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_RANK_TABLE_GENERATOR_H_

#include <memory>
#include "llm_datadist/llm_engine_types.h"
#include "llm_datadist/llm_error_codes.h"
#include "common/llm_inner_types.h"
#include "common/common.h"

namespace llm {
class RankTableGenerator {
 public:
  RankTableGenerator() = default;
  virtual ~RankTableGenerator() = default;
  virtual ge::Status Generate(int32_t local_device_id, std::string &rank_table) = 0;
  virtual int32_t GetLocalRankId() = 0;
  virtual int32_t GetPeerRankId() = 0;
};

class LocalCommResGenerator {
 public:
  static ge::Status Generate(const std::string &server_id,
                             int32_t device_id,
                             std::string &local_comm_res);
  static ge::Status GetDeviceIp(uint32_t phy_device_id, std::string &device_ip);
};

class RankTableGeneratorFactory {
 public:
  static std::unique_ptr<RankTableGenerator> Create(const std::string &local_comm_res, const std::string &peer_comm_res);
};
}  // namespace llm
#endif  // CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_RANK_TABLE_GENERATOR_H_
