/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "llm_utils.h"
#include <iostream>
#include <regex>
#include <chrono>
#include "mmpa/mmpa_api.h"
#include "llm_datadist/llm_engine_types.h"
#include "common/llm_inner_types.h"
#include "common/llm_checker.h"
#include "common/llm_string_util.h"
#include "common/llm_log.h"
#include "llm_datadist/llm_datadist.h"

namespace llm {
namespace {
constexpr const char kDisableFlag[] = "0";
constexpr const char kEnableFlag[] = "1";

ge::Status IsPositiveInteger(const std::string &input_string) {
  // 输入 字符串需要是大于0的整数
  // 检查字符串是否为空
  LLM_ASSERT_TRUE(!input_string.empty(), "Input string is empty.");
  // 检查字符串是否包含非数字字符
  for (const char &c : input_string) {
    LLM_ASSERT_TRUE(std::isdigit(c) != 0, "Input string contains non-numeric characters.");
  }
  return ge::SUCCESS;
}

ge::Status CheckBlocksContinuous(const std::vector<std::pair<int64_t, int64_t>> &current_group) {
  const bool is_contiguous =
      ((current_group.back().first - current_group.front().first) == static_cast<int64_t>(current_group.size() - 1U)) &&
      ((current_group.back().second - current_group.front().second) == static_cast<int64_t>(current_group.size() - 1U));
  LLM_CHK_BOOL_RET_STATUS(is_contiguous, ge::FAILED,
                         "aggregate contiguous block failed, src index[%ld-%ld], dst index[%ld-%ld], group size:%zu",
                         current_group.front().first, current_group.back().first, current_group.front().second,
                         current_group.back().second, current_group.size());
  return ge::SUCCESS;
}
}  // namespace

ge::Status LLMUtils::ParserWaitTimeInfo(const std::map<ge::AscendString, ge::AscendString> &options,
                                        DecoderWaitTimeInfo &wait_time_info) {
  const auto &sync_kv_wait_time_iter = options.find(LLM_OPTION_SYNC_KV_CACHE_WAIT_TIME);
  if (sync_kv_wait_time_iter != options.cend()) {
    LLMLOGI("get sync kv wait time:%s ms.", sync_kv_wait_time_iter->second.GetString());
    LLM_CHK_BOOL_RET_STATUS(IsPositiveInteger(sync_kv_wait_time_iter->second.GetString()) == ge::SUCCESS,
                           ge::LLM_PARAM_INVALID,
                           "sync kv wait time:%s is invalid, wait time value should be a positive integer.",
                           sync_kv_wait_time_iter->second.GetString());
    wait_time_info.sync_kv_wait_time = std::atoi(sync_kv_wait_time_iter->second.GetString());
  }

  return ge::SUCCESS;
}

ge::Status LLMUtils::ParseFlag(const std::string &option_name,
                               const std::map<ge::AscendString, ge::AscendString> &options,
                               bool &enabled) {
  enabled = false;
  const auto iter = options.find(option_name.c_str());
  if (iter != options.cend()) {
    const std::string &value = iter->second.GetString();
    LLM_ASSERT_TRUE((value == kEnableFlag) || (value == kDisableFlag),
                   "Option %s value (\"%s\") is invalid, should be \"0\" or \"1\"",
                   option_name.c_str(), value.c_str());
    LLMLOGI("Option %s = %s", option_name.c_str(), iter->second.GetString());
    enabled = (iter->second == kEnableFlag);
    return ge::SUCCESS;
  }
  LLMLOGI("Option %s not set", option_name.c_str());
  return ge::SUCCESS;
}

ge::Status LLMUtils::ParseDeviceId(const std::map<ge::AscendString, ge::AscendString> &options, int32_t &device_id) {
  const auto it = options.find(ge::OPTION_EXEC_DEVICE_ID);
  if (it != options.cend()) {
    LLM_CHK_STATUS_RET(LLMUtils::ToNumber(it->second.GetString(), device_id), "%s is invalid, value = %s",
                      ge::OPTION_EXEC_DEVICE_ID, it->second.GetString());
  }
  LLMLOGI("Option %s is:%d", ge::OPTION_EXEC_DEVICE_ID, device_id);
  return ge::SUCCESS;
}

ge::Status LLMUtils::FindContiguousBlockIndexPair(const std::vector<std::pair<int64_t, int64_t>> &block_mapping,
                                                  std::vector<std::vector<std::pair<int64_t, int64_t>>> &result) {
  const auto start = std::chrono::steady_clock::now();
  if (block_mapping.empty()) {
    return ge::SUCCESS;
  }
  std::vector<std::pair<int64_t, int64_t>> current_group;
  auto iter = block_mapping.begin();
  current_group.push_back(*iter);
  int64_t prev_key = iter->first;
  int64_t prev_value = iter->second;
  ++iter;

  while (iter != block_mapping.end()) {
    if ((iter->first == prev_key + 1) && (iter->second == prev_value + 1)) {
      current_group.push_back(*iter);
    } else {
      LLM_CHK_STATUS_RET(CheckBlocksContinuous(current_group), "check blocks continuous failed");
      result.push_back(current_group);
      current_group.clear();
      current_group.push_back(*iter);
    }
    prev_key = iter->first;
    prev_value = iter->second;
    ++iter;
  }
  if (!current_group.empty()) {
    LLM_CHK_STATUS_RET(CheckBlocksContinuous(current_group), "check blocks continuous failed");
    result.push_back(current_group);
  }
  const auto end = std::chrono::steady_clock::now();
  LLMLOGI("[LlmPerf] find contiguous block index pair cost time:%zu us",
         std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
  return ge::SUCCESS;
}

ge::Status LLMUtils::FindContiguousBlockIndexPair(const std::vector<uint64_t> &src_blocks,
                                                  const std::vector<uint64_t> &dst_blocks,
                                                  std::vector<std::vector<std::pair<int64_t, int64_t>>> &result) {
  LLM_CHK_BOOL_RET_STATUS(src_blocks.size() == dst_blocks.size(), ge::LLM_PARAM_INVALID,
                         "src_block num:%zu not match dst_block num:%zu", src_blocks.size(), dst_blocks.size());
  std::vector<std::pair<int64_t, int64_t>> block_mapping;
  for (size_t i = 0UL; i < src_blocks.size(); ++i) {
    block_mapping.emplace_back(std::make_pair(src_blocks[i], dst_blocks[i]));
  }
  LLM_CHK_STATUS_RET(LLMUtils::FindContiguousBlockIndexPair(block_mapping, result));
  return ge::SUCCESS;
}

ge::Status LLMUtils::IpToInt(const std::string &ip, uint32_t &ip_int) {
  constexpr uint32_t kNumBits = 8U;
  struct in_addr addr;
  LLM_CHK_BOOL_RET_STATUS(inet_pton(AF_INET, ip.c_str(), &addr) == 1, ge::LLM_PARAM_INVALID,
                         "%s is not a valid ip address", ip.c_str());
  const auto items = LLMUtils::Split(ip, '.');
  ip_int = 0;
  uint32_t shift = 0;
  for (const auto &item : items) {
    ip_int = ip_int | static_cast<uint8_t>(std::stoi(item)) << shift;
    shift += kNumBits;
  }
  return ge::SUCCESS;
}

ge::Status LLMUtils::CheckIp(const std::string &ip) {
  struct in_addr addr;
  struct sockaddr_in6 ipv6_addr;
  LLM_CHK_BOOL_RET_STATUS(inet_pton(AF_INET, ip.c_str(), &addr) == 1 ||
                          inet_pton(AF_INET6, ip.c_str(), &ipv6_addr.sin6_addr) == 1, ge::LLM_PARAM_INVALID,
                         "%s is not a valid ip address", ip.c_str());
  return ge::SUCCESS;
}

ge::Status LLMUtils::IntToIp(uint32_t ip_int, std::string &ip_str) {
  constexpr uint32_t kNumBits = 8U;
  constexpr uint32_t kNumBytes = 4U;
  constexpr uint32_t kByteMask = 0xFFU;
  ip_str.clear();
  for (uint32_t i = 0; i < kNumBytes; ++i) {
    uint8_t byte = (ip_int >> (i * kNumBits)) & kByteMask;
    ip_str += std::to_string(byte);
    if (i < kNumBytes - 1) {
      ip_str += ".";
    }
  }
  return ge::SUCCESS;
}

ge::Status LLMUtils::ParseDeviceId(const std::map<ge::AscendString, ge::AscendString> &options,
                                   std::vector<int32_t> &device_ids, const char *option) {
  const auto it = options.find(option);
  if (it == options.end()) {
    LLMLOGW("%s is not set, default value is 0.", option);
    return ge::SUCCESS;
  }
  LLM_CHK_BOOL_RET_STATUS(it->second.GetLength() > 0UL, ge::LLM_PARAM_INVALID, "option %s can not be empty.", option);
  const auto items = LLMUtils::Split(it->second.GetString(), ';');
  LLM_CHK_BOOL_RET_STATUS(!items.empty(), ge::LLM_PARAM_INVALID, "option %s can not be empty.", option);
  for (const auto &item : items) {
    int32_t device_id = -1;
    LLM_CHK_STATUS_RET(llm::LLMUtils::ToNumber(item, device_id),
                      "%s:%s is invalid, it should be composed of numbers, separated by semicolons.", option,
                      it->second.GetString());
    LLM_CHK_BOOL_RET_STATUS(device_id >= 0, ge::LLM_PARAM_INVALID, "Invalid device_id: %s", it->second.GetString());
    LLMLOGI("Parse device success, device_id = %d", device_id);
    device_ids.emplace_back(device_id);
  }
  return ge::SUCCESS;
}


ge::Status LLMUtils::ParseListenIpInfo(const std::map<ge::AscendString, ge::AscendString> &options,
                                       std::string &ip,
                                       uint32_t &port) {
  auto it = options.find(llm_datadist::OPTION_LISTEN_IP_INFO);
  LLM_CHK_BOOL_RET_STATUS(it != options.cend(), ge::LLM_PARAM_INVALID, "option llm.ListenIpInfo not set");
  LLMLOGI("Option %s = %s", llm_datadist::OPTION_LISTEN_IP_INFO, it->second.GetString());
  std::string option_str(it->second.GetString());
  LLM_CHK_STATUS_RET(ParseListenIpInfo(option_str, ip, port));
  return ge::SUCCESS;
}

ge::Status LLMUtils::ParseListenIpInfo(const std::string &option, std::string &ip, uint32_t &port) {
  std::vector<std::string> ip_and_port;
  size_t left = option.find('[');
  size_t right = option.find(']');
  if (left != std::string::npos && right != std::string::npos && left < right) {
    ip_and_port.emplace_back(option.substr(left + 1, right - left - 1));
    size_t colon = option.find(':', right);
    if (colon != std::string::npos) {
      ip_and_port.emplace_back(option.substr(colon + 1));
    }
  } else {
    ip_and_port = LLMUtils::Split(option, ':');
  }
  constexpr size_t kValidItemNum = 2U;
  LLM_CHK_BOOL_RET_STATUS(ip_and_port.size() == kValidItemNum, ge::LLM_PARAM_INVALID,
                          "llm.ListenIpInfo is invalid: %s, expect ${ip}:${port}", option.c_str());
  LLM_CHK_STATUS_RET(CheckIp(ip_and_port.front()), "IP is invalid: %s, option_val = %s", ip_and_port[0].c_str(),
                    option.c_str());
  ip = ip_and_port.front();
  int64_t port_val = -1;
  LLM_CHK_STATUS_RET(ToNumber(ip_and_port.back(), port_val), "port is invalid: %s, option_val = %s",
                    ip_and_port[1].c_str(), option.c_str());
  LLM_CHK_BOOL_RET_STATUS((port_val >= 0) && (port_val <= UINT32_MAX), ge::LLM_PARAM_INVALID,
                         "port is invalid: %s, option_val = %s", ip_and_port[1].c_str(), option.c_str());
  port = static_cast<uint32_t>(port_val);
  return ge::SUCCESS;
}

bool LLMUtils::CheckMultiplyOverflowInt64(int64_t a, int64_t b) {
  if (a > 0) {
    if (b > 0) {
      if (a > (std::numeric_limits<int64_t>::max() / b)) {
        return true;
      }
    } else {
      if (b < (std::numeric_limits<int64_t>::min() / a)) {
        return true;
      }
    }
  } else {
    if (b > 0) {
      if (a < (std::numeric_limits<int64_t>::min() / b)) {
        return true;
      }
    } else {
      if ((a != 0) && (b < (std::numeric_limits<int64_t>::max() / a))) {
        return true;
      }
    }
  }
  return false;
}

int32_t LLMUtils::CeilDiv(int32_t a, int32_t b) {
  int32_t res = a / b;
  return (res * b == a) ? res : (res + 1);
}

ge::Status LLMUtils::CalcElementCntByDims(const std::vector<int64_t> &dims, int64_t &element_cnt) {
  element_cnt = 1;
  for (const int64_t dim : dims) {
    LLM_CHK_BOOL_RET_STATUS(dim > 0, ge::LLM_PARAM_INVALID,
                           "[Check][Dim] CalcElementCntByDims failed, dim value:%ld must > 0", dim);
    LLM_CHK_BOOL_RET_STATUS(!CheckMultiplyOverflowInt64(element_cnt, dim), ge::LLM_PARAM_INVALID,
                           "[Check][Overflow] CalcElementCntByDims failed, "
                           "when multiplying %" PRId64 " and %" PRId64 ".",
                           element_cnt, dim);
    element_cnt *= dim;
  }
  return ge::SUCCESS;
}

bool LLMUtils::GetDataTypeLength(const ge::DataType data_type, uint32_t &length) {
  static const std::map<ge::DataType, uint32_t> kDataTypeToLength = {
      {ge::DT_STRING_REF, sizeof(uint64_t) * 2U},
      {ge::DT_STRING, sizeof(uint64_t) * 2U},
  };
  const auto it = kDataTypeToLength.find(data_type);
  if (it != kDataTypeToLength.end()) {
    length = it->second;
    return true;
  }

  const int32_t size = GetSizeByDataType(data_type);
  if (size > 0) {
    length = static_cast<uint32_t>(size);
    return true;
  }
  LLMLOGE(ge::LLM_PARAM_INVALID, "[Check][Param] data_type not support [%d]",
         static_cast<int32_t>(data_type));
  return false;
}

ge::Status LLMUtils::GetSizeInBytes(int64_t element_count, ge::DataType data_type, int64_t &mem_size) {
  LLM_CHK_BOOL_RET_STATUS(element_count >= 0, ge::LLM_PARAM_INVALID,
                         "GetSizeInBytes failed, element_count:%" PRId64 " less than 0.", element_count);
  uint32_t type_size = 0U;
  LLM_CHK_BOOL_RET_STATUS(GetDataTypeLength(data_type, type_size), ge::LLM_PARAM_INVALID,
                         "Failed to get type length, data_type:%d not support.", data_type);
  if (type_size > ge::kDataTypeSizeBitOffset) {
    const auto bit_size = type_size - ge::kDataTypeSizeBitOffset;
    LLM_CHK_BOOL_RET_STATUS(!CheckMultiplyOverflowInt64(element_count, static_cast<int64_t>(bit_size)),
                           ge::LLM_PARAM_INVALID,
                           "Multiply overflow, when multiplying %" PRId64 " and %u.",
                           element_count, bit_size);
    mem_size = CeilDiv(element_count * bit_size, ge::kBitNumOfOneByte);
  } else {
    LLM_CHK_BOOL_RET_STATUS(!CheckMultiplyOverflowInt64(element_count, static_cast<int64_t>(type_size)),
                           ge::LLM_PARAM_INVALID,
                           "Multiply overflow, when multiplying %" PRId64 " and %u.",
                           element_count, type_size);
    mem_size = element_count * type_size;
  }
  return ge::SUCCESS;
}

ge::Status LLMUtils::CalcTensorMemSize(const std::vector<int64_t> &dims,
                                       const ge::DataType data_type,
                                       int64_t &mem_size) {
  int64_t element_cnt = 0;
  LLM_CHK_STATUS_RET(CalcElementCntByDims(dims, element_cnt), "Failed to calc element cnt.");
  LLM_CHK_STATUS_RET(GetSizeInBytes(element_cnt, data_type, mem_size), "Failed to get size in byte.");
  return ge::SUCCESS;
}

std::vector<std::string, std::allocator<std::string>> LLMUtils::Split(const std::string &str, const char_t delim) {
  std::vector<std::string, std::allocator<std::string>> elems;
  if (str.empty()) {
    (void) elems.emplace_back("");
    return elems;
  }

  std::stringstream ss(str);
  std::string item;
  while (getline(ss, item, delim)) {
    (void) elems.push_back(item);
  }

  const auto str_size = str.size();
  if ((str_size > 0U) && (str[str_size - 1U] == delim)) {
    (void) elems.emplace_back("");
  }
  return elems;
}

bool LLMUtils::IsTimeout(const std::chrono::high_resolution_clock::time_point& start_time, int32_t timeout_ms) {
  auto now = std::chrono::high_resolution_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time);
  return (elapsed.count() >= timeout_ms);
}

TemporaryRtContext::TemporaryRtContext(aclrtContext context) {
  (void) aclrtGetCurrentContext(&prev_context_);
  LLMLOGI("Get current aclrt ctx:%p", prev_context_);
  if (context != nullptr && prev_context_ != context) {
    LLM_CHK_ACL(aclrtSetCurrentContext(context));
    LLMLOGI("Set current aclrt ctx:%p", prev_context_);
  }
}

TemporaryRtContext::~TemporaryRtContext() {
  if (prev_context_ != nullptr) {
    LLM_CHK_STATUS(aclrtSetCurrentContext(prev_context_));
    LLMLOGI("Restore current aclrt ctx:%p", prev_context_);
  }
}
}  // namespace llm
