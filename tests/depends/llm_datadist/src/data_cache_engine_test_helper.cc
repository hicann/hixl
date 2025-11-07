/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "data_cache_engine_test_helper.h"
#include "llm_datadist/llm_engine_types.h"

namespace llm {
HcclMem hccl_mems[16];
int cnt = 0;

int32_t AutoCommResRuntimeMock::device_id_ = 0;

std::unique_ptr<HcclApiStub> HcclApiStub::instance_;
void DataCacheEngineTestContext::Finalize() {
  cache_engine_.Finalize();
  comm_entity_.reset();
  comm_entity_manager_.Finalize();
}

void DataCacheEngineTestContext::Initialize(size_t pool_size, bool use_host_pool, bool use_batch_get) {
  if (comm_entity_ == nullptr) {
    comm_entity_manager_.SetCommMemManager(&comm_mem_manager_);
    comm_entity_manager_.Initialize(!use_batch_get);
    CommEntityParams param{.comm_id = 0, .peer_cluster_id = 0, .peer_rank_id = 0,
                           .local_cluster_id = 0, .local_rank_id = 0, .remote_cache_accessible = use_batch_get};
    EXPECT_EQ(comm_entity_manager_.CreateEntity(param, comm_entity_), ge::SUCCESS);
    comm_entity_->SetCacheManager(&cache_manager_);
    cache_engine_.SetCommEntityManager(&comm_entity_manager_);
    cache_engine_.SetCommMemManager(&comm_mem_manager_);
    cache_engine_.SetCacheManager(&cache_manager_);
    std::map<ge::AscendString, ge::AscendString> options;
    std::string config_value = "{\"memory_size\": " + std::to_string(pool_size) + ", \"page_shift\": 16}";
    options[llm::LLM_OPTION_MEM_POOL_CONFIG] = config_value.c_str();
    options[llm::LLM_OPTION_SYNC_KV_CACHE_WAIT_TIME] = "2000";
    if (use_host_pool) {
      options[llm::LLM_OPTION_HOST_MEM_POOL_CONFIG] = "{\"memory_size\": 102428800}";
    }
    if (use_batch_get) {
      options[llm::kLlmOptionEnableRemoteCacheAccessible] = "1";
    }
    cache_engine_.Initialize(options);
  }
}

llm::CommEntity &DataCacheEngineTestContext::GetCommEntry() {
  if (comm_entity_ == nullptr) {
    Initialize(false);
  }
  return *comm_entity_;
}

llm::CommEntityManager &DataCacheEngineTestContext::GetCommEntityManager() {
  return comm_entity_manager_;
}

llm::DataCacheEngine &DataCacheEngineTestContext::CacheEngine() {
  return cache_engine_;
}

void DataCacheEngineTestContext::LinkEntities(CommEntity &src_comm_entity,
                                              CommEntity &dst_comm_entity,
                                              llm::CommEntityManager &src_comm_entity_manager,
                                              llm::CommEntityManager &dst_comm_entity_manager,
                                              bool remote_cache_accessible) {
  src_comm_entity.Finalize();
  dst_comm_entity.Finalize();
  constexpr uint64_t kDefaultReqBufferSize = 112U * 1024U;
  constexpr uint64_t kDefaultRespBufferSize = 16U * 1024U;
  LLMLOGI("LinkEntities: remote_cache_accessible:%d.", static_cast<int32_t>(remote_cache_accessible));
  EntityCommInfo::CommParams src_params{0, {}, "ranktable", {}, 1};
  auto src_comm = std::make_shared<EntityCommInfo>(src_params);
  src_comm->Initialize();
  auto src_mem = std::make_unique<EntityMemInfo>(remote_cache_accessible,
                                                 src_comm_entity_manager.GetHostRegPool(),
                                                 src_comm_entity_manager.GetDeviceRegPool());
  src_mem->Initialize();
  src_comm_entity.Initialize(remote_cache_accessible);
  src_comm_entity.SetEntityMemInfo(src_mem);
  src_comm_entity.SetEntityCommInfo(src_comm);
  EntityCommInfo::CommParams dst_params{0, {}, "ranktable", {}, 1};
  auto dst_comm = std::make_shared<EntityCommInfo>(dst_params);
  dst_comm->Initialize();
  auto dst_mem = std::make_unique<EntityMemInfo>(remote_cache_accessible,
                                                 dst_comm_entity_manager.GetHostRegPool(),
                                                 dst_comm_entity_manager.GetDeviceRegPool());
  dst_mem->Initialize();
  dst_comm_entity.Initialize(remote_cache_accessible);
  dst_comm_entity.SetEntityMemInfo(dst_mem);
  dst_comm_entity.SetEntityCommInfo(dst_comm);
  std::vector<HcclMem> src_remote_mems(3);
  src_remote_mems[0].addr = dst_comm_entity.GetCacheManager()->GetCacheTableBufferAndSize().first;
  src_remote_mems[0].size = dst_comm_entity.GetCacheManager()->GetCacheTableBufferAndSize().second;
  std::vector<HcclMem> dst_remote_mems(3);
  dst_remote_mems[0].addr = src_comm_entity.GetCacheManager()->GetCacheTableBufferAndSize().first;
  dst_remote_mems[0].size = src_comm_entity.GetCacheManager()->GetCacheTableBufferAndSize().second;
  if (!remote_cache_accessible) {
    src_remote_mems[1].addr = dst_comm_entity.GetReq();
    src_remote_mems[1].size = kDefaultReqBufferSize;
    src_remote_mems[1].type = HCCL_MEM_TYPE_HOST;
    src_remote_mems[2].addr = dst_comm_entity.GetResp();
    src_remote_mems[2].size = kDefaultRespBufferSize;
    src_remote_mems[2].type = HCCL_MEM_TYPE_HOST;
    dst_remote_mems[1].addr = src_comm_entity.GetReq();
    dst_remote_mems[1].size = kDefaultReqBufferSize;
    dst_remote_mems[1].type = HCCL_MEM_TYPE_HOST;
    dst_remote_mems[2].addr = src_comm_entity.GetResp();
    dst_remote_mems[2].size = kDefaultRespBufferSize;
    dst_remote_mems[2].type = HCCL_MEM_TYPE_HOST;
  }
  src_comm_entity.GetRemoteMems() = src_remote_mems;
  dst_comm_entity.GetRemoteMems() = dst_remote_mems;
  src_comm_entity.SetInfo();
  dst_comm_entity.SetInfo();
  src_comm_entity.MarkEntityIdle();
  dst_comm_entity.MarkEntityIdle();
}

HcclResult HcclApiStub::HcclExchangeMemDesc(HcclComm comm,
                                            uint32_t remoteRank,
                                            HcclMemDescs *local,
                                            int timeout,
                                            HcclMemDescs *remote,
                                            uint32_t *actualNum) {
  for (uint32_t i = 0U; i < local->arrayLength; ++i) {
    strcpy(remote->array[i].desc, local->array[i].desc);
  }
  *actualNum = local->arrayLength;
  remote->arrayLength = local->arrayLength;
  return HCCL_SUCCESS;
}

HcclResult HcclApiStub::HcclCommInitClusterInfoMem(const char *cluster, uint32_t rank, HcclCommConfig *config,
                                                   HcclComm *comm) {
  return HCCL_SUCCESS;
}

HcclResult HcclApiStub::HcclCommDestroy(HcclComm comm) {
  return HCCL_SUCCESS;
}

HcclResult HcclApiStub::HcclBatchPut(HcclComm comm,
                                     uint32_t remoteRank,
                                     HcclOneSideOpDesc *desc,
                                     uint32_t descNum,
                                     rtStream_t stream) {
  LLMLOGI("remote_rank = %u, num_tasks = %u", remoteRank, descNum);
  for (uint32_t i = 0; i < descNum; ++i) {
    auto src = desc[i].localAddr;
    auto dst = desc[i].remoteAddr;
    auto size = desc[i].count;
    LLMLOGI("src:%p, dst:%p, size:%zu", src, dst, size);
    (void) memcpy(dst, src, size);
  }
  return HCCL_SUCCESS;
}

HcclResult HcclApiStub::HcclBatchGet(HcclComm comm,
                                     uint32_t remoteRank,
                                     HcclOneSideOpDesc *desc,
                                     uint32_t descNum,
                                     rtStream_t stream) {
  LLMLOGI("remote_rank = %u, num_tasks = %u", remoteRank, descNum);
  for (uint32_t i = 0; i < descNum; ++i) {
    auto dst = desc[i].localAddr;
    auto src = desc[i].remoteAddr;
    auto size = desc[i].count;
    LLMLOGI("memcpy: dst = %p, src = %p, size = %zu", dst, src, size);
    (void) memcpy(dst, src, size);
  }
  return HCCL_SUCCESS;
}

HcclResult HcclApiStub::HcclRemapRegistedMemory(HcclComm *comm,
                                                HcclMem *memInfoArray,
                                                uint64_t commSize,
                                                uint64_t arraySize) {
  return HCCL_SUCCESS;
}


HcclResult HcclApiStub::HcclRegisterGlobalMem(HcclMem *mem, void **memHandle) {
  *memHandle = mem;
  return HcclResult::HCCL_SUCCESS;
}

HcclResult HcclApiStub::HcclDeregisterGlobalMem(void *memHandle) {
  return HcclResult::HCCL_SUCCESS;
}

HcclResult HcclApiStub::HcclCommBindMem(HcclComm comm, void *memHandle) {
  return HcclResult::HCCL_SUCCESS;
}

HcclResult HcclApiStub::HcclCommUnbindMem(HcclComm comm, void *memHandle) {
  return HcclResult::HCCL_SUCCESS;
}

HcclResult HcclApiStub::HcclCommPrepare(HcclComm comm, HcclPrepareConfig *prepareConfig, int32_t timeout) {
  return HcclResult::HCCL_SUCCESS;
}

HcclApiStub &HcclApiStub::GetInstance() {
  if (instance_ != nullptr) {
    return *instance_;
  }
  static HcclApiStub default_instance;
  return default_instance;
}

void HcclApiStub::SetStub(std::unique_ptr<HcclApiStub> instance) {
  instance_ = std::move(instance);
}

void HcclApiStub::ResetStub() {
  instance_ = nullptr;
}

void *MockMmpaForHcclApi::DlOpen(const char *file_name, int32_t mode) {
  return (void *) 0x10000000;
}

void *MockMmpaForHcclApi::DlSym(void *handle, const char *func_name) {
  static const std::map<std::string, void*> func_map = {
      {"HcclCommInitClusterInfoMemConfig", reinterpret_cast<void*>(&HcclCommInitClusterInfoMem)},
      {"HcclCommConfigInit", reinterpret_cast<void*>(&HcclCommConfigInit)},
      {"HcclExchangeMemDesc", reinterpret_cast<void*>(&HcclExchangeMemDesc)},
      {"HcclCommDestroy", reinterpret_cast<void*>(&HcclCommDestroy)},
      {"HcclBatchPut", reinterpret_cast<void*>(&HcclBatchPut)},
      {"HcclBatchGet", reinterpret_cast<void*>(&HcclBatchGet)},
      {"HcclRemapRegistedMemory", reinterpret_cast<void*>(&HcclRemapRegistedMemory)},
      {"HcclRegisterGlobalMem", reinterpret_cast<void*>(&HcclRegisterGlobalMem)},
      {"HcclDeregisterGlobalMem", reinterpret_cast<void*>(&HcclDeregisterGlobalMem)},
      {"HcclCommBindMem", reinterpret_cast<void*>(&HcclCommBindMem)},
      {"HcclCommUnbindMem", reinterpret_cast<void*>(&HcclCommUnbindMem)},
      {"HcclCommPrepare", reinterpret_cast<void*>(&HcclCommPrepare)},
  };
  auto it = func_map.find(func_name);
  if (it != func_map.end()) {
    LLMLOGI("%s addr:%lu", func_name, static_cast<uint64_t>(reinterpret_cast<uintptr_t>(it->second)));
    return it->second;
  }
  return nullptr;
}

int32_t MockMmpaForHcclApi::DlClose(void *handle) {
  return 0;
}
}  // namespace llm