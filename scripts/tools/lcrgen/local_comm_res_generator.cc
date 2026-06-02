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
 * @file local_comm_res_generator.cc
 * @brief GenerateLocalCommRes 接口测试脚本
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include "acl/acl.h"
#include "local_comm_res_generator_v1.h"
#include "graph/ascend_string.h"
#include <nlohmann/json.hpp>

namespace {
void PrintUsage(const char *prog_name) {
  std::cout << "Usage: " << prog_name << " <npu_id>\n"
            << "  npu_id: Logic device ID (0, 1, 2, ...)\n"
            << "Example: " << prog_name << " 0\n";
}

aclError InitAcl() {
  aclError ret = aclInit(nullptr);
  if (ret != ACL_SUCCESS) {
    std::cerr << "aclInit failed, ret=" << ret << std::endl;
    return ACL_ERROR_INVALID_PARAM;
  }
  std::cout << "ACL initialized successfully\n";
  return ACL_SUCCESS;
}

aclError FinalizeAcl() {
  aclError ret = aclFinalize();
  if (ret != ACL_SUCCESS) {
    std::cerr << "aclFinalize failed, ret=" << ret << std::endl;
    return ACL_ERROR_INVALID_PARAM;
  }
  std::cout << "ACL finalized successfully\n";
  return ACL_SUCCESS;
}

aclError SetDevice(int32_t device_id) {
  aclError ret = aclrtSetDevice(device_id);
  if (ret != ACL_SUCCESS) {
    std::cerr << "aclrtSetDevice(" << device_id << ") failed, ret=" << ret << std::endl;
    return ACL_ERROR_INVALID_PARAM;
  }
  std::cout << "aclrtSetDevice(" << device_id << ") succeeded\n";
  return ACL_SUCCESS;
}

aclError GetCurrentDevice(int32_t *device_id) {
  aclError ret = aclrtGetDevice(device_id);
  if (ret != ACL_SUCCESS) {
    std::cerr << "aclrtGetDevice failed, ret=" << ret << std::endl;
    return ACL_ERROR_INVALID_PARAM;
  }
  std::cout << "Current device: logic_id=" << *device_id << std::endl;
  return ACL_SUCCESS;
}

aclError GetPhyDevId(int32_t logic_id, int32_t *phy_id) {
  aclError ret = aclrtGetPhyDevIdByLogicDevId(logic_id, phy_id);
  if (ret != ACL_SUCCESS) {
    std::cerr << "aclrtGetPhyDevIdByLogicDevId(" << logic_id << ") failed, ret=" << ret << std::endl;
    return ACL_ERROR_INVALID_PARAM;
  }
  std::cout << "Logic ID " << logic_id << " -> Physical ID " << *phy_id << std::endl;
  return ACL_SUCCESS;
}

// 基于 libcann_hixl.so 通过 GenerateLocalCommResJson 给出的 LocalCommResView
// 在本地拼成 JSON 字符串并打屏输出，保留 2 空格缩进便于用户复制粘贴。
void PrintLocalCommRes(const hixl::LocalCommResView &view) {
  nlohmann::json j;
  j["version"] = view.version.GetString();
  j["net_instance_id"] = view.net_instance_id.GetString();
  j["endpoint_list"] = nlohmann::json::array();
  for (const auto &ep : view.endpoint_list) {
    nlohmann::json ep_json;
    ep_json["protocol"] = ep.protocol.GetString();
    ep_json["comm_id"] = ep.comm_id.GetString();
    ep_json["placement"] = ep.placement.GetString();
    if (ep.plane.GetLength() > 0) {
      ep_json["plane"] = ep.plane.GetString();
    }
    if (ep.dst_eid.GetLength() > 0) {
      ep_json["dst_eid"] = ep.dst_eid.GetString();
    }
    j["endpoint_list"].push_back(std::move(ep_json));
  }
  std::cout << j.dump(2) << "\n";
}

}  // anonymous namespace

int main(int argc, char *argv[]) {
  if (argc != 2) {
    PrintUsage(argv[0]);
    return ACL_ERROR_INVALID_PARAM;
  }

  // 解析 npu_id 参数
  int32_t npu_id = -1;
  try {
    npu_id = std::stoi(argv[1]);
  } catch (const std::exception &e) {
    std::cerr << "Invalid npu_id: " << argv[1] << std::endl;
    PrintUsage(argv[0]);
    return ACL_ERROR_INVALID_PARAM;
  }

  if (npu_id < 0) {
    std::cerr << "npu_id must be non-negative, got: " << npu_id << std::endl;
    return ACL_ERROR_INVALID_PARAM;
  }

  // Step 1: 初始化 ACL
  if (InitAcl() != ACL_SUCCESS) {
    return ACL_ERROR_INVALID_PARAM;
  }

  // Step 2: 占用 NPU
  if (SetDevice(npu_id) != ACL_SUCCESS) {
    FinalizeAcl();
    return ACL_ERROR_INVALID_PARAM;
  }

  // Step 3: 获取当前 logic_id
  int32_t logic_id = -1;
  if (GetCurrentDevice(&logic_id) != ACL_SUCCESS) {
    aclrtResetDevice(npu_id);
    FinalizeAcl();
    return ACL_ERROR_INVALID_PARAM;
  }

  // Step 4: 转换为 physical_id
  int32_t phy_id = -1;
  if (GetPhyDevId(logic_id, &phy_id) != ACL_SUCCESS) {
    aclrtResetDevice(npu_id);
    FinalizeAcl();
    return ACL_ERROR_INVALID_PARAM;
  }

  // Step 5: 调用 GenerateLocalCommResJson
  // 内部完成 LocalCommRes 组装 + std::string → AscendString 转换，输出
  // LocalCommResView（所有字符串字段均为 AscendString，跨 .so 边界 ABI 安全）。
  // JSON 序列化由 lcrgen 工具在本地完成，避免 lcrgen 持有 std::string 变量。
  std::cout << "\nCalling GenerateLocalCommResJson(phy_id=" << phy_id << ")...\n";
  hixl::LocalCommResView view;
  int32_t ret = hixl::GenerateLocalCommResJson(phy_id, view);
  if (ret != hixl::SUCCESS) {
    std::cerr << "GenerateLocalCommResJson failed, ret=" << ret << std::endl;
    aclrtResetDevice(npu_id);
    FinalizeAcl();
    return ACL_ERROR_INVALID_PARAM;
  }

  // Step 6: 打印结果
  PrintLocalCommRes(view);

  // Step 7: 清理
  aclrtResetDevice(npu_id);
  FinalizeAcl();

  std::cout << "Test completed successfully!\n";
  return ACL_SUCCESS;
}
