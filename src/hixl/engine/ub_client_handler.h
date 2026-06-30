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

#include <map>
#include <mutex>
#include <set>
#include <vector>
#include "engine/client_handler.h"
#include "engine/client_handler_factory.h"

namespace hixl {
class UbClientHandler : public IClientHandler {
 public:
  static Status Create(const HandlerCreateArgs &args, std::unique_ptr<UbClientHandler> &out);
  explicit UbClientHandler(std::map<CommType, HixlClientHandle> handles);
  ~UbClientHandler() override = default;

  Status Connect(uint32_t timeout_ms) override;
  Status RegisterMem(const MemHandleInfo &mem_info) override;
  Status TransferAsync(const std::vector<TransferOpDesc> &op_descs, TransferOp operation, TransferReq &req) override;
  Status TransferSync(const std::vector<TransferOpDesc> &op_descs, TransferOp operation, uint32_t timeout_ms) override;
  Status GetTransferStatus(const TransferReq &req, TransferStatus &status) override;
  Status Finalize() override;

 private:
  Status ClassifyTransfers(const std::vector<TransferOpDesc> &op_descs,
                           std::map<CommType, std::vector<TransferOpDesc>> &table);
  Status GetMemType(const std::vector<SegmentPtr> &segments, uintptr_t addr, size_t len, MemType &mem_type) const;

  /**
   * @brief 懒惰模式下确保传输所需链路已连接
   */
  Status EnsureLinksConnected(const std::vector<CommType> &types, uint32_t timeout_ms);

  /**
   * @brief 并行连接一组链路并批量标记
   */
  Status ConnectHandles(const std::map<CommType, HixlClientHandle> &handles, uint32_t timeout_ms);

  /**
   * @brief 从MemInfo列表构建remote_segments_
   * @param [in] mem_info_list 对端内存信息列表
   */
  Status BuildRemoteSegmentsFromMemInfo(const std::vector<MemInfo> &mem_info_list);

  struct BatchHandle {
    CommType type;
    CompleteHandle handle;
  };

  std::map<CommType, HixlClientHandle> handles_;
  std::map<CommType, std::vector<MemHandle>> mem_handles_;
  std::vector<SegmentPtr> local_segments_;
  std::vector<SegmentPtr> remote_segments_;
  std::map<TransferReq, std::vector<BatchHandle>> complete_handles_;
  std::mutex handle_mutex_;
  std::mutex mem_handle_mutex_;
  std::mutex local_seg_mutex_;
  std::mutex remote_seg_mutex_;
  std::mutex complete_handles_mutex_;

  bool lazy_mode_ = false;
  bool connect_triggered_ = false;  // 区分 GetOrCreateClient 内部首次调用（延迟）与用户显式 Connect（实际建链）
  std::set<CommType> connected_types_;
  uint32_t connect_timeout_ms_ = 0;
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_ENGINE_UB_CLIENT_HANDLER_H_
