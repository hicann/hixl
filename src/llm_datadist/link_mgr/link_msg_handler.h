/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_LINK_MSG_HANDLER_H_
#define CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_LINK_MSG_HANDLER_H_

#include <vector>
#include <mutex>
#include "llm_datadist/llm_error_codes.h"
#include "common/llm_inner_types.h"
#include "comm_entity_manager.h"
#include "cache_mgr/comm_mem_manager.h"
#include "cache_mgr/cache_manager.h"
#include "common/msg_handler_plugin.h"

namespace llm {
enum class LinkMsgType : int32_t {
  kConnect = 1,
  kDisconnect = 2,
  kStatus = 3,
  kEnd
};

struct LLMExchangeInfo {
  uint64_t cache_table_addr;
  uint64_t cache_table_size;
  uint64_t req_addr;
  uint64_t req_size;
  uint64_t resp_addr;
  uint64_t resp_size;
  uint64_t cluster_id;
  std::string comm_res;
  std::string comm_name;
  int32_t timeout;
  bool force_link;
};

struct LLMLinkStatus {
  uint32_t error_code;
  std::string error_message;
};

struct LLMDisconnectInfo {
  uint64_t cluster_id;
  int32_t timeout;
};

class LinkMsgHandler {
 public:
  LinkMsgHandler(uint64_t cluster_id, int32_t device_id, CommEntityManager *comm_entity_manager,
                 CommMemManager *comm_mem_manager, CacheManager *cache_manager)
      : cluster_id_(cluster_id),
        device_id_(device_id),
        remote_cache_accessible_(true),
        comm_entity_manager_(comm_entity_manager),
        comm_mem_manager_(comm_mem_manager),
        cache_manager_(cache_manager),
        comm_config_{},
        rt_context_(nullptr) {};
  ~LinkMsgHandler() = default;
  ge::Status Initialize(const std::map<ge::AscendString, ge::AscendString> &options);
  void Finalize();

  ge::Status StartDaemon(uint32_t listen_port);
  ge::Status StopDaemon();
  ge::Status LinkCluster(const ClusterInfo &cluster, int32_t timeout);
  ge::Status UnlinkCluster(const ClusterInfo &cluster, int32_t timeout, bool force_flag) const;
  size_t GetLinkSize() const;

 private:
  template<typename T>
  static ge::Status SendMsg(int32_t fd, LinkMsgType msg_type, const T &msg);
  template<typename T>
  static ge::Status RecvMsg(int32_t fd, LinkMsgType msg_type, T &msg);
  ge::Status ConnectedProcess(int32_t fd, bool &keep_fd);
  ge::Status ExchangeInfoProcess(const LLMExchangeInfo &peer_exchange_info, int32_t timeout, bool force_link,
                                 EntityMemInfoPtr &mem_info_ptr);
  ge::Status DisconnectInfoProcess(const LLMDisconnectInfo &peer_disconnect_info) const;
  ge::Status ProcessDisconnectRequest(int32_t fd, const std::vector<char> &msg) const;
  ge::Status ProcessConnectRequest(int32_t fd, const std::vector<char> &msg);
  ge::Status GenerateLocalCommRes(const ClusterInfo &cluster);
  ge::Status CreateEntityMemInfo(EntityMemInfoPtr &mem_info_ptr);
  ge::Status SetEntityMemInfo(const LLMExchangeInfo &peer_exchange_info,
                              EntityPtr entity, EntityMemInfoPtr &mem_info_ptr) const;
  template<typename T>
  static ge::Status Serialize(const T &msg, std::string &msg_str);
  template<typename T>
  static ge::Status Deserialize(const std::vector<char> &msg_str, T &msg);

  uint64_t cluster_id_;
  int32_t device_id_;
  bool remote_cache_accessible_;
  CommEntityManager *comm_entity_manager_;
  CommMemManager *comm_mem_manager_;
  CacheManager *cache_manager_;
  MsgHandlerPlugin handler_plugin_;
  std::string local_comm_name_;
  HcclCommConfig comm_config_;
  rtContext_t rt_context_;
  std::mutex mutex_;
  std::string local_ip_;
  std::string local_comm_res_;
};
}  // namespace llm
#endif  // CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_LINK_MSG_HANDLER_H_
