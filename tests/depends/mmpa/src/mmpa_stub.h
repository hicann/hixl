/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.

 * The code snippet comes from Huawei's open-source Ascend project.
 * Copyright 2019-2020 Huawei Technologies Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 */

#ifndef AIR_TESTS_DEPENDS_MMPA_SRC_MMAP_STUB_H_
#define AIR_TESTS_DEPENDS_MMPA_SRC_MMAP_STUB_H_

#include <cstdint>
#include <memory>
#include "mmpa/mmpa_api.h"

namespace llm {
class MmpaStubApiGe {
 public:
  virtual ~MmpaStubApiGe() = default;

  virtual void *DlOpen(const char *file_name, int32_t mode) {
    return dlopen(file_name, mode);
  }

  virtual void *DlSym(void *handle, const char *func_name) {
    return dlsym(handle, func_name);
  }

  virtual int32_t DlClose(void *handle) {
    return dlclose(handle);
  }

  virtual int32_t RealPath(const CHAR *path, CHAR *realPath, INT32 realPathLen) {
    INT32 ret = EN_OK;
    char *ptr = realpath(path, realPath);
    if (ptr == nullptr) {
      ret = EN_ERROR;
    }
    return ret;
  };

  virtual int32_t Sleep(UINT32 microSecond) {
    INT32 ret = usleep(microSecond);
    if (ret != EN_OK) {
      return EN_ERROR;
    }
    return ret;
  }

  virtual INT32 WaitPid(mmProcess pid, INT32 *status, INT32 options) {
    if ((options != MMPA_ZERO) && (options != M_WAIT_NOHANG) && (options != M_WAIT_UNTRACED)) {
    return EN_INVALID_PARAM;
    }

    INT32 ret = waitpid(pid, status, options);
    if (ret == EN_ERROR) {
      return EN_ERROR;
    }
    if ((ret > MMPA_ZERO) && (ret == pid)) {
      if (status != NULL) {
        if (WIFEXITED(*status)) {
          *status = WEXITSTATUS(*status);
        }
        if(WIFSIGNALED(*status)) {
          *status = WTERMSIG(*status);
        }
      }
      return EN_ERR;
    }
    return EN_OK;
  }

  virtual INT32 Open2(const CHAR *path_name, INT32 flags, MODE mode) {
    INT32 fd = HANDLE_INVALID_VALUE;
    fd = open(path_name, flags, mode);
    if (fd < MMPA_ZERO) {
      return EN_ERROR;
    }
    return fd;
  }

  virtual mmSsize_t Write(INT32 fd, VOID *mm_buf, UINT32 mm_count) {
    mmSsize_t result = MMPA_ZERO;
    if ((fd < MMPA_ZERO) || (NULL == mm_buf)) {
      return EN_INVALID_PARAM;
    }

    result = write(fd, mm_buf, (size_t) mm_count);
    if (result < MMPA_ZERO) {
      return EN_ERROR;
    }
    return result;
  }

  virtual INT32 FStatGet(INT32 fd, mmStat_t *buf) {
    return fstat(fd, buf);
  }

  virtual mmSsize_t mmWrite(int32_t fd, void *buf, uint32_t bufLen) {
    if (fd < 0 || buf == nullptr) {
      return EN_INVALID_PARAM;
    }

    mmSsize_t ret = write(fd, buf, (size_t)bufLen);
    if (ret < 0) {
      return EN_ERROR;
    }
    return ret;
  }

  virtual INT32 mmAccess2(const CHAR *pathName, INT32 mode)
  {
    if (pathName == NULL) {
      return EN_INVALID_PARAM;
    }
    INT32 ret = access(pathName, mode);
    if (ret != EN_OK) {
      return EN_ERROR;
    }
    return EN_OK;
  }

  virtual INT32 Access(const CHAR *path_name)
  {
    if (path_name == NULL) {
      return EN_INVALID_PARAM;
    }

    INT32 ret = access(path_name, F_OK);
    if (ret != EN_OK) {
      return EN_ERROR;
    }
    return EN_OK;
  }

  virtual INT32 StatGet(const CHAR *path, mmStat_t *buffer) {
    if ((path == NULL) || (buffer == NULL)) {
      return EN_INVALID_PARAM;
    }

    INT32 ret = stat(path, buffer);
    if (ret != EN_OK) {
      return EN_ERROR;
    }
    return EN_OK;
  }

  virtual INT32 mmMkdir(const CHAR *lp_path_name, mmMode_t mode) {
    INT32 t_mode = mode;
    INT32 ret = EN_OK;

    if (NULL == lp_path_name) {
      syslog(LOG_ERR, "The input path is null.\r\n");
      return EN_INVALID_PARAM;
    }

    if (t_mode < MMPA_ZERO) {
      syslog(LOG_ERR, "The input mode is wrong.\r\n");
      return EN_INVALID_PARAM;
    }

    ret = mkdir(lp_path_name, mode);
    if (EN_OK != ret) {
      syslog(LOG_ERR, "Failed to create the directory, the ret is %s.\r\n", strerror(errno));
      return EN_ERROR;
    }
    return EN_OK;
  }
};

class MmpaStub {
 public:
  static MmpaStub& GetInstance() {
    static MmpaStub instance;
    return instance;
  }

  void SetImpl(const std::shared_ptr<MmpaStubApiGe> &impl) {
    impl_ = impl;
  }

  MmpaStubApiGe* GetImpl() {
    return impl_.get();
  }

  void Reset() {
    impl_ = std::make_shared<MmpaStubApiGe>();
  }

 private:
  MmpaStub(): impl_(std::make_shared<MmpaStubApiGe>()) {
  }

  std::shared_ptr<MmpaStubApiGe> impl_;
};

}  // namespace llm

#endif  // AIR_TESTS_DEPENDS_MMPA_SRC_MMAP_STUB_H_
