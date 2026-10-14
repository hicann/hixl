/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_ENGINE_UB_CLIENT_HANDLER_H_
#define CANN_HIXL_SRC_HIXL_ENGINE_UB_CLIENT_HANDLER_H_

#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>
#include "engine/client_handler.h"
#include "engine/client_handler_factory.h"
#include "common/segment.h"

namespace hixl {
// 并发契约：本类不保证 Finalize 与传输/查询的并发互斥，须由上层（HixlClient）串行化
// 生命周期操作与传输操作；类内锁仅用于共享记账的一致性保护。
class UbClientHandler : public IClientHandler {
 public:
  static Status Create(const HandlerCreateArgs &args, std::unique_ptr<UbClientHandler> &out);
  explicit UbClientHandler(std::map<CommType, HixlClientHandle> handles, const std::string &local_engine = "",
                           const std::string &remote_engine = "",
                           std::map<CommType, HandlerCreateArgs::EndpointPair> link_pairs = {});
  ~UbClientHandler() override = default;

  Status Connect(uint32_t timeout_ms) override;
  Status RegisterMem(const MemHandleInfo &mem_info) override;
  Status DeregisterMem(MemHandle mem_handle) override;
  Status TransferAsync(const std::vector<TransferOpDesc> &op_descs, TransferOp operation, TransferReq &req) override;
  Status TransferSync(const std::vector<TransferOpDesc> &op_descs, TransferOp operation, uint32_t timeout_ms) override;
  Status GetTransferStatus(const TransferReq &req, TransferStatus &status) override;
  Status Finalize() override;
  void Dump(const char *reason, DumpLogLevel level = DumpLogLevel::EVENT) const override;

 protected:
  /** 异步子请求（终态缓存：收敛即删，已终态不再下查） */
  struct SubReq {
    CommType type;
    TransferReq req{nullptr};
    TransferStatus status{TransferStatus::WAITING};
  };
  using SubReqList = std::vector<SubReq>;

  // ==================== 子类扩展点（钩子）====================
  // ---- 数据视图 ----
  virtual std::vector<HixlClientHandle> GetHandlesForType(CommType type) const;
  virtual std::map<CommType, std::vector<HixlClientHandle>> GetAllLinkHandles() const;

  // ---- 创建 ----
  virtual Status CreateHandlesForType(CommType type, const HixlClientDesc &desc, const HixlClientConfig &config,
                                      const HandlerCreateArgs &args);

  // ---- 传输 ----
  /** is_async 回填 req，同步阻塞至完成 */
  virtual Status SubmitDescsForType(CommType type, const std::vector<TransferOpDesc> &descs, bool is_get,
                                    uint32_t timeout_ms, bool is_async, TransferReq &req);
  /** 须持 complete_handles_mutex_ 调用 */
  virtual Status CheckSubReqStatus(const SubReq &sub, TransferStatus &sub_status);

  // ---- Finalize（调用方已持锁）----
  virtual void ClearTransferStateLocked();
  virtual void DestroyHandlesLocked();

  // ==================== 公共实现（基类与子类共用）====================
  /** 只建句柄，不建连接 */
  Status InitLinkPairs(const HandlerCreateArgs &args);
  /** Create 尾段：lazy 字段 + 对端段表 */
  Status InitRemoteState(const HandlerCreateArgs &args);
  Status EnsureLinksConnected(const std::vector<CommType> &types, uint32_t timeout_ms);
  /** 每类型可含多个 worker，并行连接 */
  Status ConnectHandles(const std::map<CommType, std::vector<HixlClientHandle>> &handles, uint32_t timeout_ms);
  Status ClassifyTransfers(const std::vector<TransferOpDesc> &op_descs,
                           std::map<CommType, std::vector<TransferOpDesc>> &table) const;
  Status GetMemType(const std::vector<SegmentPtr> &segments, uintptr_t addr, size_t len, MemType &mem_type) const;
  Status BuildRemoteSegmentsFromMemInfo(const std::vector<MemInfo> &mem_info_list);
  /** 须持 mem/handle 锁调用 */
  void UnregisterAllMemLocked();
  Status AddLocalRange(uintptr_t addr, uint64_t len, MemType type);
  void RemoveLocalRange(uintptr_t addr, uint64_t len, MemType type);
  Status RollbackRegisteredHandles(const std::map<HixlClientHandle, MemHandle> &addr_handles, uintptr_t addr);
  Status UnregOneHandle(HixlClientHandle client, MemHandle mh, uintptr_t addr);
  void EraseMemHandle(MemHandle mh);

  // ==================== 状态 ====================
  struct AddrMemRecord {
    uintptr_t addr{0};
    uint64_t len{0};
    MemType type{MEM_DEVICE};
    std::map<HixlClientHandle, MemHandle> handles;
  };

  std::map<CommType, HixlClientHandle> handles_;
  std::string local_engine_;
  std::string remote_engine_;
  std::map<CommType, HandlerCreateArgs::EndpointPair> link_pairs_;
  std::map<CommType, std::vector<MemHandle>> mem_handles_;
  std::map<MemHandle, AddrMemRecord> handle_to_mem_record_;
  std::vector<SegmentPtr> local_segments_;
  std::vector<SegmentPtr> remote_segments_;
  std::map<TransferReq, SubReqList> async_workers_;
  mutable std::mutex handle_mutex_;
  mutable std::mutex mem_handle_mutex_;
  mutable std::mutex local_seg_mutex_;
  mutable std::mutex remote_seg_mutex_;
  mutable std::mutex complete_handles_mutex_;  // 异步记账：async_workers_ 读写
  mutable std::mutex sync_submit_mutex_;       // 同步传输独占，与 complete_handles_mutex_ 解耦

  bool lazy_mode_ = false;
  bool connect_triggered_ = false;
  std::set<CommType> connected_types_;
  uint32_t connect_timeout_ms_ = 0;
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_ENGINE_UB_CLIENT_HANDLER_H_
