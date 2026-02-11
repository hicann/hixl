/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "mem_msg_handler.h"

#include <cstring>
#include <cinttypes>
#include <algorithm>
#include "nlohmann/json.hpp"
#include "common/ctrl_msg_plugin.h"

#include <securec.h>

namespace {
constexpr uint64_t kMaxGetRemoteMemBodySize = static_cast<uint64_t>(4ULL * 1024ULL * 1024ULL);  // 4MB 示例上限
constexpr uint32_t kMaxGetRemoteMemNum = 4096U;                                                 // 最多 4096 段远端内存

hixl::Status ExtractTypeAndJsonPtr(const std::vector<uint8_t> &body, hixl::CtrlMsgType &msg_type, const char *&json_ptr,
                                   size_t &json_len) {
  const uint64_t body_size = static_cast<uint64_t>(body.size());
  const void *src = static_cast<const void *>(body.data());

  errno_t rc = memcpy_s(&msg_type, sizeof(msg_type), src, sizeof(msg_type));
  HIXL_CHK_BOOL_RET_STATUS(rc == EOK, hixl::FAILED, "[HixlClient] memcpy_s msg_type failed, rc=%d",
                           static_cast<int32_t>(rc));

  HIXL_LOGD("[HixlClient] Parsed MsgType: %d", static_cast<int32_t>(msg_type));
  HIXL_CHK_BOOL_RET_STATUS(msg_type == hixl::CtrlMsgType::kGetRemoteMemResp, hixl::PARAM_INVALID,
                           "[HixlClient] Unexpected msg_type=%d, expect=%d", static_cast<int32_t>(msg_type),
                           static_cast<int32_t>(hixl::CtrlMsgType::kGetRemoteMemResp));

  json_len = static_cast<size_t>(body_size - sizeof(hixl::CtrlMsgType));
  json_ptr = reinterpret_cast<const char *>(body.data() + sizeof(hixl::CtrlMsgType));

  HIXL_LOGD("[HixlClient] Extracted JSON length: %zu", json_len);
  return hixl::SUCCESS;
}

void FreeExportDesc(std::vector<hixl::HixlMemDesc> &descs) {
  for (auto &d : descs) {
    if (d.export_desc != nullptr) {
      std::free(d.export_desc);
      d.export_desc = nullptr;
      d.export_len = 0U;
    }
  }
  descs.clear();
}

hixl::Status RecvAndCheckHeader(int32_t socket, uint64_t &body_size, uint32_t timeout_ms) {
  HIXL_LOGI("[HixlClient] RecvAndCheckHeader start. socket: %d", socket);
  hixl::CtrlMsgHeader header{};
  HIXL_CHK_STATUS_RET(hixl::CtrlMsgPlugin::Recv(socket, &header, static_cast<uint32_t>(sizeof(header)), timeout_ms));
  HIXL_EVENT("[HixlClient] RecvGetRemoteMemResp header ok. fd=%d, body_size=%" PRIu64, socket, header.body_size);
  HIXL_LOGD("[HixlClient] Header received. magic: 0x%X, body_size: %lu", header.magic, header.body_size);
  HIXL_CHK_BOOL_RET_STATUS(header.magic == hixl::kMagicNumber, hixl::PARAM_INVALID,
                           "[HixlClient] Invalid magic in GetRemoteMemResp, expect:0x%X, actual:0x%X",
                           hixl::kMagicNumber, header.magic);

  HIXL_CHK_BOOL_RET_STATUS(
      header.body_size > sizeof(hixl::CtrlMsgType) && header.body_size <= kMaxGetRemoteMemBodySize, hixl::PARAM_INVALID,
      "[HixlClient] Invalid body_size in GetRemoteMemResp, body_size=%" PRIu64 ", must be in (%zu, %" PRIu64 "]",
      header.body_size, sizeof(hixl::CtrlMsgType), kMaxGetRemoteMemBodySize);

  body_size = header.body_size;
  return hixl::SUCCESS;
}

hixl::Status RecvBody(int32_t socket, uint64_t body_size, std::vector<uint8_t> &body, uint32_t timeout_ms) {
  HIXL_LOGI("[HixlClient] RecvBody start. body_size: %lu", body_size);
  body.resize(static_cast<size_t>(body_size));
  HIXL_EVENT("[HixlClient] RecvGetRemoteMemResp body begin. fd=%d, body_size=%" PRIu64, socket, body_size);
  HIXL_CHK_STATUS_RET(hixl::CtrlMsgPlugin::Recv(socket, body.data(), static_cast<uint32_t>(body_size), timeout_ms));
  HIXL_EVENT("[HixlClient] RecvGetRemoteMemResp body ok. fd=%d, body_size=%" PRIu64, socket, body_size);
  return hixl::SUCCESS;
}

hixl::Status ParseResultAndGetArray(const nlohmann::json &j, const nlohmann::json *&arr_out) {
  try {
    if (!j.contains("result")) {
      HIXL_LOGE(hixl::PARAM_INVALID, "[HixlClient] GetRemoteMemResp json has no field 'result'");
      return hixl::PARAM_INVALID;
    }

    hixl::Status result = static_cast<hixl::Status>(j["result"].get<uint32_t>());
    if (result != hixl::SUCCESS) {
      HIXL_LOGE(result, "[HixlClient] GetRemoteMemResp result not SUCCESS, result=%u", static_cast<uint32_t>(result));
      return result;
    }

    if (!j.contains("mem_descs") || !j["mem_descs"].is_array()) {
      HIXL_LOGE(hixl::PARAM_INVALID, "[HixlClient] GetRemoteMemResp json 'mem_descs' is missing or not array");
      return hixl::PARAM_INVALID;
    }

    arr_out = &j["mem_descs"];
    return hixl::SUCCESS;
  } catch (const nlohmann::json::exception &e) {
    HIXL_LOGE(hixl::PARAM_INVALID, "[HixlClient] JSON error in ParseResultAndGetArray: %s", e.what());
    return hixl::PARAM_INVALID;
  } catch (...) {
    HIXL_LOGE(hixl::FAILED, "[HixlClient] Unknown error in ParseResultAndGetArray");
    return hixl::FAILED;
  }
}

hixl::Status ParseMemObject(const nlohmann::json &j_mem, HcommMem &mem) {
  try {
    if (!j_mem.contains("type") || !j_mem.contains("addr") || !j_mem.contains("size")) {
      HIXL_LOGE(hixl::PARAM_INVALID, "[HixlClient] GetRemoteMemResp.mem missing 'type' / 'addr' / 'size'");
      return hixl::PARAM_INVALID;
    }

    mem.type = static_cast<HcclMemType>(j_mem["type"].get<uint32_t>());
    const uint64_t addr_u64 = j_mem["addr"].get<uint64_t>();
    mem.addr = reinterpret_cast<void *>(static_cast<uintptr_t>(addr_u64));
    mem.size = j_mem["size"].get<uint64_t>();
    return hixl::SUCCESS;
  } catch (const nlohmann::json::exception &e) {
    HIXL_LOGE(hixl::PARAM_INVALID, "[HixlClient] JSON error in ParseMemObject: %s", e.what());
    return hixl::PARAM_INVALID;
  } catch (...) {
    HIXL_LOGE(hixl::FAILED, "[HixlClient] Unknown error in ParseMemObject");
    return hixl::FAILED;
  }
}

hixl::Status FillExportDescFromJsonField(const nlohmann::json &j_export, hixl::HixlMemDesc &desc) {
  if (desc.export_desc != nullptr) {
    std::free(desc.export_desc);
    desc.export_desc = nullptr;
  }
  desc.export_len = 0U;
  if (!j_export.is_array()) {
    HIXL_LOGE(hixl::PARAM_INVALID, "[HixlClient] export_desc must be array");
    return hixl::PARAM_INVALID;
  }
  const size_t n = j_export.size();
  if (n == 0U) {
    return hixl::SUCCESS;
  }
  void *buf = std::malloc(n);
  if (buf == nullptr) {
    HIXL_LOGE(hixl::FAILED, "[HixlClient] malloc export_desc buffer failed, len=%zu", n);
    return hixl::FAILED;
  }
  uint8_t *dst = static_cast<uint8_t *>(buf);
  for (size_t i = 0; i < n; ++i) {
    if (!j_export[i].is_number_integer()) {
      HIXL_LOGE(hixl::PARAM_INVALID, "[HixlClient] export_desc[%zu] is not integer", i);
      std::free(buf);
      return hixl::PARAM_INVALID;
    }
    const int v = j_export[i].get<int>();
    if (v < 0 || v > 255) {
      HIXL_LOGE(hixl::PARAM_INVALID, "[HixlClient] export_desc[%zu]=%d out of range [0,255]", i, v);
      std::free(buf);
      return hixl::PARAM_INVALID;
    }
    dst[i] = static_cast<uint8_t>(v);
  }
  desc.export_desc = buf;
  desc.export_len = static_cast<uint32_t>(n);
  return hixl::SUCCESS;
}

hixl::Status ParseOneMemDesc(const nlohmann::json &item, uint32_t idx, hixl::HixlMemDesc &out) {
  try {
    if (!item.contains("tag") || !item.contains("export_desc") || !item.contains("mem") || !item["mem"].is_object()) {
      HIXL_LOGE(hixl::PARAM_INVALID,
                "[HixlClient] GetRemoteMemResp.mem_descs[%u] missing 'tag' / 'export_desc' or 'mem'", idx);
      return hixl::PARAM_INVALID;
    }
    HcommMem mem{};
    hixl::Status ret = ParseMemObject(item["mem"], mem);
    HIXL_CHK_STATUS_RET(ret);
    out.mem = mem;
    out.tag = item["tag"].get<std::string>();
    return FillExportDescFromJsonField(item["export_desc"], out);
  } catch (const nlohmann::json::exception &e) {
    HIXL_LOGE(hixl::PARAM_INVALID, "[HixlClient] JSON error in ParseOneMemDesc[%u]: %s", idx, e.what());
    return hixl::PARAM_INVALID;
  } catch (...) {
    HIXL_LOGE(hixl::FAILED, "[HixlClient] Unknown error in ParseOneMemDesc[%u]", idx);
    return hixl::FAILED;
  }
}

hixl::Status ParseMemDescsArray(const nlohmann::json &arr, std::vector<hixl::HixlMemDesc> &mem_descs) {
  const uint32_t mem_num = static_cast<uint32_t>(arr.size());
  HIXL_LOGD("[HixlClient] Parsing mem_descs array, size: %u", mem_num);
  HIXL_CHK_BOOL_RET_STATUS(mem_num <= kMaxGetRemoteMemNum, hixl::PARAM_INVALID,
                           "[HixlClient] mem_num too large in GetRemoteMemResp, mem_num=%u, max=%u", mem_num,
                           kMaxGetRemoteMemNum);

  mem_descs.clear();
  mem_descs.reserve(mem_num);

  for (uint32_t i = 0; i < mem_num; ++i) {
    hixl::HixlMemDesc desc{};
    hixl::Status ret = ParseOneMemDesc(arr[i], i, desc);
    if (ret != hixl::SUCCESS) {
      FreeExportDesc(mem_descs);
      return ret;
    }
    mem_descs.emplace_back(std::move(desc));
  }
  return hixl::SUCCESS;
}

hixl::Status ParseGetRemoteMemJson(const char *json_ptr, size_t json_len, std::vector<hixl::HixlMemDesc> &mem_descs) {
  HIXL_LOGD("[HixlClient] Parsing JSON from memory address %p, len %zu", json_ptr, json_len);
  nlohmann::json j;
  try {
    j = nlohmann::json::parse(json_ptr, json_ptr + json_len);
  } catch (const nlohmann::json::exception &e) {
    HIXL_LOGE(hixl::PARAM_INVALID, "[HixlClient] Failed to parse GetRemoteMemResp json, exception:%s", e.what());
    return hixl::PARAM_INVALID;
  } catch (...) {
    HIXL_LOGE(hixl::FAILED, "[HixlClient] Unknown error during json parse");
    return hixl::FAILED;
  }

  const nlohmann::json *arr = nullptr;
  hixl::Status ret = ParseResultAndGetArray(j, arr);
  HIXL_CHK_STATUS_RET(ret);

  return ParseMemDescsArray(*arr, mem_descs);
}
}  // namespace

namespace hixl {

Status MemMsgHandler::SendGetRemoteMemRequest(int32_t socket, uint64_t endpoint_handle, uint32_t timeout_ms) {
  HIXL_EVENT("[HixlClient] SendGetRemoteMemRequest start. socket: %d, endpoint_handle: %lu", socket, endpoint_handle);
  (void)timeout_ms;
  CtrlMsgHeader header{};
  header.magic = kMagicNumber;
  header.body_size = static_cast<uint64_t>(sizeof(CtrlMsgType) + sizeof(GetRemoteMemReq));

  CtrlMsgType msg_type = CtrlMsgType::kGetRemoteMemReq;

  GetRemoteMemReq body{};
  body.dst_ep_handle = endpoint_handle;

  HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Send(socket, &header, static_cast<uint64_t>(sizeof(header))));
  HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Send(socket, &msg_type, static_cast<uint64_t>(sizeof(msg_type))));
  HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Send(socket, &body, static_cast<uint64_t>(sizeof(body))));
  HIXL_EVENT("[HixlClient] SendGetRemoteMemRequest success. fd=%d", socket);
  return SUCCESS;
}

Status MemMsgHandler::RecvGetRemoteMemResponse(int32_t socket, std::vector<HixlMemDesc> &mem_descs,
                                               uint32_t timeout_ms) {
  HIXL_EVENT("[HixlClient] RecvGetRemoteMemResponse start. socket: %d, timeout_ms: %u", socket, timeout_ms);
  uint64_t body_size = 0;
  HIXL_CHK_STATUS_RET(RecvAndCheckHeader(socket, body_size, timeout_ms));
  std::vector<uint8_t> body;
  HIXL_CHK_STATUS_RET(RecvBody(socket, body_size, body, timeout_ms));
  CtrlMsgType msg_type{};
  const char *json_ptr = nullptr;
  size_t json_len = 0;
  HIXL_CHK_STATUS_RET(ExtractTypeAndJsonPtr(body, msg_type, json_ptr, json_len));
  Status ret = ParseGetRemoteMemJson(json_ptr, json_len, mem_descs);
  if (ret == SUCCESS) {
    HIXL_EVENT("[HixlClient] RecvGetRemoteMemResponse success. Parsed %zu mem descriptors.", mem_descs.size());
  } else {
    HIXL_LOGE(ret, "[HixlClient] RecvGetRemoteMemResponse failed during parsing.");
  }
  return ret;
}
}  // namespace hixl
