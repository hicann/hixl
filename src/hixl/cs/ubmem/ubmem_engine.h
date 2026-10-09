/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_CS_UBMEM_UBMEM_ENGINE_H_
#define CANN_HIXL_SRC_HIXL_CS_UBMEM_UBMEM_ENGINE_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_set>
#include <vector>

#include "acl/acl.h"
#include "cs/endpoint.h"
#include "cs/global_config.h"
#include "cs/transfer_pool.h"
#include "hixl/hixl_types.h"

namespace hixl {

// Not thread-safe by itself. HixlCSClient serializes all calls under its mutex; any new caller must
// provide the same external serialization before using this engine.
class UbMemEngine {
 public:
  UbMemEngine(int32_t device_id, const GlobalConfig &global_config, Endpoint &local_endpoint);
  ~UbMemEngine();
  UbMemEngine(const UbMemEngine &) = delete;
  UbMemEngine &operator=(const UbMemEngine &) = delete;
  UbMemEngine(UbMemEngine &&) = delete;
  UbMemEngine &operator=(UbMemEngine &&) = delete;

  static bool OwnsHandle(uint32_t magic);

  Status SubmitAsync(bool is_get, uint32_t list_num, const HixlOneSideOpDesc *desc_list, void **query_handle);
  Status SubmitSync(bool is_get, uint32_t list_num, const HixlOneSideOpDesc *desc_list, uint32_t timeout_ms);
  Status CheckStatus(void *query_handle, HixlCompleteStatus &status);
  void Finalize();

  struct UbMemCompleteHandle {
    uint32_t magic;
    uint32_t desc_count;
    uint32_t status_count;
    std::shared_ptr<TransferPool::SlotHandle> slot;
    void *host_flag;
    void *desc_buf;
    void *status_buf;
  };

 private:
  bool UseMemcpyMode() const;
  Status ValidateInputs(uint32_t list_num, const HixlOneSideOpDesc *desc_list) const;
  Status TranslateUbMemOpDescs(uint32_t list_num, const HixlOneSideOpDesc *src,
                               std::vector<HixlOneSideOpDesc> &dst) const;
  Status AcquireUbMemSlot(std::shared_ptr<TransferPool::SlotHandle> &slot_out);
  void ReleaseUbMemSlot(std::shared_ptr<TransferPool::SlotHandle> &slot_ref);
  Status AllocateUbMemHostFlag(void *&host_flag) const;
  Status AllocateUbMemCompleteHandle(UbMemCompleteHandle *&handle) const;
  void ReleaseUbMemCompleteHandle(UbMemCompleteHandle *handle);
  Status AllocateUbMemStatusBuf(uint32_t status_count, void *&status_buf) const;
  Status CheckAicpuStatus(UbMemCompleteHandle &handle) const;
  Status SubmitMemcpyAsync(bool is_get, uint32_t list_num, const HixlOneSideOpDesc *desc_list, void **query_handle);
  Status SubmitMemcpySync(bool is_get, uint32_t list_num, const HixlOneSideOpDesc *desc_list, uint32_t timeout_ms);
  Status SubmitAicpuAsync(bool is_get, uint32_t list_num, const HixlOneSideOpDesc *desc_list, void **query_handle,
                          uint32_t timeout_ms);
  Status SubmitAicpuSync(bool is_get, uint32_t list_num, const HixlOneSideOpDesc *desc_list, uint32_t timeout_ms);
  Status PrepareAicpuDescBuf(UbMemCompleteHandle &handle, bool is_get, uint32_t list_num,
                             const HixlOneSideOpDesc *desc_list) const;
  Status LaunchAicpuChunks(bool is_get, UbMemCompleteHandle &handle, uint32_t timeout_ms) const;
  Status LaunchUbMemKernel(aclrtFuncHandle func, const char *kernel_name, UbMemCompleteHandle &handle, void *args,
                           size_t args_size, bool wait_notify) const;
  void AbortPendingHandles();
  void AbortUbMemSlot(const TransferPool::SlotHandle &slot) const;

  int32_t device_id_;
  const GlobalConfig &global_config_;
  Endpoint &local_endpoint_;
  std::shared_ptr<TransferPool::SlotHandle> active_slot_;
  std::unordered_set<UbMemCompleteHandle *> pending_handles_;
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_CS_UBMEM_UBMEM_ENGINE_H_
