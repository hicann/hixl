/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HIXL_TEST_MMPA_UTILS_H
#define HIXL_TEST_MMPA_UTILS_H

#include <fstream>
#include <string>
#include <vector>
#include <unistd.h>

#include "securec.h"
#include "depends/mmpa/src/mmpa_stub.h"

namespace hixl {
namespace test {
class TestMmpaStub : public llm::MmpaStubApiGe {
 public:
  std::string fake_real_path_;
  bool real_path_ok_ = false;
  bool access_ok_ = false;

  INT32 RealPath(const CHAR *path, CHAR *realPath, INT32 realPathLen) override {
    (void)path;
    if (!real_path_ok_ || fake_real_path_.empty() || realPathLen <= 0) {
      return EN_ERROR;
    }
    size_t dest_max = static_cast<size_t>(realPathLen);
    errno_t ret = strncpy_s(realPath, dest_max, fake_real_path_.c_str(), dest_max - 1);
    return (ret == EOK) ? EN_OK : EN_ERROR;
  }

  INT32 Access(const CHAR *path_name) override {
    (void)path_name;
    return access_ok_ ? EN_OK : EN_ERROR;
  }
};

inline std::string CreateTempFileWithContent(const std::string &file_template, const std::string &content) {
  std::vector<char> template_buf(file_template.begin(), file_template.end());
  template_buf.push_back('\0');
  int fd = mkstemp(template_buf.data());
  if (fd == -1) {
    return "";
  }
  close(fd);

  std::ofstream ofs(template_buf.data());
  if (!ofs.is_open()) {
    return "";
  }
  ofs << content;
  ofs.close();
  return std::string(template_buf.data());
}
}  // namespace test
}  // namespace hixl

#endif
