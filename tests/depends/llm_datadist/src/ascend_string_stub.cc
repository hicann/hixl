/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.

 * The code snippet comes from Huawei's open-source Ascend project.
 * Copyright 2020 Huawei Technologies Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 */

#include "graph/ascend_string.h"
#include "memory"
namespace ge {
using char_t = char;
AscendString::AscendString(const char_t *const name) {
  if (name != nullptr) {
    name_ = std::make_shared<std::string>(name);
  }
}

AscendString::AscendString(const char_t *const name, size_t length) {
  if (name != nullptr) {
    name_ = std::make_shared<std::string>(name, length);
  }
}
const char_t *AscendString::GetString() const {
  if (name_ == nullptr) {
    const static char *empty_value = "";
    return empty_value;
  }

  return (*name_).c_str();
}

size_t AscendString::GetLength() const {
  if (name_ == nullptr) {
    return 0UL;
  }

  return (*name_).length();
}

bool AscendString::operator<(const AscendString &d) const {
  if ((name_ == nullptr) && (d.name_ == nullptr)) {
    return false;
  } else if (name_ == nullptr) {
    return true;
  } else if (d.name_ == nullptr) {
    return false;
  } else {
    return (*name_) < (*(d.name_));
  }
}

bool AscendString::operator>(const AscendString &d) const {
  if ((name_ == nullptr) && (d.name_ == nullptr)) {
    return false;
  } else if (name_ == nullptr) {
    return false;
  } else if (d.name_ == nullptr) {
    return true;
  } else {
    return (*name_) > (*(d.name_));
  }
}

bool AscendString::operator==(const AscendString &d) const {
  if ((name_ == nullptr) && (d.name_ == nullptr)) {
    return true;
  } else if (name_ == nullptr) {
    return false;
  } else if (d.name_ == nullptr) {
    return false;
  } else {
    return (*name_) == (*(d.name_));
  }
}

bool AscendString::operator<=(const AscendString &d) const {
  if (name_ == nullptr) {
    return true;
  } else if (d.name_ == nullptr) {
    return false;
  } else {
    return (*name_) <= (*(d.name_));
  }
}

bool AscendString::operator>=(const AscendString &d) const {
  if (d.name_ == nullptr) {
    return true;
  } else if (name_ == nullptr) {
    return false;
  } else {
    return (*name_) >= (*(d.name_));
  }
}

bool AscendString::operator!=(const AscendString &d) const {
  if ((name_ == nullptr) && (d.name_ == nullptr)) {
    return false;
  } else if (name_ == nullptr) {
    return true;
  } else if (d.name_ == nullptr) {
    return true;
  } else {
    return (*name_) != (*(d.name_));
  }
}

bool AscendString::operator==(const char_t *const d) const {
  auto tmp = AscendString(d);
  return *this == tmp;
}

bool AscendString::operator!=(const char_t *const d) const {
  auto tmp = AscendString(d);
  return !(*this == tmp);
}
}  // namespace ge
