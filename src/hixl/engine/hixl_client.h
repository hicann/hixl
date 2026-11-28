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
#include <optional>
#include <string>
#include <vector>
#include "cs/hixl_cs.h"
#include "common/ctrl_msg_plugin.h"
#include "common/hixl_inner_types.h"
#include "common/ctrl_msg.h"
#include "engine/client_handler.h"
#include "engine/client_handler_factory.h"
#include "engine/hixl_options.h"
#include "engine/transfer_request_manager.h"

namespace hixl {

struct ClientConfig {
  std::vector<EndpointConfig> endpoint_list;
  std::string local_engine;
  std::string remote_engine;
  std::optional<uint8_t> rdma_tc;
  std::optional<uint8_t> rdma_sl;
  uint32_t timeout_ms;
  GlobalResourceConfig global_config;
  bool is_lazy = false;
};

class HixlClient {
 public:
  /**
   * @brief HixlClient  构造函数
   * @param [in] server_ip  服务端监听 IPv4 地址
   * @param [in] server_port  服务端监听端口号
   */
  HixlClient(const std::string &server_ip, uint32_t server_port, const ClientConfig &config)
      : server_ip_(server_ip),
        server_port_(server_port),
        local_engine_(config.local_engine),
        remote_engine_(config.remote_engine),
        rdma_tc_(config.rdma_tc),
        rdma_sl_(config.rdma_sl),
        global_config_(config.global_config) {}
  ~HixlClient() = default;

  /**
   * @brief 注册本端内存，在 TransferSync 和 TransferAsync 之前需要调用
   * @param [in] mem_info_list 本端注册内存信息列表
   * @return 操作结果状态码
   */
  Status RegisterMem(const std::vector<MemHandleInfo> &mem_info_list);

  /**
   * @brief 解注册本端内存
   * @param [in] mem_handle 注册内存返回的内存handle
   * @return 操作结果状态码
   */
  Status DeregisterMem(MemHandle mem_handle);

  /**
   * @brief client初始化
   * @param [in] local_endpoint_list 客户端本地 endpoint_list
   * @param [in] timeout_ms          超时时间（ms）
   * @param [in] is_lazy             是否懒惰建链模式
   * @return 操作结果状态码
   */
  Status Initialize(const std::vector<EndpointConfig> &local_endpoint_list, uint32_t timeout_ms, bool is_lazy = false);

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
   * @param [in] op_descs         批量操作的本地以及远端地址以及读取内存大小，
   * 批量操作的个数
   * @param [in] operation        读操作/写操作
   * @param [in] timeout_ms       超时时间
   * @return 操作结果状态码
   */
  Status TransferSync(const std::vector<TransferOpDesc> &op_descs, TransferOp operation, uint32_t timeout_ms);

  /**
   * @brief 异步传输
   * @param [in] op_descs         批量操作的本地以及远端地址以及写入内存大小，
   * 批量操作的个数
   * @param [in] operation        读操作/写操作
   * @param [out] req             请求的handle，用于查询请求状态
   * @return 操作结果状态码
   */
  Status TransferAsync(const std::vector<TransferOpDesc> &op_descs, TransferOp operation,
                       const TransferArgs &optional_args, TransferReq &req);

  /**
   * @brief 查询异步传输状态
   * @param [in] req             请求的handle，用于查询请求状态
   * @param [out] status         传输状态
   * @return 操作结果状态码
   */
  Status GetTransferStatus(const TransferReq &req, TransferStatus &status);

  Status SendNotify(const NotifyDesc &notify, int32_t timeout_ms) const;

  Status CheckAlive();

  /**
   * @brief 本次传输占用这条链路。只在链路池开启时由 ClientManager 调用。
   */
  void AcquireUse();

  /**
   * @brief 本次传输结束，释放 AcquireUse 加上的占用。
   */
  void ReleaseUse();

  /**
   * @brief 判断链路是否正在传输，用于链路池回收时跳过繁忙链路
   * @return 占用计数大于 0、有未完成的异步请求，或有其他线程正在操作该链路时返回 true
   */
  bool IsBusy() const;

  const std::string &GetRemoteEngine() const;

 private:
  Status SendEndpointInfoReq(const CtrlMsgPluginPtr &connection, CtrlMsgType msg_type, uint32_t timeout_ms) const;
  Status RecvEndpointInfoResp(const CtrlMsgPluginPtr &connection, std::vector<EndpointConfig> &remote_endpoint_list,
                              uint32_t timeout_ms) const;
  Status RecvNotifyAck(const CtrlMsgPluginPtr &connection, int32_t timeout_ms) const;
  void CloseCtrlSocket();
  Status CheckAliveLocked();
  bool IsCtrlSocketWritable() const;
  void CheckAliveAndLog(const char *operation);
  void ClearTransferReqs();
  void RemoveTransferReq(const TransferReq &req);
  void LogLinkPairs(const char *phase) const;

  std::string server_ip_;
  uint32_t server_port_;
  std::string local_engine_;
  std::string remote_engine_;
  std::optional<uint8_t> rdma_tc_;
  std::optional<uint8_t> rdma_sl_;
  bool is_connected_{false};  // true为已建链；false未建链
  bool is_finalized_{false};
  CtrlMsgPluginPtr connection_;
  std::unique_ptr<IClientHandler> client_handler_;
  std::vector<HandlerCreateArgs::EndpointPair> link_pairs_;
  mutable std::mutex mutex_;  // 所有方法串行执行，不支持并发调用
  // 链路池开启时，一次传输从取到 client 到调用返回之间的占用。未开启时保持 0，不参与判断。
  std::atomic<uint32_t> use_count_{0};
  TransferRequestManager request_manager_;
  GlobalResourceConfig global_config_;
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_ENGINE_HIXL_CLIENT_H_
