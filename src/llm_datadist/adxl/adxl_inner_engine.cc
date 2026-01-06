/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "adxl_inner_engine.h"
#include "runtime/rt.h"
#include "hccl/hccl_adapter.h"
#include "common/llm_utils.h"
#include "common/llm_scope_guard.h"
#include "common/llm_checker.h"
#include "statistic_manager.h"
#include "llm_datadist_timer.h"
#include "adxl_utils.h"

namespace adxl {
namespace {
constexpr uint64_t kBufferConfigSize = 2U;
constexpr uint64_t kBaseBufferSize = 1024 * 1024U;
constexpr size_t kDefaultPageShift = 16U;
constexpr uint64_t kDefaultBufferNum = 4U;
constexpr uint64_t kDefaultBufferSize = 8U;
constexpr const char *kDisabledPoolConfig = "0:0";
constexpr uint64_t kNeedUseBufferThresh = 256 * 1024U;
constexpr size_t kMemPoolNum = 2U;
constexpr uint32_t kCheckDisconnetPeriod = 10U; // ms
constexpr int32_t kConnectWhenTransferTimeout = 3000; // ms
constexpr size_t kMaxStreams = 512;
}

Status AdxlInnerEngine::ParseWaterlineRatio(const std::map<AscendString, AscendString>& json_options, 
                                            const char* option_name, double& value) {
  auto option_it = json_options.find(option_name);
  if (option_it != json_options.end()) {
    ADXL_CHK_LLM_RET(llm::LLMUtils::ToNumber(option_it->second.GetString(), value), 
                    "Invalid %s: %s", option_name, option_it->second.GetString());
    ADXL_CHK_BOOL_RET_STATUS(value > 0.0 && value < 1.0, PARAM_INVALID, 
                            "Invalid %s: %.2f (must be 0~1)", option_name, value);
  }
  return SUCCESS;
}

Status AdxlInnerEngine::LoadGlobalResourceConfig(const std::map<AscendString, AscendString>& options) {
  auto config_it = options.find(hixl::OPTION_GLOBAL_RESOURCE_CONFIG);
  std::map<AscendString, AscendString> json_options;
  if (config_it != options.end()) {
    ADXL_CHK_STATUS_RET(LoadJsonConfig(config_it->second.GetString(), json_options), 
                        "Failed to load JSON config from file: %s", config_it->second.GetString());
  }

  int32_t max_channel = kDefaultMaxChannel;
  auto max_it = json_options.find(adxl::OPTION_MAX_CHANNEL);
  if (max_it != json_options.end()) {
    ADXL_CHK_LLM_RET(llm::LLMUtils::ToNumber(max_it->second.GetString(), max_channel), 
                    "Invalid max_channel: %s", max_it->second.GetString());
    ADXL_CHK_BOOL_RET_STATUS(max_channel > 0, PARAM_INVALID, "Invalid max_channel: %d (must be > 0)", max_channel);
    ADXL_CHK_BOOL_RET_STATUS(max_channel <= kDefaultMaxChannel, PARAM_INVALID, 
                             "Invalid max_channel: %d (must be <= %d)", max_channel, kDefaultMaxChannel);
  }

  double high_waterline_ratio = -1.0;
  ADXL_CHK_STATUS_RET(ParseWaterlineRatio(json_options, adxl::OPTION_HIGH_WATERLINE, 
                                          high_waterline_ratio), 
                      "Failed to parse high_waterline");

  double low_waterline_ratio = -1.0;
  ADXL_CHK_STATUS_RET(ParseWaterlineRatio(json_options, adxl::OPTION_LOW_WATERLINE, 
                                          low_waterline_ratio), 
                      "Failed to parse low_waterline");
  user_config_channel_pool_ = (high_waterline_ratio > 0.0 && high_waterline_ratio < 1.0) &&
                              (low_waterline_ratio > 0.0 && low_waterline_ratio < 1.0);
  if (user_config_channel_pool_) {
    const int32_t high_waterline = std::max(static_cast<int32_t>(max_channel * high_waterline_ratio), 1);
    const int32_t low_waterline = std::max(static_cast<int32_t>(max_channel * low_waterline_ratio), 1);
    ADXL_CHK_BOOL_RET_STATUS(high_waterline - low_waterline >= 1, PARAM_INVALID, 
                          "high_waterline (%.2f) must be at least 1 greater than low_waterline (%.2f) when calculated", 
                          high_waterline_ratio, low_waterline_ratio);
    msg_handler_.SetUserChannelPoolConfig();
    msg_handler_.SetHighWaterline(high_waterline);
    msg_handler_.SetLowWaterline(low_waterline);
    msg_handler_.SetMaxChannel(max_channel);
  } else {
    ADXL_CHK_BOOL_RET_STATUS(max_it == json_options.end(), PARAM_INVALID, 
                            "Max channel should be set together with high&low waterlines.");
  }
  return SUCCESS;
}

Status AdxlInnerEngine::Initialize(const std::map<AscendString, AscendString> &options) {
  std::lock_guard<std::mutex> lk(mutex_);

  ADXL_CHK_STATUS_RET(LoadGlobalResourceConfig(options), "Failed to load global resource config.");
  ADXL_CHK_LLM_RET(llm::HcclAdapter::GetInstance().Initialize(), "HcclSoManager initialize failed.");
  int32_t device_id = -1;
  ADXL_CHK_ACL_RET(rtGetDevice(&device_id));
  llm::TemporaryRtContext with_context(nullptr);
  ADXL_CHK_ACL_RET(rtCtxCreateEx(&rt_context_, 0U, device_id));
  LLMEVENT("Switch new rts ctx:%p", rt_context_);
  LLM_DISMISSABLE_GUARD(fail_guard, ([this]() {
    (void) rtCtxDestroyEx(rt_context_);
  }));
  ADXL_CHK_STATUS_RET(ParseEnableFabricMem(options), "Failed to parse option.");
  if (enable_use_fabric_mem_) {
    fabric_mem_transfer_service_ = llm::MakeUnique<FabricMemTransferService>();
    ADXL_CHK_STATUS_RET(fabric_mem_transfer_service_->Initialize(kMaxStreams),
                        "Failed to initialize fabric mem transfer service.");
  }
  segment_table_ = llm::MakeUnique<SegmentTable>();
  stream_pool_ = llm::MakeUnique<StreamPool>(kMaxStreams);
  ADXL_CHK_STATUS_RET(msg_handler_.Initialize(options, segment_table_.get(), fabric_mem_transfer_service_.get()),
                      "Failed to init msg handler.");
  ADXL_CHK_STATUS_RET(InitBufferTransferService(options), "Failed to init buffer memory pool.");
  ADXL_CHK_STATUS_RET(channel_manager_.Initialize(buffer_transfer_service_.get(), segment_table_.get()),
                      "Failed to init channel manager.");
  channel_manager_.SetStreamPool(stream_pool_.get());
  channel_manager_.RegisterNotifyAckCallback([this](uint64_t req_id) {
    std::lock_guard<std::mutex> lock(notify_mutex_);
    notify_ack_ready_[req_id] = true;
    notify_cv_.notify_all();  // Notify all waiting threads, but only the one with matching req_id will continue
  });

  llm::LlmDatadistTimer::Instance().Init();
  statistic_timer_handle_ = llm::LlmDatadistTimer::Instance().CreateTimer([this]() {
    StatisticManager::GetInstance().Dump();
  });
  constexpr uint32_t kStatisticTimerPeriod = 80U * 1000U;
  (void)llm::LlmDatadistTimer::Instance().StartTimer(statistic_timer_handle_, kStatisticTimerPeriod, false);
  is_initialized_ = true;
  StatisticManager::GetInstance().SetEnableUseFabricMem(enable_use_fabric_mem_);
  LLM_DISMISS_GUARD(fail_guard);
  return SUCCESS;
}

Status AdxlInnerEngine::ParseEnableFabricMem(const std::map<AscendString, AscendString> &options) {
  std::string enable_use_fabric_mem_str;
  const auto &it = options.find(hixl::OPTION_ENABLE_USE_FABRIC_MEM);
  if (it != options.cend()) {
    enable_use_fabric_mem_str = it->second.GetString();
  } else {
  }

  if (!enable_use_fabric_mem_str.empty()) {
    uint32_t enable_use_fabric_mem = 0U;
    ADXL_CHK_LLM_RET(llm::LLMUtils::ToNumber(enable_use_fabric_mem_str, enable_use_fabric_mem),
                     "%s is invalid, value = %s", hixl::OPTION_ENABLE_USE_FABRIC_MEM,
                     enable_use_fabric_mem_str.c_str());
    ADXL_CHK_BOOL_RET_STATUS(enable_use_fabric_mem == 1U || enable_use_fabric_mem == 0U, PARAM_INVALID,
                             "%s is invalid, should be zero or one.", hixl::OPTION_ENABLE_USE_FABRIC_MEM);
    LLMLOGI("set %s to %d.", hixl::OPTION_ENABLE_USE_FABRIC_MEM, enable_use_fabric_mem);
    enable_use_fabric_mem_ = enable_use_fabric_mem;
  }
  return SUCCESS;
}

void AdxlInnerEngine::ParseBufferPool(const std::map<AscendString, AscendString> &options,
                                      std::string &pool_config) {
  const auto &pool_it = options.find(hixl::OPTION_BUFFER_POOL);
  if (pool_it != options.cend()) {
    pool_config = pool_it->second.GetString();
  } else {
    const auto &pool_it2 = options.find(adxl::OPTION_BUFFER_POOL);
    if (pool_it2 != options.cend()) {
      pool_config = pool_it2->second.GetString();
    }
  }
}

Status AdxlInnerEngine::ParseBufferPoolParams(const std::map<AscendString, AscendString> &options,
                                              uint64_t &buffer_size, uint64_t &npu_pool_size) {
  std::string pool_config;
  ParseBufferPool(options, pool_config);
  uint64_t buffer_num;
  if (!pool_config.empty()) {
    if (pool_config == kDisabledPoolConfig) {
      LLMEVENT("Buffer pool is disabled.");
      return SUCCESS;
    }
    ADXL_CHK_BOOL_RET_STATUS(!enable_use_fabric_mem_, PARAM_INVALID,
                             "Buffer pool and fabric mem mode can not be set simultaneously");
    LLMEVENT("Buffer pool config is:%s.", pool_config.c_str());
    const auto buffer_configs = llm::LLMUtils::Split(pool_config, ':');
    ADXL_CHK_BOOL_RET_STATUS(buffer_configs.size() == kBufferConfigSize, PARAM_INVALID,
                             "Option BufferPool is invalid: %s, expect ${BUFFER_NUM}:${BUFFER_SIZE}.",
                             pool_config.c_str());
    ADXL_CHK_LLM_RET(llm::LLMUtils::ToNumber(buffer_configs[0], buffer_num), "Buffer num is invalid, value = %s.",
                     buffer_configs[0].c_str());
    ADXL_CHK_BOOL_RET_STATUS(buffer_num > 0U, PARAM_INVALID, "Buffer num should be bigger than 0.");
    auto &buffer_size_str = buffer_configs[1];
    ADXL_CHK_LLM_RET(llm::LLMUtils::ToNumber(buffer_size_str, buffer_size), "Buffer size is invalid, value = %s",
                     buffer_size_str.c_str());
    ADXL_CHK_BOOL_RET_STATUS(buffer_size > 0U, PARAM_INVALID, "Buffer size should be bigger than 0.");
    user_config_buffer_pool_ = true;
  } else {
    ADXL_CHK_BOOL_RET_SPECIAL_STATUS(enable_use_fabric_mem_, SUCCESS,
                                     "Do not set default buffer pool in fabric mem mode.");
    buffer_num = kDefaultBufferNum;
    buffer_size = kDefaultBufferSize;
  }
  ADXL_CHK_BOOL_RET_STATUS(!ge::MulOverflow(buffer_size, buffer_num, npu_pool_size), PARAM_INVALID,
                           "Buffer pool config is invalid.");
  ADXL_CHK_BOOL_RET_STATUS(!ge::MulOverflow(npu_pool_size, kBaseBufferSize, npu_pool_size), PARAM_INVALID,
                           "Buffer pool config is invalid.");
  return SUCCESS;
}

Status AdxlInnerEngine::InitBufferTransferService(const std::map<ge::AscendString, ge::AscendString> &options) {
  uint64_t buffer_size = 0U;
  uint64_t npu_pool_size = 0U;
  ADXL_CHK_STATUS_RET(ParseBufferPoolParams(options, buffer_size, npu_pool_size),
                      "Failed to parse buffer pool params.");
  ADXL_CHK_BOOL_RET_SPECIAL_STATUS(npu_pool_size == 0U, SUCCESS, "Buffer pool is disabled.");
  llm::ScalableConfig config{};
  config.page_idem_num = kDefaultPageShift;
  config.page_mem_size_total_threshold = npu_pool_size;
  npu_mem_pools_.resize(kMemPoolNum);
  npu_pool_memorys_.resize(kMemPoolNum);
  pool_mem_handles_.resize(kMemPoolNum);
  LLM_DISMISSABLE_GUARD(failed_guard, [this]() {
    for (auto &mem_handle : pool_mem_handles_) {
      if (mem_handle != nullptr) {
        msg_handler_.DeregisterMem(mem_handle);
      }
    }
    for (auto &mem : npu_pool_memorys_) {
      if (mem != nullptr) {
        rtFree(mem);
      }
    }
    npu_pool_memorys_.clear();
    pool_mem_handles_.clear();
    npu_mem_pools_.clear();
  });
  for (size_t i = 0; i < kMemPoolNum; ++i) {
    npu_mem_pools_[i] = llm::MakeUnique<llm::LlmMemPool>(config);
    ADXL_CHECK_NOTNULL(npu_mem_pools_[i], "Failed to create memory pool");
    ADXL_CHK_BOOL_RET_STATUS(rtMalloc(&npu_pool_memorys_[i], npu_pool_size,
                                      RT_MEMORY_HBM | static_cast<uint32_t>(RT_MEM_MALLOC_HUGE_FIRST),
                                      LLM_MODULE_NAME_U16) == RT_ERROR_NONE,
                             FAILED, "Failed to allocate memory for memory_pool, pool size = %lu.", npu_pool_size);
    ADXL_CHK_LLM_RET(npu_mem_pools_[i]->Initialize(npu_pool_memorys_[i], npu_pool_size),
                     "Failed to initialize memory pool, pool size = %lu.", npu_pool_size);
    MemDesc pool_mem_desc{};
    pool_mem_desc.addr = reinterpret_cast<uintptr_t>(npu_pool_memorys_[i]);
    pool_mem_desc.len = npu_pool_size;
    ADXL_CHK_STATUS_RET(msg_handler_.RegisterMem(pool_mem_desc, MemType::MEM_DEVICE, pool_mem_handles_[i]),
                        "Failed to register mem");
  }
  std::vector<llm::LlmMemPool *> mem_pools;
  for (auto &mem_pool : npu_mem_pools_) {
    mem_pools.emplace_back(mem_pool.get());
  }
  buffer_transfer_service_ = llm::MakeUnique<BufferTransferService>(mem_pools, buffer_size * kBaseBufferSize);
  ADXL_CHK_STATUS_RET(buffer_transfer_service_->Initialize(), "Failed to initialize buffer transfer service.");
  LLM_DISMISS_GUARD(failed_guard);
  LLMLOGI("Init buffer transfer service suc.");
  return SUCCESS;
}

void AdxlInnerEngine::Finalize() {
  llm::TemporaryRtContext with_context(rt_context_);
  channel_manager_.Finalize();
  msg_handler_.Finalize();
  stream_pool_->Finalize();
  for (auto &mem : npu_pool_memorys_) {
    if (mem != nullptr) {
      auto ret = rtFree(mem);
      LLMLOGI("Call rtFree ret:%d.", ret);
    }
  }
  if (buffer_transfer_service_ != nullptr) {
    buffer_transfer_service_->Finalize();
  }
  if (fabric_mem_transfer_service_ != nullptr) {
    fabric_mem_transfer_service_->Finalize();
  }
  if (rt_context_ != nullptr) {
    (void) rtCtxDestroyEx(rt_context_);
  }
  rt_context_ = nullptr;
  if (statistic_timer_handle_ != nullptr) {
    (void)llm::LlmDatadistTimer::Instance().StopTimer(statistic_timer_handle_);
    (void)llm::LlmDatadistTimer::Instance().DeleteTimer(statistic_timer_handle_);
    statistic_timer_handle_ = nullptr;
  }
  llm::LlmDatadistTimer::Instance().Finalize();
  StatisticManager::GetInstance().Reset();
}

bool AdxlInnerEngine::IsInitialized() const {
  return is_initialized_.load(std::memory_order::memory_order_relaxed);
}

Status AdxlInnerEngine::RegisterMem(const MemDesc &mem, MemType type, MemHandle &mem_handle) {
  llm::TemporaryRtContext with_context(rt_context_);
  ADXL_CHK_STATUS_RET(msg_handler_.RegisterMem(mem, type, mem_handle), "Failed to register mem");
  return SUCCESS;
}

Status AdxlInnerEngine::DeregisterMem(MemHandle mem_handle) {
  llm::TemporaryRtContext with_context(rt_context_);
  ADXL_CHK_STATUS_RET(msg_handler_.DeregisterMem(mem_handle), "Failed to deregister mem");
  return SUCCESS;
}

Status AdxlInnerEngine::Connect(const AscendString &remote_engine, int32_t timeout_in_millis) {
  if (user_config_channel_pool_) {
    std::lock_guard<std::mutex> lock(connection_mutex_);
    LLMEVENT("Start to connect, local engine:%s, remote engine:%s, timeout:%d ms.",
          local_engine_.c_str(), remote_engine.GetString(), timeout_in_millis);
    llm::TemporaryRtContext with_context(rt_context_);
    ADXL_CHK_STATUS_RET(msg_handler_.Connect(remote_engine.GetString(), timeout_in_millis),
                      "Failed to connect, remote engine:%s, timeout:%d ms",
                      remote_engine.GetString(), timeout_in_millis);
    return SUCCESS;
  }
  LLMEVENT("Start to connect, local engine:%s, remote engine:%s, timeout:%d ms.",
          local_engine_.c_str(), remote_engine.GetString(), timeout_in_millis);
  llm::TemporaryRtContext with_context(rt_context_);
  ADXL_CHK_STATUS_RET(msg_handler_.Connect(remote_engine.GetString(), timeout_in_millis),
                      "Failed to connect, remote engine:%s, timeout:%d ms",
                      remote_engine.GetString(), timeout_in_millis);
  return SUCCESS;
}

Status AdxlInnerEngine::Disconnect(const AscendString &remote_engine, int32_t timeout_in_millis) {
  llm::TemporaryRtContext with_context(rt_context_);
  ADXL_CHK_STATUS_RET(msg_handler_.Disconnect(remote_engine.GetString(), timeout_in_millis),
                      "Failed to disconnect, remote engine:%s, timeout:%d ms",
                      remote_engine.GetString(), timeout_in_millis);
  return SUCCESS;
}

Status AdxlInnerEngine::GetTransferType(const ChannelPtr &channel, TransferOp operation,
                                        const std::vector<TransferOpDesc> &op_descs, bool &need_buffer,
                                        TransferType &type) {
  ADXL_CHK_BOOL_RET_STATUS(segment_table_ != nullptr, FAILED, "Segment table is null.");
  for (size_t i = 0; i < op_descs.size(); i++) {
    auto &op_desc = op_descs[i];
    auto local_segment =
        segment_table_->FindSegment(local_engine_, op_desc.local_addr, op_desc.local_addr + op_desc.len);
    MemType local_mem_type = local_segment != nullptr ? local_segment->GetMemType() : MemType::MEM_HOST;
    auto remote_segment =
        segment_table_->FindSegment(channel->GetChannelId(), op_desc.remote_addr, op_desc.remote_addr + op_desc.len);
    MemType remote_mem_type = remote_segment != nullptr ? remote_segment->GetMemType() : MemType::MEM_HOST;
    need_buffer = need_buffer || ((local_segment == nullptr) || (remote_segment == nullptr)) ||
                  (op_desc.len < kNeedUseBufferThresh);

    TransferType cur_type;
    if (operation == TransferOp::READ) {
      if (local_mem_type == MemType::MEM_HOST && remote_mem_type == MemType::MEM_HOST) {
        cur_type = TransferType::kReadRH2H;
      } else if (local_mem_type == MemType::MEM_HOST && remote_mem_type == MemType::MEM_DEVICE) {
        cur_type = TransferType::kReadRD2H;
      } else if (local_mem_type == MemType::MEM_DEVICE && remote_mem_type == MemType::MEM_HOST) {
        cur_type = TransferType::kReadRH2D;
      } else {
        cur_type = TransferType::kReadRD2D;
      }
    } else {
      if (local_mem_type == MemType::MEM_HOST && remote_mem_type == MemType::MEM_HOST) {
        cur_type = TransferType::kWriteH2RH;
      } else if (local_mem_type == MemType::MEM_HOST && remote_mem_type == MemType::MEM_DEVICE) {
        cur_type = TransferType::kWriteH2RD;
      } else if (local_mem_type == MemType::MEM_DEVICE && remote_mem_type == MemType::MEM_HOST) {
        cur_type = TransferType::kWriteD2RH;
      } else {
        cur_type = TransferType::kWriteD2RD;
      }
    }
    LLMLOGD("Cur transfer type:%d, local mem type:%d, remote mem type:%d.", static_cast<int32_t>(cur_type),
            static_cast<int32_t>(local_mem_type), static_cast<int32_t>(remote_mem_type));
    if (i > 0) {
      ADXL_CHK_BOOL_RET_STATUS(!need_buffer || (need_buffer && cur_type == type), PARAM_INVALID,
                               "All transfer type need be same in buffer transfer mode.");
    }
    type = cur_type;
  }
  return SUCCESS;
}

Status AdxlInnerEngine::ConnectWhenTransfer(const AscendString &remote_engine, int32_t timeout_in_millis) {
  auto start_time = std::chrono::steady_clock::now();
  ChannelPtr channel;
  while (true) {
    channel = channel_manager_.GetChannel(ChannelType::kClient, remote_engine.GetString());
    if (channel == nullptr || !channel->IsDisconnecting()) {
      break;
    }
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start_time).count();
    if(elapsed >= timeout_in_millis) {
      LLMEVENT("Channel is still disconneting after timeout, remote_engine: %s, timeout: %d", 
                remote_engine.GetString(), timeout_in_millis);
      return FAILED;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(kCheckDisconnetPeriod));
  }
  if (channel == nullptr) {
    // Double-checked locking: first check without lock
    std::lock_guard<std::mutex> lock(connection_mutex_);
    // Second check with lock to avoid race conditions
    channel = channel_manager_.GetChannel(ChannelType::kClient, remote_engine.GetString());
    if (channel == nullptr) {
      LLMEVENT("Start to connect, local engine:%s, remote engine:%s, timeout:%d ms.",
          local_engine_.c_str(), remote_engine.GetString(), timeout_in_millis);
      llm::TemporaryRtContext with_context(rt_context_);
      ADXL_CHK_STATUS_RET(msg_handler_.Connect(remote_engine.GetString(), timeout_in_millis),
                          "Failed to connect, remote engine:%s, timeout:%d ms",
                          remote_engine.GetString(), timeout_in_millis);
    }
  }
  return SUCCESS;
}

Status AdxlInnerEngine::TransferSync(const AscendString &remote_engine,
                                     TransferOp operation,
                                     const std::vector<TransferOpDesc> &op_descs,
                                     int32_t timeout_in_millis) {
  llm::TemporaryRtContext with_context(rt_context_);
  if (user_config_channel_pool_) {
    (void) ConnectWhenTransfer(remote_engine, timeout_in_millis);
  }
  auto channel = channel_manager_.GetChannel(ChannelType::kClient, remote_engine.GetString());
  ADXL_CHK_BOOL_RET_STATUS(channel != nullptr, NOT_CONNECTED,
                           "Failed to get channel, remote_engine:%s", remote_engine.GetString());
  if (user_config_channel_pool_) {
    channel->SetHasTransferred(true);
    channel->IncrementTransferCount();
  }
  LLM_MAKE_GUARD(transfer_count_guard, ([&channel, this]() {
    if (user_config_channel_pool_) {
      channel->DecrementTransferCount();
    }
  }));
  if (fabric_mem_transfer_service_ != nullptr) {
    std::lock_guard<std::mutex> transfer_lock(channel->GetTransferMutex());
    return fabric_mem_transfer_service_->Transfer(channel, operation, op_descs, timeout_in_millis);
  }
  if (buffer_transfer_service_ != nullptr) {
    const auto start = std::chrono::steady_clock::now();
    bool need_buffer = false;
    TransferType type = TransferType::kEnd;
    ADXL_CHK_STATUS_RET(GetTransferType(channel, operation, op_descs, need_buffer, type),
                        "Failed to get transfer type.");
    LLMLOGI("Transfer type is:%d, cost:%lu us.", type,
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count());
    if (need_buffer) {
      ADXL_CHK_BOOL_RET_STATUS(type != TransferType::kEnd, PARAM_INVALID, "Transfer type is invalid.");
      // do not need lock, add lock inner
      return buffer_transfer_service_->Transfer(channel, type, op_descs, timeout_in_millis);
    }
  }
  std::lock_guard<std::mutex> transfer_lock(channel->GetTransferMutex());
  ADXL_CHK_STATUS_RET(channel->TransferSync(operation, op_descs, timeout_in_millis),
                      "Failed to transfer sync, remote_engine:%s", remote_engine.GetString());
  return SUCCESS;
}

Status AdxlInnerEngine::TransferAsync(const AscendString &remote_engine,
                                      TransferOp operation,
                                      const std::vector<TransferOpDesc> &op_descs,
                                      const TransferArgs &optional_args,
                                      TransferReq &req) {
  llm::TemporaryRtContext with_context(rt_context_);
  if (user_config_channel_pool_) {
    (void) ConnectWhenTransfer(remote_engine, kConnectWhenTransferTimeout);
  }
  auto channel = channel_manager_.GetChannel(ChannelType::kClient, remote_engine.GetString());
  ADXL_CHK_BOOL_RET_STATUS(channel != nullptr, NOT_CONNECTED,
                           "Failed to get channel, remote_engine:%s", remote_engine.GetString());
  auto id = next_req_id_.fetch_add(1);
  req = reinterpret_cast<void *>(static_cast<uintptr_t>(id));
  std::lock_guard<std::mutex> transfer_lock(channel->GetTransferMutex());
  if (user_config_channel_pool_) {
    channel->SetHasTransferred(true);
    channel->IncrementTransferCount();
  }
  LLM_DISMISSABLE_GUARD(transfer_count_guard, ([&channel, this]() {
    if (user_config_channel_pool_) {
      channel->DecrementTransferCount();
    }
  }));
  if (fabric_mem_transfer_service_ != nullptr) {
    ADXL_CHK_STATUS_RET(fabric_mem_transfer_service_->TransferAsync(channel, operation, op_descs, req),
                        "Failed to call async transfer , remote_engine:%s", remote_engine.GetString());
  } else {
    ADXL_CHK_STATUS_RET(channel->TransferAsync(operation, op_descs, optional_args, req),
                        "Failed to transfer async, remote_engine:%s", remote_engine.GetString());
  }
  LLM_DISMISS_GUARD(transfer_count_guard);
  std::lock_guard<std::mutex> lock(req2channel_mutex_);
  req2channel_[id] = remote_engine;
  return SUCCESS;
}

Status AdxlInnerEngine::GetTransferStatus(const TransferReq &req, TransferStatus &status) {
  llm::TemporaryRtContext with_context(rt_context_);
  std::lock_guard<std::mutex> lock(req2channel_mutex_);
  auto id = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(req));
  auto it = req2channel_.find(id);
  if (it == req2channel_.end()) {
    LLMLOGE(PARAM_INVALID, "Request not found, request has been completed or does not exist, req: %llu", id);
    return PARAM_INVALID;
  }
  auto remote_engine = it->second;
  auto channel = channel_manager_.GetChannel(ChannelType::kClient, remote_engine.GetString());
  ADXL_CHK_BOOL_RET_STATUS(channel != nullptr, NOT_CONNECTED,
                           "Failed to get channel, remote_engine:%s", remote_engine.GetString());
  Status ret;
  if (fabric_mem_transfer_service_ != nullptr) {
    ret = fabric_mem_transfer_service_->GetTransferStatus(channel, req, status);
  } else {
    ret = channel->GetTransferStatus(req, status);
  }
  if (status != TransferStatus::WAITING) {
    if (user_config_channel_pool_) {
      channel->DecrementTransferCount();
    }
    req2channel_.erase(it);
  }
  ADXL_CHK_STATUS_RET(ret, "Failed to get transfer status, req: %llu, remote_engine:%s",
                      id, remote_engine.GetString());
  return ret;
}

Status AdxlInnerEngine::SendNotify(const AscendString &remote_engine, const NotifyDesc &notify, int32_t timeout_in_millis) {
  // no need for RtContext
  auto channel = channel_manager_.GetChannel(ChannelType::kClient, remote_engine.GetString());
  ADXL_CHK_BOOL_RET_STATUS(channel != nullptr, NOT_CONNECTED,
                           "Failed to get channel, remote_engine:%s", remote_engine.GetString());
  NotifyMsg notify_msg;
  notify_msg.req_id = next_notify_id_++; 
  notify_msg.name = notify.name.GetString();
  notify_msg.notify_msg = notify.notify_msg.GetString();
  
  auto send_callback = [this, &notify_msg, timeout_in_millis](int32_t fd) -> Status {
    return ControlMsgHandler::SendMsg(fd, ControlMsgType::kNotify, notify_msg, timeout_in_millis);
  };
  ADXL_CHK_STATUS_RET(channel->SendControlMsg(send_callback), "Failed to send notify message.");
  std::unique_lock<std::mutex> lock(notify_mutex_);
  auto wait_result = notify_cv_.wait_for(
    lock, 
    std::chrono::milliseconds(timeout_in_millis), 
    [this, 
      req_id = notify_msg.req_id] {
      auto it_ready = notify_ack_ready_.find(req_id);
      return (it_ready != notify_ack_ready_.end() && it_ready->second);
    });
  
  Status result_status = wait_result ? SUCCESS : TIMEOUT;
  notify_ack_ready_.erase(notify_msg.req_id);
  return result_status;
}

Status AdxlInnerEngine::GetNotifies(std::vector<NotifyDesc> &notifies) {
  // no need for RtContext
  auto server_channels = channel_manager_.GetAllServerChannel();
  for (const auto &channel : server_channels) {
    channel->GetNotifyMessages(notifies);
  }
  return SUCCESS;
}
}  // namespace adxl
