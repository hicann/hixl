/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "engine/client_handler_factory.h"
#include "common/hixl_log.h"
#include "engine/direct_client_handler.h"
#include "engine/ub_client_handler.h"

namespace hixl {

std::unique_ptr<IClientHandler> ClientHandlerFactory::Create(const HandlerCreateArgs &args) {
  if (args.handler_type == HandlerCreateArgs::HandlerType::DIRECT) {
    std::unique_ptr<DirectClientHandler> handler;
    if (DirectClientHandler::Create(args, handler) != SUCCESS) {
      HIXL_LOGE(FAILED, "ClientHandlerFactory create DirectClientHandler failed");
      return nullptr;
    }
    return handler;
  }
  std::unique_ptr<UbClientHandler> handler;
  if (UbClientHandler::Create(args, handler) != SUCCESS) {
    HIXL_LOGE(FAILED, "ClientHandlerFactory create UbClientHandler failed");
    return nullptr;
  }
  return handler;
}

}  // namespace hixl
