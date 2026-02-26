/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_CS_ENDPOINT_H_
#define CANN_HIXL_SRC_HIXL_CS_ENDPOINT_H_

#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "cs/hixl_cs.h"
#include "hixl/hixl_types.h"
#include "common/ctrl_msg.h"
#include "channel.h"

namespace hixl {
class Endpoint {
 public:
  explicit Endpoint(const EndpointDesc &endpoint) : endpoint_(endpoint) {};
  ~Endpoint() = default;

  Status Initialize();
  Status Finalize();

  EndpointHandle GetHandle() const;
  const EndpointDesc &GetEndpoint() const;

  Status RegisterMem(const char *mem_tag, const HcommMem &mem, MemHandle &mem_handle);
  Status DeregisterMem(MemHandle mem_handle);
  Status ExportMem(std::vector<HixlMemDesc> &mem_descs);
  Status CreateChannel(const EndpointDesc &remote_endpoint, ChannelHandle &channel_handle);
  Status DestroyChannel(ChannelHandle channel_handle);
  Status GetMemDesc(MemHandle mem_handle, HixlMemDesc &desc);
  Status MemImport(const void *mem_desc, uint32_t desc_len, HcommMem &out_buf);

 private:
  std::mutex mutex_;
  EndpointDesc endpoint_{};
  EndpointHandle handle_ = nullptr;
  std::map<MemHandle, HixlMemDesc> reg_mems_;
  std::map<ChannelHandle, ChannelPtr> channels_;
};

using EndpointPtr = std::shared_ptr<Endpoint>;
}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_CS_ENDPOINT_H_
