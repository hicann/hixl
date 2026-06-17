/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_GRAPH_ENGINE_TESTS_DEPENDS_LLM_ENGINE_HCCN_CONF_HELPER_H_
#define CANN_GRAPH_ENGINE_TESTS_DEPENDS_LLM_ENGINE_HCCN_CONF_HELPER_H_

#include <fstream>
#include <cstdio>
#include <iostream>
#include <string>

namespace llm {

// 从外部配置文件读取HCCN设备地址，写入/tmp/hccn.conf，避免代码中硬编码公网地址
inline void WriteHccnConfFile() {
#ifdef HCCN_TEST_CONF_PATH
  const std::string src_path = HCCN_TEST_CONF_PATH;
#else
  const std::string src_path = "tests/depends/llm_datadist/src/hccn_test.conf";
#endif
  const std::string dst_path = "/tmp/hccn.conf";

  std::ifstream src(src_path);
  if (!src.is_open()) {
    std::cout << "Failed to open source config file: " << src_path << std::endl;
    return;
  }

  std::ofstream dst(dst_path);
  if (!dst.is_open()) {
    std::cout << "Failed to create file: " << dst_path << std::endl;
    return;
  }

  dst << src.rdbuf();
}

// remove /tmp/hccn.conf
inline void RemoveHccnConfFile() {
  const std::string file_path = "/tmp/hccn.conf";
  if (std::remove(file_path.c_str()) != 0) {
    std::cout << "Failed to delete file:" << file_path.c_str() << std::endl;
  }
}

}  // namespace llm

#endif  // CANN_GRAPH_ENGINE_TESTS_DEPENDS_LLM_ENGINE_HCCN_CONF_HELPER_H_
