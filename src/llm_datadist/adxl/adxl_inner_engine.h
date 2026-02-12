/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
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
#include "fabric_mem_transfer_service.h"

namespace adxl {
namespace {
constexpr size_t kDefaultStreamNum = 4U;
}
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

  void Disconnect();

  Status TransferSync(const AscendString &remote_engine,
                      TransferOp operation,
                      const std::vector<TransferOpDesc> &op_descs,
                      int32_t timeout_in_millis);

  Status TransferAsync(const AscendString &remote_engine,
                       TransferOp operation,
                       const std::vector<TransferOpDesc> &op_descs,
                       const TransferArgs &optional_args,
                       TransferReq &req);

  Status GetTransferStatus(const TransferReq &req, TransferStatus &status);

  Status SendNotify(const AscendString &remote_engine, const NotifyDesc &notify, int32_t timeout_in_millis = 1000);

  Status GetNotifies(std::vector<NotifyDesc> &notifies);

  Status RegisterCallbackProcessor(int32_t msg_type, CallbackProcessor processor);
  
 private:
  Status GetTransferType(const ChannelPtr &channel, TransferOp operation, const std::vector<TransferOpDesc> &op_descs,
                         bool &need_buffer, TransferType &type);
  Status InitBufferTransferService(const std::map<ge::AscendString, ge::AscendString> &options);
  static void ParseBufferPool(const std::map<AscendString, AscendString> &options,
                              std::string &pool_config);
  Status ParseWaterlineRatio(const std::map<AscendString, AscendString>& json_options, 
                             const char* option_name, double& parsed_value);
  Status LoadGlobalResourceConfig(const std::map<AscendString, AscendString> &options);
  Status ParseChannelPoolConfig(const std::map<AscendString, AscendString> &json_options);
  Status ParseFabricMemoryCapacity(const std::map<AscendString, AscendString> &json_options);
  Status ConnectWhenTransfer(const AscendString &remote_engine, int32_t timeout_in_millis = 3000);
  Status ParseBufferPoolParams(const std::map<AscendString, AscendString> &options, uint64_t &buffer_size,
                               uint64_t &npu_pool_size);
  Status ParseEnableFabricMem(const std::map<AscendString, AscendString> &options);
  Status ParseAutoConnectConfig(const std::map<AscendString, AscendString> &options);
  Status DisconnectOnError(const std::string &remote_engine, int32_t timeout_in_millis);
  Status ParseTaskStreamNum(const std::map<AscendString, AscendString> &json_options);

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
  std::unique_ptr<StreamPool> stream_pool_ = nullptr;
  bool user_config_buffer_pool_{false};
  bool user_config_channel_pool_{false};
  bool auto_connect_{false};
  aclrtContext aclrt_context_{nullptr};

  std::mutex notify_mutex_;
  std::unordered_map<uint64_t, bool> notify_ack_ready_;     // Map to indicate if ack status is ready
  std::condition_variable notify_cv_;                       // Condition variable for waiting ack
  std::atomic<uint64_t> next_notify_id_{1};
  std::mutex req2channel_mutex_;
  std::map<uint64_t, AscendString> req2channel_;
  std::atomic<uint64_t> next_req_id_{1};
  // Mutex to protect connection operations (Connect and ConnectWhenTransfer)
  std::mutex connection_mutex_;
  void *statistic_timer_handle_{nullptr};

  bool enable_use_fabric_mem_ = false;
  std::unique_ptr<FabricMemTransferService> fabric_mem_transfer_service_ = nullptr;
  size_t task_stream_num_ = kDefaultStreamNum;
};
}  // namespace adxl

#endif  // CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_ADXL_INNER_ENGINE_H_
