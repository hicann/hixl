/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "comm_adapter.h"
#include <map>
#include "mmpa/mmpa_api.h"
#include "common/common.h"
#include "comm_statistic_manager.h"
#include "common/llm_checker.h"
#include "common/llm_scope_guard.h"

namespace llm {
namespace {
constexpr const char *kHcclSoName = "libhcomm.so";
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

ge::Status CommAdapter::Initialize() {
  return LoadSo();
}
void CommAdapter::Finalize() {
  (void)UnloadSo();
}

CommAdapter::~CommAdapter() {
  Finalize();
}

ge::Status CommAdapter::LoadSo() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (so_handle_ != nullptr) {
    return ge::SUCCESS;
  }
  auto ret = ge::SUCCESS;
  so_handle_ = mmDlopen(kHcclSoName, static_cast<int32_t>(static_cast<uint32_t>(MMPA_RTLD_NOW) |
                                                          static_cast<uint32_t>(MMPA_RTLD_GLOBAL)));
  LLM_CHECK_NOTNULL(so_handle_, ",open hccl so:%s failed.", kHcclSoName);

  LLMLOGI("Start to load funcs");

  dl_hccl_exchange_mem_desc_func_ =
      llm::FunctionLoader<DlHcclExchangeMemDescFunc>::load(so_handle_, kHcclExchangeMemDescName);
  LLM_CHECK_NOTNULL(dl_hccl_exchange_mem_desc_func_, ",failed to get function:%s.", kHcclExchangeMemDescName);

  dl_hccl_batch_put_func_ = llm::FunctionLoader<DlHcclBatchPutFunc>::load(so_handle_, kHcclBatchPutName);
  LLM_CHECK_NOTNULL(dl_hccl_batch_put_func_, ",failed to get function:%s.", kHcclBatchPutName);

  dl_hccl_batch_get_func_ = llm::FunctionLoader<DlHcclBatchGetFunc>::load(so_handle_, kHcclBatchGetName);
  LLM_CHECK_NOTNULL(dl_hccl_batch_get_func_, ",failed to get function:%s.", kHcclBatchGetName);

  dl_hccl_remap_registered_memory_func_ =
      llm::FunctionLoader<DlHcclRemapRegisteredMemoryFunc>::load(so_handle_, kHcclRemapRegisteredMemoryName);
  LLM_CHECK_NOTNULL(dl_hccl_remap_registered_memory_func_, ",failed to get function:%s.",
                    kHcclRemapRegisteredMemoryName);

  dl_hccl_comm_prepare_func_ = llm::FunctionLoader<DlHcclCommPrepareFunc>::load(so_handle_, kHcclCommPrepareName);

  dl_hccl_register_global_mem_func_ =
      llm::FunctionLoader<DlHcclRegisterGlobalMemFunc>::load(so_handle_, kHcclRegisterGlobalMemName);
  LLM_CHECK_NOTNULL(dl_hccl_register_global_mem_func_, ",failed to get function:%s.", kHcclRegisterGlobalMemName);

  dl_hccl_deregister_global_mem_func_ =
      llm::FunctionLoader<DlHcclDeregisterGlobalMemFunc>::load(so_handle_, kHcclDeregisterGlobalMemName);
  LLM_CHECK_NOTNULL(dl_hccl_deregister_global_mem_func_, ",failed to get function:%s.", kHcclDeregisterGlobalMemName);

  dl_hccl_comm_bind_mem_func_ = llm::FunctionLoader<DlHcclCommBindMemFunc>::load(so_handle_, kHcclCommBindMemName);
  LLM_CHECK_NOTNULL(dl_hccl_comm_bind_mem_func_, ",failed to get function:%s.", kHcclCommBindMemName);

  dl_hccl_comm_unbind_mem_func_ =
      llm::FunctionLoader<DlHcclCommUnbindMemFunc>::load(so_handle_, kHcclCommUnbindMemName);
  LLM_CHECK_NOTNULL(dl_hccl_comm_unbind_mem_func_, ",failed to get function:%s.", kHcclCommUnbindMemName);

  dl_hccl_comm_init_cluster_info_mem_func_ =
      llm::FunctionLoader<DlHcclCommInitClusterInfoMemConfigFunc>::load(so_handle_, kHcclCommInitClusterInfoMemName);
  LLM_CHECK_NOTNULL(dl_hccl_comm_init_cluster_info_mem_func_, ",failed to get function:%s.",
                    kHcclCommInitClusterInfoMemName);

  dl_hccl_comm_destroy_func_ = llm::FunctionLoader<DlHcclCommDestroyFunc>::load(so_handle_, kHcclCommDestroyName);
  LLM_CHECK_NOTNULL(dl_hccl_comm_destroy_func_, ",failed to get function:%s.", kHcclCommDestroyName);

  LLMLOGI("Success to load so:%s", kHcclSoName);
  return ret;
}

ge::Status CommAdapter::UnloadSo() {
  std::lock_guard<std::mutex> lock(mutex_);
  dl_hccl_exchange_mem_desc_func_ = nullptr;
  dl_hccl_comm_init_cluster_info_mem_func_ = nullptr;
  dl_hccl_comm_destroy_func_ = nullptr;
  dl_hccl_batch_put_func_ = nullptr;
  dl_hccl_remap_registered_memory_func_ = nullptr;
  dl_hccl_register_global_mem_func_ = nullptr;
  dl_hccl_deregister_global_mem_func_ = nullptr;
  if (so_handle_ != nullptr) {
    auto ret = mmDlclose(so_handle_);
    LLM_CHK_BOOL_RET_STATUS(ret == 0, ge::FAILED, "close hccl so failed.");
    so_handle_ = nullptr;
  }
  return ge::SUCCESS;
}

CommAdapter &CommAdapter::GetInstance() {
  static CommAdapter manager;
  return manager;
}

HcclResult CommAdapter::DlHcclExchangeMemDesc(HcclComm comm, uint32_t remote_rank, HcclMemDescs *local, int timeout,
                                                  HcclMemDescs *remote, uint32_t *actual_num) const {
  const auto start = std::chrono::steady_clock::now();
  auto ret = dl_hccl_exchange_mem_desc_func_(comm, remote_rank, local, timeout, remote, actual_num);
  const auto end = std::chrono::steady_clock::now();
  CommStatisticManager::GetInstance().AddExchangeMemCost(
      std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
  return ret;
}

HcclResult CommAdapter::DlHcclCommInitClusterInfoMemConfig(const char *cluster, uint32_t rank,
                                                               HcclCommConfig *config, HcclComm *comm) const {
  const auto start = std::chrono::steady_clock::now();
  auto ret = dl_hccl_comm_init_cluster_info_mem_func_(cluster, rank, config, comm);
  const auto end = std::chrono::steady_clock::now();
  CommStatisticManager::GetInstance().AddCommInitCost(
      std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
  return ret;
}

void CommAdapter::DlHcclCommConfigInit(HcclCommConfig *config) const {
  const uint32_t HCCL_COMM_CONFIG_MAGIC_WORD = 0xf0f0f0f0;
  const uint32_t HCCL_COMM_CONFIG_VERSION = 5U;
  const uint32_t HCCL_COMM_DEFAULT_BUFFSIZE = 200U;
  const uint32_t HCCL_COMM_DEFAULT_DETERMINISTIC = 0U;
  const uint32_t HCCL_COMM_DEFAULT_OP_EXPANSION_MODE = 0U;
  // 0xffffffff表示用户未配置TC或SL
  const uint32_t HCCL_COMM_TRAFFIC_CLASS_CONFIG_NOT_SET = 0xffffffff;
  const uint32_t HCCL_COMM_SERVICE_LEVEL_CONFIG_NOT_SET = 0xffffffff;
  struct HcclConfigInfo {
    size_t configSize;
    uint32_t hcclMagicWord;
    uint32_t hcclVersion;
    uint64_t reserved;
  };
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

HcclResult CommAdapter::DlHcclCommDestroy(HcclComm comm) const {
  const auto start = std::chrono::steady_clock::now();
  auto ret = dl_hccl_comm_destroy_func_(comm);
  const auto end = std::chrono::steady_clock::now();
  CommStatisticManager::GetInstance().AddCommDestroyCost(
      std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
  return ret;
}

HcclResult CommAdapter::DlHcclBatchPut(HcclComm comm, uint32_t remote_rank, HcclOneSideOpDesc *desc,
                                           uint32_t desc_num, aclrtStream stream) const {
  const auto start = std::chrono::steady_clock::now();
  auto ret = dl_hccl_batch_put_func_(comm, remote_rank, desc, desc_num, stream);
  const auto end = std::chrono::steady_clock::now();
  CommStatisticManager::GetInstance().AddBatchPutCost(
      std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
  return ret;
}

HcclResult CommAdapter::DlHcclBatchGet(HcclComm comm, uint32_t remote_rank, HcclOneSideOpDesc *desc,
                                           uint32_t desc_num, aclrtStream stream) const {
  auto ret = dl_hccl_batch_get_func_(comm, remote_rank, desc, desc_num, stream);
  return ret;
}

HcclResult CommAdapter::DlHcclRemapRegisteredMemory(HcclComm *comm, CommMem *mem_info_array, uint64_t comm_size,
                                                        uint64_t arraySize) const {
  auto ret = dl_hccl_remap_registered_memory_func_(comm, mem_info_array, comm_size, arraySize);
  return ret;
}

HcclResult CommAdapter::DlHcclRegisterGlobalMem(CommMem *mem, void **mem_handle) const {
  auto ret = HCCL_E_NOT_SUPPORT;
  if (dl_hccl_register_global_mem_func_ != nullptr) {
    ret = dl_hccl_register_global_mem_func_(mem, mem_handle);
    CommStatisticManager::GetInstance().AddRegisterGlobalMemTimes();
  }
  return ret;
}

HcclResult CommAdapter::DlHcclDeregisterGlobalMem(void *mem_handle) const {
  auto ret = HCCL_E_NOT_SUPPORT;
  if (dl_hccl_deregister_global_mem_func_ != nullptr) {
    ret = dl_hccl_deregister_global_mem_func_(mem_handle);
    CommStatisticManager::GetInstance().AddDeregisterGlobalMemTimes();
  }
  return ret;
}

HcclResult CommAdapter::DlHcclCommBindMem(HcclComm comm, void *mem_handle) const {
  auto ret = HCCL_E_NOT_SUPPORT;
  if (dl_hccl_comm_bind_mem_func_ != nullptr) {
    ret = dl_hccl_comm_bind_mem_func_(comm, mem_handle);
    CommStatisticManager::GetInstance().AddCommBindMemTimes();
  }
  return ret;
}

HcclResult CommAdapter::DlHcclCommUnbindMem(HcclComm comm, void *mem_handle) const {
  auto ret = HCCL_E_NOT_SUPPORT;
  if (dl_hccl_comm_unbind_mem_func_ != nullptr) {
    ret = dl_hccl_comm_unbind_mem_func_(comm, mem_handle);
    CommStatisticManager::GetInstance().AddCommUnbindMemTimes();
  }
  return ret;
}

HcclResult CommAdapter::DlHcclCommPrepare(HcclComm comm, HcclPrepareConfig *prepare_config, int32_t timeout) const {
  auto ret = HCCL_E_NOT_SUPPORT;
  if (dl_hccl_comm_prepare_func_ != nullptr) {
    const auto start = std::chrono::steady_clock::now();
    ret = dl_hccl_comm_prepare_func_(comm, prepare_config, timeout);
    const auto end = std::chrono::steady_clock::now();
    CommStatisticManager::GetInstance().AddCommPrepareCost(
        std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
  }
  return ret;
}

ge::Status CommUtils::ConvertCommErrorToStatus(HcclResult hccl_result, ge::Status default_status) {
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

const std::string CommUtils::ConvertCommMemTypeToString(CommMemType type) {
  switch (type) {
    case COMM_MEM_TYPE_DEVICE:
      return "device";
    case COMM_MEM_TYPE_HOST:
      return "host";
    default:
      return "unknown";
  }
}
}  // namespace llm
