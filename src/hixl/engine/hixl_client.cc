/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hixl_client.h"
#include <cstring>
#include <algorithm>
#include <vector>
#include <utility>
#include <cstdlib>
#include <thread>
#include "securec.h"
#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/hixl_utils.h"
#include "common/ctrl_msg.h"
#include "common/ctrl_msg_plugin.h"
#include "common/scope_guard.h"
#include "common/thread_pool.h"

namespace hixl {
namespace {
constexpr uint64_t kMaxRecvRespBodySize = static_cast<uint64_t>(4ULL * 1024ULL * 1024ULL);  // 4MB 示例上限
constexpr uint32_t kCtrlMsgPluginTimeoutMs = 10000U;
constexpr uint32_t kTransferSyncSleepTimeMs = 1U;
constexpr uint32_t kMaxUbCsClientNum = 4U;
constexpr const char *kMemTypeDevice = "DEVICE";
constexpr const char *kMemTypetHost = "HOST";

const char *CommTypeToString(CommType type) {
  switch (type) {
    case CommType::COMM_TYPE_UB_D2D:
      return "UB_D2D";
    case CommType::COMM_TYPE_UB_H2D:
      return "UB_H2D";
    case CommType::COMM_TYPE_UB_D2H:
      return "UB_D2H";
    case CommType::COMM_TYPE_UB_H2H:
      return "UB_H2H";
    case CommType::COMM_TYPE_ROCE:
      return "ROCE";
    case CommType::COMM_TYPE_HCCS:
      return "HCCS";
    default:
      return "UNKNOWN";
  }
}

// 自定义查找函数
std::map<MatchKey, EndpointConfig>::const_iterator FindMatchingKey(const std::map<MatchKey, EndpointConfig> &map,
                                                                   const MatchKey &query_key) {
  for (auto it = map.begin(); it != map.end(); ++it) {
    if (it->first.Matches(query_key)) {
      return it;  // 找到第一个匹配的 key
    }
  }
  return map.end();  // 未找到
}
}  // namespace

Status HixlClient::Initialize(const std::vector<EndpointConfig> &local_endpoint_list) {
  if (local_endpoint_list.empty()) {
    HIXL_LOGE(PARAM_INVALID, "The input local_endpoint_list is empty");
    return PARAM_INVALID;
  }
  // 创建socket，与server建链，发送请求，获取remote_endpoint_list
  std::vector<EndpointConfig> remote_endpoint_list;
  CtrlMsgPlugin::Initialize();
  {
    int32_t socket = -1;
    HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Connect(server_ip_, server_port_, socket, kCtrlMsgPluginTimeoutMs),
                        "Connect socket failed");
    ScopeGuard socket_guard([&socket]() {
      if (socket != -1) {
        HIXL_LOGI("HixlClient close socket start, socket:%d", socket);
        close(socket);
        HIXL_LOGI("HixlClient close socket end, socket:%d", socket);
        socket = -1;
      }
    });
    HIXL_CHK_STATUS_RET(SendEndpointInfoReq(socket, CtrlMsgType::kGetEndpointInfoReq),
                        "HixlClient send GetEndpointInfoReq failed, socket:%d", socket);
    HIXL_CHK_STATUS_RET(RecvEndpointInfoResp(socket, remote_endpoint_list),
                        "HixlClient receive GetEndpointInfoResp failed, socket:%d", socket);
  }
  if (remote_endpoint_list.empty()) {
    HIXL_LOGE(FAILED, "HixlClient received empty remote_endpoint_list");
    return FAILED;
  }
  HIXL_CHK_STATUS_RET(FindMatchedEndpoints(local_endpoint_list, remote_endpoint_list),
                      "HixlClient FindMatchedEndpoints failed");
  return SUCCESS;
}

Status HixlClient::SendEndpointInfoReq(int32_t fd, CtrlMsgType msg_type) {
  CtrlMsgHeader header{};
  header.magic = kMagicNumber;
  header.body_size = static_cast<uint64_t>(sizeof(CtrlMsgType));
  HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Send(fd, &header, static_cast<uint64_t>(sizeof(header))),
                      "HixlClient send header failed, fd:%d", fd);
  HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Send(fd, &msg_type, static_cast<uint64_t>(sizeof(msg_type))),
                      "HixlClient send msg_type failed, fd:%d", fd);
  return SUCCESS;
}

Status HixlClient::RecvEndpointInfoResp(int32_t fd, std::vector<EndpointConfig> &remote_endpoint_list) {
  CtrlMsgHeader header{};
  HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Recv(fd, &header, static_cast<uint32_t>(sizeof(header)), kCtrlMsgPluginTimeoutMs),
                      "HixlClient receive header failed, fd:%d", fd);
  HIXL_CHK_BOOL_RET_STATUS(header.magic == kMagicNumber, PARAM_INVALID,
                           "Invalid magic for HixlClient RecvEndpointInfoResp, expect:0x%X, actual:0x%X", kMagicNumber,
                           header.magic);
  HIXL_CHK_BOOL_RET_STATUS(
      header.body_size > sizeof(CtrlMsgType) && header.body_size <= kMaxRecvRespBodySize, PARAM_INVALID,
      "Invalid body_size in HixlClient RecvEndpointInfoResp, body_size=%" PRIu64 ", must be in (%zu, %" PRIu64 "]",
      header.body_size, sizeof(CtrlMsgType), kMaxRecvRespBodySize);

  const uint64_t body_size = header.body_size;
  std::vector<uint8_t> body(body_size);
  HIXL_EVENT("[HixlClient] RecvEndpointInfoResp: receiving remote_endpoint_list body (%" PRIu64
             " bytes) from fd=%d begin",
             body_size, fd);
  HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Recv(fd, body.data(), static_cast<uint32_t>(body_size), kCtrlMsgPluginTimeoutMs));
  HIXL_EVENT("[HixlClient] RecvEndpointInfoResp: receiving remote_endpoint_list body (%" PRIu64
             " bytes) from fd=%d success",
             body_size, fd);

  CtrlMsgType msg_type{};
  const void *src = static_cast<const void *>(body.data());
  errno_t rc = memcpy_s(&msg_type, sizeof(msg_type), src, sizeof(msg_type));
  HIXL_CHK_BOOL_RET_STATUS(rc == EOK, FAILED, "memcpy_s msg_type failed, rc=%d", static_cast<int32_t>(rc));
  HIXL_CHK_BOOL_RET_STATUS(msg_type == CtrlMsgType::kGetEndpointInfoResp, PARAM_INVALID,
                           "Unexpected msg_type=%d in RecvEndpointInfoResp, expect=%d", static_cast<int32_t>(msg_type),
                           static_cast<int32_t>(CtrlMsgType::kGetEndpointInfoResp));

  const size_t json_len = static_cast<size_t>(body_size - sizeof(CtrlMsgType));
  std::string json_str(reinterpret_cast<const char *>(body.data() + sizeof(msg_type)), json_len);
  HIXL_CHK_STATUS_RET(Deserialize(json_str, remote_endpoint_list), "Failed to deserialize json_str, json_str:%s",
                      json_str.c_str());
  return SUCCESS;
}

Status HixlClient::Deserialize(const std::string &json_str, std::vector<EndpointConfig> &endpoint_list) {
  nlohmann::json j;
  try {
    j = nlohmann::json::parse(json_str);
  } catch (const nlohmann::json::exception &e) {
    HIXL_LOGE(PARAM_INVALID, "Failed to parse json, exception:%s", e.what());
    return PARAM_INVALID;
  }
  // 检查是否为数组
  if (!j.is_array()) {
    HIXL_LOGE(PARAM_INVALID, "Invalid json format, expect array");
    return PARAM_INVALID;
  }

  for (const auto &item : j) {
    EndpointConfig endpoint{};
    // 解析字段
    HIXL_CHK_STATUS_RET(ParseJsonField(item, "protocol", endpoint.protocol), "Failed to parse protocol");
    HIXL_CHK_STATUS_RET(ParseJsonField(item, "comm_id", endpoint.comm_id), "Failed to parse comm_id");
    HIXL_CHK_STATUS_RET(ParseJsonField(item, "net_instance_id", endpoint.net_instance_id),
                        "Failed to parse net_instance_id");
    HIXL_CHK_STATUS_RET(ParseJsonField(item, "placement", endpoint.placement), "Failed to parse placement");
    HIXL_CHK_STATUS_RET(ParseJsonField(item, "plane", endpoint.plane), "Failed to parse plane");
    HIXL_CHK_STATUS_RET(ParseJsonField(item, "dst_eid", endpoint.dst_eid), "Failed to parse dst_eid");

    endpoint_list.emplace_back(std::move(endpoint));
  }
  return SUCCESS;
}

Status HixlClient::ParseJsonField(const nlohmann::json &json_obj, const std::string &field_name,
                                  std::string &field_value) {
  if (!json_obj.contains(field_name)) {
    HIXL_LOGE(PARAM_INVALID, "Missing required field '%s' in EndpointConfig", field_name.c_str());
    return PARAM_INVALID;
  }

  try {
    field_value = json_obj[field_name].get<std::string>();
    return SUCCESS;
  } catch (const nlohmann::json::exception &e) {
    HIXL_LOGE(PARAM_INVALID, "Failed to parse field '%s', exception: %s", field_name.c_str(), e.what());
    return PARAM_INVALID;
  }
}

Status HixlClient::FindMatchedEndpoints(const std::vector<EndpointConfig> &local_endpoint_list,
                                        const std::vector<EndpointConfig> &remote_endpoint_list) {
  // 如果必须使用ROCE，直接匹配并创建ROCE链路
  if (MustUseRoce(local_endpoint_list, remote_endpoint_list)) {
    return TryMatchRoceEndpoints(local_endpoint_list, remote_endpoint_list);
  }
  // 匹配创建UB链路
  std::map<CommType, bool> expected_pairs = {{CommType::COMM_TYPE_UB_D2D, false},
                                             {CommType::COMM_TYPE_UB_H2D, false},
                                             {CommType::COMM_TYPE_UB_D2H, false},
                                             {CommType::COMM_TYPE_UB_H2H, false}};
  uint32_t count = 0;
  std::map<MatchKey, EndpointConfig> peer_match_endpoints;
  BuildEndpointsMatchMap(remote_endpoint_list, peer_match_endpoints);
  for (const auto &local_endpoint : local_endpoint_list) {
    HIXL_LOGI("local_endpoint:%s", local_endpoint.ToString().c_str());
    HIXL_CHK_STATUS_RET(TryMatchUbEndpoints(local_endpoint, peer_match_endpoints, expected_pairs, count),
                        "HixlClient TryMatchUbEndpoints failed");
    if (count == kMaxUbCsClientNum) {
      HIXL_LOGI("Created all %u expected UB CS clients", count);
      return SUCCESS;
    }
  }
  if (count > 0) {
    HIXL_LOGW("Found only %u/%u expected UB endpoint pairs", count, kMaxUbCsClientNum);
    return SUCCESS;
  }
  HIXL_LOGE(FAILED, "Failed to find matched UB endpoints");
  return FAILED;
}

// 是否必须使用ROCE
bool HixlClient::MustUseRoce(const std::vector<EndpointConfig> &local_endpoint_list,
                             const std::vector<EndpointConfig> &remote_endpoint_list) const {
  // 是否开启HCCL_INTRA_ROCE_ENABLE
  std::string env_roce_enable;
  const char *env_ret = std::getenv("HCCL_INTRA_ROCE_ENABLE");
  if (env_ret != nullptr) {
    env_roce_enable = env_ret;
  }
  const bool is_env_roce_enabled = (env_roce_enable == "1");
  // 是否在同一超节点
  const bool is_net_instance_different =
      local_endpoint_list[0].net_instance_id != remote_endpoint_list[0].net_instance_id;
  return is_env_roce_enabled || is_net_instance_different;
}

// 尝试匹配ROCE端点
Status HixlClient::TryMatchRoceEndpoints(const std::vector<EndpointConfig> &local_endpoint_list,
                                         const std::vector<EndpointConfig> &remote_endpoint_list) {
  auto local_it = std::find_if(local_endpoint_list.begin(), local_endpoint_list.end(),
                               [](const EndpointConfig &endpoint) { return endpoint.protocol == kProtocolRoce; });
  auto remote_it = std::find_if(remote_endpoint_list.begin(), remote_endpoint_list.end(),
                                [](const EndpointConfig &endpoint) { return endpoint.protocol == kProtocolRoce; });
  if (local_it != local_endpoint_list.end() && remote_it != remote_endpoint_list.end()) {
    return CreateCsClients(*local_it, *remote_it, CommType::COMM_TYPE_ROCE);
  } else {
    HIXL_LOGE(FAILED, "Failed to find matched ROCE endpoints");
    return FAILED;
  }
}

void HixlClient::BuildEndpointsMatchMap(const std::vector<EndpointConfig> &endpoint_list,
                                        std::map<MatchKey, EndpointConfig> &peer_match_endpoints) const {
  for (const auto &endpoint : endpoint_list) {
    if (endpoint.protocol == kProtocolUbCtp || endpoint.protocol == kProtocolUbTp) {
      MatchKey key = {endpoint.dst_eid, endpoint.plane, endpoint.placement};
      peer_match_endpoints[key] = endpoint;
    }
  }
}

Status HixlClient::TryMatchUbEndpoints(const EndpointConfig &local_endpoint,
                                       const std::map<MatchKey, EndpointConfig> &peer_match_endpoints,
                                       std::map<CommType, bool> &expected_pairs, uint32_t &count) {
  if (local_endpoint.protocol != kProtocolUbCtp && local_endpoint.protocol != kProtocolUbTp) {
    return SUCCESS;
  }
  for (const auto &placement : {kPlacementDevice, kPlacementHost}) {
    MatchKey key = {local_endpoint.comm_id, local_endpoint.plane, placement};
    HIXL_LOGI("TryMatchUbEndpoints: key:%s", key.ToString().c_str());
    auto it = FindMatchingKey(peer_match_endpoints, key);
    if (it != peer_match_endpoints.end()) {
      HIXL_LOGI("Found matched endpoint, remote_endpoint:%s", it->second.ToString().c_str());
      CommType type = ParseCommType(local_endpoint.placement, it->second.placement);
      if (!expected_pairs[type]) {
        HIXL_CHK_STATUS_RET(CreateCsClients(local_endpoint, it->second, type),
                            "HixlClient CreateCsClients failed for type %s", CommTypeToString(type));
        expected_pairs[type] = true;
        count++;
        HIXL_LOGI("HixlClient CreateCsClients success for type %s", CommTypeToString(type));
      }
    }
  }
  return SUCCESS;
}

// 解析通信类型
CommType HixlClient::ParseCommType(const std::string &local_placement, const std::string &remote_placement) {
  if (local_placement == kPlacementDevice && remote_placement == kPlacementDevice) {
    return CommType::COMM_TYPE_UB_D2D;
  } else if (local_placement == kPlacementDevice && remote_placement == kPlacementHost) {
    return CommType::COMM_TYPE_UB_D2H;
  } else if (local_placement == kPlacementHost && remote_placement == kPlacementHost) {
    return CommType::COMM_TYPE_UB_H2H;
  } else {
    return CommType::COMM_TYPE_UB_H2D;
  }
}

// 创建cs_client
Status HixlClient::CreateCsClients(const EndpointConfig &local_endpoint_config,
                                   const EndpointConfig &remote_endpoint_config, CommType type) {
  int32_t dev_logic_id = 0;
  int32_t dev_phy_id = 0;
  HIXL_CHK_ACL_RET(aclrtGetDevice(&dev_logic_id));
  HIXL_CHK_ACL_RET(aclrtGetPhyDevIdByLogicDevId(dev_logic_id, &dev_phy_id));
  EndpointDesc local_endpoint{};
  EndpointDesc remote_endpoint{};
  HIXL_CHK_STATUS_RET(ConvertToEndpointDesc(local_endpoint_config, local_endpoint, static_cast<uint32_t>(dev_phy_id)),
                      "HixlClient convert EndpointConfig to EndpointInfo failed, local_endpoint_config:%s",
                      local_endpoint_config.ToString().c_str());
  HIXL_LOGI("Local_endpoint dev_phy_id: %u", local_endpoint.loc.device.devPhyId);
  HIXL_CHK_STATUS_RET(ConvertToEndpointDesc(remote_endpoint_config, remote_endpoint),
                      "HixlClient convert EndpointConfig to EndpointInfo failed, remote_endpoint_config:%s",
                      remote_endpoint_config.ToString().c_str());
  HIXL_LOGI("Remote_endpoint dev_phy_id: %u", remote_endpoint.loc.device.devPhyId);
  HixlClientHandle handle = nullptr;
  HixlClientDesc desc{};
  desc.server_ip = server_ip_.c_str();
  desc.server_port = server_port_;
  desc.src_endpoint = &local_endpoint;
  desc.dst_endpoint = &remote_endpoint;
  const HixlClientConfig config{};
  HIXL_CHK_STATUS_RET(HixlCSClientCreate(&desc, &config, &handle), "HixlCSClientCreate failed for type %s",
                      CommTypeToString(type));
  HIXL_LOGI("HixlCSClientCreate success for type %s, handle:%p", CommTypeToString(type), handle);
  std::lock_guard<std::mutex> lock(client_handles_mutex_);
  client_handles_[type] = handle;
  return SUCCESS;
}

Status HixlClient::SetLocalMemInfo(const std::vector<MemInfo> &mem_info_list) {
  {
    std::lock_guard<std::mutex> lock(client_handles_mutex_);
    if (client_handles_.empty()) {
      HIXL_LOGE(FAILED, "HixlClient is not initialized");
      return FAILED;
    }
  }
  // 将内存保存在 local_segments_ 中
  for (const auto &mem_info : mem_info_list) {
    auto &mem = mem_info.mem;
    auto type = mem_info.type;
    {
      std::lock_guard<std::mutex> lock(local_segments_mutex_);
      auto seg_it = std::find_if(local_segments_.begin(), local_segments_.end(),
                                 [type](const SegmentPtr &seg) { return seg->GetMemType() == type; });
      if (seg_it != local_segments_.end()) {
        HIXL_CHK_STATUS_RET((*seg_it)->AddRange(mem.addr, mem.len),
                            "Failed to add range to local_segments_, addr: 0x%lx, size: %lu, type: %s", mem.addr,
                            mem.len, (type == MemType::MEM_DEVICE) ? kMemTypeDevice : kMemTypetHost);
      } else {
        auto new_segment = MakeShared<Segment>(type);
        HIXL_CHK_BOOL_RET_STATUS(new_segment != nullptr, FAILED, "Failed to create new segment for type:%s",
                                 (type == MemType::MEM_DEVICE) ? kMemTypeDevice : kMemTypetHost);
        HIXL_CHK_STATUS_RET(new_segment->AddRange(mem.addr, mem.len),
                            "Failed to add range to local_segments_, addr: 0x%lx, size: %lu, type: %s", mem.addr,
                            mem.len, (type == MemType::MEM_DEVICE) ? kMemTypeDevice : kMemTypetHost);
        local_segments_.push_back(new_segment);
      }
    }

    // 注册内存到对应的cs client
    HIXL_CHK_STATUS_RET(RegisterMemToCsClient(mem, type),
                        "Failed to register memory to cs client, addr: 0x%lx, size: %lu, type: %s", mem.addr, mem.len,
                        (type == MemType::MEM_DEVICE) ? kMemTypeDevice : kMemTypetHost);
  }
  return SUCCESS;
}

Status HixlClient::RegisterMemToCsClient(const MemDesc &mem, const MemType type) {
  HcommMem hccl_mem{};
  hccl_mem.type = (type == MemType::MEM_DEVICE) ? HCCL_MEM_TYPE_DEVICE : HCCL_MEM_TYPE_HOST;
  hccl_mem.addr = reinterpret_cast<void *>(mem.addr);
  hccl_mem.size = mem.len;

  // 需要注册的通信类型列表
  std::vector<CommType> comm_types_to_register;
  if (type == MemType::MEM_DEVICE) {
    comm_types_to_register.push_back(CommType::COMM_TYPE_UB_D2H);
    comm_types_to_register.push_back(CommType::COMM_TYPE_UB_D2D);
  } else {
    comm_types_to_register.push_back(CommType::COMM_TYPE_UB_H2D);
    comm_types_to_register.push_back(CommType::COMM_TYPE_UB_H2H);
  }
  comm_types_to_register.push_back(CommType::COMM_TYPE_ROCE);

  // 注册内存到对应的cs client
  std::lock_guard<std::mutex> lock(client_handles_mutex_);
  for (const auto &comm_type : comm_types_to_register) {
    auto handle_it = client_handles_.find(comm_type);
    if (handle_it == client_handles_.end()) {
      continue;
    }
    MemHandle mem_handle = nullptr;
    HIXL_CHK_STATUS_RET(HixlCSClientRegMem(handle_it->second, nullptr, &hccl_mem, &mem_handle),
                        "HixlClient register memory failed, client_handle: %p, addr: 0x%lx, size: %lu, type: %s",
                        handle_it->second, mem.addr, mem.len,
                        (type == MemType::MEM_DEVICE) ? kMemTypeDevice : kMemTypetHost);
    HIXL_LOGI("HixlClient register memory success, client_handle: %p, mem_handle: %p, addr: 0x%lx, size: %lu, type: %s",
              handle_it->second, mem_handle, mem.addr, mem.len,
              (type == MemType::MEM_DEVICE) ? kMemTypeDevice : kMemTypetHost);
    {
      std::lock_guard<std::mutex> lock(mem_handles_mutex_);
      client_mem_handles_[comm_type].push_back(mem_handle);
    }
  }
  return SUCCESS;
}

Status HixlClient::Connect(uint32_t timeout_ms) {
  std::lock_guard<std::mutex> client_handles_lock(client_handles_mutex_);
  if (client_handles_.empty()) {
    HIXL_LOGE(FAILED, "HixlClient is not initialized");
    return FAILED;
  }

  HIXL_LOGI("HixlClient connect start, timeout:%u", timeout_ms);
  ThreadPool thread_pool("hixl_client_connect", client_handles_.size());
  std::vector<std::future<Status>> connect_futures;
  aclrtContext context = nullptr;
  HIXL_CHK_ACL_RET(aclrtGetCurrentContext(&context));
  HIXL_LOGI("HixlClient aclrtGetCurrentContext, context: %p", context);
  for (const auto &pair : client_handles_) {
    auto type = pair.first;
    auto handle = pair.second;
    auto future = thread_pool.commit([handle, timeout_ms, type, context]() -> Status {
      HIXL_CHK_ACL_RET(aclrtSetCurrentContext(context));
      HIXL_LOGI("HixlClient aclrtSetCurrentContext, context: %p", context);
      try {
        HIXL_CHK_STATUS_RET(HixlCSClientConnect(handle, timeout_ms),
                            "HixlClient Connect failed for type:%s, client_handle: %p, timeout:%u",
                            CommTypeToString(type), handle, timeout_ms);
        return SUCCESS;
      } catch (const std::exception &e) {
        HIXL_LOGE(FAILED, "Exception in HixlCSClientConnectSync: %s", e.what());
        return FAILED;
      } catch (...) {
        HIXL_LOGE(FAILED, "Unknown exception in HixlCSClientConnectSync");
        return FAILED;
      }
    });
    connect_futures.emplace_back(std::move(future));
  }
  for (auto &future : connect_futures) {
    HIXL_CHK_STATUS_RET(future.get(), "HixlClient Connect failed, timeout:%u", timeout_ms);
  }
  HIXL_LOGI("HixlClient Connect success, timeout:%u", timeout_ms);

  HIXL_CHK_STATUS_RET(ProcessRemoteMem(timeout_ms), "HixlClient ProcessRemoteMem failed, timeout:%u", timeout_ms);
  std::lock_guard<std::mutex> status_lock(status_mutex_);
  is_connected_ = true;
  return SUCCESS;
}

Status HixlClient::ProcessRemoteMem(uint32_t timeout_ms) {
  for (const auto &pair : client_handles_) {
    auto handle = pair.second;
    HcommMem *remote_mem_list = nullptr;
    char **mem_tag_list = nullptr;
    uint32_t list_num = 0;
    HIXL_CHK_STATUS_RET(HixlCSClientGetRemoteMem(handle, &remote_mem_list, &mem_tag_list, &list_num, timeout_ms),
                        "HixlClient get remote memories failed, client_handle: %p, timeout:%u ms", handle, timeout_ms);
    std::lock_guard<std::mutex> seg_lock(remote_segments_mutex_);
    for (uint32_t i = 0; i < list_num; i++) {
      MemType type = (remote_mem_list[i].type == HCCL_MEM_TYPE_DEVICE) ? MEM_DEVICE : MEM_HOST;
      auto it = std::find_if(remote_segments_.begin(), remote_segments_.end(),
                             [type](const SegmentPtr &seg) { return seg->GetMemType() == type; });
      if (it != remote_segments_.end()) {
        HIXL_CHK_STATUS_RET(
            (*it)->AddRange(reinterpret_cast<uintptr_t>(remote_mem_list[i].addr), remote_mem_list[i].size),
            "Failed to add range to remote_segments_, addr: 0x%lx, size: %lu, type: %s",
            reinterpret_cast<uintptr_t>(remote_mem_list[i].addr), remote_mem_list[i].size,
            (type == MemType::MEM_DEVICE) ? kMemTypeDevice : kMemTypetHost);
      } else {
        auto new_segment = MakeShared<Segment>(type);
        HIXL_CHK_BOOL_RET_STATUS(new_segment != nullptr, FAILED, "Failed to create new segment for type:%s",
                                 (type == MemType::MEM_DEVICE) ? kMemTypeDevice : kMemTypetHost);
        HIXL_CHK_STATUS_RET(
            new_segment->AddRange(reinterpret_cast<uintptr_t>(remote_mem_list[i].addr), remote_mem_list[i].size),
            "Failed to add range to remote_segments_, addr: 0x%lx, size: %lu, type: %s",
            reinterpret_cast<uintptr_t>(remote_mem_list[i].addr), remote_mem_list[i].size,
            (type == MemType::MEM_DEVICE) ? kMemTypeDevice : kMemTypetHost);
        remote_segments_.push_back(new_segment);
      }
    }
  }
  return SUCCESS;
}

Status HixlClient::Finalize() {
  HIXL_LOGI("HixlClient Finalize Start");
  Status ret = SUCCESS;
  {
    std::lock_guard<std::mutex> lock(status_mutex_);
    is_finalized_ = true;
  }

  // 释放内存
  {
    std::lock_guard<std::mutex> lock(mem_handles_mutex_);
    for (const auto &pair : client_mem_handles_) {
      auto &type = pair.first;
      auto &mem_handles = pair.second;
      ret = UnregisterMemToCsClient(type, mem_handles);
    }
    client_mem_handles_.clear();
  }

  // 销毁所有 cs client
  {
    std::lock_guard<std::mutex> lock(client_handles_mutex_);
    for (const auto &pair : client_handles_) {
      auto &type = pair.first;
      auto handle = pair.second;
      if (handle != nullptr) {
        auto status = HixlCSClientDestroy(handle);
        if (status != SUCCESS) {
          HIXL_LOGE(FAILED, "HixlClient Destroy Cs Client failed for type %s", CommTypeToString(type));
          ret = status;
        }
      }
    }
    client_handles_.clear();
  }

  // 清理其他共享资源
  {
    std::lock_guard<std::mutex> lock(local_segments_mutex_);
    local_segments_.clear();
  }
  {
    std::lock_guard<std::mutex> lock(remote_segments_mutex_);
    remote_segments_.clear();
  }
  {
    std::lock_guard<std::mutex> lock(complete_handles_mutex_);
    complete_handles_.clear();
  }
  {
    std::lock_guard<std::mutex> lock(status_mutex_);
    is_connected_ = false;
  }
  HIXL_LOGI("HixlClient Finalize End, status: %d", static_cast<int32_t>(ret));
  return ret;
}

Status HixlClient::UnregisterMemToCsClient(CommType type, const std::vector<MemHandle> &mem_handles) {
  std::lock_guard<std::mutex> lock(client_handles_mutex_);
  auto handle_it = client_handles_.find(type);
  if (handle_it == client_handles_.end()) {
    HIXL_LOGE(FAILED, "No cs client handle found for type %s, skip mem unregistration", CommTypeToString(type));
    return FAILED;
  }
  auto handle = handle_it->second;
  if (handle == nullptr) {
    HIXL_LOGE(FAILED, "Client handle is nullptr for type %s, skip mem unregistration", CommTypeToString(type));
    return FAILED;
  }
  Status ret = SUCCESS;
  for (auto &mem_handle : mem_handles) {
    if (mem_handle != nullptr) {
      auto status = HixlCSClientUnregMem(handle_it->second, mem_handle);
      if (status != SUCCESS) {
        HIXL_LOGE(status, "HixlClient UnregMem failed for type %s", CommTypeToString(type));
        ret = status;
      }
    }
  }
  return ret;
}

Status HixlClient::GetMemType(const std::vector<SegmentPtr> &segments, uintptr_t addr, size_t len, MemType &mem_type) {
  for (const auto &segment : segments) {
    if (segment->Contains(addr, addr + len)) {
      mem_type = segment->GetMemType();
      return SUCCESS;
    }
  }
  return PARAM_INVALID;
}

Status HixlClient::ClassifyTransfers(const std::vector<TransferOpDesc> &op_descs,
                                     std::map<CommType, std::vector<TransferOpDesc>> &op_descs_table) {
  for (const auto &op_desc : op_descs) {
    // 判断内存类型
    MemType local_mem_type;
    {
      std::lock_guard<std::mutex> lock(local_segments_mutex_);
      if (GetMemType(local_segments_, op_desc.local_addr, op_desc.len, local_mem_type) != SUCCESS) {
        HIXL_LOGE(PARAM_INVALID, "Local memory range does not register, start:%lu, end:%lu", op_desc.local_addr,
                  op_desc.local_addr + op_desc.len);
        return PARAM_INVALID;
      }
    }
    MemType remote_mem_type;
    {
      std::lock_guard<std::mutex> lock(remote_segments_mutex_);
      if (GetMemType(remote_segments_, op_desc.remote_addr, op_desc.len, remote_mem_type) != SUCCESS) {
        HIXL_LOGE(PARAM_INVALID, "Remote memory range does not register, start:%lu, end:%lu", op_desc.remote_addr,
                  op_desc.remote_addr + op_desc.len);
        return PARAM_INVALID;
      }
    }

    // 如果是roce，直接将op_desc保存在op_descs_table中
    {
      std::lock_guard<std::mutex> lock(client_handles_mutex_);
      if (client_handles_.find(CommType::COMM_TYPE_ROCE) != client_handles_.end()) {
        op_descs_table[CommType::COMM_TYPE_ROCE].push_back(op_desc);
        HIXL_LOGI("Current communication type: %s.", CommTypeToString(CommType::COMM_TYPE_ROCE));
        continue;
      }
    }

    // 判断通信类型，将op_desc保存在op_descs_table中
    CommType cur_type;
    if (local_mem_type == MEM_DEVICE) {
      cur_type = (remote_mem_type == MEM_DEVICE) ? CommType::COMM_TYPE_UB_D2D : CommType::COMM_TYPE_UB_D2H;
    } else {
      cur_type = (remote_mem_type == MEM_DEVICE) ? CommType::COMM_TYPE_UB_H2D : CommType::COMM_TYPE_UB_H2H;
    }
    op_descs_table[cur_type].push_back(op_desc);
    HIXL_LOGI("Current communication type: %s, local memory type: %s, remote memory type: %s.",
              CommTypeToString(cur_type), (local_mem_type == MemType::MEM_DEVICE) ? kMemTypeDevice : kMemTypetHost,
              (remote_mem_type == MemType::MEM_DEVICE) ? kMemTypeDevice : kMemTypetHost);
  }
  return SUCCESS;
}

Status HixlClient::BatchTransfer(const std::vector<TransferOpDesc> &op_descs, TransferOp operation,
                                 std::vector<TransferCompleteInfo> &complete_handle_list) {
  {
    std::lock_guard<std::mutex> lock(status_mutex_);
    if (!is_connected_) {
      HIXL_LOGE(NOT_CONNECTED, "HixlClient is not connected");
      return NOT_CONNECTED;
    }
  }
  // 根据传输类型分类
  std::map<CommType, std::vector<TransferOpDesc>> op_descs_table;
  HIXL_CHK_STATUS_RET(ClassifyTransfers(op_descs, op_descs_table), "HixlClient failed to classify transfer op_descs");

  // 执行批量传输操作
  std::lock_guard<std::mutex> lock(client_handles_mutex_);
  for (const auto &type_with_op_descs : op_descs_table) {
    auto type = type_with_op_descs.first;
    const auto &op_descs_vec = type_with_op_descs.second;  // 避免变量名冲突，改个名
    HIXL_LOGI("HixlClient BatchTransfer start, type:%s, op_descs size:%zu", CommTypeToString(type),
              op_descs_vec.size());
    HixlClientHandle handle = nullptr;
    auto it = client_handles_.find(type);
    if (it == client_handles_.end()) {
      HIXL_LOGE(FAILED, "HixlClient not found client handle for type:%s", CommTypeToString(type));
      return FAILED;
    } else {
      handle = it->second;
    }
    uint32_t list_num = op_descs_vec.size();
    std::vector<HixlOneSideOpDesc> hixl_descs(list_num);
    for (size_t i = 0; i < list_num; i++) {
      hixl_descs[i].remote_buf = reinterpret_cast<void *>(op_descs_vec[i].remote_addr);
      hixl_descs[i].local_buf = reinterpret_cast<void *>(op_descs_vec[i].local_addr);
      hixl_descs[i].len = op_descs_vec[i].len;
    }
    CompleteHandle complete_handle = nullptr;
    if (operation == WRITE) {
      HIXL_CHK_STATUS_RET(HixlCSClientBatchPutAsync(handle, list_num, hixl_descs.data(), &complete_handle),
                          "HixlClient BatchPutAsync failed, client_handle: %p", handle);
    } else {
      HIXL_CHK_STATUS_RET(HixlCSClientBatchGetAsync(handle, list_num, hixl_descs.data(), &complete_handle),
                          "HixlClient BatchGetAsync failed, client_handle: %p", handle);
    }
    TransferCompleteInfo complete_info{type, complete_handle};
    complete_handle_list.push_back(complete_info);
  }
  return SUCCESS;
}

Status HixlClient::TransferAsync(const std::vector<TransferOpDesc> &op_descs, TransferOp operation, TransferReq &req) {
  if (op_descs.empty()) {
    HIXL_LOGE(PARAM_INVALID, "HixlClient TransferAsync failed, op_descs is empty");
    return PARAM_INVALID;
  }
  // 启动传输
  std::vector<TransferCompleteInfo> complete_handle_list;
  HIXL_CHK_STATUS_RET(BatchTransfer(op_descs, operation, complete_handle_list), "HixlClient TransferAsync failed");
  req = complete_handle_list[0].complete_handle;
  std::lock_guard<std::mutex> lock(complete_handles_mutex_);
  complete_handles_[req] = complete_handle_list;
  return SUCCESS;
}

Status HixlClient::GetTransferStatus(const TransferReq &req, TransferStatus &status) {
  std::lock_guard<std::mutex> lock(complete_handles_mutex_);
  // 检查complete_handles_是否为空
  if (complete_handles_.empty()) {
    HIXL_LOGE(FAILED, "HixlClient GetTransferStatus failed, no transfer tasks in progress");
    status = TransferStatus::FAILED;
    return FAILED;
  }
  // 通过req查找对应批次complete_handle_list
  auto it = complete_handles_.find(req);
  if (it == complete_handles_.end()) {
    HIXL_LOGE(PARAM_INVALID, "HixlClient GetTransferStatus failed, invalid req");
    status = TransferStatus::FAILED;
    return PARAM_INVALID;
  }
  std::vector<TransferCompleteInfo> complete_handle_list = it->second;

  // 查询状态
  bool all_complete = true;
  for (const auto &type_with_complete_handle : complete_handle_list) {
    auto type = type_with_complete_handle.type;
    auto complete_handle = type_with_complete_handle.complete_handle;
    HixlCompleteStatus query_status = HixlCompleteStatus::HIXL_COMPLETE_STATUS_WAITING;
    Status ret = SUCCESS;
    {
      std::lock_guard<std::mutex> client_lock(client_handles_mutex_);
      auto res = HixlCSClientQueryCompleteStatus(client_handles_[type], complete_handle, &query_status);
      ret = static_cast<Status>(res);
    }
    if (ret != SUCCESS) {
      HIXL_LOGE(ret, "HixlClient QueryCompleteStatus failed");
      status = TransferStatus::FAILED;
      complete_handles_.erase(req);
      return ret;
    }
    if (query_status == HixlCompleteStatus::HIXL_COMPLETE_STATUS_COMPLETED) {
      continue;
    } else if (query_status == HixlCompleteStatus::HIXL_COMPLETE_STATUS_WAITING) {
      all_complete = false;
    }
  }
  if (all_complete) {
    HIXL_LOGI("BatchTransfer is completed");
    status = TransferStatus::COMPLETED;
    complete_handles_.erase(req);
    return SUCCESS;
  } else {
    HIXL_LOGI("BatchTransfer is running");
    status = TransferStatus::WAITING;
    return SUCCESS;
  }
}

Status HixlClient::TransferSync(const std::vector<TransferOpDesc> &op_descs, TransferOp operation,
                                uint32_t timeout_ms) {
  if (op_descs.empty()) {
    HIXL_LOGE(PARAM_INVALID, "HixlClient TransferSync failed, op_descs is empty");
    return PARAM_INVALID;
  }
  const auto start = std::chrono::steady_clock::now();

  // 启动传输
  std::vector<TransferCompleteInfo> complete_handle_list;
  HIXL_CHK_STATUS_RET(BatchTransfer(op_descs, operation, complete_handle_list), "HixlClient TransferSync failed");

  // 在超时时间内等待传输完成
  while (true) {
    // 检查是否已被Finalize
    {
      std::lock_guard<std::mutex> lock(status_mutex_);
      if (is_finalized_) {
        HIXL_LOGE(FAILED, "HixlClient TransferSync terminated by Finalize()");
        return FAILED;
      }
    }
    const auto end = std::chrono::steady_clock::now();
    auto cost = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    HIXL_CHK_BOOL_RET_STATUS(cost < timeout_ms, TIMEOUT, "HixlClient TransferSync timeout: %u ms", timeout_ms);

    // 保存未完成的handle，已完成的将被移除
    std::vector<TransferCompleteInfo> remaining_handles;
    bool all_complete = true;
    for (const auto &type_with_complete_handle : complete_handle_list) {
      auto type = type_with_complete_handle.type;
      auto complete_handle = type_with_complete_handle.complete_handle;
      HixlCompleteStatus query_status = HixlCompleteStatus::HIXL_COMPLETE_STATUS_WAITING;
      {
        std::lock_guard<std::mutex> lock(client_handles_mutex_);
        HIXL_CHK_STATUS_RET(HixlCSClientQueryCompleteStatus(client_handles_[type], complete_handle, &query_status),
                            "HixlClient QueryCompleteStatus failed, client_handle: %p, complete_handle: %p",
                            client_handles_[type], complete_handle);
      }
      if (query_status == HixlCompleteStatus::HIXL_COMPLETE_STATUS_WAITING) {
        // 传输未完成，保存到剩余列表
        remaining_handles.push_back(type_with_complete_handle);
        all_complete = false;
      }
    }
    if (all_complete) {
      return SUCCESS;
    }
    // 更新handle列表，只保留未完成的
    complete_handle_list = std::move(remaining_handles);
    std::this_thread::sleep_for(std::chrono::milliseconds(kTransferSyncSleepTimeMs));
  }
}

}  // namespace hixl