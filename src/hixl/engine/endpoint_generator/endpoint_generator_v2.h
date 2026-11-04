/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_ENGINE_ENDPOINT_GENERATOR_ENDPOINT_GENERATOR_V2_H_
#define CANN_HIXL_SRC_HIXL_ENGINE_ENDPOINT_GENERATOR_ENDPOINT_GENERATOR_V2_H_

#include <string>
#include <vector>

#include "common/hixl_inner_types.h"
#include "endpoint_generator_base.h"
#include "engine/hixl_options.h"

namespace hixl {

// Default automatic endpoint generation for SocType::kDefault (Ascend 950PR&950DT).
// HCCL_INTRA_ROCE_ENABLE=1 generates Host RoCE only and ignores protocol_desc.
// Otherwise: ScaleOut (ub_rtp/uboe by DSMI InterconType), UB CTP from the topology,
// Host RoCE when protocol_desc contains roce:host, and device ubmem when requested.
class EndpointGeneratorV2 : public EndpointGeneratorBase {
 public:
  Status AutoGen(const HixlOptions &options, const std::string &local_engine,
                 std::vector<EndpointConfig> &endpoint_list) const override;
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_ENGINE_ENDPOINT_GENERATOR_ENDPOINT_GENERATOR_V2_H_
