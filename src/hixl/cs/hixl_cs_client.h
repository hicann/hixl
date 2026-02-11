/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_CS_HIXL_CS_CLIENT_H_
#define CANN_HIXL_SRC_HIXL_CS_HIXL_CS_CLIENT_H_

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <array>
#include <atomic>
#include <vector>
#include <map>
#include "common/hixl_cs.h"
#include "hixl/hixl_types.h"
#include "endpoint.h"
#include "channel.h"
#include "hcomm_compat.h"

namespace hixl {
struct CompleteHandle {
  uint32_t magic;
  int32_t flag_index;
  uint64_t *flag_address;
};

enum class UbOpType : uint32_t {
  kGet = 0U,
  kPut = 1U,
};

struct CommunicateMem {
  uint32_t list_num;
  void **dst_buf_list;
  const void **src_buf_list;
  uint64_t *len_list;
};

struct Buffers {
  const void *remote;
  const void *local;
};

struct ImportCtx {
  Endpoint *ep{nullptr};
  EndpointHandle ep_handle{nullptr};
  uint32_t num{0U};
  bool need_tag{false};
  std::vector<HcommMem> imported;
  std::vector<void *> recorded_addrs;
  std::map<std::string, HcommMem> tag_mem_map;
  std::vector<HcommMem> mems;
  std::vector<std::vector<char>> tag_storage;
};

class HixlCSClient {
 public:
  HixlCSClient() = default;
  ~HixlCSClient() = default;

  Status InitFlagQueue() noexcept;
  Status Create(const char *server_ip, uint32_t server_port, const EndpointDesc *src_endpoint,
                const EndpointDesc *dst_endpoint, const HixlClientConfig *config);
  Status Connect(uint32_t timeout_ms);
  Status GetRemoteMem(HcommMem **remote_mem_list, char ***mem_tag_list, uint32_t *list_num, uint32_t timeout_ms);
  Status Destroy();

 private:
  Status ExchangeEndpointAndCreateChannelLocked(uint32_t timeout_ms);
  Status ImportRemoteMem(std::vector<HixlMemDesc> &desc_list, HcommMem **remote_mem_list, char ***mem_tag_list,
                         uint32_t *list_num);
  void FillOutputParams(ImportCtx &ctx, HcommMem **remote_mem_list, char ***mem_tag_list, uint32_t *list_num);
  Status ClearRemoteMemInfo();

 private:
  std::mutex mutex_;
  std::string server_ip_;
  uint32_t server_port_{0U};
  EndpointPtr src_endpoint_;
  EndpointDesc dst_endpoint_{};
  Channel client_channel_;
  ChannelHandle client_channel_handle_ = 0UL;
  uint64_t dst_endpoint_handle_{0U};
  static constexpr size_t kFlagQueueSize = 4096;  // 用于初始化队列和内存地址列表
  uint64_t *flag_queue_ = nullptr;
  std::array<uint32_t, kFlagQueueSize> available_indices_{};
  size_t top_index_ = 0;  // 栈顶指针
  std::mutex indices_mutex_;
  std::array<CompleteHandle *, kFlagQueueSize> live_handles_{};  // 用来记录读写生成的queryhandle
  int32_t socket_ = -1;
  std::map<std::string, HcommMem> tag_mem_descs_;
  std::vector<HcommMem> remote_mems_out_;
  std::vector<std::vector<char>> remote_tag_storage_;
  std::vector<char *> remote_tag_ptrs_;
  std::vector<void *> recorded_remote_addrs_;
  std::vector<HcommMem> imported_remote_bufs_;
  std::vector<HixlMemDesc> desc_list_;
};
}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_CS_HIXL_CS_CLIENT_H_
