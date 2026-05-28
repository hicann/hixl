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

#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>
#include <unistd.h>

#include "securec.h"
#include "depends/mmpa/src/mmpa_stub.h"

namespace hixl {
namespace test {

class ScopedPathGuard {
 public:
  explicit ScopedPathGuard(const std::string &path) {
    const char *old_path = std::getenv("PATH");
    had_path_ = old_path != nullptr;
    old_path_ = had_path_ ? old_path : "";
    setenv("PATH", path.c_str(), 1);
  }

  ~ScopedPathGuard() {
    if (had_path_) {
      setenv("PATH", old_path_.c_str(), 1);
    } else {
      unsetenv("PATH");
    }
  }

  ScopedPathGuard(const ScopedPathGuard &) = delete;
  ScopedPathGuard &operator=(const ScopedPathGuard &) = delete;

 private:
  bool had_path_ = false;
  std::string old_path_;
};

/**
 * Common MmpaStub mock for kernel json file path handling in unit tests.
 * Used by multiple test files to avoid code duplication.
 */
class TestMmpaStub : public llm::MmpaStubApiGe {
 public:
  std::string fake_real_path_;
  bool real_path_ok_ = false;
  bool access_ok_ = false;

  INT32 RealPath(const CHAR *path, CHAR *realPath, INT32 realPathLen) override {
    std::string path_str(path);
    // Handle kernel json file path for EnsureDeviceKernelLoadedLocked
    if (path_str.find("libcann_hixl_kernel.json") != std::string::npos && real_path_ok_) {
      strncpy_s(realPath, realPathLen, path, strlen(path));
      return EN_OK;
    }
    if (!real_path_ok_ || fake_real_path_.empty() || realPathLen <= 0) {
      return EN_ERROR;
    }
    size_t dest_max = static_cast<size_t>(realPathLen);
    errno_t ret = strncpy_s(realPath, dest_max, fake_real_path_.c_str(), dest_max - 1);
    return (ret == EOK) ? EN_OK : EN_ERROR;
  }

  INT32 Access(const CHAR *path_name) override {
    std::string path_str(path_name);
    // Handle kernel json file path for EnsureDeviceKernelLoadedLocked
    if (path_str.find("libcann_hixl_kernel.json") != std::string::npos && access_ok_) {
      return EN_OK;
    }
    return access_ok_ ? EN_OK : EN_ERROR;
  }
};

/**
 * Simplified MmpaStub mock that only handles kernel json file path.
 * Returns EN_OK for kernel json paths, delegates to base class for others.
 */
class KernelJsonMmpaStub : public llm::MmpaStubApiGe {
 public:
  INT32 Access(const CHAR *path_name) override {
    std::string path_str(path_name);
    if (path_str.find("libcann_hixl_kernel.json") != std::string::npos) {
      return EN_OK;
    }
    return llm::MmpaStubApiGe::Access(path_name);
  }

  int32_t RealPath(const CHAR *path, CHAR *realPath, INT32 realPathLen) override {
    std::string path_str(path);
    if (path_str.find("libcann_hixl_kernel.json") != std::string::npos) {
      strncpy_s(realPath, realPathLen, path, strlen(path));
      return EN_OK;
    }
    return llm::MmpaStubApiGe::RealPath(path, realPath, realPathLen);
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
