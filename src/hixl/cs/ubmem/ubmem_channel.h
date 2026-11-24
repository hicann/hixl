/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_CS_UBMEM_UBMEM_CHANNEL_H_
#define CANN_HIXL_SRC_HIXL_CS_UBMEM_UBMEM_CHANNEL_H_

#include <memory>

#include "cs/channel.h"

namespace hixl {

class Endpoint;
class UbMemEngine;

// UB_MEM transport channel. There is no Hcomm channel behind it: the "channel" is only a logical handle
// and the data plane is the process-local UbMemEngine (ACL memcpy or the UB_MEM AICPU kernel).
class UbMemChannel : public Channel {
 public:
  explicit UbMemChannel(Endpoint &local_endpoint);
  ~UbMemChannel() override;

  Status Initialize(const ChannelContext &ctx) override;
  Status Disconnect() override;
  Status Destroy() override;
  Status QueryCompleteStatus(void *complete_handle, HixlCompleteStatus *status) override;
  Status RegMem(const char *mem_tag, const CommMem *mem, MemHandle *mem_handle) override;
  Status UnregMem(MemHandle mem_handle) override;
  Status SetRemoteMemInfo(const CommMem &trans_flag) override;

 protected:
  Status DoConnect(const ChannelDesc &channel_desc, uint32_t timeout_ms) override;
  Status DoBatchTransferSync(bool is_get, const HixlOneSideOpDesc *desc_list, uint32_t list_num,
                             uint32_t timeout_ms) override;
  Status DoBatchTransferAsync(bool is_get, const HixlOneSideOpDesc *desc_list, uint32_t list_num,
                              void **complete_handle) override;

 private:
  Endpoint &local_endpoint_;
  std::unique_ptr<UbMemEngine> engine_;
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_CS_UBMEM_UBMEM_CHANNEL_H_
