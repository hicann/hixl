/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_CS_CHANNEL_H_
#define CANN_HIXL_SRC_HIXL_CS_CHANNEL_H_

#include <cstdint>
#include <memory>
#include <vector>

#include "hcomm/hcomm_res_defs.h"
#include "common/ctrl_msg_plugin.h"
#include "cs/hixl_cs.h"
#include "common/hixl_inner_types.h"
#include "hixl/hixl_types.h"
#include "common/transfer_config.h"

namespace hixl {

class Endpoint;
class HixlMemStore;
class GlobalConfig;

enum class ChannelType : int32_t {
  kClient = HCOMM_SOCKET_ROLE_CLIENT,
  kServer = HCOMM_SOCKET_ROLE_SERVER,
};

struct ChannelDesc {
  EndpointDesc remote_endpoint;
  uint8_t tc;
  uint8_t sl;
  uint32_t retry_cnt{0U};
  uint32_t retry_interval{0U};
  ChannelType channel_type{ChannelType::kClient};
  uint64_t channel_index{0UL};
  uint8_t qos{kQosUnset};
  uint32_t max_transfer_count_per_batch{kDefaultMaxTransferCountPerBatch};
  bool configurable_transfer_supported{false};
  uint64_t remote_endpoint_handle{0UL};
};

// Per-connection context handed to a Channel at Initialize time. It carries only the shared resources
// a channel needs to implement memory registration and validation; protocol policy stays inside the
// concrete channel.
struct ChannelContext {
  Endpoint *local_endpoint{nullptr};
  int32_t device_id{-1};
  const GlobalConfig *global_config{nullptr};
  HixlMemStore *mem_store{nullptr};
  bool configurable_transfer_supported{false};
};

// One transport channel owned by a client. ChannelFactory::Create picks the concrete type from the
// local endpoint (protocol + locality). Lifecycle is two-phase: Initialize() at client Create time
// (notify resources / engine), Connect() at client Connect time (establish the transport handle). The
// data-plane and completion entry points mirror the HixlCSClient* API names.
// Transport-only users that never touch the data plane (the passive server side only needs
// Connect/Disconnect/Destroy) may skip Initialize(); such a channel reports FAILED from the
// data-plane entry points instead of dereferencing an unset context.
class Channel {
 public:
  Channel() = default;
  virtual ~Channel() = default;

  virtual Status Initialize(const ChannelContext &ctx) = 0;
  // Single public connection entry point:
  // - connection == nullptr: local/passive transport connect (server side or control-free tests).
  // - connection != nullptr: unified client connect over the control connection (CreateChannel +
  //   optional protocol-defined multi-round handshake).
  Status Connect(const ChannelDesc &channel_desc, uint32_t timeout_ms, const CtrlMsgPluginPtr &connection = nullptr);
  // Advances one server-side round of the generic channel handshake protocol.
  virtual Status StepServerHandshake(const std::vector<uint8_t> &in_payload, std::vector<uint8_t> &out_payload,
                                     bool &done);
  // Tears down the transport handle but keeps the channel initialized, so a failed Connect can be retried.
  virtual Status Disconnect() = 0;
  // Full teardown: transport handle plus any per-connection resources owned by the channel.
  virtual Status Destroy() = 0;

  ChannelHandle GetHandle() const {
    return channel_handle_;
  }

  Status BatchPutSync(const HixlOneSideOpDesc *desc_list, uint32_t list_num, uint32_t timeout_ms);
  Status BatchGetSync(const HixlOneSideOpDesc *desc_list, uint32_t list_num, uint32_t timeout_ms);
  Status BatchPutAsync(const HixlOneSideOpDesc *desc_list, uint32_t list_num, void **complete_handle);
  Status BatchGetAsync(const HixlOneSideOpDesc *desc_list, uint32_t list_num, void **complete_handle);
  virtual Status QueryCompleteStatus(void *complete_handle, HixlCompleteStatus *status) = 0;
  virtual Status RegMem(const char *mem_tag, const CommMem *mem, MemHandle *mem_handle) = 0;
  virtual Status UnregMem(MemHandle mem_handle) = 0;
  // Carries the builtin transfer-completion flag this connection's peer registered. The flag tag is
  // selected from the remote endpoint location by the caller.
  virtual Status SetRemoteMemInfo(const CommMem &trans_flag) = 0;

 protected:
  virtual Status DoConnect(const ChannelDesc &channel_desc, uint32_t timeout_ms) = 0;
  virtual Status StepClientHandshake(const ChannelDesc &channel_desc, const std::vector<uint8_t> &in_payload,
                                     std::vector<uint8_t> &out_payload, bool &done);
  virtual Status DoBatchTransferSync(bool is_get, const HixlOneSideOpDesc *desc_list, uint32_t list_num,
                                     uint32_t timeout_ms) = 0;
  virtual Status DoBatchTransferAsync(bool is_get, const HixlOneSideOpDesc *desc_list, uint32_t list_num,
                                      void **complete_handle) = 0;

  // Stores the per-connection context. Concrete channels call this from their Initialize.
  Status InitContext(const ChannelContext &ctx);
  // Rejects entry points on a channel that was never Initialized (transport-only use).
  Status EnsureInitialized(const char *op) const;
  // Latched transfer failure: once a data-plane failure is observed the channel rejects further
  // transfers until it is destroyed, mirroring the previous client-level behaviour.
  void LatchTransferFailure(Status ret);

  ChannelContext ctx_{};
  ChannelHandle channel_handle_{0UL};
  bool transfer_failure_latched_{false};
  Status transfer_failure_status_{SUCCESS};
  // Cleared by a successful Destroy so later data-plane calls fail fast.
  bool initialized_{false};

 private:
  // Validates that the descriptors stay inside registered memory. Protocols that let the peer address
  // local memory directly (hccs/ubmem device) skip the local-memory half of the check.
  Status ValidateAddress(uint32_t list_num, const HixlOneSideOpDesc *desc_list) const;
  Status BatchTransferSync(bool is_get, const char *op, const HixlOneSideOpDesc *desc_list, uint32_t list_num,
                           uint32_t timeout_ms);
  Status BatchTransferAsync(bool is_get, const char *op, const HixlOneSideOpDesc *desc_list, uint32_t list_num,
                            void **complete_handle);
  Status LatchTransferFailureIfNeeded(Status ret);
};

using ChannelPtr = std::shared_ptr<Channel>;

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_CS_CHANNEL_H_
