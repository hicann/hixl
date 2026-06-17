/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef TESTS_DEPENDS_LLM_DATADIST_SRC_LLM_DATADIST_TEST_HELPER_H_
#define TESTS_DEPENDS_LLM_DATADIST_SRC_LLM_DATADIST_TEST_HELPER_H_

#include <gtest/gtest.h>
#include <numeric>
#include <map>
#include <string>
#include <vector>

#include "llm_datadist/llm_datadist.h"

namespace llm_datadist {

// helper struct for KV cache setup result
struct KvCacheSetupResult {
  std::vector<std::vector<int32_t>> buffers;
  int64_t cache_id = 0;
};

// helper to prepare common init options and JSON prefix
inline std::string PrepareInitCommon(std::map<AscendString, AscendString> &options, const std::string &device_id,
                                     bool is_prompt) {
  if (is_prompt) {
    options[OPTION_LISTEN_IP_INFO] = "127.0.0.1:26000";
  }
  options[OPTION_DEVICE_ID] = device_id.c_str();
  return "\n    {\n"
         "      \"server_count\": \"1\",\n"
         "      \"server_list\": [{\n"
         "        \"device\": [{\n"
         "          \"device_id\": \"" +
         device_id + "\"";
}

// helper to initialize LlmDataDist instance
inline void InitTestLlmDataDist(LlmDataDist &dist, const std::string &device_id, const std::string &device_ip,
                                bool is_prompt) {
  std::map<AscendString, AscendString> options;
  std::string json = PrepareInitCommon(options, device_id, is_prompt) +
                     ",\n"
                     "          \"device_ip\": \"" +
                     device_ip +
                     "\"\n"
                     "        }],\n"
                     "        \"server_id\": \"127.0.0.1\"\n"
                     "      }],\n"
                     "      \"status\": \"completed\",\n"
                     "      \"version\": \"1.0\"\n"
                     "    }";
  options["llm.LocalCommRes"] = json.c_str();
  EXPECT_EQ(dist.Initialize(options), ge::SUCCESS);
}

// helper to set up KV cache with tensor buffers
inline KvCacheSetupResult SetupKvCache(LlmDataDist &dist, const CacheDesc &kv_desc) {
  KvCacheSetupResult result;
  result.buffers =
      std::vector<std::vector<int32_t>>(kv_desc.num_tensors, std::vector<int32_t>(kv_desc.shape[0] * kv_desc.shape[1]));
  std::vector<uint64_t> addrs;
  for (uint32_t i = 0; i < kv_desc.num_tensors; ++i) {
    addrs.emplace_back(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(result.buffers[i].data())));
  }
  std::iota(result.buffers[0].begin(), result.buffers[0].end(), 0);
  RegisterCfg cfg{};
  result.cache_id = 0;
  EXPECT_EQ(dist.RegisterKvCache(kv_desc, addrs, cfg, result.cache_id), ge::SUCCESS);
  return result;
}

// helper to initialize LlmDataDist with V1.2 JSON (super_device_id, super_pod_list)
inline void InitTestLlmDataDistV12(LlmDataDist &dist, const std::string &device_id, const std::string &device_ip,
                                   bool is_prompt) {
  std::map<AscendString, AscendString> options;
  std::string json = PrepareInitCommon(options, device_id, is_prompt) +
                     ",\n"
                     "          \"super_device_id\": \"" +
                     device_id +
                     "\",\n"
                     "          \"device_ip\": \"" +
                     device_ip +
                     "\"\n"
                     "        }],\n"
                     "        \"server_id\": \"127.0.0.1\"\n"
                     "      }],\n"
                     "      \"super_pod_list\": [\n"
                     "      {\n"
                     "          \"super_pod_id\": \"0\",\n"
                     "          \"server_list\": [\n"
                     "          {\"server_id\": \"127.0.0.1\"}]\n"
                     "      }],\n"
                     "      \"status\": \"completed\",\n"
                     "      \"version\": \"1.2\"\n"
                     "    }";
  options["llm.LocalCommRes"] = json.c_str();
  EXPECT_EQ(dist.Initialize(options), ge::SUCCESS);
}

}  // namespace llm_datadist

#endif  // TESTS_DEPENDS_LLM_DATADIST_SRC_LLM_DATADIST_TEST_HELPER_H_
