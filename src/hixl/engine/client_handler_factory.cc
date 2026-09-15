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

#include <utility>

#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "engine/direct_client_handler.h"
#include "engine/direct_multi_channel_handler.h"
#include "engine/ub_client_handler.h"

namespace hixl {
Status ClientHandlerFactory::Create(const HandlerCreateArgs &args, std::unique_ptr<IClientHandler> &out) {
  out.reset();
  if (args.handler_type == HandlerCreateArgs::HandlerType::DIRECT) {
    const auto &pair = args.matched_pairs[0];
    const bool is_multi_worker_protocol =
        (pair.type == CommType::COMM_TYPE_UBOE || pair.type == CommType::COMM_TYPE_UBG);
    if (is_multi_worker_protocol && args.multi_worker_num > 1U) {
      std::unique_ptr<DirectMultiChannelHandler> handler;
      HIXL_CHK_STATUS_RET(DirectMultiChannelHandler::Create(args, handler),
                          "ClientHandlerFactory create DirectMultiChannelHandler failed");
      out = std::move(handler);
      return SUCCESS;
    }
    std::unique_ptr<DirectClientHandler> handler;
    HIXL_CHK_STATUS_RET(DirectClientHandler::Create(args, handler),
                        "ClientHandlerFactory create DirectClientHandler failed");
    out = std::move(handler);
    return SUCCESS;
  }
  std::unique_ptr<UbClientHandler> handler;
  HIXL_CHK_STATUS_RET(UbClientHandler::Create(args, handler), "ClientHandlerFactory create UbClientHandler failed");
  out = std::move(handler);
  return SUCCESS;
}

}  // namespace hixl
