/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "adxl_utils.h"
#include "llm_datadist/llm_datadist.h"

namespace adxl {
Status HcclError2AdxlStatus(HcclResult ret) {
  static const std::map<HcclResult, Status> hccl2adxl = {
      {HCCL_SUCCESS, SUCCESS},
      {HCCL_E_PARA, PARAM_INVALID},
      {HCCL_E_TIMEOUT, TIMEOUT},
      {HCCL_E_NOT_SUPPORT, UNSUPPORTED},
  };
  const auto &it = hccl2adxl.find(ret);
  if (it != hccl2adxl.cend()) {
    return it->second;
  }
  return FAILED;
}

Status AclError2AdxlStatus(rtError_t ret) {
  static const std::map<rtError_t, Status> acl2adxl = {
      {RT_ERROR_NONE, SUCCESS},
      {ACL_ERROR_RT_STREAM_SYNC_TIMEOUT, TIMEOUT},
  };
  const auto &it = acl2adxl.find(ret);
  if (it != acl2adxl.cend()) {
    return it->second;
  }
  return static_cast<Status>(ret);
}

Status LLMError2AdxlStatus(ge::Status ret) {
  static const std::map<ge::Status, Status> llm2adxl = {
      {llm_datadist::LLM_SUCCESS, SUCCESS},
      {llm_datadist::LLM_PARAM_INVALID, PARAM_INVALID},
      {llm_datadist::LLM_TIMEOUT, TIMEOUT},
      {llm_datadist::LLM_NOT_YET_LINK, NOT_CONNECTED},
      {llm_datadist::LLM_ALREADY_LINK, ALREADY_CONNECTED},
  };
  const auto &it = llm2adxl.find(ret);
  if (it != llm2adxl.cend()) {
    return it->second;
  }
  return FAILED;
}
}  // namespace adxl
