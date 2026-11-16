/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_CS_HCOMM_DEVICE_CHANNEL_H_
#define CANN_HIXL_SRC_HIXL_CS_HCOMM_DEVICE_CHANNEL_H_

#include <memory>
#include <unordered_set>
#include <vector>

#include "cs/hcomm_channel.h"
#include "cs/transfer_pool.h"

namespace hixl {

// Completion handle for the device (AICPU kernel) data plane. host_flag is the D2H copy of the slot's
// completion flag; dev_op_desc_buf holds the descriptors uploaded for the kernel.
struct DeviceCompleteHandle {
  uint32_t magic;
  std::shared_ptr<TransferPool::SlotHandle> shared_slot;
  void *host_flag;
  void *dev_op_desc_buf;
};

// Device HCOMM channel: the shared AICPU batch kernel path. Protocol specific notify resources and the
// notify-record flag are the only differences, so they are overridden by the small subclasses below.
class HcommDeviceChannel : public HcommChannel {
 public:
  explicit HcommDeviceChannel(Endpoint &local_endpoint);
  ~HcommDeviceChannel() override;

  Status Initialize(const ChannelContext &ctx) override;
  Status Destroy() override;
  Status QueryCompleteStatus(void *complete_handle, HixlCompleteStatus *status) override;
  Status SetRemoteMemInfo(const CommMem &trans_flag) override;

 protected:
  // Protocol-specific notify setup. Device protocols with nothing to resolve keep the no-op default.
  virtual Status InitNotifyResources() {
    return SUCCESS;
  }
  // Only HCCS records the notify inside the kernel; the other device protocols wait on it from the host.
  virtual bool UseNotifyRecord() const {
    return false;
  }

  Status DoBatchTransferSync(bool is_get, const HixlOneSideOpDesc *desc_list, uint32_t list_num,
                             uint32_t timeout_ms) override;
  Status DoBatchTransferAsync(bool is_get, const HixlOneSideOpDesc *desc_list, uint32_t list_num,
                              void **complete_handle) override;
  Status RegisterNotifyMemForAllSlots(const std::vector<TransferPool::SlotHandle> &slots);

 private:
  Status TransferSyncInternal(bool is_get, const HixlOneSideOpDesc *desc_list, uint32_t list_num, uint32_t timeout_ms);
  Status TransferAsyncInternal(bool is_get, const HixlOneSideOpDesc *desc_list, uint32_t list_num,
                               void **complete_handle);
  Status EnsureDeviceRemoteFlagInited();
  Status ValidateDeviceInputs(uint32_t list_num, const HixlOneSideOpDesc *desc_list) const;
  Status ConvertHostMappedDescs(uint32_t list_num, HixlOneSideOpDesc *desc_list) const;
  Status AcquireSharedSlot(std::shared_ptr<TransferPool::SlotHandle> &slot_out);
  void ReleaseSharedSlotRef(std::shared_ptr<TransferPool::SlotHandle> &slot_ref);
  Status ReleaseDevCompleteHandle(DeviceCompleteHandle *handle);
  Status AllocateHostFlag(void *&host_flag) const;
  Status AllocateDeviceDescBuf(DeviceCompleteHandle &handle, uint32_t total_list_num,
                               const HixlOneSideOpDesc *desc_list) const;
  Status PrepareDeviceRemoteFlagAndKernel(void *&remote_flag) const;
  Status BuildDeviceChunkParam(DeviceCompleteHandle &handle, uint32_t chunk_offset, uint32_t chunk_list_num,
                               bool need_notify_wait, HixlOneSideOpParam &param) const;
  Status LaunchDeviceChunkedKernels(bool is_get, DeviceCompleteHandle &handle, uint32_t list_num) const;
  Status LaunchAndCopyDoneFlag(bool is_get, DeviceCompleteHandle &handle, uint32_t list_num);
  static Status LaunchDeviceKernel(bool is_get, DeviceCompleteHandle &handle, const HixlOneSideOpParam &param,
                                   bool wait_notify);
  Status CheckStatusDevice(DeviceCompleteHandle &query_handle, HixlCompleteStatus &status);
  void CleanupActiveSlot();
  void AbortAllPendingDeviceHandles();
  void ReleaseDeviceResources();

  bool device_remote_flag_inited_{false};
  void *device_remote_flag_addr_{nullptr};
  std::vector<MemHandle> notify_mem_handles_{};
  std::unordered_set<DeviceCompleteHandle *> pending_device_handles_{};
  std::shared_ptr<TransferPool::SlotHandle> active_slot_;
  // The active slot is returned by one Abort when its last reference drops.
  bool abort_active_slot_{false};
};

// HCCS device channel: the peer addresses local memory directly, so notify addresses are not resolved
// and notify memory is not registered; the kernel records its own notify.
class HccsDeviceChannel : public HcommDeviceChannel {
 public:
  explicit HccsDeviceChannel(Endpoint &local_endpoint) : HcommDeviceChannel(local_endpoint) {}
  ~HccsDeviceChannel() override = default;

 protected:
  bool UseNotifyRecord() const override {
    return true;
  }
};

// RoCE device channel: notify addresses are resolved, but notify memory is registered by the peer side
// (not by this channel).
class RoceDeviceChannel : public HcommDeviceChannel {
 public:
  explicit RoceDeviceChannel(Endpoint &local_endpoint) : HcommDeviceChannel(local_endpoint) {}
  ~RoceDeviceChannel() override = default;

 protected:
  Status InitNotifyResources() override;
};

// UB (ub_ctp / uboe / ubg) device channel: notify addresses are resolved and notify memory is registered
// on this side.
class UbDeviceChannel : public HcommDeviceChannel {
 public:
  explicit UbDeviceChannel(Endpoint &local_endpoint) : HcommDeviceChannel(local_endpoint) {}
  ~UbDeviceChannel() override = default;

 protected:
  Status InitNotifyResources() override;
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_CS_HCOMM_DEVICE_CHANNEL_H_
