/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_ENGINE_ENDPOINT_GENERATOR_ENDPOINT_GENERATOR_BASE_H_
#define CANN_HIXL_SRC_HIXL_ENGINE_ENDPOINT_GENERATOR_ENDPOINT_GENERATOR_BASE_H_

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "common/hixl_checker.h"
#include "common/hixl_inner_types.h"
#include "common/soc_type.h"
#include "engine/hixl_options.h"

namespace hixl {

// Base class for SoC-dependent automatic endpoint generation. Derived versions implement the
// generation flow for a SoC category; the caller must set the target device (aclrtSetDevice)
// before AutoGen is invoked. Shared protocol_desc helpers live here so both the facade and the
// derived versions can reuse them.
class EndpointGeneratorBase {
 public:
  virtual ~EndpointGeneratorBase() = default;

  /**
   * @brief Generate the default endpoint list for the current SoC category
   * @param [in] options Parsed HixlOptions
   * @param [in] local_engine Local listen address
   * @param [out] endpoint_list Generated endpoint list
   * @return SUCCESS on success, other error codes on failure
   */
  virtual Status AutoGen(const HixlOptions &options, const std::string &local_engine,
                         std::vector<EndpointConfig> &endpoint_list) const = 0;

  // Selects the derived generator for the SoC category.
  static std::unique_ptr<EndpointGeneratorBase> Create(SocType soc_type);

  // Shared protocol_desc helpers used by the facade and the derived generators.
  static std::string BuildProtocolDescKey(const std::string &protocol, const std::string &placement);
  static bool IsSupportedProtocolDesc(const std::string &protocol, const std::string &placement);
  static Status ParseProtocolDesc(const std::vector<std::string> &protocol_desc, std::set<std::string> &desc_set);
  static Status FilterEndpointsByProtocolDescList(const std::vector<std::string> &protocol_desc,
                                                  std::vector<EndpointConfig> &endpoint_list);
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_ENGINE_ENDPOINT_GENERATOR_ENDPOINT_GENERATOR_BASE_H_
