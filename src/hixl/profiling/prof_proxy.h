/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_PROFILING_PROF_PROXY_H_
#define CANN_HIXL_SRC_HIXL_PROFILING_PROF_PROXY_H_

#include <atomic>
#include <cstdint>

#include "prof_types.h"

extern "C" {
__attribute__((weak)) void *aclprofCreateStamp();
__attribute__((weak)) int32_t aclsysGetVersionNum(char *pkg_name, int32_t *version_num);
}

namespace hixl {
using HixlAclProfCreateStampFunc = void *(*)();
using HixlAclProfGetVersionNumFunc = int32_t (*)(char *, int32_t *);

uint64_t AclStartRange(const HixlProfType prof_type);
void AclStopRange(const uint64_t range_id);
void AclDestroyRange(const uint64_t range_id);
uint64_t MsprofStartRange(const HixlProfType prof_type);
void MsprofStopRange(const uint64_t range_id);
void MsprofDestroyRange(const uint64_t range_id);

struct ProfFuncs {
  uint64_t (*start)(HixlProfType);
  void (*stop)(uint64_t);
  void (*destroy)(uint64_t);
};

inline bool HixlIsAclProfRuntimeSupported(HixlAclProfGetVersionNumFunc get_version_num) {
  if (get_version_num == nullptr) {
    return false;
  }
  char pkg_name[] = "runtime";
  int32_t version_num = 0;
  return (get_version_num(pkg_name, &version_num) == 0) && (version_num > kAclProfRuntimeVersionThreshold);
}

inline ProfFuncs HixlResolveProfFuncs(HixlAclProfCreateStampFunc create_stamp,
                                      HixlAclProfGetVersionNumFunc get_version_num) {
  if ((create_stamp != nullptr) && HixlIsAclProfRuntimeSupported(get_version_num)) {
    return {AclStartRange, AclStopRange, AclDestroyRange};
  }
  return {MsprofStartRange, MsprofStopRange, MsprofDestroyRange};
}

inline const ProfFuncs &HixlGetProfFuncs() {
  static const ProfFuncs funcs = HixlResolveProfFuncs(aclprofCreateStamp, aclsysGetVersionNum);
  return funcs;
}

class ProfStart {
 public:
  explicit ProfStart(const HixlProfType prof_type) : rid_(HixlGetProfFuncs().start(prof_type)) {}
  ~ProfStart() noexcept {
    const uint64_t rid = rid_.exchange(kInvalidProfRange);
    if (rid != kInvalidProfRange) {
      HixlGetProfFuncs().destroy(rid);
    }
  }

  ProfStart(const ProfStart &) = delete;
  ProfStart &operator=(const ProfStart &) = delete;

  void Stop() noexcept {
    const uint64_t rid = rid_.exchange(kInvalidProfRange);
    if (rid != kInvalidProfRange) {
      HixlGetProfFuncs().stop(rid);
    }
  }

 private:
  std::atomic<uint64_t> rid_{kInvalidProfRange};
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_PROFILING_PROF_PROXY_H_
