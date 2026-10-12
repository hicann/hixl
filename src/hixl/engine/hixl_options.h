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

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "hixl/hixl_types.h"
#include "adxl/adxl_types.h"
#include "common/hixl_inner_types.h"
#include "common/transfer_config.h"
#include "cs/ubmem/ubmem_types.h"

namespace hixl {

// Single source of GlobalResourceConfig JSON keys. Parsers and serializers must share these constants
// so that a new field only needs one definition here instead of scattered string literals.
constexpr const char *kGlobalResCfgFabricMemory = "fabric_memory";
constexpr const char *kGlobalResCfgListenPort = "comm_resource_config.listen_port";
// The qos key is shared with the CS-layer parser (common/hixl_inner_types.h: kQosName);
// alias it so the key list stays complete without defining the same literal twice.
constexpr const char *kGlobalResCfgQos = kQosName;
constexpr const char *kGlobalResCfgMaxActiveChannels = "comm_resource_config.max_active_channels";
constexpr const char *kGlobalResCfgProtocolDesc = "comm_resource_config.protocol_desc";
constexpr const char *kGlobalResCfgNumWorkers = "comm_resource_config.multi_channel.num_workers";
constexpr const char *kGlobalResCfgSplitBatchSize = "comm_resource_config.multi_channel.split_batch_size";
constexpr const char *kGlobalResCfgTransferCount = "transfer_config.max_transfer_count_per_batch";
constexpr const char *kGlobalResCfgMaxCapacity = "fabric_memory.max_capacity";
constexpr const char *kGlobalResCfgStartAddress = "fabric_memory.start_address";
constexpr const char *kGlobalResCfgTaskStreamNum = "fabric_memory.task_stream_num";
constexpr const char *kGlobalResCfgEnableAicpuUnfold = "fabric_memory.enable_aicpu_unfold";
constexpr const char *kGlobalResCfgConnectPoolThreadNum = "connect_pool.thread_num";
constexpr const char *kGlobalResCfgConnectPoolTaskQueueCapacity = "connect_pool.task_queue_capacity";
constexpr const char *kGlobalResCfgLocalCommResPath = "local_comm_res_path";
constexpr const char *kGlobalResCfgTopoFilePath = "topo_file_path";

struct ConnectPoolConfig {
  std::optional<int32_t> thread_num;
  std::optional<int32_t> task_queue_capacity;
};

struct CommResourceConfigDesc {
  std::optional<std::vector<std::string>> protocol_desc;
  std::optional<uint32_t> listen_port;
  std::optional<uint8_t> qos;
  std::optional<uint32_t> max_active_channels;
  std::optional<uint32_t> multi_worker_num;
  std::optional<uint32_t> multi_channel_split_batch_size;
};

struct TransferConfig {
  uint32_t max_transfer_count_per_batch{kDefaultMaxTransferCountPerBatch};
  bool max_transfer_count_per_batch_configured{false};
};

struct GlobalResourceConfig {
  UbMemoryConfig fabric_memory;
  ConnectPoolConfig connect_pool;
  CommResourceConfigDesc comm_resource_config;
  TransferConfig transfer_config;
  std::optional<std::string> local_comm_res_path;  // local_comm_res JSON file path
  std::optional<std::string> topo_file_path;       // hardware topo JSON path for auto-gen
};

class HixlOptions {
 public:
  static Status Parse(const std::map<AscendString, AscendString> &options, HixlOptions &result);

  Status CheckSupportedOptions(const std::unordered_set<std::string> &supported_keys) const;
  const std::map<AscendString, AscendString> &RawOptions() const {
    return raw_options_;
  }

  std::optional<uint8_t> RdmaTrafficClass() const {
    return rdma_traffic_class_;
  }
  std::optional<uint8_t> RdmaServiceLevel() const {
    return rdma_service_level_;
  }
  std::optional<std::string> LocalCommRes() const {
    return local_comm_res_;
  }
  std::optional<bool> EnableUbMem() const {
    return enable_ubmem_;
  }
  std::optional<bool> AutoConnect() const {
    return auto_connect_;
  }

  std::optional<GlobalResourceConfig> GlobalResourceCfg() const {
    return global_resource_config_;
  }
  std::vector<std::string> GetProtocolDesc() const;
  bool HasProtocolDesc(const std::string &token) const;
  std::optional<std::string> TopoFilePath() const;

 private:
  std::map<AscendString, AscendString> raw_options_;
  std::unordered_set<std::string> parsed_keys_;

  std::optional<uint8_t> rdma_traffic_class_;
  std::optional<uint8_t> rdma_service_level_;
  std::optional<std::string> local_comm_res_;
  std::optional<bool> enable_ubmem_;
  std::optional<bool> auto_connect_;
  std::optional<GlobalResourceConfig> global_resource_config_;

  Status ParseRdmaOptions(const std::map<AscendString, AscendString> &options);
  Status ParseEndpointOptions(const std::map<AscendString, AscendString> &options);
  Status ParseUbMemOptions(const std::map<AscendString, AscendString> &options);
  Status ParseAutoConnectOptions(const std::map<AscendString, AscendString> &options);
  Status ParseGlobalResourceConfig(const std::map<AscendString, AscendString> &options);
  Status ParseGlobalResourceConfig(const std::string &config_str);
  Status ResolveLocalCommResFromFile();
  Status ApplyUbMemEquivalence();
};

// Options accepted by the HIXL engine layer. Defined once so HixlEngine/CommEngine no longer keep
// duplicated copies of the same list.
const std::unordered_set<std::string> &GetSupportedOptionKeys();

// Serialize GlobalResourceConfig into the JSON form expected by the CS boundary. Server and client
// keep different field subsets, so both variants live next to the parsing code.
std::string SerializeGlobalResourceConfigForServer(const GlobalResourceConfig &config);
std::string SerializeGlobalResourceConfigForClient(const GlobalResourceConfig &config, std::optional<uint8_t> rdma_tc,
                                                   std::optional<uint8_t> rdma_sl);

}  // namespace hixl

#endif  // HIXL_SRC_HIXL_ENGINE_HIXL_OPTIONS_H_
