/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_ENGINE_HIXL_CLIENT_H_
#define CANN_HIXL_SRC_HIXL_ENGINE_HIXL_CLIENT_H_

#include <cstdint>
#include <mutex>
#include <string>
#include <sstream>
#include <vector>
#include <map>
#include "cs/hixl_cs.h"
#include "common/hixl_inner_types.h"
#include "common/segment.h"
#include "common/ctrl_msg.h"
#include "nlohmann/json.hpp"

namespace hixl {

enum class CommType : uint32_t {
  COMM_TYPE_UB_D2D = 0U,
  COMM_TYPE_UB_H2D = 1U,
  COMM_TYPE_UB_D2H = 2U,
  COMM_TYPE_UB_H2H = 3U,
  COMM_TYPE_ROCE = 4U,
  COMM_TYPE_HCCS = 5U
};

struct MatchKey {
  std::string dst_eid;
  std::string plane;
  std::string placement;

  // 添加ToString()方法
  std::string ToString() const {
    std::ostringstream oss;
    oss << "MatchKey{";
    oss << "dst_eid: " << dst_eid << ", ";
    oss << "plane: " << plane << ", ";
    oss << "placement: " << placement;
    oss << "}";
    return oss.str();
  }

  bool operator<(const MatchKey &other) const {
    if (dst_eid != other.dst_eid) {
      return dst_eid < other.dst_eid;
    } else if (plane != other.plane) {
      return plane < other.plane;
    } else {
      return placement < other.placement;
    }
  }

  bool Matches(const MatchKey &query) const {
    // 规则：
    // 1. 如果 dst_eid 和 query 的 dst_eid 非空，必须相等；
    //    如果 dst_eid 或 query 的 dst_eid 为空，忽略 dst_eid 匹配；
    // 2. plane 必须精确匹配；
    // 3. placement 必须精确匹配；
    if (!dst_eid.empty() && !query.dst_eid.empty() && (dst_eid != query.dst_eid)) {
      return false;
    }
    if (plane != query.plane) {
      return false;
    }
    if (placement != query.placement) {
      return false;
    }
    return true;
  }
};

struct TransferCompleteInfo {
  CommType type;
  void *complete_handle;
};

class HixlClient {
 public:
  /**
   * @brief HixlClient  构造函数
   * @param [in] server_ip  服务端监听 IPv4 地址
   * @param [in] server_port  服务端监听端口号
   */
  HixlClient(const std::string &server_ip, uint32_t server_port) : server_ip_(server_ip), server_port_(server_port) {};
  ~HixlClient() = default;

  /**
   * @brief 设置本端内存信息，在 BatchPut 和 BatchGet 之前需要调用
   * @param [in] mem_info_list 本端注册内存信息
   * @return 操作结果状态码
   */
  Status SetLocalMemInfo(const std::vector<MemInfo> &mem_info_list);

  /**
   * @brief client初始化
   * @param [in] local_endpoint_list 客户端本地 endpoint_list
   * @return 操作结果状态码
   */
  Status Initialize(const std::vector<EndpointConfig> &local_endpoint_list);

  /**
   * @brief 建链
   * @param [in] timeout_ms       超时时间（ms）
   * @return 操作结果状态码
   */
  Status Connect(uint32_t timeout_ms);

  /**
   * @brief 断链&销毁
   * @return 操作结果状态码
   */
  Status Finalize();

  /**
   * @brief 同步传输
   * @param [in] op_descs         批量操作的本地以及远端地址以及读取内存大小，批量操作的个数
   * @param [in] operation        读操作/写操作
   * @param [in] timeout_ms       超时时间
   * @return 操作结果状态码
   */
  Status TransferSync(const std::vector<TransferOpDesc> &op_descs, TransferOp operation, uint32_t timeout_ms);

  /**
   * @brief 异步传输
   * @param [in] op_descs         批量操作的本地以及远端地址以及写入内存大小，批量操作的个数
   * @param [in] operation        读操作/写操作
   * @param [out] req             请求的handle，用于查询请求状态
   * @return 操作结果状态码
   */
  Status TransferAsync(const std::vector<TransferOpDesc> &op_descs, TransferOp operation, TransferReq &req);

  /**
   * @brief 查询异步传输状态
   * @param [in] req             请求的handle，用于查询请求状态
   * @param [out] status         传输状态
   * @return 操作结果状态码
   */
  Status GetTransferStatus(const TransferReq &req, TransferStatus &status);

 private:
  Status Deserialize(const std::string &json_str, std::vector<EndpointConfig> &endpoint_list);

  Status ParseJsonField(const nlohmann::json &json_obj, const std::string &field_name, std::string &field_value);

  Status SendEndpointInfoReq(int32_t fd, CtrlMsgType msg_type);

  Status RecvEndpointInfoResp(int32_t fd, std::vector<EndpointConfig> &remote_endpoint_list);

  // 解析通信类型
  CommType ParseCommType(const std::string &local_placement, const std::string &remote_placement);

  bool MustUseRoce(const std::vector<EndpointConfig> &local_endpoint_list,
                   const std::vector<EndpointConfig> &remote_endpoint_list) const;

  Status TryMatchRoceEndpoints(const std::vector<EndpointConfig> &local_endpoint_list,
                               const std::vector<EndpointConfig> &remote_endpoint_list);

  Status TryMatchUbEndpoints(const EndpointConfig &local_endpoint,
                             const std::map<MatchKey, EndpointConfig> &peer_match_endpoints,
                             std::map<CommType, bool> &expected_pairs, uint32_t &count);

  void BuildEndpointsMatchMap(const std::vector<EndpointConfig> &endpoint_list,
                              std::map<MatchKey, EndpointConfig> &peer_match_endpoints) const;

  Status FindMatchedEndpoints(const std::vector<EndpointConfig> &local_endpoint_list,
                              const std::vector<EndpointConfig> &remote_endpoint_list);

  // 创建cs_client
  Status CreateCsClients(const EndpointConfig &local_endpoint_config, const EndpointConfig &remote_endpoint_config,
                         CommType type);

  Status GetMemType(const std::vector<SegmentPtr> &segments, uintptr_t addr, size_t len, MemType &mem_type);

  // 将 op_descs 根据 local_segments_ 和 remote_segments_ 的信息，按照 D2D，H2D，D2H，H2H 进行分类，结果保存在
  // op_descs_table
  Status ClassifyTransfers(const std::vector<TransferOpDesc> &op_descs,
                           std::map<CommType, std::vector<TransferOpDesc>> &op_descs_table);

  Status BatchTransfer(const std::vector<TransferOpDesc> &op_descs, TransferOp operation,
                       std::vector<TransferCompleteInfo> &complete_handle_list);

  Status ProcessRemoteMem(uint32_t timeout_ms);

  Status RegisterMemToCsClient(const MemDesc &mem, const MemType type);

  Status UnregisterMemToCsClient(CommType type, const std::vector<MemHandle> &mem_handles);

  std::string server_ip_;
  uint32_t server_port_;
  bool is_connected_{false};  // true为已建链；false未建链
  bool is_finalized_{false};
  std::map<CommType, HixlClientHandle> client_handles_;  // ub链路时会创建4个 cs_client，roce链路会创建1个 cs_client
  std::map<TransferReq, std::vector<TransferCompleteInfo>> complete_handles_;  // 保存异步传输的完成句柄
  std::map<CommType, std::vector<MemHandle>> client_mem_handles_;              // 每种类型 cs client 注册的内存句柄
  std::vector<SegmentPtr> local_segments_;  // 内存段数组，包含 MEM_DEVICE and MEM_HOST 两种 std::shared_ptr<Segment>
  std::vector<SegmentPtr> remote_segments_;

  std::mutex status_mutex_;            // 保护is_connected_和is_finalized_
  std::mutex client_handles_mutex_;    // 保护client_handles_
  std::mutex complete_handles_mutex_;  // 保护complete_handles_
  std::mutex mem_handles_mutex_;       // 保护client_mem_handles_
  std::mutex local_segments_mutex_;    // 保护local_segments_
  std::mutex remote_segments_mutex_;   // 保护remote_segments_
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_ENGINE_HIXL_CLIENT_H_