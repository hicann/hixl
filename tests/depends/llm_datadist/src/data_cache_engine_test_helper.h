/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_GRAPH_ENGINE_TESTS_DEPENDS_LLM_ENGINE_DATA_CACHE_ENGINE_TEST_HELPER_H_
#define CANN_GRAPH_ENGINE_TESTS_DEPENDS_LLM_ENGINE_DATA_CACHE_ENGINE_TEST_HELPER_H_

#include <gtest/gtest.h>
#include <numeric>
#include <fstream>
#include <cstdio>  // for std::remove
#include "cache_mgr/data_cache_engine.h"
#include "common/llm_utils.h"
#include "depends/mmpa/src/mmpa_stub.h"
#include "depends/runtime/src/runtime_stub.h"

namespace llm {
class HcclApiStub {
 public:
  HcclApiStub() = default;
  virtual ~HcclApiStub() = default;
  static HcclApiStub &GetInstance();

  virtual HcclResult HcclExchangeMemDesc(HcclComm comm, uint32_t remoteRank, HcclMemDescs *local, int timeout,
                                         HcclMemDescs *remote, uint32_t *actualNum);
  virtual HcclResult HcclCommInitClusterInfoMem(const char *cluster, uint32_t rank, HcclCommConfig *config,
                                                HcclComm *comm);
  virtual HcclResult HcclCommDestroy(HcclComm comm);
  virtual HcclResult HcclBatchPut(HcclComm comm, uint32_t remoteRank, HcclOneSideOpDesc *desc, uint32_t descNum,
                                  rtStream_t stream);
  virtual HcclResult HcclBatchGet(HcclComm comm, uint32_t remoteRank, HcclOneSideOpDesc *desc, uint32_t descNum,
                                  rtStream_t stream);
  virtual HcclResult HcclRemapRegistedMemory(HcclComm *comm, HcclMem *memInfoArray, uint64_t commSize,
                                             uint64_t arraySize);
  virtual HcclResult HcclRegisterGlobalMem(HcclMem *mem, void **memHandle);

  virtual HcclResult HcclDeregisterGlobalMem(void *memHandle);

  virtual HcclResult HcclCommBindMem(HcclComm comm, void *memHandle);

  virtual HcclResult HcclCommUnbindMem(HcclComm comm, void *memHandle);

  virtual HcclResult HcclCommPrepare(HcclComm comm, HcclPrepareConfig *prepareConfig, int32_t timeout);

  static void SetStub(std::unique_ptr<HcclApiStub> instance);
  static void ResetStub();
 private:
  static std::unique_ptr<HcclApiStub> instance_;
};

class MockHccnTool : public llm::MmpaStubApiGe {
 public:
  static void Install() {
    llm::MmpaStub::GetInstance().SetImpl(std::make_shared<MockHccnTool>());
  }
  static void Reset() {
    HcclApiStub::ResetStub();
    llm::MmpaStub::GetInstance().Reset();
  }

  INT32 Access(const CHAR *path_name) override {
    return 0;
  }
};

class MockGetHccnResult : public llm::MmpaStubApiGe {
  public:
  static void Install() {
    llm::MmpaStub::GetInstance().SetImpl(std::make_shared<MockGetHccnResult>());
  }
  static void Reset() {
    HcclApiStub::ResetStub();
    llm::MmpaStub::GetInstance().Reset();
  }

  INT32 Access(const CHAR *path_name) override {
    return 1;
  }
};

class MockMmpaForHcclApi : public llm::MmpaStubApiGe {
 public:
  static void Install() {
    llm::MmpaStub::GetInstance().SetImpl(std::make_shared<MockMmpaForHcclApi>());
  }
  static void Reset() {
    HcclApiStub::ResetStub();
    llm::MmpaStub::GetInstance().Reset();
  }

  void *DlOpen(const char *file_name, int32_t mode) override;

  void *DlSym(void *handle, const char *func_name) override;

  int32_t DlClose(void *handle) override;

  int32_t RealPath(const CHAR *path, CHAR *realPath, INT32 realPathLen) override {
    std::string stub_path = path;
    if (stub_path == "/etc/hccn.conf") {
      stub_path = "/tmp/hccn.conf";
    }
    memcpy_s(realPath, realPathLen, stub_path.c_str(), stub_path.length());
    std::string tempPath = "/tmp/hccn.conf";
    if (FILE *file = fopen(tempPath.c_str(), "r")) {
      if (fclose(file) == 0) {
        std::cout << "Successfully closed the file /tmp/hccn.conf" << std::endl;
      }
      return 0;
    } else {
      return 1;
    }
  }

 private:
  static HcclResult HcclExchangeMemDesc(HcclComm comm, uint32_t remoteRank, HcclMemDescs *local, int timeout,
                                        HcclMemDescs *remote, uint32_t *actualNum) {
    return HcclApiStub::GetInstance().HcclExchangeMemDesc(comm, remoteRank, local, timeout, remote, actualNum);
  }
  static HcclResult HcclCommInitClusterInfoMem(const char *cluster, uint32_t rank, HcclCommConfig *config,
                                               HcclComm *comm) {
    return HcclApiStub::GetInstance().HcclCommInitClusterInfoMem(cluster, rank, config, comm);
  }
  static HcclResult HcclCommDestroy(HcclComm comm) {
    return HcclApiStub::GetInstance().HcclCommDestroy(comm);
  }
  static HcclResult HcclBatchPut(HcclComm comm, uint32_t remoteRank, HcclOneSideOpDesc *desc, uint32_t descNum,
                                 rtStream_t stream) {
    return HcclApiStub::GetInstance().HcclBatchPut(comm, remoteRank, desc, descNum, stream);
  }
  static HcclResult HcclBatchGet(HcclComm comm, uint32_t remoteRank, HcclOneSideOpDesc *desc, uint32_t descNum,
                                 rtStream_t stream) {
    return HcclApiStub::GetInstance().HcclBatchGet(comm, remoteRank, desc, descNum, stream);
  }
  static void HcclCommConfigInit(HcclCommConfig *config) {
  }
  static HcclResult HcclRemapRegistedMemory(HcclComm *comm, HcclMem *memInfoArray, uint64_t commSize,
      uint64_t arraySize) {
    return HcclApiStub::GetInstance().HcclRemapRegistedMemory(comm, memInfoArray, commSize, arraySize);
  }

  static HcclResult HcclRegisterGlobalMem(HcclMem *mem, void **memHandle) {
    return HcclApiStub::GetInstance().HcclRegisterGlobalMem(mem, memHandle);
  }

  static HcclResult HcclDeregisterGlobalMem(void *memHandle) {
    return HcclApiStub::GetInstance().HcclDeregisterGlobalMem(memHandle);
  }

  static HcclResult HcclCommBindMem(HcclComm comm, void *memHandle) {
    return HcclApiStub::GetInstance().HcclCommBindMem(comm, memHandle);
  }

  static HcclResult HcclCommUnbindMem(HcclComm comm, void *memHandle) {
    return HcclApiStub::GetInstance().HcclCommUnbindMem(comm, memHandle);
  }

  static HcclResult HcclCommPrepare(HcclComm comm, HcclPrepareConfig *prepareConfig, int32_t timeout) {
    return HcclApiStub::GetInstance().HcclCommPrepare(comm, prepareConfig, timeout);
  }
};

class DataCacheEngineRuntimeMock : public llm::RuntimeStub {
 public:
  rtError_t rtEventQueryStatus(rtEvent_t evt, rtEventStatus_t *status) override {
    if (++counter_ % 5 == 0) {
      *status = RT_EVENT_RECORDED;
    } else {
      *status = RT_EVENT_INIT;
    }
    return RT_ERROR_NONE;
  }

 private:
  int32_t counter_ = 0;
};

class AutoCommResRuntimeMock : public llm::RuntimeStub {
 public:
  static void Install() {
    llm::RuntimeStub::SetInstance(std::make_shared<AutoCommResRuntimeMock>());
    WriteHccnConfFile();
  }

  static void Reset() {
    llm::RuntimeStub::Reset();
    RemoveHccnConfFile();
  }

  static void InstallWithoutHccnConfFile() {
    llm::RuntimeStub::SetInstance(std::make_shared<AutoCommResRuntimeMock>());
  }

  static void ResetWithoutHccnConfFile() {
    llm::RuntimeStub::Reset();
  }

  static void DeleteHccnConfIfExist() {
    std::string path = "/tmp/hccn.conf";
    if (FILE *file = fopen(path.c_str(), "r")) {
      if (fclose(file) == 0) {
        std::cout << "Successfully closed the file /tmp/hccn.conf" << std::endl;
      }
      if (std::remove(path.c_str())) {
        std::cout << "Successfully deleted the file /tmp/hccn.conf" << std::endl;
      }
    }
  }

  rtError_t rtGetSocVersion(char *version, const uint32_t maxLen) override {
    (void)strcpy_s(version, maxLen, "Ascend910_9391");
    return RT_ERROR_NONE;
  }

  static void SetDevice(int32_t device_id) {
    device_id_ = device_id;
  }

  rtError_t rtGetDevice(int32_t *deviceId) override {
    *deviceId = device_id_;
    return RT_ERROR_NONE;
  }

  rtError_t rtsMemcpyBatch(void **dsts, void **srcs, size_t *sizes, size_t count, rtMemcpyBatchAttr *attrs,
                                   size_t *attrs_idxs, size_t num_attrs, size_t *fail_idx) override {
    return ACL_ERROR_RT_FEATURE_NOT_SUPPORT;
  }

 private:
  // write /tmp/hccn.conf
  static void WriteHccnConfFile() {
    const std::string file_path = "/tmp/hccn.conf";
    std::ofstream file(file_path);
    if (!file.is_open()) {
      std::cout << "Failed to create file:" << file_path << std::endl;
      return;
    }

    file << "netmask_0=1.2.3.4\n"
         << "address_0=1.1.1.0\n"
         << "netmask_1=1.2.3.4\n"
         << "address_1=1.1.1.1\n"
         << "netmask_2=1.2.3.4\n"
         << "address_2=1.1.1.2\n"
         << "netmask_3=1.2.3.4\n"
         << "address_3=1.1.1.3\n"
         << "netmask_4=1.2.3.4\n"
         << "address_4=1.1.1.4\n"
         << "netmask_5=1.2.3.4\n"
         << "address_5=1.1.1.5\n"
         << "netmask_6=1.2.3.4\n"
         << "address_6=1.1.1.6\n"
         << "netmask_7=1.2.3.4\n"
         << "address_7=1.1.1.7\n";

    file.close();
  }

  // remove /tmp/hccn.conf
  static void RemoveHccnConfFile() {
    const std::string file_path = "/tmp/hccn.conf";
    if (std::remove(file_path.c_str()) != 0) {
      std::cout << "Failed to delete file:" << file_path.c_str() << std::endl;
    }
  }

  static int32_t device_id_;
};

class DataCacheEngineTestContext {
 public:
  void Finalize();

  void Initialize(size_t pool_size = 102428800, bool use_host_pool = false, bool use_batch_get = false);

  llm::CommEntity &GetCommEntry();
  llm::CommEntityManager &GetCommEntityManager();

  llm::DataCacheEngine &CacheEngine();

  static void LinkEntities(llm::CommEntity &src_comm_entity,
                           llm::CommEntity &dst_comm_entity,
                           llm::CommEntityManager &src_comm_entity_manager,
                           llm::CommEntityManager &dst_comm_entity_manager,
                           bool remote_cache_accessible = false);

 private:
  llm::DataCacheEngine cache_engine_;
  std::shared_ptr<llm::CommEntity> comm_entity_;
  llm::CacheManager cache_manager_;
  llm::CommEntityManager comm_entity_manager_;
  llm::CommMemManager comm_mem_manager_;
};

class DataCacheEngineTestRunner {
 public:
  explicit DataCacheEngineTestRunner(size_t pool_size = 100 * 1024 * 1024, bool use_host_pool = false, bool use_batch_get = false) {
    src_test_context_.Initialize(pool_size, false, use_batch_get);
    dst_test_context_.Initialize(pool_size, use_host_pool, use_batch_get);
    alloc_host_mem_ = use_host_pool;
    llm::DataCacheEngineTestContext::LinkEntities(src_test_context_.GetCommEntry(),
                                                  dst_test_context_.GetCommEntry(),
                                                  src_test_context_.GetCommEntityManager(),
                                                  dst_test_context_.GetCommEntityManager(),
                                                  use_batch_get);
  }

  ~DataCacheEngineTestRunner() {
    Finalize();
  }

  void Initialize(const llm::CacheDesc &src_cache_desc,
                  const llm::CacheDesc &dst_cache_desc,
                  const llm::PullCacheParam &pull_cache_param,
                  const std::vector<llm::CacheKey> *cache_keys = nullptr) {
    std::vector<llm::CacheKey> src_cachey_keys;
    std::vector<llm::CacheKey> dst_cachey_keys;
    if (!pull_cache_param.prompt_blocks.empty()) {
      src_cachey_keys.resize(1);
      src_cachey_keys.front().is_allocate_blocks = true;
    }
    if (cache_keys != nullptr) {
      src_cachey_keys = *cache_keys;
    }

    AllocateOrRegisterCache(src_cache_desc, src_cachey_keys, true);
    AllocateOrRegisterCache(dst_cache_desc, dst_cachey_keys, false);
  }

  void InitializeV2(const llm::CacheDesc &src_cache_desc,
                    const llm::CacheDesc &dst_cache_desc,
                    const llm::PullCacheParam &pull_cache_param,
                    std::vector<llm::CacheKey> &dst_cache_keys) {
    std::vector<llm::CacheKey> src_cachey_keys;
    AllocateOrRegisterCache(src_cache_desc, src_cachey_keys, true);
    AllocateOrRegisterCache(dst_cache_desc, dst_cache_keys, false);
  }

  void Finalize() {
    src_test_context_.Finalize();
    dst_test_context_.Finalize();
  }

  void AllocateOrRegisterCache(const CacheDesc &cache_desc, std::vector<CacheKey> &cache_keys, bool src_or_dst) {
    auto &cache_engine = src_or_dst ? src_test_context_.CacheEngine() : dst_test_context_.CacheEngine();
    auto &cache = src_or_dst ? src_cache_ : dst_cache_;
    auto &host_buffers = src_or_dst ? src_host_buffers_ : dst_host_buffers_;
    if ((cache_desc.placement == 1 && (!register_dev_mem_))|| (cache_desc.placement == 0 && alloc_host_mem_)) {
      ASSERT_EQ(cache_engine.Allocate(cache_desc, cache_keys, cache), ge::SUCCESS);
    } else {
      cache.per_device_tensor_addrs.resize(1);
      size_t num_elements = 1;
      for (auto dim : cache_desc.shape) {
        num_elements *= dim;
      }
      host_buffers.resize(cache_desc.num_tensors, std::vector<int32_t>(num_elements));
      for (auto &host_buffer : host_buffers) {
        cache.per_device_tensor_addrs[0].emplace_back(reinterpret_cast<uintptr_t>(&host_buffer[0]));
      }
      ASSERT_EQ(cache_engine.Register(cache_desc, cache_keys, cache), ge::SUCCESS);
    }

    if (src_or_dst) {
      int64_t ele_cnt = 1;
      LLMUtils::CalcElementCntByDims(cache_desc.shape, ele_cnt);
      auto start = reinterpret_cast<int32_t *>(cache.per_device_tensor_addrs[0][0]);
      auto end = start + ele_cnt;
      std::iota(start, end, 1);
      start = reinterpret_cast<int32_t *>(cache.per_device_tensor_addrs[0][cache_desc.num_tensors - 1]);
      end = start + ele_cnt;
      std::iota(start, end, 1);
    }
  }

  ge::Status Run(const llm::PullCacheParam &pull_cache_param, const llm::CacheKey *cache_key = nullptr) {
    llm::CacheKey pull_cache_key{};
    if (cache_key != nullptr) {
      pull_cache_key = *cache_key;
    } else {
      pull_cache_key.prompt_cluster_id = 0;
      pull_cache_key.prompt_cache_id = 1;
    }

    return dst_test_context_.CacheEngine().PullCache(dst_cache_.cache_id, pull_cache_key, pull_cache_param);
  }

  ge::Status RunTransfer(const llm::TransferCacheConfig &transfer_cache_config,
                         const llm::TransferBlockConfig &transfer_block_config) {
    return src_test_context_.CacheEngine().TransferCache(0, transfer_cache_config, transfer_block_config);
  }

    void GetCacheData(std::vector<int32_t> &pull_result, size_t tensor_index = 0) {
      auto pulled_data = reinterpret_cast<int32_t *>(dst_cache_.per_device_tensor_addrs[0][tensor_index]);
    memcpy(pull_result.data(), pulled_data, sizeof(int32_t) * pull_result.size());
  }

  static void GetTransferCacheData(const llm::TransferCacheConfig &transfer_cache_config,
                                   std::vector<int32_t> &pull_result) {
    auto pulled_data = reinterpret_cast<int32_t *>(transfer_cache_config.dst_addrs[0]);
    memcpy(pull_result.data(), pulled_data, sizeof(int32_t) * pull_result.size());
  }

  const Cache &GetDstCache() const {
    return dst_cache_;
  }

  const Cache &GetSrcCache() const {
    return src_cache_;
  }

  DataCacheEngineTestContext &GetSrcTestContext() {
    return src_test_context_;
  }

  void SetRegisterDevMem(bool register_dev_mem) {
    register_dev_mem_ = register_dev_mem;
  }

 private:
  llm::DataCacheEngineTestContext src_test_context_;
  llm::DataCacheEngineTestContext dst_test_context_;
  std::vector<std::vector<int32_t>> src_host_buffers_;
  std::vector<std::vector<int32_t>> dst_host_buffers_;
  llm::Cache src_cache_;
  llm::Cache dst_cache_;
  bool register_dev_mem_ = false;
  bool alloc_host_mem_ = false;
};
}  // namespace llm

#endif // CANN_GRAPH_ENGINE_TESTS_DEPENDS_LLM_ENGINE_DATA_CACHE_ENGINE_TEST_HELPER_H_
