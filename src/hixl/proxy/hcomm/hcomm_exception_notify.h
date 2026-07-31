/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HCOMM_EXCEPTION_NOTIFY_H
#define HCOMM_EXCEPTION_NOTIFY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 异常扩展信息类型枚举
 */
typedef enum tagHcommExceptionExpandType {
  HCOMM_EXCEPTION_INVALID = 0,
  HCOMM_EXCEPTION_STARS,
  HCOMM_EXCEPTION_ROCE_HOST,
  HCOMM_EXCEPTION_ROCE_DEVICE,
  HCOMM_EXCEPTION_UB_HOST,
  HCOMM_EXCEPTION_UB_DEVICE,
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
 * @brief 异常信息结构体
 */
typedef struct tagHcommExceptionInfo {
  uint64_t thread;
  uint64_t channel;
  uint32_t taskId;
  uint32_t retCode;
  uint8_t rsvd[40];
  HcommExceptionExpandInfo expandInfo;
} HcommExceptionInfo;

/**
 * @brief 异常回调函数原型
 * @param[in] exceptionInfo 异常信息
 * @param[in] userData 用户自定义数据指针
 */
typedef void (*ExceptionCallback)(const HcommExceptionInfo *exceptionInfo, void *userData);

/**
 * @brief 注册异常回调
 * @param[in] cb 回调函数指针
 * @param[in] userData 用户自定义数据指针，回调时透传
 * @return 0表示成功，非0表示失败
 */
int32_t HcommRegisterExceptionCallback(ExceptionCallback cb, void *userData);

/**
 * @brief 注销异常回调
 * @param[in] cb 需要注销的回调函数指针
 * @return 0表示成功，非0表示失败
 */
int32_t HcommUnregisterExceptionCallback(ExceptionCallback cb);

#ifdef __cplusplus
}
#endif

#endif
