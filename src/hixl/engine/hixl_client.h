/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_ENGINE_HIXL_CLIENT_H_
#define CANN_HIXL_SRC_HIXL_ENGINE_HIXL_CLIENT_H_

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>
#include <map>
#include "cs/hixl_cs.h"
#include "common/hixl_inner_types.h"
#include "common/ctrl_msg.h"
#include "engine/client_handler.h"

namespace hixl {

struct ClientConfig {
  std::vector<EndpointConfig> endpoint_list;
  std::string remote_engine;
  uint8_t rdma_tc;
  uint8_t rdma_sl;
  uint32_t timeout_ms;
};

class HixlClient {
 public:
  /**
   * @brief HixlClient  构造函数
   * @param [in] server_ip  服务端监听 IPv4 地址
   * @param [in] server_port  服务端监听端口号
   */
  HixlClient(const std::string &server_ip, uint32_t server_port, const ClientConfig &config)
      : server_ip_(server_ip), server_port_(server_port), rdma_tc_(config.rdma_tc), rdma_sl_(config.rdma_sl) {}
  ~HixlClient() = default;

  /**
   * @brief 设置本端内存信息，在 BatchPut 和 BatchGet 之前需要调用
   * @param [in] mem_info_list 本端注册内存信息
   * @return 操作结果状态码
   */
  Status SetLocalMemInfo(const std::vector<MemInfo> &mem_info_list);

  /**
   * @brief client初始化
   * @param [in] local_endpoint_list 客户端本地 endpoint_list
   * @param [in] timeout_ms          超时时间（ms）
   * @return 操作结果状态码
   */
  Status Initialize(const std::vector<EndpointConfig> &local_endpoint_list, uint32_t timeout_ms);

  /**
   * @brief 建链
   * @param [in] timeout_ms       超时时间（ms）
   * @return 操作结果状态码
   */
  Status Connect(uint32_t timeout_ms);

  /**
   * @brief 断链&销毁
   * @return 操作结果状态码
   */
  Status Finalize();

  /**
   * @brief 同步传输
   * @param [in] op_descs         批量操作的本地以及远端地址以及读取内存大小，批量操作的个数
   * @param [in] operation        读操作/写操作
   * @param [in] timeout_ms       超时时间
   * @return 操作结果状态码
   */
  Status TransferSync(const std::vector<TransferOpDesc> &op_descs, TransferOp operation, uint32_t timeout_ms);

  /**
   * @brief 异步传输
   * @param [in] op_descs         批量操作的本地以及远端地址以及写入内存大小，批量操作的个数
   * @param [in] operation        读操作/写操作
   * @param [out] req             请求的handle，用于查询请求状态
   * @return 操作结果状态码
   */
  Status TransferAsync(const std::vector<TransferOpDesc> &op_descs, TransferOp operation, TransferReq &req);

  /**
   * @brief 查询异步传输状态
   * @param [in] req             请求的handle，用于查询请求状态
   * @param [out] status         传输状态
   * @return 操作结果状态码
   */
  Status GetTransferStatus(const TransferReq &req, TransferStatus &status);

  Status SendNotify(const NotifyDesc &notify, int32_t timeout_ms);

  Status CheckAlive();

 private:
  Status SendEndpointInfoReq(int32_t fd, CtrlMsgType msg_type) const;
  Status RecvEndpointInfoResp(int32_t fd, std::vector<EndpointConfig> &remote_endpoint_list, uint32_t timeout_ms) const;
  void WaitBatchCsSyncInflightDrain();
  Status RecvNotifyAck(int32_t fd, int32_t timeout_ms);
  void CloseCtrlSocketLocked();

  std::string server_ip_;
  uint32_t server_port_;
  uint8_t rdma_tc_{kRdmaTrafficClass};
  uint8_t rdma_sl_{kRdmaServiceLevel};
  bool is_connected_{false};  // true为已建链；false未建链
  bool is_finalized_{false};
  bool finalize_pending_{
      false};  // Finalize 置位后拒绝新 TransferSync；在析构 CS client 前等待为 0（与 TransferSync 内 fetch_add 配对）
  std::atomic<int> batch_cs_sync_inflight_{0};
  int32_t ctrl_socket_{-1};
  std::unique_ptr<IClientHandler> client_handler_;
  std::mutex ctrl_socket_mutex_;
  std::mutex status_mutex_;  // 保护 is_connected_、is_finalized_、finalize_pending_；TransferSync 与 Finalize 在此与
                             // inflight 配对
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_ENGINE_HIXL_CLIENT_H_
