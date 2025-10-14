/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_ENTITY_H_
#define CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_ENTITY_H_

#include <vector>
#include <list>

#include "runtime/rt.h"

#include "llm_datadist/llm_error_codes.h"
#include "ge/ge_api_types.h"
#include "common/llm_inner_types.h"
#include "common/common.h"
#include "cache_mgr/cache_manager.h"
#include "hccl/hccl_adapter.h"
#include "common/llm_mem_pool.h"
#include "statistic_manager.h"
#include "utils/cache_access_table.h"

namespace llm {
struct EntityInfo {
  void *local_req_flag_ptr;
  void *local_req_ptr;
  void *local_resp_flag_ptr;
  void *local_resp_ptr;

  uint8_t *send_buffer_req_flag_ptr;
  uint8_t *send_buffer_req_ptr;
  uint8_t *send_buffer_resp_flag_ptr;
  uint8_t *send_buffer_resp_ptr;

  uint8_t *send_dev_buffer_req_flag_ptr;
  uint8_t *send_dev_buffer_req_ptr;
  uint8_t *send_dev_buffer_resp_flag_ptr;
  uint8_t *send_dev_buffer_resp_ptr;

  void *remote_flag_ptr;
  void *remote_req_ptr;
  void *remote_resp_flag_ptr;
  void *remote_resp_ptr;
};

using FillRequestFunc = std::function<void(TransferCacheReq&, uint64_t &size)>;
using FillResponseFunc = std::function<void(ResponseInfo &, uint64_t &size)>;

class DataTransferJob;

class RegBufferPool {
 public:
  RegBufferPool(uint64_t capacity, bool is_host);
  ~RegBufferPool();
  ge::Status Initialize();
  void Finalize();
  ge::Status Alloc(void *&buffer);
  void Free(void *buffer);

 private:
  uint64_t capacity_;
  bool is_host_;
  std::mutex mutex_;
  void *buffer_;
  void *handle_;
  std::map<void *, bool> reg_buffers_;
};

class EntityMemInfo {
 public:
  EntityMemInfo(bool remote_cache_accessible, RegBufferPool *host_reg_pool, RegBufferPool *device_reg_pool);
  ~EntityMemInfo();
  ge::Status Initialize();

 private:
  friend class CommEntity;
  friend class LinkMsgHandler;
  friend class CommLinkManager;

  bool remote_cache_accessible_;
  void *msg_buffer_ = nullptr;
  void *req_ = nullptr;
  void *transfer_buffer_ = nullptr;
  void *host_transfer_buffer_ = nullptr;
  void *resp_ = nullptr;
  void *host_transfer_req_ = nullptr;
  void *host_transfer_resp_ = nullptr;
  void *transfer_req_ = nullptr;
  void *transfer_resp_ = nullptr;
  RegBufferPool *host_reg_pool_ = nullptr;
  RegBufferPool *device_reg_pool_ = nullptr;
};

using EntityMemInfoPtr = std::unique_ptr<EntityMemInfo>;

class EntityCommInfo {
 public:
  struct CommParams {
    uint32_t rank_id;
    HcclCommConfig comm_config;
    std::string rank_table;
    std::vector<void *> mem_handles;
    int32_t timeout = 0;
    int32_t link_retry_count = 1;
  };

  explicit EntityCommInfo(const CommParams &comm_params);
  EntityCommInfo(const HcclComm &comm, std::vector<void *> mem_handles, int32_t link_total_time, int32_t link_retry_count);
  ~EntityCommInfo();
  ge::Status Initialize();
  ge::Status Finalize();

 private:
  friend class CommEntity;
  friend class CommLinkManager;

  ge::Status PrepareHcclComm() const;

  CommParams params_;
  HcclComm comm_;
  bool comm_inited_;
  std::vector<void *> bind_handles_;
};

using EntityCommInfoPtr = std::shared_ptr<EntityCommInfo>;

class CommEntity {
 public:
  CommEntity(uint64_t comm_id, uint64_t cluster_id, uint32_t rank_id,
             uint64_t local_cluster_id, uint32_t local_rank_id);
  ge::Status Initialize(bool remote_cache_accessible);
  ge::Status Initialize(bool remote_cache_accessible, const EntityCommInfo::CommParams &params);
  ge::Status Finalize();
  ~CommEntity();
  void ClearReqFlag() const;
  ge::Status SetInfo();
  bool CheckEntityInfo() const;
  ge::Status ProcessState();
  ge::Status ChangeState(FsmState next_state);
  int8_t *GetCacheInfoFlag() const;

  void *GetReq();
  void *GetResp();
  rtStream_t  GetStream() const;
  rtContext_t GetCurrentContext() const;
  void SetContext(rtContext_t context);
  uint64_t GetClusterId() const;
  uint64_t GetCommId() const;
  HcclComm GetComm() const;
  std::vector<HcclMem> &GetRemoteMems();
  const EntityInfo &GetEntityInfo() const;
  void SetCacheManager(CacheManager *cache_manager);
  CacheManager *GetCacheManager() const;
  const std::string &GetDesc() const;
  FsmState GetCurState() const;
  void MarkEntityDestroyed();
  void MarkEntityError();
  void MarkEntityIdle();
  void SetHostMemPool(LlmMemPool *host_mem_pool);
  LlmMemPool *GetHostMemPool() const;
  void SetEntityMemInfo(EntityMemInfoPtr &mem_info);
  void SetEntityCommInfo(EntityCommInfoPtr comm_info);
  ge::Status BatchPutAsync(std::vector<HcclOneSideOpDesc> &op_descs, rtStream_t stream = nullptr);
  ge::Status BatchGetAsync(std::vector<HcclOneSideOpDesc> &op_descs, rtStream_t stream = nullptr);
  SendStatisticInfo &GetSendStatisticInfo(rtStream_t stream = nullptr);
  RecvStatisticInfo &GetRecvStatisticInfo();
  void Dump() const;
  ge::Status SendRequest(const FillRequestFunc &fill_request_func, rtStream_t stream);
  ge::Status SendResponse(const FillResponseFunc &fill_response_func);
  ge::Status SendResponse(ge::Status status);
  ge::Status GetResponse(const ResponseInfo *&response_info,
                         const std::chrono::steady_clock::time_point *end_time_point = nullptr) const;
  const TransferCacheReq &GetRequest() const;
  const std::unique_ptr<DataTransferJob> &GetDataTransferJob() const;
  void SetDataTransferJob(std::unique_ptr<DataTransferJob> &&data_transfer_job);
  void SetTimeoutPoint(const std::chrono::steady_clock::time_point &timeout_point);
  const std::chrono::steady_clock::time_point &GetTimeoutPoint() const;
  void ClearResponseFlags();
  const std::pair<uint64_t, uint64_t> &GetCacheKeyToRemove() const;
  void SetCacheKeyToRemove(const std::pair<uint64_t, uint64_t> &cache_key_to_remove);
  std::mutex &GetPullMutex();
  std::mutex &GetProcessMutex();
  void *GetTransferBuffer() const;
  ge::Status SetRemoteAddresses();
  CacheAccessTable &GetCacheAccessTable();

 private:
  std::mutex process_mutex_;
  FsmState cur_state_ = FsmState::FSM_INIT_STATE;
  uint64_t comm_id_;
  uint64_t cluster_id_;  // remote cluster id
  uint32_t rank_id_;     // remote rank id
  uint64_t local_cluster_id_;
  uint32_t local_rank_id_;
  std::string desc_;
  rtStream_t stream_;
  rtContext_t rt_context_{nullptr};
  EntityMemInfoPtr mem_info_ptr_{nullptr};
  EntityCommInfoPtr comm_info_ptr_{nullptr};
  bool inner_comm_ = false;
  CacheAccessTable cache_access_table_;
  EntityInfo info_{};
  std::vector<HcclMem> remote_mems_{};
  CacheManager *cache_manager_{};
  LlmMemPool *host_mem_pool_{};
  std::mutex info_mutex_;
  std::map<rtStream_t, SendStatisticInfo> send_statistic_infos_;
  RecvStatisticInfo recv_statistic_info_;
  std::unique_ptr<DataTransferJob> data_transfer_job_;
  std::chrono::steady_clock::time_point timeout_point_;
  std::pair<uint64_t, uint64_t> cache_key_to_remove_;
  bool is_exchanged_mem_{false};
  std::mutex pull_mutex_;
};

class BufferedSender {
 public:
  void Initialize(CommEntity &comm_entity, rtStream_t stream = nullptr, bool put_or_get = true);

  ge::Status Put(void *local_addr, void *remote_addr, size_t size, bool flush = false);

  ge::Status Flush();

 private:
  std::vector<HcclOneSideOpDesc> op_descs_;
  CommEntity *comm_entity_ = nullptr;
  rtStream_t stream_ = nullptr;
  bool put_or_get_ = true;
};
}  // namespace llm
#endif  // CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_ENTITY_H_
