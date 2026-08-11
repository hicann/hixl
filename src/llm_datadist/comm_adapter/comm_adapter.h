/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HIXL_SRC_LLM_DATADIST_COMM_ADAPTER_COMM_ADAPTER_H_
#define HIXL_SRC_LLM_DATADIST_COMM_ADAPTER_COMM_ADAPTER_H_

#include <mutex>
#include <dlfcn.h>
#include "acl/acl.h"

#include "llm_datadist/llm_error_codes.h"
#include "comm_types.h"

namespace llm {
using DlHcclExchangeMemDescFunc = HcclResult (*)(HcclComm comm, uint32_t remote_rank, HcclMemDescs *local, int timeout,
                                                 HcclMemDescs *remote, uint32_t *actual_num);
using DlHcclCommConfigInitFunc = void (*)(HcclCommConfig *config);
using DlHcclCommInitClusterInfoMemConfigFunc = HcclResult (*)(const char *cluster, uint32_t rank,
                                                              HcclCommConfig *config, HcclComm *comm);
using DlHcclCommDestroyFunc = HcclResult (*)(HcclComm comm);
using DlHcclBatchPutFunc = HcclResult (*)(HcclComm comm, uint32_t remote_rank, HcclOneSideOpDesc *desc,
                                          uint32_t desc_num, aclrtStream stream);
using DlHcclBatchGetFunc = HcclResult (*)(HcclComm comm, uint32_t remote_rank, HcclOneSideOpDesc *desc,
                                          uint32_t desc_num, aclrtStream stream);
using DlHcclRemapRegisteredMemoryFunc = HcclResult (*)(HcclComm *comm, CommMem *mem_info_array, uint64_t comm_size,
                                                       uint64_t array_size);

using DlHcclRegisterGlobalMemFunc = HcclResult (*)(CommMem *mem, void **mem_handle);
using DlHcclDeregisterGlobalMemFunc = HcclResult (*)(void *mem_handle);
using DlHcclCommBindMemFunc = HcclResult (*)(HcclComm comm, void *mem_handle);
using DlHcclCommUnbindMemFunc = HcclResult (*)(HcclComm comm, void *mem_handle);
using DlHcclCommPrepareFunc = HcclResult (*)(HcclComm comm, HcclPrepareConfig *prepare_config, int32_t timeout);

class CommAdapter {
 public:
  static CommAdapter &GetInstance();
  ~CommAdapter();
  ge::Status Initialize();
  void Finalize();
  HcclResult DlHcclExchangeMemDesc(HcclComm comm, uint32_t remote_rank, HcclMemDescs *local, int timeout,
                                   HcclMemDescs *remote, uint32_t *actual_num) const;
  void DlHcclCommConfigInit(HcclCommConfig *config) const;
  HcclResult DlHcclCommInitClusterInfoMemConfig(const char *cluster, uint32_t rank, HcclCommConfig *config,
                                                HcclComm *comm) const;
  HcclResult DlHcclCommDestroy(HcclComm comm) const;
  HcclResult DlHcclBatchPut(HcclComm comm, uint32_t remote_rank, HcclOneSideOpDesc *desc, uint32_t desc_num,
                            aclrtStream stream) const;
  HcclResult DlHcclBatchGet(HcclComm comm, uint32_t remote_rank, HcclOneSideOpDesc *desc, uint32_t desc_num,
                            aclrtStream stream) const;
  HcclResult DlHcclRemapRegisteredMemory(HcclComm *comm, CommMem *mem_info_array, uint64_t comm_size,
                                         uint64_t arraySize) const;
  HcclResult DlHcclRegisterGlobalMem(CommMem *mem, void **mem_handle) const;
  HcclResult DlHcclDeregisterGlobalMem(void *mem_handle) const;
  HcclResult DlHcclCommBindMem(HcclComm comm, void *mem_handle) const;
  HcclResult DlHcclCommUnbindMem(HcclComm comm, void *mem_handle) const;
  HcclResult DlHcclCommPrepare(HcclComm comm, HcclPrepareConfig *prepare_config, int32_t timeout) const;
  CommAdapter(const CommAdapter &) = delete;
  CommAdapter(const CommAdapter &&) = delete;
  CommAdapter &operator=(const CommAdapter &) = delete;
  CommAdapter &operator=(const CommAdapter &&) = delete;

 private:
  ge::Status LoadSo();
  ge::Status UnloadSo();
  CommAdapter() = default;

  std::mutex mutex_;
  void *so_handle_ = nullptr;
  DlHcclExchangeMemDescFunc dl_hccl_exchange_mem_desc_func_{};
  DlHcclCommInitClusterInfoMemConfigFunc dl_hccl_comm_init_cluster_info_mem_func_{};
  DlHcclCommDestroyFunc dl_hccl_comm_destroy_func_{};
  DlHcclBatchPutFunc dl_hccl_batch_put_func_{};
  DlHcclBatchGetFunc dl_hccl_batch_get_func_{};
  DlHcclRemapRegisteredMemoryFunc dl_hccl_remap_registered_memory_func_{};
  DlHcclRegisterGlobalMemFunc dl_hccl_register_global_mem_func_{};
  DlHcclDeregisterGlobalMemFunc dl_hccl_deregister_global_mem_func_{};
  DlHcclCommBindMemFunc dl_hccl_comm_bind_mem_func_{};
  DlHcclCommUnbindMemFunc dl_hccl_comm_unbind_mem_func_{};
  DlHcclCommPrepareFunc dl_hccl_comm_prepare_func_{};
};

class CommUtils {
 public:
  static ge::Status ConvertCommErrorToStatus(HcclResult hccl_result, ge::Status default_status = ge::FAILED);
  static const std::string ConvertCommMemTypeToString(CommMemType type);
};

template <typename T>
class FunctionLoader {
 private:
  union FunctionPointerConverter {
    void *from;
    T to;
  };

 public:
  static T load(void *so_handle, const char *func_name) {
    void *symbol = dlsym(so_handle, func_name);
    if (!symbol) {
      return nullptr;
    }

    FunctionPointerConverter converter;
    converter.from = symbol;
    return converter.to;
  }
};
}  // namespace llm

#endif  // HIXL_SRC_LLM_DATADIST_COMM_ADAPTER_COMM_ADAPTER_H_
