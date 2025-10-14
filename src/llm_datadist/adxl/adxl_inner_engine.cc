/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "adxl_inner_engine.h"
#include "runtime/rt.h"
#include "hccl/hccl_adapter.h"
#include "common/llm_utils.h"
#include "common/llm_scope_guard.h"
#include "common/llm_checker.h"

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
}
Status AdxlInnerEngine::Initialize(const std::map<AscendString, AscendString> &options) {
  std::lock_guard<std::mutex> lk(mutex_);
  ADXL_CHK_LLM_RET(llm::HcclAdapter::GetInstance().Initialize(), "HcclSoManager initialize failed.");
  segment_table_ = llm::MakeUnique<SegmentTable>();
  ADXL_CHK_STATUS_RET(msg_handler_.Initialize(options, segment_table_.get()), "Failed to init msg handler.");
  ADXL_CHK_STATUS_RET(InitBufferTransferService(options), "Failed to init buffer memory pool.");
  ADXL_CHK_STATUS_RET(channel_manager_.Initialize(buffer_transfer_service_.get()), "Failed to init channel manager.");
  is_initialized_ = true;
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

Status AdxlInnerEngine::InitBufferTransferService(const std::map<ge::AscendString, ge::AscendString> &options) {
  std::string pool_config;
  ParseBufferPool(options, pool_config);
  uint64_t buffer_num;
  uint64_t buffer_size;
  if (!pool_config.empty()) {
    if (pool_config == kDisabledPoolConfig) {
      LLMEVENT("Buffer pool is disabled.");
      return SUCCESS;
    }
    LLMEVENT("Buffer pool config is:%s.", pool_config.c_str());
    const auto buffer_configs = llm::LLMUtils::Split(pool_config, ':');
    ADXL_CHK_BOOL_RET_STATUS(buffer_configs.size() == kBufferConfigSize, PARAM_INVALID,
                             "Option BufferPool is invalid: %s, expect ${BUFFER_NUM}:${BUFFER_SIZE}.",
                             pool_config.c_str());
    ADXL_CHK_LLM_RET(llm::LLMUtils::ToNumber(buffer_configs[0], buffer_num), "Buffer num is invalid, value = %s.",
                     buffer_configs[0].c_str());
    ADXL_CHK_BOOL_RET_STATUS(buffer_num > 0, PARAM_INVALID, "Buffer num should be bigger than 0.");
    auto &buffer_size_str = buffer_configs[1];
    ADXL_CHK_LLM_RET(llm::LLMUtils::ToNumber(buffer_size_str, buffer_size), "Buffer size is invalid, value = %s",
                     buffer_size_str.c_str());
    ADXL_CHK_BOOL_RET_STATUS(buffer_size > 0, PARAM_INVALID, "Buffer size should be bigger than 0.");
    user_config_buffer_pool_ = true;
  } else {
    buffer_num = kDefaultBufferNum;
    buffer_size = kDefaultBufferSize;
  }
  uint64_t npu_pool_size = 0UL;
  LLM_ASSERT_TRUE(!ge::MulOverflow(buffer_size, buffer_num, npu_pool_size));
  LLM_ASSERT_TRUE(!ge::MulOverflow(npu_pool_size, kBaseBufferSize, npu_pool_size));
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
    ADXL_CHK_BOOL_RET_STATUS(rtMalloc(&npu_pool_memorys_[i], npu_pool_size, RT_MEMORY_HBM | RT_MEM_MALLOC_HUGE_FIRST,
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
  mem_pools.reserve(kMemPoolNum);
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
  channel_manager_.Finalize();
  msg_handler_.Finalize();
  for (auto &mem_handle : pool_mem_handles_) {
    if (mem_handle != nullptr) {
      msg_handler_.DeregisterMem(mem_handle);
    }
  }
  for (auto &mem : npu_pool_memorys_) {
    if (mem != nullptr) {
      auto ret = rtFree(mem);
      LLMLOGI("Call rtFree ret:%d.", ret);
    }
  }
  if (buffer_transfer_service_ != nullptr) {
    buffer_transfer_service_->Finalize();
  }
}

bool AdxlInnerEngine::IsInitialized() const {
  return is_initialized_.load(std::memory_order::memory_order_relaxed);
}

Status AdxlInnerEngine::RegisterMem(const MemDesc &mem, MemType type, MemHandle &mem_handle) {
  ADXL_CHK_STATUS_RET(msg_handler_.RegisterMem(mem, type, mem_handle), "Failed to register mem");
  return SUCCESS;
}

Status AdxlInnerEngine::DeregisterMem(MemHandle mem_handle) {
  ADXL_CHK_STATUS_RET(msg_handler_.DeregisterMem(mem_handle), "Failed to deregister mem");
  return SUCCESS;
}

Status AdxlInnerEngine::Connect(const AscendString &remote_engine, int32_t timeout_in_millis) {
  LLMEVENT("Start to connect, local engine:%s, remote engine:%s, timeout:%d ms.",
          local_engine_.c_str(), remote_engine.GetString(), timeout_in_millis);
  ADXL_CHK_STATUS_RET(msg_handler_.Connect(remote_engine.GetString(), timeout_in_millis),
                      "Failed to connect, remote engine:%s, timeout:%d ms",
                      remote_engine.GetString(), timeout_in_millis);
  return SUCCESS;
}

Status AdxlInnerEngine::Disconnect(const AscendString &remote_engine, int32_t timeout_in_millis) {
  ADXL_CHK_STATUS_RET(msg_handler_.Disconnect(remote_engine.GetString(), timeout_in_millis),
                      "Failed to disconnect, remote engine:%s, timeout:%d ms",
                      remote_engine.GetString(), timeout_in_millis);
  return SUCCESS;
}

Status AdxlInnerEngine::GetTransferType(const ChannelPtr &channel, TransferOp operation,
                                        const std::vector<TransferOpDesc> &op_descs, bool &need_buffer,
                                        TransferType &type) {
  for (size_t i = 0; i < op_descs.size(); i++) {
    auto &op_desc = op_descs[i];
    auto local_segment =
        segment_table_->FindSegment(local_engine_, op_desc.local_addr, op_desc.local_addr + op_desc.len);
    MemType local_mem_type = local_segment != nullptr ? local_segment->GetMemType() : MemType::MEM_HOST;
    auto remote_segment =
        segment_table_->FindSegment(channel->GetChannelId(), op_desc.remote_addr, op_desc.remote_addr + op_desc.len);
    MemType remote_mem_type = remote_segment != nullptr ? remote_segment->GetMemType() : MemType::MEM_HOST;
    need_buffer = (local_segment == nullptr) || (remote_segment == nullptr);
    need_buffer = need_buffer || (op_desc.len < kNeedUseBufferThresh);

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
    if (i > 0) {
      ADXL_CHK_BOOL_RET_STATUS(!need_buffer || (need_buffer && cur_type == type), PARAM_INVALID,
                               "All transfer type need be same in buffer transfer mode.");
    }
    type = cur_type;
  }
  return SUCCESS;
}

Status AdxlInnerEngine::TransferSync(const AscendString &remote_engine,
                                     TransferOp operation,
                                     const std::vector<TransferOpDesc> &op_descs,
                                     int32_t timeout_in_millis) {
  auto channel = channel_manager_.GetChannel(ChannelType::kClient, remote_engine.GetString());
  ADXL_CHK_BOOL_RET_STATUS(channel != nullptr, NOT_CONNECTED,
                           "Failed to get channel, remote_engine:%s", remote_engine.GetString());
  if (buffer_transfer_service_ != nullptr) {
    const auto start = std::chrono::steady_clock::now();
    bool need_buffer = false;
    TransferType type;
    ADXL_CHK_STATUS_RET(GetTransferType(channel, operation, op_descs, need_buffer, type),
                        "Failed to get transfer type.");
    LLMLOGI("Transfer type is:%d, cost:%lu us.", type,
           std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count());
    if (need_buffer) {
      ADXL_CHK_BOOL_RET_STATUS(type != TransferType::kEnd, PARAM_INVALID, "Transfer type is invalid.");
      return buffer_transfer_service_->Transfer(channel, type, op_descs, timeout_in_millis);
    }
  }
  ADXL_CHK_STATUS_RET(channel->TransferSync(operation, op_descs, timeout_in_millis),
                      "Failed to transfer sync, remote_engine:%s", remote_engine.GetString());
  return SUCCESS;
}
}  // namespace adxl
