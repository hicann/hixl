/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_INC_EXTERNAL_HIXL_HIXL_CS_H_
#define CANN_HIXL_INC_EXTERNAL_HIXL_HIXL_CS_H_

#include <cstdint>
#include <netinet/in.h>
#include <string>
#include "hcomm/hcomm_res_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void *HixlServerHandle;
typedef void *HixlClientHandle;
typedef void *CompleteHandle;
typedef uint32_t HixlStatus;
typedef void *MemHandle;
#define HIXL_SUCCESS 0U
#define HIXL_PARAM_INVALID 103900U
#define HIXL_TIMEOUT 103901U
#define HIXL_FAILED 503900U


struct HixlServerConfig {
  uint8_t reserved[128] = {};
};

struct HixlClientConfig {
  uint8_t reserved[128] = {};
};

struct HixlClientDesc {
  const char *server_ip;
  uint32_t server_port;
  const EndpointDesc *src_endpoint;
  const EndpointDesc *dst_endpoint;
};

struct HixlServerDesc {
  const char *server_ip;
  uint32_t server_port;
  const EndpointDesc *endpoint_list;
  uint32_t endpoint_list_num;
};

struct HixlOneSideOpDesc {
  void *remote_buf;
  void *local_buf;
  uint64_t len;
};

enum HixlCompleteStatus {
  HIXL_COMPLETE_STATUS_WAITING,
  HIXL_COMPLETE_STATUS_COMPLETED
};

/**
 * @brief 创建Server
 * @param [in] server_desc server的描述信息
 * @param [in] config server的配置信息
 * @param [out] server_handle server创建返回的handle信息，用于后续调用其他接口
 * @return 成功:SUCCESS, 失败:其它.
 */
HixlStatus HixlCSServerCreate(const HixlServerDesc *server_desc,
                              const HixlServerConfig *config, HixlServerHandle *server_handle);

/**
 * @brief Server注册内存
 * @param [in] server_handle 创建server返回的handle
 * @param [in] mem_tag 用于表标识注册内存的描述信息
 * @param [in] mem server的注册的内存信息
 * @param [out] mem_handle server注册内存返回的handle信息
 * @return 成功:SUCCESS, 失败:其它.
 */
HixlStatus HixlCSServerRegMem(HixlServerHandle server_handle, const char *mem_tag, const HcommMem *mem,
                              MemHandle *mem_handle);

/**
 * @brief Server启动监听
 * @param [in] server_handle 创建server返回的handle
 * @param [in] backlog server连接队列的最大长度
 * @return 成功:SUCCESS, 失败:其它.
 */
HixlStatus HixlCSServerListen(HixlServerHandle server_handle, uint32_t backlog);

/**
 * @brief Server解注册内存
 * @param [in] server_handle 创建server返回的handle
 * @param [in] mem_handle server注册内存返回的handle信息
 * @return 成功:SUCCESS, 失败:其它.
 */
HixlStatus HixlCSServerUnregMem(HixlServerHandle server_handle, MemHandle mem_handle);

/**
 * @brief Server销毁
 * @param [in] server_handle 创建server返回的handle
 * @return 成功:SUCCESS, 失败:其它.
 */
HixlStatus HixlCSServerDestroy(HixlServerHandle server_handle);

/**
 * @brief 创建 Client 实例
 * @param [in] client_desc client的描述信息
 * @param [in] config client的配置信息
 * @param [out] client_handle 输出的客户端句柄
 * @return 成功:SUCCESS, 失败:其它.
 */
HixlStatus HixlCSClientCreate(const HixlClientDesc *client_desc, const HixlClientConfig *config,
                              HixlClientHandle *client_handle);

/**
 * @brief 发起 Client 连接（同步建链入口）
 * @param [in] client_handle 客户端句柄
 * @param [in] timeout_ms   连接超时时间（ms）
 * @return 成功:SUCCESS, 失败:其它.
 */
HixlStatus HixlCSClientConnect(HixlClientHandle client_handle, uint32_t timeout_ms);

/**
 * @brief 获取服务端共享内存信息
 * @param [in] client_handle 客户端句柄
 * @param [out] remote_mem_list 输出的服务端共享内存列表
 * @param [out] mem_tag_list 输出的服务端共享内存标识列表
 * @param [out] list_num 输出的共享内存实际数量
 * @param [in] timeout_ms 请求的超时时间（ms）
 * @return 成功:SUCCESS, 失败:其它.
 */
HixlStatus HixlCSClientGetRemoteMem(HixlClientHandle client_handle, HcommMem **remote_mem_list, char ***mem_tag_list,
                                    uint32_t *list_num, uint32_t timeout_ms);

/**
 * @brief 注册client给endpoint分配的内存
 * @param [in] client_handle 客户端句柄
 * @param [in] mem_tag 用于表标识注册内存的描述信息
 * @param [in] mem client的注册的内存信息
 * @param [out] mem_handle client注册内存返回的handle信息
 * @return 成功:SUCCESS, 失败:其它.
 */
HixlStatus HixlCSClientRegMem(HixlClientHandle client_handle, const char *mem_tag, const HcommMem *mem, MemHandle *mem_handle);

/**
 * @brief 注销client给endpoint分配的内存
 * @param [in] client_handle 客户端句柄
 * @param [in] mem_handle client注册内存返回的handle信息
 * @return 成功:SUCCESS, 失败:其它.
 */
HixlStatus HixlCSClientUnregMem(HixlClientHandle client_handle, MemHandle mem_handle);

/**
 * @brief 批量向server侧写入内存内容
 * @param [in] client_handle 客户端句柄
 * @param [in] list_num 本次传输任务的数目
 * @param [in] desc_list 记录了本次传输任务中每组传输任务的的server侧内存地址、client侧内存地址、内存偏移量大小
 * @param [out] complete_handle 本次传输任务生成的句柄
 * @return 成功:SUCCESS, 失败:其它.
 */
HixlStatus HixlCSClientBatchPutAsync(HixlClientHandle client_handle, uint32_t list_num,
                                     const HixlOneSideOpDesc *desc_list, CompleteHandle *complete_handle);

/**
 * @brief 批量读取server侧的内存内容
 * @param [in] client_handle 客户端句柄
 * @param [in] list_num 本次传输任务的数目
 * @param [in] desc_list 记录了本次传输任务中每组传输任务的的server侧内存地址、client侧内存地址、内存偏移量大小
 * @param [out] complete_handle 本次传输任务生成的句柄
 * @return 成功:SUCCESS, 失败:其它.
 */
HixlStatus HixlCSClientBatchGetAsync(HixlClientHandle client_handle, uint32_t list_num,
                                     const HixlOneSideOpDesc *desc_list, CompleteHandle *complete_handle);

/**
 * @brief 检查创建的批量读写任务的状态
 * @param [in] client_handle 客户端句柄
 * @param [in] complete_handle 先前传输任务生成的句柄
 * @param [out] complete_status 记录了本次查询任务的完成情况
 * @return 成功:SUCCESS, 失败:其它.
 */
HixlStatus HixlCSClientQueryCompleteStatus(HixlClientHandle client_handle, CompleteHandle complete_handle,
                                           HixlCompleteStatus *complete_status);

/**
 * @brief 销毁 Client 实例
 * @param [in] client_handle 客户端句柄
 * @return 成功:SUCCESS, 失败:其它.
 */
HixlStatus HixlCSClientDestroy(HixlClientHandle client_handle);
#ifdef __cplusplus
}
#endif

#endif  // CANN_HIXL_INC_EXTERNAL_HIXL_HIXL_CS_H_
