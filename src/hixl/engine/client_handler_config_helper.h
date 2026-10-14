/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_ENGINE_CLIENT_HANDLER_CONFIG_HELPER_H_
#define CANN_HIXL_SRC_HIXL_ENGINE_CLIENT_HANDLER_CONFIG_HELPER_H_

#include <string>

#include "common/hixl_checker.h"
#include "common/hixl_inner_types.h"
#include "cs/hixl_cs.h"
#include "engine/client_handler_factory.h"
#include "engine/endpoint_generator/endpoint_generator.h"
#include "hixl_options.h"

namespace hixl {

class ClientHandlerConfigHelper {
 public:
  static std::string BuildGlobalResourceConfig(const HandlerCreateArgs &args) {
    return SerializeGlobalResourceConfigForClient(args.global_config, args.rdma_tc, args.rdma_sl);
  }

  static Status FillClientDesc(const HandlerCreateArgs &args, const HandlerCreateArgs::EndpointPair &pair,
                               EndpointDesc &local_endpoint, EndpointDesc &remote_endpoint, HixlClientDesc &desc) {
    HIXL_CHK_STATUS_RET(EndpointGenerator::ConvertToEndpointDesc(pair.local, local_endpoint),
                        "ConvertToEndpointDesc failed for local endpoint");
    HIXL_CHK_STATUS_RET(EndpointGenerator::ConvertToEndpointDesc(pair.remote, remote_endpoint),
                        "ConvertToEndpointDesc failed for remote endpoint");
    desc.server_ip = args.server_ip.c_str();
    desc.server_port = args.server_port;
    desc.local_endpoint = &local_endpoint;
    desc.remote_endpoint = &remote_endpoint;
    desc.tc = args.rdma_tc.value_or(kRdmaTrafficClass);
    desc.sl = args.rdma_sl.value_or(kRdmaServiceLevel);
    return SUCCESS;
  }
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_ENGINE_CLIENT_HANDLER_CONFIG_HELPER_H_
