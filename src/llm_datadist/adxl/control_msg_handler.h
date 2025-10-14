/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_ADXL_CONTROL_MSG_HANDLER_H_
#define CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_ADXL_CONTROL_MSG_HANDLER_H_

#include "nlohmann/json.hpp"
#include "runtime/rt.h"
#include "adxl/adxl_types.h"
#include "adxl_checker.h"
#include "common/llm_log.h"

namespace adxl {
const uint32_t kMagicNumber = 0xA1B2C3D4;
struct ProtocolHeader {
  uint32_t magic;
  uint64_t body_size;
};

enum class ControlMsgType : int32_t { kHeartBeat = 1, kBufferReq = 2, kBufferResp = 3, kEnd };

struct HeartbeatMsg {
  char msg;
};

enum class TransferType : int32_t {
  kWriteH2RH = 1,
  kReadRH2H = 2,
  kWriteH2RD = 3,
  kReadRH2D = 4,
  kWriteD2RH = 5,
  kReadRD2H = 6,
  kWriteD2RD = 7,
  kReadRD2D = 8,
  kEnd
};

struct BufferReq {
  TransferType transfer_type{TransferType::kEnd};
  uint64_t req_id{0};
  uint64_t timeout{0};
  std::vector<uintptr_t> src_addrs{};
  uintptr_t buffer_addr{};
  std::vector<uintptr_t> dst_addrs{};
  std::vector<size_t> buffer_lens{};
  uint64_t total_buffer_len{0};
  // just use for local process
  uintptr_t local_buffer_addr{0};
  std::chrono::steady_clock::time_point recv_start_time{};
};

struct BufferResp {
  TransferType transfer_type{TransferType::kEnd};
  uint64_t req_id{0};
  uint64_t timeout{0};
  std::vector<uintptr_t> src_addrs{};
  uintptr_t buffer_addr{0};
  std::vector<size_t> buffer_lens{};
};

inline void from_json(const nlohmann::json &j, BufferReq &req) {
  req.transfer_type = static_cast<TransferType>(j.at("transfer_type").get<int32_t>());
  j.at("req_id").get_to(req.req_id);
  j.at("timeout").get_to(req.timeout);
  j.at("src_addrs").get_to(req.src_addrs);
  j.at("buffer_addr").get_to(req.buffer_addr);
  j.at("dst_addrs").get_to(req.dst_addrs);
  j.at("buffer_lens").get_to(req.buffer_lens);
  j.at("total_buffer_len").get_to(req.total_buffer_len);
}

inline void to_json(nlohmann::json &j, const BufferReq &req) {
  j = nlohmann::json{{"transfer_type", static_cast<int32_t>(req.transfer_type)},
                     {"req_id", req.req_id},
                     {"timeout", req.timeout},
                     {"src_addrs", req.src_addrs},
                     {"buffer_addr", req.buffer_addr},
                     {"dst_addrs", req.dst_addrs},
                     {"buffer_lens", req.buffer_lens},
                     {"total_buffer_len", req.total_buffer_len}};
}

inline void to_json(nlohmann::json &j, const BufferResp &resp) {
  j = nlohmann::json{
      {"transfer_type", static_cast<int32_t>(resp.transfer_type)},
      {"req_id", resp.req_id},
      {"timeout", resp.timeout},
      {"src_addrs", resp.src_addrs},
      {"buffer_addr", resp.buffer_addr},
      {"buffer_lens", resp.buffer_lens},
  };
}

inline void from_json(const nlohmann::json &j, BufferResp &resp) {
  resp.transfer_type = static_cast<TransferType>(j.at("transfer_type").get<int32_t>());
  j.at("req_id").get_to(resp.req_id);
  j.at("timeout").get_to(resp.timeout);
  j.at("src_addrs").get_to(resp.src_addrs);
  j.at("buffer_addr").get_to(resp.buffer_addr);
  j.at("buffer_lens").get_to(resp.buffer_lens);
}

inline void to_json(nlohmann::json &j, const HeartbeatMsg &msg) {
  j = nlohmann::json{{"msg", msg.msg}};
}

inline void from_json(const nlohmann::json &j, HeartbeatMsg &msg) {
  j.at("msg").get_to(msg.msg);
}

class ControlMsgHandler {
 public:
  template <typename T>
  static Status Deserialize(const char *msg_str, T &msg) {
    try {
      auto j = nlohmann::json::parse(msg_str);
      msg = j.get<T>();
    } catch (const nlohmann::json::exception &e) {
      LLMLOGE(PARAM_INVALID, "Failed to load msg, exception:%s", e.what());
      return PARAM_INVALID;
    }
    return SUCCESS;
  }

  template <typename T>
  static Status SendMsg(int32_t fd, ControlMsgType msg_type, const T &msg, uint64_t timeout) {
    std::string msg_str;
    ADXL_CHK_STATUS_RET(Serialize(msg, msg_str), "Failed to serialize msg");
    ADXL_CHK_LLM_RET(SendMsgByProtocol(fd, msg_type, msg_str, timeout), "Failed to send msg");
    return SUCCESS;
  }

  template <typename T>
  static Status Serialize(const T &msg, std::string &msg_str) {
    try {
      nlohmann::json j = msg;
      msg_str = j.dump();
    } catch (const nlohmann::json::exception &e) {
      LLMLOGE(PARAM_INVALID, "Failed to dump msg to str, exception:%s", e.what());
      return PARAM_INVALID;
    }
    return SUCCESS;
  }

 private:
  static Status SendMsgByProtocol(int32_t fd, ControlMsgType msg_type, const std::string &msg_str, uint64_t timeout);
  static Status Write(int32_t fd, const void *buf, size_t len, uint64_t timeout,
                      std::chrono::steady_clock::time_point &start);
};
}  // namespace adxl

#endif  // CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_ADXL_CONTROL_MSG_HANDLER_H_
