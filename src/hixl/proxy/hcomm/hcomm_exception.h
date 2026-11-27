/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HCOMM_EXCEPTION_H
#define HCOMM_EXCEPTION_H

#include <stdint.h>
#include "hcomm_res_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 异常扩展信息来源枚举
 */
typedef enum tagHcommExceptionExpandType {
  HCOMM_EXCEPTION_INVALID = -1,
  HCOMM_EXCEPTION_STARS,
  HCOMM_EXCEPTION_ROCE,
  HCOMM_EXCEPTION_URMA,
} HcommExceptionExpandType;

/**
 * @brief STARS异常详情信息
 *        当异常类型为HCOMM_EXCEPTION_STARS时使用
 */
typedef struct tagStarsExDetailInfo {
  uint32_t starsErrcode;
  uint8_t sqeType;
  uint8_t statusMerged;
  uint8_t rsvd[122];  // 结构体对齐128
} StarsExDetailInfo;

/**
 * @brief 异常扩展信息
 */
typedef struct tagHcommExceptionExpandInfo {
  HcommExceptionExpandType type;
  union {
    StarsExDetailInfo starsInfo;
  } detail;
} HcommExceptionExpandInfo;

/**
 * @brief Host/Device 共用异常信息结构体，与 HCOMM include/hcomm_exception.h 保持一致。
 */
typedef struct tagHcommExceptionInfo {
  CommAbiHeader header;  // Host ABI 信息；Device 本地回调不使用。
  uint64_t thread;       // Host 为线程实体句柄；Device 为设备侧线程对象地址。
  uint64_t channel;      // 预留，当前为 0。
  uint32_t taskId;
  uint32_t retCode;
  uint32_t deviceId;  // Host 为发送方逻辑设备 ID；Device 本地回调不使用。
  uint8_t rsvd[36];
  HcommExceptionExpandInfo expandInfo;  // Device 错误详情；Host 不使用。
} HcommExceptionInfo;

// Host 异常回调使用的 ABI 校验常量；HIXL Device 本地异常回调当前不校验 header。
static const uint32_t HCOMM_EXCEPTION_MAGIC_WORD = 0x0f0f0f4f;
static const uint32_t HCOMM_EXCEPTION_VERSION = 2;

/**
 * @brief 异常回调函数原型
 * @param[in] exceptionInfo 异常信息
 * @param[in] userData 用户自定义数据指针，回调时透传
 */
typedef void (*HcommExceptionCallback)(const HcommExceptionInfo *exceptionInfo, void *userData);

/**
 * @brief 注册异常回调（同一回调重复注册仅更新 userData）
 * @param[in] cb 回调函数指针
 * @param[in] userData 用户自定义数据指针，回调时透传
 * @return 0表示成功，非0表示失败；HCCL_E_NOT_SUPPORT 表示环境不支持（HAL esched 符号不可解析），
 *         调用方可据此走 device 侧注册 fallback
 * @note Host 侧 userData 生命周期契约：回调分发为快照后异步调用，Unregister 后仍可能有在途回调
 *       使用 userData；调用方须保证 userData 在回调不再被引用前保持存活（如延迟释放或
 *       引用计数），否则构成 use-after-free
 */
HcommResult HcommExceptionRegisterCallback(HcommExceptionCallback cb, void *userData);

/**
 * @brief 注销异常回调
 * @param[in] cb 需要注销的回调函数指针
 * @return 0表示成功，非0表示失败
 * @note Host 侧注销仅保证后续事件不再分发到该回调；已在途的回调仍会执行完毕，
 *       userData 的释放时机见注册接口的 @note
 */
HcommResult HcommExceptionUnregisterCallback(HcommExceptionCallback cb);

#ifdef __cplusplus
}
#endif

#endif
