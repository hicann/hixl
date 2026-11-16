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
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "cs/hixl_cs.h"
#include "common/ctrl_msg_plugin.h"
#include "common/hixl_inner_types.h"
#include "common/hixl_utils.h"
#include "channel.h"
#include "endpoint.h"
#include "hixl_mem_store.h"
#include "global_config.h"

namespace hixl {

struct ImportCtx {
  Endpoint *ep{nullptr};
  EndpointHandle ep_handle{nullptr};
  HixlMemStore *store{nullptr};
  uint32_t num{0U};
  bool need_tag{false};
  std::vector<CommMem> imported;
  std::vector<void *> recorded_addrs;
  std::map<std::string, CommMem> tag_mem_map;
  std::vector<CommMem> mems;
  std::vector<std::vector<char>> tag_storage;
};

class HixlCSClient {
 public:
  HixlCSClient();
  ~HixlCSClient();
  Status Create(const HixlClientDesc *client_desc, const HixlClientConfig *config);

  Status Connect(uint32_t timeout_ms);

  Status GetRemoteMem(CommMem **remote_mem_list, char ***mem_tag_list, uint32_t *list_num, uint32_t timeout_ms);

  // 注册client的endpoint的内存信息到内存注册表中。
  Status RegMem(const char *mem_tag, const CommMem *mem, MemHandle *mem_handle);

  Status BatchPutAsync(const HixlOneSideOpDesc *desc_list, uint32_t list_num, void **query_handle);
  Status BatchGetAsync(const HixlOneSideOpDesc *desc_list, uint32_t list_num, void **query_handle);

  Status BatchPutSync(const HixlOneSideOpDesc *desc_list, uint32_t list_num, uint32_t timeout_ms);
  Status BatchGetSync(const HixlOneSideOpDesc *desc_list, uint32_t list_num, uint32_t timeout_ms);

  // 通过已经建立好的channel，检查批量读写的状态。
  Status QueryCompleteStatus(void *query_handle, HixlCompleteStatus *status);

  // 注销client的endpoint的内存信息。
  Status UnregMem(MemHandle mem_handle);

  Status Destroy();

 private:
  void CloseSocket();
  void FinalizeEndpointAfterCreateFailure();
  Status ParseGlobalConfig(const HixlClientDesc *client_desc, const HixlClientConfig *config);
  Status InitBaseClient(const HixlClientDesc *client_desc);
  Status InitDeviceResource(const EndpointDesc &ep);
  Status ExchangeEndpointAndCreateChannel(uint32_t timeout_ms);
  Status GetRemoteMemImpl(uint32_t timeout_ms, CommMem **remote_mem_list, char ***mem_tag_list, uint32_t *list_num);
  Status InitRdmaRetryConfig();
  Status ImportRemoteMem(std::vector<HixlMemDesc> &desc_list, CommMem **remote_mem_list, char ***mem_tag_list,
                         uint32_t *list_num);
  void FillOutputParams(ImportCtx &ctx, CommMem **remote_mem_list, char ***mem_tag_list, uint32_t *list_num);
  Status ClearRemoteMemInfo();

  // 获取 context 切换 guard，用于对外接口的 context 管理
  std::unique_ptr<hixl::TemporaryRtContext> GetContextGuard() const;

 private:
  std::mutex mutex_;  // 所有方法串行执行，不支持并发调用
  // 用于记录内存地址的分配情况
  HixlMemStore mem_store_;
  std::string server_ip_;
  uint32_t server_port_{0U};
  EndpointPtr local_endpoint_;
  EndpointDesc remote_endpoint_{};
  uint8_t tc_{kRdmaTrafficClass};
  uint8_t sl_{kRdmaServiceLevel};
  uint32_t retry_cnt_{kRdmaRetryCntDefault};
  uint32_t retry_interval_{kRdmaRetryIntervalDefault};
  GlobalConfig global_config_;
  bool tls_initialized_ = false;
  bool configurable_transfer_supported_{false};
  // 每个 client 持有一个 channel，所有传输/内存/完成态操作都经该 channel 下发。
  ChannelPtr client_channel_;
  uint64_t remote_endpoint_handle_{0U};
  CtrlMsgPluginPtr connection_;
  bool is_connected_{false};
  std::map<std::string, CommMem> tag_mem_descs_;
  std::vector<CommMem> remote_mems_out_;
  std::vector<std::vector<char>> remote_tag_storage_;
  std::vector<char *> remote_tag_ptrs_;
  std::vector<void *> recorded_remote_addrs_;
  std::vector<CommMem> imported_remote_bufs_;
  std::vector<HixlMemDesc> desc_list_;
  int32_t device_id_{-1};
};
}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_CS_HIXL_CS_CLIENT_H_
