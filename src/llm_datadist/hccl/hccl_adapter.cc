/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hccl/hccl_adapter.h"
#include <map>
#include "mmpa/mmpa_api.h"
#include "common/common.h"
#include "statistic_manager.h"
#include "common/llm_checker.h"
#include "common/llm_scope_guard.h"

namespace llm {
namespace {
constexpr const char *kHcclSoName = "libhccl.so";
constexpr const char *kHcclExchangeMemDescName = "HcclExchangeMemDesc";
constexpr const char *kHcclCommInitClusterInfoMemName = "HcclCommInitClusterInfoMemConfig";
constexpr const char *kHcclCommDestroyName = "HcclCommDestroy";
constexpr const char *kHcclBatchPutName = "HcclBatchPut";
constexpr const char *kHcclBatchGetName = "HcclBatchGet";
constexpr const char *kHcclRemapRegisteredMemoryName = "HcclRemapRegistedMemory";
constexpr const char *kHcclRegisterGlobalMemName = "HcclRegisterGlobalMem";
constexpr const char *kHcclDeregisterGlobalMemName = "HcclDeregisterGlobalMem";
constexpr const char *kHcclCommBindMemName = "HcclCommBindMem";
constexpr const char *kHcclCommUnbindMemName = "HcclCommUnbindMem";
constexpr const char *kHcclCommPrepareName = "HcclCommPrepare";
}  // namespace

ge::Status HcclAdapter::Initialize() {
  return LoadSo();
}
void HcclAdapter::Finalize() {
  (void)UnloadSo();
}

HcclAdapter::~HcclAdapter() {
  Finalize();
}

ge::Status HcclAdapter::LoadSo() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (so_handle_ != nullptr) {
    return ge::SUCCESS;
  }
  auto ret = ge::SUCCESS;
  so_handle_ = mmDlopen(kHcclSoName, static_cast<int32_t>(static_cast<uint32_t>(MMPA_RTLD_NOW) |
                                                          static_cast<uint32_t>(MMPA_RTLD_GLOBAL)));
  LLM_CHECK_NOTNULL(so_handle_, ",open hccl so:%s failed.", kHcclSoName);

  LLMLOGI("Start to load funcs");

  hccl_exchange_mem_desc_func_ =
      reinterpret_cast<HcclExchangeMemDescFunc>(mmDlsym(so_handle_, kHcclExchangeMemDescName));
  LLM_CHECK_NOTNULL(hccl_exchange_mem_desc_func_, ",failed to get function:%s.", kHcclExchangeMemDescName);

  hccl_batch_put_func_ = reinterpret_cast<HcclBatchPutFunc>(mmDlsym(so_handle_, kHcclBatchPutName));
  LLM_CHECK_NOTNULL(hccl_batch_put_func_, ",failed to get function:%s.", kHcclBatchPutName);

  hccl_batch_get_func_ = reinterpret_cast<HcclBatchGetFunc>(mmDlsym(so_handle_, kHcclBatchGetName));
  LLM_CHECK_NOTNULL(hccl_batch_get_func_, ",failed to get function:%s.", kHcclBatchGetName);

  hccl_remap_registered_memory_func_ = reinterpret_cast<HcclRemapRegisteredMemoryFunc>(mmDlsym(so_handle_,
      kHcclRemapRegisteredMemoryName));
  LLM_CHECK_NOTNULL(hccl_remap_registered_memory_func_, ",failed to get function:%s.", kHcclRemapRegisteredMemoryName);

  hccl_comm_prepare_func_ = reinterpret_cast<HcclCommPrepareFunc>(mmDlsym(so_handle_,
      kHcclCommPrepareName));

  hccl_register_global_mem_func_ = reinterpret_cast<HcclRegisterGlobalMemFunc>(mmDlsym(so_handle_,
      kHcclRegisterGlobalMemName));
  LLM_CHECK_NOTNULL(hccl_register_global_mem_func_, ",failed to get function:%s.", kHcclRegisterGlobalMemName);

  hccl_deregister_global_mem_func_ = reinterpret_cast<HcclDeregisterGlobalMemFunc>(mmDlsym(so_handle_,
      kHcclDeregisterGlobalMemName));
  LLM_CHECK_NOTNULL(hccl_deregister_global_mem_func_, ",failed to get function:%s.", kHcclDeregisterGlobalMemName);

  hccl_comm_bind_mem_func_ = reinterpret_cast<HcclCommBindMemFunc>(mmDlsym(so_handle_,
      kHcclCommBindMemName));
  LLM_CHECK_NOTNULL(hccl_comm_bind_mem_func_, ",failed to get function:%s.", kHcclCommBindMemName);

  hccl_comm_unbind_mem_func_ = reinterpret_cast<HcclCommUnbindMemFunc>(mmDlsym(so_handle_,
      kHcclCommUnbindMemName));
  LLM_CHECK_NOTNULL(hccl_comm_unbind_mem_func_, ",failed to get function:%s.", kHcclCommUnbindMemName);

  hccl_comm_init_cluster_info_mem_func_ =
      reinterpret_cast<HcclCommInitClusterInfoMemConfigFunc>(mmDlsym(so_handle_, kHcclCommInitClusterInfoMemName));
  LLM_CHECK_NOTNULL(hccl_comm_init_cluster_info_mem_func_, ",failed to get function:%s.",
                   kHcclCommInitClusterInfoMemName);

  hccl_comm_destroy_func_ = reinterpret_cast<HcclCommDestroyFunc>(mmDlsym(so_handle_, kHcclCommDestroyName));
  LLM_CHECK_NOTNULL(hccl_comm_destroy_func_, ",failed to get function:%s.", kHcclCommDestroyName);

  LLMLOGI("Success to load so:%s", kHcclSoName);
  return ret;
}

ge::Status HcclAdapter::UnloadSo() {
  std::lock_guard<std::mutex> lock(mutex_);
  hccl_exchange_mem_desc_func_ = nullptr;
  hccl_comm_init_cluster_info_mem_func_ = nullptr;
  hccl_comm_destroy_func_ = nullptr;
  hccl_batch_put_func_ = nullptr;
  hccl_remap_registered_memory_func_ = nullptr;
  hccl_register_global_mem_func_ = nullptr;
  hccl_deregister_global_mem_func_ = nullptr;
  if (so_handle_ != nullptr) {
    auto ret = mmDlclose(so_handle_);
    LLM_CHK_BOOL_RET_STATUS(ret == 0, ge::FAILED, "close hccl so failed.");
    so_handle_ = nullptr;
  }
  return ge::SUCCESS;
}

HcclAdapter &HcclAdapter::GetInstance() {
  static HcclAdapter manager;
  return manager;
}

HcclResult HcclAdapter::HcclExchangeMemDesc(HcclComm comm, uint32_t remote_rank, HcclMemDescs *local, int timeout,
                                            HcclMemDescs *remote, uint32_t *actual_num) {
  const auto start = std::chrono::steady_clock::now();
  auto ret = hccl_exchange_mem_desc_func_(comm, remote_rank, local, timeout, remote, actual_num);
  const auto end = std::chrono::steady_clock::now();
  StatisticManager::GetInstance().AddExchangeMemCost(
      std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
  return ret;
}

HcclResult HcclAdapter::HcclCommInitClusterInfoMemConfig(const char *cluster, uint32_t rank, HcclCommConfig *config,
                                                         HcclComm *comm) {
  const auto start = std::chrono::steady_clock::now();
  auto ret = hccl_comm_init_cluster_info_mem_func_(cluster, rank, config, comm);
  const auto end = std::chrono::steady_clock::now();
  StatisticManager::GetInstance().AddCommInitCost(
      std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
  return ret;
}

void HcclAdapter::HcclCommConfigInit(HcclCommConfig *config) {
  const uint32_t HCCL_COMM_CONFIG_MAGIC_WORD = 0xf0f0f0f0;
  const uint32_t HCCL_COMM_CONFIG_VERSION = 5U;
  const uint32_t HCCL_COMM_DEFAULT_BUFFSIZE = 200U;
  const uint32_t HCCL_COMM_DEFAULT_DETERMINISTIC = 0U;
  const uint32_t HCCL_COMM_DEFAULT_OP_EXPANSION_MODE = 0U;
  // 0xffffffff表示用户未配置TC或SL
  const uint32_t HCCL_COMM_TRAFFIC_CLASS_CONFIG_NOT_SET = 0xffffffff;
  const uint32_t HCCL_COMM_SERVICE_LEVEL_CONFIG_NOT_SET = 0xffffffff;
  typedef struct {
    size_t configSize;
    uint32_t hcclMagicWord;
    uint32_t hcclVersion;
    uint64_t reserved;
  } HcclConfigInfo;
  auto *info = reinterpret_cast<HcclConfigInfo *>(config);
  info->configSize = sizeof(HcclCommConfig);
  info->hcclMagicWord = HCCL_COMM_CONFIG_MAGIC_WORD;
  info->hcclVersion = HCCL_COMM_CONFIG_VERSION;
  info->reserved = 0U;

  config->hcclBufferSize = HCCL_COMM_DEFAULT_BUFFSIZE;
  config->hcclDeterministic = HCCL_COMM_DEFAULT_DETERMINISTIC;
  config->hcclCommName[0] = '\0';
  config->hcclUdi[0] = '\0';
  config->hcclOpExpansionMode = HCCL_COMM_DEFAULT_OP_EXPANSION_MODE;
  config->hcclRdmaTrafficClass = HCCL_COMM_TRAFFIC_CLASS_CONFIG_NOT_SET;
  config->hcclRdmaServiceLevel = HCCL_COMM_SERVICE_LEVEL_CONFIG_NOT_SET;
}

HcclResult HcclAdapter::HcclCommDestroy(HcclComm comm) {
  const auto start = std::chrono::steady_clock::now();
  auto ret = hccl_comm_destroy_func_(comm);
  const auto end = std::chrono::steady_clock::now();
  StatisticManager::GetInstance().AddCommDestroyCost(
      std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
  return ret;
}

HcclResult HcclAdapter::HcclBatchPut(HcclComm comm, uint32_t remote_rank, HcclOneSideOpDesc *desc, uint32_t desc_num,
                                     rtStream_t stream) {
  const auto start = std::chrono::steady_clock::now();
  auto ret = hccl_batch_put_func_(comm, remote_rank, desc, desc_num, stream);
  const auto end = std::chrono::steady_clock::now();
  StatisticManager::GetInstance().AddBatchPutCost(
      std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
  return ret;
}

HcclResult HcclAdapter::HcclBatchGet(HcclComm comm,
                                     uint32_t remote_rank,
                                     HcclOneSideOpDesc *desc,
                                     uint32_t desc_num,
                                     rtStream_t stream) const {
  auto ret = hccl_batch_get_func_(comm, remote_rank, desc, desc_num, stream);
  return ret;
}

HcclResult HcclAdapter::HcclRemapRegisteredMemory(HcclComm *comm,
                                                  HcclMem *memInfoArray,
                                                  uint64_t commSize,
                                                  uint64_t arraySize) const {
  auto ret = hccl_remap_registered_memory_func_(comm, memInfoArray, commSize, arraySize);
  return ret;
}

HcclResult HcclAdapter::HcclRegisterGlobalMem(HcclMem *mem, void **memHandle) {
  auto ret = HCCL_E_NOT_SUPPORT;
  if (hccl_register_global_mem_func_ != nullptr) {
    ret = hccl_register_global_mem_func_(mem, memHandle);
    StatisticManager::GetInstance().AddRegisterGlobalMemTimes();
  }
  return ret;
}

HcclResult HcclAdapter::HcclDeregisterGlobalMem(void *memHandle) {
  auto ret = HCCL_E_NOT_SUPPORT;
  if (hccl_deregister_global_mem_func_ != nullptr) {
    ret = hccl_deregister_global_mem_func_(memHandle);
    StatisticManager::GetInstance().AddDeregisterGlobalMemTimes();
  }
  return ret;
}

HcclResult HcclAdapter::HcclCommBindMem(HcclComm comm, void *memHandle) {
  auto ret = HCCL_E_NOT_SUPPORT;
  if (hccl_comm_bind_mem_func_ != nullptr) {
    ret = hccl_comm_bind_mem_func_(comm, memHandle);
    StatisticManager::GetInstance().AddCommBindMemTimes();
  }
  return ret;
}

HcclResult HcclAdapter::HcclCommUnbindMem(HcclComm comm, void *memHandle) {
  auto ret = HCCL_E_NOT_SUPPORT;
  if (hccl_comm_unbind_mem_func_ != nullptr) {
    ret = hccl_comm_unbind_mem_func_(comm, memHandle);
    StatisticManager::GetInstance().AddCommUnbindMemTimes();
  }
  return ret;
}

HcclResult HcclAdapter::HcclCommPrepare(HcclComm comm, HcclPrepareConfig *prepareConfig, int32_t timeout) {
  auto ret = HCCL_E_NOT_SUPPORT;
  if (hccl_comm_prepare_func_ != nullptr) {
    const auto start = std::chrono::steady_clock::now();
    ret = hccl_comm_prepare_func_(comm, prepareConfig, timeout);
    const auto end = std::chrono::steady_clock::now();
    StatisticManager::GetInstance().AddCommPrepareCost(
        std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
  }
  return ret;
}

ge::Status HcclUtils::ConvertHcclErrorCode(HcclResult hccl_result, ge::Status default_status) {
  const static std::map<HcclResult, ge::Status> hccl_to_ge_status = {
      {HCCL_E_PARA, ge::LLM_PARAM_INVALID},
      {HCCL_E_TIMEOUT, ge::LLM_TIMEOUT},
      {HCCL_E_NOT_SUPPORT, ge::LLM_FEATURE_NOT_ENABLED},
  };
  const auto &it = hccl_to_ge_status.find(hccl_result);
  if (it != hccl_to_ge_status.cend()) {
    return it->second;
  }
  return default_status;
}
}  // namespace llm
