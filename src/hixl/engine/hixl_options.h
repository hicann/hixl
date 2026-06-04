/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HIXL_SRC_HIXL_ENGINE_HIXL_OPTIONS_H_
#define HIXL_SRC_HIXL_ENGINE_HIXL_OPTIONS_H_

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "hixl/hixl_types.h"
#include "adxl/adxl_types.h"

namespace hixl {

struct FabricMemoryConfig {
  std::optional<size_t> max_capacity;
  std::optional<size_t> start_address;
  std::optional<size_t> task_stream_num;
};

struct ConnectPoolConfig {
  std::optional<int32_t> thread_num;
  std::optional<int32_t> task_queue_capacity;
};

struct CommResourceConfigDesc {
  std::optional<std::vector<std::string>> protocol_desc;
};

struct GlobalResourceConfig {
  FabricMemoryConfig fabric_memory;
  ConnectPoolConfig connect_pool;
  CommResourceConfigDesc comm_resource_config;
};

class HixlOptions {
 public:
  static Status Parse(const std::map<AscendString, AscendString> &options, HixlOptions &result);

  Status CheckSupportedOptions(const std::unordered_set<std::string> &supported_keys) const;
  const std::map<AscendString, AscendString>& RawOptions() const { return raw_options_; }

  std::optional<uint8_t> RdmaTrafficClass() const { return rdma_traffic_class_; }
  std::optional<uint8_t> RdmaServiceLevel() const { return rdma_service_level_; }
  std::optional<std::string> LocalCommRes() const { return local_comm_res_; }
  std::optional<bool> EnableFabricMem() const { return enable_fabric_mem_; }
  std::optional<bool> AutoConnect() const { return auto_connect_; }

  std::optional<GlobalResourceConfig> GlobalResourceCfg() const { return global_resource_config_; }

 private:
  std::map<AscendString, AscendString> raw_options_;
  std::unordered_set<std::string> parsed_keys_;

  std::optional<uint8_t> rdma_traffic_class_;
  std::optional<uint8_t> rdma_service_level_;
  std::optional<std::string> local_comm_res_;
  std::optional<bool> enable_fabric_mem_;
  std::optional<bool> auto_connect_;
  std::optional<GlobalResourceConfig> global_resource_config_;

  Status ParseRdmaOptions(const std::map<AscendString, AscendString> &options);
  Status ParseEndpointOptions(const std::map<AscendString, AscendString> &options);
  Status ParseFabricMemOptions(const std::map<AscendString, AscendString> &options);
  Status ParseGlobalResourceConfig(const std::map<AscendString, AscendString> &options);
};

}  // namespace hixl

#endif  // HIXL_SRC_HIXL_ENGINE_HIXL_OPTIONS_H_
