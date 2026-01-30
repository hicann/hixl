/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_HCCL_SO_MANAGER_H_
#define CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_HCCL_SO_MANAGER_H_

#include <mutex>
#include "acl/acl.h"
#include "mmpa/mmpa_api.h"

#include "llm_datadist/llm_error_codes.h"
#include "hccl_mem_comm.h"

namespace llm {
using HcclExchangeMemDescFunc = HcclResult (*)(HcclComm comm, uint32_t remote_rank, HcclMemDescs *local, int timeout,
                                               HcclMemDescs *remote, uint32_t *actual_num);
using HcclCommConfigInitFunc = void (*)(HcclCommConfig *config);
using HcclCommInitClusterInfoMemConfigFunc = HcclResult (*)(const char *cluster, uint32_t rank, HcclCommConfig *config,
                                                      HcclComm *comm);
using HcclCommDestroyFunc = HcclResult (*)(HcclComm comm);
using HcclBatchPutFunc = HcclResult (*)(HcclComm comm, uint32_t remote_rank, HcclOneSideOpDesc *desc, uint32_t desc_num,
                                        aclrtStream stream);
using HcclBatchGetFunc = HcclResult (*)(HcclComm comm, uint32_t remote_rank, HcclOneSideOpDesc *desc, uint32_t desc_num,
                                        aclrtStream stream);
using HcclRemapRegisteredMemoryFunc = HcclResult (*)(HcclComm *comm, HcclMem *mem_info_array, uint64_t comm_size,
                                                     uint64_t array_size);

using HcclRegisterGlobalMemFunc = HcclResult (*)(HcclMem *mem, void **mem_handle);
using HcclDeregisterGlobalMemFunc = HcclResult (*)(void *mem_handle);
using HcclCommBindMemFunc = HcclResult (*)(HcclComm comm, void *mem_handle);
using HcclCommUnbindMemFunc = HcclResult (*)(HcclComm comm, void *mem_handle);
using HcclCommPrepareFunc = HcclResult (*)(HcclComm comm, HcclPrepareConfig *prepare_config, int32_t timeout);

class HcclAdapter {
 public:
  static HcclAdapter &GetInstance();
  ~HcclAdapter();
  ge::Status Initialize();
  void Finalize();
  HcclResult HcclExchangeMemDesc(HcclComm comm, uint32_t remote_rank, HcclMemDescs *local, int timeout,
                                 HcclMemDescs *remote, uint32_t *actual_num);
  void HcclCommConfigInit(HcclCommConfig *config);
  HcclResult HcclCommInitClusterInfoMemConfig(const char *cluster, uint32_t rank, HcclCommConfig *config,
                                              HcclComm *comm);
  HcclResult HcclCommDestroy(HcclComm comm);
  HcclResult HcclBatchPut(HcclComm comm, uint32_t remote_rank, HcclOneSideOpDesc *desc, uint32_t desc_num,
                          aclrtStream stream);
  HcclResult HcclBatchGet(HcclComm comm, uint32_t remote_rank, HcclOneSideOpDesc *desc, uint32_t desc_num,
                          aclrtStream stream) const;
  HcclResult HcclRemapRegisteredMemory(HcclComm *comm, HcclMem *mem_info_array, uint64_t comm_size,
                                       uint64_t arraySize) const;
  HcclResult HcclRegisterGlobalMem(HcclMem *mem, void **mem_handle) const;
  HcclResult HcclDeregisterGlobalMem(void *mem_handle) const;
  HcclResult HcclCommBindMem(HcclComm comm, void *mem_handle) const;
  HcclResult HcclCommUnbindMem(HcclComm comm, void *mem_handle) const;
  HcclResult HcclCommPrepare(HcclComm comm, HcclPrepareConfig *prepare_config, int32_t timeout) const;
  HcclAdapter(const HcclAdapter &) = delete;
  HcclAdapter(const HcclAdapter &&) = delete;
  HcclAdapter &operator=(const HcclAdapter &) = delete;
  HcclAdapter &operator=(const HcclAdapter &&) = delete;

 private:
  ge::Status LoadSo();
  ge::Status UnloadSo();
  HcclAdapter() = default;

  std::mutex mutex_;
  void *so_handle_ = nullptr;
  HcclExchangeMemDescFunc hccl_exchange_mem_desc_func_{};
  HcclCommInitClusterInfoMemConfigFunc hccl_comm_init_cluster_info_mem_func_{};
  HcclCommDestroyFunc hccl_comm_destroy_func_{};
  HcclBatchPutFunc hccl_batch_put_func_{};
  HcclBatchGetFunc hccl_batch_get_func_{};
  HcclRemapRegisteredMemoryFunc hccl_remap_registered_memory_func_{};
  HcclRegisterGlobalMemFunc hccl_register_global_mem_func_{};
  HcclDeregisterGlobalMemFunc hccl_deregister_global_mem_func_{};
  HcclCommBindMemFunc hccl_comm_bind_mem_func_{};
  HcclCommUnbindMemFunc hccl_comm_unbind_mem_func_{};
  HcclCommPrepareFunc hccl_comm_prepare_func_{};
};

class HcclUtils {
 public:
  static ge::Status ConvertHcclErrorCode(HcclResult hccl_result, ge::Status default_status = ge::FAILED);
};

template<typename T>
class FunctionLoader{
 private:
  union FunctionPointerConverter {
    void* from;
    T to;
  };

 public:
  static T load(void* so_handle, const char* func_name) {
    void* symbol = mmDlsym(so_handle, func_name);
        if (!symbol) {
            return nullptr;
        }
        
        FunctionPointerConverter converter;
        converter.from = symbol;
        return converter.to;
  }
};
}  // namespace llm

#endif  // CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_HCCL_SO_MANAGER_H_
