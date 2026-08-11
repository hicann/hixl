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

#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <limits.h>
#include <unistd.h>
#include <vector>
#include "common/hixl_log.h"
#include "common/scope_guard.h"
#include "common/hixl_checker.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

namespace hixl {
namespace {
constexpr uint32_t kCpuKernelMode = 0U;
constexpr const char *kKernelJsonSuffix = "/opp/built-in/op_impl/aicpu/config/libcann_hixl_kernel.json";
constexpr const char *kDefaultAscendPath = "/usr/local/Ascend/cann";
Status GetKernelFilePath(std::string &json_path) {
  std::string libPath;
  const char *getPath = std::getenv("ASCEND_HOME_PATH");
  if (getPath != nullptr) {
    libPath = getPath;
  } else {
    libPath = kDefaultAscendPath;
    HIXL_LOGW("[GetKernelFilePath] ENV:ASCEND_HOME_PATH is not set, using default: %s", kDefaultAscendPath);
  }
  libPath += kKernelJsonSuffix;
  json_path = libPath;
  HIXL_LOGD("[GetKernelFilePath] kernel folder path[%s]", json_path.c_str());
  return SUCCESS;
}

Status LoadBinaryFromJson(const char *json_path, aclrtBinHandle &bin_handle) {
  if (json_path == nullptr) {
    return PARAM_INVALID;
  }
  char resolved_path[PATH_MAX] = {0};
  if (realpath(json_path, resolved_path) == nullptr) {
    HIXL_LOGE(PARAM_INVALID, "[LoadKernel] realpath failed. path=%s, errno=%d, errmsg=%s", json_path, errno,
              strerror(errno));
    return PARAM_INVALID;
  }
  if (access(resolved_path, F_OK) != 0) {
    HIXL_LOGE(FAILED, "[LoadKernel] Cannot access file: %s, errno=%d, errmsg=%s", resolved_path, errno,
              strerror(errno));
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
    HIXL_LOGE(FAILED, "[LoadKernel] aclrtBinaryLoadFromFile failed. path=%s ret=%d", resolved_path,
              static_cast<int32_t>(aerr));
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
    HIXL_LOGE(FAILED, "[LoadKernel] aclrtBinaryGetFunction failed. func=%s ret=%d", func_name,
              static_cast<int32_t>(aerr));
    return FAILED;
  }
  HIXL_LOGI("[LoadKernel] resolve stub success. func=%s stub=%p", func_name, func_handle);
  return SUCCESS;
}

}  // namespace

Status LoadDeviceKernelFunctions(const std::vector<const char *> &func_names, aclrtBinHandle &bin_handle,
                                 std::vector<aclrtFuncHandle> &func_handles) {
  func_handles.clear();
  HIXL_CHK_BOOL_RET_STATUS(!func_names.empty(), PARAM_INVALID, "[LoadKernel] No functions requested.");
  std::string json_path;
  HIXL_CHK_STATUS_RET(GetKernelFilePath(json_path), "[LoadKernel] GetKernelFilePath failed");
  if (bin_handle == nullptr) {
    HIXL_CHK_STATUS_RET(LoadBinaryFromJson(json_path.c_str(), bin_handle),
                        "[LoadKernel] LoadBinaryFromJson failed. path=%s", json_path.c_str());
  }
  func_handles.reserve(func_names.size());
  for (const char *func_name : func_names) {
    aclrtFuncHandle func_handle = nullptr;
    HIXL_CHK_STATUS_RET(GetFuncHandle(bin_handle, func_name, func_handle), "[LoadKernel] GetFuncHandle failed. func=%s",
                        func_name == nullptr ? "" : func_name);
    func_handles.emplace_back(func_handle);
  }
  return SUCCESS;
}

Status LoadDeviceKernelAndGetHandles(const char *func_get, const char *func_put, aclrtBinHandle &bin_handle,
                                     DeviceFuncHandles &func_handles, const char *func_sync_context) {
  func_handles.batch_get = nullptr;
  func_handles.batch_put = nullptr;
  func_handles.sync_transfer_context = nullptr;
  std::string json_path;
  HIXL_CHK_STATUS_RET(GetKernelFilePath(json_path), "[LoadKernel] GetKernelFilePath failed");
  if (bin_handle == nullptr) {
    HIXL_CHK_STATUS_RET(LoadBinaryFromJson(json_path.c_str(), bin_handle),
                        "[LoadKernel] LoadBinaryFromJson failed. path=%s", json_path.c_str());
  }
  HIXL_CHK_STATUS_RET(GetFuncHandle(bin_handle, func_get, func_handles.batch_get),
                      "[LoadKernel] GetFuncHandle failed for get_func. func=%s", func_get);
  HIXL_CHK_STATUS_RET(GetFuncHandle(bin_handle, func_put, func_handles.batch_put),
                      "[LoadKernel] GetFuncHandle failed for put_func. func=%s", func_put);
  if (func_sync_context != nullptr) {
    HIXL_CHK_STATUS_RET(GetFuncHandle(bin_handle, func_sync_context, func_handles.sync_transfer_context),
                        "[LoadKernel] GetFuncHandle failed for sync_context_func. func=%s", func_sync_context);
  }
  return SUCCESS;
}

}  // namespace hixl
