/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_GRAPH_ENGINE_INC_EXTERNAL_LLM_DATADIST_LLM_DATA_DIST_H_
#define CANN_GRAPH_ENGINE_INC_EXTERNAL_LLM_DATADIST_LLM_DATA_DIST_H_

#include <cstdint>
#include <map>
#include <vector>
#include "external/ge_common/ge_api_error_codes.h"

#ifdef FUNC_VISIBILITY
#define ASCEND_FUNC_VISIBILITY __attribute__((visibility("default")))
#else
#define ASCEND_FUNC_VISIBILITY
#endif

namespace llm_datadist {
namespace {
constexpr uint8_t kDefaultTensorNumPerLayer = 2U;
}
using Status = ge::Status;
using AscendString = ge::AscendString;
using DataType = ge::DataType;

// options
constexpr const char OPTION_LISTEN_IP_INFO[] = "llm.ListenIpInfo";
constexpr const char OPTION_DEVICE_ID[] = "llm.DeviceId";
constexpr const char OPTION_SYNC_CACHE_WAIT_TIME[] = "llm.SyncKvCacheWaitTime";
constexpr const char OPTION_BUF_POOL_CFG[] = "llm.BufPoolCfg";
constexpr const char OPTION_ENABLE_SET_ROLE[] = "llm.EnableSwitchRole";
constexpr const char OPTION_LOCAL_COMM_RES[] = "llm.LocalCommRes";
constexpr const char OPTION_TRANSFER_BACKEND[] = "llm.TransferBackend";

// status codes
constexpr Status LLM_SUCCESS = 0x0U;
constexpr Status LLM_FAILED = 0xFFFFFFFFU;
constexpr Status LLM_WAIT_PROC_TIMEOUT = 0x5010B001U;
constexpr Status LLM_KV_CACHE_NOT_EXIST = 0x5010B002U;
constexpr Status LLM_PARAM_INVALID = 0x5010B005U;
constexpr Status LLM_NOT_YET_LINK = 0x5010B007U;
constexpr Status LLM_ALREADY_LINK = 0x5010B008U;
constexpr Status LLM_LINK_FAILED = 0x5010B009U;
constexpr Status LLM_UNLINK_FAILED = 0x5010B00AU;
constexpr Status LLM_NOTIFY_PROMPT_UNLINK_FAILED = 0x5010B00BU;
constexpr Status LLM_CLUSTER_NUM_EXCEED_LIMIT = 0x5010B00CU;
constexpr Status LLM_PROCESSING_LINK = 0x5010B00DU;
constexpr Status LLM_DEVICE_OUT_OF_MEMORY = 0x5010B00EU;
constexpr Status LLM_EXIST_LINK = 0x5010B018U;
constexpr Status LLM_FEATURE_NOT_ENABLED = 0x5010B019U;
constexpr Status LLM_TIMEOUT = 0x5010B01AU;
constexpr Status LLM_LINK_BUSY = 0x5010B01BU;
constexpr Status LLM_OUT_OF_MEMORY = 0x5010B01CU;

// data types
using ge::DT_BOOL;
using ge::DT_BF16;
using ge::DT_FLOAT16;
using ge::DT_FLOAT;
using ge::DT_INT8;
using ge::DT_UINT8;
using ge::DT_INT16;
using ge::DT_UINT16;
using ge::DT_INT32;
using ge::DT_UINT32;
using ge::DT_INT64;
using ge::DT_UINT64;
using ge::DT_DOUBLE;
using ge::DT_STRING;
using ge::DT_DUAL_SUB_INT8;
using ge::DT_DUAL_SUB_UINT8;
using ge::DT_COMPLEX64;
using ge::DT_COMPLEX128;
using ge::DT_QINT8;
using ge::DT_QUINT8;
using ge::DT_QINT16;
using ge::DT_QUINT16;
using ge::DT_QINT32;
using ge::DT_RESOURCE;
using ge::DT_STRING_REF;
using ge::DT_DUAL;
using ge::DT_VARIANT;
using ge::DT_INT4;
using ge::DT_UINT1;
using ge::DT_INT2;
using ge::DT_UINT2;
using ge::DT_COMPLEX32;
using ge::DT_HIFLOAT8;
using ge::DT_FLOAT8_E5M2;
using ge::DT_FLOAT8_E4M3FN;
using ge::DT_FLOAT8_E8M0;
using ge::DT_FLOAT6_E3M2;
using ge::DT_FLOAT6_E2M3;
using ge::DT_FLOAT4_E2M1;
using ge::DT_FLOAT4_E1M2;
using ge::DT_UNDEFINED;

struct IpInfo {
  AscendString ip;
  uint16_t port = 0U;
  uint8_t reserved[128];
};

struct ClusterInfo {
  uint64_t remote_cluster_id = 0U;
  int32_t remote_role_type = 0;
  std::vector<IpInfo> local_ip_infos;
  std::vector<IpInfo> remote_ip_infos;
  uint8_t reserved[128];
};

enum class LlmRole : int32_t {
  kPrompt = 1,
  kDecoder = 2,
  kMix = 3,
  kEnd
};

struct CacheIndex {
  uint64_t cluster_id;
  int64_t cache_id;
  uint32_t batch_index;
  uint8_t reserved[128];
};

enum class CachePlacement : uint32_t {
  kHost = 0U,
  kDevice = 1U,
};

struct CacheDesc {
  CachePlacement placement = CachePlacement::kDevice;
  uint32_t num_tensors = 0U;
  DataType data_type = DT_UNDEFINED;
  std::vector<int64_t> shape;
  uint8_t reserved[128];
};

struct Cache {
  int64_t cache_id = -1;
  std::vector<uintptr_t> tensor_addrs;
  CacheDesc cache_desc;
  uint8_t reserved[128];
};

struct KvCacheExtParam {
  std::pair<int32_t, int32_t> src_layer_range = {-1, -1};
  std::pair<int32_t, int32_t> dst_layer_range{-1, -1};
  uint8_t tensor_num_per_layer = kDefaultTensorNumPerLayer;
  uint8_t reserved[127];
};

struct RegisterCfg {
  uint8_t reserved[128] = {0};
};
class ASCEND_FUNC_VISIBILITY LlmDataDist {
 public:
  /**
   * @brief 构造函数
   * @param cluster_id 集群ID
   * @param role 当前LlmDataDist的角色
   */
  LlmDataDist(uint64_t cluster_id, LlmRole role);

  /**
   * @brief 析构函数
   */
  ~LlmDataDist();

  /**
   * @brief 初始化LlmDataDist, 在调用其他接口前需要先调用该接口
   * @param options 初始化所需的选项
   * @return 成功:LLM_SUCCESS, 失败:其它.
   */
  Status Initialize(const std::map<AscendString, AscendString> &options);

  /**
   * @brief 终结LlmDataDist
   */
  void Finalize();

  /**
   * @brief 设置角色
   * @param options 设置角色时传入的选项，设置成Prompt时需要包含OPTION_LISTEN_IP_INFO
   * @return 成功:LLM_SUCCESS, 失败:其它
   */
  Status SetRole(LlmRole role, const std::map<AscendString, AscendString> &options = {});

  /**
   *  @brief 进行llm datadist间建链
   *  @param [in] clusters 需要建链的cluster信息
   *  @param [in] timeout_in_millis 超时时间，单位ms
   *  @param [out] rets 每个cluster建链结果
   *  @return 成功:LLM_SUCCESS, 失败:其它
   */
  Status LinkLlmClusters(const std::vector<ClusterInfo> &clusters,
                         std::vector<Status> &rets,
                         int32_t timeout_in_millis = 1000);

  /**
   *  @brief 进行llm datadist间断链
   *  @param [in] clusters 需要断链的cluster信息
   *  @param [in] timeout_in_millis 超时时间，单位ms
   *  @param [in] force_flag 是否强制断链
   *  @param [out] rets 每个cluster断链结果
   *  @return 成功:LLM_SUCCESS, 失败:其它
   */
  Status UnlinkLlmClusters(const std::vector<ClusterInfo> &clusters,
                           std::vector<Status> &rets,
                           int32_t timeout_in_millis = 1000,
                           bool force_flag = false);

  /**
   *  @brief 分配Cache
   *  @param [in] cache_desc cache描述
   *  @param [out] cache 分配出的cache
   *  @return 成功:LLM_SUCCESS, 失败:其它
   */
  Status AllocateCache(const CacheDesc &cache_desc, Cache &cache);

  /**
   *  @brief 释放Cache
   *  @param [in] cache_id cache的id
   *  @return 成功:LLM_SUCCESS, 失败:其它. Cache不存在或已释放也会返回LLM_SUCCESS
   */
  Status DeallocateCache(int64_t cache_id);

  /**
   * @brief 从远端拉取连续KV Cache
   * @param [in] src_cache_index 远端cache索引
   * @param [in] dst_cache 拉取到的本地目标cache
   * @param [in] batch_index 拉取到的本地目标cache的batch index
   * @param [in] size 拉取的大小, -1表示拉取源cache的完整数据
   * @param [in] ext_param 扩展参数
   * @return 成功:LLM_SUCCESS, 失败:其它
   */
  Status PullKvCache(const CacheIndex &src_cache_index,
                     const Cache &dst_cache,
                     uint32_t batch_index = 0U,
                     int64_t size = -1,
                     const KvCacheExtParam &ext_param = {});

  /**
   * @brief 从远端拉取KV blocks
   * @param [in] src_cache_index 远端cache索引
   * @param [in] dst_cache 拉取到的本地目标cache
   * @param [in] src_blocks 源block列表
   * @param [in] dst_blocks 目标block列表
   * @param [in] ext_param 扩展参数
   * @return 成功:LLM_SUCCESS, 失败:其它
   */
  Status PullKvBlocks(const CacheIndex &src_cache_index,
                      const Cache &dst_cache,
                      const std::vector<uint64_t> &src_blocks,
                      const std::vector<uint64_t> &dst_blocks,
                      const KvCacheExtParam &ext_param = {});

  /**
   * @brief 拷贝连续Cache
   * @param src_cache 源Cache
   * @param dst_cache 目标Cache
   * @param src_batch_index 源batch index
   * @param dst_batch_index 目标batch index
   * @param offset 偏移
   * @param size 大小, -1表示拷贝源cache的完整数据
   * @return 成功:LLM_SUCCESS, 失败:其它
   */
  Status CopyKvCache(const Cache &src_cache,
                     const Cache &dst_cache,
                     uint32_t src_batch_index = 0U,
                     uint32_t dst_batch_index = 0U,
                     uint64_t offset = 0U,
                     int64_t size = -1);

  /**
   * @brief 拷贝KV block
   * @param src_cache 源Cache
   * @param dst_cache 目标Cache
   * @param src_blocks 源block列表
   * @param dst_blocks_list 目标block列表，可以为多组，即可以同时拷贝到多个目标
   * @return 成功:LLM_SUCCESS, 失败:其它
   */
  Status CopyKvBlocks(const Cache &src_cache,
                      const Cache &dst_cache,
                      const std::vector<uint64_t> &src_blocks,
                      const std::vector<std::vector<uint64_t>> &dst_blocks_list);

  /**
   * @brief 向远端push连续KV Cache
   * @param [in] src_cache 本地cache
   * @param [in] dst_cache_index 远端cache索引
   * @param [in] src_batch_index 本地cache的batch index
   * @param [in] size 拉取的大小, -1表示拉取源cache的完整数据
   * @param [in] ext_param 扩展参数
   * @return 成功:LLM_SUCCESS, 失败:其它
   */
  Status PushKvCache(const Cache &src_cache,
                     const CacheIndex &dst_cache_index,
                     uint32_t src_batch_index = 0U,
                     int64_t size = -1,
                     const KvCacheExtParam &ext_param = {});

  /**
   * @brief 向远端push KV blocks
   * @param [in] src_cache 本地cache
   * @param [in] dst_cache_index 远端cache索引
   * @param [in] src_blocks 源block列表
   * @param [in] dst_blocks 目标block列表
   * @param [in] ext_param 扩展参数
   * @return 成功:LLM_SUCCESS, 失败:其它
   */
  Status PushKvBlocks(const Cache &src_cache,
                      const CacheIndex &dst_cache_index,
                      const std::vector<uint64_t> &src_blocks,
                      const std::vector<uint64_t> &dst_blocks,
                      const KvCacheExtParam &ext_param = {});

  /**
   * @brief 注册KV内存
   * @param [in] cache_desc 内存描述信息
   * @param [in] addrs 内存地址列表
   * @param [in] cfg 注册配置信息
   * @param [out] cache_id 内存cache
   * @return 成功:LLM_SUCCESS, 失败:其它
   */
  Status RegisterKvCache(const CacheDesc &cache_desc,
                         const std::vector<uint64_t> &addrs,
                         const RegisterCfg &cfg,
                         int64_t &cache_id);

  /**
   * @brief 解注册KV内存
   * @param [in] cache_id 内存cache
   * @return 成功:LLM_SUCCESS, 失败:其它
   */
  Status UnregisterKvCache(int64_t cache_id);

 private:
  class LlmDataDistImpl;
  std::unique_ptr<LlmDataDistImpl> impl_;
};
}  // namespace llm_datadist

#endif  // CANN_GRAPH_ENGINE_INC_EXTERNAL_LLM_DATADIST_LLM_DATA_DIST_H_
