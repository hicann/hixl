/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_ENGINE_UB_MULTI_CHANNEL_HANDLER_H_
#define CANN_HIXL_SRC_HIXL_ENGINE_UB_MULTI_CHANNEL_HANDLER_H_

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "cs/hixl_cs.h"
#include "hixl/hixl_types.h"
#include "engine/multi_channel_dispatcher.h"
#include "engine/ub_client_handler.h"

namespace hixl {

class UbMultiChannelHandler : public UbClientHandler {
 public:
  static Status Create(const HandlerCreateArgs &args, std::unique_ptr<UbMultiChannelHandler> &out);
  ~UbMultiChannelHandler() override = default;

  UbMultiChannelHandler(const std::string &local_engine, const std::string &remote_engine,
                        std::map<CommType, HandlerCreateArgs::EndpointPair> link_pairs);

 protected:
  std::vector<HixlClientHandle> GetHandlesForType(CommType type) const override;
  std::map<CommType, std::vector<HixlClientHandle>> GetAllLinkHandles() const override;
  Status CreateHandlesForType(CommType type, const HixlClientDesc &desc, const HixlClientConfig &config,
                              const HandlerCreateArgs &args) override;
  Status SubmitDescsForType(CommType type, const std::vector<TransferOpDesc> &descs, bool is_get, uint32_t timeout_ms,
                            bool is_async, TransferReq &req) override;
  Status CheckSubReqStatus(const SubReq &sub, TransferStatus &sub_status) override;
  void ClearTransferStateLocked() override;
  void DestroyHandlesLocked() override;

 private:
  std::map<CommType, MultiChannelDispatcher> dispatchers_;
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_ENGINE_UB_MULTI_CHANNEL_HANDLER_H_
