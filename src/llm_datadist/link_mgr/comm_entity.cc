/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "comm_entity.h"
#include "common/def_types.h"
#include "common/mem_utils.h"
#include "common/llm_log.h"
#include "fsm/state_manager.h"
#include "data_transfer/data_transfer_job.h"
#include "cache_mgr/cache_manager.h"
#include "cache_mgr/comm_mem_manager.h"
#include "utils/sync_flag.h"
#include "common/llm_checker.h"
#include "common/llm_scope_guard.h"

namespace llm {
namespace {
constexpr uint64_t kDefaultMsgBufferSize = 128U * 1024U;
// evaluate with the maximum block number set to 1k and the maximum tensor number set to 1k.
constexpr uint64_t kDefaultReqBufferSize = 112U * 1024U;
constexpr uint64_t kDefaultRespBufferSize = 16U * 1024U;
constexpr uint32_t kFlagSize = 8U;
constexpr size_t kMaxOpDescNum = 64U;
constexpr size_t kMinRemoteMemSize = 3U;
constexpr int32_t kRetryCountMin = 1;
constexpr int32_t kRetryCountMax = 100;
// hccl HcclCommInitClusterInfoMemConfig not support parallel call, so use mutex to protect it
std::mutex g_mutex_;
}  // namespace


RegBufferPool::RegBufferPool(uint64_t capacity, bool is_host)
    : capacity_(capacity), is_host_(is_host), buffer_(nullptr), handle_(nullptr) {}

ge::Status RegBufferPool::Initialize() {
  uint64_t buffer_size = kDefaultMsgBufferSize * capacity_;
  if (is_host_) {
    LLM_CHK_ACL_RET(aclrtMallocHost(&buffer_, buffer_size));
  } else {
    LLM_CHK_ACL_RET(aclrtMalloc(&buffer_, buffer_size,
                                static_cast<aclrtMemMallocPolicy>(ACL_MEM_TYPE_HIGH_BAND_WIDTH | ACL_MEM_MALLOC_HUGE_FIRST)));
  }
  LLM_DISMISSABLE_GUARD(fail_guard, ([this]() {
    if (is_host_) {
      LLM_CHK_ACL(aclrtFreeHost(buffer_));
    } else {
      LLM_CHK_ACL(aclrtFree(buffer_));
    }
  }));
  auto type = is_host_ ? HcclMemType::HCCL_MEM_TYPE_HOST : HcclMemType::HCCL_MEM_TYPE_DEVICE;
  LLM_CHK_STATUS_RET(GlobalMemManager::GetInstance().RegisterMem(buffer_, buffer_size, type, handle_),
                    "Failed to register buffer pool addr.");
  LLM_DISMISS_GUARD(fail_guard);

  for (uint64_t i = 0; i < capacity_; ++i) {
    void *ptr = static_cast<uint8_t *>(buffer_) + i * kDefaultMsgBufferSize;
    reg_buffers_[ptr] = false;
  }
  return ge::SUCCESS;
}

RegBufferPool::~RegBufferPool() {
  // buffer freed after all mem deregistered
  if (buffer_ != nullptr) {
    if (is_host_) {
      LLM_CHK_ACL(aclrtFreeHost(buffer_));
    } else {
      LLM_CHK_ACL(aclrtFree(buffer_));
    }
    buffer_ = nullptr;
  }
}

void RegBufferPool::Finalize() {
  if (handle_ != nullptr) {
    (void) GlobalMemManager::GetInstance().UnregisterMem(handle_);
    handle_ = nullptr;
  }
}

ge::Status RegBufferPool::Alloc(void *&buffer) {
  std::lock_guard<std::mutex> lock(mutex_);
  ge::Status ret = ge::FAILED;
  for (auto &it : reg_buffers_) {
    if (it.second == false) {
      buffer = it.first;
      it.second = true;
      ret = ge::SUCCESS;
      break;
    }
  }
  return ret;
}

void RegBufferPool::Free(void *buffer) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = reg_buffers_.find(buffer);
  if (it != reg_buffers_.end()) {
    it->second = false;
  }
}

EntityMemInfo::EntityMemInfo(bool remote_cache_accessible, 
                             RegBufferPool *host_reg_pool,
                             RegBufferPool *device_reg_pool)
    : remote_cache_accessible_(remote_cache_accessible),
      msg_buffer_(nullptr),
      req_(nullptr),
      transfer_buffer_(nullptr),
      host_transfer_buffer_(nullptr),
      resp_(nullptr),
      host_transfer_req_(nullptr),
      host_transfer_resp_(nullptr),
      transfer_req_(nullptr),
      transfer_resp_(nullptr),
      host_reg_pool_(host_reg_pool),
      device_reg_pool_(device_reg_pool) {};

ge::Status EntityMemInfo::Initialize() {
  if (remote_cache_accessible_) {
    LLM_CHK_ACL_RET(aclrtMallocHost(&msg_buffer_, kDefaultMsgBufferSize));
  } else {
    LLM_CHECK_NOTNULL(host_reg_pool_);
    LLM_CHECK_NOTNULL(device_reg_pool_);
    LLM_CHK_ACL_RET(aclrtMallocHost(&host_transfer_buffer_, kDefaultMsgBufferSize));
    host_transfer_req_ = host_transfer_buffer_;
    host_transfer_resp_ = static_cast<uint8_t *>(host_transfer_buffer_) + kDefaultReqBufferSize;
    LLM_CHK_STATUS_RET(host_reg_pool_->Alloc(msg_buffer_), "Failed to alloc host reg buffer");
    LLM_CHK_STATUS_RET(device_reg_pool_->Alloc(transfer_buffer_), "Failed to alloc dev reg buffer");
    transfer_req_ = transfer_buffer_;
    transfer_resp_ = static_cast<uint8_t *>(transfer_buffer_) + kDefaultReqBufferSize;
  }
  req_ = msg_buffer_;
  resp_ = static_cast<uint8_t *>(msg_buffer_) + kDefaultReqBufferSize;
  LLMLOGI("Mem info init success, remote_cache_accessible:%d", static_cast<int32_t>(remote_cache_accessible_));
  return ge::SUCCESS;
}

EntityMemInfo::~EntityMemInfo() {
  if (host_transfer_buffer_ != nullptr) {
    LLM_CHK_ACL(aclrtFreeHost(host_transfer_buffer_));
    host_transfer_buffer_ = nullptr;
  }
  if (remote_cache_accessible_) {
    if (msg_buffer_ != nullptr) {
      LLM_CHK_ACL(aclrtFreeHost(msg_buffer_));
    }
    msg_buffer_ = nullptr;
  } else {
    host_reg_pool_->Free(msg_buffer_);
    device_reg_pool_->Free(transfer_buffer_);
  }
}

EntityCommInfo::EntityCommInfo(const CommParams &comm_params)
    : params_(comm_params), comm_{}, comm_inited_(false) {};

EntityCommInfo::EntityCommInfo(const HcclComm &comm, std::vector<void *> mem_handles, int32_t link_total_time, int32_t link_retry_count)
    : params_({0, {}, "", mem_handles, link_total_time, link_retry_count}), comm_(comm), comm_inited_(true) {};

ge::Status EntityCommInfo::Initialize() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!comm_inited_) {
    std::lock_guard<std::mutex> g_lock(g_mutex_);
    HcclResult ret = HcclAdapter::GetInstance().HcclCommInitClusterInfoMemConfig(params_.rank_table.c_str(),
                                                                                 params_.rank_id,
                                                                                 &params_.comm_config,
                                                                                 &comm_);
    LLM_CHK_BOOL_RET_STATUS(ret == HcclResult::HCCL_SUCCESS, ge::LLM_LINK_FAILED,
                          "Call HcclCommInitClusterInfoMemConfig failed, ret:%d.", ret);
    comm_inited_ = true;
  }

  LLM_DISMISSABLE_GUARD(fail_guard, ([this]() {
    for (auto bind_handle : bind_handles_) {
      (void) HcclAdapter::GetInstance().HcclCommUnbindMem(comm_, bind_handle);
    }
    bind_handles_.clear();
    (void) HcclAdapter::GetInstance().HcclCommDestroy(comm_);
    comm_inited_ = false;
  }));

  for (auto reg_handle : params_.mem_handles) {
    HcclResult bind_ret = HcclAdapter::GetInstance().HcclCommBindMem(comm_, reg_handle);
    LLM_CHK_BOOL_RET_STATUS(bind_ret == HcclResult::HCCL_SUCCESS, ge::LLM_LINK_FAILED,
                          "Call HcclCommBindMem failed, ret:%d.", bind_ret);
    bind_handles_.emplace_back(reg_handle);
  }

  LLM_CHK_STATUS_RET(PrepareHcclComm(), "Failed to prepare hccl comm");
  LLM_CHK_BOOL_RET_STATUS(params_.timeout >= 0, ge::LLM_PARAM_INVALID, 
                         "timeout should be greater than or equal 0, given value is %d", params_.timeout);
  LLM_CHK_BOOL_RET_STATUS(params_.link_retry_count >= kRetryCountMin && params_.link_retry_count <= kRetryCountMax,
                         ge::LLM_PARAM_INVALID,
                         "link_retry_count should be an integer between [1, 10], given value is %d", params_.link_retry_count);
  LLM_DISMISS_GUARD(fail_guard);
  return ge::SUCCESS;
}

ge::Status EntityCommInfo::PrepareHcclComm() const {
  const auto start = std::chrono::steady_clock::now();
  HcclPrepareConfig prepareConfig{};
  int32_t avg_timeout = params_.timeout / params_.link_retry_count;
  HcclResult prepare_ret = HcclResult::HCCL_SUCCESS;
  for (int32_t i = 0; i < params_.link_retry_count; i++) {
    prepare_ret = HcclAdapter::GetInstance().HcclCommPrepare(comm_, &prepareConfig, avg_timeout);
    if (prepare_ret != HcclResult::HCCL_SUCCESS && (!stop_flag_)) {
      LLMEVENT("Retrying, there will be a total of %d retries, this time is %d, returned value this time:%d; " 
              "the hccl logs during the calling of HcclCommPrepare from current thread could be ignored " 
              "if HcclCommPrepare finally succeeds.", 
              params_.link_retry_count, i + 1, prepare_ret);
    } else {
      break;
    }
  }
  auto cost = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
  LLM_CHK_BOOL_RET_STATUS(prepare_ret == HcclResult::HCCL_SUCCESS,
                         HcclUtils::ConvertHcclErrorCode(prepare_ret, ge::LLM_LINK_FAILED),
                        "Call HcclCommPrepare failed, ret:%d, cost:%ld ms.", prepare_ret, cost);
  LLMLOGI("HcclCommPrepare success, cost=%ld ms.", cost);
  return ge::SUCCESS;
}

ge::Status EntityCommInfo::Finalize() {
  stop_flag_ = true;
  LLMEVENT("Begin to finalize comm info.");
  std::lock_guard<std::mutex> lock(mutex_);
  if (!comm_inited_) {
    return ge::SUCCESS;
  }
  auto ret = ge::SUCCESS;
  for (auto bind_handle : bind_handles_) {
    auto hccl_ret = HcclAdapter::GetInstance().HcclCommUnbindMem(comm_, bind_handle);
    ret = hccl_ret != HcclResult::HCCL_SUCCESS ? ge::LLM_UNLINK_FAILED : ret;
  }
  bind_handles_.clear();
  auto hccl_ret = HcclAdapter::GetInstance().HcclCommDestroy(comm_);
  comm_inited_ = false;
  ret = hccl_ret != HcclResult::HCCL_SUCCESS ? ge::LLM_UNLINK_FAILED : ret;
  return ret;
}

EntityCommInfo::~EntityCommInfo() {
  (void) Finalize();
}

CommEntity::CommEntity(uint64_t comm_id, uint64_t cluster_id, uint32_t rank_id,
                       uint64_t local_cluster_id, uint32_t local_rank_id)
    : comm_id_(comm_id),
      cluster_id_(cluster_id),
      rank_id_(rank_id),
      local_cluster_id_(local_cluster_id),
      local_rank_id_(local_rank_id),
      stream_(nullptr) {
  std::stringstream ss;
  ss << "local:" << local_cluster_id_ << "_" << local_rank_id_ << " ";
  ss << "remote:" << cluster_id_ << "_" << rank_id_;
  desc_ = ss.str();
}

ge::Status CommEntity::Initialize(bool remote_cache_accessible) {
  LLMLOGI("comm entity init begin, remote_cache_accessible:%d", static_cast<int32_t>(remote_cache_accessible));
  LLM_CHK_STATUS_RET(cache_access_table_.Initialize(remote_cache_accessible));
  LLM_ASSERT_RT_OK(aclrtCreateStreamWithConfig(&stream_, 0,
                  ACL_STREAM_FAST_LAUNCH | ACL_STREAM_FAST_SYNC));
  LLMLOGI("Entity:%s initialize success, stream:%p, remote_cache_accessible:%d",
         desc_.c_str(), stream_, static_cast<int32_t>(remote_cache_accessible));
  return ge::SUCCESS;
}

ge::Status CommEntity::Finalize(bool force) {
  (void) force;
  auto ret = ge::SUCCESS;
  if (GetStream() != nullptr) {
    auto aclrt_ret = aclrtStreamAbort(GetStream());
    LLMLOGI("Call aclrtStreamAbort ret:%d.", ret);
    ret = aclrt_ret != ACL_ERROR_NONE ? ge::LLM_UNLINK_FAILED : ret;
  }

  if (comm_info_ptr_ != nullptr) {
    comm_info_ptr_.reset();
    comm_info_ptr_ = nullptr;
  }

  if (GetStream() != nullptr) {
    auto aclrt_ret = aclrtDestroyStream(GetStream());
    LLMLOGI("Call aclrtDestroyStream ret:%d.", ret);
    ret = aclrt_ret != ACL_ERROR_NONE ? ge::LLM_UNLINK_FAILED : ret;
  }
  stream_ = nullptr;
  return ret;
}

CommEntity::~CommEntity() {
  if (mem_info_ptr_ != nullptr) {
    aclrtSetCurrentContext(aclrt_context_);
    mem_info_ptr_.reset();
    mem_info_ptr_ = nullptr;
  }
  Dump();
}

const std::string &CommEntity::GetDesc() const {
  return desc_;
}

void CommEntity::MarkEntityDestroyed() {
  cur_state_ = FsmState::FSM_DESTROYED_STATE;
}

void CommEntity::MarkEntityError() {
  cur_state_ = FsmState::FSM_ERROR_STATE;
}

void CommEntity::MarkEntityIdle() {
  cur_state_ = FsmState::FSM_IDLE_STATE;
  ClearReqFlag();
}

ge::Status CommEntity::ProcessState() {
  const auto state = StateManager::GetInstance().GetState(cur_state_);
  LLM_CHK_BOOL_RET_STATUS(state != nullptr, ge::FAILED, "Failed to get state:%s, entity:%s",
                         StateManager::GetInstance().GetStateDesc(cur_state_).c_str(), desc_.c_str());
  return state->Process(*this);
}

ge::Status CommEntity::ChangeState(FsmState next_state) {
  cur_state_ = next_state;
  const auto state = StateManager::GetInstance().GetState(next_state);
  LLM_CHK_BOOL_RET_STATUS(state != nullptr, ge::FAILED, "Failed to get state:%s, entity:%s",
                         StateManager::GetInstance().GetStateDesc(next_state).c_str(), desc_.c_str());
  return state->Preprocess(*this);
}

int8_t *CommEntity::GetCacheInfoFlag() const {
  return PtrToPtr<void, int8_t>(info_.local_req_flag_ptr);
}

void CommEntity::ClearReqFlag() const {
  int8_t *ptr = PtrToPtr<void, int8_t>(info_.local_req_flag_ptr);
  *ptr = 0;
}

ge::Status CommEntity::SetInfo() {
  auto transfer_func = [this](void *remote, void *local, size_t size, int32_t timeout) -> ge::Status {
    std::list<HcclOneSideOpDesc> op_descs;
    op_descs.emplace_back(HcclOneSideOpDesc{local, remote, size, HCCL_DATA_TYPE_UINT8});
    LLM_CHK_STATUS_RET(BatchTransfer(op_descs, false, false, timeout), "Failed to batch get");
    return ge::SUCCESS;
  };

  auto remote_cache_accessible = (GetTransferBuffer() == nullptr);
  LLM_CHK_BOOL_RET_STATUS((!remote_mems_.empty()), ge::LLM_LINK_FAILED, "remote mem num is 0.");
  cache_access_table_.SetTransferFunc(transfer_func, remote_mems_[0U].addr);
  LLM_CHK_STATUS_RET(GetCacheAccessTable().CheckRemoteFlag(remote_cache_accessible),
                    "Check remote flag failed, entity:%s", GetDesc().c_str());

  /**
   * receive area      send area
   * ----------------|-----------
   * flag content    |flag content
   */
  // receive area
  info_.local_req_flag_ptr = mem_info_ptr_->req_;
  info_.local_req_ptr = static_cast<uint8_t *>(mem_info_ptr_->req_) + kFlagSize;
  info_.local_resp_flag_ptr = mem_info_ptr_->resp_;
  info_.local_resp_ptr = static_cast<uint8_t *>(mem_info_ptr_->resp_) + kFlagSize;

  // send host area
  info_.send_buffer_req_flag_ptr = static_cast<uint8_t *>(mem_info_ptr_->host_transfer_req_);
  info_.send_buffer_req_ptr = info_.send_buffer_req_flag_ptr + kFlagSize;
  info_.send_buffer_resp_flag_ptr = static_cast<uint8_t *>(mem_info_ptr_->host_transfer_resp_);
  info_.send_buffer_resp_ptr = info_.send_buffer_resp_flag_ptr + kFlagSize;

  // send device area
  info_.send_dev_buffer_req_flag_ptr = static_cast<uint8_t *>(mem_info_ptr_->transfer_req_);
  info_.send_dev_buffer_req_ptr = info_.send_dev_buffer_req_flag_ptr + kFlagSize;
  info_.send_dev_buffer_resp_flag_ptr = static_cast<uint8_t *>(mem_info_ptr_->transfer_resp_);
  info_.send_dev_buffer_resp_ptr = info_.send_dev_buffer_resp_flag_ptr + kFlagSize;

  if (!remote_cache_accessible) {  // not remote_cache_accessible mode
    LLM_CHK_STATUS_RET(SetRemoteAddresses());
  }
  is_exchanged_mem_ = true;
  return ge::SUCCESS;
}

ge::Status CommEntity::SetRemoteAddresses() {
  constexpr size_t kIndexRemoteReq = 1U;
  constexpr size_t kIndexRemoteResp = 2U;
  LLM_CHK_BOOL_RET_STATUS(remote_mems_.size() >= kMinRemoteMemSize, ge::LLM_LINK_FAILED,
                         "remote mem num:%zu, expected min:%zu.", remote_mems_.size(), kMinRemoteMemSize);
  LLM_CHK_BOOL_RET_STATUS(
      (remote_mems_[kIndexRemoteReq].type == HcclMemType::HCCL_MEM_TYPE_HOST) &&
          (remote_mems_[kIndexRemoteReq].size == kDefaultReqBufferSize),
      ge::LLM_LINK_FAILED, "Remote mem type:%d, size:%llu is not valid.",
      remote_mems_[kIndexRemoteReq].type, remote_mems_[kIndexRemoteReq].size);
  LLM_CHK_BOOL_RET_STATUS(
      (remote_mems_[kIndexRemoteResp].type == HcclMemType::HCCL_MEM_TYPE_HOST) &&
          (remote_mems_[kIndexRemoteResp].size == kDefaultRespBufferSize),
      ge::LLM_LINK_FAILED, "Remote mem type:%d, size:%llu is not valid.",
      remote_mems_[kIndexRemoteResp].type, remote_mems_[kIndexRemoteResp].size);

  // only need remote receive area
  // no need check index here.
  info_.remote_flag_ptr = remote_mems_[kIndexRemoteReq].addr;
  info_.remote_req_ptr = ValueToPtr(PtrToValue(info_.remote_flag_ptr) + kFlagSize);
  info_.remote_resp_flag_ptr = remote_mems_[kIndexRemoteResp].addr;
  info_.remote_resp_ptr = ValueToPtr(PtrToValue(info_.remote_resp_flag_ptr) + kFlagSize);
  return ge::SUCCESS;
}

bool CommEntity::CheckEntityInfo() const {
  return is_exchanged_mem_;
}

void *CommEntity::GetReq() {
  return mem_info_ptr_->req_;
}

void *CommEntity::GetResp() {
  return mem_info_ptr_->resp_;
}

aclrtStream CommEntity::GetStream() const {
  return stream_;
}

std::vector<HcclMem> &CommEntity::GetRemoteMems() {
  return remote_mems_;
}

uint64_t CommEntity::GetClusterId() const {
  return cluster_id_;
}

uint64_t CommEntity::GetCommId() const {
  return comm_id_;
}

HcclComm CommEntity::GetComm() const {
  HcclComm default_comm{};
  return comm_info_ptr_ != nullptr ? comm_info_ptr_->comm_ : default_comm;
}

const EntityInfo &CommEntity::GetEntityInfo() const {
  return info_;
}

void CommEntity::SetCacheManager(CacheManager *cache_manager) {
  cache_manager_ = cache_manager;
}

CacheManager *CommEntity::GetCacheManager() const {
  return cache_manager_;
}

FsmState CommEntity::GetCurState() const {
  return cur_state_;
}

aclrtContext CommEntity::GetCurrentContext() const {
  return aclrt_context_;
}

void CommEntity::SetContext(aclrtContext context) {
  aclrt_context_ = context;
}

void CommEntity::SetEntityMemInfo(EntityMemInfoPtr &mem_info) {
  mem_info_ptr_ = std::move(mem_info);
}

void CommEntity::SetEntityCommInfo(EntityCommInfoPtr comm_info) {
  comm_info_ptr_ = comm_info;
}

ge::Status CommEntity::BatchPutAsync(std::vector<HcclOneSideOpDesc> &op_descs, aclrtStream stream) {
  auto stream_to_use = stream != nullptr ? stream : stream_;
  const auto start = std::chrono::steady_clock::now();
  auto ret = HcclAdapter::GetInstance().HcclBatchPut(GetComm(), rank_id_, op_descs.data(),
                                                     op_descs.size(), stream_to_use);
  LLM_CHK_BOOL_RET_STATUS(ret == HCCL_SUCCESS, ge::FAILED, "Failed to invoke HcclBatchPut, ret = %d",
                         static_cast<int32_t>(ret));
  const auto end = std::chrono::steady_clock::now();
  const auto cost = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
  LLMLOGI("HcclBatchPut success, num = %zu, cost = %ld us.", op_descs.size(), cost);
  auto &send_statistic_info = GetSendStatisticInfo(stream_to_use);
  StatisticManager::GetInstance().UpdateCost(
      cost, send_statistic_info.batch_put_times, send_statistic_info.batch_put_min_cost,
      send_statistic_info.batch_put_max_cost, send_statistic_info.batch_put_total_cost);
  send_statistic_info.send_total_num += op_descs.size();
  return ge::SUCCESS;
}

SendStatisticInfo &CommEntity::GetSendStatisticInfo(aclrtStream stream) {
  auto stream_to_use = stream != nullptr ? stream : stream_;
  std::lock_guard<std::mutex> lk(info_mutex_);
  const auto &iter = send_statistic_infos_.find(stream_to_use);
  if (iter != send_statistic_infos_.cend()) {
    return iter->second;
  }
  SendStatisticInfo send_statistic_info{};
  send_statistic_infos_.emplace(stream_to_use, send_statistic_info);
  return send_statistic_infos_[stream_to_use];
}

ge::Status CommEntity::BatchGetAsync(std::vector<HcclOneSideOpDesc> &op_descs, aclrtStream stream) {
  auto stream_to_use = stream != nullptr ? stream : stream_;
  const auto start = std::chrono::steady_clock::now();
  auto ret = HcclAdapter::GetInstance().HcclBatchGet(GetComm(), rank_id_, op_descs.data(),
                                                     op_descs.size(), stream_to_use);
  LLM_CHK_BOOL_RET_STATUS(ret == HCCL_SUCCESS,
                         HcclUtils::ConvertHcclErrorCode(ret),
                         "Failed to invoke HcclBatchGet, hccl_result = %d",
                         static_cast<int32_t>(ret));
  const auto end = std::chrono::steady_clock::now();
  const auto cost = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
  LLMLOGI("HcclBatchGet success, num = %zu, cost = %ld us.", op_descs.size(), cost);;
  StatisticManager::UpdateCost(
      cost, recv_statistic_info_.batch_get_times, recv_statistic_info_.batch_get_min_cost,
      recv_statistic_info_.batch_get_max_cost, recv_statistic_info_.batch_get_total_cost);
  recv_statistic_info_.get_total_num += op_descs.size();
  return ge::SUCCESS;
}

ge::Status CommEntity::BatchTransfer(std::list<HcclOneSideOpDesc> &tasks, bool is_put, bool reversed, int32_t timeout_ms) {
  LLM_DISMISSABLE_GUARD(stream, [this]() -> void {
    LLM_CHK_ACL(aclrtStreamAbort(stream_));
  });
  const auto start = std::chrono::steady_clock::now();
  BufferedSender buffered_sender;
  buffered_sender.Initialize(*this, stream_, is_put);
  auto task_num = tasks.size();
  LLMLOGI("task num = %zu", task_num);
  while (!tasks.empty()) {
    auto op_desc = tasks.front();
    tasks.pop_front();
    // local, remote is reversed in get mode
    if (reversed) {
      std::swap(op_desc.localAddr, op_desc.remoteAddr);
    }
    LLM_CHK_STATUS_RET(buffered_sender.Put(op_desc.localAddr, op_desc.remoteAddr, op_desc.count));
  }
  LLM_CHK_STATUS_RET(buffered_sender.Flush());
  const auto ret = aclrtSynchronizeStreamWithTimeout(stream_, timeout_ms);
  LLM_CHK_BOOL_RET_STATUS(ret == ACL_ERROR_NONE,
                         llm::ConvertAclError2Ge(ret),
                         "Failed to sync stream, rt_ret = %d", ret);
  const auto end = std::chrono::steady_clock::now();
  const auto cost = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
  LLMLOGI("sync stream success, cost = %ld us.", cost);
  if (is_put) {
    auto &send_statistic_info = GetSendStatisticInfo(stream_);
    StatisticManager::GetInstance().UpdateCost(
        cost, send_statistic_info.batch_put_times, send_statistic_info.batch_put_min_cost,
        send_statistic_info.batch_put_max_cost, send_statistic_info.batch_put_total_cost);
    send_statistic_info.send_total_num += task_num;
  } else {
    auto &recv_statistic_info = GetRecvStatisticInfo();
    StatisticManager::UpdateCost(cost, recv_statistic_info.pull_times, recv_statistic_info.pull_min_cost,
                                recv_statistic_info.pull_max_cost, recv_statistic_info.pull_total_cost);
  }
  LLM_DISMISS_GUARD(stream);
  return ge::SUCCESS;
}

RecvStatisticInfo &CommEntity::GetRecvStatisticInfo() {
  return recv_statistic_info_;
}

void CommEntity::Dump() const {
  for (const auto &send_statistic_info : send_statistic_infos_) {
    const uint64_t batch_put_avg_time =
        send_statistic_info.second.batch_put_times == 0U
            ? 0U
            : send_statistic_info.second.batch_put_total_cost / send_statistic_info.second.batch_put_times;
    const uint64_t event_record_avg_time =
        send_statistic_info.second.event_record_times == 0U
            ? 0U
            : send_statistic_info.second.event_record_total_cost / send_statistic_info.second.event_record_times;
    const uint64_t send_avg_time =
        send_statistic_info.second.send_times == 0U
            ? 0U
            : send_statistic_info.second.send_total_cost / send_statistic_info.second.send_times;
    LLMEVENT(
        "Comm entity info:desc[%s], Send statistic info: batch put info[times:%lu, max:%lu us, min:%lu us, "
        "avg_cost:%lu us], "
        "send info [total num:%lu, send times:%lu, max:%lu us, min:%lu us, avg_cost:%lu us], "
        "event record info[times:%lu, avg_cost:%lu us], sync flag put times:%lu, req info put times:%lu. ",
        desc_.c_str(), send_statistic_info.second.batch_put_times, send_statistic_info.second.batch_put_max_cost,
        send_statistic_info.second.batch_put_min_cost, batch_put_avg_time, send_statistic_info.second.send_total_num,
        send_statistic_info.second.send_times, send_statistic_info.second.send_max_cost,
        send_statistic_info.second.send_min_cost, send_avg_time, send_statistic_info.second.event_record_times,
        event_record_avg_time, send_statistic_info.second.sync_flag_put_times,
        send_statistic_info.second.req_info_put_times);
  }

  const uint64_t batch_get_avg_time =
      recv_statistic_info_.batch_get_times == 0U
      ? 0U
      : recv_statistic_info_.batch_get_total_cost / recv_statistic_info_.batch_get_times;
  const uint64_t pull_avg_time =
      recv_statistic_info_.pull_times == 0U
      ? 0U
      : recv_statistic_info_.pull_total_cost / recv_statistic_info_.pull_times;
  LLMEVENT("Comm entity info:desc[%s] Recv statistic info:req_info get time:%lu, sync_flag get time:%lu, "
          "HcclBatchGet [times:%lu, max:%lu us, min:%lu us, avg_cost:%lu us], "
          "get info [total num:%lu, get times:%lu, max:%lu us, min:%lu us, avg_cost:%lu us]", desc_.c_str(),
          recv_statistic_info_.req_info_get_times, recv_statistic_info_.sync_flag_get_times,
          recv_statistic_info_.batch_get_times, recv_statistic_info_.batch_get_max_cost,
          recv_statistic_info_.batch_get_min_cost, batch_get_avg_time,
          recv_statistic_info_.pull_times, recv_statistic_info_.get_total_num, recv_statistic_info_.pull_max_cost,
          recv_statistic_info_.pull_min_cost, pull_avg_time);
}

ge::Status CommEntity::SendRequest(const FillRequestFunc &fill_request_func, aclrtStream stream) {
  uint64_t req_size = 0U;
  auto &req_info = *PtrToPtr<void, TransferCacheReq>(info_.send_buffer_req_ptr);
  fill_request_func(req_info, req_size);
  auto *local_sync_flag_ptr = PtrToPtr<void, int8_t>(info_.send_buffer_req_flag_ptr);
  *local_sync_flag_ptr = 1;
  LLM_CHK_ACL_RET(aclrtMemcpyAsync(info_.send_dev_buffer_req_flag_ptr,
                                   kDefaultReqBufferSize,
                                   info_.send_buffer_req_flag_ptr,
                                   kFlagSize + req_size,
                                   ACL_MEMCPY_HOST_TO_DEVICE,
                                   stream));
  std::vector<HcclOneSideOpDesc> request_desc{
      HcclOneSideOpDesc{info_.send_dev_buffer_req_ptr, info_.remote_req_ptr, req_size, HCCL_DATA_TYPE_UINT8}};
  LLM_CHK_STATUS_RET(BatchPutAsync(request_desc, stream), "put request to remote_cluster[%lu] failed", cluster_id_);
  std::vector<HcclOneSideOpDesc> flag_desc{
      HcclOneSideOpDesc{info_.send_dev_buffer_req_flag_ptr, info_.remote_flag_ptr, 1, HCCL_DATA_TYPE_UINT8}};
  LLM_CHK_STATUS_RET(BatchPutAsync(flag_desc, stream), "put flag to remote_cluster[%lu] failed", cluster_id_);
  auto &send_statistic_info = GetSendStatisticInfo(stream);
  ++send_statistic_info.req_info_put_times;
  return ge::SUCCESS;
}

ge::Status CommEntity::SendResponse(ge::Status status) {
  return SendResponse([status](ResponseInfo &response_info, uint64_t &size) -> void {
    response_info.ret_code = static_cast<int32_t>(status);
    size = sizeof(ResponseInfo);
  });
}

ge::Status CommEntity::SendResponse(const FillResponseFunc &fill_response_func) {
  uint64_t resp_size = 0U;
  auto &resp_info = *PtrToPtr<void, ResponseInfo>(info_.send_buffer_resp_ptr);
  fill_response_func(resp_info, resp_size);
  auto *local_sync_flag_ptr = PtrToPtr<void, int8_t>(info_.send_buffer_resp_flag_ptr);
  *local_sync_flag_ptr = 1;
  LLM_CHK_ACL_RET(aclrtMemcpyAsync(info_.send_dev_buffer_resp_flag_ptr,
                                   kDefaultRespBufferSize,
                                   info_.send_buffer_resp_flag_ptr,
                                   kFlagSize + resp_size,
                                   ACL_MEMCPY_HOST_TO_DEVICE,
                                   stream_));
  std::vector<HcclOneSideOpDesc> response_desc{
      HcclOneSideOpDesc{info_.send_dev_buffer_resp_ptr, info_.remote_resp_ptr, resp_size, HCCL_DATA_TYPE_UINT8}};
  LLM_CHK_STATUS_RET(BatchPutAsync(response_desc), "put response to remote_cluster[%lu] failed", cluster_id_);

  std::vector<HcclOneSideOpDesc> flag_desc{
      HcclOneSideOpDesc{info_.send_dev_buffer_resp_flag_ptr, info_.remote_resp_flag_ptr, 1, HCCL_DATA_TYPE_UINT8}};
  LLM_CHK_STATUS_RET(BatchPutAsync(flag_desc), "put flag to remote_cluster[%lu] failed", cluster_id_);
  auto &send_statistic_info = GetSendStatisticInfo(stream_);
  send_statistic_info.sync_flag_put_times++;
  return ge::SUCCESS;
}

ge::Status CommEntity::GetResponse(const ResponseInfo *&response_info,
                                   const std::chrono::steady_clock::time_point *end_time_point) const {
  LLM_CHK_BOOL_RET_STATUS(SyncFlag(info_.local_resp_flag_ptr).Wait(end_time_point) != 0, ge::LLM_TIMEOUT,
                         "wait resp flag timeout");
  LLMLOGI("response received");
  response_info = PtrToPtr<void, ResponseInfo>(info_.local_resp_ptr);
  return ge::SUCCESS;
}

const TransferCacheReq &CommEntity::GetRequest() const {
  return *PtrToPtr<void, TransferCacheReq>(info_.local_req_ptr);
}

const std::unique_ptr<DataTransferJob> &CommEntity::GetDataTransferJob() const {
  return data_transfer_job_;
}

void CommEntity::SetDataTransferJob(std::unique_ptr<DataTransferJob> &&data_transfer_job) {
  data_transfer_job_ = std::move(data_transfer_job);
}

void CommEntity::SetTimeoutPoint(const std::chrono::steady_clock::time_point &timeout_point) {
  timeout_point_ = timeout_point;
}

const std::chrono::steady_clock::time_point &CommEntity::GetTimeoutPoint() const {
  return timeout_point_;
}

void CommEntity::ClearResponseFlags() {
  *PtrToPtr<void, int32_t>(info_.local_resp_flag_ptr) = 0;
}

const std::pair<uint64_t, uint64_t> &CommEntity::GetCacheKeyToRemove() const {
  return cache_key_to_remove_;
}

void CommEntity::SetCacheKeyToRemove(const std::pair<uint64_t, uint64_t> &cache_key_to_remove) {
  cache_key_to_remove_ = cache_key_to_remove;
}

std::mutex &CommEntity::GetPullMutex() {
  return pull_mutex_;
}

std::mutex &CommEntity::GetProcessMutex() {
  return process_mutex_;
}

void *CommEntity::GetTransferBuffer() const {
  return mem_info_ptr_->transfer_buffer_;
}

void BufferedSender::Initialize(CommEntity &comm_entity, aclrtStream stream, bool put_or_get) {
  comm_entity_ = &comm_entity;
  stream_ = stream;
  put_or_get_ = put_or_get;
  op_descs_.reserve(kMaxOpDescNum);
}

ge::Status BufferedSender::Put(void *local_addr, void *remote_addr, size_t size, bool flush) {
  HcclOneSideOpDesc desc{};
  desc.dataType = HCCL_DATA_TYPE_UINT8;
  desc.localAddr = local_addr;
  desc.remoteAddr = remote_addr;
  desc.count = size;
  op_descs_.emplace_back(desc);
  if (flush || (op_descs_.size() == op_descs_.capacity())) {
    return Flush();
  }
  return ge::SUCCESS;
}

ge::Status BufferedSender::Flush() {
  if (!op_descs_.empty()) {
    if (put_or_get_) {
      auto ret = comm_entity_->BatchPutAsync(op_descs_, stream_);
      LLM_CHK_STATUS_RET(ret, "Failed to invoke HcclBatchPut");
      LLMLOGI("BatchPut success, buffer_num = %zu", op_descs_.size());
    } else {
      auto ret = comm_entity_->BatchGetAsync(op_descs_, stream_);
      LLM_CHK_STATUS_RET(ret, "Failed to invoke HcclBatchGet");
      LLMLOGI("BatchGet success, buffer_num = %zu", op_descs_.size());
    }
    op_descs_.clear();
  }
  return ge::SUCCESS;
}

CacheAccessTable &CommEntity::GetCacheAccessTable() {
  return cache_access_table_;
}
}  // namespace llm
