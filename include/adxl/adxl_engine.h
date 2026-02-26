/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_INCLUDE_ADXL_ADXL_ENGINE_H_
#define CANN_HIXL_INCLUDE_ADXL_ADXL_ENGINE_H_

#include <memory>
#include <map>
#include <vector>
#include "adxl_types.h"

#ifdef FUNC_VISIBILITY
#define ASCEND_FUNC_VISIBILITY __attribute__((visibility("default")))
#else
#define ASCEND_FUNC_VISIBILITY
#endif

namespace adxl {
class ASCEND_FUNC_VISIBILITY AdxlEngine {
 public:
  /**
   * @brief 构造函数
   */
  AdxlEngine();

  /**
   * @brief 析构函数
   */
  ~AdxlEngine();

  /**
   * @brief 初始化AdxlEngine, 在调用其他接口前需要先调用该接口
   * @param [in] local_engine AdxlEngine的唯一标识，如果是ipv4格式为host_ip:host_port或host_ip,
   * 如果是ipv6格式为[host_ip]:host_port或[host_ip],
   * 当设置host_port且host_port > 0时代表当前AdxlEngine作为server端，需要对配置端口进行监听
   * @param [in] options 初始化所需的选项
   * @return 成功:SUCCESS, 失败:其它.
   */
  Status Initialize(const AscendString &local_engine, const std::map<AscendString, AscendString> &options);

  /**
   * @brief AdxlEngine资源清理函数
   */
  void Finalize();

 /**
   * @brief 注册内存
   * @param [in] mem 需要注册的内存的描述信息
   * @param [in] type 需要注册的内存的类型
   * @param [out] mem_handle 注册成功返回的内存handle, 可用于内存解注册
   * @return 成功:SUCCESS, 失败:其它.
   */
  Status RegisterMem(const MemDesc &mem, MemType type, MemHandle &mem_handle);

  /**
   * @brief 解注册内存
   * @param [in] mem_handle 注册内存返回的内存handle
   * @return 成功:SUCCESS, 失败:其它.
   */
  Status DeregisterMem(MemHandle mem_handle);

  /**
   * @brief 与远端AdxlEngine进行建链
   * @param [in] remote_engine 远端AdxlEngine的唯一标识
   * @param [in] timeout_in_millis 建链的超时时间，单位ms
   * @return 成功:SUCCESS, 失败:其它.
   */
  Status Connect(const AscendString &remote_engine, int32_t timeout_in_millis = 1000);

  /**
   * @brief 与远端AdxlEngine进行断链
   * @param [in] remote_engine 远端AdxlEngine的唯一标识
   * @param [in] timeout_in_millis 断链的超时时间，单位ms
   * @return 成功:SUCCESS, 失败:其它.
   */
  Status Disconnect(const AscendString &remote_engine, int32_t timeout_in_millis = 1000);

  /**
   * @brief 与远端AdxlEngine进行内存传输
   * @param [in] remote_engine 远端AdxlEngine的唯一标识
   * @param [in] operation 将远端内存读到本地或者将本地内存写到远端
   * @param [in] op_descs 批量操作的本地以及远端地址
   * @param [in] timeout_in_millis 断链的超时时间，单位ms
   * @return 成功:SUCCESS, 失败:其它.
   */
  Status TransferSync(const AscendString &remote_engine,
                      TransferOp operation,
                      const std::vector<TransferOpDesc> &op_descs,
                      int32_t timeout_in_millis = 1000);
  
  /**
   * @brief 批量异步传输，下发传输请求
   * @param [in] remote_engine 远端Hixl的唯一标识
   * @param [in] operation 将远端内存读到本地或者将本地内存写到远端
   * @param [in] op_descs 批量操作的本地以及远端地址
   * @param [in] optional_args 可选参数，预留
   * @param [out] req 请求的handle，用于查询请求状态
   * @return 成功:SUCCESS, 失败:其它.
   */
  Status TransferAsync(const AscendString &remote_engine,
                       TransferOp operation,
                       const std::vector<TransferOpDesc> &op_descs,
                       const TransferArgs &optional_args,
                       TransferReq &req);
  
  /**
   * @brief 获取请求状态
   * @param [in] req 请求handle，由TransferAsync API调用产生
   * @param [out] status 传输状态
   * @return 成功:SUCCESS, 失败:其它.
   */
  Status GetTransferStatus(const TransferReq &req, TransferStatus &status);

  /**
   * @brief Client向Server发送Notify信息
   * @param [in] remote_engine 远端AdxlEngine的唯一标识
   * @param [in] notify 要发送的Notify内容
   * @param [in] timeout_in_millis 发送超时时间，单位ms
   * @return 成功:SUCCESS, 失败:其它.
   */
  Status SendNotify(const AscendString &remote_engine, const NotifyDesc &notify, int32_t timeout_in_millis = 1000);

  /**
   * @brief 获取当前AdxlEngine内所有Server收到的Notify信息，并清空已收到信息
   * @param [in] notifies 存放notify信息的vector
   * @return 成功:SUCCESS, 失败:其它.
   */
  Status GetNotifies(std::vector<NotifyDesc> &notifies);

 private:
  class AdxlEngineImpl;
  std::unique_ptr<AdxlEngineImpl> impl_;
};
}  // namespace adxl

#endif  // CANN_HIXL_INCLUDE_ADXL_ADXL_ENGINE_H_
