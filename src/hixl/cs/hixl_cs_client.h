/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_CS_HIXL_CS_CLIENT_H_
#define CANN_HIXL_SRC_HIXL_CS_HIXL_CS_CLIENT_H_

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <array>
#include <unordered_set>
#include <vector>
#include <map>
#include "cs/hixl_cs.h"
#include "common/hixl_inner_types.h"
#include "endpoint.h"
#include "channel.h"
#include "hixl_mem_store.h"
#include "transfer_pool.h"
#include "hcomm/hcomm_res_defs.h"
#include "hixl_mem_store.h"

namespace hixl {
struct CompleteHandleInfo {
  uint32_t magic;
  int32_t flag_index;
  uint64_t *flag_address;
};

enum class DeviceOpType : uint32_t {
  kGet = 0U,
  kPut = 1U,
};

struct DeviceKernelArgs {
  ThreadHandle thread;
  ChannelHandle channel;
  void *dev_flag;
  uint32_t list_num;
  DeviceOpType op;
};

struct DeviceArgs {
  ThreadHandle thread;
  ChannelHandle channel;
  uint32_t list_num;
  void **dst_buf_list;
  void **src_buf_list;
  uint64_t *len_list;
  uint64_t remote_flag;
  uint64_t local_flag;
  uint32_t flag_size;
};

struct MemDev {
  void *dst_buf_list_dev;
  void *src_buf_list_dev;
  uint64_t *len_list_dev;
};

struct DeviceCompleteHandle {
  uint32_t magic;
  uint32_t reserved;
  std::unique_ptr<TransferPool::SlotHandle> slot;
  DeviceArgs args;
  MemDev mem_dev;
};

struct CommunicateMem {
  uint32_t list_num;
  void **dst_buf_list;
  const void **src_buf_list;
  uint64_t *len_list;
};

struct Buffers {
  const void *remote;
  const void *local;
};

struct ImportCtx {
  Endpoint *ep{nullptr};
  EndpointHandle ep_handle{nullptr};
  HixlMemStore *store{nullptr};
  uint32_t num{0U};
  bool need_tag{false};
  std::vector<CommMem> imported;
  std::vector<void *> recorded_addrs;
  std::map<std::string, CommMem> tag_mem_map;
  std::vector<CommMem> mems;
  std::vector<std::vector<char>> tag_storage;
};

class HixlCSClient {
 public:
  HixlCSClient();
  ~HixlCSClient();
  Status Create(const HixlClientDesc *client_desc, const HixlClientConfig *config);

  Status Connect(uint32_t timeout_ms);

  Status GetRemoteMem(CommMem **remote_mem_list, char ***mem_tag_list, uint32_t *list_num, uint32_t timeout_ms);

  // 注册client的endpoint的内存信息到内存注册表中。
  Status RegMem(const char *mem_tag, const CommMem *mem, MemHandle *mem_handle);

  // 通过已经建立好的channel，从用户提供的地址列表等信息，进行数据传输
  Status BatchTransfer(bool is_get, CommunicateMem &communicate_mem_param, void **query_handle);

  Status BatchTransferSync(bool is_get, CommunicateMem &communicate_mem_param, uint32_t timeout_ms);

  // 通过已经建立好的channel，检查批量读写的状态。
  Status CheckStatus(void *query_handle, HixlCompleteStatus *status);

  // 注销client的endpoint的内存信息。
  Status UnRegMem(MemHandle mem_handle);

  Status Destroy();

  static bool IsDeviceEndpoint(const EndpointDesc &ep);

 private:
  void ReleaseFlagIndex(int32_t flag_index);
  Status InitBaseClient(const HixlClientDesc *client_desc);
  Status InitDeviceResource();
  Status InitDeviceConstMemory();
  Status ExchangeEndpointAndCreateChannelLocked(uint32_t timeout_ms);
  Status GetRemoteMemLocked(uint32_t timeout_ms, CommMem **remote_mem_list, char ***mem_tag_list, uint32_t *list_num);
  Status InitFlagQueue() noexcept;
  int32_t AcquireFlagIndex();
  Status ReleaseCompleteHandle(CompleteHandleInfo *query_handle);
  Status ReleaseDevCompleteHandle(DeviceCompleteHandle *handle);
  Status CheckStatusHost(CompleteHandleInfo &query_handle, HixlCompleteStatus &status);
  Status CheckStatusDevice(DeviceCompleteHandle &query_handle, HixlCompleteStatus &status);
  Status BatchTransferHost(bool is_get, const CommunicateMem &communicate_mem, void **query_handle);
  Status BatchTransferHostSync(bool is_get, const CommunicateMem &communicate_mem, uint32_t timeout_ms);
  Status BatchTransferDevice(bool is_get, const CommunicateMem &communicate_mem, void **query_handle);
  Status BatchTransferDeviceSync(bool is_get, const CommunicateMem &communicate_mem, uint32_t timeout_ms);
  template <typename T>
  Status ConvertHostRegisterAddr(bool is_server, const char *name, T &addr);
  Status ConvertUboeCommunicateMem(bool is_get, CommunicateMem &communicate_mem_param);
  Status EnsureDeviceRemoteFlagInitedLocked();
  Status EnsureDeviceKernelLoadedLocked();
  void *GetDeviceKernelFunc(bool is_get);
  Status ImportRemoteMem(std::vector<HixlMemDesc> &desc_list, CommMem **remote_mem_list, char ***mem_tag_list,
                         uint32_t *list_num);
  Status ValidateAddress(bool is_get, const CommunicateMem &communicate_mem_param);
  Status TransferWithRetry(bool is_get, uint64_t channel_handle, void *dst_buf, const void *src_buf, uint64_t len);
  Status BatchTransferTask(bool is_get, const CommunicateMem &communicate_mem_param);
  void FillOutputParams(ImportCtx &ctx, CommMem **remote_mem_list, char ***mem_tag_list, uint32_t *list_num);
  Status ClearRemoteMemInfo();
  Status ValidateDeviceInputs(bool is_get, const CommunicateMem &mem_param, void *&query_handle) const;
  Status PrepareDeviceRemoteFlagAndKernel(void *&remote_flag);
  Status PrepareDeviceBatchMemBuffers(const CommunicateMem &communicate_mem_param, MemDev &mem_dev) const;
  Status ResolveNotifyDeviceAddress(aclrtNotify notify, uint64_t &notify_addr, uint32_t &notify_len);
  Status RegisterNotifyMemForAllSlots(const std::vector<TransferPool::SlotHandle> &slots);
  Status FillDeviceArgs(const CommunicateMem &mem_param, MemDev &mem_dev, const TransferPool::SlotHandle &slot,
                        void *remote_flag, DeviceArgs &args);
  Status LaunchDeviceKernel(bool is_get, DeviceCompleteHandle &handle, const void *remote_flag);
  void ReleaseLegacyHandlesLocked();
  void AbortAllPendingDeviceHandlesLocked();
  void ReleaseDeviceResourcesLocked();

 private:
  std::mutex mutex_;
  // 用于记录内存地址的分配情况
  HixlMemStore mem_store_;
  std::string server_ip_;
  uint32_t server_port_{0U};
  EndpointPtr local_endpoint_;
  EndpointDesc remote_endpoint_{};
  uint8_t tc_{kRdmaTrafficClass};
  uint8_t sl_{kRdmaServiceLevel};
  Channel client_channel_;
  ChannelHandle client_channel_handle_ = 0UL;
  uint64_t remote_endpoint_handle_{0U};
  static constexpr size_t kFlagQueueSize = 4096;  // 用于初始化队列和内存地址列表
  uint64_t *flag_queue_ = nullptr;
  std::array<uint32_t, kFlagQueueSize> available_indices_{};
  size_t top_index_ = 0;  // 栈顶指针
  std::mutex indices_mutex_;
  std::array<CompleteHandleInfo *, kFlagQueueSize> live_handles_{};  // 用来记录读写生成的 query_handle
  int32_t socket_ = -1;
  std::map<std::string, CommMem> tag_mem_descs_;
  std::vector<CommMem> remote_mems_out_;
  std::vector<std::vector<char>> remote_tag_storage_;
  std::vector<char *> remote_tag_ptrs_;
  std::vector<void *> recorded_remote_addrs_;
  std::vector<CommMem> imported_remote_bufs_;
  std::vector<HixlMemDesc> desc_list_;
  int32_t device_id_{-1};
  std::mutex device_mu_;
  bool device_remote_flag_inited_{false};
  void *device_remote_flag_addr_{nullptr};
  uint64_t device_remote_flag_size_{0ULL};
  bool device_kernel_loaded_{false};
  aclrtBinHandle device_kernel_handle_{nullptr};
  void *device_func_get_{nullptr};
  void *device_func_put_{nullptr};
  void *dev_const_one_{nullptr};
  std::vector<MemHandle> notify_mem_handles_{};
  std::unordered_set<DeviceCompleteHandle *> pending_device_handles_{};
};
}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_CS_HIXL_CS_CLIENT_H_
