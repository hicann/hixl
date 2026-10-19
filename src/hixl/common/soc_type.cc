/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "soc_type.h"

#include <array>
#include <string>

#include "acl/acl.h"
#include "hixl_checker.h"
#include "hixl_log.h"

namespace hixl {
namespace {
struct SocPrefix {
  const char *prefix;
  SocType type;
};

// Prefix table, matched in order. An unknown or empty name falls through to SocType::kDefault.
constexpr std::array<SocPrefix, 2> kSocPrefixes = {{
    {"Ascend910B", SocType::kGen1},
    {"Ascend910_93", SocType::kGen2},
}};

bool StartsWith(const std::string &value, const char *prefix) {
  const std::string prefix_str(prefix);
  return value.size() >= prefix_str.size() && value.compare(0, prefix_str.size(), prefix_str) == 0;
}

SocType GetSocTypeByName(const std::string &soc_name) {
  for (const auto &entry : kSocPrefixes) {
    if (StartsWith(soc_name, entry.prefix)) {
      return entry.type;
    }
  }
  return SocType::kDefault;
}

const char *SocTypeToString(SocType soc_type) {
  switch (soc_type) {
    case SocType::kGen1:
      return "gen1";
    case SocType::kGen2:
      return "gen2";
    default:
      return "default";
  }
}
}  // namespace

Status SocTypeUtil::GetSocType(SocType &soc_type) {
  const char *soc_name_cstr = aclrtGetSocName();
  HIXL_CHK_BOOL_RET_STATUS(soc_name_cstr != nullptr, FAILED, "aclrtGetSocName returned nullptr");
  const std::string soc_name(soc_name_cstr);
  HIXL_CHK_BOOL_RET_STATUS(!soc_name.empty(), FAILED, "soc_name is empty");
  soc_type = GetSocTypeByName(soc_name);
  HIXL_EVENT("[GetSocType] soc=%s, soc_type=%s", soc_name.c_str(), SocTypeToString(soc_type));
  return SUCCESS;
}

}  // namespace hixl
