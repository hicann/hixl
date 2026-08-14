/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

/**
 * @file hixl_tool.cc
 * @brief hixl_tool 子命令分发入口
 *
 * 用法：
 *   hixl_tool host_route [--output <dir>]
 *   hixl_tool local_comm_res --topo_file_path <path> [options...]
 *
 * route_data 由 DSMI + urma_admin + DCMI 自动生成，local_comm_res 不再依赖 host_route.json。
 */

#include <cstdio>
#include <cstring>
#include <string>
#include "host_route.h"
#include "local_comm_res.h"

namespace {

void PrintUsage() {
  std::printf("Usage: hixl_tool <subcommand> [options]\n\n");
  std::printf("Subcommands:\n");
  std::printf("  host_route       Optional: export Host/NPU EID inventory to host_route.json\n");
  std::printf("  local_comm_res   Generate LocalCommRes configuration files for each NPU\n\n");
  std::printf("Run 'hixl_tool <subcommand> --help' for subcommand-specific options.\n");
}

}  // namespace

int main(int argc, char *argv[]) {
  if (argc < 2) {
    PrintUsage();
    return 1;
  }

  std::string subcommand = argv[1];
  // Shift argv to skip subcommand name, so subcommand handlers see their own options as argv[1]
  int sub_argc = argc - 1;
  char **sub_argv = argv + 1;

  if (subcommand == "host_route") {
    return hixl_tool::RunHostRoute(sub_argc, sub_argv);
  }
  if (subcommand == "local_comm_res") {
    return hixl_tool::RunLocalCommRes(sub_argc, sub_argv);
  }

  std::fprintf(stderr, "[ERROR] Unknown subcommand: %s\n", subcommand.c_str());
  PrintUsage();
  return 1;
}
