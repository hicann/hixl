/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "load_kernel.h"

#include <cerrno>
#include <cstdint>
#include <cstring>

#include <limits.h>
#include <unistd.h>
#include "mmpa/mmpa_api.h"
#include "common/hixl_log.h"
#include "common/scope_guard.h"

#include <common/hixl_checker.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

namespace hixl {
namespace {
constexpr uint32_t kCpuKernelMode = 0U;

Status GetKernelFilePath(std::string &json_path) {
  // 获取二进制文件路径
  std::string libPath;
  const char *getPath = std::getenv("ASCEND_HOME_PATH");
  MM_SYS_GET_ENV(MM_ENV_ASCEND_HOME_PATH, getPath);
  if (getPath != nullptr) {
    libPath = getPath;
  } else {
    libPath = "/usr/local/Ascend/cann";
    HIXL_LOGW("[GetKernelFilePath]ENV:ASCEND_HOME_PATH is not set");
  }

  libPath += "/opp/built-in/op_impl/aicpu/config/libscatter_hixl_kernel.json";
  json_path = libPath;
  HIXL_LOGD("[GetKernelFilePath]kernel folder path[%s]", json_path.c_str());

  return SUCCESS;
}

Status LoadBinaryFromJson(const char *json_path, aclrtBinHandle &bin_handle) {
  if (json_path == nullptr) {
    return PARAM_INVALID;
  }
  char resolved_path[MMPA_MAX_PATH] = {0};
  auto mm_ret = mmRealPath(json_path, resolved_path, MMPA_MAX_PATH);
  if (mm_ret != EN_OK) {
    HIXL_LOGE(PARAM_INVALID, "[LoadKernel] mmRealPath failed. path=%s, ret=%d", json_path, mm_ret);
    return PARAM_INVALID;
  }
  if (mmAccess(resolved_path) != EN_OK) {
    HIXL_LOGE(FAILED, "[LoadKernel] Can not access file: %s", resolved_path);
    return FAILED;
  }
  aclrtBinaryLoadOptions load_options{};
  aclrtBinaryLoadOption option{};
  option.type = ACL_RT_BINARY_LOAD_OPT_CPU_KERNEL_MODE;
  option.value.cpuKernelMode = kCpuKernelMode;
  load_options.numOpt = 1U;
  load_options.options = &option;
  aclError aerr = aclrtBinaryLoadFromFile(resolved_path, &load_options, &bin_handle);
  if (aerr != ACL_SUCCESS) {
    HIXL_LOGE(FAILED, "[LoadKernel] aclrtBinaryLoadFromFile failed. path=%s ret=%d",
              resolved_path, static_cast<int32_t>(aerr));
    return FAILED;
  }
  HIXL_LOGI("[LoadKernel] aclrtBinaryLoadFromFile success. path=%s handle=%p", resolved_path, bin_handle);
  return SUCCESS;
}

Status GetFuncHandle(aclrtBinHandle bin_handle, const char *func_name, aclrtFuncHandle &func_handle) {


  if (bin_handle == nullptr) {
    return PARAM_INVALID;
  }
  if (func_name == nullptr) {
    return PARAM_INVALID;
  }


  aclError aerr = aclrtBinaryGetFunction(bin_handle, func_name, &func_handle);
  if (aerr != ACL_SUCCESS) {
    HIXL_LOGE(FAILED, "[LoadKernel] aclrtBinaryGetFunction failed. func=%s ret=%d",
              func_name, static_cast<int32_t>(aerr));
    return FAILED;
  }


  HIXL_LOGI("[LoadKernel] resolve stub success. func=%s stub=%p", func_name, func_handle);
  return SUCCESS;
}

}  // namespace

Status LoadUbKernelAndGetHandles(const char *func_get, const char *func_put,
                                 aclrtBinHandle &bin_handle, UbFuncHandles &func_handles) {
  func_handles.batchGet = nullptr;
  func_handles.batchPut = nullptr;

  int32_t old_device = -1;
  bool need_restore = false;
  std::string json_path;
  HIXL_CHK_STATUS_RET(GetKernelFilePath(json_path), "[LoadKernel] GetKernelFilePath failed");
  HIXL_DISMISSABLE_GUARD(dev_restore, [&]() {
    if (need_restore) {
      (void)aclrtSetDevice(old_device);
    }
  });
  if (bin_handle == nullptr) {
    HIXL_CHK_STATUS_RET(LoadBinaryFromJson(json_path.c_str(), bin_handle),
                        "[LoadKernel] LoadBinaryFromJson failed. path=%s", json_path.c_str());
  }

  HIXL_CHK_STATUS_RET(GetFuncHandle(bin_handle, func_get, func_handles.batchGet),
                      "[LoadKernel] GetFuncHandle failed for get_func. func=%s", func_get);
  HIXL_CHK_STATUS_RET(GetFuncHandle(bin_handle, func_put, func_handles.batchPut),
                      "[LoadKernel] GetFuncHandle failed for put_func. func=%s", func_put);

  return SUCCESS;
}

}  // namespace hixl
