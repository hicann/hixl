/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_ADXL_INNER_ENGINE_H_
#define CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_ADXL_INNER_ENGINE_H_

#include <mutex>
#include <map>
#include "channel_msg_handler.h"
#include "channel_manager.h"
#include "common/llm_mem_pool.h"
#include "buffer_transfer_service.h"
#include "segment_table.h"

namespace adxl {
class AdxlInnerEngine {
 public:
  explicit AdxlInnerEngine(const AscendString &local_engine)
      : local_engine_(local_engine.GetString()),
        msg_handler_(local_engine_, &channel_manager_),
        is_initialized_{false} {};

  ~AdxlInnerEngine() = default;

  Status Initialize(const std::map<AscendString, AscendString> &options);

  void Finalize();

  bool IsInitialized() const;

  Status RegisterMem(const MemDesc &mem, MemType type, MemHandle &mem_handle);

  Status DeregisterMem(MemHandle mem_handle);

  Status Connect(const AscendString &remote_engine, int32_t timeout_in_millis);

  Status Disconnect(const AscendString &remote_engine, int32_t timeout_in_millis);

  Status TransferSync(const AscendString &remote_engine,
                      TransferOp operation,
                      const std::vector<TransferOpDesc> &op_descs,
                      int32_t timeout_in_millis);

 private:
  Status GetTransferType(const ChannelPtr &channel, TransferOp operation, const std::vector<TransferOpDesc> &op_descs,
                         bool &need_buffer, TransferType &type);
  Status InitBufferTransferService(const std::map<ge::AscendString, ge::AscendString> &options);
  static void ParseBufferPool(const std::map<AscendString, AscendString> &options,
                              std::string &pool_config);

  std::string local_engine_;
  ChannelManager channel_manager_;
  ChannelMsgHandler msg_handler_;
  std::mutex mutex_;
  std::atomic<bool> is_initialized_;
  std::vector<std::unique_ptr<llm::LlmMemPool>> npu_mem_pools_;
  std::vector<void *> npu_pool_memorys_{};
  std::vector<MemHandle> pool_mem_handles_{};
  std::unique_ptr<BufferTransferService> buffer_transfer_service_ = nullptr;
  std::unique_ptr<SegmentTable> segment_table_ = nullptr;
  bool user_config_buffer_pool_{false};
};
}  // namespace adxl

#endif  // CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_ADXL_INNER_ENGINE_H_
