/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#ifndef CANN_HIXL_SRC_HIXL_CS_TRANSFER_POOL_H_
#define CANN_HIXL_SRC_HIXL_CS_TRANSFER_POOL_H_

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>
#include "acl/acl.h"
#include "cs/hixl_cs.h"
#include "hcomm/hcomm_res_defs.h"
#include "common/hixl_utils.h"
#include "load_kernel.h"
#include "rt_external.h"

struct tagHcommExceptionInfoLite;

namespace hixl {

enum class SocType;

class TransferPool {
 public:
  static constexpr uint32_t kMaxPoolSize = 8192U;
  static TransferPool *GetInstance(int32_t device_id);

  struct SlotHandle {
    int32_t device_id;
    uint32_t slot_index;
    aclrtContext ctx;
    aclrtStream stream;
    ThreadHandle thread;
    aclrtNotify notify;
    void *dev_const_one;
    uint32_t notify_id;
    uint64_t notify_addr;
    uint32_t notify_len;
    uint8_t *err_flag_host_addr;
    uint64_t err_flag_dev_addr;
    // 该 slot 在 pool 级 flag block 中的 8 字节 device 地址，非 HCCS device 的完成通知 flag。
    uint64_t flag_dev_addr{0};
    uint64_t launched_tasks{0};
    // Worker stream for the UB_MEM AICPU kernels, created on the first unfolded transfer because the
    // kernel parameters carry its RTSQ metadata. The Host memcpy path uses slot.stream instead. Freed
    // on slot teardown.
    aclrtStream ubmem_stream{nullptr};
  };

  TransferPool(const TransferPool &) = delete;
  TransferPool &operator=(const TransferPool &) = delete;

  Status Initialize(uint32_t pool_size);
  void Finalize();
  Status Acquire(SlotHandle *handle);
  void Release(const SlotHandle &handle);
  void Abort(const SlotHandle &handle);
  bool IsInitialized() const;
  bool IsHostExceptionEnabled() const;
  Status EnsureUbMemStream(SlotHandle &handle);
  // aclrtSetDeviceTaskAbortCallback(POST)回调专用：标记设备已 task abort，线程需重建。
  void OnDeviceAbortPost();

 private:
  // 设备 abort 后消费全局标志，全量重建空闲 slot 的 hcomm 线程（持 mu_）。
  void EnsureThreadsRebuiltIfNeededLocked();
  void DeleteStaleSlotContextsLocked();
  std::vector<uint32_t> RebuildSlotThreadsLocked();
  void AddRebuiltSlotContextsLocked(const std::vector<uint32_t> &rebuilt_idx);

 public:
  Status GetAllSlots(std::vector<SlotHandle> &out) const;
  Status ResolveNotifyAddr();
  aclrtContext GetContext() const;
  aclrtFuncHandle GetDeviceKernelFunc(bool is_get, CommProtocol protocol = COMM_PROTOCOL_ROCE) const;

  explicit TransferPool(int32_t device_id);
  ~TransferPool();

 private:
  struct Slot {
    bool in_use;
    aclrtContext ctx;
    aclrtStream stream;
    ThreadHandle thread;
    aclrtNotify notify;
    uint32_t notify_id;
    uint64_t notify_addr;
    uint32_t notify_len;
    uint8_t *err_flag_host_addr;
    uint64_t err_flag_dev_addr;
    // 该 slot 在 pool 级 flag block 中的 8 字节 device 地址，非 HCCS device 的完成通知 flag。
    uint64_t flag_dev_addr{0};
    uint64_t launched_tasks{0};
    // 当前 stream 已成功 abort，后续清理跳过重复 abort；失败时保留 false 以便重试。
    bool cb_aborted{false};
    aclrtStream ubmem_stream{nullptr};
  };

  void InitFreeListLocked();
  void ResetSlotsLocked(uint32_t pool_size);
  Status InitializeResourcesLocked();
  Status InitOneSlotLocked(Slot &slot, uint32_t slot_index) const;
  Status ResolveNotifyAddressLocked(Slot &slot) const;
  Status EnsureNotifyLocked(Slot &slot) const;
  static void ResetNotifyResourcesLocked(Slot &slot);
  static Status CreateNotifyLocked(Slot &slot);
  Status InitAllSlotsLocked();
  void DeinitAllSlotsLocked();
  Status EnsureContextLocked(Slot &slot) const;
  Status EnsureDefaultStreamLocked(Slot &slot) const;
  Status EnsureThreadLocked(Slot &slot) const;
  Status CreateUbMemStreamLocked(Slot &slot) const;
  static void DestroyUbMemStreamLocked(Slot &slot);
  Status DestroySlotLocked(Slot &slot, bool sync_context = true) const;

  Status EnsureErrFlagMemLocked();
  Status AllocErrFlagHostMappedLocked(uint64_t total_size, uint32_t logic_dev_id);
  Status AllocErrFlagDeviceMappedLocked(uint64_t total_size, uint32_t logic_dev_id);
  void FreeErrFlagMemLocked();
  void AssignSlotErrFlagLocked(Slot &slot, uint32_t slot_index) const;
  void ResetSlotErrFlagLocked(uint32_t slot_index) const;

  Status EnsureFlagBlockLocked();
  void FreeFlagBlockLocked();
  void ResetSlotFlagLocked(Slot &slot) const;

  // host 异常回调：进程级注册唯一一个回调，按 deviceId 路由到 pool，按 thread 匹配 in_use slot。
  // Enable/Disable 绝不在 mu_ 下调用（与 hcomm 回调线程存在可证死锁环）。
  // 注销只在 ~TransferPool 执行；Finalize 不注销，pool 对象仍存活，回调靠 inited_ 自守。
  bool EnableHostExceptionCallback(SocType soc_type);
  void DisableHostExceptionCallback();
  static void OnHostException(const tagHcommExceptionInfoLite *info, void *user_data);
  void AbortSlotStreamOnHostExceptionLocked(uint32_t idx, const tagHcommExceptionInfoLite &info);

  void AbortInUseStreamsLocked() const;
  static void AbortInUseStreamLocked(const Slot &slot);
  void AbortSlotRuntimeLocked(Slot &slot) const;
  static void ResetAbortSlotNotifyLocked(Slot &slot);
  Status DeleteSlotThreadContextForAbortLocked(Slot &slot, uint32_t slot_index,
                                               uint64_t *out_dispatched = nullptr) const;
  void DestroySlotContextForAbortLocked(Slot &slot) const;
  void CleanupSlotAfterAbortReinitFailureLocked(Slot &slot, uint32_t slot_index) const;
  Status ReinitSlotAfterAbortLocked(Slot &slot, uint32_t slot_index) const;
  void ReturnSlotToFreeListLocked(uint32_t slot_index);
  void AbortSlotByIndexLocked(uint32_t slot_index, uint64_t launched);

  static void FillHandleFromSlot(int32_t device_id, uint32_t index, const Slot &slot, SlotHandle *handle);
  Status LoadOptionalUbMemKernelsLocked();
  Status EnsureDevConstOneLocked();
  Status EnsureDeviceKernelsLocked();
  Status SyncContextsLocked(const std::vector<HixlTransferContextSyncEntry> &entries, uint32_t op,
                            uint32_t expect_state, uint64_t *out_dispatched = nullptr) const;
  Status RunSyncContextOnceLocked(std::vector<HixlTransferContextSyncEntry> &pending, uint32_t op,
                                  uint32_t expect_state, std::vector<HixlTransferContextSyncEntry> &retry_entries,
                                  std::vector<uint32_t> &retry_states, uint64_t *out_dispatched = nullptr) const;
  Status CollectRetrySyncEntries(const std::vector<HixlTransferContextSyncEntry> &entries,
                                 const std::vector<uint32_t> &states, uint32_t op, uint32_t expect_state,
                                 std::vector<HixlTransferContextSyncEntry> &retry_entries,
                                 std::vector<uint32_t> &retry_states) const;
  Status HandleSyncContextTimeout(const std::vector<HixlTransferContextSyncEntry> &pending,
                                  const std::vector<uint32_t> &states, uint32_t op) const;
  Status AddTransferContextsLocked() const;
  Status DeleteTransferContextsLocked(const std::vector<HixlTransferContextSyncEntry> &entries) const;
  Status SyncOneTransferContextLocked(const Slot &slot, uint32_t op, uint32_t expect_state,
                                      uint64_t *out_dispatched = nullptr) const;
  Status LaunchSyncContextKernelLocked(std::vector<HixlTransferContextSyncEntry> &entries,
                                       std::vector<uint32_t> &states, bool readback_entries) const;
  static HixlTransferContextSyncEntry BuildSyncEntry(const Slot &slot, uint32_t op);
  static std::vector<HixlTransferContextSyncEntry> BuildSyncEntriesFromSlots(const std::vector<Slot> &slots,
                                                                             uint32_t op);

  mutable std::mutex mu_{};
  // abort(POST) 标志：回调只置位不抢 mu_，避免持锁做长驱动调用时死锁。
  std::atomic<bool> device_abort_post_{false};
  const int32_t device_id_;
  uint32_t ref_cnt_;
  bool inited_;
  uint32_t pool_size_;
  std::deque<uint32_t> free_list_;
  std::vector<Slot> slots_;
  void *dev_const_one_{nullptr};
  void *err_flag_host_base_{nullptr};
  void *err_flag_device_base_{nullptr};
  bool err_flag_from_device_{false};
  // Pool-level contiguous device flag memory: pool_size * sizeof(uint64_t), one 8-byte flag per slot.
  // Kept isolated from err_flag_* and dev_const_one_ so completion polling never aliases error signalling.
  void *flag_device_base_{nullptr};
  // 本 pool 是否成功启用 host 异常回调（进程级结果），用于下发 param.host_exception_enabled。
  bool host_exception_enabled_{false};
  // 本 pool 是否正持有进程级 host 异常回调注册的引用；保证每个 pool 只计数一次，且释放幂等。
  bool host_cb_ref_held_{false};
  aclrtContext rts_context_{nullptr};
  aclrtBinHandle kernel_bin_handle_{nullptr};
  DeviceFuncHandles device_func_handles_{};
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_CS_TRANSFER_POOL_H_
